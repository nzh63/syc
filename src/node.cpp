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
#include "node.h"

namespace SYC::Node {
BaseNode::~BaseNode() {}
void BaseNode::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "[node Node]" << std::endl;
}
void BaseNode::print_indentation(int indentation, bool end, std::ostream& out) {
  for (int i = 0; i < indentation; i++) {
    out << "│   ";
  }
  if (end)
    out << "└──";
  else
    out << "├──";
}

Identifier::Identifier(const std::string& name) : name(name) {}
void Identifier::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Identifier: " << this->name << std::endl;
}

ConditionExpression::ConditionExpression(Expression& value) : value(value) {}
void ConditionExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ConditionExpression" << std::endl;
  this->value.print(indentation + 1, true, out);
}

BinaryExpression::BinaryExpression(Expression& lhs, int op, Expression& rhs)
    : lhs(lhs), rhs(rhs), op(op) {}
void BinaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "BinaryExpression OP: " << this->op << std::endl;
  this->lhs.print(indentation + 1, false, out);
  this->rhs.print(indentation + 1, true, out);
}

UnaryExpression::UnaryExpression(int op, Expression& rhs) : rhs(rhs), op(op) {}
void UnaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "UnaryExpression OP: " << this->op << std::endl;
  this->rhs.print(indentation + 1, true, out);
}

void CommaExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "CommaExpression" << std::endl;
  for (auto v : values) v->print(indentation + 1, true, out);
}

void FunctionCallArgList::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionCallArgList" << std::endl;
  for (auto i = args.begin(); i != args.end(); i++)
    (*i)->print(indentation + 1, i + 1 == args.end(), out);
}

FunctionCall::FunctionCall(Identifier& name, FunctionCallArgList& args)
    : name(name), args(args) {}
void FunctionCall::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionCall" << std::endl;
  name.print(indentation + 1, false, out);
  args.print(indentation + 1, false, out);
}

Number::Number(const std::string& value) : value(std::stoi(value, 0, 0)) {}
Number::Number(INTEGER value) : value(value) {}
void Number::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Number: " << value << std::endl;
}

void Block::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Block" << std::endl;
  for (auto i = statements.begin(); i != statements.end(); i++)
    (*i)->print(indentation + 1, i + 1 == statements.end(), out);
}

Assignment::Assignment(Identifier& lhs, Expression& rhs) : lhs(lhs), rhs(rhs) {}
void Assignment::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Assignment" << std::endl;
  lhs.print(indentation + 1, false, out);
  rhs.print(indentation + 1, true, out);
}

AfterInc::AfterInc(Identifier& lhs, int op) : lhs(lhs), op(op) {}

void AfterInc::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "AfterInc op:" << op << std::endl;
  lhs.print(indentation + 1, false, out);
}

IfElseStatement::IfElseStatement(ConditionExpression& cond, Statement& thenstmt,
                                 Statement& elsestmt)
    : cond(cond), thenstmt(thenstmt), elsestmt(elsestmt) {}

void IfElseStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "IfElseStatement" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond.print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Then" << std::endl;
  thenstmt.print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, true, out);
  out << "Else" << std::endl;
  elsestmt.print(indentation + 2, true, out);
}

WhileStatement::WhileStatement(ConditionExpression& cond, Statement& dostmt)
    : cond(cond), dostmt(dostmt) {}
void WhileStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "WhileStatement" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond.print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Do" << std::endl;
  dostmt.print(indentation + 2, false, out);
}

void BreakStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Break" << std::endl;
}

void ContinueStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Continue" << std::endl;
}

ReturnStatement::ReturnStatement(Expression* value) : value(value) {}
void ReturnStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Return" << std::endl;
  if (value) value->print(indentation + 1, true, out);
}

EvalStatement::EvalStatement(Expression& value) : value(value) {}
void EvalStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Eval" << std::endl;
  value.print(indentation + 1, true, out);
}

void VoidStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Void" << std::endl;
}

DeclareStatement::DeclareStatement(int type) : type(type){};
void DeclareStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Declare Type: " << type << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

ArrayDeclareInitValue::ArrayDeclareInitValue(bool is_number, Expression* value)
    : is_number(is_number), value(value) {}
void ArrayDeclareInitValue::print(int indentation, bool end,
                                  std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclareInitValue" << std::endl;
  if (is_number)
    value->print(indentation + 1, true, out);
  else
    for (auto i = value_list.begin(); i != value_list.end(); i++)
      (*i)->print(indentation + 1, i + 1 == value_list.end(), out);
}

VarDeclareWithInit::VarDeclareWithInit(Identifier& name, Expression& value,
                                       bool is_const)
    : name(name), value(value), is_const(is_const) {}
void VarDeclareWithInit::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "DeclareWithInit" << std::endl;
  name.print(indentation + 1, false, out);
  value.print(indentation + 1, true, out);
}

VarDeclare::VarDeclare(Identifier& name) : name(name) {}
void VarDeclare::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Declare" << std::endl;
  name.print(indentation + 1, true, out);
}

ArrayIdentifier::ArrayIdentifier(Identifier& name)
    : Identifier(name), name(name) {}
void ArrayIdentifier::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayIdentifier" << std::endl;
  name.print(indentation + 1, false, out);
  this->print_indentation(indentation + 1, true, out);
  out << "Shape" << std::endl;
  for (auto i = shape.begin(); i != shape.end(); i++)
    (*i)->print(indentation + 2, i + 1 == shape.end(), out);
}

ArrayDeclareWithInit::ArrayDeclareWithInit(ArrayIdentifier& name,
                                           ArrayDeclareInitValue& value,
                                           bool is_const)
    : name(name), value(value), is_const(is_const) {}
void ArrayDeclareWithInit::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclareWithInit" << std::endl;
  name.print(indentation + 1, false, out);
  value.print(indentation + 1, true, out);
}

ArrayDeclare::ArrayDeclare(ArrayIdentifier& name) : name(name) {}
void ArrayDeclare::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclare" << std::endl;
  name.print(indentation + 1, true, out);
}

FunctionDefineArg::FunctionDefineArg(int type, Identifier& name)
    : type(type), name(name) {}
void FunctionDefineArg::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefineArg Type: " << type << std::endl;
  name.print(indentation + 1, true, out);
}

void FunctionDefineArgList::print(int indentation, bool end,
                                  std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefineArgList" << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

FunctionDefine::FunctionDefine(int return_type, Identifier& name,
                               FunctionDefineArgList& args, Block& body)
    : return_type(return_type), name(name), args(args), body(body) {}
void FunctionDefine::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefine" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Return type: " << return_type << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Name" << std::endl;
  name.print(indentation + 2, true, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Args" << std::endl;
  args.print(indentation + 2, true, out);

  this->print_indentation(indentation + 1, true, out);
  out << "Body" << std::endl;
  body.print(indentation + 2, true, out);
}

void Root::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Root" << std::endl;
  for (auto i = body.begin(); i != body.end(); i++)
    (*i)->print(indentation + 1, i + 1 == body.end(), out);
}
}  // namespace SYC::Node