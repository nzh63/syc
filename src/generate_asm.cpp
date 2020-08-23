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

#include "config.h"
#include "context_asm.h"
#include "ir.h"
#include "node.h"

using namespace std;

namespace {
constexpr bitset<ContextASM::reg_count> non_volatile_reg = 0b11111110000;

void generate_function_asm(IRList& irs, ostream& out, IRList::iterator begin,
                           IRList::iterator end) {
  assert(begin->op_code == IR::OpCode::FUNCTION_BEGIN);
  ContextASM ctx(&irs, begin, out);

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
    if (it->op_code == IR::OpCode::CALL || it->op_code == IR::OpCode::IDIV ||
        it->op_code == IR::OpCode::MOD) {
      ctx.has_function_call = true;
      break;
    }
  }
  ctx.stack_size[0] = ctx.has_function_call ? 4 : 0;

  for (auto it = begin; it != end; it++) {
    out << "# " << ctx.ir_to_time[&*it] << " ";
    it->print(out);
  }

  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    ///////////////////////////////////// 计算栈大小 (数组分配)
    if (ir.op_code == IR::OpCode::MALLOC_IN_STACK) {
      ctx.stack_offset_map[ir.dest.name] = ctx.stack_size[2];
      ctx.stack_size[2] += ir.op1.value;
    } else if (ir.op_code == IR::OpCode::SET_ARG) {
      ctx.stack_size[3] = std::max(ctx.stack_size[3], (ir.dest.value - 3) * 4);
    }
  }

  // 寄存器分配
  for (const auto& i : ctx.var_define_timestamp_heap) {
    int cur_time = i.first;
    ctx.expire_old_intervals(cur_time);

    if (ctx.var_in_reg(i.second)) {
      // TODO: 有这种情况吗????
      continue;
    } else {
      if (i.second.substr(0, 5) == "$arg:") {
        int reg = stoi(i.second.substr(5));
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
        ctx.get_specified_reg_for(i.second, reg);
      } else {
        if (i.second[0] != '%') continue;
        // 与期间内固定分配寄存器冲突
        bool conflict = false;
        int latest = ctx.var_latest_use_timestamp[i.second];
        for (const auto& j : ctx.var_define_timestamp_heap) {
          if (j.first < i.first) continue;
          if (j.first > latest) break;
          if (j.second.substr(0, 5) == "$arg:") {
            conflict = true;
            break;
          }
        }

        if (ctx.find_free_reg(conflict ? 4 : 0) != -1) {
          ctx.get_specified_reg_for(i.second,
                                    ctx.find_free_reg(conflict ? 4 : 0));
        } else {
          string cur_max = ctx.select_var_to_overflow(conflict ? 4 : 0);
          if (ctx.var_latest_use_timestamp[i.second] <
              ctx.var_latest_use_timestamp[cur_max]) {
            ctx.get_specified_reg_for(i.second, ctx.var_to_reg[cur_max]);
          } else {
            ctx.overflow_var(i.second);
          }
        }
      }
    }
  }

  // 翻译
  for (auto it = begin; it != end; it++) {
    auto& ir = *it;
    auto& stack_size = ctx.stack_size;
    out << "#";
    ir.print(out);

    if (ir.op_code == IR::OpCode::FUNCTION_BEGIN) {
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
      if (ctx.has_function_call) ctx.store_to_stack("lr", OpName("$ra"), out);

      // 保护现场
      int offset = 4;
      ctx.store_to_stack_offset("r11", stack_size[2] + stack_size[3], out);
      for (int i = 0; i < ContextASM::reg_count; i++) {
        if (non_volatile_reg[i] != ctx.savable_reg[i]) {
          ctx.store_to_stack_offset(
              "r" + to_string(i), stack_size[2] + stack_size[3] + offset, out);
          offset += 4;
        }
      }
    } else if (ir.op_code == IR::OpCode::MOV ||
               ir.op_code == IR::OpCode::PHI_MOV) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      if (ir.op1.type == OpName::Type::Imm) {
        if (dest_in_reg) {
          ctx.load_imm("r" + to_string(ctx.var_to_reg[ir.dest.name]),
                       ir.op1.value, out);
        } else {
          ctx.load_imm("r12", ir.op1.value, out);
          ctx.store_to_stack("r12", ir.dest, out);
        }
      } else if (ir.op1.type == OpName::Type::Var) {
        bool op1_in_reg =
            ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
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
#define F(OP_NAME, OP)                                                   \
  else if (ir.op_code == IR::OpCode::OP_NAME) {                          \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                     \
    bool op1_in_reg =                                                    \
        ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name); \
    bool op2_in_reg =                                                    \
        ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name); \
    bool op2_is_imm = ir.op2.type == OpName::Type::Imm &&                \
                      (ir.op2.value >= 0 && ir.op2.value < 256);         \
    int op1 = op1_in_reg ? ctx.var_to_reg[ir.op1.name] : 11;             \
    int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;             \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;          \
    if (!op2_in_reg && !op2_is_imm) {                                    \
      ctx.load("r" + to_string(op2), ir.op2, out);                       \
    }                                                                    \
    if (!op1_in_reg) {                                                   \
      ctx.load("r" + to_string(op1), ir.op1, out);                       \
    }                                                                    \
    out << "    " OP " r" << dest << ", r" << op1 << ", ";               \
    if (op2_is_imm) {                                                    \
      out << "#" << ir.op2.value << endl;                                \
    } else {                                                             \
      out << "r" << op2 << endl;                                         \
    }                                                                    \
    if (!dest_in_reg) {                                                  \
      ctx.store_to_stack("r" + to_string(dest), ir.dest, out);           \
    }                                                                    \
  }
    F(ADD, "ADD")
    F(SUB, "SUB")
#undef F
    else if (ir.op_code == IR::OpCode::IMUL) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg =
          ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg =
          ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
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
  else if (ir.op_code == IR::OpCode::OP_NAME) {                            \
    assert(ir.op2.type == OpName::Type::Imm);                              \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    bool op1_in_reg =                                                      \
        ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);   \
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
    else if (ir.op_code == IR::OpCode::IDIV) {
      if (config::optimize_level > 0 && ir.op2.type == OpName::Type::Imm) {
        bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
        int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 11;
        bool op1_in_reg =
            ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
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
    else if (ir.op_code == IR::OpCode::MOD) {
      ctx.load("r0", ir.op1, out);
      ctx.load("r1", ir.op2, out);
      out << "    BL __aeabi_idivmod" << endl;
      if (ctx.var_in_reg(ir.dest.name)) {
        out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r1" << endl;
      } else {
        ctx.store_to_stack("r1", ir.dest, out);
      }
    }
    else if (ir.op_code == IR::OpCode::CALL) {
      out << "    BL " << ir.label << endl;
      if (ir.dest.type == OpName::Type::Var) {
        if (ctx.var_in_reg(ir.dest.name)) {
          out << "    MOV r" << ctx.var_to_reg[ir.dest.name] << ", r0" << endl;
        } else {
          ctx.store_to_stack("r0", ir.dest, out);
        }
      }
    }
    else if (ir.op_code == IR::OpCode::SET_ARG) {
      if (ir.dest.value < 4) {
        ctx.load("r" + to_string(ir.dest.value), ir.op1, out);
      } else {
        ctx.load("r12", ir.op1, out);
        ctx.store_to_stack_offset("r12", (ir.dest.value - 4) * 4, out);
      }
    }
    else if (ir.op_code == IR::OpCode::CMP) {
      bool op1_in_reg =
          ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg =
          ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
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
  else if (ir.op_code == IR::OpCode::OP_NAME) { \
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
  else if (ir.op_code == IR::OpCode::OP_NAME) {                            \
    bool dest_in_reg = ctx.var_in_reg(ir.dest.name);                       \
    int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;            \
    if (ir.op1.type == OpName::Type::Imm && ir.op1.value >= 0 &&           \
        ir.op1.value < 256 && ir.op2.type == OpName::Type::Imm &&          \
        ir.op2.value >= 0 && ir.op2.value < 256) {                         \
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
    else if (ir.op_code == IR::OpCode::AND) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg =
          ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg =
          ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
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
    else if (ir.op_code == IR::OpCode::OR) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      bool op1_in_reg =
          ir.op1.type == OpName::Type::Var && ctx.var_in_reg(ir.op1.name);
      bool op2_in_reg =
          ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
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
    else if (ir.op_code == IR::OpCode::STORE) {
      // op1 基地址
      bool op3_in_reg =
          ir.op3.type == OpName::Type::Var && ctx.var_in_reg(ir.op3.name);
      int op3 = op3_in_reg ? ctx.var_to_reg[ir.op3.name] : 11;
      if (ir.op1.type == OpName::Type::Var &&
          ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.type == OpName::Type::Imm) {
        if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.store_to_stack_offset("r" + to_string(op3), offset, out);
      } else {
        bool op2_in_reg =
            ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
        int op1 = 11;
        int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;
        if (!op2_in_reg) ctx.load("r" + to_string(op2), ir.op2, out);
        ctx.load("r" + to_string(op1), ir.op1, out);
        out << "    ADD r12, r" << op1 << ", r" << op2 << endl;
        if (!op3_in_reg) ctx.load("r" + to_string(op3), ir.op3, out);
        out << "    STR r" << op3 << ", [r12]" << endl;
      }
    }
    else if (ir.op_code == IR::OpCode::LOAD) {
      bool dest_in_reg = ctx.var_in_reg(ir.dest.name);
      int dest = dest_in_reg ? ctx.var_to_reg[ir.dest.name] : 12;
      // op1 基地址
      if (ir.op1.type == OpName::Type::Var &&
          ir.op1.name.substr(0, 2) == "%&" &&
          ir.op2.type == OpName::Type::Imm) {
        int offset = ctx.resolve_stack_offset(ir.op1.name) + ir.op2.value;
        ctx.load_from_stack_offset("r" + to_string(dest), offset, out);
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      } else {
        bool op2_in_reg =
            ir.op2.type == OpName::Type::Var && ctx.var_in_reg(ir.op2.name);
        int op1 = 11;
        int op2 = op2_in_reg ? ctx.var_to_reg[ir.op2.name] : 12;

        if (!op2_in_reg) ctx.load("r" + to_string(op2), ir.op2, out);
        ctx.load("r" + to_string(op1), ir.op1, out);
        out << "    ADD r12, r" << op1 << ", r" << op2 << endl;
        out << "    LDR r" << dest << ", [r12]" << endl;
        if (!dest_in_reg) {
          ctx.store_to_stack("r" + to_string(dest), ir.dest, out);
        }
      }
    }
    else if (ir.op_code == IR::OpCode::RET) {
      if (ir.op1.type != OpName::Type::Null) {
        ctx.load("r0", ir.op1, out);
      }
      if (ctx.has_function_call) ctx.load("lr", OpName("$ra"), out);

      // 恢复现场
      int offset = 4;
      ctx.load_from_stack_offset("r11", stack_size[2] + stack_size[3], out);
      for (int i = 0; i < ContextASM::reg_count; i++) {
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
      out << "    MOV PC, LR" << endl;
    }

    else if (ir.op_code == IR::OpCode::LABEL) {
      out << ir.label << ':' << endl;
    }
  }
}  // namespace
}  // namespace

void generate_asm(IRList& irs, ostream& out) {
  out << R"(
    .macro mov32, reg, val
        movw \reg, #:lower16:\val
        movt \reg, #:upper16:\val
    .endm
  )" << endl;
  IRList::iterator function_begin_it;
  for (auto outter_it = irs.begin(); outter_it != irs.end(); outter_it++) {
    auto& ir = *outter_it;
    if (ir.op_code == IR::OpCode::DATA_BEGIN) {
      out << ".data" << endl;
      out << ".global " << ContextASM::rename(ir.label) << endl;
      out << ContextASM::rename(ir.label) << ":" << endl;
    } else if (ir.op_code == IR::OpCode::DATA_WORD) {
      out << ".word " << ir.dest.value << endl;
    } else if (ir.op_code == IR::OpCode::DATA_SPACE) {
      out << ".space " << ir.dest.value << endl;
    } else if (ir.op_code == IR::OpCode::DATA_END) {
      // do nothing
    } else if (ir.op_code == IR::OpCode::FUNCTION_BEGIN) {
      function_begin_it = outter_it;
    } else if (ir.op_code == IR::OpCode::FUNCTION_END) {
      generate_function_asm(irs, out, function_begin_it, outter_it);
    }
  }
}