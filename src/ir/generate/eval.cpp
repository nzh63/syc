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
//
#include <cassert>

#include "config.h"
#include "ir/generate/convert.h"
#include "parser.hpp"

// 编译期间求值

namespace syc {
using syc::ir::convert_runtime;
std::int64_t ast::node::Expression::_eval(ir::Context& ctx) {
  throw std::runtime_error("can not eval this value at compile time.");
}
std::int64_t ast::node::Identifier::_eval(ir::Context& ctx) {
  try {
    auto v = ctx.find_const(this->name);
    if (v.is_array) {
      throw std::runtime_error(this->name + " is a array.");
    } else {
      return v.value.front();
    }
  } catch (std::out_of_range e) {
    if (ctx.cfg->optimize_level > 0) {
      return ctx.find_const_assign(ctx.find_symbol(this->name).name).value[0];
    }
    throw e;
  }
}
std::int64_t ast::node::ArrayIdentifier::_eval(ir::Context& ctx) {
  auto v = ctx.find_const(this->name->name);
  if (v.is_array) {
    if (this->shape.size() == v.shape.size()) {
      int index = 0, size = 1;
      for (int i = this->shape.size() + 1; i >= 0; i++) {
        index += (int)this->shape[i]->eval(ctx) * size;
        size *= v.shape[i];
      }
      return v.value[index];
    } else {
      throw std::runtime_error(this->name->name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name->name + " is not a array.");
  }
}
std::int64_t ast::node::ConditionExpression::_eval(ir::Context& ctx) {
  return this->value->eval(ctx);
}
std::int64_t ast::node::BinaryExpression::_eval(ir::Context& ctx) {
  // signed/unsigned 语义按 C 的 usual arithmetic conversions 判定；
  // 两个操作数先转换到公共类型再运算，结果按公共类型截断规范化，
  // 保证逐运算的 32 位回绕语义与真机一致。
  using namespace types;
  TypeId arith = canonical(usual_arithmetic(expr_type_of(*this->lhs, ctx).id,
                                            expr_type_of(*this->rhs, ctx).id));
  auto conv = [&](Expression& e) {
    return convert_const(e.eval(ctx), arith);
  };
  // 浮点常量折叠：操作数按各自静态类型解释为 double，运算后按公共
  // 类型规范化为位模式
  if (is_floating(arith)) {
    auto convf = [&](Expression& e) {
      return const_to_double(e.eval(ctx), canonical(expr_type_of(e, ctx).id));
    };
    switch (this->op) {
      case PLUS:
        return double_to_const_bits(convf(*lhs) + convf(*rhs), arith);
      case MINUS:
        return double_to_const_bits(convf(*lhs) - convf(*rhs), arith);
      case MUL:
        return double_to_const_bits(convf(*lhs) * convf(*rhs), arith);
      case DIV: {
        double l = convf(*lhs), r = convf(*rhs);
        if (r == 0)
          throw std::runtime_error("division by zero in constant expression");
        return double_to_const_bits(l / r, arith);
      }
      case EQ: return convf(*lhs) == convf(*rhs);
      case NE: return convf(*lhs) != convf(*rhs);
      case GT: return convf(*lhs) > convf(*rhs);
      case GE: return convf(*lhs) >= convf(*rhs);
      case LT: return convf(*lhs) < convf(*rhs);
      case LE: return convf(*lhs) <= convf(*rhs);
      case AND:
        return convf(*lhs) != 0.0 && convf(*rhs) != 0.0;
      case OR:
        return convf(*lhs) != 0.0 || convf(*rhs) != 0.0;
      default:
        throw std::runtime_error("operator % on floating value");
    }
  }
  bool un = is_unsigned(arith);
  switch (this->op) {
    case PLUS:
      return convert_const(conv(*lhs) + conv(*rhs), arith);
      break;

    case MINUS:
      return convert_const(conv(*lhs) - conv(*rhs), arith);
      break;

    case MUL:
      return convert_const(conv(*lhs) * conv(*rhs), arith);
      break;

    case DIV: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      if (r == 0)
        throw std::runtime_error("division by zero in constant expression");
      if (un)
        return (std::int64_t)((std::uint64_t)l / (std::uint64_t)r);
      if (l == INT64_MIN && r == -1) return l;  // 回绕语义，避免除法溢出
      return l / r;
    }

    case MOD: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      if (r == 0)
        throw std::runtime_error("division by zero in constant expression");
      if (un)
        return (std::int64_t)((std::uint64_t)l % (std::uint64_t)r);
      if (l == INT64_MIN && r == -1) return 0;
      return l % r;
    }

    case EQ:
      return conv(*lhs) == conv(*rhs);
      break;

    case NE:
      return conv(*lhs) != conv(*rhs);
      break;

    case GT: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      return un ? (std::uint64_t)l > (std::uint64_t)r : l > r;
    }

    case GE: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      return un ? (std::uint64_t)l >= (std::uint64_t)r : l >= r;
    }

    case LT: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      return un ? (std::uint64_t)l < (std::uint64_t)r : l < r;
    }
      break;

    case LE: {
      std::int64_t l = conv(*lhs), r = conv(*rhs);
      return un ? (std::uint64_t)l <= (std::uint64_t)r : l <= r;
    }

    case AND:
      return lhs->eval(ctx) && rhs->eval(ctx);
      break;

    case OR:
      return lhs->eval(ctx) || rhs->eval(ctx);
      break;

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}

std::int64_t ast::node::UnaryExpression::_eval(ir::Context& ctx) {
  using namespace types;
  switch (this->op) {
    case PLUS:
      return this->rhs->eval(ctx);
      break;

    case MINUS: {
      TypeId t = canonical(usual_arithmetic(expr_type_of(*this->rhs, ctx).id,
                                            expr_type_of(*this->rhs, ctx).id));
      if (is_floating(t)) {
        return double_to_const_bits(-const_to_double(this->rhs->eval(ctx), t), t);
      }
      return convert_const(-convert_const(this->rhs->eval(ctx), t), t);
      break;
    }
      break;

    case NOT:
      return !this->rhs->eval(ctx);
      break;

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}

std::int64_t ast::node::CommaExpression::_eval(ir::Context& ctx) {
  int ret;
  for (auto v : values) {
    ret = v->eval(ctx);
  }
  return ret;
}

std::int64_t ast::node::Number::_eval(ir::Context& ctx) { return this->value; }

std::int64_t ast::node::Assignment::_eval(ir::Context& ctx) {
  if (dynamic_cast<ast::node::ArrayIdentifier*>(this->lhs.get()) != nullptr) {
    throw std::runtime_error("only can eval a local var");
  }
  auto& v = ctx.find_symbol(this->lhs->name);
  auto val = this->rhs->eval(ctx);
  // 写入窄类型变量时按 C 转换语义截断并扩展（浮点为位模式）
  val = syc::ast::types::convert_const(
      val, expr_type_of(*this->rhs, ctx).id, v.ty);
  if (v.name[0] != '%' || v.is_array) {
    throw std::runtime_error("only can eval a local var");
  }
  v.name = "%" + std::to_string(ctx.get_id());
  ctx.insert_const_assign(v.name, val);
  return val;
}

std::int64_t ast::node::AfterInc::_eval(ir::Context& ctx) {
  if (dynamic_cast<ast::node::ArrayIdentifier*>(this->lhs.get()) != nullptr) {
    throw std::runtime_error("only can eval a local var");
  }
  // 浮点变量的自增不支持常量折叠（位模式不能直接 ±1），走运行期路径
  if (syc::ast::types::is_floating(ctx.find_symbol(this->lhs->name).ty)) {
    throw std::runtime_error("float const assign not supported");
  }
  auto val = this->lhs->eval(ctx);
  auto& v = ctx.find_symbol(this->lhs->name);
  if (v.name[0] != '%' || v.is_array) {
    throw std::runtime_error("only can eval a local var");
  }
  v.name = "%" + std::to_string(ctx.get_id());
  auto new_val = this->op == PLUS ? val + 1 : val - 1;
  // 自增结果写回窄类型变量前做转换
  new_val = syc::ast::types::convert_const(new_val, v.ty);
  ctx.insert_const_assign(v.name, new_val);
  return val;
}

std::int64_t ast::node::EvalStatement::_eval(ir::Context& ctx) {
  return this->value->eval(ctx);
}

// ── 表达式静态类型推断（在 IR 生成阶段按需调用一次并缓存）─────────────────
types::TypeInfo ast::node::Identifier::_infer_type(ir::Context& ctx) {
  return types::TypeInfo{ctx.find_symbol(this->name).ty};
}
types::TypeInfo ast::node::ArrayIdentifier::_infer_type(ir::Context& ctx) {
  // 数组名退化为指针参与运算的场景本语言不支持；此处返回元素类型，
  // 即数组元素读写（ArrayIdentifier 作为左值）的静态类型
  return types::TypeInfo{ctx.find_symbol(this->name->name).ty};
}
types::TypeInfo ast::node::ConditionExpression::_infer_type(
    ir::Context& ctx) {
  return expr_type_of(*this->value, ctx);
}
types::TypeInfo ast::node::BinaryExpression::_infer_type(ir::Context& ctx) {
  auto l = types::canonical(expr_type_of(*this->lhs, ctx).id);
  auto r = types::canonical(expr_type_of(*this->rhs, ctx).id);
  switch (this->op) {
    case LT: case LE: case GT: case GE: case EQ: case NE:
    case AND: case OR:
    case NOT:  // NOT 由 UnaryExpression 处理，防御性包含
      return types::TypeInfo{types::TypeId::INT};
    default:
      return types::TypeInfo{types::usual_arithmetic(l, r)};
  }
}
types::TypeInfo ast::node::UnaryExpression::_infer_type(ir::Context& ctx) {
  if (this->op == NOT)
    return types::TypeInfo{types::TypeId::INT};
  return types::TypeInfo{types::usual_arithmetic(
      expr_type_of(*this->rhs, ctx).id, expr_type_of(*this->rhs, ctx).id)};
}
types::TypeInfo ast::node::CommaExpression::_infer_type(ir::Context& ctx) {
  if (values.empty()) return types::TypeInfo{types::TypeId::INT};
  return expr_type_of(*values.back(), ctx);
}
types::TypeInfo ast::node::FunctionCall::_infer_type(ir::Context& ctx) {
  if (const ir::FuncSig* sig = ctx.find_function(this->name->name)) {
    return sig->return_ty;
  }
  return types::TypeInfo{types::TypeId::INT};  // 内建库函数默认按 int 处理
}
types::TypeInfo ast::node::StringLiteral::_infer_type(ir::Context&) {
  return types::TypeInfo{types::TypeId::INT};  // 字符串仅作实参使用，占位类型
}
types::TypeInfo ast::node::Assignment::_infer_type(ir::Context& ctx) {
  return expr_type_of(*this->lhs, ctx);
}
types::TypeInfo ast::node::AfterInc::_infer_type(ir::Context& ctx) {
  return expr_type_of(*this->lhs, ctx);
}
types::TypeInfo ast::node::EvalStatement::_infer_type(ir::Context& ctx) {
  return expr_type_of(*this->value, ctx);
}

// 运行期间求值

ir::OpName ast::node::Expression::_eval_runtime(ir::Context& ctx,
                                                ir::IRList& ir) {
  throw std::runtime_error("can not eval this value at run time.");
}

ir::OpName ast::node::Identifier::_eval_runtime(ir::Context& ctx,
                                                ir::IRList& ir) {
  auto v = ctx.find_symbol(this->name);
  if (v.is_array) {
    return v.name;
  } else {
    if (ctx.cfg->optimize_level > 0) {
      try {
        return ctx.find_const_assign(v.name).value[0];
      } catch (...) {
      }
    }
    return v.name;
  }
}
ir::OpName ast::node::ArrayIdentifier::_eval_runtime(ir::Context& ctx,
                                                     ir::IRList& ir) {
  auto v = ctx.find_symbol(this->name->name);
  if (v.is_array) {
    if (this->shape.size() == v.shape.size()) {
      // 元素槽位宽度：≤4 字节元素维持 4 字节/元素，8 字节元素占 8 字节
      int elem_w = 4;
      {
        using namespace types;
        if (width_bytes(canonical(v.ty)) >= 8) elem_w = 8;
      }
      ir::OpName dest = "%" + std::to_string(ctx.get_id());
      if (ctx.cfg->optimize_level > 0) {
        try {
          int index = 0, size = elem_w;
          for (int i = this->shape.size() - 1; i >= 0; i--) {
            index += (int)this->shape[i]->eval(ctx) * size;
            size *= v.shape[i];
          }
          ir.emplace_back(ir::OpCode::LOAD, dest, v.name, index);
          ir.back().ty = TypeInfo{v.ty};
          return dest;
        } catch (...) {
        }
      }
      ir::OpName index = "%" + std::to_string(ctx.get_id());
      ir::OpName size = "%" + std::to_string(ctx.get_id());
      ir.emplace_back(
          ir::OpCode::SAL, index,
          this->shape[this->shape.size() - 1]->eval_runtime(ctx, ir),
          elem_w == 8 ? 3 : 2);
      if (this->shape.size() != 1) {
        ir::OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(ir::OpCode::MOV, size,
                        elem_w * v.shape[this->shape.size() - 1]);
      }
      for (int i = this->shape.size() - 2; i >= 0; i--) {
        ir::OpName tmp = "%" + std::to_string(ctx.get_id());
        ir::OpName tmp2 = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(ir::OpCode::IMUL, tmp, size,
                        this->shape[i]->eval_runtime(ctx, ir));
        ir.emplace_back(ir::OpCode::ADD, tmp2, index, tmp);
        index = tmp2;
        if (i != 0) {
          ir::OpName tmp = "%" + std::to_string(ctx.get_id());
          ir.emplace_back(ir::OpCode::IMUL, tmp, size, v.shape[i]);
          size = tmp;
        }
      }
      ir.emplace_back(ir::OpCode::LOAD, dest, v.name, index);
      ir.back().ty = TypeInfo{v.ty};
      return dest;
    } else {
      throw std::runtime_error(this->name->name + "'s shape unmatch.");
    }
  } else {
    throw std::runtime_error(this->name->name + " is not a array.");
  }
}
ir::OpName ast::node::ConditionExpression::_eval_runtime(ir::Context& ctx,
                                                         ir::IRList& ir) {
  return this->value->eval_runtime(ctx, ir);
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
ir::OpName ast::node::BinaryExpression::_eval_runtime(ir::Context& ctx,
                                                      ir::IRList& ir) {
  // 类型检查：按操作符分类
  //  - 算术/位运算：操作数做常用算术转换，结果的符号性决定除法与比较
  //    操作码的选取；宽度 8（long long）的结果以 IR::ty 标注，
  //    由后端以寄存器对实现；
  //  - 逻辑运算（&&/||）：真值判断对任意位表示均成立，不做宽度限制。
  using namespace types;
  TypeId arith_ty;
  bool w8;
  bool fp;
  {
    arith_ty = canonical(
        usual_arithmetic(expr_type_of(*this->lhs, ctx).id,
                         expr_type_of(*this->rhs, ctx).id));
    w8 = width_bytes(arith_ty) >= 8;
    fp = is_floating(arith_ty);
  }
  bool arith_unsigned = is_unsigned(arith_ty);
  if (ctx.cfg->optimize_level > 0) {
    try {
      return this->eval(ctx);
    } catch (...) {
    }
  }
  // 64 位运算的结果统一为 int 0/1 或按公共类型宽度
  auto set_ty = [&](types::TypeId t) { ir.back().ty = TypeInfo{t}; };
  ir::OpName dest = "%" + std::to_string(ctx.get_id()), lhs, rhs;
  if (this->op != AND && this->op != OR) {
    if (ctx.cfg->optimize_level > 0) {
      try {
        lhs = this->lhs->eval(ctx);
      } catch (...) {
        lhs = this->lhs->eval_runtime(ctx, ir);
      }
      try {
        rhs = this->rhs->eval(ctx);
      } catch (...) {
        rhs = this->rhs->eval_runtime(ctx, ir);
      }
    } else {
      lhs = this->lhs->eval_runtime(ctx, ir);
      rhs = this->rhs->eval_runtime(ctx, ir);
    }
    if (w8 || fp) {
      // 64 位/浮点运算：先把操作数转换到公共类型
      lhs = convert_runtime(lhs, expr_type_of(*this->lhs, ctx).id, arith_ty,
                            ctx, ir);
      rhs = convert_runtime(rhs, expr_type_of(*this->rhs, ctx).id, arith_ty,
                            ctx, ir);
    }
  }
  switch (this->op) {
    case PLUS:
      ir.emplace_back(ir::OpCode::ADD, dest, lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      break;

    case MINUS:
      ir.emplace_back(ir::OpCode::SUB, dest, lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      break;

    case MUL:
      if (!w8 && !fp && ctx.cfg->optimize_level > 0) {
        if (lhs.is_imm() && (1 << log2(lhs.value)) == lhs.value) {
          if (log2(lhs.value) < 32)
            ir.emplace_back(ir::OpCode::SAL, dest, rhs, log2(lhs.value));
          else
            ir.emplace_back(ir::OpCode::MOV, dest, 0);
          break;
        }
        if (rhs.is_imm() && (1 << log2(rhs.value)) == rhs.value) {
          if (log2(rhs.value) < 32)
            ir.emplace_back(ir::OpCode::SAL, dest, lhs, log2(rhs.value));
          else
            ir.emplace_back(ir::OpCode::MOV, dest, 0);
          break;
        }
      }
      ir.emplace_back(ir::OpCode::IMUL, dest, lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      break;

    case DIV:
      if (fp) {
        ir.emplace_back(ir::OpCode::FDIV, dest, lhs, rhs);
        set_ty(arith_ty);
        break;
      }
      ir.emplace_back(arith_unsigned ? ir::OpCode::UDIV : ir::OpCode::IDIV, dest, lhs,
                      rhs);
      if (w8) set_ty(arith_ty);
      break;

    case MOD:
      if (fp) {
        throw std::runtime_error("operator % on floating value");
      }
      if (!w8 && !arith_unsigned && ctx.cfg->optimize_level > 0 && rhs.is_imm()) {
        ir::OpName tmp1 = "%" + std::to_string(ctx.get_id());
        ir::OpName tmp2 = "%" + std::to_string(ctx.get_id());
        // TODO: 这个IDIV可能可以DAG优化
        ir.emplace_back(ir::OpCode::IDIV, tmp1, lhs, rhs);
        ir.emplace_back(ir::OpCode::IMUL, tmp2, tmp1, rhs);
        ir.emplace_back(ir::OpCode::SUB, dest, lhs, tmp2);
      } else {
        ir.emplace_back(arith_unsigned ? ir::OpCode::UMOD : ir::OpCode::MOD, dest, lhs,
                        rhs);
        if (w8) set_ty(arith_ty);
      }
      break;

    case EQ:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      ir.emplace_back(ir::OpCode::MOVEQ, dest, 1, 0);
      break;

    case NE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      ir.emplace_back(ir::OpCode::MOVNE, dest, 1, 0);
      break;

    case GT:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      // 宽度 8 的 CMP 产出三方谓词，后端条件码一律按有符号解释
      ir.emplace_back(w8 || fp ? ir::OpCode::MOVGT
                         : arith_unsigned ? ir::OpCode::MOVHI : ir::OpCode::MOVGT,
                       dest, 1, 0);
      break;

    case GE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      ir.emplace_back(w8 || fp ? ir::OpCode::MOVGE
                         : arith_unsigned ? ir::OpCode::MOVHS : ir::OpCode::MOVGE,
                       dest, 1, 0);
      break;

    case LT:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      ir.emplace_back(w8 || fp ? ir::OpCode::MOVLT
                         : arith_unsigned ? ir::OpCode::MOVLO : ir::OpCode::MOVLT,
                       dest, 1, 0);
      break;

    case LE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8 || fp) set_ty(arith_ty);
      ir.emplace_back(w8 || fp ? ir::OpCode::MOVLE
                         : arith_unsigned ? ir::OpCode::MOVLS : ir::OpCode::MOVLE,
                       dest, 1, 0);
      break;

    case AND: {
      std::string label = ".L.COND" + std::to_string(ctx.get_id()) + "_END";
      ir::IRList end;
      end.emplace_back(ir::OpCode::LABEL, label);

      auto lhs = this->lhs->eval_cond_runtime(ctx, ir);
      ir.emplace_back(ir::OpCode::PHI_MOV, dest, ir::OpName(0));
      ir.back().phi_block = end.begin();
      ir.emplace_back(lhs.else_op, label);
      auto rhs = this->rhs->eval_runtime(ctx, ir);
      if (width_bytes(canonical(expr_type_of(*this->rhs, ctx).id)) >= 8 ||
          is_floating(canonical(expr_type_of(*this->rhs, ctx).id))) {
        // 64 位/浮点右操作数先做真值规范化，避免低字为 0 的高位
        // 非零值被误判（浮点的 -0.0 位模式非零）
        ir::OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(ir::OpCode::CMP, ir::OpName(), rhs, ir::OpName(0));
        ir.back().ty = TypeInfo{expr_type_of(*this->rhs, ctx).id};
        ir.emplace_back(ir::OpCode::MOVNE, tmp, 1, 0);
        rhs = tmp;
      }
      ir.emplace_back(ir::OpCode::PHI_MOV, dest, rhs);
      ir.back().phi_block = end.begin();

      ir.splice(ir.end(), end);
      break;
    }

    case OR: {
      std::string label = ".L.COND" + std::to_string(ctx.get_id()) + "_END";
      ir::IRList end;
      end.emplace_back(ir::OpCode::LABEL, label);

      auto lhs = this->lhs->eval_cond_runtime(ctx, ir);
      ir.emplace_back(ir::OpCode::PHI_MOV, dest, ir::OpName(1));
      ir.back().phi_block = end.begin();
      ir.emplace_back(lhs.then_op, label);
      auto rhs = this->rhs->eval_runtime(ctx, ir);
      if (width_bytes(canonical(expr_type_of(*this->rhs, ctx).id)) >= 8 ||
          is_floating(canonical(expr_type_of(*this->rhs, ctx).id))) {
        ir::OpName tmp = "%" + std::to_string(ctx.get_id());
        ir.emplace_back(ir::OpCode::CMP, ir::OpName(), rhs, ir::OpName(0));
        ir.back().ty = TypeInfo{expr_type_of(*this->rhs, ctx).id};
        ir.emplace_back(ir::OpCode::MOVNE, tmp, 1, 0);
        rhs = tmp;
      }
      ir.emplace_back(ir::OpCode::PHI_MOV, dest, rhs);
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
ir::OpName ast::node::UnaryExpression::_eval_runtime(ir::Context& ctx,
                                                     ir::IRList& ir) {
  if (ctx.cfg->optimize_level > 0) {
    try {
      return this->eval(ctx);
    } catch (...) {
    }
  }
  ir::OpName dest = "%" + std::to_string(ctx.get_id());
  switch (this->op) {
    case PLUS:
      return this->rhs->eval_runtime(ctx, ir);
      break;

    case MINUS: {
      auto val = this->rhs->eval_runtime(ctx, ir);
      // 一元负号的结果类型 = 操作数经整型提升后的类型
      types::TypeId t = types::canonical(types::usual_arithmetic(
          expr_type_of(*this->rhs, ctx).id, expr_type_of(*this->rhs, ctx).id));
      if (types::is_floating(t)) {
        ir.emplace_back(ir::OpCode::FNEG, dest, val);
        ir.back().ty = types::TypeInfo{t};
        return dest;
      }
      ir.emplace_back(ir::OpCode::SUB, dest, 0, val);
      ir.back().ty = types::TypeInfo{t};
      return dest;
      break;
    }

    case NOT: {
      auto val = this->rhs->eval_runtime(ctx, ir);
      ir.push_back(
          ir::IR(ir::OpCode::CMP, ir::OpName(), 0, val));
      // 64 位操作数的非零判断按 64 位比较
      ir.back().ty = types::TypeInfo{types::canonical(
          expr_type_of(*this->rhs, ctx).id)};
      ir.emplace_back(ir::OpCode::MOVEQ, dest, 1, 0);
      return dest;
      break;
    }

    default:
      throw std::runtime_error("Unknow OP");
      break;
  }
}
ir::OpName ast::node::CommaExpression::_eval_runtime(ir::Context& ctx,
                                                     ir::IRList& ir) {
  ir::OpName ret;
  for (auto v : values) {
    ret = v->eval_runtime(ctx, ir);
  }
  return ret;
}

ir::OpName ast::node::FunctionCall::_eval_runtime(ir::Context& ctx,
                                                  ir::IRList& ir) {
  using namespace types;
  // 双池参数槽：整型参数走核心池（r0-r3/栈），已知签名的浮点参数走
  // VFP 池（float 占 1 个 s 槽、double 占 2 个连续偶对齐槽，≥16 溢出
  // 到栈字）；变参调用按 C 默认实参提升（float->double，核心池），
  // 未登记函数的实参按 32 位 int 接口转换。
  struct ArgInfo {
    ir::OpName val;
    int index;
    int n_slot;
    bool vfp;
    types::TypeId ty;
  };
  std::vector<ArgInfo> args;
  ir::OpName dest = "%" + std::to_string(ctx.get_id());
  const ir::FuncSig* sig = ctx.find_function(this->name->name);
  bool variadic = sig && sig->variadic;
  int core_acc = 0, fp_acc = 0;
  for (int i = 0; i < (int)this->args->args.size(); i++) {
    auto val = this->args->args[i]->eval_runtime(ctx, ir);
    types::TypeId src_ty = expr_type_of(*this->args->args[i], ctx).id;
    types::TypeId dst_ty;
    if (sig && i < (int)sig->params.size()) {
      dst_ty = sig->params[i].id;
    } else if (variadic) {
      dst_ty = is_floating(canonical(src_ty)) ? TypeId::DOUBLE
                                              : canonical(src_ty);
    } else {
      dst_ty = TypeId::INT;
    }
    val = convert_runtime(val, src_ty, dst_ty, ctx, ir);
    ArgInfo a;
    a.val = val;
    a.ty = canonical(dst_ty);
    a.vfp = false;
    a.n_slot = width_bytes(a.ty) >= 8 ? 2 : 1;
    if (!variadic && sig && i < (int)sig->params.size() && is_floating(a.ty)) {
      if (a.n_slot == 2 && (fp_acc & 1)) fp_acc++;  // double 槽号偶对齐
      a.index = fp_acc;
      fp_acc += a.n_slot;
      a.vfp = true;
    } else {
      a.index = core_acc;
      core_acc += a.n_slot;
    }
    args.push_back(a);
  }
  for (int i = (int)args.size() - 1; i >= 0; i--) {
    ir.emplace_back(ir::OpCode::SET_ARG, args[i].index, args[i].val);
    if (args[i].n_slot == 2) ir.back().ty = TypeInfo{args[i].ty};
    if (args[i].vfp) ir.back().label = "vfp";  // 标记 VFP 池参数
  }
  ir.emplace_back(ir::OpCode::CALL, dest, this->name->name);
  if (sig) ir.back().ty = sig->return_ty;
  return dest;
}
ir::OpName ast::node::Number::_eval_runtime(ir::Context& ctx, ir::IRList& ir) {
  return this->value;
}

ir::OpName ast::node::StringLiteral::_eval_runtime(ir::Context& ctx,
                                                   ir::IRList& ir) {
  return ir::OpName(ctx.insert_string(this->value));
}

ir::OpName ast::node::Statement::_eval_runtime(ir::Context& ctx,
                                               ir::IRList& ir) {
  this->generate_ir(ctx, ir);
  return ir::OpName();
}

ir::OpName ast::node::Assignment::_eval_runtime(ir::Context& ctx,
                                                ir::IRList& ir) {
  this->generate_ir(ctx, ir);
  if (dynamic_cast<ast::node::ArrayIdentifier*>(this->lhs.get())) {
    assert(ir.back().op_code == ir::OpCode::STORE);
    return ir.back().op3;
  } else {
    assert(ir.back().dest.is_var());
    return ir.back().dest;
  }
}

ir::OpName ast::node::AfterInc::_eval_runtime(ir::Context& ctx,
                                              ir::IRList& ir) {
  auto v = ctx.find_symbol(this->lhs->name);
  auto n0 = std::make_shared<ast::node::Number>(1);
  auto n1 = std::make_shared<ast::node::BinaryExpression>(this->lhs, this->op, n0);
  auto n2 = std::make_shared<ast::node::Assignment>(this->lhs, n1);
  n0->line = n1->line = n2->line = this->line;
  n0->column = n1->column = n2->column = this->column;
  n2->eval_runtime(ctx, ir);
  return v.name;
}

ir::OpName ast::node::EvalStatement::_eval_runtime(ir::Context& ctx,
                                                   ir::IRList& ir) {
  return this->value->eval_runtime(ctx, ir);
}

ast::node::Expression::CondResult ast::node::Expression::_eval_cond_runtime(
    ir::Context& ctx, ir::IRList& ir) {
  CondResult ret;
  auto val = this->eval_runtime(ctx, ir);
  ir.emplace_back(ir::OpCode::CMP, ir::OpName(), val, ir::OpName(0));
  // 64 位值的真值判断按 64 位比较
  ir.back().ty = types::TypeInfo{
      types::canonical(expr_type_of(*this, ctx).id)};
  ret.then_op = ir::OpCode::JNE;
  ret.else_op = ir::OpCode::JEQ;
  return ret;
}

ast::node::Expression::CondResult
ast::node::BinaryExpression::_eval_cond_runtime(ir::Context& ctx,
                                                ir::IRList& ir) {
  if (ctx.cfg->optimize_level > 0) {
    try {
      if (this->eval(ctx)) {
        return CondResult{ir::OpCode::JMP, ir::OpCode::NOOP};
      } else {
        return CondResult{ir::OpCode::NOOP, ir::OpCode::JMP};
      }
    } catch (...) {
    }
  }
  CondResult ret;
  bool cmp_unsigned;
  types::TypeId arith_ty;
  bool w8;
  bool fp;
  {
    using namespace types;
    arith_ty = canonical(
        usual_arithmetic(expr_type_of(*this->lhs, ctx).id,
                         expr_type_of(*this->rhs, ctx).id));
    cmp_unsigned = is_unsigned(arith_ty);
    w8 = width_bytes(arith_ty) >= 8;
    fp = is_floating(arith_ty);
  }
  ir::OpName lhs, rhs;
  if (this->op != AND && this->op != OR) {
    if (ctx.cfg->optimize_level > 0) {
      try {
        lhs = this->lhs->eval(ctx);
      } catch (...) {
        lhs = this->lhs->eval_runtime(ctx, ir);
      }
      try {
        rhs = this->rhs->eval(ctx);
      } catch (...) {
        rhs = this->rhs->eval_runtime(ctx, ir);
      }
    } else {
      lhs = this->lhs->eval_runtime(ctx, ir);
      rhs = this->rhs->eval_runtime(ctx, ir);
    }
    if (w8 || fp) {
      // 64 位/浮点比较：先把操作数转换到公共类型
      lhs = convert_runtime(lhs, expr_type_of(*this->lhs, ctx).id, arith_ty,
                            ctx, ir);
      rhs = convert_runtime(rhs, expr_type_of(*this->rhs, ctx).id, arith_ty,
                            ctx, ir);
    }
  }
  // 浮点比较条件：先经 CMP+MOVcc 归一化为 0/1 谓词（后端将二者融合，
  // 无序值时除 != 外一律为假，即 C 语义），再按整数比较跳转
  // （仅算术比较；&&/|| 走通用真值路径）
  if (fp && this->op != AND && this->op != OR) {
    ir::OpCode movcc;
    switch (this->op) {
      case EQ: movcc = ir::OpCode::MOVEQ; break;
      case NE: movcc = ir::OpCode::MOVNE; break;
      case GT: movcc = ir::OpCode::MOVGT; break;
      case GE: movcc = ir::OpCode::MOVGE; break;
      case LT: movcc = ir::OpCode::MOVLT; break;
      case LE: movcc = ir::OpCode::MOVLE; break;
      default: movcc = ir::OpCode::MOVEQ; break;
    }
    ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
    ir.back().ty = TypeInfo{arith_ty};
    ir::OpName pred = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(movcc, pred, 1, 0);
    ir.emplace_back(ir::OpCode::CMP, ir::OpName(), pred, ir::OpName(0));
    ret.then_op = ir::OpCode::JNE;
    ret.else_op = ir::OpCode::JEQ;
    return ret;
  }
  switch (this->op) {
    case EQ:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      ret.then_op = ir::OpCode::JEQ;
      ret.else_op = ir::OpCode::JNE;
      break;

    case NE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      ret.then_op = ir::OpCode::JNE;
      ret.else_op = ir::OpCode::JEQ;
      break;

    case GT:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      // 宽度 8 的 CMP 产出三方谓词，后端条件码一律按有符号解释
      ret.then_op = w8 ? ir::OpCode::JGT
                       : cmp_unsigned ? ir::OpCode::JMPI : ir::OpCode::JGT;
      ret.else_op = w8 ? ir::OpCode::JLE
                       : cmp_unsigned ? ir::OpCode::JMPLS : ir::OpCode::JLE;
      break;

    case GE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      ret.then_op = w8 ? ir::OpCode::JGE
                       : cmp_unsigned ? ir::OpCode::JMPHS : ir::OpCode::JGE;
      ret.else_op = w8 ? ir::OpCode::JLT
                       : cmp_unsigned ? ir::OpCode::JMPLO : ir::OpCode::JLT;
      break;

    case LT:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      ret.then_op = w8 ? ir::OpCode::JLT
                       : cmp_unsigned ? ir::OpCode::JMPLO : ir::OpCode::JLT;
      ret.else_op = w8 ? ir::OpCode::JGE
                       : cmp_unsigned ? ir::OpCode::JMPHS : ir::OpCode::JGE;
      break;

    case LE:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(), lhs, rhs);
      if (w8) ir.back().ty = TypeInfo{arith_ty};
      ret.then_op = w8 ? ir::OpCode::JLE
                       : cmp_unsigned ? ir::OpCode::JMPLS : ir::OpCode::JLE;
      ret.else_op = w8 ? ir::OpCode::JGT
                       : cmp_unsigned ? ir::OpCode::JMPI : ir::OpCode::JGT;
      break;

    default:
      ir.emplace_back(ir::OpCode::CMP, ir::OpName(),
                      this->eval_runtime(ctx, ir), ir::OpName(0));
      ir.back().ty = types::TypeInfo{
          types::canonical(expr_type_of(*this, ctx).id)};
      ret.then_op = ir::OpCode::JNE;
      ret.else_op = ir::OpCode::JEQ;
      break;
  }
  return ret;
}
}  // namespace syc
