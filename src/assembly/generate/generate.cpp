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
#include <algorithm>
#include <bitset>
#include <cassert>
#include <cmath>
#include <string>

#include "assembly/generate/context.h"
#include "ast/node.h"
#include "config.h"
#include "ir/ir.h"

using namespace std;

namespace syc::assembly {
namespace {
constexpr bitset<Context::reg_count> non_volatile_reg = 0b11111110000;

void generate_function_asm(ir::IRList& irs, ir::IRList::iterator begin,
                           ir::IRList::iterator end, ostream& out,
                           ostream& log_out) {
  assert(begin->op_code == ir::OpCode::FUNCTION_BEGIN);
  Context ctx(&irs, begin, log_out);

  // 标号
  for (auto it = begin; it != end; it++) {
    ctx.set_ir_timestamp(*it);
  }

  for (auto it = begin; it != end; it++) {
    ctx.set_var_define_timestamp(*it);
  }

  for (auto it = end; it != begin; it--) {
    ctx.set_var_latest_use_timestamp(*it);
  }

  for (auto it = begin; it != end; it++) {
    if (it->op_code == ir::OpCode::CALL || it->op_code == ir::OpCode::IDIV ||
        it->op_code == ir::OpCode::MOD) {
      ctx.has_function_call = true;
      break;
    }
  }
  ctx.stack_size[0] = ctx.has_function_call ? 4 : 0;

  for (auto it = begin; it != end; it++) {
    log_out << "# " << ctx.ir_to_time[&*it] << " ";
    it->print(log_out);
  }

  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    ///////////////////////////////////// 计算栈大小 (数组分配)
    if (ir.op_code == ir::OpCode::MALLOC_IN_STACK) {
      ctx.stack_offset_map[ir.dest.name] = ctx.stack_size[2];
      ctx.stack_size[2] += ir.op1.value;
    } else if (ir.op_code == ir::OpCode::SET_ARG) {
      ctx.stack_size[3] = std::max(ctx.stack_size[3], (ir.dest.value - 3) * 4);
    }
  }

  // 寄存器分配
  for (const auto& i : ctx.var_define_timestamp_heap) {
    int cur_time = i.first;
    auto var_name = i.second;
    ctx.expire_old_intervals(cur_time);

    if (ctx.var_in_reg(var_name)) {
      // TODO: 有这种情况吗????
      continue;
    } else {
      if (var_name.substr(0, 5) == "$arg:") {
        int reg = stoi(var_name.substr(5));
        if (ctx.used_reg[reg]) {
          string cur_var = ctx.reg_to_var[reg];
          if (ctx.find_free_reg(4) != -1) {
            ctx.move_reg(cur_var, ctx.find_free_reg(4));
          } else {
            string overflow_var = ctx.select_var_to_overflow(4);
            if (ctx.var_latest_use_timestamp[cur_var] >=
                ctx.var_latest_use_timestamp[overflow_var]) {
              overflow_var = cur_var;
            }
            ctx.overflow_var(overflow_var);
            if (ctx.used_reg[reg]) {
              ctx.move_reg(cur_var, ctx.find_free_reg(4));
            }
          }
        }
        ctx.get_specified_reg_for(var_name, reg);
      } else {
        if (var_name[0] != '%') continue;
        // 与期间内固定分配寄存器冲突
        bool conflict = false;
        int latest = ctx.var_latest_use_timestamp[var_name];
        for (const auto& j : ctx.var_define_timestamp_heap) {
          if (j.first <= cur_time) continue;
          if (j.first > latest) break;
          if (j.second.substr(0, 5) == "$arg:") {
            conflict = true;
            break;
          }
        }

        if (ctx.find_free_reg(conflict ? 4 : 0) != -1) {
          ctx.get_specified_reg_for(var_name,
                                    ctx.find_free_reg(conflict ? 4 : 0));
        } else {
          string cur_max = ctx.select_var_to_overflow(conflict ? 4 : 0);
          if (ctx.var_latest_use_timestamp[var_name] <
              ctx.var_latest_use_timestamp[cur_max]) {
            ctx.get_specified_reg_for(var_name, ctx.var_to_reg[cur_max]);
          } else {
            ctx.overflow_var(var_name);
          }
        }
      }
    }
  }

  // 翻译
  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    auto& stack_size = ctx.stack_size;
    log_out << "#";
    ir.print(log_out);

    if (ir.op_code == ir::OpCode::FUNCTION_BEGIN) {
      out << ".text" << endl;
      out << ".global " << ir.label << endl;
      out << ".type	" << ir.label << ", %function" << endl;
      out << ir.label << ":" << endl;
      if (stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] > 256) {
        ctx.load("r12",
                 stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3],
                 out);
        out << "    SUB sp, sp, r12" << endl;
      } else {
        out << "    SUB sp, sp, #"
            << stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3]
            << endl;
      }
      if (ctx.has_function_call)
        ctx.store_to_stack("lr", ir::OpName("$ra"), out);

      // 保护现场
      int offset = 4;
      ctx.store_to_stack_offset("r11", stack_size[2] + stack_size[3], out);
      for (int i = 0; i < Context::reg_count; i++) {
        if (non_volatile_reg[i] != ctx.savable_reg[i]) {
          ctx.store_to_stack_offset(
              "r" + to_string(i), stack_size[2] + stack_size[3] + offset, out);
          offset += 4;
        }
      }
    } else if (ir.op_code == ir::OpCode::MOV ||
               ir.op_code == ir::OpCode::PHI_MOV) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      if (ir.op1.is_imm()) {
        if (dest_in_reg) {
          ctx.load_imm("r" + to_string(ctx.var_to_reg[ir.dest.name]),
                       ir.op1.value, out);
        } else {
          ctx.load_imm("r12", ir.op1.value, out);
          ctx.store_to_stack("r12", ir.dest, out);
        }
      } else if (ir.op1.is_var()) {
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
        int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
        if (!op1_in_reg) {
          ctx.load("r" + to_string(op1), ir.op1, out);
        }
        if (dest_in_reg && op1 != dest)
          out << "    MOV r" << dest << ", r" << op1 << endl;
        else if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(op1), ir.dest, out);
        }
      }
    }
#define F(OP_NAME, OP)                                                \
  else if (ir.op_code == ir::OpCode::OP_NAME) {                       \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                  \
    bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name); \
    bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name); \
    bool op2_is_imm =                                                 \
        ir.op2.is_imm() && (ir.op2.value >= 0 && ir.op2.value < 256); \
    int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 11;          \
    int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;          \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;       \
    if (!op2_in_reg && !op2_is_imm) {                                 \
      ctx.load("r" + to_string(op2), ir.op2, out);                    \
    }                                                                 \
    if (!op1_in_reg) {                                                \
      ctx.load("r" + to_string(op1), ir.op1, out);                    \
    }                                                                 \
    out << "    " OP " r" << dest << ", r" << op1 << ", ";            \
    if (op2_is_imm) {                                                 \
      out << "#" << ir.op2.value << endl;                             \
    } else {                                                          \
      out << "r" << op2 << endl;                                      \
    }                                                                 \
    if (!dest_in_reg) {                                               \
      ctx.store_to_stack("r" + to_string(dest), ir.dest, out);        \
    }                                                                 \
  }
    F(ADD, "ADD")
    F(SUB, "SUB")
#undef F
    else if (ir.op_code == ir::OpCode::IMUL) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 11;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (dest == op1) {
        out << "    MUL r12, r" << op1 << ", r" << op2 << endl;
        out << "    MOV r" << dest << ", r12" << endl;
      } else {
        out << "    MUL r" << dest << ", r" << op1 << ", r" << op2 << endl;
      }
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
#define F(OP_NAME, OP)                                                     \
  else if (ir.op_code == ir::OpCode::OP_NAME) {                            \
    assert(ir.op2.is_imm());                                               \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);      \
    int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 11;               \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;            \
    if (!op1_in_reg) {                                                     \
      ctx.load("r" + to_string(op1), ir.op1, out);                         \
    }                                                                      \
    out << "    " OP " r" << dest << ", r" << op1 << ", #" << ir.op2.value \
        << endl;                                                           \
    if (!dest_in_reg) {                                                    \
      ctx.store_to_stack("r" + to_string(dest), ir.dest, out);             \
    }                                                                      \
  }
    F(SAL, "LSL")
    F(SAR, "ASR")
#undef F
    else if (ir.op_code == ir::OpCode::IDIV) {
      if (config::optimize_level > 0 && ir.op2.is_imm()) {
        bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
        int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 11;
        bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
        int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 0;
        if (!op1_in_reg) {
          ctx.load("r" + to_string(op1), ir.op1, out);
        }

        int c = ir.op2.value;
        if (c < 0) {
          c = -c;
          out << "    MOV r3, #0" << endl;
          out << "    SUBS r3, r3, r" << op1 << endl;  // op1 = -op1;
          op1 = 3;
        } else {
          out << "    CMP r" << op1 << ", #0" << endl;
        }
        int k = 0;
        int g = c;
        while (g != 1) {
          g = g / 2;
          k += 1;
        }
        if ((c & (c - 1)) ? false : true) {
          out << "    ASR r" << dest << ", r" << op1 << ", #" << k << endl;
          out << "    ADDLT r" << dest << ", r" << dest << ", #1" << endl;
        } else {
          // E = 2^k/c
          double E = (double)((int64_t)1 << k) / c;
          // f = 2^(k+32)/c
          double f = E * ((int64_t)1 << 31);
          // s = |f|
          int64_t s = ceil(f);
          double e = s - f;
          if (e > E) {
            s = floor(f);
          }
          s = ceil(f);
          ctx.load_imm("r1", s, out);
          out << "    SMULL r12, r2, r" << op1 << ", r1" << endl;
          out << "    ASR r" << dest << ", r2, #" << k - 1 << endl;
          out << "    ADDLT r" << dest << ", r" << dest << ", #1" << endl;
        }

        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      } else {
        ctx.load("r0", ir.op1, out);
        ctx.load("r1", ir.op2, out);
        out << "    BL __aeabi_idiv" << endl;
        if (ctx.var_in_reg(ir.dest.name)) {
          out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
        } else {
          ctx.store_to_stack("r0", ir.dest, out);
        }
      }
    }
    else if (ir.op_code == ir::OpCode::MOD) {
      ctx.load("r0", ir.op1, out);
      ctx.load("r1", ir.op2, out);
      out << "    BL __aeabi_idivmod" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r1" << endl;
      } else {
        ctx.store_to_stack("r1", ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::CALL) {
      out << "    BL " << ir.label << endl;
      if (ir.dest.is_var()) {
        if (ctx.var_in_reg(ir.dest.name)) {
          out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
        } else {
          ctx.store_to_stack("r0", ir.dest, out);
        }
      }
    }
    else if (ir.op_code == ir::OpCode::SET_ARG) {
      if (ir.dest.value < 4) {
        ctx.load("r" + to_string(ir.dest.value), ir.op1, out);
      } else {
        ctx.load("r12", ir.op1, out);
        ctx.store_to_stack_offset("r12", (ir.dest.value - 4) * 4, out);
      }
    }
    else if (ir.op_code == ir::OpCode::CMP) {
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 11;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    CMP r" << op1 << ", r" << op2 << endl;
    }
#define F(OP_NAME, OP)                          \
  else if (ir.op_code == ir::OpCode::OP_NAME) { \
    out << "    " OP " " << ir.label << endl;   \
  }
    F(JMP, "B")
    F(JEQ, "BEQ")
    F(JNE, "BNE")
    F(JLE, "BLE")
    F(JLT, "BLT")
    F(JGE, "BGE")
    F(JGT, "BGT")
#undef F
#define F(OP_NAME, OP_THEN, OP_ELSE)                                       \
  else if (ir.op_code == ir::OpCode::OP_NAME) {                            \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;            \
    if (ir.op1.is_imm() && ir.op1.value >= 0 && ir.op1.value < 256 &&      \
        ir.op2.is_imm() && ir.op2.value >= 0 && ir.op2.value < 256) {      \
      out << "    " OP_THEN " r" << dest << ", #" << ir.op1.value << endl; \
      out << "    " OP_ELSE " r" << dest << ", #" << ir.op2.value << endl; \
      if (!dest_in_reg) {                                                  \
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);           \
      }                                                                    \
    } else { /* TODO */                                                    \
    }                                                                      \
  }
    F(MOVEQ, "MOVEQ", "MOVNE")
    F(MOVNE, "MOVNE", "MOVEQ")
    F(MOVLE, "MOVLE", "MOVGT")
    F(MOVGT, "MOVGT", "MOVLE")
    F(MOVLT, "MOVLT", "MOVGE")
    F(MOVGE, "MOVGE", "MOVLT")
#undef F
    else if (ir.op_code == ir::OpCode::AND) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 11;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    TST r" << op1 << ", r" << op2 << endl;
      out << "    MOVEQ r" << dest << ", #0" << endl;
      out << "    MOVNE r" << dest << ", #1" << endl;
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::OR) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg = ir.op1.is_var() && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
      int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 12;
      int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 11;
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      if (!op1_in_reg) {
        ctx.load("r" + to_string(op1), ir.op1, out);
      }
      if (!op2_in_reg) {
        ctx.load("r" + to_string(op2), ir.op2, out);
      }
      out << "    ORRS r12, r" << op1 << ", r" << op2 << endl;
      out << "    MOVEQ r" << dest << ", #0" << endl;
      out << "    MOVNE r" << dest << ", #1" << endl;
      if (!dest_in_reg) {
        ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
      }
    }
    else if (ir.op_code == ir::OpCode::STORE) {
      // op1 基地址
      bool op3_in_reg = ir.op3.is_var() && ctx.var_in_reg(ir.op3.name);
      int op3 = op3_in_reg ? ctx.var_to_reg[ir.op3.name] : 11;
      if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.is_imm()) {
        if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.store_to_stack_offset("r" + to_string(op3), offset, out);
      } else {
        bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
        int op1 = 12;
        int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 11;
        bool offset_is_small =
            ir.op2.is_imm() && ir.op2.value >= 0 && ir.op2.value < 256;

        if (!op2_in_reg && !offset_is_small)
          ctx.load("r" + to_string(op2), ir.op2, out);
        ctx.load("r" + to_string(op1), ir.op1, out);
        if (!offset_is_small)
          out << "    ADD r12, r" << op1 << ", r" << op2 << endl;
        if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
        out << "    STR r" << op3 << ", ["
            << (offset_is_small
                    ? "r" + to_string(op1) + ",#" + to_string(ir.op2.value)
                    : "r12")
            << "]" << endl;
      }
    }
    else if (ir.op_code == ir::OpCode::LOAD) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      // op1 基地址
      if (ir.op1.is_var() && ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.is_imm()) {
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.load_from_stack_offset("r" + to_string(dest), offset, out);
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      } else {
        bool op2_in_reg = ir.op2.is_var() && ctx.var_in_reg(ir.op2.name);
        int op1 = 12;
        int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 11;
        bool offset_is_small =
            ir.op2.is_imm() && ir.op2.value >= 0 && ir.op2.value < 256;

        if (!op2_in_reg && !offset_is_small)
          ctx.load("r" + to_string(op2), ir.op2, out);
        ctx.load("r" + to_string(op1), ir.op1, out);
        if (!offset_is_small)
          out << "    ADD r12, r" << op1 << ", r" << op2 << endl;
        out << "    LDR r" << dest << ", ["
            << (offset_is_small
                    ? "r" + to_string(op1) + ",#" + to_string(ir.op2.value)
                    : "r12")
            << "]" << endl;
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      }
    }
    else if (ir.op_code == ir::OpCode::RET) {
      if (!ir.op1.is_null()) {
        ctx.load("r0", ir.op1, out);
      }
      if (ctx.has_function_call) ctx.load("lr", ir::OpName("$ra"), out);

      // 恢复现场
      int offset = 4;
      ctx.load_from_stack_offset("r11", stack_size[2] + stack_size[3], out);
      for (int i = 0; i < Context::reg_count; i++) {
        if (non_volatile_reg[i] != ctx.savable_reg[i]) {
          ctx.load_from_stack_offset(
              "r" + to_string(i), stack_size[2] + stack_size[3] + offset, out);
          offset += 4;
        }
      }

      if (stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] > 127) {
        ctx.load("r12",
                 stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3],
                 out);
        out << "    ADD sp, sp, r12" << endl;
      } else {
        out << "    ADD sp, sp, #"
            << stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3]
            << endl;
      }
      out << "    MOV pc, lr" << endl;
    }

    else if (ir.op_code == ir::OpCode::LABEL) {
      out << ir.label << ':' << endl;
    }
  }
}  // namespace
}  // namespace

void generate(ir::IRList& irs, ostream& out, ostream& log_out) {
  out << R"(
.macro mov32, reg, val
    movw \reg, #:lower16:\val
    movt \reg, #:upper16:\val
.endm
)" << endl;
  ir::IRList::iterator function_begin_it;
  for (auto outter_it = irs.begin(); outter_it != irs.end(); outter_it++) {
    auto& ir = *outter_it;
    if (ir.op_code == ir::OpCode::DATA_BEGIN) {
      out << ".data" << endl;
      out << ".global " << Context::rename(ir.label) << endl;
      out << Context::rename(ir.label) << ":" << endl;
    } else if (ir.op_code == ir::OpCode::DATA_WORD) {
      out << ".word " << ir.dest.value << endl;
    } else if (ir.op_code == ir::OpCode::DATA_SPACE) {
      out << ".space " << ir.dest.value << endl;
    } else if (ir.op_code == ir::OpCode::DATA_END) {
      // do nothing
    } else if (ir.op_code == ir::OpCode::FUNCTION_BEGIN) {
      function_begin_it = outter_it;
    } else if (ir.op_code == ir::OpCode::FUNCTION_END) {
      generate_function_asm(irs, function_begin_it, outter_it, out, log_out);
    }
  }
}

void generate(ir::IRList& irs, ostream& out) {
  ofstream null;
  null.setstate(std::ios_base::badbit);
  generate(irs, out, null);
}
}  // namespace syc::assembly
