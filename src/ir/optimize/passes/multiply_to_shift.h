/*
 * syc, a compiler for SysY
 * Copyright (C) 2020-2021  nzh21, skywf21
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
// 乘法强度削减：32 位 IMUL 乘以 2 的幂（常见于数组下标 ×元素大小）
// 改写为 SAL（后端为单周期 LSL 立即数移位，替代多周期 MUL）。
// 64 位乘法走 __aeabi_lmul，不在此处理。
void multiply_to_shift(IRList &ir);
}  // namespace syc::ir::passes
