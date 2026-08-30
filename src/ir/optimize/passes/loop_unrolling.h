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
// 循环展开：识别「常量初值/步长/边界」的计数循环，按与运行时一致的
// NZCV 语义模拟出迭代次数后完全展开为直线代码（含 0 次迭代时删除循环）。
// 归纳变量在每份副本中替换为立即数，交给常量传播继续折叠。
void loop_unrolling(IRList &ir);
}  // namespace syc::ir::passes
