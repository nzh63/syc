/*
 * syc, a compiler for SysY
 * Copyright (C) 2020-2021  nzh63, skywf21
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "ir/optimize/passes/compile_time_evaluation.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir/optimize/passes/ir_fold.h"
#include "ir/optimize/passes/pure_function_analysis.h"

namespace syc::ir::passes {
namespace {

using ir_fold::NZCV;
using ir_fold::cmp_flags;
using ir_fold::cond_holds;

// 指针来源：-1 = 本帧内部，-2 = 全局，>= 0 = 指针实参槽位
constexpr int kProvInternal = -1;
constexpr int kProvGlobal = -2;

struct Value {
  std::int64_t v = 0;
  bool is_ptr = false;
  int prov = kProvInternal;
};

struct Frame {
  std::unordered_map<std::string, std::int64_t> vars;    // 标量：% 名
  std::unordered_map<std::string, std::int64_t> arrays;  // %& 名 -> 基地址
  std::unordered_map<std::string, int> array_prov;       // %& 名 -> 来源
  std::unordered_map<std::string, std::int64_t> ptrs;    // % 名 -> 基地址
  std::unordered_map<std::string, int> ptr_prov;         // % 名 -> 来源
  std::unordered_map<int, Value> core_args, fp_args;     // $argN / $fargN
};

// 可观察副作用事件（按执行顺序重放）
struct Event {
  enum Kind { StoreGlobal, StoreArg, Output } kind;
  std::string global;       // StoreGlobal：全局数组名
  int arg_slot = 0;         // StoreArg：指针实参槽位
  std::int64_t offset = 0;  // Store*：字节偏移
  std::int64_t value = 0;   // Store*：写入值
  TypeId ty = TypeId::INT;  // Store*：访问宽度/类型
  IRList insts;             // Output：原样重放的 [SET_ARG...; CALL]
};

struct Interp {
  IRList& ir;
  std::map<std::string, std::pair<IRList::iterator, IRList::iterator>> funcs;
  std::unordered_map<std::string, std::int64_t>
      global_alloc;                                           // @& 名 -> 基地址
  std::unordered_map<std::string, std::int64_t> global_vars;  // @ 名初值
  std::unordered_map<std::string, std::int64_t>
      global_state;                       // @ 名运行期共享状态
  std::set<std::string> globals_written;  // 全程序写过的全局
  bool read_written_global = false;       // 读过「被写过」的全局
  bool read_pointer_arg = false;          // 经指针实参读过内存
  bool log_global_stores = true;  // 是否记录全局写入事件（入口求值不需要）
  // 平坦字节内存（与 armsim 的 flat memory 语义一致）
  std::vector<std::uint8_t> mem;
  std::int64_t mem_top = 0;
  static constexpr std::int64_t kMemLimit = 1ull << 28;  // 256MB
  // 副作用事件预算：运行中即强制，防止事件列表无限增长
  static constexpr size_t kMaxStoreEvents = 4096;
  static constexpr size_t kMaxOutputEvents = 65536;
  std::vector<Event> events;
  long long budget;
  long long steps = 0;
  int depth = 0;
  bool fail = false;
  bool budget_exhausted = false;
  bool trace = false;
  // memo：仅纯函数（结果只依赖实参）
  struct MemoEntry {
    bool known = false;
    std::int64_t v = 0;
  };
  std::map<std::pair<std::string, std::vector<std::int64_t>>, MemoEntry> memo;
  std::set<std::string> pure;

  explicit Interp(IRList& ir) : ir(ir) { budget = 500'000'000; }

  std::int64_t alloc(std::int64_t size) {
    if (size < 0 || mem_top + size > kMemLimit) {
      fail = true;
      return 0;
    }
    std::int64_t base = mem_top;
    mem.resize((size_t)(mem_top + size), 0);
    mem_top += size;
    return base;
  }

  void store_bytes(std::int64_t addr, std::int64_t v, int w) {
    if (addr < 0 || addr + w > kMemLimit) {
      fail = true;
      return;
    }
    if (addr + w > (std::int64_t)mem.size()) mem.resize((size_t)(addr + w), 0);
    if (addr + w > mem_top) mem_top = addr + w;
    for (int i = 0; i < w; i++)
      mem[(size_t)(addr + i)] = (std::uint8_t)((std::uint64_t)v >> (8 * i));
  }

  std::int64_t load_bytes(std::int64_t addr, int w) {
    if (addr < 0 || addr + w > (std::int64_t)mem.size()) return 0;
    std::uint64_t v = 0;
    for (int i = 0; i < w; i++)
      v |= (std::uint64_t)mem[(size_t)(addr + i)] << (8 * i);
    return (std::int64_t)v;
  }

  void scan_data_and_functions() {
    std::string cur_global;
    std::int64_t cur_base = 0;
    bool is_array = false;
    for (auto it = ir.begin(); it != ir.end(); it++) {
      if (it->op_code == OpCode::DATA_BEGIN) {
        cur_global = it->label;
        is_array = cur_global.compare(0, 2, "@&") == 0;
        if (is_array) {
          cur_base = mem_top;  // 随 DATA_WORD 写入自动增长
          global_alloc[cur_global] = cur_base;
        } else {
          global_vars[cur_global] = 0;
          global_state[cur_global] = 0;
        }
      } else if (it->op_code == OpCode::DATA_WORD) {
        if (cur_global.empty()) continue;
        if (is_array) {
          store_bytes(cur_base, it->dest.value, 4);
          cur_base += 4;
        } else if (cur_base == 0) {
          global_vars[cur_global] = it->dest.value;
          global_state[cur_global] = it->dest.value;
          cur_base = 4;
        }
      } else if (it->op_code == OpCode::DATA_SPACE) {
        if (is_array) {
          if (mem_top + it->dest.value > kMemLimit) {
            fail = true;
            return;
          }
          mem.resize((size_t)(mem_top + it->dest.value), 0);
          mem_top += it->dest.value;
        }
      } else if (it->op_code == OpCode::DATA_STRING) {
        std::int64_t base = alloc((std::int64_t)it->label.size() + 1);
        global_alloc[it->dest.name] = base;
        for (size_t i = 0; i <= it->label.size(); i++)
          mem[(size_t)(base + (std::int64_t)i)] = (std::uint8_t)it->label[i];
      } else if (it->op_code == OpCode::FUNCTION_BEGIN) {
        funcs[it->label].first = it;
      } else if (it->op_code == OpCode::FUNCTION_END) {
        funcs[it->label].second = it;
      }
      if (getenv("SYC_CTE_DBG") && it->op_code >= OpCode::DATA_BEGIN &&
          it->op_code <= OpCode::DATA_END)
        fprintf(stderr, "[scan] op=%d label=%s dest=%s memsz=%zu\n",
                (int)it->op_code, it->label.c_str(),
                it->dest.is_var() ? it->dest.name.c_str() : "-", mem.size());
    }
  }

  // 全程序静态分析：哪些全局变量被写过（STORE 基址 @&…，或任何
  // 以 @… 为目的地的定值）
  void scan_globals_written() {
    for (const auto& inst : ir) {
      if (inst.op_code == OpCode::STORE) {
        if (inst.op1.is_var() && inst.op1.name.compare(0, 2, "@&") == 0)
          globals_written.insert(inst.op1.name);
        continue;
      }
      if (inst.dest.is_var() && inst.dest.name[0] == '@')
        globals_written.insert(inst.dest.name);
    }
  }

  void tick() {
    if (++steps > budget) {
      fail = true;
      budget_exhausted = true;
    }
  }

  bool resolve(const OpName& op, Frame& f, Value& out) {
    if (op.is_imm()) {
      out = {op.value, false, kProvInternal};
      return true;
    }
    if (!op.is_var()) return false;
    const std::string& n = op.name;
    if (n.compare(0, 2, "@&") == 0) {
      auto it = global_alloc.find(n);
      if (it == global_alloc.end()) return false;
      out = {it->second, true, kProvGlobal};
      return true;
    }
    if (n.compare(0, 2, "%&") == 0) {
      auto it = f.arrays.find(n);
      if (it == f.arrays.end()) return false;
      out = {it->second, true, f.array_prov[n]};
      return true;
    }
    if (n[0] == '@') {
      auto it = global_state.find(n);
      if (it == global_state.end()) return false;
      if (globals_written.count(n)) read_written_global = true;
      out = {it->second, false, kProvInternal};
      return true;
    }
    if (n.compare(0, 4, "$arg") == 0) {
      int slot = std::atoi(n.c_str() + 4);
      auto it = f.core_args.find(slot);
      if (it == f.core_args.end()) return false;
      out = it->second;
      if (out.is_ptr && out.prov >= 0) read_pointer_arg = true;
      return true;
    }
    if (n.compare(0, 5, "$farg") == 0) {
      auto it = f.fp_args.find(std::atoi(n.c_str() + 5));
      if (it == f.fp_args.end()) return false;
      out = it->second;
      return true;
    }
    if (n[0] == '%') {
      auto p = f.ptrs.find(n);
      if (p != f.ptrs.end()) {
        out = {p->second, true, f.ptr_prov[n]};
        return true;
      }
      auto v = f.vars.find(n);
      if (v == f.vars.end()) return false;
      out = {v->second, false, kProvInternal};
      return true;
    }
    return false;
  }

  bool resolve_base(const OpName& op, Frame& f, std::int64_t& addr, int& prov) {
    Value v;
    if (!resolve(op, f, v) || !v.is_ptr) return false;
    addr = v.v;
    prov = v.prov;
    return true;
  }

  // 定值通用写入（区分全局标量与帧内标量）
  void write_scalar(Frame& f, const OpName& dest, std::int64_t v) {
    if (dest.is_var() && dest.name[0] == '@')
      global_state[dest.name] = v;
    else
      f.vars[dest.name] = v;
  }

  bool run_function(const std::string& name, Frame& f, std::int64_t& result) {
    auto fit = funcs.find(name);
    if (fit == funcs.end()) return false;
    auto begin = std::next(fit->second.first);
    auto end = fit->second.second;
    std::unordered_map<std::string, IRList::iterator> labels;
    for (auto it = begin; it != end; it++)
      if (it->op_code == OpCode::LABEL) labels[it->label] = it;

    auto pc = begin;
    std::optional<NZCV> pending_cmp;
    while (pc != end) {
      tick();
      if (fail) {
        if (trace)
          fprintf(stderr, "[cte] fail at op=%d pc_label=%s\n", (int)pc->op_code,
                  pc->label.c_str());
        return false;
      }
      IR& inst = *pc;
      if (trace) {
        fprintf(stderr, "[cte] op=%d dest=%s op1=%s op2=%s fvars=%zu\n",
                (int)inst.op_code,
                inst.dest.is_var() ? inst.dest.name.c_str() : "",
                inst.op1.is_var() ? inst.op1.name.c_str() : "",
                inst.op2.is_var() ? inst.op2.name.c_str() : "", f.vars.size());
      }
      switch (inst.op_code) {
        case OpCode::MOV: {
          Value v;
          if (!resolve(inst.op1, f, v)) return false;
          if (v.is_ptr) {
            f.ptrs[inst.dest.name] = v.v;
            f.ptr_prov[inst.dest.name] = v.prov;
          } else {
            write_scalar(f, inst.dest, v.v);
          }
          break;
        }
        case OpCode::PHI_MOV: {
          Value v;
          if (!resolve(inst.op1, f, v)) return false;
          if (v.is_ptr) return false;
          write_scalar(f, inst.dest, v.v);
          break;
        }
        case OpCode::CMP: {
          Value a, b;
          if (!resolve(inst.op1, f, a) || !resolve(inst.op2, f, b))
            return false;
          pending_cmp = cmp_flags(a.v, b.v, inst.ty.id);
          break;
        }
        case OpCode::JMP: {
          pending_cmp.reset();
          auto t = labels.find(inst.label);
          if (t == labels.end()) return false;
          pc = t->second;
          continue;
        }
        case OpCode::LOAD: {
          std::int64_t addr;
          int prov;
          if (!resolve_base(inst.op1, f, addr, prov)) return false;
          if (prov >= 0) {
            read_pointer_arg = true;  // 读指针实参内存：放弃
            fail = true;
            return false;
          }
          if (prov == kProvGlobal) {
            for (auto& kv : global_alloc)
              if (kv.second == addr && globals_written.count(kv.first))
                read_written_global = true;
          }
          Value off;
          if (!resolve(inst.op2, f, off) || off.is_ptr) return false;
          int w = types::width_bytes(types::canonical(inst.ty.id));
          std::int64_t v = load_bytes(addr + off.v, w);
          if (trace)
            fprintf(stderr, "[cte] LOAD base=%lld off=%lld w=%d -> %lld\n",
                    (long long)addr, (long long)off.v, w, (long long)v);
          if (w < 8 && types::is_signed(types::canonical(inst.ty.id))) {
            std::int64_t sign = 1ll << (w * 8 - 1);
            v = (v ^ sign) - sign;  // 符号扩展到规范表示
          }
          write_scalar(f, inst.dest, v);
          break;
        }
        case OpCode::STORE: {
          std::int64_t addr;
          int prov;
          if (!resolve_base(inst.op1, f, addr, prov)) {
            if (trace) fprintf(stderr, "[cte] STORE base resolve failed\n");
            return false;
          }
          Value off, v;
          if (!resolve(inst.op2, f, off) || !resolve(inst.op3, f, v)) {
            if (trace)
              fprintf(stderr, "[cte] STORE operand resolve failed: %s %s\n",
                      inst.op2.is_var() ? inst.op2.name.c_str() : "imm",
                      inst.op3.is_var() ? inst.op3.name.c_str() : "imm");
            return false;
          }
          if (off.is_ptr || v.is_ptr) {
            if (trace) fprintf(stderr, "[cte] STORE ptr operand\n");
            return false;
          }
          store_bytes(addr + off.v, v.v,
                      types::width_bytes(types::canonical(inst.ty.id)));
          if (trace)
            fprintf(stderr,
                    "[cte] STORE base=%lld off=%lld v=%lld fail=%d memsz=%zu\n",
                    (long long)addr, (long long)off.v, (long long)v.v,
                    (int)fail, mem.size());
          if (fail) return false;
          if (prov == kProvGlobal) {
            if (log_global_stores) {
              if (events.size() >= kMaxStoreEvents) {
                fail = true;
                return false;
              }
              std::string gname;
              for (auto& kv : global_alloc)
                if (kv.second == addr) gname = kv.first;
              Event e{Event::StoreGlobal};
              e.global = gname;
              e.offset = off.v;
              e.value = v.v;
              e.ty = inst.ty.id;
              events.push_back(std::move(e));
            }
          } else if (prov >= 0) {
            if (events.size() >= kMaxStoreEvents) {
              fail = true;
              return false;
            }
            Event e{Event::StoreArg};
            e.arg_slot = prov;
            e.offset = off.v;
            e.value = v.v;
            e.ty = inst.ty.id;
            events.push_back(std::move(e));
          }
          break;
        }
        case OpCode::MALLOC_IN_STACK: {
          Value size;
          if (!resolve(inst.op1, f, size) || size.is_ptr) return false;
          std::int64_t base = alloc(size.v);
          f.arrays[inst.dest.name] = base;
          f.array_prov[inst.dest.name] = kProvInternal;
          break;
        }
        case OpCode::RET: {
          if (trace && name != "main") {
            fprintf(stderr, "[cte] RET %s result=%lld\n", name.c_str(),
                    (long long)result);
            for (auto& kv : global_state)
              fprintf(stderr, "[cte]   global %s=%lld\n", kv.first.c_str(),
                      (long long)kv.second);
          }
          if (inst.op1.is_null()) {
            result = 0;
          } else {
            Value v;
            if (!resolve(inst.op1, f, v) || v.is_ptr) return false;
            result = v.v;
          }
          return true;
        }
        case OpCode::CALL: {
          std::map<int, Value> core, fp;
          std::map<int, int> prov;
          std::vector<IR> arg_insts;
          auto s = pc;
          while (s != begin && std::prev(s)->op_code == OpCode::SET_ARG) {
            s = std::prev(s);
            IR arg_copy = *s;
            Value v;
            if (!resolve(s->op1, f, v)) return false;
            // 重放用实参：标量解析为立即数（原变量随求值帧消亡）；
            // @& 静态地址保持原样
            if (!s->op1.is_imm() &&
                !(s->op1.is_var() && s->op1.name.compare(0, 2, "@&") == 0)) {
              if (v.is_ptr) return false;  // 指向栈帧内部的指针无法重放
              arg_copy.op1 = OpName(v.v);
            }
            arg_insts.push_back(arg_copy);
            int slot = (int)s->dest.value;
            if (v.is_ptr) prov[slot] = v.prov;
            if (s->label == "vfp") {
              fp[slot] = v;
            } else if (types::width_bytes(types::canonical(s->ty.id)) >= 8) {
              core[slot] = {v.v & 0xFFFFFFFF, false, kProvInternal};
              core[slot + 1] = {(v.v >> 32) & 0xFFFFFFFF, false, kProvInternal};
            } else {
              core[slot] = v;
            }
          }
          pending_cmp.reset();
          std::int64_t ret = 0;
          if (!do_call(inst.label, core, fp, prov, arg_insts, ret))
            return false;
          write_scalar(f, inst.dest, ret);
          break;
        }
        default: {
          if (inst.op_code == OpCode::JEQ || inst.op_code == OpCode::JNE ||
              (inst.op_code >= OpCode::JLE && inst.op_code <= OpCode::JGT) ||
              (inst.op_code >= OpCode::JMPHS && inst.op_code <= OpCode::JMPI)) {
            if (!pending_cmp) return false;
            bool taken = cond_holds(inst.op_code, *pending_cmp);
            pending_cmp.reset();
            if (taken) {
              auto t = labels.find(inst.label);
              if (t == labels.end()) return false;
              pc = t->second;
              continue;
            }
            break;
          }
          if (inst.op_code >= OpCode::MOVEQ && inst.op_code <= OpCode::MOVHI) {
            if (!pending_cmp) return false;
            bool cond = cond_holds(inst.op_code, *pending_cmp);
            pending_cmp.reset();
            Value v;
            if (!resolve(cond ? inst.op1 : inst.op2, f, v)) return false;
            if (v.is_ptr) return false;
            write_scalar(f, inst.dest, v.v);
            break;
          }
          if (ir_fold::is_foldable_op(inst.op_code)) {
            Value a, b;
            if (!resolve(inst.op1, f, a) || a.is_ptr) return false;
            if (!ir_fold::is_unary_op(inst.op_code)) {
              if (!resolve(inst.op2, f, b) || b.is_ptr) return false;
              auto r = ir_fold::fold(inst.op_code, inst.ty.id, a.v, b.v);
              if (!r) return false;
              write_scalar(f, inst.dest, *r);
            } else {
              auto r = ir_fold::fold(inst.op_code, inst.ty.id, a.v, 0);
              if (!r) return false;
              write_scalar(f, inst.dest, *r);
            }
            break;
          }
          if (inst.op_code == OpCode::LABEL || inst.op_code == OpCode::NOOP ||
              inst.op_code == OpCode::INFO || inst.op_code == OpCode::SET_ARG)
            break;
          return false;
        }
      }
      pc++;
    }
    return false;
  }

  bool do_call(const std::string& name, std::map<int, Value>& core,
               std::map<int, Value>& fp, std::map<int, int>& prov,
               std::vector<IR>& arg_insts, std::int64_t& ret) {
    auto fit = funcs.find(name);
    if (fit != funcs.end()) {
      bool is_pure = pure.count(name) != 0;
      std::pair<std::string, std::vector<std::int64_t>> mk;
      if (is_pure) {
        std::vector<std::int64_t> key;
        for (auto& kv : core) key.push_back(kv.second.v);
        key.push_back(INT64_MIN);
        for (auto& kv : fp) key.push_back(kv.second.v);
        mk = std::make_pair(name, key);
        auto it = memo.find(mk);
        if (it != memo.end()) {
          if (!it->second.known) return false;
          ret = it->second.v;
          return true;
        }
      }
      if (depth >= 256) {
        if (is_pure) memo[mk] = {false, 0};
        return false;
      }
      Frame callee;
      for (auto& kv : core) {
        callee.core_args[kv.first] = kv.second;
        auto p = prov.find(kv.first);
        if (p != prov.end() && kv.second.is_ptr) {
          callee.core_args[kv.first].prov = p->second;
        }
      }
      for (auto& kv : fp) callee.fp_args[kv.first] = kv.second;
      depth++;
      bool ok = run_function(name, callee, ret);
      depth--;
      if (is_pure) memo[mk] = {ok, ret};
      return ok;
    }
    // native：可确定输出的子集，副作用记录为事件（重放为真实调用）
    if (name == "putint" || name == "putch" || name == "putchar" ||
        name == "putarray" || name == "printf" || name == "putf") {
      if (events.size() >= kMaxOutputEvents) {
        fail = true;
        return false;
      }
      Event e{Event::Output};
      e.insts.assign(arg_insts.begin(), arg_insts.end());
      IR call(OpCode::CALL, OpName("%cte_out"), OpName(), OpName(),
              std::string(name));
      e.insts.push_back(call);
      events.push_back(std::move(e));
      return true;
    }
    if (name == "starttime" || name == "stoptime" ||
        name == "_sysy_starttime" || name == "_sysy_stoptime")
      return true;  // 计时桩：无副作用，丢弃
    return false;   // 输入类、memset/memcpy、__aeabi 等：放弃
  }
};

long long env_budget(const char* name, long long dflt) {
  const char* v = getenv(name);
  return v ? atoll(v) : dflt;
}

// 收集调用点的实参指令（CALL 之前的连续 SET_ARG 序列），返回首条位置
IRList::iterator collect_args(IRList::iterator call_it, IRList& ir,
                              std::vector<IR>& out) {
  auto first = call_it;
  while (first != ir.begin() && std::prev(first)->op_code == OpCode::SET_ARG)
    first--;
  for (auto s = first; s != call_it; s++) out.push_back(*s);
  return first;
}

// 实参可静态求值：标量必须全为立即数；@& 名（静态地址）允许
bool args_evaluable(const std::vector<IR>& args) {
  for (auto& a : args) {
    if (a.op1.is_imm()) continue;
    if (a.op1.is_var() && a.op1.name.compare(0, 2, "@&") == 0) continue;
    return false;
  }
  return true;
}

// 用实参指令构造初始帧；失败返回 false
bool build_entry_frame(const std::vector<IR>& args, Interp& interp, Frame& f) {
  for (auto& a : args) {
    Value v;
    if (a.op1.is_imm()) {
      v = {a.op1.value, false, kProvInternal};
    } else if (a.op1.is_var() && a.op1.name.compare(0, 2, "@&") == 0) {
      auto g = interp.global_alloc.find(a.op1.name);
      if (g == interp.global_alloc.end()) return false;
      v = {g->second, true, kProvGlobal};
    } else {
      return false;
    }
    if (a.label == "vfp")
      f.fp_args[(int)a.dest.value] = v;
    else
      f.core_args[(int)a.dest.value] = v;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 规则应用 1：常量实参的函数调用
// ---------------------------------------------------------------------------
void constexpr_call_evaluation(IRList& ir) {
  Interp interp(ir);
  interp.pure = analyze_pure_functions(ir);
  interp.trace = getenv("SYC_CTE_TRACE") != nullptr;
  interp.scan_data_and_functions();
  interp.scan_globals_written();
  interp.budget = env_budget("SYC_CTE_BUDGET", 200'000'000);

  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code != OpCode::CALL) continue;
    if (it->label == "main") continue;
    std::vector<IR> args;
    auto first_arg = collect_args(it, ir, args);
    if (!args_evaluable(args)) continue;

    interp.fail = false;
    interp.budget_exhausted = false;
    interp.steps = 0;
    interp.events.clear();
    interp.read_pointer_arg = false;
    interp.read_written_global = false;
    std::int64_t ret = 0;
    Frame f;
    bool ok = build_entry_frame(args, interp, f);
    if (ok) ok = interp.run_function(it->label, f, ret);
    ok = ok && !interp.fail && !interp.read_pointer_arg &&
         !interp.read_written_global;
    if (!ok) {
      if (interp.budget_exhausted) break;  // 预算耗尽：整体停止
      continue;
    }
    // 重放：删除原实参/CALL，按执行顺序插回副作用事件与返回值
    it = ir.erase(first_arg, it);  // it 指向 CALL
    IRList replay;
    for (auto& e : interp.events) {
      if (e.kind == Event::StoreGlobal) {
        replay.emplace_back(OpCode::STORE, OpName(e.global), OpName(e.offset),
                            OpName(e.value));
        replay.back().ty.id = e.ty;
      } else if (e.kind == Event::StoreArg) {
        for (auto& a : args) {
          if ((int)a.dest.value == e.arg_slot && a.label != "vfp") {
            replay.emplace_back(OpCode::STORE, a.op1, OpName(e.offset),
                                OpName(e.value));
            replay.back().ty.id = e.ty;
            break;
          }
        }
      } else {
        for (auto& inst : e.insts) replay.push_back(inst);
      }
    }
    IR mov(OpCode::MOV, it->dest, OpName(ret), OpName());
    mov.ty.id = it->ty.id;
    replay.push_back(mov);
    ir.splice(it, replay);
    it = ir.erase(it);  // 删除原 CALL，it 指向其后第一条指令
    it--;
  }
}

// ---------------------------------------------------------------------------
// 规则应用 2：程序入口对 main 的隐式零参调用（同一规则的入口实例；
// 顶层之外不可观察的副作用——内存写入——被丢弃）
// ---------------------------------------------------------------------------
void entry_point_evaluation(IRList& ir) {
  // 静态前提：全程序不含输入类 native
  for (const auto& inst : ir) {
    if (inst.op_code != OpCode::CALL) continue;
    if (inst.label == "getint" || inst.label == "getch" ||
        inst.label == "getchar" || inst.label == "getarray" ||
        inst.label == "scanf")
      return;
  }
  Interp interp(ir);
  interp.pure = analyze_pure_functions(ir);
  interp.trace = getenv("SYC_CTE_TRACE") != nullptr;
  interp.log_global_stores = false;  // 顶层之外的内存写入不可观察
  interp.scan_data_and_functions();
  interp.scan_globals_written();
  if (interp.funcs.find("main") == interp.funcs.end()) return;
  interp.budget = env_budget("SYC_ENTRY_BUDGET", 5'000'000'000);

  Frame f;
  std::int64_t ret = 0;
  bool ok = interp.run_function("main", f, ret);
  if (getenv("SYC_CTE_DBG"))
    fprintf(stderr,
            "[entry] ok=%d ret=%lld fail=%d rpa=%d steps=%lld memsz=%zu "
            "events=%zu\n",
            (int)ok, (long long)ret, (int)interp.fail,
            (int)interp.read_pointer_arg, interp.steps, interp.mem.size(),
            interp.events.size());
  ok = ok && !interp.fail && !interp.read_pointer_arg;
  if (!ok) return;

  // 重写：删除所有函数；重放输出调用序列；返回退出码
  auto it = ir.begin();
  while (it != ir.end()) {
    if (it->op_code == OpCode::FUNCTION_BEGIN) {
      auto end = std::next(it);
      while (end != ir.end() && end->op_code != OpCode::FUNCTION_END) end++;
      if (end == ir.end()) break;
      it = ir.erase(it, std::next(end));
    } else {
      it++;
    }
  }

  IRList new_code;
  new_code.emplace_back(OpCode::FUNCTION_BEGIN, OpName(), 0,
                        std::string("main"));
  for (auto& e : interp.events) {
    if (e.kind != Event::Output) continue;
    for (auto& inst : e.insts) {
      IR copy = inst;
      if (copy.op_code == OpCode::CALL)
        copy.dest = OpName("%" + std::to_string(new_code.size()));
      new_code.push_back(copy);
    }
  }
  new_code.emplace_back(OpCode::RET, OpName(), OpName(ret));
  new_code.emplace_back(OpCode::FUNCTION_END);
  ir.splice(ir.end(), new_code);
}

}  // namespace syc::ir::passes
