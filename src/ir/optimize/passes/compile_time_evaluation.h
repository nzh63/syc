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
// 编译期求值（IR 解释器），统一规则：
//   一个函数调用若所有标量实参都是常量，且被调函数（传递地）
//   ① 不读输入（getint/getch/getarray 等输入 native），
//   ② 不经指针实参读内存、只读「全程序从未被写过」的全局变量，
//   ③ 步数/深度/副作用数量在预算内，
// 则在编译期解释执行整个调用，并把可观察副作用按执行顺序重放为
// 真实指令（常量实参的输出 native 调用、对全局/指针实参内存的
// 常量 STORE），返回值替换为常量。规则同样适用于程序入口对 main
// 的隐式零参调用——顶层之外不可观察的副作用（内存写入）被丢弃。
// 任何前提不满足即放弃该调用点，留待运行时。
void constexpr_call_evaluation(IRList &ir);
void entry_point_evaluation(IRList &ir);
}  // namespace syc::ir::passes
