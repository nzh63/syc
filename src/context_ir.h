/*
 * syc, a compiler for SysY
 * Copyright (C) 2020  nzh63, skywf21
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
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SYC {
class VarInfo {
 public:
  std::vector<int> shape;
  bool is_array;
  std::string name;  // with @$%
  VarInfo(std::string name, bool is_array = false, std::vector<int> shape = {});
};

class ConstInfo {
 public:
  bool is_array;
  std::vector<int> shape;
  std::vector<int> value;
  ConstInfo(std::vector<int> value, bool is_array = false,
            std::vector<int> shape = {});
  ConstInfo(int value);
};

class ContextIR {
 public:
  unsigned id = 1;

 public:
  ContextIR();
  unsigned get_id();

  using SymbolTable = std::vector<std::unordered_map<std::string, VarInfo>>;
  using ConstTable = std::vector<std::unordered_map<std::string, ConstInfo>>;

  SymbolTable symbol_table = {{}};
  ConstTable const_table = {{}};
  ConstTable const_assign_table = {{}};

  void insert_symbol(std::string name, VarInfo value);
  void insert_const(std::string name, ConstInfo value);
  void insert_const_assign(std::string name, ConstInfo value);

  VarInfo& find_symbol(std::string name);
  ConstInfo& find_const(std::string name);
  ConstInfo& find_const_assign(std::string name);

  void create_scope();
  void end_scope();

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
};
}  // namespace SYC
