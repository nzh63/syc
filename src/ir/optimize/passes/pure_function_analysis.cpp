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
#include "ir/optimize/passes/pure_function_analysis.h"

#include <map>
#include <vector>

namespace syc::ir::passes {
std::set<std::string> analyze_pure_functions(const IRList &ir) {
  std::set<std::string> function_names;
  // 每个函数记录：是否引用全局、是否做了不安全的 STORE（基址不是本栈帧
  // 内的 MALLOC_IN_STACK 数组 %&…，例如经指针写调用方内存或写全局）、
  // 以及被调函数名列表
  struct FuncInfo {
    bool touches_global = false;
    bool unsafe_store = false;
    bool calls_external = false;
    std::vector<std::string> callees;
  };
  std::map<std::string, FuncInfo> info;
  std::string current;
  for (const auto &inst : ir) {
    if (inst.op_code == OpCode::FUNCTION_BEGIN) {
      current = inst.label;
      info[current];
    } else if (inst.op_code == OpCode::FUNCTION_END) {
      current.clear();
      continue;
    }
    if (current.empty()) continue;
    auto &fi = info[current];
    if (inst.some(&OpName::is_global_var)) fi.touches_global = true;
    if (inst.op_code == OpCode::STORE) {
      // STORE op1[op2] = op3：只允许写本栈帧内的数组（基址为 %& 开头，
      // 即 MALLOC_IN_STACK 分配的局部数组）
      if (!(inst.op1.is_var() && inst.op1.name[0] == '%' &&
            inst.op1.name.substr(0, 2) == "%&"))
        fi.unsafe_store = true;
    }
    if (inst.op_code == OpCode::CALL) fi.callees.push_back(inst.label);
  }

  // 调用图中所有用户函数名；CALL 的 callee 不在其中即为外部/native 函数
  for (auto &kv : info) function_names.insert(kv.first);

  // 不动点：假设全纯，逐步剔除
  std::set<std::string> pure = function_names;
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto &kv : info) {
      if (pure.find(kv.first) == pure.end()) continue;
      bool is_pure = !kv.second.touches_global && !kv.second.unsafe_store &&
                     !kv.second.calls_external;
      if (is_pure) {
        for (auto &callee : kv.second.callees) {
          if (function_names.count(callee) == 0 ||
              pure.find(callee) == pure.end()) {
            is_pure = false;
            break;
          }
        }
      }
      if (!is_pure) {
        pure.erase(kv.first);
        changed = true;
      }
    }
  }
  return pure;
}
}  // namespace syc::ir::passes
