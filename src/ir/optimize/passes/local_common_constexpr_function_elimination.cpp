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
#include "ir/optimize/passes/local_common_constexpr_function_elimination.h"

#include <set>
#include <string>
#include <unordered_map>

#include "assembly/generate/context.h"
#include "config.h"
#include "ir/ir.h"

namespace syc::ir::passes {
namespace {
bool is_constexpr_function(const IRList &irs, IRList::const_iterator begin,
                           IRList::const_iterator end) {
  for (auto it = begin; it != end; it++) {
    auto &ir = *it;
    if (ir.some(&OpName::is_global_var)) {
      return false;
    }
    if (ir.op_code == OpCode::INFO && ir.label == "NOT CONSTEXPR") {
      return false;
    }
  }
  return true;
}

std::set<std::string> find_constexpr_function(const IRList &irs) {
  std::set<std::string> ret;
  IRList::const_iterator function_begin_it;
  for (auto outter_it = irs.begin(); outter_it != irs.end(); outter_it++) {
    auto &ir = *outter_it;
    if (ir.op_code == OpCode::FUNCTION_BEGIN) {
      function_begin_it = outter_it;
    } else if (ir.op_code == OpCode::FUNCTION_END) {
      if (is_constexpr_function(irs, function_begin_it, outter_it)) {
        ret.insert(function_begin_it->label);
      }
    }
  }
  return ret;
}
void _local_common_constexpr_function_elimination(
    IRList &ir, const std::set<std::string> &constexpr_function) {
  typedef std::unordered_map<int, OpName> CallArgs;
  std::unordered_map<std::string, std::vector<std::pair<CallArgs, std::string>>>
      calls;
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::LABEL || it->op_code == OpCode::FUNCTION_BEGIN) {
      calls.clear();
    }
    if (it->op_code == OpCode::CALL) {
      CallArgs args;
      auto function_name = it->label;
      if (constexpr_function.find(function_name) == constexpr_function.end()) {
        continue;
      }
      auto it2 = std::prev(it);
      while (it2->op_code == OpCode::SET_ARG) {
        args.insert({it2->dest.value, it2->op1});
        it2--;
      }
      bool can_optimize = true;
      for (auto kv : args) {
        if (kv.second.is_var()) {
          if (kv.second.name[0] != '%') {
            can_optimize = false;
            break;
          }
          if (kv.second.name.substr(0, 2) == "%&") {
            can_optimize = false;
            break;
          }
        }
      }
      if (!can_optimize) continue;
      bool has_same_call = false;
      auto eq = [](const OpName &a, const OpName &b) -> bool {
        if (a.type != b.type) return false;
        if (a.is_imm()) return a.value == b.value;
        if (a.is_var()) return a.name == b.name;
        return true;
      };
      std::string prev_call_result;
      for (const auto &prev_call : calls[function_name]) {
        bool same = true;
        const auto &prev_call_args = prev_call.first;
        prev_call_result = prev_call.second;
        for (auto &kv : args) {
          if (prev_call_args.find(kv.first) == prev_call_args.end()) {
            same = false;
            break;
          }
          if (!eq(kv.second, prev_call_args.at(kv.first))) {
            same = false;
            break;
          }
        }
        if (same) {
          has_same_call = true;
          break;
        }
      }
      if (!has_same_call) {
        if (it->dest.is_var()) {
          calls[function_name].push_back({args, it->dest.name});
        }
      } else {
        if (it->dest.is_var()) {
          it->op_code = OpCode::MOV;
          it->op1 = OpName(prev_call_result);
          it->op2 = OpName();
          it->op3 = OpName();
          it->label.clear();
        }
      }
    }
  }
}
}  // namespace
void local_common_constexpr_function_elimination(IRList &ir) {
  return _local_common_constexpr_function_elimination(
      ir, find_constexpr_function(ir));
}

}  // namespace syc::ir::passes
