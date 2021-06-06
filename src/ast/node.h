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
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "ir/ir.h"

namespace syc {
namespace ir {
class Context;
}  // namespace ir
using INTEGER = std::int32_t;

namespace ast::node {
class BaseNode {
 public:
  virtual ~BaseNode();
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  void print_indentation(int indentation = 0, bool end = false,
                         std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class Expression : public BaseNode {
 public:
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
  struct CondResult {
    ir::OpCode then_op;
    ir::OpCode else_op;
  };
  virtual CondResult eval_cond_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Statement : public Expression {
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Declare : public BaseNode {};

class Identifier : public Expression {
 public:
  std::string name;
  Identifier(const std::string& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class ConditionExpression : public Expression {
 public:
  Expression& value;
  ConditionExpression(Expression& value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class BinaryExpression : public Expression {
 public:
  int op;
  Expression& lhs;
  Expression& rhs;
  BinaryExpression(Expression& lhs, int op, Expression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
  virtual CondResult eval_cond_runtime(ir::Context& ctx, ir::IRList& ir);
};

class UnaryExpression : public Expression {
 public:
  int op;
  Expression& rhs;
  UnaryExpression(int op, Expression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class CommaExpression : public Expression {
 public:
  std::vector<Expression*> values;
  CommaExpression() = default;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class FunctionCallArgList : public Expression {
 public:
  std::vector<Expression*> args;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionCall : public Expression {
 public:
  Identifier& name;
  FunctionCallArgList& args;
  FunctionCall(Identifier& name, FunctionCallArgList& args);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Number : public Expression {
 public:
  INTEGER value;
  Number(const std::string& value);
  Number(INTEGER value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Block : public Statement {
 public:
  std::vector<Statement*> statements;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class Assignment : public Statement {
 public:
  Identifier& lhs;
  Expression& rhs;
  Assignment(Identifier& lhs, Expression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class AfterInc : public Statement {
 public:
  int op;
  Identifier& lhs;
  AfterInc(Identifier& lhs, int op);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class IfElseStatement : public Statement {
 public:
  ConditionExpression& cond;
  Statement& thenstmt;
  Statement& elsestmt;
  IfElseStatement(ConditionExpression& cond, Statement& thenstmt,
                  Statement& elsestmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class WhileStatement : public Statement {
 public:
  ConditionExpression& cond;
  Statement& dostmt;
  WhileStatement(ConditionExpression& cond, Statement& dostmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class BreakStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ContinueStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ReturnStatement : public Statement {
 public:
  Expression* value;
  ReturnStatement(Expression* value = NULL);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class EvalStatement : public Statement {
 public:
  Expression& value;
  EvalStatement(Expression& value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class VoidStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class DeclareStatement : public Statement {
 public:
  std::vector<Declare*> list;
  int type;
  DeclareStatement(int type);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayDeclareInitValue : public Expression {
 public:
  bool is_number;
  Expression* value;
  std::vector<ArrayDeclareInitValue*> value_list;
  ArrayDeclareInitValue(bool is_number, Expression* value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class VarDeclareWithInit : public Declare {
 public:
  Identifier& name;
  Expression& value;
  bool is_const;
  VarDeclareWithInit(Identifier& name, Expression& value,
                     bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class VarDeclare : public Declare {
 public:
  Identifier& name;
  VarDeclare(Identifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayIdentifier : public Identifier {
 public:
  Identifier& name;
  std::vector<Expression*> shape;
  ArrayIdentifier(Identifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ir::Context& ctx);
  virtual ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
  void store_runtime(ir::OpName value, ir::Context& ctx, ir::IRList& ir);
};

class ArrayDeclareWithInit : public Declare {
 public:
  ArrayIdentifier& name;
  ArrayDeclareInitValue& value;
  bool is_const;
  ArrayDeclareWithInit(ArrayIdentifier& name, ArrayDeclareInitValue& value,
                       bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayDeclare : public Declare {
 public:
  ArrayIdentifier& name;
  ArrayDeclare(ArrayIdentifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class FunctionDefineArg : public Expression {
 public:
  int type;
  Identifier& name;
  FunctionDefineArg(int type, Identifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionDefineArgList : public Expression {
 public:
  std::vector<FunctionDefineArg*> list;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionDefine : public BaseNode {
 public:
  int return_type;
  Identifier& name;
  FunctionDefineArgList& args;
  Block& body;
  FunctionDefine(int return_type, Identifier& name, FunctionDefineArgList& args,
                 Block& body);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class Root : public BaseNode {
 public:
  std::vector<BaseNode*> body;
  virtual void print(int indentation = 0, bool end = true,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ir::Context& ctx, ir::IRList& ir);
};
}  // namespace ast::node
}  // namespace syc