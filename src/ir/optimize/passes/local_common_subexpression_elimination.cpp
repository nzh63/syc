/*
 * syc, a compiler for SysY
 * Copyright (C) 2020  nzh63, skywf21
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
#include "ir/optimize/passes/local_common_subexpression_elimination.h"

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"
#include "set"

namespace syc::ir::passes {
namespace {
struct compare_ir {
  bool operator()(const std::pair<IR, int> &_a,
                  const std::pair<IR, int> &_b) const {
    const auto &a = _a.first, &b = _b.first;
    if (a.op_code != b.op_code) return a.op_code < b.op_code;

#define F(op1)                                                  \
  if (a.op1.type != b.op1.type) return a.op1.type < b.op1.type; \
  if (a.op1.is_var() && a.op1.name != b.op1.name)               \
    return a.op1.name < b.op1.name;                             \
  if (a.op1.is_imm() && a.op1.value != b.op1.value)             \
    return a.op1.value < b.op1.value;
    F(op1)
    F(op2)
    F(op3)
#undef F
    return false;
  }
};
}  // namespace

void local_common_subexpression_elimination(IRList &ir) {
  std::set<std::pair<IR, int>, compare_ir> maybe_opt;
  std::set<std::string> mutability_var;

  syc::assembly::Context ctx(&ir, ir.begin());
  for (auto it = ir.begin(); it != ir.end(); it++) {
    ctx.set_ir_timestamp(*it);
  }

  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code != OpCode::PHI_MOV) {
      mutability_var.insert(it->dest.name);
    }
    if (it->op_code != OpCode::LABEL || it->op_code != OpCode::FUNCTION_BEGIN) {
      maybe_opt.clear();
    }
    if (it->dest.is_var() && it->dest.name[0] == '%' && !it->op1.is_null() &&
        !it->op2.is_null() && it->op_code != OpCode::MOVEQ &&
        it->op_code != OpCode::MOVNE && it->op_code != OpCode::MOVGT &&
        it->op_code != OpCode::MOVGE && it->op_code != OpCode::MOVLT &&
        it->op_code != OpCode::MOVLE &&
        it->op_code != OpCode::MALLOC_IN_STACK && it->op_code != OpCode::LOAD &&
        it->op_code != OpCode::MOV && it->op_code != OpCode::PHI_MOV) {
      if (maybe_opt.find({*it, 0}) != maybe_opt.end()) {
        auto opt_ir = maybe_opt.find({*it, 0})->first;
        auto time = maybe_opt.find({*it, 0})->second;
        if (mutability_var.find(opt_ir.dest.name) == mutability_var.end()) {
          // 避免相距太远子表达式有望延长生命周期而溢出
          if (ctx.ir_to_time[&*it] - time < 20) {
            IR temp(OpCode::MOV, it->dest, opt_ir.dest);
            it = ir.erase(it);
            it = ir.insert(it, temp);
            it--;
          }
        }
      }
      maybe_opt.insert({*it, ctx.ir_to_time[&*it]});
    }
  }
}

}  // namespace syc::ir::passes
