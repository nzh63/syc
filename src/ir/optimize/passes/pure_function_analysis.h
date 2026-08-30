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
#include <set>
#include <string>

#include "ir/ir.h"

namespace syc::ir::passes {
// 跨过程纯函数分析：纯函数 = 只读写自身的局部/栈内存（形参标量与
// MALLOC_IN_STACK 数组）、不引用全局变量、不写指针指向的外部内存、
// 且只调用其他纯函数（外部/native 一律视为不纯）。结果可传递。
std::set<std::string> analyze_pure_functions(const IRList &ir);
}  // namespace syc::ir::passes
