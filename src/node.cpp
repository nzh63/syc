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

Node::~Node() {}
void Node::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "[node Node]" << std::endl;
}
void Node::printIndentation(int indentation, bool end, std::ostream& out) {
  for (int i = 0; i < indentation; i++) {
    out << "│   ";
  }
  if (end)
    out << "└──";
  else
    out << "├──";
}

NIdentifier::NIdentifier(const std::string& name) : name(name) {}
void NIdentifier::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Identifier: " << this->name << std::endl;
}

NConditionExpression::NConditionExpression(NExpression& value) : value(value) {}
void NConditionExpression::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "ConditionExpression" << std::endl;
  this->value.print(indentation + 1, true, out);
}

NBinaryExpression::NBinaryExpression(NExpression& lhs, int op, NExpression& rhs)
    : lhs(lhs), rhs(rhs), op(op) {}
void NBinaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "BinaryExpression OP: " << this->op << std::endl;
  this->lhs.print(indentation + 1, false, out);
  this->rhs.print(indentation + 1, true, out);
}

NUnaryExpression::NUnaryExpression(int op, NExpression& rhs)
    : rhs(rhs), op(op) {}
void NUnaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "UnaryExpression OP: " << this->op << std::endl;
  this->rhs.print(indentation + 1, true, out);
}

void NCommaExpression::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "CommaExpression" << std::endl;
  for (auto v : values) v->print(indentation + 1, true, out);
}

void NFunctionCallArgList::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "FunctionCallArgList" << std::endl;
  for (auto i = args.begin(); i != args.end(); i++)
    (*i)->print(indentation + 1, i + 1 == args.end(), out);
}

NFunctionCall::NFunctionCall(NIdentifier& name, NFunctionCallArgList& args)
    : name(name), args(args) {}
void NFunctionCall::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "FunctionCall" << std::endl;
  name.print(indentation + 1, false, out);
  args.print(indentation + 1, false, out);
}

NNumber::NNumber(const std::string& value) : value(std::stoi(value, 0, 0)) {}
NNumber::NNumber(INTEGER value) : value(value) {}
void NNumber::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Number: " << value << std::endl;
}

void NBlock::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Block" << std::endl;
  for (auto i = statements.begin(); i != statements.end(); i++)
    (*i)->print(indentation + 1, i + 1 == statements.end(), out);
}

NAssignment::NAssignment(NIdentifier& lhs, NExpression& rhs)
    : lhs(lhs), rhs(rhs) {}
void NAssignment::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Assignment" << std::endl;
  lhs.print(indentation + 1, false, out);
  rhs.print(indentation + 1, true, out);
}

NAfterInc::NAfterInc(NIdentifier& lhs, int op) : lhs(lhs), op(op) {}

void NAfterInc::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "AfterInc op:" << op << std::endl;
  lhs.print(indentation + 1, false, out);
}

NIfElseStatement::NIfElseStatement(NConditionExpression& cond,
                                   NStatement& thenstmt, NStatement& elsestmt)
    : cond(cond), thenstmt(thenstmt), elsestmt(elsestmt) {}

void NIfElseStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "IfElseStatement" << std::endl;

  this->printIndentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond.print(indentation + 2, false, out);

  this->printIndentation(indentation + 1, false, out);
  out << "Then" << std::endl;
  thenstmt.print(indentation + 2, false, out);

  this->printIndentation(indentation + 1, true, out);
  out << "Else" << std::endl;
  elsestmt.print(indentation + 2, true, out);
}

NIfStatement::NIfStatement(NConditionExpression& cond, NStatement& thenstmt)
    : cond(cond), thenstmt(thenstmt) {}
void NIfStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "IfStatement" << std::endl;

  this->printIndentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond.print(indentation + 2, false, out);

  this->printIndentation(indentation + 1, false, out);
  out << "Then" << std::endl;
  thenstmt.print(indentation + 2, false, out);
}

NWhileStatement::NWhileStatement(NConditionExpression& cond, NStatement& dostmt)
    : cond(cond), dostmt(dostmt) {}
void NWhileStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "WhileStatement" << std::endl;

  this->printIndentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond.print(indentation + 2, false, out);

  this->printIndentation(indentation + 1, false, out);
  out << "Do" << std::endl;
  dostmt.print(indentation + 2, false, out);
}

void NBreakStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Break" << std::endl;
}

void NContinueStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Continue" << std::endl;
}

NReturnStatement::NReturnStatement(NExpression* value) : value(value) {}
void NReturnStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Return" << std::endl;
  if (value) value->print(indentation + 1, true, out);
}

NEvalStatement::NEvalStatement(NExpression& value) : value(value) {}
void NEvalStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Eval" << std::endl;
  value.print(indentation + 1, true, out);
}

void NVoidStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Void" << std::endl;
}

NDeclareStatement::NDeclareStatement(int type) : type(type){};
void NDeclareStatement::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Declare Type: " << type << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

NArrayDeclareInitValue::NArrayDeclareInitValue(bool is_number,
                                               NExpression* value)
    : is_number(is_number), value(value) {}
void NArrayDeclareInitValue::print(int indentation, bool end,
                                   std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "ArrayDeclareInitValue" << std::endl;
  if (is_number)
    value->print(indentation + 1, true, out);
  else
    for (auto i = value_list.begin(); i != value_list.end(); i++)
      (*i)->print(indentation + 1, i + 1 == value_list.end(), out);
}

NVarDeclareWithInit::NVarDeclareWithInit(NIdentifier& name, NExpression& value,
                                         bool is_const)
    : name(name), value(value), is_const(is_const) {}
void NVarDeclareWithInit::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "DeclareWithInit" << std::endl;
  name.print(indentation + 1, false, out);
  value.print(indentation + 1, true, out);
}

NVarDeclare::NVarDeclare(NIdentifier& name) : name(name) {}
void NVarDeclare::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Declare" << std::endl;
  name.print(indentation + 1, true, out);
}

NArrayIdentifier::NArrayIdentifier(NIdentifier& name)
    : NIdentifier(name), name(name) {}
void NArrayIdentifier::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "ArrayIdentifier" << std::endl;
  name.print(indentation + 1, false, out);
  this->printIndentation(indentation + 1, true, out);
  out << "Shape" << std::endl;
  for (auto i = shape.begin(); i != shape.end(); i++)
    (*i)->print(indentation + 2, i + 1 == shape.end(), out);
}

NArrayDeclareWithInit::NArrayDeclareWithInit(NArrayIdentifier& name,
                                             NArrayDeclareInitValue& value,
                                             bool is_const)
    : name(name), value(value), is_const(is_const) {}
void NArrayDeclareWithInit::print(int indentation, bool end,
                                  std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "ArrayDeclareWithInit" << std::endl;
  name.print(indentation + 1, false, out);
  value.print(indentation + 1, true, out);
}

NArrayDeclare::NArrayDeclare(NArrayIdentifier& name) : name(name) {}
void NArrayDeclare::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "ArrayDeclare" << std::endl;
  name.print(indentation + 1, true, out);
}

NFunctionDefineArg::NFunctionDefineArg(int type, NIdentifier& name)
    : type(type), name(name) {}
void NFunctionDefineArg::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "FunctionDefineArg Type: " << type << std::endl;
  name.print(indentation + 1, true, out);
}

void NFunctionDefineArgList::print(int indentation, bool end,
                                   std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "FunctionDefineArgList" << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

NFunctionDefine::NFunctionDefine(int return_type, NIdentifier& name,
                                 NFunctionDefineArgList& args, NBlock& body)
    : return_type(return_type), name(name), args(args), body(body) {}
void NFunctionDefine::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "FunctionDefine" << std::endl;

  this->printIndentation(indentation + 1, false, out);
  out << "Return type: " << return_type << std::endl;

  this->printIndentation(indentation + 1, false, out);
  out << "Name" << std::endl;
  name.print(indentation + 2, true, out);

  this->printIndentation(indentation + 1, false, out);
  out << "Args" << std::endl;
  args.print(indentation + 2, true, out);

  this->printIndentation(indentation + 1, true, out);
  out << "Body" << std::endl;
  body.print(indentation + 2, true, out);
}

void NRoot::print(int indentation, bool end, std::ostream& out) {
  this->printIndentation(indentation, end, out);
  out << "Root" << std::endl;
  for (auto i = body.begin(); i != body.end(); i++)
    (*i)->print(indentation + 1, i + 1 == body.end(), out);
}