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
#include "context_ir.h"

#include <exception>

namespace SYC {
VarInfo::VarInfo(std::string name, bool is_array, std::vector<int> shape)
    : name(name), shape(shape), is_array(is_array) {}
ConstInfo::ConstInfo(std::vector<int> value, bool is_array,
                     std::vector<int> shape)
    : value(value), shape(shape), is_array(is_array) {}
ConstInfo::ConstInfo(int value) : value({value}), shape({}), is_array(false) {}

ContextIR::ContextIR() {
  this->insert_symbol("_sysy_l1", VarInfo("@&^_sysy_l1", true, {1024}));
  this->insert_symbol("_sysy_l2", VarInfo("@&^_sysy_l2", true, {1024}));
  this->insert_symbol("_sysy_h", VarInfo("@&^_sysy_h", true, {1024}));
  this->insert_symbol("_sysy_m", VarInfo("@&^_sysy_m", true, {1024}));
  this->insert_symbol("_sysy_s", VarInfo("@&^_sysy_s", true, {1024}));
  this->insert_symbol("_sysy_us", VarInfo("@&^_sysy_us", true, {1024}));
  this->insert_symbol("_sysy_idx", VarInfo("@^_sysy_idx", false));
}

unsigned ContextIR::get_id() { return ++id; }

void ContextIR::insert_symbol(std::string name, VarInfo value) {
  symbol_table.back().insert({name, value});
}
void ContextIR::insert_const(std::string name, ConstInfo value) {
  const_table.back().insert({name, value});
}
void ContextIR::insert_const_assign(std::string name, ConstInfo value) {
  const_assign_table.back().insert({name, value});
}

VarInfo& ContextIR::find_symbol(std::string name) {
  for (int i = symbol_table.size() - 1; i >= 0; i--) {
    auto find = symbol_table[i].find(name);
    if (find != symbol_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such symbol:" + name);
}

ConstInfo& ContextIR::find_const(std::string name) {
  for (int i = const_table.size() - 1; i >= 0; i--) {
    auto find = const_table[i].find(name);
    if (find != const_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such const:" + name);
}
ConstInfo& ContextIR::find_const_assign(std::string name) {
  for (int i = const_assign_table.size() - 1; i >= 0; i--) {
    auto find = const_assign_table[i].find(name);
    if (find != const_assign_table[i].end()) return find->second;
  }
  throw std::out_of_range("No such const:" + name);
}

void ContextIR::create_scope() {
  symbol_table.push_back({});
  const_table.push_back({});
  const_assign_table.push_back({});
}

void ContextIR::end_scope() {
  symbol_table.pop_back();
  const_table.pop_back();
  const_assign_table.pop_back();
}

bool ContextIR::is_global() {
  return symbol_table.size() == 1 && const_table.size() == 1;
}
bool ContextIR::in_loop() { return !loop_label.empty(); }
}  // namespace SYC
