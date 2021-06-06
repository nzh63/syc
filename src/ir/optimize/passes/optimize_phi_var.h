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
#pragma once
#include "ir/ir.h"

namespace syc::ir::passes {
// 移除不必要的PHI_MOV指令
// 如：
// MOV %42, some_thing
// PHI_MOV %43, %42     # %42 only be used here
// =>
// MOV %43, some_thing  # rename %42 to %43
// PHI_MOV %43, %43
void optimize_phi_var(IRList &ir);
}  // namespace syc::ir::passes
