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
// 自递归尾调用消除：函数体内 `CALL 自身` 紧跟 `RET`（返回值即调用结果，
// 或无值返回）时改写为「重设实参 + JMP 到函数入口」，把尾递归变成循环，
// 省去调用/返回开销。入口标号由本 pass 插入到 FUNCTION_BEGIN 之后。
void tail_call_elimination(IRList &ir);
}  // namespace syc::ir::passes
