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
#include <algorithm>
#include <bitset>
#include <cassert>
#include <cmath>
#include <string>

#include "assembly/generate/context.h"
#include "ast/node.h"
#include "config.h"
#include "ir/ir.h"

using namespace std;

namespace syc::assembly {
namespace {
// 将字符串内容转义为汇编器可识别的 C 风格字符串
std::string escape_asm_string(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\0': out += "\\0"; break;
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      default:   out += c; break;
    }
  }
  return out;
}

void write_dwarf2(ir::IR ir, ostream& out, const syc::Config& cfg) {
  if (cfg.enable_dwarf2) {
    out << ".loc 1 " << ir.line << " " << ir.column << endl;
  }
}

constexpr bitset<Context::reg_count> non_volatile_reg = 0b111111110000;
// VFP callee-saved：s16-s27（s28-s31 为草稿寄存器）
constexpr bitset<Context::freg_count> non_volatile_freg =
    0b00001111111111110000000000000000ULL;

// IR 是否为 64 位（宽度 8）运算
bool ir_wide(const ir::IR& i) {
  return syc::ast::types::width_bytes(
             syc::ast::types::canonical(i.ty.id)) >= 8;
}

// IR 是否为浮点运算（结果类型 FLOAT/DOUBLE）
bool ir_fp(const ir::IR& i) {
  return syc::ast::types::is_floating(syc::ast::types::canonical(i.ty.id));
}

void generate_function_asm(ir::IRList& irs, ir::IRList::iterator begin,
                           ir::IRList::iterator end, ostream& out,
                           ostream& log_out, const syc::Config& cfg) {
  assert(begin->op_code == ir::OpCode::FUNCTION_BEGIN);
  Context ctx(&irs, begin, log_out);

  // 标号
  for (auto it = begin; it != end; it++) {
    ctx.set_ir_timestamp(*it);
  }

  // 64 位名字收集：宽度 8 运算的 dest 与（除 SEXT/ZEXT/LOAD/STORE
  // 地址与偏移外的）操作数持有 64 位值
  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    if (!ir_wide(ir)) continue;
    if (ir.dest.is_var()) ctx.mark_wide(ir.dest.name);
    switch (ir.op_code) {
      case ir::OpCode::MOV:
      case ir::OpCode::PHI_MOV:
      case ir::OpCode::ADD:
      case ir::OpCode::SUB:
      case ir::OpCode::IMUL:
      case ir::OpCode::IDIV:
      case ir::OpCode::UDIV:
      case ir::OpCode::MOD:
      case ir::OpCode::UMOD:
      case ir::OpCode::CMP:
      case ir::OpCode::SET_ARG:
      case ir::OpCode::RET:
        if (ir.op1.is_var()) ctx.mark_wide(ir.op1.name);
        if (ir.op2.is_var()) ctx.mark_wide(ir.op2.name);
        break;
      case ir::OpCode::STORE:
        if (ir.op3.is_var()) ctx.mark_wide(ir.op3.name);
        break;
      default:
        break;  // SEXT/ZEXT 的 op1 是 32 位源；LOAD/STORE 的 op1/op2 是地址
    }
  }

  // 浮点类别收集：浮点 ty 的 dest 与（除转换源/地址外）操作数
  // 持有浮点值，由 VFP 寄存器池分配
  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    if (!ir_fp(ir)) continue;
    // F2I/F2U 的 dest 是 32 位整型（ty 记录的是源浮点宽度），不进 VFP 池
    if (ir.dest.is_var() && ir.op_code != ir::OpCode::F2I &&
        ir.op_code != ir::OpCode::F2U)
      ctx.mark_fp(ir.dest.name, ir.ty.id);
    switch (ir.op_code) {
      case ir::OpCode::MOV:
      case ir::OpCode::PHI_MOV:
      case ir::OpCode::ADD:
      case ir::OpCode::SUB:
      case ir::OpCode::IMUL:
      case ir::OpCode::FDIV:
      case ir::OpCode::CMP:
      case ir::OpCode::SET_ARG:
      case ir::OpCode::RET:
        if (ir.op1.is_var()) ctx.mark_fp(ir.op1.name, ir.ty.id);
        if (ir.op2.is_var()) ctx.mark_fp(ir.op2.name, ir.ty.id);
        break;
      case ir::OpCode::F2F:
        // F2F 的源宽度与结果相反
        if (ir.op1.is_var())
          ctx.mark_fp(ir.op1.name,
                      syc::ast::types::canonical(ir.ty.id) ==
                              syc::ast::types::TypeId::DOUBLE
                          ? syc::ast::types::TypeId::FLOAT
                          : syc::ast::types::TypeId::DOUBLE);
        break;
      case ir::OpCode::STORE:
        if (ir.op3.is_var()) ctx.mark_fp(ir.op3.name, ir.ty.id);
        break;
      default:
        break;  // SITOF/UITOF 的 op1 是整型源；F2I/F2U 的 dest 是整型
    }
  }

  for (auto it = begin; it != end; it++) {
    ctx.set_var_define_timestamp(*it);
  }

  for (auto it = end; it != begin; it--) {
    ctx.set_var_latest_use_timestamp(*it);
  }

  for (auto it = begin; it != end; it++) {
    if (it->op_code == ir::OpCode::CALL || it->op_code == ir::OpCode::IDIV ||
        it->op_code == ir::OpCode::MOD || it->op_code == ir::OpCode::UDIV ||
        it->op_code == ir::OpCode::UMOD ||
        (it->op_code == ir::OpCode::IMUL && ir_wide(*it))) {
      ctx.has_function_call = true;
      break;
    }
  }

  for (auto it = begin; it != end; it++) {
    log_out << "# " << ctx.ir_to_time[&*it] << " ";
    it->print(log_out);
  }

  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    ///////////////////////////////////// 计算栈大小 (数组分配)
    if (ir.op_code == ir::OpCode::MALLOC_IN_STACK) {
      int sz = (int)ir.op1.value;
      // 8 字节元素的栈上数组 8 对齐（double / long long）
      if (sz % 8 == 0) ctx.stack_size[2] = (ctx.stack_size[2] + 7) & ~7;
      ctx.stack_offset_map[ir.dest.name] = ctx.stack_size[2];
      ctx.stack_size[2] += sz;
    } else if (ir.op_code == ir::OpCode::SET_ARG) {
      if (ir.label == "vfp") {
        // VFP 实参溢出到栈：位于核心栈参数区之后
        if (ir.dest.value >= 16) {
          ctx.fstack_arg_words =
              std::max(ctx.fstack_arg_words, (int)ir.dest.value - 15);
        }
      } else {
        // 64 位实参额外占用一个栈槽
        int extra = ir_wide(ir) ? 1 : 0;
        ctx.stack_size[3] = std::max(
            ctx.stack_size[3], (int)((ir.dest.value + extra - 3) * 4));
      }
    }
  }
  ctx.stack_size[0] = ctx.fstack_arg_words * 4;

  // 寄存器分配
  for (const auto& i : ctx.var_define_timestamp_heap) {
    int cur_time = i.first;
    auto var_name = i.second;
    ctx.expire_old_intervals(cur_time);
    ctx.expire_old_freg_intervals(cur_time);

    if (var_name.substr(0, 6) == "$fsarg:") {
      // VFP 实参/调用销钉：占用指定 s 槽位
      int reg = stoi(var_name.substr(6));
      if (ctx.used_freg[reg]) ctx.overflow_fvar(ctx.freg_to_var[reg]);
      ctx.get_specified_freg_for(var_name, reg);
      continue;
    }
    if (var_name[0] == '%' && ctx.is_fpreg(var_name)) {
      // 浮点变量走 VFP 池；与 $fsarg 销钉冲突时避开 s0-s15
      bool fdbl = ctx.fp_class(var_name) == Context::FpClass::F64;
      bool fconflict = false;
      int latest = ctx.var_latest_use_timestamp[var_name];
      for (const auto& j : ctx.var_define_timestamp_heap) {
        if (j.first <= cur_time) continue;
        if (j.first > latest) break;
        if (j.second.substr(0, 6) == "$fsarg:") {
          fconflict = true;
          break;
        }
      }
      ctx.get_new_freg_for(var_name, fconflict ? 16 : 0);
      continue;
    }
    if (ctx.var_in_reg(var_name)) {
      // TODO: 有这种情况吗????
      continue;
    } else {
      if (var_name.substr(0, 5) == "$arg:") {
        int reg = stoi(var_name.substr(5));
        if (ctx.used_reg[reg]) {
          string cur_var = ctx.reg_to_var[reg];
          if (ctx.find_free_reg(4) != -1) {
            ctx.move_reg(cur_var, ctx.find_free_reg(4));
          } else {
            string overflow_var = ctx.select_var_to_overflow(4);
            if (ctx.var_latest_use_timestamp[cur_var] >=
                ctx.var_latest_use_timestamp[overflow_var]) {
              overflow_var = cur_var;
            }
            ctx.overflow_var(overflow_var);
            if (ctx.used_reg[reg]) {
              ctx.move_reg(cur_var, ctx.find_free_reg(4));
            }
          }
        }
        ctx.get_specified_reg_for(var_name, reg);
      } else {
        if (var_name[0] != '%') continue;
        // 与期间内固定分配寄存器冲突
        bool conflict = false;
        int latest = ctx.var_latest_use_timestamp[var_name];
        for (const auto& j : ctx.var_define_timestamp_heap) {
          if (j.first <= cur_time) continue;
          if (j.first > latest) break;
          if (j.second.substr(0, 5) == "$arg:") {
            conflict = true;
            break;
          }
        }

        if (ctx.find_free_reg(conflict ? 4 : 0) != -1) {
          ctx.get_specified_reg_for(var_name,
                                    ctx.find_free_reg(conflict ? 4 : 0));
        } else {
          string cur_max = ctx.select_var_to_overflow(conflict ? 4 : 0);
          if (ctx.var_latest_use_timestamp[var_name] <
              ctx.var_latest_use_timestamp[cur_max]) {
            ctx.get_specified_reg_for(var_name, ctx.var_to_reg[cur_max]);
          } else {
            ctx.overflow_var(var_name);
          }
        }
      }
    }
  }

  // 翻译
  bool skip_next_ir = false;  // 浮点 CMP 已融合处理相邻 Jcc/MOVcc
  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    if (skip_next_ir) {
      skip_next_ir = false;
      continue;
    }
    write_dwarf2(ir, out, cfg);
    auto& stack_size = ctx.stack_size;
    log_out << "#";
    ir.print(log_out);

    // 将某个 64 位值的第 half 半（0=低,1=高，已在 vreg 中）写到 dest 的落点
    auto store_dest_half = [&](const string& name, const string& vreg,
                               int half) {
      const string target = half == 0 ? name : Context::hi_name(name);
      if (name[0] == '@') {
        const string scratch = vreg == "r12" ? "r14" : "r12";
        out << "    MOV32 " << scratch << ", " << Context::rename(name)
            << endl;
        out << "    STR " << vreg << ", [" << scratch << ",#" << half * 4
            << "]" << endl;
      } else if (ctx.var_in_reg(target)) {
        int d = ctx.var_to_reg[target];
        if ("r" + to_string(d) != vreg)
          out << "    MOV r" << d << ", " << vreg << endl;
      } else {
        ctx.store_to_stack(vreg, ir::OpName(target), out);
      }
    };

    // 浮点操作数已占用的 VFP 寄存器名（"sN"/"dN"）
    auto fploc = [&](const ir::OpName& op) -> string {
      int r = ctx.var_to_freg[op.name];
      return ctx.fp_class(op.name) == Context::FpClass::F64
                 ? "d" + to_string(r / 2)
                 : "s" + to_string(r);
    };
    // 浮点单/双精度草稿寄存器（s28-s31 保留区）
    const string FP_A = "s30", FP_B = "s31", FP_DA = "d14", FP_DB = "d15";
    // 浮点二元运算：op1 → A（必要时），op2 → B（必要时），结果写 dest
    auto fp_binary = [&](const ir::IR& ir2, const string& opc) {
      const bool dbl = ctx.fp_class(ir2.dest.name) == Context::FpClass::F64;
      const string mn = opc + (dbl ? ".F64" : ".F32");
      const string& a = dbl ? FP_DA : FP_A;
      const string& b = dbl ? FP_DB : FP_B;
      string areg, breg;
      if (ir2.op1.is_var() && ctx.var_in_freg(ir2.op1.name)) {
        areg = fploc(ir2.op1);
      } else {
        ctx.load_freg(dbl ? 28 : 30, dbl, ir2.op1, out);
        areg = a;
      }
      if (ir2.op2.is_var() && ctx.var_in_freg(ir2.op2.name)) {
        breg = fploc(ir2.op2);
      } else {
        ctx.load_freg(dbl ? 30 : 31, dbl, ir2.op2, out);
        breg = b;
      }
      bool dest_in_freg = ir2.dest.is_var() && ctx.var_in_freg(ir2.dest.name);
      string dreg = dest_in_freg ? fploc(ir2.dest) : a;
      out << "    " << mn << " " << dreg << ", " << areg << ", " << breg
          << endl;
      if (!dest_in_freg) {
        ctx.store_freg_dest(dbl ? 28 : 30, dbl, ir2.dest.name, out);
      }
    };

    if (ir.op_code == ir::OpCode::FUNCTION_BEGIN) {
      out << ".text" << endl;
      out << ".global " << ir.label << endl;
      out << ".type	" << ir.label << ", %function" << endl;
      out << ir.label << ":" << endl;
      if (stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] > 256) {
        ctx.load("r12",
                 stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3],
                 out);
        out << "    SUB sp, sp, r12" << endl;
      } else {
        out << "    SUB sp, sp, #"
            << stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3]
            << endl;
      }
      ctx.store_to_stack("r14", ir::OpName("$ra"), out);

      // 保护现场
      int offset = 0;
      for (int i = 0; i < Context::reg_count; i++) {
        if (non_volatile_reg[i] != ctx.savable_reg[i]) {
          ctx.store_to_stack_offset(
              "r" + to_string(i), stack_size[2] + stack_size[3] + offset, out);
          offset += 4;
        }
      }
      // VFP callee-saved：s16-s27
      for (int i = 16; i < Context::freg_count; i++) {
        if (non_volatile_freg[i] != ctx.savable_freg[i]) {
          ctx.store_to_stack_offset("s" + to_string(i),
                                    stack_size[2] + stack_size[3] + offset,
                                    out, "VSTR");
          offset += 4;
        }
      }
    } else if (ir.op_code == ir::OpCode::MOV ||
               ir.op_code == ir::OpCode::PHI_MOV) {
      if (ir.dest == ir.op1) continue;
      if (ir_fp(ir)) {
        // 浮点搬移：位模式在 VFP 寄存器间流动
        const bool dbl = ctx.fp_class(ir.dest.name) == Context::FpClass::F64;
        int src_idx;
        if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
          src_idx = ctx.var_to_freg[ir.op1.name];
        } else {
          ctx.load_freg(dbl ? 28 : 30, dbl, ir.op1, out);
          src_idx = dbl ? 28 : 30;
        }
        ctx.store_freg_dest(src_idx, dbl, ir.dest.name, out);
      } else if (ir_wide(ir)) {
        // 64 位搬移：低/高半分别装载与存放
        ctx.load_half("r12", ir.op1, 0, out);
        store_dest_half(ir.dest.name, "r12", 0);
        ctx.load_half("r12", ir.op1, 1, out);
        store_dest_half(ir.dest.name, "r12", 1);
      } else if (ir.op1.is_var() && ctx.is_wide(ir.op1.name)) {
        // 64 位 -> 32 位窄化：取低字
        ctx.load_half("r12", ir.op1, 0, out);
        bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
        if (dest_in_reg) {
          if ("r" + to_string(ctx.var_to_reg[ir.dest.name]) != "r12")
            out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r12"
                << endl;
        } else {
          ctx.store_to_stack("r12", ir.dest, out);
        }
      } else {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      if (ir.op1.is_imm()) {
        if (dest_in_reg) {
          ctx.load_imm("r" + to_string(ctx.var_to_reg[ir.dest.name]),
                       ir.op1.value, out);
        } else {
          ctx.load_imm("r12", ir.op1.value, out);
          ctx.store_to_stack("r12", ir.dest, out);
        }
      } else if (ir.op1.is_var()) {
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
        int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
        if (!op1_in_reg) {
          ctx.load("r" + to_string(op1), ir.op1, out);
        }
        if (dest_in_reg && op1 != dest)
          out << "    MOV r" << dest << ", r" << op1 << endl;
        else if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(op1), ir.dest, out);
        }
      }
      }
    }
    else if (ir.op_code == ir::OpCode::ADD || ir.op_code == ir::OpCode::SUB) {
      const bool is_add = ir.op_code == ir::OpCode::ADD;
      if (ir_fp(ir)) {
        fp_binary(ir, is_add ? "VADD" : "VSUB");
      } else if (ir_wide(ir)) {
        // 64 位加减：低半运算 + CMP 捕捉进位/借位 + 高半修正。
        // ADD: 进位 = (a.lo + b.lo) 无符号 < a.lo
        // SUB: 借位 = a.lo 无符号 < b.lo
        ctx.load_half("r12", ir.op1, 0, out);
        ctx.load_half("r14", ir.op2, 0, out);
        if (is_add) {
          out << "    ADD r12, r12, r14" << endl;
          store_dest_half(ir.dest.name, "r12", 0);
          ctx.load_half("r14", ir.op1, 0, out);
          out << "    CMP r12, r14" << endl;
        } else {
          out << "    CMP r12, r14" << endl;
          out << "    SUB r12, r12, r14" << endl;
          store_dest_half(ir.dest.name, "r12", 0);
        }
        ctx.load_half("r12", ir.op1, 1, out);
        ctx.load_half("r14", ir.op2, 1, out);
        if (is_add) {
          out << "    ADD r14, r14, r12" << endl;
          out << "    ADDLO r14, r14, #1" << endl;
        } else {
          out << "    SUB r14, r12, r14" << endl;
          out << "    SUBLO r14, r14, #1" << endl;
        }
        store_dest_half(ir.dest.name, "r14", 1);
      } else {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      bool op2_is_imm =
          ir.op2.is_imm() && (ir.op2.value >= 0 && ir.op2.value < 256);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 14;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op2_in_reg && !op2_is_imm) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      out << "    " << (is_add ? "ADD" : "SUB") << " r" << dest << ", r"
          << op1 << ", ";
      if (op2_is_imm) {
        out << "#" << ir.op2.value << endl;
      } else {
        out << "r" << op2 << endl;
      }
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
      }
    }
    else if (ir.op_code == ir::OpCode::IMUL) {
      if (ir_fp(ir)) {
        fp_binary(ir, "VMUL");
      } else if (ir_wide(ir)) {
        // 64 位乘法调用 __aeabi_lmul（r0:r1 × r2:r3 → r0:r1）
        ctx.load_half("r0", ir.op1, 0, out);
        ctx.load_half("r1", ir.op1, 1, out);
        ctx.load_half("r2", ir.op2, 0, out);
        ctx.load_half("r3", ir.op2, 1, out);
        out << "    BL __aeabi_lmul" << endl;
        store_dest_half(ir.dest.name, "r0", 0);
        store_dest_half(ir.dest.name, "r1", 1);
      } else {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 14;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (dest == op1) {
        out << "    MUL r12, r" << op1 << ", r" << op2 << endl;
        out << "    MOV r" << dest << ", r12" << endl;
      } else {
        out << "    MUL r" << dest << ", r" << op1 << ", r" << op2 << endl;
      }
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
      }
    }
#define F(OP_NAME, OP)                                                     \
  else if (ir.op_code == ir::OpCode::OP_NAME) {                            \
    assert(ir.op2.is_imm());                                               \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);      \
    int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 14;               \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;            \
    if (!op1_in_reg) {                                                     \
      ctx.load("r" + to_string(op1), ir.op1, out);                         \
    }                                                                      \
    out << "    " OP " r" << dest << ", r" << op1 << ", #" << ir.op2.value \
        << endl;                                                           \
    if (!dest_in_reg) {                                                    \
      ctx.store_to_stack("r" + to_string(dest), ir.dest, out);             \
    }                                                                      \
  }
    F(SAL, "LSL")
    F(SAR, "ASR")
    F(LSR, "LSR")
#undef F
    // 窄类型规范化：截断到目标宽度后按符号性扩展回 32 位。
    // 右移/与立即数配合即可完成零扩展与符号扩展：
    //   S8: LSL24+ASR24  U8: AND #255   U16/S16 同理
    else if (ir.op_code == ir::OpCode::TRUNC_U8 ||
             ir.op_code == ir::OpCode::TRUNC_S8 ||
             ir.op_code == ir::OpCode::TRUNC_U16 ||
             ir.op_code == ir::OpCode::TRUNC_S16) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 14;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      switch (ir.op_code) {
        case ir::OpCode::TRUNC_U8:
          out << "    AND r" << dest << ", r" << op1 << ", #255" << endl;
          break;
        case ir::OpCode::TRUNC_S8:
          out << "    LSL r" << dest << ", r" << op1 << ", #24" << endl;
          out << "    ASR r" << dest << ", r" << dest << ", #24" << endl;
          break;
        case ir::OpCode::TRUNC_U16:
          out << "    LSL r" << dest << ", r" << op1 << ", #16" << endl;
          out << "    LSR r" << dest << ", r" << dest << ", #16" << endl;
          break;
        case ir::OpCode::TRUNC_S16:
          out << "    LSL r" << dest << ", r" << op1 << ", #16" << endl;
          out << "    ASR r" << dest << ", r" << dest << ", #16" << endl;
          break;
        default:
          break;
      }
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::SEXT || ir.op_code == ir::OpCode::ZEXT) {
      // 32 -> 64 扩展：dest 低半 = op1，高半 = 符号位复制 / 0
      ctx.load_half("r12", ir.op1, 0, out);
      store_dest_half(ir.dest.name, "r12", 0);
      if (ir.op_code == ir::OpCode::SEXT)
        out << "    ASR r12, r12, #31" << endl;
      else
        out << "    MOV r12, #0" << endl;
      store_dest_half(ir.dest.name, "r12", 1);
    }
    else if (ir.op_code == ir::OpCode::IDIV || ir.op_code == ir::OpCode::UDIV ||
             ir.op_code == ir::OpCode::MOD || ir.op_code == ir::OpCode::UMOD) {
      const bool is_div = ir.op_code == ir::OpCode::IDIV ||
                          ir.op_code == ir::OpCode::UDIV;
      if (ir_wide(ir)) {
        // 64 位除法/取模调用 __aeabi_ldivmod / __aeabi_uldivmod：
        // 入参 r0:r1, r2:r3；商 r0:r1，余数 r2:r3
        ctx.load_half("r0", ir.op1, 0, out);
        ctx.load_half("r1", ir.op1, 1, out);
        ctx.load_half("r2", ir.op2, 0, out);
        ctx.load_half("r3", ir.op2, 1, out);
        bool is_signed = ir.op_code == ir::OpCode::IDIV ||
                         ir.op_code == ir::OpCode::MOD;
        out << "    BL " << (is_signed ? "__aeabi_ldivmod" : "__aeabi_uldivmod")
            << endl;
        int lo = is_div ? 0 : 2;
        store_dest_half(ir.dest.name, "r" + to_string(lo), 0);
        store_dest_half(ir.dest.name, "r" + to_string(lo + 1), 1);
      }
      else if (ir.op_code == ir::OpCode::IDIV) {
      if (cfg.optimize_level > 0 && ir.op2.is_imm()) {
        bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
        int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 14;
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 0;
        if (!op1_in_reg) {
          ctx.load("r" + to_string(op1), ir.op1, out);
        }

        int c = ir.op2.value;
        if (c < 0) {
          c = -c;
          out << "    MOV r3, #0" << endl;
          out << "    SUBS r3, r3, r" << op1 << endl;  // op1 = -op1;
          op1 = 3;
        } else {
          out << "    CMP r" << op1 << ", #0" << endl;
        }
        int k = 0;
        int g = c;
        while (g != 1) {
          g = g / 2;
          k += 1;
        }
        if ((c & (c - 1)) ? false : true) {
          out << "    ASR r" << dest << ", r" << op1 << ", #" << k << endl;
          out << "    ADDLT r" << dest << ", r" << dest << ", #1" << endl;
        } else {
          // E = 2^k/c
          double E = (double)((int64_t)1 << k) / c;
          // f = 2^(k+32)/c
          double f = E * ((int64_t)1 << 31);
          // s = |f|
          int64_t s = ceil(f);
          double e = s - f;
          if (e > E) {
            s = floor(f);
          }
          s = ceil(f);
          ctx.load_imm("r1", s, out);
          out << "    SMULL r12, r2, r" << op1 << ", r1" << endl;
          out << "    ASR r" << dest << ", r2, #" << k - 1 << endl;
          out << "    ADDLT r" << dest << ", r" << dest << ", #1" << endl;
        }

        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      } else {
        ctx.load("r0", ir.op1, out);
        ctx.load("r1", ir.op2, out);
        out << "    BL __aeabi_idiv" << endl;
        if (ctx.var_in_reg(ir.dest.name)) {
          out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
        } else {
          ctx.store_to_stack("r0", ir.dest, out);
        }
      }
    }
    else if (ir.op_code == ir::OpCode::MOD) {
      ctx.load("r0", ir.op1, out);
      ctx.load("r1", ir.op2, out);
      out << "    BL __aeabi_idivmod" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r1" << endl;
      } else {
        ctx.store_to_stack("r1", ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::UDIV) {
      ctx.load("r0", ir.op1, out);
      ctx.load("r1", ir.op2, out);
      out << "    BL __aeabi_uidiv" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
      } else {
        ctx.store_to_stack("r0", ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::UMOD) {
      ctx.load("r0", ir.op1, out);
      ctx.load("r1", ir.op2, out);
      out << "    BL __aeabi_uidivmod" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r1" << endl;
      } else {
        ctx.store_to_stack("r1", ir.dest, out);
      }
    }
    }
    else if (ir.op_code == ir::OpCode::FDIV) {
      fp_binary(ir, "VDIV");
    }
    else if (ir.op_code == ir::OpCode::FNEG) {
      const bool dbl = ctx.fp_class(ir.dest.name) == Context::FpClass::F64;
      const string& a = dbl ? FP_DA : FP_A;
      string areg;
      if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
        areg = fploc(ir.op1);
      } else {
        ctx.load_freg(dbl ? 28 : 30, dbl, ir.op1, out);
        areg = a;
      }
      bool dest_in_freg = ir.dest.is_var() && ctx.var_in_freg(ir.dest.name);
      string dreg = dest_in_freg ? fploc(ir.dest) : a;
      out << "    VNEG" << (dbl ? ".F64" : ".F32") << " " << dreg << ", "
          << areg << endl;
      if (!dest_in_freg) {
        ctx.store_freg_dest(dbl ? 28 : 30, dbl, ir.dest.name, out);
      }
    }
    else if (ir.op_code == ir::OpCode::SITOF || ir.op_code == ir::OpCode::UITOF) {
      // 整数 → 浮点：核心寄存器 → s 草稿 → VCVT
      const bool dbl = ctx.fp_class(ir.dest.name) == Context::FpClass::F64;
      ctx.load("r12", ir.op1, out);
      out << "    VMOV s31, r12" << endl;
      bool dest_in_freg = ir.dest.is_var() && ctx.var_in_freg(ir.dest.name);
      string dreg = dest_in_freg ? fploc(ir.dest) : (dbl ? FP_DA : FP_A);
      out << "    VCVT." << (dbl ? "F64" : "F32") << "."
          << (ir.op_code == ir::OpCode::UITOF ? "U32" : "S32") << " " << dreg
          << ", s31" << endl;
      if (!dest_in_freg) {
        ctx.store_freg_dest(dbl ? 28 : 30, dbl, ir.dest.name, out);
      }
    }
    else if (ir.op_code == ir::OpCode::F2I || ir.op_code == ir::OpCode::F2U) {
      // 浮点 → 32 位整数：VCVT 截断到 s 草稿，再送回核心寄存器
      // 源宽度取 ty（F2I/F2U 的 ty 记录源浮点类型），imm 操作数同样成立
      const bool srcdbl = syc::ast::types::canonical(ir.ty.id) ==
                          syc::ast::types::TypeId::DOUBLE;
      string areg;
      if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
        areg = fploc(ir.op1);
      } else {
        ctx.load_freg(srcdbl ? 28 : 30, srcdbl, ir.op1, out);
        areg = srcdbl ? FP_DA : FP_A;
      }
      out << "    VCVT." << (ir.op_code == ir::OpCode::F2U ? "U32" : "S32")
          << "." << (srcdbl ? "F64" : "F32") << " s31, " << areg << endl;
      out << "    VMOV r12, s31" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r12" << endl;
      } else {
        ctx.store_to_stack("r12", ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::F2F) {
      // 浮点宽度转换 F32 <-> F64（源宽度与 dest ty 相反，imm 操作数同样成立）
      const bool dbl_d = syc::ast::types::canonical(ir.ty.id) ==
                         syc::ast::types::TypeId::DOUBLE;
      const bool srcdbl = !dbl_d;
      string areg;
      if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
        areg = fploc(ir.op1);
      } else {
        ctx.load_freg(srcdbl ? 28 : 30, srcdbl, ir.op1, out);
        areg = srcdbl ? FP_DA : FP_A;
      }
      bool dest_in_freg = ir.dest.is_var() && ctx.var_in_freg(ir.dest.name);
      string dreg = dest_in_freg ? fploc(ir.dest) : (dbl_d ? FP_DA : FP_A);
      out << "    VCVT." << (dbl_d ? "F64.F32" : "F32.F64") << " " << dreg
          << ", " << areg << endl;
      if (!dest_in_freg) {
        ctx.store_freg_dest(dbl_d ? 28 : 30, dbl_d, ir.dest.name, out);
      }
    }
    else if (ir.op_code == ir::OpCode::CALL) {
      out << "    BL " << ir.label << endl;
      if (ir.dest.is_var() && ir_fp(ir)) {
        // 浮点返回值在 s0（float）/ d0（double）
        const bool dbl = ctx.fp_class(ir.dest.name) == Context::FpClass::F64;
        if (ctx.var_in_freg(ir.dest.name)) {
          string dd = fploc(ir.dest);
          string src = dbl ? "d0" : "s0";
          if (dd != src)
            out << "    VMOV " << dd << ", " << src << endl;
        } else {
          int off = ctx.resolve_stack_offset(ir.dest.name);
          ctx.store_to_stack_offset(dbl ? "d0" : "s0", off, out, "VSTR");
        }
      } else if (ir.dest.is_var()) {
        if (ir_wide(ir)) {
          // 64 位返回值在 r0:r1
          store_dest_half(ir.dest.name, "r0", 0);
          store_dest_half(ir.dest.name, "r1", 1);
        } else if (ctx.var_in_reg(ir.dest.name)) {
          out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
        } else {
          ctx.store_to_stack("r0", ir.dest, out);
        }
      }
    }
    else if (ir.op_code == ir::OpCode::SET_ARG) {
      if (ir.label == "vfp") {
        // VFP 实参：槽位 N<16 写 sN/dN（double 占偶对齐两个 s 槽），
        // N>=16 溢出到核心栈参数区之后的栈字
        const int i = (int)ir.dest.value;
        const bool dbl =
            syc::ast::types::width_bytes(
                syc::ast::types::canonical(ir.ty.id)) >= 8;
        if (i < 16) {
          if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
            string src = fploc(ir.op1);
            string dd = dbl ? "d" + to_string(i / 2) : "s" + to_string(i);
            if (src != dd) out << "    VMOV " << dd << ", " << src << endl;
          } else {
            ctx.load_freg(i, dbl, ir.op1, out);
          }
        } else {
          ctx.load_freg(dbl ? 28 : 30, dbl, ir.op1, out);
          ctx.store_to_stack_offset(dbl ? "d14" : "s30",
                                    ctx.stack_size[3] + (i - 16) * 4, out,
                                    "VSTR");
        }
      } else if (ir_wide(ir) && ir.op1.is_var() && ctx.is_fpreg(ir.op1.name)) {
        // 变参调用的 double 位模式（C 默认实参提升后）经核心槽位传递
        const int i = (int)ir.dest.value;
        if (ctx.var_in_freg(ir.op1.name)) {
          string dreg = fploc(ir.op1);
          if (i + 1 < 4) {
            out << "    VMOV r" << i << ", r" << (i + 1) << ", " << dreg
                << endl;
          } else if (i == 3) {
            // 低位半在 r3，高位半在栈参数区第一个字
            out << "    VMOV r12, r14, " << dreg << endl;
            out << "    MOV r3, r12" << endl;
            ctx.store_to_stack_offset("r14", 0, out);
          } else {
            out << "    VMOV r12, r14, " << dreg << endl;
            ctx.store_to_stack_offset("r12", (i - 4) * 4, out);
            ctx.store_to_stack_offset("r14", (i - 4) * 4 + 4, out);
          }
        } else {
          ctx.load_freg(28, true, ir.op1, out);
          if (i + 1 < 4) {
            out << "    VMOV r" << i << ", r" << (i + 1) << ", d14" << endl;
          } else if (i == 3) {
            out << "    VMOV r12, r14, d14" << endl;
            out << "    MOV r3, r12" << endl;
            ctx.store_to_stack_offset("r14", 0, out);
          } else {
            out << "    VMOV r12, r14, d14" << endl;
            ctx.store_to_stack_offset("r12", (i - 4) * 4, out);
            ctx.store_to_stack_offset("r14", (i - 4) * 4 + 4, out);
          }
        }
      } else if (ir_wide(ir)) {
        // 64 位实参占连续两个槽位（寄存器对或连续栈字；
        // 起始于 r3/槽位 3 时高低半分置两处）
        int i = (int)ir.dest.value;
        if (i + 1 < 4) {
          ctx.load_half("r" + to_string(i), ir.op1, 0, out);
          ctx.load_half("r" + to_string(i + 1), ir.op1, 1, out);
        } else if (i == 3) {
          ctx.load_half("r3", ir.op1, 0, out);
          ctx.load_half("r12", ir.op1, 1, out);
          ctx.store_to_stack_offset("r12", 0, out);
        } else {
          ctx.load_half("r12", ir.op1, 0, out);
          ctx.store_to_stack_offset("r12", (i - 4) * 4, out);
          ctx.load_half("r12", ir.op1, 1, out);
          ctx.store_to_stack_offset("r12", (i - 4) * 4 + 4, out);
        }
      } else if (ir.dest.value < 4) {
        ctx.load("r" + to_string(ir.dest.value), ir.op1, out);
      } else {
        ctx.load("r12", ir.op1, out);
        ctx.store_to_stack_offset("r12", (ir.dest.value - 4) * 4, out);
      }
    }
    else if (ir.op_code == ir::OpCode::CMP) {
      if (ir_fp(ir)) {
        // 浮点比较：VCMP + VMRS 之后利用原始 FP 标志（N=小于、Z=等于或
        // 无序、C=大于等于或无序、V=无序）与相邻的 Jcc/MOVcc 融合，正确
        // 实现 C 语义（无序值时 <、<=、>、>=、== 全假而 != 为真）；无相邻
        // 消费者时退化为三值谓词（仅设置标志，无正确性影响）
        const bool dbl =
            syc::ast::types::canonical(ir.ty.id) ==
            syc::ast::types::TypeId::DOUBLE;
        const string& a = dbl ? FP_DA : FP_A;
        const string& b = dbl ? FP_DB : FP_B;
        if (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name)) {
        } else {
          ctx.load_freg(dbl ? 28 : 30, dbl, ir.op1, out);
        }
        string areg =
            (ir.op1.is_var() && ctx.var_in_freg(ir.op1.name))
                ? fploc(ir.op1)
                : a;
        if (ir.op2.is_var() && ctx.var_in_freg(ir.op2.name)) {
        } else {
          ctx.load_freg(dbl ? 30 : 31, dbl, ir.op2, out);
        }
        string breg =
            (ir.op2.is_var() && ctx.var_in_freg(ir.op2.name))
                ? fploc(ir.op2)
                : b;
        out << "    VCMP" << (dbl ? ".F64" : ".F32") << " " << areg << ", "
            << breg << endl;
        out << "    VMRS" << endl;

        auto nxt = std::next(it);
        ir::OpCode nop = nxt != end ? nxt->op_code : ir::OpCode::NOOP;
        const string nl = nxt != end ? nxt->label : string();
        const string base =
            ".LCMPF_" + to_string(ctx.wide_label_id++) + "_";
        const string skip = base + "SKIP";
        const string lt = base + "LT", eq = base + "EQ",
                     un = base + "U", done = base + "DONE";
        bool fused = true;
        switch (nop) {
          case ir::OpCode::JLT:
            // 有序且小于时 N 才为 1
            out << "    BMI " << nl << endl;
            break;
          case ir::OpCode::JGE:
            // 无序时 N!=V 不成立
            out << "    BGE " << nl << endl;
            break;
          case ir::OpCode::JGT:
            // 无序时 Z=1 不成立
            out << "    BGT " << nl << endl;
            break;
          case ir::OpCode::JLE:
            out << "    BVS " << skip << endl;
            out << "    BMI " << nl << endl;
            out << "    BEQ " << nl << endl;
            out << skip << ":" << endl;
            break;
          case ir::OpCode::JEQ:
            out << "    BVS " << skip << endl;
            out << "    BEQ " << nl << endl;
            out << skip << ":" << endl;
            break;
          case ir::OpCode::JNE:
            // 无序时 != 为真
            out << "    BNE " << nl << endl;
            out << "    BVS " << nl << endl;
            break;
          case ir::OpCode::JMP:
            out << "    B " << nl << endl;
            break;
          case ir::OpCode::MOVEQ:
          case ir::OpCode::MOVNE:
          case ir::OpCode::MOVLT:
          case ir::OpCode::MOVGT:
          case ir::OpCode::MOVGE:
          case ir::OpCode::MOVLE: {
            bool dest_in_reg = ctx.var_in_reg(nxt->dest.name);
            string rd = dest_in_reg
                            ? "r" + to_string(ctx.var_to_reg[nxt->dest.name])
                            : "r12";
            switch (nop) {
              case ir::OpCode::MOVEQ:
                out << "    MOV " << rd << ", #0" << endl;
                out << "    BVS " << skip << endl;
                out << "    MOVEQ " << rd << ", #1" << endl;
                out << skip << ":" << endl;
                break;
              case ir::OpCode::MOVNE:
                out << "    MOV " << rd << ", #1" << endl;
                out << "    BVS " << skip << endl;
                out << "    MOVEQ " << rd << ", #0" << endl;
                out << skip << ":" << endl;
                break;
              case ir::OpCode::MOVLT:
                out << "    MOVLT " << rd << ", #1" << endl;
                out << "    MOVPL " << rd << ", #0" << endl;
                break;
              case ir::OpCode::MOVGT:
                out << "    MOVGT " << rd << ", #1" << endl;
                out << "    MOVLE " << rd << ", #0" << endl;
                break;
              case ir::OpCode::MOVGE:
                out << "    MOVGE " << rd << ", #1" << endl;
                out << "    MOVLT " << rd << ", #0" << endl;
                break;
              case ir::OpCode::MOVLE:
                out << "    MOV " << rd << ", #0" << endl;
                out << "    BVS " << skip << endl;
                out << "    MOVMI " << rd << ", #1" << endl;
                out << "    MOVEQ " << rd << ", #1" << endl;
                out << skip << ":" << endl;
                break;
              default:
                break;
            }
            if (!dest_in_reg) ctx.store_to_stack("r12", nxt->dest, out);
            break;
          }
          default:
            fused = false;
        }
        if (fused) {
          skip_next_ir = true;
        } else {
          // 兜底：三值谓词（无相邻消费者，仅设置标志）
          out << "    BVS " << un << endl;
          out << "    BMI " << lt << endl;
          out << "    BEQ " << eq << endl;
          out << "    MOV r12, #1" << endl;
          out << "    B " << done << endl;
          out << lt << ":" << endl;
          out << "    MOV r12, #1" << endl;
          out << "    SUB r12, r12, #2" << endl;
          out << "    B " << done << endl;
          out << eq << ":" << endl;
          out << "    MOV r12, #0" << endl;
          out << "    B " << done << endl;
          out << un << ":" << endl;
          out << "    MOV r12, #2" << endl;
          out << done << ":" << endl;
          out << "    CMP r12, #0" << endl;
        }
      } else if (ir_wide(ir)) {
        // 64 位比较：合成三方谓词 pred∈{-1,0,1} 后 CMP pred,#0，
        // 使后续条件码统一按有符号语义解释（高半按结果类型符号性比较，
        // 低半恒为无符号比较）
        const bool uns =
            !syc::ast::types::is_signed(syc::ast::types::canonical(ir.ty.id));
        const string base =
            ".LCMP64_" + to_string(ctx.wide_label_id++) + "_";
        const string lt = base + "LT", gt = base + "GT", done = base + "DONE";
        ctx.load_half("r12", ir.op1, 1, out);
        ctx.load_half("r14", ir.op2, 1, out);
        out << "    CMP r12, r14" << endl;
        out << "    " << (uns ? "BLO" : "BLT") << " " << lt << endl;
        out << "    " << (uns ? "BHI" : "BGT") << " " << gt << endl;
        ctx.load_half("r12", ir.op1, 0, out);
        ctx.load_half("r14", ir.op2, 0, out);
        out << "    CMP r12, r14" << endl;
        out << "    BLO " << lt << endl;
        out << "    BHI " << gt << endl;
        out << "    MOV r12, #0" << endl;
        out << "    B " << done << endl;
        out << lt << ":" << endl;
        out << "    MOV r12, #1" << endl;
        out << "    SUB r12, r12, #2" << endl;
        out << "    B " << done << endl;
        out << gt << ":" << endl;
        out << "    MOV r12, #1" << endl;
        out << done << ":" << endl;
        out << "    CMP r12, #0" << endl;
      } else {
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 14;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    CMP r" << op1 << ", r" << op2 << endl;
      }
    }
#define F(OP_NAME, OP)                          \
  else if (ir.op_code == ir::OpCode::OP_NAME) { \
    out << "    " OP " " << ir.label << endl;   \
  }
    F(JMP, "B")
    F(JEQ, "BEQ")
    F(JNE, "BNE")
    F(JLE, "BLE")
    F(JLT, "BLT")
    F(JGE, "BGE")
    F(JGT, "BGT")
    // 无符号条件码：HS(>=) LO(<) LS(<=) HI(>)
    F(JMPHS, "BHS")
    F(JMPLO, "BLO")
    F(JMPLS, "BLS")
    F(JMPI, "BHI")
#undef F
#define F(OP_NAME, OP_THEN, OP_ELSE)                                       \
  else if (ir.op_code == ir::OpCode::OP_NAME) {                            \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;            \
    if (ir.op1.is_imm() && ir.op1.value >= 0 && ir.op1.value < 256 &&      \
        ir.op2.is_imm() && ir.op2.value >= 0 && ir.op2.value < 256) {      \
      out << "    " OP_THEN " r" << dest << ", #" << ir.op1.value << endl; \
      out << "    " OP_ELSE " r" << dest << ", #" << ir.op2.value << endl; \
      if (!dest_in_reg) {                                                  \
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);           \
      }                                                                    \
    } else { /* TODO */                                                    \
    }                                                                      \
  }
    F(MOVEQ, "MOVEQ", "MOVNE")
    F(MOVNE, "MOVNE", "MOVEQ")
    F(MOVLE, "MOVLE", "MOVGT")
    F(MOVGT, "MOVGT", "MOVLE")
    F(MOVLT, "MOVLT", "MOVGE")
    F(MOVGE, "MOVGE", "MOVLT")
    // 无符号比较结果规范化
    F(MOVHS, "MOVHS", "MOVLO")
    F(MOVLO, "MOVLO", "MOVHS")
    F(MOVLS, "MOVLS", "MOVHI")
    F(MOVHI, "MOVHI", "MOVLS")
#undef F
    else if (ir.op_code == ir::OpCode::AND) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 14;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    TST r" << op1 << ", r" << op2 << endl;
      out << "    MOVEQ r" << dest << ", #0" << endl;
      out << "    MOVNE r" << dest << ", #1" << endl;
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::OR) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 14;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    ORRS r12, r" << op1 << ", r" << op2 << endl;
      out << "    MOVEQ r" << dest << ", #0" << endl;
      out << "    MOVNE r" << dest << ", #1" << endl;
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::STORE) {
      // op1 基地址
      if (ir_fp(ir)) {
        // 浮点存储：值装入 VFP 寄存器后 VSTR（宽度按 STORE 的元素类型）
        const bool dbl =
            syc::ast::types::canonical(ir.ty.id) ==
            syc::ast::types::TypeId::DOUBLE;
        if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
            ir.op2.is_imm()) {
          int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
          if (ir.op3.is_var() && ctx.var_in_freg(ir.op3.name)) {
            ctx.store_to_stack_offset(fploc(ir.op3), offset, out, "VSTR");
          } else {
            ctx.load_freg(dbl ? 28 : 30, dbl, ir.op3, out);
            ctx.store_to_stack_offset(dbl ? "d14" : "s30", offset, out,
                                      "VSTR");
          }
        } else {
          // 先装值（load_freg 需要用 r12/r14 做草稿），后合成地址：
          // 基址在寄存器时直接作基址寄存器，小偏移且按 4 对齐时用
          // VSTR 自带偏移
          string vreg;
          if (ir.op3.is_var() && ctx.var_in_freg(ir.op3.name)) {
            vreg = fploc(ir.op3);
          } else {
            ctx.load_freg(dbl ? 28 : 30, dbl, ir.op3, out);
            vreg = dbl ? "d14" : "s30";
          }
          bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
          string base =
              op1_in_reg ? "r" + to_string(ctx.var_to_reg[ir.op1.name])
                         : "r12";
          if (!op1_in_reg) ctx.load("r12", ir.op1, out);
          if (ir.op2.is_imm()) {
            long long v = ir.op2.value;
            if (op1_in_reg && v == 0) {
              out << "    VSTR " << vreg << ", [" << base << "]" << endl;
            } else if (op1_in_reg && v % 4 == 0 && v >= -1020 && v <= 1020) {
              out << "    VSTR " << vreg << ", [" << base << ",#" << v << "]"
                  << endl;
            } else if (v >= -4096 && v < 4096) {
              if (v != 0) out << "    ADD r12, " << base << ", #" << v << endl;
              out << "    VSTR " << vreg << ", [r12,#0]" << endl;
            } else {
              ctx.load_imm("r14", v, out);
              out << "    ADD r12, " << base << ", r14" << endl;
              out << "    VSTR " << vreg << ", [r12,#0]" << endl;
            }
          } else {
            ctx.load("r14", ir.op2, out);
            out << "    ADD r12, " << base << ", r14" << endl;
            out << "    VSTR " << vreg << ", [r12,#0]" << endl;
          }
        }
      } else if (ir_wide(ir)) {
        // 64 位存储：写低字与高字（+4）
        if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
            ir.op2.is_imm()) {
          int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
          ctx.load_half("r14", ir.op3, 0, out);
          ctx.store_to_stack_offset("r14", offset, out);
          ctx.load_half("r14", ir.op3, 1, out);
          ctx.store_to_stack_offset("r14", offset + 4, out);
        } else {
          // 有效地址合成到 r12，再分两次写字
          ctx.load("r12", ir.op1, out);
          if (ir.op2.is_imm()) {
            if (ir.op2.value >= -4096 && ir.op2.value < 4096) {
              if (ir.op2.value != 0)
                out << "    ADD r12, r12, #" << ir.op2.value << endl;
            } else {
              ctx.load_imm("r14", ir.op2.value, out);
              out << "    ADD r12, r12, r14" << endl;
            }
          } else {
            ctx.load("r14", ir.op2, out);
            out << "    ADD r12, r12, r14" << endl;
          }
          ctx.load_half("r14", ir.op3, 0, out);
          out << "    STR r14, [r12]" << endl;
          ctx.load_half("r14", ir.op3, 1, out);
          out << "    STR r14, [r12,#4]" << endl;
        }
      } else {
      // op1 基地址
      bool op3_in_reg = ir.op3.is_var() && ctx.var_in_reg(ir.op3.name);
      int op3 = op3_in_reg ? ctx.var_to_reg[ir.op3.name] : 14;
      if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.is_imm()) {
        if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.store_to_stack_offset("r" + to_string(op3), offset, out);
      } else {
        bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        bool op3_in_reg = ir.op3.is_var() && ctx.var_in_reg(ir.op3.name);
        int op3 = op3_in_reg ? ctx.var_to_reg[ir.op3.name] : 14;
        bool offset_is_small =
            ir.op2.is_imm() && ir.op2.value >= -4096 && ir.op2.value < 4096;
        int b = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
        int o = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 14;
        // 值在 r12 而基地址也要装载到 r12 时，值退到 r14
        if (op3_in_reg && op3 == 12 && !op1_in_reg) {
          op3 = 14;
          op3_in_reg = false;
        }
        // 偏移与值缺省寄存器同为 r14，二者都需装载时不能一步完成：
        // 走「地址先合成到 r12，再装值」的保守路径
        bool off_conflict =
            !offset_is_small && o == 14 && op3 == 14 && !op3_in_reg;
        bool base_conflict =
            offset_is_small && b == 14 && op3 == 14 && !op3_in_reg;
        if (!off_conflict && !base_conflict &&
            !(!op1_in_reg && !offset_is_small && op2_in_reg && o == 12)) {
          if (!op2_in_reg && !offset_is_small)
            ctx.load("r14", ir.op2, out);
          if (!op1_in_reg) ctx.load("r12", ir.op1, out);
          if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
          out << "    STR r" << op3 << ", [r" << b << ","
              << (offset_is_small ? '#' : 'r')
              << (offset_is_small ? ir.op2.value : o) << "]" << endl;
        } else {
          if (op1_in_reg) {
            if (b != 12) out << "    MOV r12, r" << b << endl;
          } else {
            ctx.load("r12", ir.op1, out);
          }
          if (offset_is_small) {
            if (ir.op2.value != 0)
              out << "    ADD r12, r12, #" << ir.op2.value << endl;
          } else {
            int o2 = 14;
            if (op2_in_reg && ctx.var_to_reg[ir.op2.name] != 12) {
              o2 = ctx.var_to_reg[ir.op2.name];
            } else {
              ctx.load("r14", ir.op2, out);
            }
            out << "    ADD r12, r12, r" << o2 << endl;
          }
          if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
          out << "    STR r" << op3 << ", [r12]" << endl;
        }
      }
      }
    }
    else if (ir.op_code == ir::OpCode::LOAD) {
      if (ir_fp(ir)) {
        // 浮点装载：地址合成到 r12 后 VLDR（单条 32/64 位）
        const bool dbl = ctx.fp_class(ir.dest.name) == Context::FpClass::F64;
        const string dreg =
            (ir.dest.is_var() && ctx.var_in_freg(ir.dest.name))
                ? fploc(ir.dest)
                : (dbl ? "d14" : "s30");
        if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
            ir.op2.is_imm()) {
          int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
          ctx.load_from_stack_offset(dreg, offset, out, "VLDR");
        } else {
          // 基址已在寄存器时直接合成地址（省去 r12 搬运），小偏移
          // 且按 4 对齐时用 VLDR/VSTR 自带偏移
          bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
          string base =
              op1_in_reg ? "r" + to_string(ctx.var_to_reg[ir.op1.name])
                         : "r12";
          if (!op1_in_reg) ctx.load("r12", ir.op1, out);
          auto vldr_off = [&](const string &reg, long long v) {
            if (op1_in_reg && v == 0) {
              out << "    VLDR " << reg << ", [" << base << "]"
                  << endl;
            } else if (op1_in_reg && v % 4 == 0 && v >= -1020 && v <= 1020) {
              out << "    VLDR " << reg << ", [" << base << ",#"
                  << v << "]" << endl;
            } else if (v >= -4096 && v < 4096) {
              if (v != 0) out << "    ADD r12, " << base << ", #" << v << endl;
              out << "    VLDR " << reg << ", [r12,#0]" << endl;
            } else {
              ctx.load_imm("r14", v, out);
              out << "    ADD r12, " << base << ", r14" << endl;
              out << "    VLDR " << reg << ", [r12,#0]" << endl;
            }
          };
          if (ir.op2.is_imm()) {
            vldr_off(dreg, ir.op2.value);
          } else {
            ctx.load("r14", ir.op2, out);
            out << "    ADD r12, " << base << ", r14" << endl;
            out << "    VLDR " << dreg << ", [r12,#0]" << endl;
          }
        }
        if (!(ir.dest.is_var() && ctx.var_in_freg(ir.dest.name))) {
          ctx.store_freg_dest(dbl ? 28 : 30, dbl, ir.dest.name, out);
        }
      } else if (ir_wide(ir)) {
        // 64 位装载：读低字与高字（+4）
        if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
            ir.op2.is_imm()) {
          int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
          ctx.load_from_stack_offset("r12", offset, out);
          store_dest_half(ir.dest.name, "r12", 0);
          ctx.load_from_stack_offset("r12", offset + 4, out);
          store_dest_half(ir.dest.name, "r12", 1);
        } else {
          // 有效地址合成到 r12
          ctx.load("r12", ir.op1, out);
          if (ir.op2.is_imm()) {
            if (ir.op2.value >= -4096 && ir.op2.value < 4096) {
              if (ir.op2.value != 0)
                out << "    ADD r12, r12, #" << ir.op2.value << endl;
            } else {
              ctx.load_imm("r14", ir.op2.value, out);
              out << "    ADD r12, r12, r14" << endl;
            }
          } else {
            ctx.load("r14", ir.op2, out);
            out << "    ADD r12, r12, r14" << endl;
          }
          int d_lo = ctx.var_in_reg(ir.dest.name)
                         ? ctx.var_to_reg[ir.dest.name]
                         : -1;
          const string hi = Context::hi_name(ir.dest.name);
          int d_hi = ctx.var_in_reg(hi) ? ctx.var_to_reg[hi] : -1;
          if (d_lo >= 0) {
            out << "    LDR r" << d_lo << ", [r12]" << endl;
          } else {
            out << "    LDR r14, [r12]" << endl;
            ctx.store_to_stack("r14", ir.dest, out);
          }
          if (d_hi >= 0) {
            out << "    LDR r" << d_hi << ", [r12,#4]" << endl;
          } else {
            out << "    LDR r14, [r12,#4]" << endl;
            ctx.store_to_stack("r14", ir::OpName(hi), out);
          }
        }
      } else {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      // op1 基地址
      if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.is_imm()) {
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.load_from_stack_offset("r" + to_string(dest), offset, out);
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      } else {
        bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
        // 基址已在寄存器时直接用作 LDR 基址寄存器，省去每次 r12 搬运
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
        int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 14;
        bool offset_is_small =
            ir.op2.is_imm() && ir.op2.value >= -4096 && ir.op2.value < 4096;

        if (!op2_in_reg && !offset_is_small)
          ctx.load("r" + to_string(op2), ir.op2, out);
        if (!op1_in_reg) ctx.load("r12", ir.op1, out);
        out << "    LDR r" << dest << ", [r" << op1 << ","
            << (offset_is_small ? '#' : 'r')
            << (offset_is_small ? ir.op2.value : op2) << "]" << endl;
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      }
      }
    }
    else if (ir.op_code == ir::OpCode::RET) {
      if (!ir.op1.is_null() && ir_fp(ir)) {
        // 浮点返回值在 s0（float）/ d0（double）
        const bool dbl =
            syc::ast::types::canonical(ir.ty.id) ==
            syc::ast::types::TypeId::DOUBLE;
        ctx.load_freg(0, dbl, ir.op1, out);
      } else if (!ir.op1.is_null()) {
        if (ir_wide(ir)) {
          // 64 位返回值放 r0（低半）与 r1（高半）
          ctx.load_half("r0", ir.op1, 0, out);
          ctx.load_half("r1", ir.op1, 1, out);
        } else {
          ctx.load("r0", ir.op1, out);
        }
      }
      ctx.load("r14", ir::OpName("$ra"), out);

      // 恢复现场
      int offset = 0;
      for (int i = 0; i < Context::reg_count; i++) {
        if (non_volatile_reg[i] != ctx.savable_reg[i]) {
          ctx.load_from_stack_offset(
              "r" + to_string(i), stack_size[2] + stack_size[3] + offset, out);
          offset += 4;
        }
      }
      for (int i = 16; i < Context::freg_count; i++) {
        if (non_volatile_freg[i] != ctx.savable_freg[i]) {
          ctx.load_from_stack_offset("s" + to_string(i),
                                     stack_size[2] + stack_size[3] + offset,
                                     out, "VLDR");
          offset += 4;
        }
      }

      if (stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] > 127) {
        ctx.load("r12",
                 stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3],
                 out);
        out << "    ADD sp, sp, r12" << endl;
      } else {
        out << "    ADD sp, sp, #"
            << stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3]
            << endl;
      }
      out << "    MOV pc, r14" << endl;
    }

    else if (ir.op_code == ir::OpCode::LABEL) {
      out << ir.label << ':' << endl;
    }
  }
}  // namespace
}  // namespace

void generate(ir::IRList& irs, ostream& out, ostream& log_out,
              const syc::Config& cfg) {
  out << R"(
.macro mov32, reg, val
    movw \reg, #:lower16:\val
    movt \reg, #:upper16:\val
.endm
)" << endl;
  if (cfg.enable_dwarf2) {
    out << ".file 1 \"" << cfg.input_filename << "\"" << endl;
  }
  ir::IRList::iterator function_begin_it;
  for (auto outter_it = irs.begin(); outter_it != irs.end(); outter_it++) {
    auto& ir = *outter_it;
    if (ir.op_code == ir::OpCode::DATA_BEGIN) {
      write_dwarf2(ir, out, cfg);
      out << ".data" << endl;
      out << ".global " << Context::rename(ir.label) << endl;
      out << Context::rename(ir.label) << ":" << endl;
    } else if (ir.op_code == ir::OpCode::DATA_WORD) {
      write_dwarf2(ir, out, cfg);
      out << ".word " << ir.dest.value << endl;
    } else if (ir.op_code == ir::OpCode::DATA_SPACE) {
      write_dwarf2(ir, out, cfg);
      out << ".space " << ir.dest.value << endl;
    } else if (ir.op_code == ir::OpCode::DATA_END) {
      // do nothing
    } else if (ir.op_code == ir::OpCode::DATA_STRING) {
      write_dwarf2(ir, out, cfg);
      out << ".section .rodata" << endl;
      out << Context::rename(ir.dest.name) << ":" << endl;
      out << ".asciz \"" << escape_asm_string(ir.label) << "\"" << endl;
    } else if (ir.op_code == ir::OpCode::FUNCTION_BEGIN) {
      function_begin_it = outter_it;
    } else if (ir.op_code == ir::OpCode::FUNCTION_END) {
      generate_function_asm(irs, function_begin_it, outter_it, out, log_out,
                            cfg);
    }
  }
}

void generate(ir::IRList& irs, ostream& out, const syc::Config& cfg) {
  ofstream null;
  null.setstate(std::ios_base::badbit);
  generate(irs, out, null, cfg);
}
}  // namespace syc::assembly
