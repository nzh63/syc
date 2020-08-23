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
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "context_ir.h"
#include "ir.h"

using INTEGER = std::int32_t;

class Node {
 public:
  virtual ~Node();
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  void printIndentation(int indentation = 0, bool end = false,
                        std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NExpression : public Node {
 public:
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
  struct CondResult {
    IR::OpCode then_op;
    IR::OpCode else_op;
  };
  virtual CondResult eval_cond_runntime(ContextIR& ctx, IRList& ir);
};

class NStatement : public NExpression {
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NDeclare : public Node {};

class NIdentifier : public NExpression {
 public:
  std::string name;
  NIdentifier(const std::string& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NConditionExpression : public NExpression {
 public:
  NExpression& value;
  NConditionExpression(NExpression& value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NBinaryExpression : public NExpression {
 public:
  int op;
  NExpression& lhs;
  NExpression& rhs;
  NBinaryExpression(NExpression& lhs, int op, NExpression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
  virtual CondResult eval_cond_runntime(ContextIR& ctx, IRList& ir);
};

class NUnaryExpression : public NExpression {
 public:
  int op;
  NExpression& rhs;
  NUnaryExpression(int op, NExpression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NCommaExpression : public NExpression {
 public:
  std::vector<NExpression*> values;
  NCommaExpression() = default;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NFunctionCallArgList : public NExpression {
 public:
  std::vector<NExpression*> args;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class NFunctionCall : public NExpression {
 public:
  NIdentifier& name;
  NFunctionCallArgList& args;
  NFunctionCall(NIdentifier& name, NFunctionCallArgList& args);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NNumber : public NExpression {
 public:
  INTEGER value;
  NNumber(const std::string& value);
  NNumber(INTEGER value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NBlock : public NStatement {
 public:
  std::vector<NStatement*> statements;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NAssignment : public NStatement {
 public:
  NIdentifier& lhs;
  NExpression& rhs;
  NAssignment(NIdentifier& lhs, NExpression& rhs);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NAfterInc : public NStatement {
 public:
  int op;
  NIdentifier& lhs;
  NAfterInc(NIdentifier& lhs, int op);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NIfElseStatement : public NStatement {
 public:
  NConditionExpression& cond;
  NStatement& thenstmt;
  NStatement& elsestmt;
  NIfElseStatement(NConditionExpression& cond, NStatement& thenstmt,
                   NStatement& elsestmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NIfStatement : public NStatement {
 public:
  NConditionExpression& cond;
  NStatement& thenstmt;
  NIfStatement(NConditionExpression& cond, NStatement& thenstmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NWhileStatement : public NStatement {
 public:
  NConditionExpression& cond;
  NStatement& dostmt;
  NWhileStatement(NConditionExpression& cond, NStatement& dostmt);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NBreakStatement : public NStatement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NContinueStatement : public NStatement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NReturnStatement : public NStatement {
 public:
  NExpression* value;
  NReturnStatement(NExpression* value = NULL);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NEvalStatement : public NStatement {
 public:
  NExpression& value;
  NEvalStatement(NExpression& value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
};

class NVoidStatement : public NStatement {
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NDeclareStatement : public NStatement {
 public:
  std::vector<NDeclare*> list;
  int type;
  NDeclareStatement(int type);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NArrayDeclareInitValue : public NExpression {
 public:
  bool is_number;
  NExpression* value;
  std::vector<NArrayDeclareInitValue*> value_list;
  NArrayDeclareInitValue(bool is_number, NExpression* value);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class NVarDeclareWithInit : public NDeclare {
 public:
  NIdentifier& name;
  NExpression& value;
  bool is_const;
  NVarDeclareWithInit(NIdentifier& name, NExpression& value,
                      bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NVarDeclare : public NDeclare {
 public:
  NIdentifier& name;
  NVarDeclare(NIdentifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NArrayIdentifier : public NIdentifier {
 public:
  NIdentifier& name;
  std::vector<NExpression*> shape;
  NArrayIdentifier(NIdentifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual int eval(ContextIR& ctx);
  virtual OpName eval_runntime(ContextIR& ctx, IRList& ir);
  void store_runntime(OpName value, ContextIR& ctx, IRList& ir);
};

class NArrayDeclareWithInit : public NDeclare {
 public:
  NArrayIdentifier& name;
  NArrayDeclareInitValue& value;
  bool is_const;
  NArrayDeclareWithInit(NArrayIdentifier& name, NArrayDeclareInitValue& value,
                        bool is_const = false);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NArrayDeclare : public NDeclare {
 public:
  NArrayIdentifier& name;
  NArrayDeclare(NArrayIdentifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NFunctionDefineArg : public NExpression {
 public:
  int type;
  NIdentifier& name;
  NFunctionDefineArg(int type, NIdentifier& name);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class NFunctionDefineArgList : public NExpression {
 public:
  std::vector<NFunctionDefineArg*> list;
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
};

class NFunctionDefine : public Node {
 public:
  int return_type;
  NIdentifier& name;
  NFunctionDefineArgList& args;
  NBlock& body;
  NFunctionDefine(int return_type, NIdentifier& name,
                  NFunctionDefineArgList& args, NBlock& body);
  virtual void print(int indentation = 0, bool end = false,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};

class NRoot : public Node {
 public:
  std::vector<Node*> body;
  virtual void print(int indentation = 0, bool end = true,
                     std::ostream& out = std::cerr);
  virtual void generate_ir(ContextIR& ctx, IRList& ir);
};