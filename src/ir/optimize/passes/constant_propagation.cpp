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
#include "ir/optimize/passes/constant_propagation.h"

#include <cmath>
#include "ir/optimize/passes/ir_fold.h"
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace syc::ir::passes {
namespace {

using types::TypeId;
using types::TypeInfo;
// 变量 -> 常量（int64 规范表示 + 类型），缺失即非常量/未知
using Consts = std::map<std::string, std::pair<std::int64_t, TypeId>>;

bool is_local_scalar(const OpName &op) {
  return op.is_var() && op.name[0] == '%' && op.name.compare(0, 2, "%&") != 0;
}


bool is_jcc(OpCode op) {
  switch (op) {
    case OpCode::JEQ:
    case OpCode::JNE:
    case OpCode::JLE:
    case OpCode::JLT:
    case OpCode::JGE:
    case OpCode::JGT:
    case OpCode::JMPHS:
    case OpCode::JMPLO:
    case OpCode::JMPLS:
    case OpCode::JMPI:
      return true;
    default:
      return false;
  }
}

bool is_movcc(OpCode op) {
  switch (op) {
    case OpCode::MOVEQ:
    case OpCode::MOVNE:
    case OpCode::MOVLE:
    case OpCode::MOVLT:
    case OpCode::MOVGE:
    case OpCode::MOVGT:
    case OpCode::MOVHS:
    case OpCode::MOVLO:
    case OpCode::MOVLS:
    case OpCode::MOVHI:
      return true;
    default:
      return false;
  }
}

// 按条件码求值；浮点 ty 用 IEEE 语义（调用方需保证无 NaN）
bool eval_cond(OpCode cc, TypeId ty, std::int64_t a, std::int64_t b) {
  if (types::is_floating(ty)) {
    double x = types::const_bits_to_double(a, ty);
    double y = types::const_bits_to_double(b, ty);
    switch (cc) {
      case OpCode::JEQ: return x == y;
      case OpCode::JNE: return x != y;
      case OpCode::JLT: case OpCode::JMPLO: return x < y;
      case OpCode::JLE: case OpCode::JMPLS: return x <= y;
      case OpCode::JGT: case OpCode::JMPI: return x > y;
      case OpCode::JGE: case OpCode::JMPHS: return x >= y;
      default: return false;
    }
  }
  switch (cc) {
    case OpCode::JEQ: return a == b;
    case OpCode::JNE: return a != b;
    case OpCode::JLT: return a < b;
    case OpCode::JLE: return a <= b;
    case OpCode::JGT: return a > b;
    case OpCode::JGE: return a >= b;
    case OpCode::JMPLO: return (std::uint64_t)a < (std::uint64_t)b;
    case OpCode::JMPHS: return (std::uint64_t)a >= (std::uint64_t)b;
    case OpCode::JMPLS: return (std::uint64_t)a <= (std::uint64_t)b;
    case OpCode::JMPI: return (std::uint64_t)a > (std::uint64_t)b;
    default: return false;
  }
}


// 折叠运算；不可折叠（除零、移位越界、浮点转整溢出等）返回 nullopt，
// 留给运行时保持原语义
std::optional<std::int64_t> fold(OpCode op, TypeId ty, std::int64_t a,
                                 std::int64_t b) {
  using types::width_bytes;
  using types::is_signed;
  int w = width_bytes(types::canonical(ty));
  switch (op) {
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::IMUL: {
      // 浮点加/减/乘（内联展开后会出现带浮点 ty 的这三类运算）：
      // 按元素精度计算再编码回位模式
      if (types::is_floating(ty)) {
        if (ty == TypeId::FLOAT) {
          float fa = (float)types::const_bits_to_double(a, ty);
          float fb = (float)types::const_bits_to_double(b, ty);
          float r = op == OpCode::ADD ? fa + fb
                                      : op == OpCode::SUB ? fa - fb : fa * fb;
          std::uint32_t bits;
          std::memcpy(&bits, &r, sizeof(bits));
          return (std::int64_t)bits;
        }
        double da = types::const_bits_to_double(a, ty);
        double db = types::const_bits_to_double(b, ty);
        double r = op == OpCode::ADD ? da + db
                                     : op == OpCode::SUB ? da - db : da * db;
        return types::double_to_const_bits(r, TypeId::DOUBLE);
      }
      if (op == OpCode::ADD)
        return (std::int64_t)((std::uint64_t)a + (std::uint64_t)b);
      if (op == OpCode::SUB)
        return (std::int64_t)((std::uint64_t)a - (std::uint64_t)b);
      return (std::int64_t)((std::uint64_t)a * (std::uint64_t)b);
    }
    case OpCode::IDIV:
    case OpCode::MOD: {
      if (b == 0) return std::nullopt;
      if (a == INT64_MIN && b == -1) return std::nullopt;
      if (w == 4 && a == INT32_MIN && b == -1) return std::nullopt;
      return op == OpCode::IDIV ? a / b : a % b;
    }
    case OpCode::UDIV:
    case OpCode::UMOD: {
      std::uint64_t ub = (std::uint64_t)b;
      if (ub == 0) return std::nullopt;
      if (w == 4) {
        std::uint32_t ua = (std::uint32_t)a, ub32 = (std::uint32_t)b;
        if (ub32 == 0) return std::nullopt;
        return op == OpCode::UDIV ? (std::int64_t)(ua / ub32)
                                  : (std::int64_t)(ua % ub32);
      }
      return op == OpCode::UDIV ? (std::int64_t)((std::uint64_t)a / ub)
                                : (std::int64_t)((std::uint64_t)a % ub);
    }
    case OpCode::FDIV: {
      if (ty == TypeId::FLOAT) {
        float fa = (float)types::const_bits_to_double(a, ty);
        float fb = (float)types::const_bits_to_double(b, ty);
        float r = fa / fb;
        std::uint32_t bits;
        std::memcpy(&bits, &r, sizeof(bits));
        return (std::int64_t)bits;
      }
      double r = types::const_bits_to_double(a, ty) /
                 types::const_bits_to_double(b, ty);
      return types::double_to_const_bits(r, TypeId::DOUBLE);
    }
    case OpCode::AND: return (a != 0 && b != 0) ? 1 : 0;
    case OpCode::OR: return (a != 0 || b != 0) ? 1 : 0;
    case OpCode::SAL: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) {
        std::uint32_t r = (std::uint32_t)a << b;
        return is_signed(ty) ? (std::int64_t)(std::int32_t)r : (std::int64_t)r;
      }
      return (std::int64_t)((std::uint64_t)a << b);
    }
    case OpCode::SAR: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) return (std::int64_t)((std::int32_t)a >> b);
      return a >> b;
    }
    case OpCode::LSR: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) return (std::int64_t)((std::uint32_t)a >> b);
      return (std::int64_t)((std::uint64_t)a >> b);
    }
    case OpCode::TRUNC_U8: return (std::int64_t)(std::uint8_t)a;
    case OpCode::TRUNC_S8: return (std::int64_t)(std::int8_t)a;
    case OpCode::TRUNC_U16: return (std::int64_t)(std::uint16_t)a;
    case OpCode::TRUNC_S16: return (std::int64_t)(std::int16_t)a;
    case OpCode::SEXT: return (std::int64_t)(std::int32_t)a;
    case OpCode::ZEXT: return (std::int64_t)(std::uint64_t)(std::uint32_t)a;
    case OpCode::FNEG: {
      if (ty == TypeId::FLOAT) {
        float f = (float)types::const_bits_to_double(a, ty);
        float r = -f;
        std::uint32_t bits;
        std::memcpy(&bits, &r, sizeof(bits));
        return (std::int64_t)bits;
      }
      return types::double_to_const_bits(
          -types::const_bits_to_double(a, ty), TypeId::DOUBLE);
    }
    case OpCode::SITOF: {
      double x = (double)(std::int32_t)a;
      return types::double_to_const_bits(x, ty);
    }
    case OpCode::UITOF: {
      double x = (double)(std::uint32_t)a;
      return types::double_to_const_bits(x, ty);
    }
    case OpCode::F2I: {
      double x = types::const_bits_to_double(a, ty);
      if (!(x == x) || x >= 2147483648.0 || x <= -2147483649.0)
        return std::nullopt;
      return (std::int64_t)(std::int32_t)x;
    }
    case OpCode::F2U: {
      double x = types::const_bits_to_double(a, ty);
      if (!(x == x) || x >= 4294967296.0 || x <= -1.0) return std::nullopt;
      return (std::int64_t)(std::uint32_t)x;
    }
    case OpCode::F2F: {
      TypeId src = (ty == TypeId::FLOAT) ? TypeId::DOUBLE : TypeId::FLOAT;
      return types::convert_const(a, src, ty);
    }
    default:
      return std::nullopt;
  }
}

struct Block {
  IRList::iterator begin;  // 首条指令（LABEL/FUNCTION_BEGIN 或普通指令）
  IRList::iterator end;    // 末条指令的下一位置
  std::string label;       // 以 LABEL 开头时非空
  std::vector<int> succs, preds;
};

// 在当前状态 m 下取操作数的常量值
bool operand_value(const Consts &m, const OpName &op, std::int64_t &out) {
  if (op.is_imm()) {
    out = op.value;
    return true;
  }
  if (is_local_scalar(op)) {
    auto it = m.find(op.name);
    if (it != m.end()) {
      out = it->second.first;
      return true;
    }
  }
  return false;
}

// 处理一条定值指令，更新常量状态 m
void update_state(Consts &m, const IR &inst) {
  if (!is_local_scalar(inst.dest)) return;
  const std::string &d = inst.dest.name;
  if (inst.op_code == OpCode::MOV) {
    std::int64_t v;
    if (operand_value(m, inst.op1, v))
      m[d] = {v, inst.ty.id};
    else
      m.erase(d);
    return;
  }
  if (inst.op_code == OpCode::PHI_MOV) {
    // PHI_MOV 在前驱块内定值，其值语义由汇合块入口决定，此处不作定值
    m.erase(d);
    return;
  }
  if (ir_fold::is_foldable_op(inst.op_code)) {
    std::int64_t a;
    if (!operand_value(m, inst.op1, a)) {
      m.erase(d);
      return;
    }
    if (ir_fold::is_unary_op(inst.op_code)) {
      auto r = ir_fold::fold(inst.op_code, inst.ty.id, a, 0);
      if (r)
        m[d] = {*r, inst.ty.id};
      else
        m.erase(d);
      return;
    }
    std::int64_t b;
    if (!operand_value(m, inst.op2, b)) {
      m.erase(d);
      return;
    }
    auto r = ir_fold::fold(inst.op_code, inst.ty.id, a, b);
    if (r)
      m[d] = {*r, inst.ty.id};
    else
      m.erase(d);
    return;
  }
  m.erase(d);
}

void substitute(Consts &m, OpName &op) {
  if (!op.is_var()) return;
  auto it = m.find(op.name);
  if (it != m.end()) op = OpName(it->second.first);
}

// 汇合块入口对 PHI 定值目的名的约束：所有前驱都经 PHI_MOV 给出同一常量
void apply_phi_contributions(const std::vector<Block> &blocks,
                             const std::vector<Consts> &out, int b,
                             Consts &in) {
  struct PhiSlot {
    bool seen = false;   // 已有任何前驱给出过贡献
    bool known = false;  // 目前所有贡献一致且为常量
    std::int64_t v = 0;
    TypeId ty = TypeId::INT;
  };
  // 收集指向 b 的所有 PHI_MOV：phi_block == b 的 LABEL（b.begin）
  std::map<std::string, PhiSlot> phi_values;
  for (int p : blocks[b].preds) {
    for (auto it = blocks[p].begin; it != blocks[p].end; it++) {
      if (it->op_code != OpCode::PHI_MOV) continue;
      if (!it->dest.is_var()) continue;
      if (it->phi_block != blocks[b].begin) continue;
      auto &slot = phi_values[it->dest.name];
      slot.ty = it->ty.id;
      std::int64_t v;
      bool is_const = operand_value(out[p], it->op1, v);
      if (!slot.seen) {
        slot.seen = true;
        slot.known = is_const;
        slot.v = v;
      } else if (slot.known) {
        if (!is_const || slot.v != v) slot.known = false;
      }
    }
  }
  for (auto &kv : phi_values) {
    // 该名字在 b 的任何前驱都没有 PHI_MOV 时不传播（沿旧值路径进入汇合点）
    bool defined_in_all_preds = true;
    for (int p : blocks[b].preds) {
      bool found = false;
      for (auto it = blocks[p].begin; it != blocks[p].end; it++) {
        if (it->op_code == OpCode::PHI_MOV && it->dest.is_var() &&
            it->dest.name == kv.first && it->phi_block == blocks[b].begin) {
          found = true;
          break;
        }
      }
      if (!found) {
        defined_in_all_preds = false;
        break;
      }
    }
    if (!defined_in_all_preds || !kv.second.known)
      in.erase(kv.first);
    else
      in[kv.first] = {kv.second.v, kv.second.ty};
  }
}

Consts transfer(const std::vector<Block> &blocks, const std::vector<Consts> &out,
                int b, const Consts &in) {
  Consts m = in;
  for (auto it = blocks[b].begin; it != blocks[b].end; it++) {
    update_state(m, *it);
  }
  return m;
}

}  // namespace

void constant_propagation(IRList &ir) {
  auto it = ir.begin();
  while (it != ir.end()) {
    if (it->op_code != OpCode::FUNCTION_BEGIN) {
      it++;
      continue;
    }
    auto func_end = std::next(it);  // FUNCTION_BEGIN 之后找 FUNCTION_END
    while (func_end != ir.end() && func_end->op_code != OpCode::FUNCTION_END)
      func_end++;

    // 划分基本块：LABEL、跳转/返回指令之后是块边界
    std::vector<Block> blocks;
    std::map<std::string, int> label_block;
    {
      auto b = it;
      std::string cur_label;
      for (auto i = it; i != func_end; i++) {
        if (i->op_code == OpCode::LABEL) {
          if (b != i) blocks.push_back({b, i, cur_label, {}, {}});
          b = i;
          cur_label = i->label;
        } else if (i->op_code == OpCode::JMP || is_jcc(i->op_code) ||
                   i->op_code == OpCode::RET) {
          blocks.push_back({b, std::next(i), cur_label, {}, {}});
          b = std::next(i);
          cur_label.clear();
        }
      }
      if (b != func_end) blocks.push_back({b, func_end, cur_label, {}, {}});
      for (int idx = 0; idx < (int)blocks.size(); idx++)
        if (!blocks[idx].label.empty()) label_block[blocks[idx].label] = idx;
    }
    // 边：跳转目标 + 顺序落空
    for (int idx = 0; idx < (int)blocks.size(); idx++) {
      auto last = std::prev(blocks[idx].end);
      if (last->op_code == OpCode::JMP || is_jcc(last->op_code)) {
        auto t = label_block.find(last->label);
        if (t != label_block.end()) blocks[idx].succs.push_back(t->second);
      }
      if (last->op_code != OpCode::JMP && last->op_code != OpCode::RET &&
          idx + 1 < (int)blocks.size())
        blocks[idx].succs.push_back(idx + 1);
      for (int s : blocks[idx].succs) blocks[s].preds.push_back(idx);
    }

    // worklist 数据流
    int n = (int)blocks.size();
    std::vector<Consts> in(n), out(n);
    std::vector<bool> visited(n, false);
    bool changed = true;
    while (changed) {
      changed = false;
      for (int b = 0; b < n; b++) {
        Consts new_in;
        bool any_pred_visited = false;
        for (int p : blocks[b].preds) {
          if (!visited[p]) continue;
          if (!any_pred_visited) {
            new_in = out[p];
            any_pred_visited = true;
          } else {
            // meet：两侧均知的同名同值常量保留
            Consts met;
            for (auto &kv : new_in) {
              auto f = out[p].find(kv.first);
              if (f != out[p].end() && f->second == kv.second)
                met.insert(kv);
            }
            new_in = std::move(met);
          }
        }
        if (!any_pred_visited && b != 0) {
          // 不可达（尚无前驱到达）：不处理
          continue;
        }
        apply_phi_contributions(blocks, out, b, new_in);
        Consts new_out = transfer(blocks, out, b, new_in);
        if (!visited[b] || new_in != in[b] || new_out != out[b]) {
          in[b] = new_in;
          out[b] = new_out;
          visited[b] = true;
          changed = true;
        }
      }
    }

    // 重写：代入常量操作数、折叠 CMP + 相邻条件跳转/条件搬数
    for (int b = 0; b < n; b++) {
      if (!visited[b]) continue;
      Consts m = in[b];
      auto i = blocks[b].begin;
      while (i != blocks[b].end) {
        IR &inst = *i;
        if (inst.op_code == OpCode::CMP) {
          substitute(m, inst.op1);
          substitute(m, inst.op2);
          std::int64_t a, c;
          if (operand_value(m, inst.op1, a) && operand_value(m, inst.op2, c) &&
              i != std::prev(blocks[b].end)) {
            auto nx = std::next(i);
            OpCode cc = nx->op_code;
            bool fp = types::is_floating(inst.ty.id);
            bool nan = fp && (std::isnan(types::const_bits_to_double(a, inst.ty.id)) ||
                              std::isnan(types::const_bits_to_double(c, inst.ty.id)));
            if ((is_jcc(cc) || is_movcc(cc)) && !nan) {
              bool cond = eval_cond(cc, inst.ty.id, a, c);
              if (is_jcc(cc)) {
                if (cond) {
                  nx->op_code = OpCode::JMP;
                  i = ir.erase(i);  // i 指向 JMP，跳过它
                  i++;
                } else {
                  i = ir.erase(i, std::next(nx));
                }
                continue;
              }
              // MOVcc：dest = cond ? op1 : op2
              OpName chosen = cond ? nx->op1 : nx->op2;
              if (chosen.is_var()) substitute(m, chosen);
              nx->op_code = OpCode::MOV;
              nx->op1 = chosen;
              nx->op2 = OpName();
              nx->op3 = OpName();
              i = ir.erase(i);  // i 指向 MOV，交由下一轮正常处理
              continue;
            }
          }
          i++;
          continue;
        }
        if (inst.op_code == OpCode::PHI_MOV) {
          // PHI_MOV 的操作数带块边界语义（后端按“op1 为变量”做 phi 合并
          // 与活跃区间计算），不代入立即数，只更新状态
          update_state(m, inst);
          i++;
          continue;
        }
        substitute(m, inst.op1);
        substitute(m, inst.op2);
        substitute(m, inst.op3);
        update_state(m, inst);
        i++;
      }
    }

    it = func_end;
    it++;
  }
}
}  // namespace syc::ir::passes
