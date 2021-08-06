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
#include "ir/ir.h"

#include <cassert>

namespace syc::ir {
OpName::OpName() : type(OpName::Type::Null) {}
OpName::OpName(std::string name) : type(OpName::Type::Var), name(name) {}
OpName::OpName(int value) : type(OpName::Type::Imm), value(value) {}
bool OpName::is_var() const { return this->type == OpName::Type::Var; }
bool OpName::is_local_var() const {
  return this->is_var() &&
         (this->name.starts_with('%') || this->name.starts_with("$arg"));
}
bool OpName::is_global_var() const {
  return this->is_var() && this->name.starts_with('@');
}
bool OpName::is_imm() const { return this->type == OpName::Type::Imm; }
bool OpName::is_null() const { return this->type == OpName::Type::Null; }

bool OpName::operator==(const OpName& other) const {
  if (this->type != other.type) return false;
  if (this->is_var()) {
    return this->name == other.name;
  } else if (this->is_imm()) {
    return this->value == other.value;
  } else {
    assert(this->is_null());
    return true;
  }
}

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
bool IR::some(decltype(&syc::ir::OpName::is_var) callback,
              bool include_dest) const {
  return this->some([callback](OpName op) { return (op.*callback)(); },
                    include_dest);
}
bool IR::some(std::function<bool(const syc::ir::OpName&)> callback,
              bool include_dest) const {
  return (include_dest && callback(this->dest)) || callback(this->op1) ||
         callback(this->op2) || callback(this->op3);
}
void IR::forEachOp(std::function<void(const syc::ir::OpName&)> callback,
                   bool include_dest) const {
  this->some(
      [callback](const syc::ir::OpName& op) {
        callback(op);
        return false;
      },
      include_dest);
}

void IR::print(std::ostream& out, bool verbose) const {
  switch (this->op_code) {
    case OpCode::MALLOC_IN_STACK:
      out << "MALLOC_IN_STACK"
          << std::string(16 - std::string("MALLOC_IN_STACK").size(), ' ');
      break;
    case OpCode::MOV:
      out << "MOV" << std::string(16 - std::string("MOV").size(), ' ');
      break;
    case OpCode::ADD:
      out << "ADD" << std::string(16 - std::string("ADD").size(), ' ');
      break;
    case OpCode::SUB:
      out << "SUB" << std::string(16 - std::string("SUB").size(), ' ');
      break;
    case OpCode::IMUL:
      out << "IMUL" << std::string(16 - std::string("IMUL").size(), ' ');
      break;
    case OpCode::IDIV:
      out << "IDIV" << std::string(16 - std::string("IDIV").size(), ' ');
      break;
    case OpCode::MOD:
      out << "MOD" << std::string(16 - std::string("MOD").size(), ' ');
      break;
    case OpCode::SET_ARG:
      out << "SET_ARG" << std::string(16 - std::string("SET_ARG").size(), ' ');
      break;
    case OpCode::CALL:
      out << "CALL" << std::string(16 - std::string("CALL").size(), ' ');
      break;
    case OpCode::CMP:
      out << "CMP" << std::string(16 - std::string("CMP").size(), ' ');
      break;
    case OpCode::JMP:
      out << "JMP" << std::string(16 - std::string("JMP").size(), ' ');
      break;
    case OpCode::JEQ:
      out << "JEQ" << std::string(16 - std::string("JEQ").size(), ' ');
      break;
    case OpCode::JNE:
      out << "JNE" << std::string(16 - std::string("JNE").size(), ' ');
      break;
    case OpCode::JLE:
      out << "JLE" << std::string(16 - std::string("JLE").size(), ' ');
      break;
    case OpCode::JLT:
      out << "JLT" << std::string(16 - std::string("JLT").size(), ' ');
      break;
    case OpCode::JGE:
      out << "JGE" << std::string(16 - std::string("JGE").size(), ' ');
      break;
    case OpCode::JGT:
      out << "JGT" << std::string(16 - std::string("JGT").size(), ' ');
      break;
    case OpCode::MOVEQ:
      out << "MOVEQ" << std::string(16 - std::string("MOVEQ").size(), ' ');
      break;
    case OpCode::MOVNE:
      out << "MOVNE" << std::string(16 - std::string("MOVNE").size(), ' ');
      break;
    case OpCode::MOVLE:
      out << "MOVLE" << std::string(16 - std::string("MOVLE").size(), ' ');
      break;
    case OpCode::MOVLT:
      out << "MOVLT" << std::string(16 - std::string("MOVLT").size(), ' ');
      break;
    case OpCode::MOVGE:
      out << "MOVGE" << std::string(16 - std::string("MOVGE").size(), ' ');
      break;
    case OpCode::MOVGT:
      out << "MOVGT" << std::string(16 - std::string("MOVGT").size(), ' ');
      break;
    case OpCode::AND:
      out << "AND" << std::string(16 - std::string("AND").size(), ' ');
      break;
    case OpCode::OR:
      out << "OR" << std::string(16 - std::string("OR").size(), ' ');
      break;
    case OpCode::SAL:
      out << "SAL" << std::string(16 - std::string("SAL").size(), ' ');
      break;
    case OpCode::SAR:
      out << "SAR" << std::string(16 - std::string("SAR").size(), ' ');
      break;
    case OpCode::STORE:
      out << "STORE" << std::string(16 - std::string("STORE").size(), ' ');
      break;
    case OpCode::LOAD:
      out << "LOAD" << std::string(16 - std::string("LOAD").size(), ' ');
      break;
    case OpCode::RET:
      out << "RET" << std::string(16 - std::string("RET").size(), ' ');
      break;
    case OpCode::LABEL:
      out << "LABEL" << std::string(16 - std::string("LABEL").size(), ' ');
      break;
    case OpCode::DATA_BEGIN:
      out << "DATA_BEGIN"
          << std::string(16 - std::string("DATA_BEGIN").size(), ' ');
      break;
    case OpCode::DATA_SPACE:
      out << "DATA_SPACE"
          << std::string(16 - std::string("DATA_SPACE").size(), ' ');
      break;
    case OpCode::DATA_WORD:
      out << "DATA_WORD"
          << std::string(16 - std::string("DATA_WORD").size(), ' ');
      break;
    case OpCode::DATA_END:
      out << "DATA_END"
          << std::string(16 - std::string("DATA_END").size(), ' ');
      break;
    case OpCode::FUNCTION_BEGIN:
      out << "FUNCTION_BEGIN"
          << std::string(16 - std::string("FUNCTION_BEGIN").size(), ' ');
      break;
    case OpCode::FUNCTION_END:
      out << "FUNCTION_END"
          << std::string(16 - std::string("FUNCTION_END").size(), ' ');
      break;
    case OpCode::PHI_MOV:
      out << "PHI_MOV" << std::string(16 - std::string("PHI_MOV").size(), ' ');
      break;
    case OpCode::NOOP:
      out << "NOOP" << std::string(16 - std::string("NOOP").size(), ' ');
      break;
    case OpCode::INFO:
      out << "INFO" << std::string(16 - std::string("INFO").size(), ' ');
      break;
  }
  this->forEachOp([&out](const OpName& op) {
    if (op.is_imm()) {
      out << op.value << '\t';
    } else if (op.is_var()) {
      out << op.name << '\t';
    } else if (op.is_null()) {
      out << '\t';
    }
  });
  out << this->label;
  out << std::endl;
}
}  // namespace syc::ir
