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
#include <array>
#include <bitset>
#include <ostream>
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
  static constexpr int reg_count = 12;

  std::ostream& log_out;

  ir::IRList* irs;
  // stack_size[0]: VFP 栈参数区（浮点实参溢出到栈的字数 * 4）
  // stack_size[1]: 保护现场 // 顺序为 r14(lr), r4, r5, ..., r11, s16..s27
  // stack_size[2]: 寄存器溢出
  // stack_size[3]: 核心寄存器函数调用实参
  std::array<int, 4> stack_size{0, 4, 0, 0};
  ir::IRList::iterator function_begin_it;
  std::unordered_map<std::string, int> stack_offset_map;
  // 宽度 8（long long）的 IR 名字集合：这些值占用寄存器对
  // （低半用本名，高半用 hi_name(name)），由函数 IR 预扫描填充
  std::unordered_map<std::string, int> var_width;
  static std::string hi_name(const std::string& name);
  bool is_wide(const std::string& name) const;
  void mark_wide(const std::string& name);
  // 64 位比较谓词合成用的内部标签计数（需程序内唯一）
  int wide_label_id = 0;
  // 获取每一条it对应时间戳
  std::unordered_map<ir::IR*, int> ir_to_time;
  // var定义的时间戳
  std::unordered_map<std::string, int> var_define_timestamp;
  std::multimap<int, std::string> var_define_timestamp_heap;
  // var最后使用的时间戳
  std::unordered_map<std::string, int> var_latest_use_timestamp;
  std::multimap<int, std::string> var_latest_use_timestamp_heap;

  // 保护现场后可用的寄存器
  std::bitset<reg_count> savable_reg = 0b111111110000;
  // 已使用的寄存器
  std::bitset<reg_count> used_reg = 0b000000000000;

  // 当前在寄存器中的变量极其寄存器号(active)
  std::unordered_map<std::string, int> var_to_reg = {
      {"$arg0", 0}, {"$arg1", 1}, {"$arg2", 2}, {"$arg3", 3}};
  std::unordered_map<int, std::string> reg_to_var;

  // ── VFP（浮点）寄存器分配 ─────────────────────────────────────
  // 统一 32 个单精度寄存器 s0-s31（dN = s2N:s2N+1）；s0-s15 caller-saved，
  // s16-s27 callee-saved，s28-s31 保留为指令选择的草稿寄存器。
  // double 以偶对齐的连续两个 s 寄存器（即 dN）为分配单位。
  static constexpr int freg_count = 32;
  static constexpr int freg_allocatable = 28;  // s0-s27 参与分配
  enum class FpClass { NONE, F32, F64 };
  // 浮点变量类别：由函数 IR 预扫描按定义 IR 的 ty 填充
  std::unordered_map<std::string, FpClass> var_fp;
  FpClass fp_class(const std::string& name) const;
  bool is_fpreg(const std::string& name) const;
  void mark_fp(const std::string& name, types::TypeId ty);
  std::bitset<freg_count> savable_freg = 0b00001111111111110000000000000000;
  std::bitset<freg_count> used_freg = 0;
  std::unordered_map<std::string, int> var_to_freg;
  std::unordered_map<int, std::string> freg_to_var;
  // VFP 栈参数字数（映射进 stack_size[0]）
  int fstack_arg_words = 0;

  bool var_in_freg(std::string name);
  // 寻找空闲 VFP 寄存器（dbl 时返回偶数序号，即 dN 的低单精度序号）
  int find_free_freg(bool dbl, int begin = 0);
  // 获取寄存器，必要时回收 callee-saved 或溢出占用者
  int get_new_freg(bool dbl, int begin = 0);
  void overflow_fvar(std::string name);
  int get_new_freg_for(std::string name, int begin = 0);
  int get_specified_freg_for(std::string name, int reg_id);
  void expire_old_freg_intervals(int cur_time);

  // 浮点装载：把 op1（VFP 寄存器 / 溢出槽 / $fargN / 全局标量 / 立即数
  // 位模式）装到目标单精度（dbl 时为双精度）寄存器；已在位则不发射
  void load_freg(int sidx, bool dbl, ir::OpName op, std::ostream& out);
  // 浮点存放：把 sidx 寄存器的值写到 dest（VFP 寄存器 / 溢出槽 / 全局）
  void store_freg_dest(int sidx, bool dbl, const std::string& name,
                       std::ostream& out);

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
  void load_imm(std::string reg, std::int64_t value, std::ostream& out);

  void load(std::string reg, ir::OpName op, std::ostream& out);

  // 装载 64 位值的某一半到寄存器（half: 0=低 32 位, 1=高 32 位）。
  // 支持 64 位立即数、局部变量（高半为独立分配的 #h 名字）、
  // 全局变量（高半位于 +4 地址）以及 $argN 寄存器/栈对。
  void load_half(std::string reg, ir::OpName op, int half, std::ostream& out);

  void store_to_stack_offset(std::string reg, int offset, std::ostream& out,
                             std::string op = "STR");

  void load_from_stack_offset(std::string reg, int offset, std::ostream& out,
                              std::string op = "LDR");

  void store_to_stack(std::string reg, ir::OpName op, std::ostream& out,
                      std::string op_code = "STR");
};
}  // namespace syc::assembly