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
//
#include <cassert>

#include "config.h"
#include "parser.hpp"

// 编译期间求值

namespace SYC {
int Node::Expression::eval(ContextIR& ctx) {
  throw std::runtime_error("can not eval this value at compile time.");
}
int Node::Identifier::eval(ContextIR& ctx) {
  try {
    auto v = ctx.find_const(this->name);
    if (v.is_array) {
      throw std::runtime_error(this->name + " is a array.");
    } else {
      return v.value.front();
    }
  } catch (std::out_of_range e) {
    if (config::optimize_level > 0) {
      return ctx.find_const_assign(ctx.find_symbol(this->name).name).value[0];
    }
    throw e;
  }
}
int Node::ArrayIdentifier::eval(ContextIR& ctx) {
  auto v = ctx.find_const(this->name.name);
  if (v.is_array) {
    if (this->shape.size() == v.shape.size()) {
      int index = 0, size = 1;
      for (int i = this->shape.size() + 1; i >= 0; i++) {
        index += this->shape[i]->eval(ctx) * size;
        size *= v.shape[i];
      }
      return v.value[index];
    } else {
      throw std::runtime_error(this->name.name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name.name + " is not a array.");
  }
}
int Node::ConditionExpression::eval(ContextIR& ctx) {
  return this->value.eval(ctx);
}
int Node::BinaryExpression::eval(ContextIR& ctx) {
  switch (this->op) {
    case PLUS:
      return lhs.eval(ctx) + rhs.eval(ctx);
      break;

    case MINUS:
      return lhs.eval(ctx) - rhs.eval(ctx);
      break;

    case MUL:
      return lhs.eval(ctx) * rhs.eval(ctx);
      break;

    case DIV:
      return lhs.eval(ctx) / rhs.eval(ctx);
      break;

    case MOD:
      return lhs.eval(ctx) % rhs.eval(ctx);
      break;

    case EQ:
      return lhs.eval(ctx) == rhs.eval(ctx);
      break;

    case NE:
      return lhs.eval(ctx) != rhs.eval(ctx);
      break;

    case GT:
      return lhs.eval(ctx) > rhs.eval(ctx);
      break;

    case GE:
      return lhs.eval(ctx) >= rhs.eval(ctx);
      break;

    case LT:
      return lhs.eval(ctx) < rhs.eval(ctx);
      break;

    case LE:
      return lhs.eval(ctx) <= rhs.eval(ctx);
      break;

    case AND:
      return lhs.eval(ctx) && rhs.eval(ctx);
      break;

    case OR:
      return lhs.eval(ctx) || rhs.eval(ctx);
      break;

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}

int Node::UnaryExpression::eval(ContextIR& ctx) {
  switch (this->op) {
    case PLUS:
      return rhs.eval(ctx);
      break;

    case MINUS:
      return -rhs.eval(ctx);
      break;

    case NOT:
      return !rhs.eval(ctx);
      break;

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}

int Node::CommaExpression::eval(ContextIR& ctx) {
  int ret;
  for (auto v : values) {
    ret = v->eval(ctx);
  }
  return ret;
}

int Node::Number::eval(ContextIR& ctx) { return this->value; }

int Node::Assignment::eval(ContextIR& ctx) {
  if (dynamic_cast<Node::ArrayIdentifier*>(&this->lhs) != nullptr) {
    throw std::runtime_error("only can eval a local var");
  }
  auto val = this->rhs.eval(ctx);
  auto& v = ctx.find_symbol(this->lhs.name);
  if (v.name[0] != '%' || v.is_array) {
    throw std::runtime_error("only can eval a local var");
  }
  v.name = "%" + std::to_string(ctx.get_id());
  ctx.insert_const_assign(v.name, val);
  return val;
}

int Node::AfterInc::eval(ContextIR& ctx) {
  if (dynamic_cast<Node::ArrayIdentifier*>(&this->lhs) != nullptr) {
    throw std::runtime_error("only can eval a local var");
  }
  auto val = this->lhs.eval(ctx);
  auto& v = ctx.find_symbol(this->lhs.name);
  if (v.name[0] != '%' || v.is_array) {
    throw std::runtime_error("only can eval a local var");
  }
  v.name = "%" + std::to_string(ctx.get_id());
  auto new_val = this->op == PLUS ? val + 1 : val - 1;
  ctx.insert_const_assign(v.name, new_val);
  return val;
}

int Node::EvalStatement::eval(ContextIR& ctx) { return this->value.eval(ctx); }

// 运行期间求值

IR::OpName Node::Expression::eval_runtime(ContextIR& ctx, IRList& ir) {
  throw std::runtime_error("can not eval this value at run time.");
}

IR::OpName Node::Identifier::eval_runtime(ContextIR& ctx, IRList& ir) {
  auto v = ctx.find_symbol(this->name);
  if (v.is_array) {
    return v.name;
  } else {
    if (config::optimize_level > 0) {
      try {
        return ctx.find_const_assign(v.name).value[0];
      } catch (...) {
      }
    }
    return v.name;
  }
}
IR::OpName Node::ArrayIdentifier::eval_runtime(ContextIR& ctx, IRList& ir) {
  auto v = ctx.find_symbol(this->name.name);
  if (v.is_array) {
    if (this->shape.size() == v.shape.size()) {
      IR::OpName dest = "%" + std::to_string(ctx.get_id());
      if (config::optimize_level > 0) {
        try {
          int index = 0, size = 4;
          for (int i = this->shape.size() - 1; i >= 0; i--) {
            index += this->shape[i]->eval(ctx) * size;
            size *= v.shape[i];
          }
          ir.emplace_back(IR::OpCode::LOAD, dest, v.name, index);
          return dest;
        } catch (...) {
        }
      }
      IR::OpName index = "%" + std::to_string(ctx.get_id());
      IR::OpName size = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(
          IR::OpCode::SAL, index,
          this->shape[this->shape.size() - 1]->eval_runtime(ctx, ir), 2);
      if (this->shape.size() != 1) {
        IR::OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(IR::OpCode::MOV, size,
                        4 * v.shape[this->shape.size() - 1]);
      }
      for (int i = this->shape.size() - 2; i >= 0; i--) {
        IR::OpName tmp = "%" + std::to_string(ctx.get_id());
        IR::OpName tmp2 = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(IR::OpCode::IMUL, tmp, size,
                        this->shape[i]->eval_runtime(ctx, ir));
        ir.emplace_back(IR::OpCode::ADD, tmp2, index, tmp);
        index = tmp2;
        if (i != 0) {
          IR::OpName tmp = "%" + std::to_string(ctx.get_id());
          ir.emplace_back(IR::OpCode::IMUL, tmp, size, v.shape[i]);
          size = tmp;
        }
      }
      ir.emplace_back(IR::OpCode::LOAD, dest, v.name, index);
      return dest;
    } else {
      throw std::runtime_error(this->name.name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name.name + " is not a array.");
  }
}
IR::OpName Node::ConditionExpression::eval_runtime(ContextIR& ctx, IRList& ir) {
  return this->value.eval_runtime(ctx, ir);
}

namespace {
int log2(int a) {
  int ans = -1;
  while (a > 0) {
    ans++;
    a /= 2;
  }
  return ans;
}
}  // namespace
IR::OpName Node::BinaryExpression::eval_runtime(ContextIR& ctx, IRList& ir) {
  if (config::optimize_level > 0) {
    try {
      return this->eval(ctx);
    } catch (...) {
    }
  }
  IR::OpName dest = "%" + std::to_string(ctx.get_id()), lhs, rhs;
  if (this->op != AND && this->op != OR) {
    if (config::optimize_level > 0) {
      try {
        lhs = this->lhs.eval(ctx);
      } catch (...) {
        lhs = this->lhs.eval_runtime(ctx, ir);
      }
      try {
        rhs = this->rhs.eval(ctx);
      } catch (...) {
        rhs = this->rhs.eval_runtime(ctx, ir);
      }
    } else {
      lhs = this->lhs.eval_runtime(ctx, ir);
      rhs = this->rhs.eval_runtime(ctx, ir);
    }
  }
  switch (this->op) {
    case PLUS:
      ir.emplace_back(IR::OpCode::ADD, dest, lhs, rhs);
      break;

    case MINUS:
      ir.emplace_back(IR::OpCode::SUB, dest, lhs, rhs);
      break;

    case MUL:
      if (config::optimize_level > 0) {
        if (lhs.is_imm() && (1 << log2(lhs.value)) == lhs.value) {
          if (log2(lhs.value) < 32)
            ir.emplace_back(IR::OpCode::SAL, dest, rhs, log2(lhs.value));
          else
            ir.emplace_back(IR::OpCode::MOV, dest, 0);
          break;
        }
        if (rhs.is_imm() && (1 << log2(rhs.value)) == rhs.value) {
          if (log2(rhs.value) < 32)
            ir.emplace_back(IR::OpCode::SAL, dest, lhs, log2(rhs.value));
          else
            ir.emplace_back(IR::OpCode::MOV, dest, 0);
          break;
        }
      }
      ir.emplace_back(IR::OpCode::IMUL, dest, lhs, rhs);
      break;

    case DIV:
      ir.emplace_back(IR::OpCode::IDIV, dest, lhs, rhs);
      break;

    case MOD:
      if (config::optimize_level > 0 && rhs.is_imm()) {
        IR::OpName tmp1 = "%" + std::to_string(ctx.get_id());
        IR::OpName tmp2 = "%" + std::to_string(ctx.get_id());
        // TODO: 这个IDIV可能可以DAG优化
        ir.emplace_back(IR::OpCode::IDIV, tmp1, lhs, rhs);
        ir.emplace_back(IR::OpCode::IMUL, tmp2, tmp1, rhs);
        ir.emplace_back(IR::OpCode::SUB, dest, lhs, tmp2);
      } else {
        ir.emplace_back(IR::OpCode::MOD, dest, lhs, rhs);
      }
      break;

    case EQ:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVEQ, dest, 1, 0);
      break;

    case NE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVNE, dest, 1, 0);
      break;

    case GT:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVGT, dest, 1, 0);
      break;

    case GE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVGE, dest, 1, 0);
      break;

    case LT:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVLT, dest, 1, 0);
      break;

    case LE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ir.emplace_back(IR::OpCode::MOVLE, dest, 1, 0);
      break;

    case AND: {
      std::string label = "COND" + std::to_string(ctx.get_id()) + "_end";
      IRList end;
      end.emplace_back(IR::OpCode::LABEL, label);

      auto lhs = this->lhs.eval_cond_runtime(ctx, ir);
      ir.emplace_back(IR::OpCode::PHI_MOV, dest, IR::OpName(0));
      ir.back().phi_block = end.begin();
      ir.emplace_back(lhs.else_op, label);
      auto rhs = this->rhs.eval_runtime(ctx, ir);
      ir.emplace_back(IR::OpCode::PHI_MOV, dest, rhs);
      ir.back().phi_block = end.begin();

      ir.splice(ir.end(), end);
      break;
    }

    case OR: {
      std::string label = "COND" + std::to_string(ctx.get_id()) + "_end";
      IRList end;
      end.emplace_back(IR::OpCode::LABEL, label);

      auto lhs = this->lhs.eval_cond_runtime(ctx, ir);
      ir.emplace_back(IR::OpCode::PHI_MOV, dest, IR::OpName(1));
      ir.back().phi_block = end.begin();
      ir.emplace_back(lhs.then_op, label);
      auto rhs = this->rhs.eval_runtime(ctx, ir);
      ir.emplace_back(IR::OpCode::PHI_MOV, dest, rhs);
      ir.back().phi_block = end.begin();

      ir.splice(ir.end(), end);
      break;
    }

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
  return dest;
}
IR::OpName Node::UnaryExpression::eval_runtime(ContextIR& ctx, IRList& ir) {
  if (config::optimize_level > 0) {
    try {
      return this->eval(ctx);
    } catch (...) {
    }
  }
  IR::OpName dest = "%" + std::to_string(ctx.get_id());
  switch (this->op) {
    case PLUS:
      return rhs.eval_runtime(ctx, ir);
      break;

    case MINUS:
      ir.emplace_back(IR::OpCode::SUB, dest, 0, rhs.eval_runtime(ctx, ir));
      return dest;
      break;

    case NOT:
      ir.push_back(
          IR(IR::OpCode::CMP, IR::OpName(), 0, rhs.eval_runtime(ctx, ir)));
      ir.emplace_back(IR::OpCode::MOVEQ, dest, IR::OpName(0));
      ir.emplace_back(IR::OpCode::MOVNE, dest, IR::OpName(1));
      return dest;
      break;

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}
IR::OpName Node::CommaExpression::eval_runtime(ContextIR& ctx, IRList& ir) {
  IR::OpName ret;
  for (auto v : values) {
    ret = v->eval_runtime(ctx, ir);
  }
  return ret;
}

IR::OpName Node::FunctionCall::eval_runtime(ContextIR& ctx, IRList& ir) {
  std::vector<IR::OpName> list;
  IR::OpName dest = "%" + std::to_string(ctx.get_id());
  for (int i = 0; i < this->args.args.size(); i++) {
    list.push_back(this->args.args[i]->eval_runtime(ctx, ir));
  }
  for (int i = this->args.args.size() - 1; i >= 0; i--) {
    ir.emplace_back(IR::OpCode::SET_ARG, i, list[i]);
  }
  ir.emplace_back(IR::OpCode::CALL, dest, this->name.name);
  return dest;
}
IR::OpName Node::Number::eval_runtime(ContextIR& ctx, IRList& ir) {
  return this->value;
}

IR::OpName Node::Statement::eval_runtime(ContextIR& ctx, IRList& ir) {
  this->generate_ir(ctx, ir);
  return IR::OpName();
}

IR::OpName Node::Assignment::eval_runtime(ContextIR& ctx, IRList& ir) {
  this->generate_ir(ctx, ir);
  if (dynamic_cast<Node::ArrayIdentifier*>(&this->lhs)) {
    assert(ir.back().op_code == IR::OpCode::STORE);
    return ir.back().op3;
  } else {
    assert(ir.back().dest.is_var());
    return ir.back().dest;
  }
}

IR::OpName Node::AfterInc::eval_runtime(ContextIR& ctx, IRList& ir) {
  auto v = ctx.find_symbol(this->lhs.name);
  auto n0 = new Node::Number(1);
  auto n1 = new Node::BinaryExpression(lhs, this->op, *n0);
  auto n2 = new Node::Assignment(lhs, *n1);
  n2->eval_runtime(ctx, ir);
  delete n2;
  delete n1;
  delete n0;
  return v.name;
}

IR::OpName Node::EvalStatement::eval_runtime(ContextIR& ctx, IRList& ir) {
  return this->value.eval_runtime(ctx, ir);
}

Node::Expression::CondResult Node::Expression::eval_cond_runtime(ContextIR& ctx,
                                                                 IRList& ir) {
  CondResult ret;
  IR::OpName dest = "%" + std::to_string(ctx.get_id());
  ir.emplace_back(IR::OpCode::CMP, dest, this->eval_runtime(ctx, ir),
                  IR::OpName(0));
  ret.then_op = IR::OpCode::JNE;
  ret.else_op = IR::OpCode::JEQ;
  return ret;
}

Node::Expression::CondResult Node::BinaryExpression::eval_cond_runtime(
    ContextIR& ctx, IRList& ir) {
  if (config::optimize_level > 0) {
    try {
      if (this->eval(ctx)) {
        return CondResult{IR::OpCode::JMP, IR::OpCode::NOOP};
      } else {
        return CondResult{IR::OpCode::NOOP, IR::OpCode::JMP};
      }
    } catch (...) {
    }
  }
  CondResult ret;
  IR::OpName lhs, rhs;
  if (this->op != AND && this->op != OR) {
    if (config::optimize_level > 0) {
      try {
        lhs = this->lhs.eval(ctx);
      } catch (...) {
        lhs = this->lhs.eval_runtime(ctx, ir);
      }
      try {
        rhs = this->rhs.eval(ctx);
      } catch (...) {
        rhs = this->rhs.eval_runtime(ctx, ir);
      }
    } else {
      lhs = this->lhs.eval_runtime(ctx, ir);
      rhs = this->rhs.eval_runtime(ctx, ir);
    }
  }
  switch (this->op) {
    case EQ:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JEQ;
      ret.else_op = IR::OpCode::JNE;
      break;

    case NE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JNE;
      ret.else_op = IR::OpCode::JEQ;
      break;

    case GT:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JGT;
      ret.else_op = IR::OpCode::JLE;
      break;

    case GE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JGE;
      ret.else_op = IR::OpCode::JLT;
      break;

    case LT:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JLT;
      ret.else_op = IR::OpCode::JGE;
      break;

    case LE:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(), lhs, rhs);
      ret.then_op = IR::OpCode::JLE;
      ret.else_op = IR::OpCode::JGT;
      break;

    default:
      ir.emplace_back(IR::OpCode::CMP, IR::OpName(),
                      this->eval_runtime(ctx, ir), IR::OpName(0));
      ret.then_op = IR::OpCode::JNE;
      ret.else_op = IR::OpCode::JEQ;
      break;
  }
  return ret;
}
}  // namespace SYC
