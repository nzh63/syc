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
#include "ir/optimize/passes/tail_call_elimination.h"

#include <set>

namespace syc::ir::passes {
void tail_call_elimination(IRList &ir) {
  // 预扫描：找出存在「CALL 自身 + RET（返回调用结果或无值）」的函数
  std::set<std::string> tco_functions;
  std::string current;
  for (const auto &inst : ir) {
    if (inst.op_code == OpCode::FUNCTION_BEGIN) {
      current = inst.label;
    } else if (inst.op_code == OpCode::FUNCTION_END) {
      current.clear();
    } else if (!current.empty() && inst.op_code == OpCode::CALL &&
               inst.label == current) {
      tco_functions.insert(current);
    }
  }

  current.clear();
  std::string entry_label;
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::FUNCTION_BEGIN) {
      current = it->label;
      if (tco_functions.count(current)) {
        // 在函数入口插入标号，供尾调用回跳
        entry_label = ".L.TCO_" + current + "_entry";
        IRList lbl;
        lbl.emplace_back(OpCode::LABEL, entry_label);
        ir.splice(std::next(it), lbl);
        it++;
      } else {
        entry_label.clear();
      }
    } else if (it->op_code == OpCode::FUNCTION_END) {
      current.clear();
      continue;
    }
    if (current.empty() || entry_label.empty()) continue;
    if (it->op_code != OpCode::CALL || it->label != current) continue;
    auto next = std::next(it);
    if (next == ir.end() || next->op_code != OpCode::RET) continue;
    // RET 的返回值必须就是调用结果，或是无值返回
    bool tail = next->op1.is_null() ||
                (next->op1.is_var() && it->dest.is_var() &&
                 next->op1.name == it->dest.name);
    if (!tail) continue;
    // 删去尾部 RET，把 CALL 本身改写为 JMP 入口（其前的 SET_ARG
    // 实参序列保持原样）；紧随其后的附加 RET 等死代码交给 UCE
    ir.erase(next);
    it->op_code = OpCode::JMP;
    it->label = entry_label;
    it->dest = OpName();
    it->op1 = OpName();
    it->op2 = OpName();
    it->op3 = OpName();
  }
}
}  // namespace syc::ir::passes
