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
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <string>

#include "ast/type.h"
#include "ir/generate/context.h"

namespace syc::ir {
class OpName {
 protected:
  enum Type {
    Var,
    Imm,
    Null,
  };

 public:
  Type type;
  std::string name;
  std::int64_t value;
  OpName();
  OpName(std::string name);
  OpName(int value);
  OpName(std::int64_t value);
  bool is_var() const;
  bool is_local_var() const;
  bool is_global_var() const;
  bool is_imm() const;
  bool is_null() const;
  bool operator==(const OpName& other) const;
};
enum class OpCode {
  MALLOC_IN_STACK,  // dest = offset(new StackArray(size op1))
  MOV,              // dest = op1
  ADD,              // dest = op1 + op2
  SUB,              // dest = op1 - op2
  IMUL,             // dest = op1 * op2
  IDIV,             // dest = op1 / op2 （有符号）
  MOD,              // dest = op1 % op2 （有符号）
  UDIV,             // dest = op1 /u op2 （无符号）
  UMOD,             // dest = op1 %u op2 （无符号）
  TRUNC_U8,         // dest = op1 零扩展低 8 位（规范化到 uint8）
  TRUNC_S8,         // dest = op1 符号扩展低 8 位（规范化到 int8）
  TRUNC_U16,        // dest = op1 零扩展低 16 位
  TRUNC_S16,        // dest = op1 符号扩展低 16 位
  SEXT,             // dest(8字节) = (int64_t)(int32_t)op1 符号扩展 32->64
  ZEXT,             // dest(8字节) = (uint64_t)(uint32_t)op1 零扩展 32->64
  FDIV,             // dest(浮点 ty) = op1 / op2 浮点除法
  FNEG,             // dest(浮点 ty) = -op1
  SITOF,            // dest(浮点 ty) = (浮点)(int32_t)op1（ARM64 SCVTF）
  UITOF,            // dest(浮点 ty) = (浮点)(uint32_t)op1（ARM64 UCVTF）
  F2I,              // dest = (int32_t)op1 向零截断（ARM64 FCVTZS）；ty 记录源浮点宽度
  F2U,              // dest = (uint32_t)op1 向零截断（ARM64 FCVTZU）；ty 记录源浮点宽度
  F2F,              // dest(浮点 ty) = 浮点宽度转换 op1（F32<->F64，FCVT）
  SET_ARG,          // if dest < 4: R(dest)) = op1 else: push_stack(op1)
  CALL,             // call label
  CMP,              // cmp op1, op2
  JMP,              // jmp label
  JEQ,              // if EQ: jmp label
  JNE,              // if NE: jmp label
  JLE,              // if LE: jmp label
  JLT,              // if LT: jmp label
  JGE,              // if GE: jmp label
  JGT,              // if GT: jmp label
  JMPHS,            // if HS(无符号 >=): jmp label
  JMPLO,            // if LO(无符号 <): jmp label
  JMPLS,            // if LS(无符号 <=): jmp label
  JMPI,             // if HI(无符号 >): jmp label
  MOVEQ,            // if EQ: dest = op1 else: dest = op2
  MOVNE,            // if NE: dest = op1 else: dest = op2
  MOVLE,            // if LE: dest = op1 else: dest = op2
  MOVLT,            // if LT: dest = op1 else: dest = op2
  MOVGE,            // if GE: dest = op1 else: dest = op2
  MOVGT,            // if GT: dest = op1 else: dest = op2
  MOVHS,            // if 无符号>=: dest = op1 else: dest = op2
  MOVLO,            // if 无符号<: dest = op1 else: dest = op2
  MOVLS,            // if 无符号<=: dest = op1 else: dest = op2
  MOVHI,            // if 无符号>: dest = op1 else: dest = op2
  AND,              // dest = op1 && op2
  OR,               // dest = op1 || op2
  SAL,              // dest = op1 << op2 算数左移
  SAR,              // dest = op1 >> op2 算数右移
  LSR,              // dest = op1 >>u op2 逻辑右移（无符号）
  STORE,            // op1[op2] = op3
  LOAD,             // dest = op1[op2]
  RET,              // return / return op1
  LABEL,            // label:
  DATA_BEGIN,       //.data
  DATA_WORD,        //.word
  DATA_SPACE,       //.space
  DATA_STRING,      //.asciz
  DATA_END,         // nothing
  FUNCTION_BEGIN,   // FUNCTION_BEGIN
  FUNCTION_END,     // FUNCTION_END
  PHI_MOV,          // PHI
  NOOP,             // no operation
  INFO,             // info for compiler
};
class IR {
 public:
  int line, column;
  OpCode op_code;
  // dest 的类型：宽度 8 的指令按 64 位解释操作数与结果，
  // LLONG/ULLONG 的符号性供 CMP 等运算区分；
  // FLOAT/DOUBLE 的指令按浮点解释（后端走 VFP 寄存器路径）。
  // 例外：F2I/F2U 的 dest 是 32 位整型，ty 记录源浮点宽度
  //（op1 位模式的解释方式，供折叠/求值与 VCVT 变体选择）。
  types::TypeInfo ty{types::TypeId::INT};
  std::string label;
  OpName op1, op2, op3, dest;
  std::list<IR>::iterator phi_block;
  IR(OpCode op_code, OpName dest, OpName op1, OpName op2, OpName op3,
     std::string label = "");
  IR(OpCode op_code, OpName dest, OpName op1, OpName op2,
     std::string label = "");
  IR(OpCode op_code, OpName dest, OpName op1, std::string label = "");
  IR(OpCode op_code, OpName dest, std::string label = "");
  IR(OpCode op_code, std::string label = "");
  bool some(decltype(&syc::ir::OpName::is_var) callback,
            bool include_dest = true) const;
  bool some(std::function<bool(const syc::ir::OpName&)> callback,
            bool include_dest = true) const;
  void forEachOp(std::function<void(const syc::ir::OpName&)> callback,
                 bool include_dest = true) const;
  void print(std::ostream& out = std::cerr, bool verbose = false) const;

 protected:
  void setup_file_postion();
};

using IRList = std::list<IR>;
}  // namespace syc::ir
