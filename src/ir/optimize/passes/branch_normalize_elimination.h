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
#pragma once
#include "ir/ir.h"

namespace syc::ir::passes {
// 比较规范化折叠：`a CMP b; MOVcc t,1,0; ...; CMP t,0; Jcc' L` 中，
// 当 t 只被第二个 CMP 使用时，合并为 `a CMP b; J<cc'/取反> L`，
// 删去中间的规范化与再比较（每个 if/比较判断都产生此模式）。
// ARM 条件码在取反下精确闭合（含浮点无序语义），变换是精确的。
void branch_normalize_elimination(IRList &ir);
}  // namespace syc::ir::passes
