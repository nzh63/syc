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

#include "ast/node.h"

namespace syc::ir {
OpName::OpName() : type(OpName::Type::Null) {}
OpName::OpName(std::string name) : type(OpName::Type::Var), name(name) {}
OpName::OpName(int value) : type(OpName::Type::Imm), value(value) {}
OpName::OpName(std::int64_t value) : type(OpName::Type::Imm), value(value) {}
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
    : op_code(op_code), dest(dest), op1(op1), op2(op2), op3(op3), label(label) {
  this->setup_file_postion();
}
IR::IR(OpCode op_code, OpName dest, OpName op1, OpName op2, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(op2),
      op3(OpName()),
      label(label) {
  this->setup_file_postion();
}
IR::IR(OpCode op_code, OpName dest, OpName op1, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(OpName()),
      op3(OpName()),
      label(label) {
  this->setup_file_postion();
}
IR::IR(OpCode op_code, OpName dest, std::string label)
    : op_code(op_code),
      dest(dest),
      op1(OpName()),
      op2(OpName()),
      op3(OpName()),
      label(label) {
  this->setup_file_postion();
}
IR::IR(OpCode op_code, std::string label)
    : op_code(op_code),
      dest(OpName()),
      op1(OpName()),
      op2(OpName()),
      op3(OpName()),
      label(label) {
  this->setup_file_postion();
}
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

#define PRINT_OP(op) case OpCode::op: out << #op << std::string(16 - std::string(#op).size(), ' '); break
void IR::print(std::ostream& out, bool verbose) const {
  switch (this->op_code) {
    PRINT_OP(MALLOC_IN_STACK);
    PRINT_OP(MOV);
    PRINT_OP(ADD);
    PRINT_OP(SUB);
    PRINT_OP(IMUL);
    PRINT_OP(IDIV);
    PRINT_OP(MOD);
    PRINT_OP(UDIV);
    PRINT_OP(UMOD);
    PRINT_OP(TRUNC_U8);
    PRINT_OP(TRUNC_S8);
    PRINT_OP(TRUNC_U16);
    PRINT_OP(TRUNC_S16);
    PRINT_OP(SEXT);
    PRINT_OP(ZEXT);
    PRINT_OP(FDIV);
    PRINT_OP(FNEG);
    PRINT_OP(SITOF);
    PRINT_OP(UITOF);
    PRINT_OP(F2I);
    PRINT_OP(F2U);
    PRINT_OP(F2F);
    PRINT_OP(SET_ARG);
    PRINT_OP(CALL);
    PRINT_OP(CMP);
    PRINT_OP(JMP);
    PRINT_OP(JEQ);
    PRINT_OP(JNE);
    PRINT_OP(JLE);
    PRINT_OP(JLT);
    PRINT_OP(JGE);
    PRINT_OP(JGT);
    PRINT_OP(JMPHS);
    PRINT_OP(JMPLO);
    PRINT_OP(JMPLS);
    PRINT_OP(JMPI);
    PRINT_OP(MOVEQ);
    PRINT_OP(MOVNE);
    PRINT_OP(MOVLE);
    PRINT_OP(MOVLT);
    PRINT_OP(MOVGE);
    PRINT_OP(MOVGT);
    PRINT_OP(MOVHS);
    PRINT_OP(MOVLO);
    PRINT_OP(MOVLS);
    PRINT_OP(MOVHI);
    PRINT_OP(AND);
    PRINT_OP(OR);
    PRINT_OP(SAL);
    PRINT_OP(SAR);
    PRINT_OP(LSR);
    PRINT_OP(STORE);
    PRINT_OP(LOAD);
    PRINT_OP(RET);
    PRINT_OP(LABEL);
    PRINT_OP(DATA_BEGIN);
    PRINT_OP(DATA_SPACE);
    PRINT_OP(DATA_WORD);
    PRINT_OP(DATA_STRING);
    PRINT_OP(DATA_END);
    PRINT_OP(FUNCTION_BEGIN);
    PRINT_OP(FUNCTION_END);
    PRINT_OP(PHI_MOV);
    PRINT_OP(NOOP);
    PRINT_OP(INFO);
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
  if (ast::types::is_floating(ast::types::canonical(this->ty.id)) ||
      width_bytes(ast::types::canonical(this->ty.id)) == 8) {
    out << "  ; " << ast::types::name_of(this->ty.id);
  }
  out << std::endl;
}
// 位置不再在 IR 构造期填入；由 ast::node 的 generate_ir/eval_runtime
// 包装函数按子节点覆盖范围回填（见 node.cpp 的 backfill_position）
void IR::setup_file_postion() {
  this->line = 0;
  this->column = 0;
}
}  // namespace syc::ir
