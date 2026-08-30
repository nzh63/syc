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
// 小函数内联：把体量小、不含 CALL 且所有 RET 都带回返回值的用户函数
// 复制到调用点（实参经 SET_ARG 槽位对应替换，局部名字加前缀重命名，
// PHI_MOV 的 phi_block 重新映射到复制体内的 LABEL），随后由主循环中的
// 常量传播/DCE 消化。内联后调用点的 SET_ARG 与 CALL 一并删除。
void inline_small_functions(IRList &ir);
}  // namespace syc::ir::passes
