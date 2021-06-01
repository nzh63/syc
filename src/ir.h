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
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "context_ir.h"

namespace SYC {
class IR {
 public:
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
    int value;
    OpName();
    OpName(std::string name);
    OpName(int value);
    bool is_var() const;
    bool is_imm() const;
    bool is_null() const;
  };
  enum class OpCode {
    MALLOC_IN_STACK,  // dest = offset(new StackArray(size op1))
    MOV,              // dest = op1
    ADD,              // dest = op1 + op2
    SUB,              // dest = op1 - op2
    IMUL,             // dest = op1 * op2
    IDIV,             // dest = op1 / op2
    MOD,              // dest = op1 % op2
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
    MOVEQ,            // if EQ: dest = op1 else: dest = op2
    MOVNE,            // if NE: dest = op1 else: dest = op2
    MOVLE,            // if LE: dest = op1 else: dest = op2
    MOVLT,            // if LT: dest = op1 else: dest = op2
    MOVGE,            // if GE: dest = op1 else: dest = op2
    MOVGT,            // if GT: dest = op1 else: dest = op2
    AND,              // dest = op1 && op2
    OR,               // dest = op1 || op2
    SAL,              // dest = op1 << op2 算数左移
    SAR,              // dest = op1 >> op2 算数右移
    STORE,            // op1[op2] = op3
    LOAD,             // dest = op1[op2]
    RET,              // return / return op1
    LABEL,            // label:
    DATA_BEGIN,       //.data
    DATA_WORD,        //.word
    DATA_SPACE,       //.space
    DATA_END,         // nothing
    FUNCTION_BEGIN,   // FUNCTION_BEGIN
    FUNCTION_END,     // FUNCTION_END
    PHI_MOV,          // PHI
    NOOP,             // no operation
    INFO,             // info for compiler
  };
  OpCode op_code;
  std::string label;
  IR::OpName op1, op2, op3, dest;
  std::list<IR>::iterator phi_block;
  IR(OpCode op_code, IR::OpName dest, IR::OpName op1, IR::OpName op2,
     IR::OpName op3, std::string label = "");
  IR(OpCode op_code, IR::OpName dest, IR::OpName op1, IR::OpName op2,
     std::string label = "");
  IR(OpCode op_code, IR::OpName dest, IR::OpName op1, std::string label = "");
  IR(OpCode op_code, IR::OpName dest, std::string label = "");
  IR(OpCode op_code, std::string label = "");
  void print(std::ostream& out = std::cerr, bool verbose = false) const;
};

using IRList = std::list<IR>;
}  // namespace SYC
