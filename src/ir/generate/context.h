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
#include <map>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast/type.h"
#include "config.h"
#include "ir/ir.h"

namespace syc::ir {
class VarInfo {
 public:
  std::vector<int> shape;
  bool is_array;
  std::string name;       // with @$%
  types::TypeId ty;       // 标量的声明类型；数组时为元素类型
  VarInfo(std::string name, bool is_array = false, std::vector<int> shape = {},
          types::TypeId ty = types::TypeId::INT);
};

class ConstInfo {
 public:
  bool is_array;
  std::vector<int> shape;
  std::vector<std::int64_t> value;
  types::TypeId ty;  // 元素类型
  ConstInfo(std::vector<std::int64_t> value, bool is_array = false,
            std::vector<int> shape = {},
            types::TypeId ty = types::TypeId::INT);
  ConstInfo(std::int64_t value);
};

// 函数签名（用于调用点的参数/返回值转换）
struct FuncSig {
  types::TypeInfo return_ty{types::TypeId::INT};
  std::vector<types::TypeInfo> params;
  // 变参函数（printf/putf 等）：实参按 C 默认实参提升后走核心池
  bool variadic = false;
};

class Context {
 public:
  Context();
  // 编译配置（由 ir::generate 注入，随 Context 拷贝传播）
  const syc::Config* cfg = nullptr;
  unsigned id = 1;
  unsigned get_id();

  using SymbolTable = std::vector<std::unordered_map<std::string, VarInfo>>;
  using ConstTable = std::vector<std::unordered_map<std::string, ConstInfo>>;

  SymbolTable symbol_table = {{}};
  ConstTable const_table = {{}};
  ConstTable const_assign_table = {{}};

  std::unordered_map<std::string, FuncSig> function_table;

  void insert_symbol(std::string name, VarInfo value);
  void insert_const(std::string name, ConstInfo value);
  void insert_const_assign(std::string name, ConstInfo value);

  // 函数签名登记与查询；未登记的内建库函数返回 nullptr
  void insert_function(const std::string& name, const FuncSig& sig);
  const FuncSig* find_function(const std::string& name) const;

  // 当前正在生成的函数返回类型栈（ReturnStatement 转换用）
  std::vector<FuncSig> function_stack;
  FuncSig& cur_func() { return this->function_stack.back(); }

  VarInfo& find_symbol(std::string name);
  ConstInfo& find_const(std::string name);
  ConstInfo& find_const_assign(std::string name);

  void create_scope();
  void end_scope();

  // 字符串字面量常量池，返回值为该字符串全局符号（去重）
  std::string insert_string(const std::string& content);
  const std::vector<std::pair<std::string, std::string>>& strings() const {
    return *this->string_list;
  }

  bool is_global();
  bool in_loop();

  std::stack<std::string> loop_label;
  std::stack<std::vector<SymbolTable>> loop_continue_symbol_snapshot;
  std::stack<std::vector<SymbolTable>> loop_break_symbol_snapshot;
  std::stack<std::map<std::pair<int, std::string>, std::string>>
      loop_continue_phi_move;
  std::stack<std::map<std::pair<int, std::string>, std::string>>
      loop_break_phi_move;
  std::stack<std::vector<std::string>> loop_var{};

  // Shared across Context copies: loop-body generation (WhileStatement) runs
  // with a Context copy, so string literals must land in the same pool used by
  // Root::_generate_ir when it flushes DATA_STRING records.
  std::shared_ptr<std::unordered_map<std::string, std::string>> string_pool;
  std::shared_ptr<std::vector<std::pair<std::string, std::string>>> string_list;
};
}  // namespace syc::ir
