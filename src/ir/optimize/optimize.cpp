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
#include "ir/optimize/optimize.h"

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"
#include "ir/optimize/passes.h"

namespace syc::ir {
void optimize(IRList &ir) {
  using namespace syc::ir::passes;
  for (int i = 0; i < 2; i++) {
    local_common_subexpression_elimination(ir);
    local_common_constexpr_function_elimination(ir);
    optimize_phi_var(ir);
    dead_code_elimination(ir);
    unreachable_code_elimination(ir);
  }
}

void optimize_loop_ir(IRList &ir_before, IRList &ir_cond, IRList &ir_jmp,
                      IRList &ir_do, IRList &ir_continue) {
  using namespace syc::ir::passes;
  if (config::optimize_level > 0) {
    while (loop_invariant_code_motion(ir_before, ir_cond, ir_jmp, ir_do,
                                      ir_continue))
      ;
  }
}
}  // namespace syc::ir