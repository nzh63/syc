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

#include <istream>
#include <ostream>

namespace syc::assembly::passes {
// 使用LIS算法对指令进行重排
// 请参考论文：X. Shi and P. Guo, "A Novel Lightweight Instruction Scheduling
// Algorithm for Just-in-Time Compiler," 2009 WRI World Congress on Software
// Engineering, 2009, pp. 73-77, doi: 10.1109/WCSE.2009.39.
void reorder(std::istream& in, std::ostream& out);
}  // namespace syc::assembly::passes
