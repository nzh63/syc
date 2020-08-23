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
#include "ir.h"

OpName::OpName() : type(OpName::Type::Null) {}
OpName::OpName(std::string name) : type(OpName::Type::Var), name(name) {}
OpName::OpName(int value) : type(OpName::Type::Imm), value(value) {}

IR::IR(OpCode op_code, OpName dest, OpName op1, OpName op2, OpName op3,
       std::string label)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(op2),
      op3(op3),
      label(label) {}
IR::IR(OpCode op_code, OpName dest, OpName op1, OpName op2, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(op2),
      op3(OpName()),
      label(label) {}
IR::IR(OpCode op_code, OpName dest, OpName op1, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(OpName()),
      op3(OpName()),
      label(label) {}
IR::IR(OpCode op_code, OpName dest, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(OpName()),
      op2(OpName()),
      op3(OpName()),
      label(label) {}
IR::IR(OpCode op_code, std::string label)
    : op_code(op_code),
      dest(OpName()),
      op1(OpName()),
      op2(OpName()),
      op3(OpName()),
      label(label) {}
void IR::print(std::ostream& out, bool verbos) const {
  switch (this->op_code) {
    case IR::OpCode::MALLOC_IN_STACK:
      out << "MALLOC_IN_STACK"
          << std::string(16 - std::string("MALLOC_IN_STACK").size(), ' ');
      break;
    case IR::OpCode::MOV:
      out << "MOV" << std::string(16 - std::string("MOV").size(), ' ');
      break;
    case IR::OpCode::ADD:
      out << "ADD" << std::string(16 - std::string("ADD").size(), ' ');
      break;
    case IR::OpCode::SUB:
      out << "SUB" << std::string(16 - std::string("SUB").size(), ' ');
      break;
    case IR::OpCode::IMUL:
      out << "IMUL" << std::string(16 - std::string("IMUL").size(), ' ');
      break;
    case IR::OpCode::IDIV:
      out << "IDIV" << std::string(16 - std::string("IDIV").size(), ' ');
      break;
    case IR::OpCode::MOD:
      out << "MOD" << std::string(16 - std::string("MOD").size(), ' ');
      break;
    case IR::OpCode::SET_ARG:
      out << "SET_ARG" << std::string(16 - std::string("SET_ARG").size(), ' ');
      break;
    case IR::OpCode::CALL:
      out << "CALL" << std::string(16 - std::string("CALL").size(), ' ');
      break;
    case IR::OpCode::CMP:
      out << "CMP" << std::string(16 - std::string("CMP").size(), ' ');
      break;
    case IR::OpCode::JMP:
      out << "JMP" << std::string(16 - std::string("JMP").size(), ' ');
      break;
    case IR::OpCode::JEQ:
      out << "JEQ" << std::string(16 - std::string("JEQ").size(), ' ');
      break;
    case IR::OpCode::JNE:
      out << "JNE" << std::string(16 - std::string("JNE").size(), ' ');
      break;
    case IR::OpCode::JLE:
      out << "JLE" << std::string(16 - std::string("JLE").size(), ' ');
      break;
    case IR::OpCode::JLT:
      out << "JLT" << std::string(16 - std::string("JLT").size(), ' ');
      break;
    case IR::OpCode::JGE:
      out << "JGE" << std::string(16 - std::string("JGE").size(), ' ');
      break;
    case IR::OpCode::JGT:
      out << "JGT" << std::string(16 - std::string("JGT").size(), ' ');
      break;
    case IR::OpCode::MOVEQ:
      out << "MOVEQ" << std::string(16 - std::string("MOVEQ").size(), ' ');
      break;
    case IR::OpCode::MOVNE:
      out << "MOVNE" << std::string(16 - std::string("MOVNE").size(), ' ');
      break;
    case IR::OpCode::MOVLE:
      out << "MOVLE" << std::string(16 - std::string("MOVLE").size(), ' ');
      break;
    case IR::OpCode::MOVLT:
      out << "MOVLT" << std::string(16 - std::string("MOVLT").size(), ' ');
      break;
    case IR::OpCode::MOVGE:
      out << "MOVGE" << std::string(16 - std::string("MOVGE").size(), ' ');
      break;
    case IR::OpCode::MOVGT:
      out << "MOVGT" << std::string(16 - std::string("MOVGT").size(), ' ');
      break;
    case IR::OpCode::AND:
      out << "AND" << std::string(16 - std::string("AND").size(), ' ');
      break;
    case IR::OpCode::OR:
      out << "OR" << std::string(16 - std::string("OR").size(), ' ');
      break;
    case IR::OpCode::SAL:
      out << "SAL" << std::string(16 - std::string("SAL").size(), ' ');
      break;
    case IR::OpCode::SAR:
      out << "SAR" << std::string(16 - std::string("SAR").size(), ' ');
      break;
    case IR::OpCode::STORE:
      out << "STORE" << std::string(16 - std::string("STORE").size(), ' ');
      break;
    case IR::OpCode::LOAD:
      out << "LOAD" << std::string(16 - std::string("LOAD").size(), ' ');
      break;
    case IR::OpCode::RET:
      out << "RET" << std::string(16 - std::string("RET").size(), ' ');
      break;
    case IR::OpCode::LABEL:
      out << "LABEL" << std::string(16 - std::string("LABEL").size(), ' ');
      break;
    case IR::OpCode::DATA_BEGIN:
      out << "DATA_BEGIN"
          << std::string(16 - std::string("DATA_BEGIN").size(), ' ');
      break;
    case IR::OpCode::DATA_SPACE:
      out << "DATA_SPACE"
          << std::string(16 - std::string("DATA_SPACE").size(), ' ');
      break;
    case IR::OpCode::DATA_WORD:
      out << "DATA_WORD"
          << std::string(16 - std::string("DATA_WORD").size(), ' ');
      break;
    case IR::OpCode::DATA_END:
      out << "DATA_END"
          << std::string(16 - std::string("DATA_END").size(), ' ');
      break;
    case IR::OpCode::FUNCTION_BEGIN:
      out << "FUNCTION_BEGIN"
          << std::string(16 - std::string("FUNCTION_BEGIN").size(), ' ');
      break;
    case IR::OpCode::FUNCTION_END:
      out << "FUNCTION_END"
          << std::string(16 - std::string("FUNCTION_END").size(), ' ');
      break;
    case IR::OpCode::PHI_MOV:
      out << "PHI_MOV" << std::string(16 - std::string("PHI_MOV").size(), ' ');
      break;
    case IR::OpCode::NOOP:
      out << "NOOP" << std::string(16 - std::string("NOOP").size(), ' ');
      break;
    case IR::OpCode::INFO:
      out << "INFO" << std::string(16 - std::string("INFO").size(), ' ');
      break;
  }
#define F(op)                                       \
  if (this->op.type == OpName::Type::Imm) {         \
    out << this->op.value << '\t';                  \
  } else if (this->op.type == OpName::Type::Var) {  \
    out << this->op.name << '\t';                   \
  } else if (this->op.type == OpName::Type::Null) { \
    out << '\t';                                    \
  }
  F(dest);
  F(op1);
  F(op2);
  F(op3);
  out << this->label;
  out << std::endl;
}
