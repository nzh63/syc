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
#include <cassert>
#include <exception>
#include <unordered_set>

#include "config.h"
#include "context_ir.h"
#include "ir.h"
#include "node.h"
#include "optimize_ir.h"
//
#include "parser.hpp"

void Node::generate_ir(ContextIR& ctx, IRList& ir) {
  this->print();
  throw std::runtime_error("Can't generate IR for this node.");
}

void NRoot::generate_ir(ContextIR& ctx, IRList& ir) {
  for (auto& i : this->body) {
    i->generate_ir(ctx, ir);
  }
}

void NVarDeclare::generate_ir(ContextIR& ctx, IRList& ir) {
  if (ctx.is_global()) {
    ir.emplace_back(IR::OpCode::DATA_BEGIN, "@" + this->name.name);
    ir.emplace_back(IR::OpCode::DATA_WORD, OpName(0));
    ir.emplace_back(IR::OpCode::DATA_END);
    ctx.insert_symbol(this->name.name, VarInfo("@" + this->name.name));
  } else {
    ctx.insert_symbol(this->name.name,
                      VarInfo("%" + std::to_string(ctx.get_id())));
  }
}

void NVarDeclareWithInit::generate_ir(ContextIR& ctx, IRList& ir) {
  if (ctx.is_global()) {
    ir.emplace_back(IR::OpCode::DATA_BEGIN, "@" + this->name.name);
    ir.emplace_back(IR::OpCode::DATA_WORD, OpName(this->value.eval(ctx)));
    ir.emplace_back(IR::OpCode::DATA_END);
    ctx.insert_symbol(this->name.name, VarInfo("@" + this->name.name));
    if (this->is_const) {
      ctx.insert_const(this->name.name, ConstInfo(this->value.eval(ctx)));
    }
  } else {
    ctx.insert_symbol(this->name.name,
                      VarInfo("%" + std::to_string(ctx.get_id())));
    NAssignment(this->name, this->value).generate_ir(ctx, ir);
    if (this->is_const) {
      ctx.insert_const(this->name.name, ConstInfo(this->value.eval(ctx)));
    }
  }
}

void NArrayDeclare::generate_ir(ContextIR& ctx, IRList& ir) {
  std::vector<int> shape;
  for (auto i : this->name.shape) shape.push_back(i->eval(ctx));
  int size = 1;
  for (auto i : shape) size *= i;
  if (ctx.is_global()) {
    ir.emplace_back(IR::OpCode::DATA_BEGIN, "@&" + this->name.name.name);
    ir.emplace_back(IR::OpCode::DATA_SPACE, size * 4);
    ir.emplace_back(IR::OpCode::DATA_END);
    ctx.insert_symbol(this->name.name.name,
                      VarInfo("@&" + this->name.name.name, true, shape));
  } else {
    ctx.insert_symbol(
        this->name.name.name,
        VarInfo("%&" + std::to_string(ctx.get_id()), true, shape));
    ir.push_back(IR(IR::OpCode::MALLOC_IN_STACK,
                    OpName(ctx.find_symbol(this->name.name.name).name),
                    size * 4));
  }
}

namespace {
void ArrayDeclareWithInit(NArrayDeclareWithInit& that,
                          std::vector<NArrayDeclareInitValue*> v,
                          std::vector<NExpression*> shape,
                          std::vector<int>& init_value, int index,
                          ContextIR& ctx, IRList& ir) {
  const auto output_const = [&](int value, bool is_space = false) {
    if (is_space) {
      if (ctx.is_global()) {
        for (int i = 0; i < value; i++) init_value.push_back(0);
        ir.emplace_back(IR::OpCode::DATA_SPACE, value * 4);
      } else {
        for (int i = 0; i < value; i++) {
          init_value.push_back(0);
        }
      }
    } else {
      init_value.push_back(value);
      if (ctx.is_global())
        ir.emplace_back(IR::OpCode::DATA_WORD, value);
      else
        ir.emplace_back(IR::OpCode::STORE, OpName(),
                        OpName(ctx.find_symbol(that.name.name.name).name),
                        init_value.size() * 4 - 4, value);
    }
  };
  const auto output = [&](OpName value, bool is_space = false) {
    if (is_space) {
      if (ctx.is_global()) {
        for (int i = 0; i < value.value; i++) init_value.push_back(0);
        ir.emplace_back(IR::OpCode::DATA_SPACE, value.value * 4);
      } else {
        for (int i = 0; i < value.value; i++) {
          init_value.push_back(0);
        }
      }
    } else {
      init_value.push_back(0);
      if (ctx.is_global())
        ir.emplace_back(IR::OpCode::DATA_WORD, value);
      else
        ir.emplace_back(IR::OpCode::STORE, OpName(),
                        OpName(ctx.find_symbol(that.name.name.name).name),
                        init_value.size() * 4 - 4, value);
    }
  };

  if (index >= shape.size()) return;
  int size = 1, write_size = 0;
  for (auto it = shape.begin() + index; it != shape.end(); it++)
    size *= (*it)->eval(ctx);
  int size_this_shape = size / shape[index]->eval(ctx);
  for (auto i : v) {
    if (i->is_number) {
      write_size++;
      try {
        if (that.is_const)
          output_const(i->value->eval(ctx));
        else
          output(i->value->eval(ctx));
      } catch (...) {
        output(i->value->eval_runntime(ctx, ir));
      }
    } else {
      if (write_size % size_this_shape != 0) {
        if (that.is_const)
          output_const(size_this_shape - (write_size % size_this_shape), true);
        else
          output(size_this_shape - (write_size % size_this_shape), true);
      }
      ArrayDeclareWithInit(that, i->value_list, shape, init_value, index + 1,
                           ctx, ir);
      write_size += size_this_shape;
    }
  }
  if (that.is_const)
    output_const(size - write_size, true);
  else
    output(size - write_size, true);
}
}  // namespace

void NArrayDeclareWithInit::generate_ir(ContextIR& ctx, IRList& ir) {
  std::vector<int> shape;
  for (auto i : this->name.shape) shape.push_back(i->eval(ctx));
  int size = 1;
  for (auto i : shape) size *= i;

  std::vector<int> init_value;
  if (ctx.is_global()) {
    ir.emplace_back(IR::OpCode::DATA_BEGIN, "@&" + this->name.name.name);
    ArrayDeclareWithInit(*this, this->value.value_list, this->name.shape,
                         init_value, 0, ctx, ir);
    ir.emplace_back(IR::OpCode::DATA_END);
    ctx.insert_symbol(this->name.name.name,
                      VarInfo("@&" + this->name.name.name, true, shape));
  } else {
    ctx.insert_symbol(
        this->name.name.name,
        VarInfo("%&" + std::to_string(ctx.get_id()), true, shape));
    ir.emplace_back(IR::OpCode::MALLOC_IN_STACK,
                    OpName(ctx.find_symbol(this->name.name.name).name),
                    size * 4);
    ir.emplace_back(IR::OpCode::SET_ARG, 0,
                    OpName(ctx.find_symbol(this->name.name.name).name));
    ir.emplace_back(IR::OpCode::SET_ARG, 1, OpName(0));
    ir.emplace_back(IR::OpCode::SET_ARG, 2, OpName(size * 4));
    ir.emplace_back(IR::OpCode::CALL, "memset");
    ArrayDeclareWithInit(*this, this->value.value_list, this->name.shape,
                         init_value, 0, ctx, ir);
  }
}

void NFunctionDefine::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.create_scope();
  int arg_len = this->args.list.size();
  ir.emplace_back(IR::OpCode::FUNCTION_BEGIN, OpName(), arg_len,
                  this->name.name);
  for (int i = 0; i < arg_len; i++) {
    auto identifier =
        dynamic_cast<NArrayIdentifier*>(&this->args.list[i]->name);
    if (identifier) {
      std::vector<int> shape;
      for (auto i : identifier->shape) shape.push_back(i->eval(ctx));
      auto tmp = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(IR::OpCode::MOV, tmp, OpName("$arg" + std::to_string(i)));
      ctx.insert_symbol(identifier->name.name, VarInfo(tmp, true, shape));
      ir.emplace_back(IR::OpCode::INFO, "NOT CONSTEXPR");
    } else {
      auto tmp = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(IR::OpCode::MOV, tmp, OpName("$arg" + std::to_string(i)));
      ctx.insert_symbol(this->args.list[i]->name.name, VarInfo(tmp));
    }
  }
  this->body.generate_ir(ctx, ir);
  if (this->return_type == INT) {
    ir.emplace_back(IR::OpCode::RET, 0);
  } else {
    ir.emplace_back(IR::OpCode::RET);
  }
  ir.emplace_back(IR::OpCode::FUNCTION_END, this->name.name);
  ctx.end_scope();
}

void NBlock::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.create_scope();
  for (auto i : this->statements) i->generate_ir(ctx, ir);
  ctx.end_scope();
}

void NDeclareStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  for (auto i : this->list) i->generate_ir(ctx, ir);
}

void NVoidStatement::generate_ir(ContextIR& ctx, IRList& ir) {}

void NEvalStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  this->value.eval_runntime(ctx, ir);
}

void NReturnStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  if (this->value != NULL)
    ir.emplace_back(IR::OpCode::RET, OpName(),
                    this->value->eval_runntime(ctx, ir));
  else
    ir.emplace_back(IR::OpCode::RET);
}

void NContinueStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.loop_continue_symbol_snapshot.top().push_back(ctx.symbol_table);
  for (auto& i : ctx.loop_continue_phi_move.top()) {
    ir.emplace_back(
        IR::OpCode::MOV, i.second,
        OpName(
            ctx.symbol_table[i.first.first].find(i.first.second)->second.name));
  }
  ir.emplace_back(IR::OpCode::JMP,
                  "LOOP_" + ctx.loop_label.top() + "_CONTINUE");
}

void NBreakStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.loop_break_symbol_snapshot.top().push_back(ctx.symbol_table);
  for (auto& i : ctx.loop_break_phi_move.top()) {
    ir.emplace_back(
        IR::OpCode::MOV, i.second,
        OpName(
            ctx.symbol_table[i.first.first].find(i.first.second)->second.name));
  }
  ir.emplace_back(IR::OpCode::JMP, "LOOP_" + ctx.loop_label.top() + "_END");
}

void NWhileStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.create_scope();
  ctx.loop_label.push(std::to_string(ctx.get_id()));
  ctx.loop_var.push({});

  // 此处的块与基本快有所不同，见下图
  /*      ┌───────────┐
     COND:│ cmp       │
          ├───────────┤
      JMP:│ // jne DO │
          │ jeq END   │
          ├───────────┤
       DO:│ ...       │
          │ break;    │
          │ continue; │
          ├───────────┤
 CONTINUE:│ jmp COND  │
          ├───────────┤
      END:│           │
          └───────────┘
  */
  // 在 `jne END`、`break;`、`continue`前均需要插入 PHI_MOV

  // BRFORE
  ContextIR ctx_before = ctx;
  IRList ir_before;

  // COND
  ContextIR ctx_cond = ctx_before;
  IRList ir_cond;
  ir_cond.emplace_back(IR::OpCode::LABEL,
                       "LOOP_" + ctx.loop_label.top() + "_BEGIN");
  auto cond = this->cond.eval_cond_runntime(ctx_cond, ir_cond);

  // JMP
  IRList ir_jmp;
  ir_jmp.emplace_back(cond.else_op, "LOOP_" + ctx.loop_label.top() + "_END");

  // DO (fake)
  ContextIR ctx_do_fake = ctx_cond;
  IRList ir_do_fake;
  ctx_do_fake.loop_continue_symbol_snapshot.push({});
  ctx_do_fake.loop_break_symbol_snapshot.push({});
  ctx_do_fake.loop_continue_phi_move.push({});
  ctx_do_fake.loop_break_phi_move.push({});
  this->dostmt.generate_ir(ctx_do_fake, ir_do_fake);
  ctx_do_fake.loop_continue_symbol_snapshot.top().push_back(
      ctx_do_fake.symbol_table);

  // DO
  ContextIR ctx_do = ctx_cond;
  IRList ir_do;
  ctx_do.loop_continue_symbol_snapshot.push({});
  ctx_do.loop_break_symbol_snapshot.push({});
  ctx_do.loop_continue_phi_move.push({});
  ctx_do.loop_break_phi_move.push({});
  for (int i = 0; i < ctx_do_fake.symbol_table.size(); i++) {
    for (auto& symbol : ctx_do_fake.symbol_table[i]) {
      for (int j = 0;
           j < ctx_do_fake.loop_continue_symbol_snapshot.top().size(); j++) {
        if (symbol.second.name !=
            ctx_do_fake.loop_continue_symbol_snapshot.top()[j][i]
                .find(symbol.first)
                ->second.name) {
          ctx_do.loop_continue_phi_move.top().insert(
              {{i, symbol.first}, "%" + std::to_string(ctx_do.get_id())});
          break;
        }
      }
    }
  }
  for (int i = 0; i < ctx_cond.symbol_table.size(); i++) {
    for (auto& symbol : ctx_cond.symbol_table[i]) {
      for (int j = 0; j < ctx_do_fake.loop_break_symbol_snapshot.top().size();
           j++) {
        const auto do_name = ctx_do_fake.loop_break_symbol_snapshot.top()[j][i]
                                 .find(symbol.first)
                                 ->second.name;
        if (symbol.second.name != do_name) {
          ctx_do.loop_break_phi_move.top().insert(
              {{i, symbol.first}, symbol.second.name});
          break;
        }
      }
    }
  }
  ir_do.emplace_back(IR::OpCode::LABEL, "LOOP_" + ctx.loop_label.top() + "_DO");
  this->dostmt.generate_ir(ctx_do, ir_do);
  for (auto& i : ctx_do.loop_continue_phi_move.top()) {
    ir_do.emplace_back(IR::OpCode::PHI_MOV, i.second,
                       OpName(ctx_do.symbol_table[i.first.first]
                                  .find(i.first.second)
                                  ->second.name));
  }

  for (auto& i : ctx_do.loop_continue_phi_move.top()) {
    ctx_do.symbol_table[i.first.first].find(i.first.second)->second.name =
        i.second;
  }

  for (auto& i : ctx_do.loop_break_phi_move.top()) {
    ctx_cond.symbol_table[i.first.first].find(i.first.second)->second.name =
        i.second;
  }

  ctx_do.loop_continue_symbol_snapshot.pop();
  ctx_do.loop_break_symbol_snapshot.pop();
  ctx_do.loop_continue_phi_move.pop();
  ctx_do.loop_break_phi_move.pop();

  // CONTINUE
  ContextIR ctx_continue = ctx_do;
  IRList ir_continue;
  ir_continue.emplace_back(IR::OpCode::LABEL,
                           "LOOP_" + ctx.loop_label.top() + "_CONTINUE");

  ir_cond.clear();
  ir_cond.emplace_back(IR::OpCode::LABEL,
                       "LOOP_" + ctx.loop_label.top() + "_BEGIN");

  for (int i = 0; i < ctx_before.symbol_table.size(); i++) {
    for (const auto& symbol_before : ctx_before.symbol_table[i]) {
      const auto& symbo_continue =
          *ctx_continue.symbol_table[i].find(symbol_before.first);
      if (symbol_before.second.name != symbo_continue.second.name) {
        const std::string new_name = "%" + std::to_string(ctx_before.get_id());
        ir_before.emplace_back(IR::OpCode::PHI_MOV, new_name,
                               OpName(symbol_before.second.name));
        ir_before.back().phi_block = ir_cond.begin();
        ir_continue.emplace_back(IR::OpCode::PHI_MOV, new_name,
                                 OpName(symbo_continue.second.name));
        ctx_before.symbol_table[i].find(symbol_before.first)->second.name =
            new_name;
        ctx_before.loop_var.top().push_back(new_name);
      }
    }
  }
  ir_continue.emplace_back(IR::OpCode::JMP,
                           "LOOP_" + ctx.loop_label.top() + "_BEGIN");
  ir_continue.emplace_back(IR::OpCode::LABEL,
                           "LOOP_" + ctx.loop_label.top() + "_END");

  //////////////////////////////////////////////////////////////////////

  // COND real
  ctx_cond = ctx_before;
  cond = this->cond.eval_cond_runntime(ctx_cond, ir_cond);

  // JMP real
  ir_jmp.clear();
  ir_jmp.emplace_back(cond.else_op, "LOOP_" + ctx.loop_label.top() + "_END");

  // DO (fake) real
  ctx_do_fake = ctx_cond;
  ir_do_fake.clear();
  ctx_do_fake.loop_continue_symbol_snapshot.push({});
  ctx_do_fake.loop_break_symbol_snapshot.push({});
  ctx_do_fake.loop_continue_phi_move.push({});
  ctx_do_fake.loop_break_phi_move.push({});
  this->dostmt.generate_ir(ctx_do_fake, ir_do_fake);
  ctx_do_fake.loop_continue_symbol_snapshot.top().push_back(
      ctx_do_fake.symbol_table);

  // DO real
  ctx_do = ctx_cond;
  ir_do.clear();
  ir_continue.clear();
  ir_continue.emplace_back(IR::OpCode::NOOP);
  IRList end;
  end.emplace_back(IR::OpCode::LABEL, "LOOP_" + ctx.loop_label.top() + "_END");
  ctx_do.loop_continue_symbol_snapshot.push({});
  ctx_do.loop_break_symbol_snapshot.push({});
  ctx_do.loop_continue_phi_move.push({});
  ctx_do.loop_break_phi_move.push({});
  for (int i = 0; i < ctx_do_fake.symbol_table.size(); i++) {
    for (auto& symbol : ctx_do_fake.symbol_table[i]) {
      for (int j = 0;
           j < ctx_do_fake.loop_continue_symbol_snapshot.top().size(); j++) {
        if (symbol.second.name !=
            ctx_do_fake.loop_continue_symbol_snapshot.top()[j][i]
                .find(symbol.first)
                ->second.name) {
          ctx_do.loop_continue_phi_move.top().insert(
              {{i, symbol.first}, "%" + std::to_string(ctx_do.get_id())});
          break;
        }
      }
    }
  }
  for (int i = 0; i < ctx_cond.symbol_table.size(); i++) {
    for (auto& symbol : ctx_cond.symbol_table[i]) {
      for (int j = 0; j < ctx_do_fake.loop_break_symbol_snapshot.top().size();
           j++) {
        const auto do_name = ctx_do_fake.loop_break_symbol_snapshot.top()[j][i]
                                 .find(symbol.first)
                                 ->second.name;
        if (symbol.second.name != do_name) {
          ctx_do.loop_break_phi_move.top().insert(
              {{i, symbol.first}, symbol.second.name});
          break;
        }
      }
    }
  }
  ir_do.emplace_back(IR::OpCode::LABEL, "LOOP_" + ctx.loop_label.top() + "_DO");
  this->dostmt.generate_ir(ctx_do, ir_do);
  for (auto& i : ctx_do.loop_continue_phi_move.top()) {
    ir_do.emplace_back(IR::OpCode::PHI_MOV, i.second,
                       OpName(ctx_do.symbol_table[i.first.first]
                                  .find(i.first.second)
                                  ->second.name));
    ir_do.back().phi_block = end.begin();
  }
  for (auto& i : ctx_do.loop_continue_phi_move.top()) {
    ctx_do.symbol_table[i.first.first].find(i.first.second)->second.name =
        i.second;
  }

  for (auto& i : ctx_do.loop_break_phi_move.top()) {
    ctx_cond.symbol_table[i.first.first].find(i.first.second)->second.name =
        i.second;
  }

  ctx_do.loop_continue_symbol_snapshot.pop();
  ctx_do.loop_break_symbol_snapshot.pop();
  ctx_do.loop_continue_phi_move.pop();
  ctx_do.loop_break_phi_move.pop();

  // CONTINUE real
  ctx_continue = ctx_do;
  ir_continue.emplace_back(IR::OpCode::LABEL,
                           "LOOP_" + ctx.loop_label.top() + "_CONTINUE");
  for (int i = 0; i < ctx_before.symbol_table.size(); i++) {
    for (const auto& symbol_before : ctx_before.symbol_table[i]) {
      const auto& symbo_continue =
          *ctx_continue.symbol_table[i].find(symbol_before.first);
      if (symbol_before.second.name != symbo_continue.second.name) {
        ir_continue.emplace_back(IR::OpCode::PHI_MOV, symbol_before.second.name,
                                 OpName(symbo_continue.second.name));
        ir_continue.back().phi_block = ir_cond.begin();
      }
    }
  }
  ir_continue.emplace_back(IR::OpCode::JMP,
                           "LOOP_" + ctx.loop_label.top() + "_BEGIN");

  /////////////////////////////////////////////////////////

  optimize_loop_ir(ir_before, ir_cond, ir_jmp, ir_do, ir_continue);

  /////////////////////////////////////////////////////////

  // 为 continue 块增加假读以延长其生命周期
  std::unordered_set<std::string> writed;
  for (const auto& irs : std::vector<IRList*>{&ir_cond, &ir_jmp, &ir_do}) {
    for (const auto& i : *irs) {
      if (i.dest.type == OpName::Type::Var) writed.insert(i.dest.name);
#define F(op1)                                                     \
  if (i.op1.type == OpName::Type::Var) {                           \
    if (!writed.count(i.op1.name)) {                               \
      ir_continue.emplace_back(IR::OpCode::NOOP, OpName(), i.op1); \
    }                                                              \
  }
      F(op1);
      F(op2);
      F(op3);
#undef F
    }
  }

  /////////////////////////////////////////////////////////

  ctx = ctx_cond;
  ctx.id = ctx_continue.id;
  ir.splice(ir.end(), ir_before);
  ir.splice(ir.end(), ir_cond);
  ir.splice(ir.end(), ir_jmp);
  ir.splice(ir.end(), ir_do);
  ir.splice(ir.end(), ir_continue);
  ir.splice(ir.end(), end);

  ctx.loop_var.pop();
  ctx.loop_label.pop();
  ctx.end_scope();
}

void NIfStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.create_scope();

  auto id = std::to_string(ctx.get_id());

  auto cond = this->cond.eval_cond_runntime(ctx, ir);

  ir.emplace_back(cond.else_op, "IF_" + id + "_ELSE");

  IRList ir_then, ir_else;
  ContextIR ctx_then = ctx, ctx_else = ctx;

  ctx_then.create_scope();
  this->thenstmt.generate_ir(ctx_then, ir_then);
  ctx_then.end_scope();

  ctx_else.id = ctx_then.id;
  ctx_else.create_scope();
  // this->elsestmt.generate_ir(ctx_else, ir_else);
  ctx_else.end_scope();

  ctx.id = ctx_else.id;

  IRList end;
  end.emplace_back(IR::OpCode::LABEL, "IF_" + id + "_END");

  for (int i = 0; i < ctx_then.symbol_table.size(); i++) {
    for (auto& s : ctx_then.symbol_table[i]) {
      if (s.second.name !=
          ctx_else.symbol_table[i].find(s.first)->second.name) {
        auto& v = ctx.find_symbol(s.first);
        assert(!v.is_array);
        if (v.name[0] == '%') v.name = "%" + std::to_string(ctx.get_id());
        ir_then.emplace_back(IR::OpCode::PHI_MOV, v.name,
                             OpName(s.second.name));
        ir_then.back().phi_block = end.begin();
        ir_else.emplace_back(
            IR::OpCode::PHI_MOV, v.name,
            OpName(ctx_else.symbol_table[i].find(s.first)->second.name));
        ir_else.back().phi_block = end.begin();
      }
    }
  }

  ir.splice(ir.end(), ir_then);
  if (!ir_else.empty()) ir.emplace_back(IR::OpCode::JMP, "IF_" + id + "_END");
  ir.emplace_back(IR::OpCode::LABEL, "IF_" + id + "_ELSE");
  ir.splice(ir.end(), ir_else);
  ir.splice(ir.end(), end);

  if (!ctx.loop_break_symbol_snapshot.empty()) {
    auto& br = ctx.loop_break_symbol_snapshot.top();
    auto& then_br = ctx_then.loop_break_symbol_snapshot.top();
    auto& else_br = ctx_else.loop_break_symbol_snapshot.top();
    br.insert(br.end(), then_br.begin(), then_br.end());
    br.insert(br.end(), else_br.begin(), else_br.end());
    auto& co = ctx.loop_continue_symbol_snapshot.top();
    auto& then_co = ctx_then.loop_continue_symbol_snapshot.top();
    auto& else_co = ctx_else.loop_continue_symbol_snapshot.top();
    co.insert(co.end(), then_co.begin(), then_co.end());
    co.insert(co.end(), else_co.begin(), else_co.end());
  }

  ctx.end_scope();
}

void NIfElseStatement::generate_ir(ContextIR& ctx, IRList& ir) {
  ctx.create_scope();

  auto id = std::to_string(ctx.get_id());

  auto cond = this->cond.eval_cond_runntime(ctx, ir);

  ir.emplace_back(cond.else_op, "IF_" + id + "_ELSE");

  IRList ir_then, ir_else;
  ContextIR ctx_then = ctx, ctx_else = ctx;

  ctx_then.create_scope();
  this->thenstmt.generate_ir(ctx_then, ir_then);
  ctx_then.end_scope();

  ctx_else.id = ctx_then.id;
  ctx_else.create_scope();
  this->elsestmt.generate_ir(ctx_else, ir_else);
  ctx_else.end_scope();

  ctx.id = ctx_else.id;

  IRList end;
  end.emplace_back(IR::OpCode::LABEL, "IF_" + id + "_END");

  for (int i = 0; i < ctx_then.symbol_table.size(); i++) {
    for (auto& s : ctx_then.symbol_table[i]) {
      if (s.second.name !=
          ctx_else.symbol_table[i].find(s.first)->second.name) {
        auto& v = ctx.find_symbol(s.first);
        assert(!v.is_array);
        if (v.name[0] == '%') v.name = "%" + std::to_string(ctx.get_id());
        ir_then.emplace_back(IR::OpCode::PHI_MOV, v.name,
                             OpName(s.second.name));
        ir_then.back().phi_block = end.begin();
        ir_else.emplace_back(
            IR::OpCode::PHI_MOV, v.name,
            OpName(ctx_else.symbol_table[i].find(s.first)->second.name));
        ir_else.back().phi_block = end.begin();
      }
    }
  }

  ir.splice(ir.end(), ir_then);
  if (!ir_else.empty()) ir.emplace_back(IR::OpCode::JMP, "IF_" + id + "_END");
  ir.emplace_back(IR::OpCode::LABEL, "IF_" + id + "_ELSE");
  ir.splice(ir.end(), ir_else);
  ir.splice(ir.end(), end);

  if (!ctx.loop_break_symbol_snapshot.empty()) {
    auto& br = ctx.loop_break_symbol_snapshot.top();
    auto& then_br = ctx_then.loop_break_symbol_snapshot.top();
    auto& else_br = ctx_else.loop_break_symbol_snapshot.top();
    br.insert(br.end(), then_br.begin(), then_br.end());
    br.insert(br.end(), else_br.begin(), else_br.end());
    auto& co = ctx.loop_continue_symbol_snapshot.top();
    auto& then_co = ctx_then.loop_continue_symbol_snapshot.top();
    auto& else_co = ctx_else.loop_continue_symbol_snapshot.top();
    co.insert(co.end(), then_co.begin(), then_co.end());
    co.insert(co.end(), else_co.begin(), else_co.end());
  }

  ctx.end_scope();
}

void NAssignment::generate_ir(ContextIR& ctx, IRList& ir) {
  if (dynamic_cast<NArrayIdentifier*>(&this->lhs)) {
    auto rhs = this->rhs.eval_runntime(ctx, ir);
    dynamic_cast<NArrayIdentifier*>(&this->lhs)->store_runntime(rhs, ctx, ir);
  } else {
    auto rhs = this->rhs.eval_runntime(ctx, ir);
    auto& v = ctx.find_symbol(this->lhs.name);
    if (v.is_array) {
      throw std::runtime_error("Can't assign to a array.");
    } else {
      if (rhs.type == OpName::Type::Var && rhs.name[0] == '%' &&
          (v.name[0] == '%' || v.name.substr(0, 4) == "$arg") &&
          v.name[0] != '@') {
        if (ctx.in_loop()) {
          bool lhs_is_loop_var = false, rhs_is_loop_vae = false;
          int lhs_level = -1, rhs_level = -1;
          for (const auto& i : ctx.loop_var.top()) {
            if (i == v.name) lhs_is_loop_var = true;
            if (i == rhs.name) rhs_is_loop_vae = true;
          }
          if (lhs_is_loop_var && rhs_is_loop_vae) {
            v.name = "%" + std::to_string(ctx.get_id());
            ir.emplace_back(IR::OpCode::MOV, v.name, rhs);
          } else {
            v.name = rhs.name;
          }
        } else {
          v.name = rhs.name;
        }
      } else if (v.name[0] == '@') {
        ir.emplace_back(IR::OpCode::MOV, v.name, rhs);
      } else {
        v.name = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(IR::OpCode::MOV, v.name, rhs);
        if (config::optimize_level > 0 && rhs.type == OpName::Type::Imm) {
          ctx.insert_const_assign(v.name, rhs.value);
        }
      }
    }
  }
}

void NAfterInc::generate_ir(ContextIR& ctx, IRList& ir) {
  auto n0 = new NNumber(1);
  auto n1 = new NBinaryExpression(lhs, this->op, *n0);
  auto n2 = new NAssignment(lhs, *n1);
  n2->generate_ir(ctx, ir);
  delete n2;
  delete n1;
  delete n0;
}

void NArrayIdentifier::store_runntime(OpName value, ContextIR& ctx,
                                      IRList& ir) {
  auto v = ctx.find_symbol(this->name.name);
  if (v.is_array) {
    if (this->shape.size() == v.shape.size()) {
      if (config::optimize_level > 0) {
        try {
          int index = 0, size = 4;
          for (int i = this->shape.size() - 1; i >= 0; i--) {
            index += this->shape[i]->eval(ctx) * size;
            size *= v.shape[i];
          }
          ir.emplace_back(IR::OpCode::STORE, OpName(), v.name, index, value);
          return;
        } catch (...) {
        }
      }
      OpName index = "%" + std::to_string(ctx.get_id());
      OpName size = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(
          IR::OpCode::SAL, index,
          this->shape[this->shape.size() - 1]->eval_runntime(ctx, ir), 2);
      if (this->shape.size() != 1) {
        OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(IR::OpCode::MOV, size,
                        4 * v.shape[this->shape.size() - 1]);
      }
      for (int i = this->shape.size() - 2; i >= 0; i--) {
        OpName tmp = "%" + std::to_string(ctx.get_id());
        OpName tmp2 = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(IR::OpCode::IMUL, tmp, size,
                        this->shape[i]->eval_runntime(ctx, ir));
        ir.emplace_back(IR::OpCode::ADD, tmp2, index, tmp);
        index = tmp2;
        if (i != 0) {
          OpName tmp = "%" + std::to_string(ctx.get_id());
          ir.emplace_back(IR::OpCode::IMUL, tmp, size, v.shape[i]);
          size = tmp;
        }
      }
      ir.emplace_back(IR::OpCode::STORE, OpName(), v.name, index, value);
    } else {
      throw std::runtime_error(this->name.name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name.name + " is not a array.");
  }
}
