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
#include "ir/optimize/passes/branch_normalize_elimination.h"

#include <map>

namespace syc::ir::passes {
namespace {

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

// 条件码取反（ARM 条件码精确闭合，含浮点无序语义）
OpCode invert_cc(OpCode cc) {
  switch (cc) {
    case OpCode::MOVEQ: return OpCode::MOVNE;
    case OpCode::MOVNE: return OpCode::MOVEQ;
    case OpCode::MOVLT: return OpCode::MOVGE;
    case OpCode::MOVGE: return OpCode::MOVLT;
    case OpCode::MOVLE: return OpCode::MOVGT;
    case OpCode::MOVGT: return OpCode::MOVLE;
    case OpCode::MOVLO: return OpCode::MOVHS;
    case OpCode::MOVHS: return OpCode::MOVLO;
    case OpCode::MOVLS: return OpCode::JMPI;
    case OpCode::MOVHI: return OpCode::JMPLS;
    default: return OpCode::JMP;
  }
}

OpCode to_jcc(OpCode cc) {
  switch (cc) {
    case OpCode::MOVEQ: return OpCode::JEQ;
    case OpCode::MOVNE: return OpCode::JNE;
    case OpCode::MOVLT: return OpCode::JLT;
    case OpCode::MOVGE: return OpCode::JGE;
    case OpCode::MOVLE: return OpCode::JLE;
    case OpCode::MOVGT: return OpCode::JGT;
    case OpCode::MOVLO: return OpCode::JMPLO;
    case OpCode::MOVHS: return OpCode::JMPHS;
    case OpCode::MOVLS: return OpCode::JMPLS;
    case OpCode::MOVHI: return OpCode::JMPI;
    default: return OpCode::JMP;
  }
}

}  // namespace

void branch_normalize_elimination(IRList &ir) {
  // 条件真值临时变量的使用计数（排除 dest）
  std::map<std::string, int> use_count;
  for (const auto &inst : ir) {
    inst.forEachOp(
        [&](const OpName &op) {
          if (op.is_var()) use_count[op.name]++;
        },
        false);
  }

  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (!is_movcc(it->op_code)) continue;
    if (!it->dest.is_var() || !it->op1.is_imm() || !it->op2.is_imm())
      continue;
    // t = cc ? op1 : op2，仅处理 0/1 真值形式
    bool positive, negative;
    positive = it->op1.value == 1 && it->op2.value == 0;   // t = cc
    negative = it->op1.value == 0 && it->op2.value == 1;   // t = !cc
    if (!positive && !negative) continue;
    const std::string &t = it->dest.name;
    if (use_count[t] != 1) continue;
    if (it == ir.begin()) continue;
    auto cmp1 = std::prev(it);
    if (cmp1->op_code != OpCode::CMP) continue;
    auto cmp2 = std::next(it);
    if (cmp2 == ir.end() || cmp2->op_code != OpCode::CMP) continue;
    if (!(cmp2->op1.is_var() && cmp2->op1.name == t && cmp2->op2.is_imm() &&
          cmp2->op2.value == 0))
      continue;
    auto j = std::next(cmp2);
    if (j == ir.end() || !is_jcc(j->op_code)) continue;
    if (j->op_code != OpCode::JEQ && j->op_code != OpCode::JNE) continue;

    // 第二个条件：JEQ 分支当 t==0（即真值为假），JNE 分支当真值为真
    bool branch_on_true = j->op_code == OpCode::JNE;
    // positive: 真值 = cc；negative: 真值 = !cc
    bool branch_on_cc = positive ? branch_on_true : !branch_on_true;
    OpCode src_cc = it->op_code;
    OpCode target = to_jcc(branch_on_cc ? src_cc : invert_cc(src_cc));

    it->op_code = target;
    it->label = j->label;
    it->dest = OpName();
    it->op1 = OpName();
    it->op2 = OpName();
    it->op3 = OpName();
    it = ir.erase(cmp2);  // 指向旧 Jcc
    it = ir.erase(it);    // 指向 Jcc 之后
    it--;
  }
}
}  // namespace syc::ir::passes
