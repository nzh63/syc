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
#include "ir/optimize/passes/unreachable_code_elimination.h"

#include <list>
#include <set>
#include <unordered_map>

#include "config.h"
#include "ir/ir.h"

namespace syc::ir::passes {
void unreachable_code_elimination(IRList &ir) {
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::RET || it->op_code == OpCode::JMP) {
      for (auto next = std::next(it); next != ir.end();) {
        if (next->op_code != OpCode::LABEL &&
            next->op_code != OpCode::FUNCTION_END) {
          if (next->op_code != OpCode::NOOP && next->op_code != OpCode::INFO)
            next = ir.erase(next);
          else
            next++;
        } else {
          break;
        }
      }
    }
  }
}
}  // namespace syc::ir::passes
