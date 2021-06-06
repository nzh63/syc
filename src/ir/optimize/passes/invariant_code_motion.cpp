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
#include "ir/optimize/passes/invariant_code_motion.h"

#include <set>

#include "config.h"
#include "ir/ir.h"

namespace syc::ir::passes {
bool loop_invariant_code_motion(IRList &ir_before, IRList &ir_cond,
                                IRList &ir_jmp, IRList &ir_do,
                                IRList &ir_continue) {
  std::set<std::string> never_write_var;
  bool do_optimize = false;
  for (auto irs :
       std::vector<IRList *>({&ir_cond, &ir_jmp, &ir_do, &ir_continue})) {
    for (auto &ir : *irs) {
      if (ir.op_code == OpCode::LOAD || ir.op_code == OpCode::CALL) {
        continue;
      }
#define F(op)                           \
  if (ir.op.is_var()) {                 \
    never_write_var.insert(ir.op.name); \
  }
      F(op1);
      F(op2);
      F(op3);
#undef F
    }
  }
  for (auto irs :
       std::vector<IRList *>({&ir_cond, &ir_jmp, &ir_do, &ir_continue})) {
    for (auto &ir : *irs) {
      if (ir.dest.is_var()) {
        never_write_var.erase(ir.dest.name);
      }
    }
  }
  for (auto irs :
       std::vector<IRList *>({&ir_cond, &ir_jmp, &ir_do, &ir_continue})) {
    for (auto &ir : *irs) {
      bool can_optimize = true;
      if (ir.op_code == OpCode::LOAD || ir.op_code == OpCode::CALL ||
          ir.op_code == OpCode::PHI_MOV || ir.op_code == OpCode::LABEL ||
          ir.op_code == OpCode::CMP || ir.op_code == OpCode::MOVEQ ||
          ir.op_code == OpCode::MOVNE || ir.op_code == OpCode::MOVGT ||
          ir.op_code == OpCode::MOVGE || ir.op_code == OpCode::MOVLT ||
          ir.op_code == OpCode::MOVLE || ir.op_code == OpCode::NOOP) {
        can_optimize = false;
      }
#define F(op)                                                      \
  if (ir.op.is_var() &&                                            \
      never_write_var.find(ir.op.name) == never_write_var.end()) { \
    can_optimize = false;                                          \
  }
      F(op1);
      F(op2);
      F(op3);
#undef F
      if (!ir.dest.is_var()) {
        can_optimize = false;
      }
      if (can_optimize) {
        ir_before.push_back(ir);
        ir.op_code = OpCode::NOOP;
        ir.dest = OpName();
        ir.op1 = OpName();
        ir.op2 = OpName();
        ir.op3 = OpName();
        do_optimize = true;
      }
    }
  }
  return do_optimize;
}
}  // namespace syc::ir::passes
