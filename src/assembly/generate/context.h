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
#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>

#include "ast/node.h"
#include "config.h"
#include "ir/generate/context.h"
#include "ir/ir.h"

namespace syc::assembly {
class Context {
 private:
  int time = 0;

 public:
  static constexpr int reg_count = 11;

  std::ostream& log_out;

  ir::IRList* irs;
  std::array<int, 4> stack_size{6 * 4, 4, 0, 0};
  ir::IRList::iterator function_begin_it;
  std::unordered_map<std::string, int> stack_offset_map;
  // 获取每一条it对应时间戳
  std::unordered_map<ir::IR*, int> ir_to_time;
  // var定义的时间戳
  std::unordered_map<std::string, int> var_define_timestamp;
  std::multimap<int, std::string> var_define_timestamp_heap;
  // var最后使用的时间戳
  std::unordered_map<std::string, int> var_latest_use_timestamp;
  std::multimap<int, std::string> var_latest_use_timestamp_heap;

  // 保护现场后可用的寄存器
  std::bitset<reg_count> savable_reg = 0b11111110000;
  // 已使用的寄存器
  std::bitset<reg_count> used_reg = 0b00000000000;

  // 当前在寄存器中的变量极其寄存器号(active)
  std::unordered_map<std::string, int> var_to_reg = {
      {"$arg0", 0}, {"$arg1", 1}, {"$arg2", 2}, {"$arg3", 3}};
  std::unordered_map<int, std::string> reg_to_var;

  bool has_function_call = false;

  Context(ir::IRList* irs, ir::IRList::iterator function_begin_it,
          std::ostream& log_out = std::cerr);

  static std::string rename(std::string name);

  int resolve_stack_offset(std::string name);

  void set_ir_timestamp(ir::IR& cur);
  void set_var_latest_use_timestamp(ir::IR& cur);
  void set_var_define_timestamp(ir::IR& cur);

  void expire_old_intervals(int cur_time);

  bool var_in_reg(std::string name);

  // 将变量的寄存器分配移动至 dest
  void move_reg(std::string name, int reg_dest);

  void overflow_var(std::string name);

  void overflow_reg(int reg_id);

  // 选择最晚使用的变量，以准备进行淘汰
  // 注意：该函数并未真正进行溢出，请调用 overflow_var
  std::string select_var_to_overflow(int begin = 0);

  // 寻找可使用的寄存器，但不获取它
  int find_free_reg(int begin = 0);

  // 获取一个可用寄存器，如 mark=true，设置 used_reg[i] = 1
  // 警告：该寄存器尚未与变量绑定，考虑配合bind_var_to_reg或使用get_new_reg_for
  int get_new_reg(int begin = 0);

  // 获取寄存器，但不进行变量绑定
  void get_specified_reg(int reg_id);

  // 将一个变量绑定到寄存器
  // 警告：需要先获取可用寄存器，即 used_reg[i] = 1
  // 考虑配合 get_new_reg 或使用 get_new_reg_for
  void bind_var_to_reg(std::string name, int reg_id);

  // 推荐使用
  int get_new_reg_for(std::string name);

  // 推荐使用
  int get_specified_reg_for(std::string name, int reg_id);

  // 加载操作
  void load_imm(std::string reg, int value, std::ostream& out);

  void load(std::string reg, ir::OpName op, std::ostream& out);

  void store_to_stack_offset(std::string reg, int offset, std::ostream& out,
                             std::string op = "STR");

  void load_from_stack_offset(std::string reg, int offset, std::ostream& out,
                              std::string op = "LDR");

  void store_to_stack(std::string reg, ir::OpName op, std::ostream& out,
                      std::string op_code = "STR");
};
}  // namespace syc::assembly