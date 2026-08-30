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
// 稠密常量传播：按 LABEL/跳转指令划分基本块，做 worklist 数据流分析，
// 标量局部变量（% 开头、非 %&）取常量时沿路径传播；全常量运算直接折叠，
// CMP + 相邻条件跳转/条件搬数在两侧常量时折叠成 JMP/删除/MOV。
// LOAD 结果不视为常量（无别名分析），IDIV/MOD/UDIV/UMOD 除零不折叠。
void constant_propagation(IRList &ir);
}  // namespace syc::ir::passes
