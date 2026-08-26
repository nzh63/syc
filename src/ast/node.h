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
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "ast/type.h"
#include "ir/ir.h"

namespace syc {
namespace ir {
class Context;
}  // namespace ir
using INTEGER = std::int32_t;

using ast::types::TypeId;
using ast::types::TypeInfo;
// 提供 types::* 的限定访问（别名位于 syc 命名空间）
namespace types = ast::types;

namespace ast::node {
class BaseNode;
BaseNode* current();
class BaseNode {
 public:
  int line, column;
  // 位置默认 0；语法分析阶段由 parser.y 的 mknode 写入
  BaseNode(int line = 0, int column = 0);
  virtual ~BaseNode();
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  void print_indentation(int indentation = 0, bool end = false,
                         std::ostream& out = std::cerr);
  void generate_ir(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

// 所有表达式的静态类型在此缓存：ir 生成阶段按需推断一次。
TypeInfo expr_type_of(class Expression& e, ir::Context& ctx);

class Expression : public BaseNode {
 public:
  // 该表达式的静态类型（由 expr_type_of 推断并缓存）
  TypeInfo etype{};
  bool etype_known = false;

  std::int64_t eval(ir::Context& ctx);
  ir::OpName eval_runtime(ir::Context& ctx, ir::IRList& ir);
  struct CondResult {
    ir::OpCode then_op;
    ir::OpCode else_op;
  };
  CondResult eval_cond_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
  friend TypeInfo expr_type_of(class Expression& e, ir::Context& ctx);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);
  virtual CondResult _eval_cond_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Statement : public Expression {
 protected:
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);
};

class Declare : public BaseNode {
 public:
  // 该声明的元素类型（由 DeclareStatement 在生成 IR 前写入）
  TypeInfo ty{TypeId::INT};
};

class Identifier : public Expression {
 public:
  std::string name;
  Identifier(const std::string& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

// AST 子节点一律以 shared_ptr 持有（语义树可能存在共享子节点，如
// `++LVal` 会将同一个左值同时作为赋值目标与表达式操作数），
// Root 析构时整棵树自动释放，编译器可在同一进程内反复编译。
class ConditionExpression : public Expression {
 public:
  std::shared_ptr<Expression> value;
  ConditionExpression(std::shared_ptr<Expression> value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class BinaryExpression : public Expression {
 public:
  int op;
  std::shared_ptr<Expression> lhs;
  std::shared_ptr<Expression> rhs;
  BinaryExpression(std::shared_ptr<Expression> lhs, int op,
                   std::shared_ptr<Expression> rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);
  virtual CondResult _eval_cond_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class UnaryExpression : public Expression {
 public:
  int op;
  std::shared_ptr<Expression> rhs;
  UnaryExpression(int op, std::shared_ptr<Expression> rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class CommaExpression : public Expression {
 public:
  std::vector<std::shared_ptr<Expression>> values;
  CommaExpression() = default;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class FunctionCallArgList : public Expression {
 public:
  std::vector<std::shared_ptr<Expression>> args;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionCall : public Expression {
 public:
  std::shared_ptr<Identifier> name;
  std::shared_ptr<FunctionCallArgList> args;
  FunctionCall(std::shared_ptr<Identifier> name,
               std::shared_ptr<FunctionCallArgList> args);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class Number : public Expression {
 public:
  // 字面量取值（按后缀确定类型的完整值域，如 1ll、0x100000000）
  std::int64_t value;
  // 字面量自身类型（由后缀决定，如 1u、2ll）
  TypeInfo ty{TypeId::INT};
  Number(const std::string& value);
  Number(std::int64_t value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class StringLiteral : public Expression {
 public:
  std::string value;
  StringLiteral(const std::string& value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class Block : public Statement {
 public:
  std::vector<std::shared_ptr<Statement>> statements;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class Assignment : public Statement {
 public:
  std::shared_ptr<Identifier> lhs;
  std::shared_ptr<Expression> rhs;
  Assignment(std::shared_ptr<Identifier> lhs, std::shared_ptr<Expression> rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class AfterInc : public Statement {
 public:
  int op;
  std::shared_ptr<Identifier> lhs;
  AfterInc(std::shared_ptr<Identifier> lhs, int op);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class IfElseStatement : public Statement {
 public:
  std::shared_ptr<ConditionExpression> cond;
  std::shared_ptr<Statement> thenstmt;
  std::shared_ptr<Statement> elsestmt;
  IfElseStatement(std::shared_ptr<ConditionExpression> cond,
                  std::shared_ptr<Statement> thenstmt,
                  std::shared_ptr<Statement> elsestmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class WhileStatement : public Statement {
 public:
  std::shared_ptr<ConditionExpression> cond;
  std::shared_ptr<Statement> dostmt;
  WhileStatement(std::shared_ptr<ConditionExpression> cond,
                 std::shared_ptr<Statement> dostmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class BreakStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ContinueStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ReturnStatement : public Statement {
 public:
  std::shared_ptr<Expression> value;
  ReturnStatement(std::shared_ptr<Expression> value = nullptr);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class EvalStatement : public Statement {
 public:
  std::shared_ptr<Expression> value;
  EvalStatement(std::shared_ptr<Expression> value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class VoidStatement : public Statement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class DeclareStatement : public Statement {
 public:
  std::vector<std::shared_ptr<Declare>> list;
  TypeInfo ty{TypeId::INT};
  DeclareStatement(TypeInfo ty);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayDeclareInitValue : public Expression {
 public:
  bool is_number;
  std::shared_ptr<Expression> value;
  std::vector<std::shared_ptr<ArrayDeclareInitValue>> value_list;
  ArrayDeclareInitValue(bool is_number, std::shared_ptr<Expression> value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class VarDeclareWithInit : public Declare {
 public:
  std::shared_ptr<Identifier> name;
  std::shared_ptr<Expression> value;
  bool is_const;
  VarDeclareWithInit(std::shared_ptr<Identifier> name,
                     std::shared_ptr<Expression> value, bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class VarDeclare : public Declare {
 public:
  std::shared_ptr<Identifier> name;
  VarDeclare(std::shared_ptr<Identifier> name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayIdentifier : public Identifier {
 public:
  std::shared_ptr<Identifier> name;
  std::vector<std::shared_ptr<Expression>> shape;
  ArrayIdentifier(std::shared_ptr<Identifier> name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual std::int64_t _eval(ir::Context& ctx);
  virtual ir::OpName _eval_runtime(ir::Context& ctx, ir::IRList& ir);
  void store_runtime(ir::OpName value, ir::Context& ctx, ir::IRList& ir,
                     types::TypeId src_ty = types::TypeId::INT);

 protected:
  virtual TypeInfo _infer_type(ir::Context& ctx);
};

class ArrayDeclareWithInit : public Declare {
 public:
  std::shared_ptr<ArrayIdentifier> name;
  std::shared_ptr<ArrayDeclareInitValue> value;
  bool is_const;
  ArrayDeclareWithInit(std::shared_ptr<ArrayIdentifier> name,
                       std::shared_ptr<ArrayDeclareInitValue> value,
                       bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class ArrayDeclare : public Declare {
 public:
  std::shared_ptr<ArrayIdentifier> name;
  ArrayDeclare(std::shared_ptr<ArrayIdentifier> name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class FunctionDefineArg : public Expression {
 public:
  TypeInfo ty{TypeId::INT};
  std::shared_ptr<Identifier> name;
  FunctionDefineArg(TypeInfo ty, std::shared_ptr<Identifier> name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionDefineArgList : public Expression {
 public:
  std::vector<std::shared_ptr<FunctionDefineArg>> list;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class FunctionDefine : public BaseNode {
 public:
  TypeInfo return_ty{TypeId::INT};
  std::shared_ptr<Identifier> name;
  std::shared_ptr<FunctionDefineArgList> args;
  std::shared_ptr<Block> body;
  FunctionDefine(TypeInfo return_ty, std::shared_ptr<Identifier> name,
                 std::shared_ptr<FunctionDefineArgList> args,
                 std::shared_ptr<Block> body);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};

class Root : public BaseNode {
 public:
  std::vector<std::shared_ptr<BaseNode>> body;
  virtual void print(int indentation = 0, bool end = true,
                     std::ostream& out = std::cerr);

 protected:
  virtual void _generate_ir(ir::Context& ctx, ir::IRList& ir);
};
}  // namespace ast::node
}  // namespace syc