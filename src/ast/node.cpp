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
#include "ast/node.h"

#include <cstdlib>
#include <stdexcept>
#include <stack>

#include "parser.hpp"

namespace {
// 子节点调用返回后，为本节点期间新产生的、尚未带位置（内层节点
// 未覆盖到）的 IR 补上本节点的源码位置——等价于原先“最内层活动
// AST 节点”的语义，且不依赖任何全局状态
void backfill_position(syc::ir::IRList& ir, size_t begin, int line, int column) {
  for (auto it = std::next(ir.begin(), (long)begin); it != ir.end(); ++it) {
    if (it->line == 0) {
      it->line = line;
      it->column = column;
    }
  }
}
}  // namespace
namespace syc::ast::node {
BaseNode::BaseNode(int line, int column) : line(line), column(column) {}
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
void BaseNode::generate_ir(ir::Context& ctx, ir::IRList& ir) {
  size_t begin = ir.size();
  this->_generate_ir(ctx, ir);
  backfill_position(ir, begin, this->line, this->column);
}

std::int64_t Expression::eval(ir::Context& ctx) {
  return this->_eval(ctx);
}

TypeInfo expr_type_of(Expression& e, ir::Context& ctx) {
  if (!e.etype_known) {
    e.etype = e._infer_type(ctx);
    e.etype_known = true;
  }
  return e.etype;
}

// 表达式类型的默认实现；具体表达式在 eval.cpp 中覆写
TypeInfo Expression::_infer_type(ir::Context&) { return TypeInfo{TypeId::INT}; }
ir::OpName Expression::eval_runtime(ir::Context& ctx, ir::IRList& ir) {
  size_t begin = ir.size();
  auto ret = this->_eval_runtime(ctx, ir);
  backfill_position(ir, begin, this->line, this->column);
  return ret;
};
Expression::CondResult Expression::eval_cond_runtime(ir::Context& ctx,
                                                     ir::IRList& ir) {
  size_t begin = ir.size();
  auto ret = this->_eval_cond_runtime(ctx, ir);
  backfill_position(ir, begin, this->line, this->column);
  return ret;
};

Identifier::Identifier(const std::string& name) : name(name) {}
void Identifier::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Identifier: " << this->name << std::endl;
}

ConditionExpression::ConditionExpression(std::shared_ptr<Expression> value)
    : value(std::move(value)) {}
void ConditionExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ConditionExpression" << std::endl;
  this->value->print(indentation + 1, true, out);
}

BinaryExpression::BinaryExpression(std::shared_ptr<Expression> lhs, int op,
                                   std::shared_ptr<Expression> rhs)
    : lhs(std::move(lhs)), rhs(std::move(rhs)), op(op) {}
void BinaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "BinaryExpression OP: " << this->op << std::endl;
  this->lhs->print(indentation + 1, false, out);
  this->rhs->print(indentation + 1, true, out);
}

UnaryExpression::UnaryExpression(int op, std::shared_ptr<Expression> rhs)
    : rhs(std::move(rhs)), op(op) {}
void UnaryExpression::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "UnaryExpression OP: " << this->op << std::endl;
  this->rhs->print(indentation + 1, true, out);
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

FunctionCall::FunctionCall(std::shared_ptr<Identifier> name,
                           std::shared_ptr<FunctionCallArgList> args)
    : name(std::move(name)), args(std::move(args)) {}
void FunctionCall::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionCall" << std::endl;
  name->print(indentation + 1, false, out);
  args->print(indentation + 1, false, out);
}

namespace {
// 浮点字面量判定：非十六进制且含 ./e/E 或 f/l 后缀（十六进制整数
// 如 0x1E5 中的 E 不是指数）
bool is_float_literal(const std::string& v) {
  if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0) return false;
  for (char c : v)
    if (c == '.' || c == 'e' || c == 'E' || c == 'f' || c == 'F') return true;
  return false;
}

// 解析整数字面量的 u/l 后缀并确定字面量类型
void parse_literal_suffix(const std::string& v, bool& uns, int& longs,
                          size_t& digits_end) {
  uns = false;
  longs = 0;
  size_t n = v.size();
  while (n > 0) {
    char c = v[n - 1];
    if (c == 'u' || c == 'U') {
      if (uns)
        throw std::runtime_error("invalid suffix '" + std::string(1, c) +
                                 "' on integer constant '" + v + "'");
      uns = true;
      n--;
    } else if (c == 'l' || c == 'L') {
      if (longs >= 2)
        throw std::runtime_error("invalid suffix on integer constant '" + v +
                                 "'");
      longs++;
      n--;
    } else {
      break;
    }
  }
  digits_end = n;
}
}  // namespace

Number::Number(const std::string& value_str) {
  // 浮点字面量：按 C99 规则确定类型——f/F 后缀为 float，
  // 其余（无后缀、l/L 即 long double）为 double；value 存位模式
  if (is_float_literal(value_str)) {
    bool is_f = !value_str.empty() &&
                (value_str.back() == 'f' || value_str.back() == 'F');
    std::string body = value_str;
    if (!body.empty() &&
        (body.back() == 'f' || body.back() == 'F' || body.back() == 'l' ||
         body.back() == 'L'))
      body.pop_back();
    double d = strtod(body.c_str(), nullptr);
    this->ty.id = is_f ? TypeId::FLOAT : TypeId::DOUBLE;
    // 按字面量类型规范化为位模式（float 为低 32 位模式）
    this->value = double_to_const_bits(d, this->ty.id);
    return;
  }
  bool uns;
  int longs;
  size_t digits_end;
  parse_literal_suffix(value_str, uns, longs, digits_end);
  std::string digits = value_str.substr(0, digits_end);
  unsigned long long magnitude = strtoull(digits.c_str(), nullptr, 0);
  // 类型规则模拟 C99（本平台 long 为 4 字节）：
  //  - 带 ll 后缀：LLONG/ULLONG
  //  - 其余：≤32 位值域内保持原有行为（无符号或超 int 正程即 UINT）
  //  - 超 32 位且无 u 后缀：十进制 → LLONG，十六进制 → LLONG；
  //    超 int64 正程 → ULLONG
  this->value = static_cast<std::int64_t>(magnitude);
  if (longs >= 2) {
    this->ty.id = uns ? TypeId::ULLONG : TypeId::LLONG;
  } else if (uns && longs == 1) {
    this->ty.id = magnitude <= 0xFFFFFFFFull ? TypeId::ULONG : TypeId::ULLONG;
  } else if (uns) {
    this->ty.id = magnitude <= 0xFFFFFFFFull ? TypeId::UINT : TypeId::ULLONG;
  } else if (magnitude <= 0x7FFFFFFFull) {
    this->ty.id = TypeId::INT;
  } else if (magnitude <= 0xFFFFFFFFull) {
    this->ty.id = TypeId::UINT;
  } else if (digits.rfind("0x", 0) == 0 || digits.rfind("0X", 0) == 0) {
    // 十六进制无后缀：先试 long long，再试 unsigned long long
    this->ty.id = magnitude <= 0x7FFFFFFFFFFFFFFFull ? TypeId::LLONG
                                                     : TypeId::ULLONG;
  } else {
    // 十进制无后缀：超出 int/long 值域 → long long
    this->ty.id = magnitude <= 0x7FFFFFFFFFFFFFFFull ? TypeId::LLONG
                                                     : TypeId::ULLONG;
  }
}
Number::Number(std::int64_t value) : value(value), ty{TypeId::INT} {}
void Number::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Number: " << value << " (" << types::name_of(this->ty.id) << ")"
      << std::endl;
}

TypeInfo Number::_infer_type(ir::Context&) { return this->ty; }

StringLiteral::StringLiteral(const std::string& value) : value(value) {}
void StringLiteral::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "StringLiteral: \"" << value << '"' << std::endl;
}

void Block::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Block" << std::endl;
  for (auto i = statements.begin(); i != statements.end(); i++)
    (*i)->print(indentation + 1, i + 1 == statements.end(), out);
}

Assignment::Assignment(std::shared_ptr<Identifier> lhs,
                       std::shared_ptr<Expression> rhs)
    : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
void Assignment::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Assignment" << std::endl;
  lhs->print(indentation + 1, false, out);
  rhs->print(indentation + 1, true, out);
}

AfterInc::AfterInc(std::shared_ptr<Identifier> lhs, int op)
    : lhs(std::move(lhs)), op(op) {}

void AfterInc::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "AfterInc op:" << op << std::endl;
  lhs->print(indentation + 1, false, out);
}

IfElseStatement::IfElseStatement(std::shared_ptr<ConditionExpression> cond,
                                 std::shared_ptr<Statement> thenstmt,
                                 std::shared_ptr<Statement> elsestmt)
    : cond(std::move(cond)), thenstmt(std::move(thenstmt)),
      elsestmt(std::move(elsestmt)) {}

void IfElseStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "IfElseStatement" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond->print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Then" << std::endl;
  thenstmt->print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, true, out);
  out << "Else" << std::endl;
  elsestmt->print(indentation + 2, true, out);
}

WhileStatement::WhileStatement(std::shared_ptr<ConditionExpression> cond,
                               std::shared_ptr<Statement> dostmt)
    : cond(std::move(cond)), dostmt(std::move(dostmt)) {}
void WhileStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "WhileStatement" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Cond" << std::endl;
  cond->print(indentation + 2, false, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Do" << std::endl;
  dostmt->print(indentation + 2, false, out);
}

void BreakStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Break" << std::endl;
}

void ContinueStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Continue" << std::endl;
}

ReturnStatement::ReturnStatement(std::shared_ptr<Expression> value)
    : value(std::move(value)) {}
void ReturnStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Return" << std::endl;
  if (value) value->print(indentation + 1, true, out);
}

EvalStatement::EvalStatement(std::shared_ptr<Expression> value)
    : value(std::move(value)) {}
void EvalStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Eval" << std::endl;
  value->print(indentation + 1, true, out);
}

void VoidStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Void" << std::endl;
}

DeclareStatement::DeclareStatement(TypeInfo ty) : ty(ty){};
void DeclareStatement::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Declare Type: " << types::name_of(ty.id) << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

ArrayDeclareInitValue::ArrayDeclareInitValue(bool is_number,
                                             std::shared_ptr<Expression> value)
    : is_number(is_number), value(std::move(value)) {}
void ArrayDeclareInitValue::print(int indentation, bool end,
                                  std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclareInitValue" << std::endl;
  if (is_number)
    this->value->print(indentation + 1, true, out);
  else
    for (auto i = value_list.begin(); i != value_list.end(); i++)
      (*i)->print(indentation + 1, i + 1 == value_list.end(), out);
}

VarDeclareWithInit::VarDeclareWithInit(std::shared_ptr<Identifier> name,
                                       std::shared_ptr<Expression> value,
                                       bool is_const)
    : name(std::move(name)), value(std::move(value)), is_const(is_const) {}
void VarDeclareWithInit::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "DeclareWithInit" << std::endl;
  name->print(indentation + 1, false, out);
  this->value->print(indentation + 1, true, out);
}

VarDeclare::VarDeclare(std::shared_ptr<Identifier> name)
    : name(std::move(name)) {}
void VarDeclare::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Declare" << std::endl;
  name->print(indentation + 1, true, out);
}

ArrayIdentifier::ArrayIdentifier(std::shared_ptr<Identifier> name)
    : Identifier(name->name), name(std::move(name)) {}
void ArrayIdentifier::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayIdentifier" << std::endl;
  this->name->print(indentation + 1, false, out);
  this->print_indentation(indentation + 1, true, out);
  out << "Shape" << std::endl;
  for (auto i = shape.begin(); i != shape.end(); i++)
    (*i)->print(indentation + 2, i + 1 == shape.end(), out);
}

ArrayDeclareWithInit::ArrayDeclareWithInit(
    std::shared_ptr<ArrayIdentifier> name,
    std::shared_ptr<ArrayDeclareInitValue> value, bool is_const)
    : name(std::move(name)), value(std::move(value)), is_const(is_const) {}
void ArrayDeclareWithInit::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclareWithInit" << std::endl;
  name->print(indentation + 1, false, out);
  this->value->print(indentation + 1, true, out);
}

ArrayDeclare::ArrayDeclare(std::shared_ptr<ArrayIdentifier> name)
    : name(std::move(name)) {}
void ArrayDeclare::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "ArrayDeclare" << std::endl;
  name->print(indentation + 1, true, out);
}

FunctionDefineArg::FunctionDefineArg(TypeInfo ty,
                                     std::shared_ptr<Identifier> name)
    : ty(ty), name(std::move(name)) {}
void FunctionDefineArg::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefineArg Type: " << types::name_of(ty.id) << std::endl;
  name->print(indentation + 1, true, out);
}

void FunctionDefineArgList::print(int indentation, bool end,
                                  std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefineArgList" << std::endl;
  for (auto i = list.begin(); i != list.end(); i++)
    (*i)->print(indentation + 1, i + 1 == list.end(), out);
}

FunctionDefine::FunctionDefine(TypeInfo return_ty,
                               std::shared_ptr<Identifier> name,
                               std::shared_ptr<FunctionDefineArgList> args,
                               std::shared_ptr<Block> body)
    : return_ty(return_ty), name(std::move(name)), args(std::move(args)),
      body(std::move(body)) {}
void FunctionDefine::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "FunctionDefine" << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Return type: " << types::name_of(return_ty.id) << std::endl;

  this->print_indentation(indentation + 1, false, out);
  out << "Name" << std::endl;
  name->print(indentation + 2, true, out);

  this->print_indentation(indentation + 1, false, out);
  out << "Args" << std::endl;
  args->print(indentation + 2, true, out);

  this->print_indentation(indentation + 1, true, out);
  out << "Body" << std::endl;
  body->print(indentation + 2, true, out);
}

void Root::print(int indentation, bool end, std::ostream& out) {
  this->print_indentation(indentation, end, out);
  out << "Root" << std::endl;
  for (auto i = body.begin(); i != body.end(); i++)
    (*i)->print(indentation + 1, i + 1 == body.end(), out);
}
}  // namespace syc::ast::node