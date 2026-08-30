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
#include "ir/optimize/passes/inline_small_functions.h"

#include <cstdlib>
#include <map>
#include <vector>

namespace syc::ir::passes {
namespace {
// 被内联函数体的最大指令数
constexpr int kMaxInlineSize = 32;

// 收集所有函数区域：name -> [FUNCTION_BEGIN, FUNCTION_END]
std::map<std::string, std::pair<IRList::iterator, IRList::iterator>>
collect_functions(IRList &ir) {
  std::map<std::string, std::pair<IRList::iterator, IRList::iterator>> funcs;
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::FUNCTION_BEGIN)
      funcs[it->label].first = it;
    else if (it->op_code == OpCode::FUNCTION_END)
      funcs[it->label].second = it;
  }
  return funcs;
}

// 局部名字（%xxx / %&xxx）加前缀重命名；其余（全局 @、形参槽 $arg/$farg）
// 保持不变
void rename_locals(IR &inst, const std::string &prefix) {
  auto rename = [&](OpName &op) {
    if (!op.is_var()) return;
    if (op.name.compare(0, 2, "%&") == 0)
      op.name = "%&" + prefix + op.name.substr(2);
    else if (op.name[0] == '%')
      op.name = "%" + prefix + op.name.substr(1);
  };
  rename(inst.dest);
  rename(inst.op1);
  rename(inst.op2);
  rename(inst.op3);
}

// 复制体内的控制流标号（LABEL/JMP/Jcc 的 label 字段）加前缀，避免与
// 原函数或多次内联副本的标号重名
void rename_labels(IR &inst, const std::string &prefix) {
  switch (inst.op_code) {
    case OpCode::LABEL:
    case OpCode::JMP:
    case OpCode::JEQ:
    case OpCode::JNE:
    case OpCode::JLE:
    case OpCode::JLT:
    case OpCode::JGE:
    case OpCode::JGT:
    case OpCode::JMPHS:
    case OpCode::JMPLO:
    case OpCode::JMPLS:
    case OpCode::JMPI:
      if (!inst.label.empty()) inst.label = ".L." + prefix + inst.label;
      break;
    default:
      break;
  }
}

// $argN / $fargN 槽位替换为调用点实参；槽位缺失时返回 false
bool substitute_args(IR &inst, const std::map<int, OpName> &core_args,
                     const std::map<int, OpName> &fp_args) {
  bool ok = true;
  auto subst = [&](OpName &op) {
    if (!op.is_var()) return;
    if (op.name.compare(0, 5, "$farg") == 0) {
      int slot = std::atoi(op.name.c_str() + 5);
      auto f = fp_args.find(slot);
      if (f != fp_args.end())
        op = f->second;
      else
        ok = false;
    } else if (op.name.compare(0, 4, "$arg") == 0) {
      int slot = std::atoi(op.name.c_str() + 4);
      auto f = core_args.find(slot);
      if (f != core_args.end())
        op = f->second;
      else
        ok = false;
    }
  };
  subst(inst.op1);
  subst(inst.op2);
  subst(inst.op3);
  subst(inst.dest);
  return ok;
}
}  // namespace

void inline_small_functions(IRList &ir) {
  auto funcs = collect_functions(ir);
  // 收集所有调用点及其所在函数
  struct CallSite {
    IRList::iterator call;
    std::string caller;
  };
  std::vector<CallSite> sites;
  std::string current;
  for (auto it = ir.begin(); it != ir.end(); it++) {
    if (it->op_code == OpCode::FUNCTION_BEGIN)
      current = it->label;
    else if (it->op_code == OpCode::FUNCTION_END)
      current.clear();
    else if (it->op_code == OpCode::CALL)
      sites.push_back({it, current});
  }

  int uid = 0;
  for (auto &site : sites) {
    auto call_it = site.call;
    std::string callee_name = call_it->label;
    // 不内联自身（递归）与 main
    if (callee_name == site.caller || callee_name == "main") continue;
    auto f = funcs.find(callee_name);
    if (f == funcs.end()) continue;  // 外部函数
    auto body_begin = std::next(f->second.first);
    auto body_end = f->second.second;  // FUNCTION_END
    // 体量与形态检查：不含 CALL、所有 RET 都带回返回值
    int size = 0;
    bool ok = true;
    for (auto it = body_begin; it != body_end; it++) {
      if (it->op_code == OpCode::CALL) {
        ok = false;
        break;
      }
      if (it->op_code == OpCode::RET && it->op1.is_null()) {
        ok = false;
        break;
      }
      size++;
    }
    if (!ok || size == 0 || size > kMaxInlineSize) continue;

    // 收集调用点实参（SET_ARG 紧邻 CALL 之前）：核心池与 VFP 槽位
    std::map<int, OpName> core_args, fp_args;
    auto first_arg = call_it;
    while (first_arg != ir.begin() &&
           std::prev(first_arg)->op_code == OpCode::SET_ARG)
      first_arg--;
    for (auto it = first_arg; it != call_it; it++) {
      if (it->label == "vfp")
        fp_args[it->dest.value] = it->op1;
      else
        core_args[it->dest.value] = it->op1;
    }

    // 复制函数体（不含 FUNCTION_BEGIN/END），记录原 LABEL 节点地址到
    // 副本迭代器的映射，供 PHI_MOV 的 phi_block 重定向
    std::string prefix = std::to_string(++uid) + "_";
    std::string join_label = ".L.INLINE_" + prefix + "join";
    IRList body;
    std::map<const IR *, IRList::iterator> label_map;
    for (auto it = body_begin; it != body_end; it++) {
      body.push_back(*it);
      if (it->op_code == OpCode::LABEL) label_map[&*it] = std::prev(body.end());
    }

    // 拼接到调用点（splice 不返回迭代器，first_arg 前即副本起始）
    int body_size = (int)body.size();
    ir.splice(first_arg, body);
    auto spliced_at = std::prev(first_arg, body_size);

    // PHI_MOV 的 phi_block 重定向到副本内的 LABEL；无法映射则放弃内联
    bool ok2 = true;
    for (auto it = spliced_at; it != call_it; it++) {
      if (it->op_code != OpCode::PHI_MOV) continue;
      auto m = label_map.find(&*it->phi_block);
      if (m == label_map.end()) {
        ok2 = false;
        break;
      }
      it->phi_block = m->second;
    }
    if (ok2) {
      for (auto it = spliced_at; it != call_it; it++) {
        // 先重命名被调方局部名，再替换 $arg/$farg 实参：
        // 顺序颠倒会把调用方传入的 % 名一并重命名
        rename_locals(*it, prefix);
        rename_labels(*it, prefix);
        if (!substitute_args(*it, core_args, fp_args)) {
          ok2 = false;  // 实参槽位对不上：放弃内联
          break;
        }
        if (it->op_code == OpCode::RET) {
          // RET v => MOV dest, v; JMP join
          it->op_code = OpCode::MOV;
          it->dest = call_it->dest;
          it->label.clear();
          IRList jmp;
          jmp.emplace_back(OpCode::JMP, OpName(), OpName(), OpName(),
                           join_label);
          ir.splice(std::next(it), jmp);
          it++;
        }
      }
    }
    if (!ok2) {
      // 回滚：把副本整体移出主列表丢弃，调用点保持原样
      IRList rollback;
      rollback.splice(rollback.begin(), ir, spliced_at, call_it);
      continue;
    }

    // 删除调用点（SET_ARG 序列与 CALL），再把汇合标号放到副本末尾
    auto after = ir.erase(first_arg, std::next(call_it));
    IRList join;
    join.emplace_back(OpCode::LABEL, join_label);
    ir.splice(after, join);
  }
}
}  // namespace syc::ir::passes
