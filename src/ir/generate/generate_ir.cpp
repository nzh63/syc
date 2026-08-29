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
#include <cassert>
#include <exception>
#include <unordered_set>

#include "ast/node.h"
#include "config.h"
#include "ir/generate/convert.h"
#include "ir/generate/context.h"
#include "ir/ir.h"
#include "ir/optimize/optimize.h"
//
#include "parser.hpp"

using namespace syc::ir;

namespace syc::ast::node {
namespace {
// ── 循环出口 phi 的物化 ──────────────────────────────────────────────────
// 候选集合由 BaseNode::collect_assigned 虚函数静态预扫描（AST 下钻），
// 替代旧的「fake pass」（先把循环体生成一遍再比对符号表）：多报的候选
// （如被内层声明遮蔽的名字）无害——多出的 phi 是自搬移，后端跳过、
// optimize_phi_var 消除。
// break/continue 首次出现时，把预扫描候选物化为出口 phi 映射表：
//  break    → 写回循环入口处的 IR 名（END 汇合点直接使用入口名）
//  continue → 为每个候选分配新的稳定名（循环体末端统一收拢到该名）
void materialize_exit_phi(Context& ctx, bool is_break) {
  auto& info = *(is_break ? ctx.loop_break_info.top()
                          : ctx.loop_continue_info.top());
  if (info.materialized) return;
  info.materialized = true;
  for (auto& [scope_i, src, entry_name] : info.vars) {
    info.phi_move[{scope_i, src}] =
        is_break ? entry_name : "%" + std::to_string(ctx.get_id());
  }
}
}  // namespace

void BaseNode::_generate_ir(Context& ctx, IRList& ir) {
  this->print();
  throw std::runtime_error("Can't generate IR for this node.");
}

void Root::_generate_ir(Context& ctx, IRList& ir) {
  for (auto& i : this->body) {
    i->generate_ir(ctx, ir);
  }
  for (const auto& s : ctx.strings()) {
    ir.emplace_back(OpCode::DATA_STRING, OpName(s.first), OpName(),
                    OpName(), OpName(), s.second);
  }
}

void VarDeclare::_generate_ir(Context& ctx, IRList& ir) {
  if (ctx.is_global()) {
    ir.emplace_back(OpCode::DATA_BEGIN, "@" + this->name->name);
    if (syc::ast::types::width_bytes(
            syc::ast::types::canonical(this->ty.id)) >= 8) {
      ir.emplace_back(OpCode::DATA_WORD, OpName(0));
      ir.emplace_back(OpCode::DATA_WORD, OpName(0));
    } else {
      ir.emplace_back(OpCode::DATA_WORD, OpName(0));
    }
    ir.emplace_back(OpCode::DATA_END);
    ctx.insert_symbol(this->name->name,
                      VarInfo("@" + this->name->name, false, {}, this->ty.id));
  } else {
    ctx.insert_symbol(
        this->name->name,
        VarInfo("%" + std::to_string(ctx.get_id()), false, {}, this->ty.id));
  }
}

void VarDeclareWithInit::_generate_ir(Context& ctx, IRList& ir) {
  if (ctx.is_global()) {
    // 全局初始化必须可编译期求值；按声明类型完成转换后写进数据段
    std::int64_t init_val = syc::ast::types::convert_const(
        this->value->eval(ctx), expr_type_of(*this->value, ctx).id,
        this->ty.id);
    ir.emplace_back(OpCode::DATA_BEGIN, "@" + this->name->name);
    if (syc::ast::types::width_bytes(
            syc::ast::types::canonical(this->ty.id)) >= 8) {
      ir.emplace_back(OpCode::DATA_WORD,
                      OpName((std::int32_t)(std::uint32_t)(std::uint64_t)init_val));
      ir.emplace_back(OpCode::DATA_WORD,
                      OpName((std::int32_t)(std::uint32_t)((std::uint64_t)init_val >> 32)));
    } else {
      ir.emplace_back(OpCode::DATA_WORD, OpName((std::int32_t)init_val));
    }
    ir.emplace_back(OpCode::DATA_END);
    ctx.insert_symbol(this->name->name,
                      VarInfo("@" + this->name->name, false, {}, this->ty.id));
    if (this->is_const) {
      ctx.insert_const(
          this->name->name,
          ConstInfo(std::vector<std::int64_t>{init_val}, false, {},
                    this->ty.id));
    }
  } else {
    ctx.insert_symbol(
        this->name->name,
        VarInfo("%" + std::to_string(ctx.get_id()), false, {}, this->ty.id));
    // 局部初始化走赋值路径，Assignment 内部按左值类型完成窄化转换
    Assignment assignment(this->name, this->value);
    assignment.line = this->line;
    assignment.column = this->column;
    assignment.generate_ir(ctx, ir);
    if (this->is_const) {
      ctx.insert_const(
          this->name->name,
          ConstInfo(
              std::vector<std::int64_t>{
                  syc::ast::types::convert_const(
                      this->value->eval(ctx),
                      expr_type_of(*this->value, ctx).id, this->ty.id)},
              false, {}, this->ty.id));
    }
  }
}

void ArrayDeclare::_generate_ir(Context& ctx, IRList& ir) {
  // 元素槽位宽度：≤4 字节元素维持 4 字节/元素，8 字节元素占 8 字节
  int elem_w = syc::ast::types::width_bytes(
                   syc::ast::types::canonical(this->ty.id)) >= 8
                   ? 8
                   : 4;
  std::vector<int> shape;
  for (auto& i : this->name->shape) shape.push_back((int)i->eval(ctx));
  int size = 1;
  for (auto i : shape) size *= i;
  if (ctx.is_global()) {
    ir.emplace_back(OpCode::DATA_BEGIN, "@&" + this->name->name->name);
    ir.emplace_back(OpCode::DATA_SPACE, size * elem_w);
    ir.emplace_back(OpCode::DATA_END);
    ctx.insert_symbol(this->name->name->name,
                      VarInfo("@&" + this->name->name->name, true, shape,
                              this->ty.id));
  } else {
    ctx.insert_symbol(
        this->name->name->name,
        VarInfo("%&" + std::to_string(ctx.get_id()), true, shape, this->ty.id));
    ir.push_back(IR(OpCode::MALLOC_IN_STACK,
                    OpName(ctx.find_symbol(this->name->name->name).name),
                    size * elem_w));
  }
}

namespace {
void _ArrayDeclareWithInit(
    ArrayDeclareWithInit& that,
    const std::vector<std::shared_ptr<ArrayDeclareInitValue>>& v,
    const std::vector<std::shared_ptr<Expression>>& shape,
    std::vector<std::int64_t>& init_value, int index, Context& ctx,
    IRList& ir) {
  using syc::ast::types::TypeInfo;
  const auto elem_ty = syc::ast::types::canonical(that.ty.id);
  // 元素槽位宽度：≤4 字节元素维持 4 字节/元素，8 字节元素占 8 字节
  const int elem_w =
      syc::ast::types::width_bytes(elem_ty) >= 8 ? 8 : 4;
  const auto output_const = [&](std::int64_t value, bool is_space = false,
                                syc::ast::types::TypeId src_ty =
                                    syc::ast::types::TypeId::INT) {
    if (is_space) {
      if (ctx.is_global()) {
        for (std::int64_t i = 0; i < value; i++) init_value.push_back(0);
        ir.emplace_back(OpCode::DATA_SPACE, (int)(value * elem_w));
      } else {
        for (std::int64_t i = 0; i < value; i++) {
          init_value.push_back(0);
        }
      }
    } else {
      // 初始化值按元素声明类型做 C 转换语义
      value = syc::ast::types::convert_const(value, src_ty, elem_ty);
      init_value.push_back(value);
      if (ctx.is_global()) {
        if (elem_w == 8) {
          ir.emplace_back(OpCode::DATA_WORD,
                          OpName((std::int32_t)(std::uint32_t)(std::uint64_t)value));
          ir.emplace_back(OpCode::DATA_WORD,
                          OpName(
                              (std::int32_t)(std::uint32_t)((std::uint64_t)value >> 32)));
        } else {
          ir.emplace_back(OpCode::DATA_WORD, (std::int32_t)value);
        }
      } else {
        ir.emplace_back(OpCode::STORE, OpName(),
                        OpName(ctx.find_symbol(that.name->name->name).name),
                        (int)(init_value.size() * elem_w - elem_w), value);
        if (elem_w == 8 || syc::ast::types::is_floating(elem_ty))
          ir.back().ty = TypeInfo{elem_ty};
      }
    }
  };
  const auto output = [&](OpName value, bool is_space = false,
                          syc::ast::types::TypeId src_ty =
                              syc::ast::types::TypeId::INT) {
    if (is_space) {
      if (ctx.is_global()) {
        for (int i = 0; i < value.value; i++) init_value.push_back(0);
        ir.emplace_back(OpCode::DATA_SPACE, (int)(value.value * elem_w));
      } else {
        for (int i = 0; i < value.value; i++) {
          init_value.push_back(0);
        }
      }
    } else {
      // 运行期初始化值同样按元素声明类型转换
      value = convert_runtime(value, src_ty, elem_ty, ctx, ir);
      init_value.push_back(0);
      if (ctx.is_global()) {
        if (value.is_imm() && elem_w == 8) {
          ir.emplace_back(OpCode::DATA_WORD,
                          OpName((std::int32_t)(std::uint32_t)(std::uint64_t)value.value));
          ir.emplace_back(OpCode::DATA_WORD,
                          OpName((std::int32_t)(std::uint32_t)(
                              (std::uint64_t)value.value >> 32)));
        } else {
          ir.emplace_back(OpCode::DATA_WORD, value);
        }
      } else {
        ir.emplace_back(OpCode::STORE, OpName(),
                        OpName(ctx.find_symbol(that.name->name->name).name),
                        (int)(init_value.size() * elem_w - elem_w), value);
        if (elem_w == 8 || syc::ast::types::is_floating(elem_ty))
          ir.back().ty = TypeInfo{elem_ty};
      }
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
          output_const(i->value->eval(ctx), false,
                       syc::ast::types::canonical(expr_type_of(*i->value, ctx).id));
        else
          output(i->value->eval(ctx));
      } catch (...) {
        output(i->value->eval_runtime(ctx, ir), false,
               syc::ast::types::canonical(expr_type_of(*i->value, ctx).id));
      }
    } else {
      if (write_size % size_this_shape != 0) {
        if (that.is_const)
          output_const(size_this_shape - (write_size % size_this_shape), true);
        else
          output(size_this_shape - (write_size % size_this_shape), true);
      }
      _ArrayDeclareWithInit(that, i->value_list, shape, init_value, index + 1,
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

void ArrayDeclareWithInit::_generate_ir(Context& ctx, IRList& ir) {
  // 元素槽位宽度：≤4 字节元素维持 4 字节/元素，8 字节元素占 8 字节
  int elem_w = syc::ast::types::width_bytes(
                   syc::ast::types::canonical(this->ty.id)) >= 8
                   ? 8
                   : 4;
  std::vector<int> shape;
  for (auto& i : this->name->shape) shape.push_back((int)i->eval(ctx));
  int size = 1;
  for (auto i : shape) size *= i;

  std::vector<std::int64_t> init_value;
  if (ctx.is_global()) {
    ir.emplace_back(OpCode::DATA_BEGIN, "@&" + this->name->name->name);
    _ArrayDeclareWithInit(*this, this->value->value_list, this->name->shape,
                          init_value, 0, ctx, ir);
    ir.emplace_back(OpCode::DATA_END);
    ctx.insert_symbol(this->name->name->name,
                      VarInfo("@&" + this->name->name->name, true, shape,
                              this->ty.id));
  } else {
    ctx.insert_symbol(
        this->name->name->name,
        VarInfo("%&" + std::to_string(ctx.get_id()), true, shape, this->ty.id));
    ir.emplace_back(OpCode::MALLOC_IN_STACK,
                    OpName(ctx.find_symbol(this->name->name->name).name),
                    size * elem_w);
    ir.emplace_back(OpCode::SET_ARG, 0,
                    OpName(ctx.find_symbol(this->name->name->name).name));
    ir.emplace_back(OpCode::SET_ARG, 1, OpName(0));
    ir.emplace_back(OpCode::SET_ARG, 2, OpName(size * elem_w));
    ir.emplace_back(OpCode::CALL, "memset");
    _ArrayDeclareWithInit(*this, this->value->value_list, this->name->shape,
                          init_value, 0, ctx, ir);
  }
}

void FunctionDefine::_generate_ir(Context& ctx, IRList& ir) {
  // 登记函数签名，供调用点做实参转换；并压入返回类型栈
  FuncSig sig;
  sig.return_ty = this->return_ty;
  for (auto& a : this->args->list) {
    // 数组形参按 4 字节指针传递，签名里记录指针宽度而不是元素类型
    if (dynamic_cast<ArrayIdentifier*>(a->name.get()))
      sig.params.push_back(TypeInfo{types::TypeId::INT});
    else
      sig.params.push_back(a->ty);
  }
  ctx.insert_function(this->name->name, sig);
  ctx.function_stack.push_back(sig);

  ctx.create_scope();
  int arg_len = (int)this->args->list.size();
  ir.emplace_back(OpCode::FUNCTION_BEGIN, OpName(), arg_len, this->name->name);
  // 形参槽位记账与调用方一致：整型参数走核心池（$argN），浮点参数走
  // VFP 池（$fargN，double 占 2 个连续偶对齐槽）
  int core_acc = 0, fp_acc = 0;
  for (int i = 0; i < arg_len; i++) {
    auto identifier = dynamic_cast<ArrayIdentifier*>(this->args->list[i]->name.get());
    if (identifier) {
      std::vector<int> shape;
      for (auto& i : identifier->shape) shape.push_back(i->eval(ctx));
      auto tmp = "%" + std::to_string(ctx.get_id());
      // 数组形参按 4 字节指针传递
      ir.emplace_back(OpCode::MOV, tmp, OpName("$arg" + std::to_string(core_acc++)));
      ctx.insert_symbol(identifier->name->name,
                        VarInfo(tmp, true, shape, this->args->list[i]->ty.id));
      ir.emplace_back(OpCode::INFO, "NOT CONSTEXPR");
    } else {
      auto tmp = "%" + std::to_string(ctx.get_id());
      // 入参已是调用方按形参类型传入的规范值，直接搬运（宽度 8 的
      // 参数占连续两个槽位，由后端 MOV 宽度识别）
      auto param_ty = syc::ast::types::canonical(this->args->list[i]->ty.id);
      if (syc::ast::types::is_floating(param_ty)) {
        int n_slot = syc::ast::types::width_bytes(param_ty) >= 8 ? 2 : 1;
        if (n_slot == 2 && (fp_acc & 1)) fp_acc++;
        ir.emplace_back(OpCode::MOV, tmp,
                        OpName("$farg" + std::to_string(fp_acc)));
        ir.back().ty = TypeInfo{param_ty};
        fp_acc += n_slot;
      } else {
        ir.emplace_back(OpCode::MOV, tmp,
                        OpName("$arg" + std::to_string(core_acc)));
        ir.back().ty = TypeInfo{param_ty};
        core_acc += syc::ast::types::width_bytes(param_ty) >= 8 ? 2 : 1;
      }
      ctx.insert_symbol(this->args->list[i]->name->name,
                        VarInfo(tmp, false, {}, this->args->list[i]->ty.id));
    }
  }
  this->body->generate_ir(ctx, ir);
  if (syc::ast::types::canonical(this->return_ty.id) !=
      syc::ast::types::TypeId::VOID) {
    ir.emplace_back(OpCode::RET, OpName(), 0);
    ir.back().ty = this->return_ty;
  } else {
    ir.emplace_back(OpCode::RET);
  }
  ir.emplace_back(OpCode::FUNCTION_END, this->name->name);
  ctx.function_stack.pop_back();
  ctx.end_scope();
}

void Block::_generate_ir(Context& ctx, IRList& ir) {
  ctx.create_scope();
  for (auto i : this->statements) i->generate_ir(ctx, ir);
  ctx.end_scope();
}

void DeclareStatement::_generate_ir(Context& ctx, IRList& ir) {
  // 将声明的类型下发给每个声明项（数组的元素类型同理）
  for (auto i : this->list) {
    i->ty = this->ty;
    i->generate_ir(ctx, ir);
  }
}

void VoidStatement::_generate_ir(Context& ctx, IRList& ir) {}

void EvalStatement::_generate_ir(Context& ctx, IRList& ir) {
  this->value->eval_runtime(ctx, ir);
}

void ReturnStatement::_generate_ir(Context& ctx, IRList& ir) {
  if (this->value) {
    auto ret = this->value->eval_runtime(ctx, ir);
    if (!ctx.function_stack.empty()) {
      // 返回值转换为函数声明的返回类型（窄化），语义等价 C 的 return 转换
      ret = convert_runtime(ret, expr_type_of(*this->value, ctx).id,
                            ctx.cur_func().return_ty.id, ctx, ir);
    }
    ir.emplace_back(OpCode::RET, OpName(), ret);
    ir.back().ty = ctx.cur_func().return_ty;
  } else {
    ir.emplace_back(OpCode::RET);
  }
}

void ContinueStatement::_generate_ir(Context& ctx, IRList& ir) {
  materialize_exit_phi(ctx, false);
  for (auto& i : ctx.loop_continue_info.top()->phi_move) {
    ir.emplace_back(
        OpCode::MOV, i.second,
        OpName(
            ctx.symbol_table[i.first.first].find(i.first.second)->second.name));
    ir.back().ty = TypeInfo{
        ctx.symbol_table[i.first.first].find(i.first.second)->second.ty};
  }
  ir.emplace_back(OpCode::JMP, ".L.LOOP_" + ctx.loop_label.top() + "_CONTINUE");
}

void BreakStatement::_generate_ir(Context& ctx, IRList& ir) {
  materialize_exit_phi(ctx, true);
  for (auto& i : ctx.loop_break_info.top()->phi_move) {
    ir.emplace_back(
        OpCode::MOV, i.second,
        OpName(
            ctx.symbol_table[i.first.first].find(i.first.second)->second.name));
    ir.back().ty = TypeInfo{
        ctx.symbol_table[i.first.first].find(i.first.second)->second.ty};
  }
  ir.emplace_back(OpCode::JMP, ".L.LOOP_" + ctx.loop_label.top() + "_END");
}

void WhileStatement::_generate_ir(Context& ctx, IRList& ir) {
  ctx.create_scope();
  ctx.loop_label.push(std::to_string(ctx.get_id()));
  ctx.loop_var.push({});

  //      ┌───────────┐
  //      │ ir_before │  入口 phi（循环携带版本的初始值）
  //      ├───────────┤
  // COND:│ cmp       │
  //      ├───────────┤
  //  JMP:│ jne/jeq   │
  //      ├───────────┤
  //   DO:│ ...       │
  //      │ break;    │
  //      │ continue; │
  //      ├───────────┤
  // CONT:│ 出口 phi  │
  //      │ jmp COND  │
  //      ├───────────┤
  //  END:│           │
  //      └───────────┘
  // 循环体只生成一遍；被赋值变量由 AST 静态预扫描确定，break/continue
  // 首次出现时才物化出口搬移（见 materialize_exit_phi）。

  Context ctx_before = ctx;
  IRList ir_before;

  IRList ir_cond;
  ir_cond.emplace_back(OpCode::LABEL,
                       ".L.LOOP_" + ctx.loop_label.top() + "_BEGIN");
  auto cond_begin = ir_cond.begin();

  // 静态预扫描循环体中被赋值的源变量名
  AssignedSet assigned;
  this->dostmt->collect_assigned(assigned);

  // 入口 phi：候选 ∩ 外层符号表的局部标量变量 → 新版本名；
  // 候选同时登记为 break/continue 的出口 phi 集合
  Context::LoopExitInfo exit_info;
  for (int i = 0; i < (int)ctx_before.symbol_table.size(); i++) {
    for (auto& [name, var] : ctx_before.symbol_table[i]) {
      if (!assigned.count(name) || var.is_array || var.name[0] != '%') continue;
      const std::string new_name = "%" + std::to_string(ctx_before.get_id());
      ir_before.emplace_back(OpCode::PHI_MOV, new_name, OpName(var.name));
      ir_before.back().phi_block = cond_begin;
      ir_before.back().ty = TypeInfo{var.ty};
      var.name = new_name;
      ctx_before.loop_var.top().push_back(new_name);
      exit_info.vars.push_back({i, name, new_name});
    }
  }

  // COND
  Context ctx_cond = ctx_before;
  auto cond = this->cond->eval_cond_runtime(ctx_cond, ir_cond);

  // JMP
  IRList ir_jmp;
  ir_jmp.emplace_back(cond.else_op, ".L.LOOP_" + ctx.loop_label.top() + "_END");

  // DO
  Context ctx_do = ctx_cond;
  ctx_do.loop_break_info.push(
      std::make_shared<Context::LoopExitInfo>(exit_info));
  ctx_do.loop_continue_info.push(
      std::make_shared<Context::LoopExitInfo>(exit_info));
  IRList ir_do;
  ir_do.emplace_back(OpCode::LABEL, ".L.LOOP_" + ctx.loop_label.top() + "_DO");
  this->dostmt->generate_ir(ctx_do, ir_do);

  // continue 出口 phi：循环体内出现过 continue 时，把循环体末端的
  // 最终版本收拢到 continue 处物化的稳定名
  IRList end;
  end.emplace_back(OpCode::LABEL, ".L.LOOP_" + ctx.loop_label.top() + "_END");
  if (ctx_do.loop_continue_info.top()->materialized) {
    for (auto& [key, fresh] : ctx_do.loop_continue_info.top()->phi_move) {
      auto& v = ctx_do.symbol_table[key.first].find(key.second)->second;
      ir_do.emplace_back(OpCode::PHI_MOV, fresh, OpName(v.name));
      ir_do.back().phi_block = end.begin();
      ir_do.back().ty = TypeInfo{v.ty};
      v.name = fresh;
    }
  }

  // CONTINUE：出口变量收拢回循环入口版本，供下一轮条件判断
  IRList ir_continue;
  ir_continue.emplace_back(OpCode::LABEL,
                           ".L.LOOP_" + ctx.loop_label.top() + "_CONTINUE");
  for (auto& [scope_i, src, entry_name] : exit_info.vars) {
    auto& v = ctx_do.symbol_table[scope_i].find(src)->second;
    if (v.name == entry_name) continue;
    ir_continue.emplace_back(OpCode::PHI_MOV, entry_name, OpName(v.name));
    ir_continue.back().phi_block = cond_begin;
    ir_continue.back().ty = TypeInfo{v.ty};
  }
  ir_continue.emplace_back(OpCode::JMP,
                           ".L.LOOP_" + ctx.loop_label.top() + "_BEGIN");

  if (ctx.cfg->optimize_level > 0)
    syc::ir::optimize_loop_ir(ir_before, ir_cond, ir_jmp, ir_do, ir_continue,
                              *ctx.cfg);

  // 为 continue 块增加假读以延长其生命周期
  std::unordered_set<std::string> written;
  for (const auto& irs : std::vector<IRList*>{&ir_cond, &ir_jmp, &ir_do}) {
    for (const auto& i : *irs) {
      if (i.dest.is_var()) written.insert(i.dest.name);
      i.forEachOp(
          [&](OpName op) {
            if (op.is_var()) {
              if (!written.count(op.name)) {
                ir_continue.emplace_back(OpCode::NOOP, OpName(), op);
              }
            }
          },
          false);
    }
  }

  ctx = ctx_cond;
  ctx.id = ctx_do.id;
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

void IfElseStatement::_generate_ir(Context& ctx, IRList& ir) {
  ctx.create_scope();

  auto id = std::to_string(ctx.get_id());

  auto cond = this->cond->eval_cond_runtime(ctx, ir);
  if (cond.then_op == OpCode::JMP) {
    this->thenstmt->generate_ir(ctx, ir);
    return;
  }
  if (cond.else_op == OpCode::JMP) {
    this->elsestmt->generate_ir(ctx, ir);
    return;
  }

  ir.emplace_back(cond.else_op, ".L.IF_" + id + "_ELSE");

  IRList ir_then, ir_else;
  Context ctx_then = ctx, ctx_else = ctx;

  ctx_then.create_scope();
  this->thenstmt->generate_ir(ctx_then, ir_then);
  ctx_then.end_scope();

  ctx_else.id = ctx_then.id;
  ctx_else.create_scope();
  this->elsestmt->generate_ir(ctx_else, ir_else);
  ctx_else.end_scope();

  ctx.id = ctx_else.id;

  IRList end;
  end.emplace_back(OpCode::LABEL, ".L.IF_" + id + "_END");

  for (int i = 0; i < ctx_then.symbol_table.size(); i++) {
    for (auto& s : ctx_then.symbol_table[i]) {
      if (s.second.name !=
          ctx_else.symbol_table[i].find(s.first)->second.name) {
        auto& v = ctx.find_symbol(s.first);
        assert(!v.is_array);
        if (v.name[0] == '%') v.name = "%" + std::to_string(ctx.get_id());
        ir_then.emplace_back(OpCode::PHI_MOV, v.name, OpName(s.second.name));
        ir_then.back().phi_block = end.begin();
        ir_then.back().ty = TypeInfo{s.second.ty};
        ir_else.emplace_back(
            OpCode::PHI_MOV, v.name,
            OpName(ctx_else.symbol_table[i].find(s.first)->second.name));
        ir_else.back().phi_block = end.begin();
        ir_else.back().ty = TypeInfo{s.second.ty};
      }
    }
  }

  ir.splice(ir.end(), ir_then);
  if (!ir_else.empty()) ir.emplace_back(OpCode::JMP, ".L.IF_" + id + "_END");
  ir.emplace_back(OpCode::LABEL, ".L.IF_" + id + "_ELSE");
  ir.splice(ir.end(), ir_else);
  ir.splice(ir.end(), end);

  ctx.end_scope();
}

void Assignment::_generate_ir(Context& ctx, IRList& ir) {
  if (dynamic_cast<ArrayIdentifier*>(this->lhs.get())) {
    auto rhs = this->rhs->eval_runtime(ctx, ir);
    dynamic_cast<ArrayIdentifier*>(this->lhs.get())
        ->store_runtime(rhs, ctx, ir, expr_type_of(*this->rhs, ctx).id);
  } else {
    auto rhs = this->rhs->eval_runtime(ctx, ir);
    auto& v = ctx.find_symbol(this->lhs->name);
    if (v.is_array) {
      throw std::runtime_error("Can't assign to a array.");
    } else {
      // 标量写入：先按左值声明类型完成规范化转换
      rhs = convert_runtime(rhs, expr_type_of(*this->rhs, ctx).id, v.ty, ctx,
                            ir);
      if (rhs.is_var() && rhs.name[0] == '%' &&
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
            ir.emplace_back(OpCode::MOV, v.name, rhs);
            ir.back().ty = TypeInfo{v.ty};
          } else {
            v.name = rhs.name;
          }
        } else {
          v.name = rhs.name;
        }
      } else if (v.name[0] == '@') {
        ir.emplace_back(OpCode::MOV, v.name, rhs);
        ir.back().ty = TypeInfo{v.ty};
      } else {
        v.name = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(OpCode::MOV, v.name, rhs);
        ir.back().ty = TypeInfo{v.ty};
        if (ctx.cfg->optimize_level > 0 && rhs.is_imm()) {
          ctx.insert_const_assign(v.name, rhs.value);
        }
      }
    }
  }
}

void AfterInc::_generate_ir(Context& ctx, IRList& ir) {
  auto n0 = std::make_shared<Number>(1);
  auto n1 = std::make_shared<BinaryExpression>(this->lhs, this->op, n0);
  auto n2 = std::make_shared<Assignment>(this->lhs, n1);
  n0->line = n1->line = n2->line = this->line;
  n0->column = n1->column = n2->column = this->column;
  n2->generate_ir(ctx, ir);
}

void ArrayIdentifier::store_runtime(OpName value, Context& ctx, IRList& ir,
                                    types::TypeId src_ty) {
  auto v = ctx.find_symbol(this->name->name);
  if (v.is_array) {
    // 元素槽位宽度：≤4 字节元素维持 4 字节/元素，8 字节元素占 8 字节
    int elem_w = syc::ast::types::width_bytes(
                     syc::ast::types::canonical(v.ty)) >= 8
                     ? 8
                     : 4;
    // 数组元素写入：先按元素声明类型完成规范化转换
    value = convert_runtime(value, src_ty, v.ty, ctx, ir);
    if (this->shape.size() == v.shape.size()) {
      if (ctx.cfg->optimize_level > 0) {
        try {
          int index = 0, size = elem_w;
          for (int i = this->shape.size() - 1; i >= 0; i--) {
            index += (int)this->shape[i]->eval(ctx) * size;
            size *= v.shape[i];
          }
          ir.emplace_back(OpCode::STORE, OpName(), v.name, index, value);
          if (elem_w == 8 || syc::ast::types::is_floating(v.ty))
            ir.back().ty = TypeInfo{v.ty};
          return;
        } catch (...) {
        }
      }
      OpName index = "%" + std::to_string(ctx.get_id());
      OpName size = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(
          OpCode::SAL, index,
          this->shape[this->shape.size() - 1]->eval_runtime(ctx, ir),
          elem_w == 8 ? 3 : 2);
      if (this->shape.size() != 1) {
        OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(OpCode::MOV, size, elem_w * v.shape[this->shape.size() - 1]);
      }
      for (int i = this->shape.size() - 2; i >= 0; i--) {
        OpName tmp = "%" + std::to_string(ctx.get_id());
        OpName tmp2 = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(OpCode::IMUL, tmp, size,
                        this->shape[i]->eval_runtime(ctx, ir));
        ir.emplace_back(OpCode::ADD, tmp2, index, tmp);
        index = tmp2;
        if (i != 0) {
          OpName tmp = "%" + std::to_string(ctx.get_id());
          ir.emplace_back(OpCode::IMUL, tmp, size, v.shape[i]);
          size = tmp;
        }
      }
      ir.emplace_back(OpCode::STORE, OpName(), v.name, index, value);
      if (elem_w == 8 || syc::ast::types::is_floating(v.ty))
        ir.back().ty = TypeInfo{v.ty};
    } else {
      throw std::runtime_error(this->name->name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name->name + " is not a array.");
  }
}
}  // namespace syc::ast::node
