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
#include "context.h"

#include <exception>

namespace syc::ir {
VarInfo::VarInfo(std::string name, bool is_array, std::vector<int> shape,
                 types::TypeId ty)
    : name(name), shape(shape), is_array(is_array), ty(ty) {}
ConstInfo::ConstInfo(std::vector<std::int64_t> value, bool is_array,
                     std::vector<int> shape, types::TypeId ty)
    : value(value), shape(shape), is_array(is_array), ty(ty) {}
ConstInfo::ConstInfo(std::int64_t value)
    : value({value}), shape({}), is_array(false),
      ty(types::TypeId::INT) {}

Context::Context()
    : string_pool(std::make_shared<std::unordered_map<std::string, std::string>>()),
      string_list(std::make_shared<std::vector<std::pair<std::string, std::string>>>()) {
  this->insert_symbol("_sysy_l1", VarInfo("@&^_sysy_l1", true, {1024}));
  this->insert_symbol("_sysy_l2", VarInfo("@&^_sysy_l2", true, {1024}));
  this->insert_symbol("_sysy_h", VarInfo("@&^_sysy_h", true, {1024}));
  this->insert_symbol("_sysy_m", VarInfo("@&^_sysy_m", true, {1024}));
  this->insert_symbol("_sysy_s", VarInfo("@&^_sysy_s", true, {1024}));
  this->insert_symbol("_sysy_us", VarInfo("@&^_sysy_us", true, {1024}));
  this->insert_symbol("_sysy_idx", VarInfo("@^_sysy_idx", false));
  // 内建库函数签名：printf/putf 为变参（浮点实参按默认实参提升传位模式）
  this->function_table["printf"] = FuncSig{types::TypeInfo{types::TypeId::INT}, {}, true};
  this->function_table["putf"] = FuncSig{types::TypeInfo{types::TypeId::INT}, {}, true};
}

unsigned Context::get_id() { return ++id; }

void Context::insert_symbol(std::string name, VarInfo value) {
  symbol_table.back().insert({name, value});
}
void Context::insert_const(std::string name, ConstInfo value) {
  const_table.back().insert({name, value});
}
void Context::insert_const_assign(std::string name, ConstInfo value) {
  const_assign_table.back().insert({name, value});
}

VarInfo& Context::find_symbol(std::string name) {
  for (int i = symbol_table.size() - 1; i >= 0; i--) {
    auto find = symbol_table[i].find(name);
    if (find != symbol_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such symbol:" + name);
}

ConstInfo& Context::find_const(std::string name) {
  for (int i = const_table.size() - 1; i >= 0; i--) {
    auto find = const_table[i].find(name);
    if (find != const_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such const:" + name);
}
ConstInfo& Context::find_const_assign(std::string name) {
  for (int i = const_assign_table.size() - 1; i >= 0; i--) {
    auto find = const_assign_table[i].find(name);
    if (find != const_assign_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such const:" + name);
}

void Context::create_scope() {
  symbol_table.push_back({});
  const_table.push_back({});
  const_assign_table.push_back({});
}

void Context::end_scope() {
  symbol_table.pop_back();
  const_table.pop_back();
  const_assign_table.pop_back();
}

bool Context::is_global() {
  return symbol_table.size() == 1 && const_table.size() == 1;
}
bool Context::in_loop() { return !loop_label.empty(); }

void Context::insert_function(const std::string& name, const FuncSig& sig) {
  this->function_table.insert_or_assign(name, sig);
}

const FuncSig* Context::find_function(const std::string& name) const {
  auto find = this->function_table.find(name);
  if (find == this->function_table.end()) return nullptr;
  return &find->second;
}

std::string Context::insert_string(const std::string& content) {
  auto find = this->string_pool->find(content);
  if (find != this->string_pool->end()) return find->second;
  std::string name = "@&^.L.str" + std::to_string(this->string_list->size());
  this->string_pool->insert({content, name});
  this->string_list->push_back({name, content});
  return name;
}
}  // namespace syc::ir
