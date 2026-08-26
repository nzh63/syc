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

#include "ast/type.h"
#include "ir/generate/context.h"

namespace syc::ir {
// 运行期转换：把值规范化成目标类型在槽位中的规范表示。
//
// 本后端约定：所有宽度 <= 4 字节的整数值都以「按声明类型扩展到 32 位」
// 的规范形式存放在单个 32 位槽位中；8 字节（long long）值占用一对槽位
// （寄存器对或两个连续 4 字节栈槽），按 64 位补码解释。因此：
//   - 同宽度类之间的转换（如 int <-> unsigned、char -> int）无需任何动作，
//     解释差异由比较/除法类操作码的选取体现；
//   - 8 字节 -> 更窄：取低 32 位后按目标宽度截断（MOV + TRUNC_*）；
//   - 更窄 -> 8 字节：按源类型的符号性做 SEXT / ZEXT；
//   - 源类型的符号性决定扩展方式，因此调用方必须提供 src。
// 立即数版本直接折叠为转换后的立即数。
OpName convert_runtime(OpName v, types::TypeId src, types::TypeId dst,
                       Context& ctx, IRList& ir);
}  // namespace syc::ir
