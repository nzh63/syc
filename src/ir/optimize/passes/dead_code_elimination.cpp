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
#include "ir/optimize/passes/dead_code_elimination.h"

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"
#include "ir/optimize/passes/pure_function_analysis.h"

namespace syc::ir::passes {
void dead_code_elimination(IRList &ir) {
  auto pure_functions = analyze_pure_functions(ir);
  syc::assembly::Context ctx(&ir, ir.begin());
  for (auto it = ir.begin(); it != ir.end(); it++) {
    ctx.set_ir_timestamp(*it);
  }

  for (auto it = std::prev(ir.end()); it != ir.begin(); it--) {
    ctx.set_var_latest_use_timestamp(*it);
    auto cur = ctx.ir_to_time[&*it];
    if (it->op_code == OpCode::CALL) {
      // 纯函数调用：结果无人使用（或无结果）时可整体删除，
      // 连同为其准备的 SET_ARG 参数序列
      bool dest_dead = !it->dest.is_var() ||
                       (it->dest.name[0] == '%' &&
                        (ctx.var_latest_use_timestamp.find(it->dest.name) ==
                             ctx.var_latest_use_timestamp.end() ||
                         ctx.var_latest_use_timestamp[it->dest.name] <= cur));
      if (dest_dead && pure_functions.count(it->label)) {
        auto first_arg = it;
        while (first_arg != ir.begin() &&
               std::prev(first_arg)->op_code == OpCode::SET_ARG)
          first_arg--;
        it = ir.erase(first_arg, std::next(it));
        continue;
      }
    }
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
