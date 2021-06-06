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
#include "ir/optimize/passes/dead_code_elimination.h"

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"

namespace syc::ir::passes {
void dead_code_elimination(IRList &ir) {
  syc::assembly::Context ctx(&ir, ir.begin());
  for (auto it = ir.begin(); it != ir.end(); it++) {
    ctx.set_ir_timestamp(*it);
  }

  for (auto it = std::prev(ir.end()); it != ir.begin(); it--) {
    ctx.set_var_latest_use_timestamp(*it);
    auto cur = ctx.ir_to_time[&*it];
    if (it->dest.is_var() && it->dest.name[0] == '%' &&
        it->op_code != OpCode::CALL && it->op_code != OpCode::PHI_MOV) {
      if (ctx.var_latest_use_timestamp.find(it->dest.name) ==
              ctx.var_latest_use_timestamp.end() ||
          ctx.var_latest_use_timestamp[it->dest.name] <= cur) {
        it = ir.erase(it);
      }
    }
  }

  for (auto it = ir.begin(); it != ir.end(); it++) {
    ctx.set_var_define_timestamp(*it);
  }
}
}  // namespace syc::ir::passes
