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
#include "ir/optimize/passes/optimize_phi_var.h"

#include <list>
#include <set>
#include <unordered_map>

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"

namespace syc::ir::passes {
// MOV %42, some_thing
// ...
// PHI_MOV %43, %42     # %42 only be used here
// =>
// MOV %43, some_thing  # rename %42 to %43
// ...
// PHI_MOV %43, %43
void optimize_phi_var(IRList &ir) {
  std::map<std::string, int> use_count;
  std::map<std::string, std::string> replace_table;
  for (const auto &i : ir) {
#define F(op1)                                         \
  if (i.op1.is_var()) {                                \
    if (use_count.find(i.op1.name) == use_count.end()) \
      use_count[i.op1.name] = 0;                       \
    use_count[i.op1.name]++;                           \
  }
    F(op1)
    F(op2)
    F(op3)
#undef F
  }
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::PHI_MOV) {
      if (it->op1.is_var() && use_count[it->op1.name] == 1) {
        int line = 10;
        if (it == ir.begin()) continue;
        auto it2 = it;
        do {
          it2 = std::prev(it2);
          if (it2->dest.is_var() && it2->dest.name == it->op1.name) {
            replace_table[it2->dest.name] = it->dest.name;
          }
        } while (it2 != ir.begin());
      }
    }
  }
  for (auto &i : ir) {
#define F(op1)                                                 \
  if (i.op1.is_var()) {                                        \
    if (replace_table.find(i.op1.name) != replace_table.end()) \
      i.op1.name = replace_table[i.op1.name];                  \
  }
    F(dest)
    F(op1)
    F(op2)
    F(op3)
#undef F
  }
}

}  // namespace syc::ir::passes
