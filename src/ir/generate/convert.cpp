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
#include "ir/generate/convert.h"

namespace syc::ir {
namespace {
bool is_wide(types::TypeId t) {
  return syc::ast::types::width_bytes(syc::ast::types::canonical(t)) >= 8;
}
}  // namespace

OpName convert_runtime(OpName v, types::TypeId src, types::TypeId dst,
                       Context& ctx, IRList& ir) {
  using namespace syc::ast::types;
  if (v.is_imm()) return OpName(convert_const(v.value, src, dst));
  // 地址（数组名退化）永远按指针传递，不做数值转换
  if (v.is_var() && (v.name.rfind("@&", 0) == 0 || v.name.rfind("%&", 0) == 0))
    return v;
  TypeId s = canonical(src);
  TypeId d = canonical(dst);
  if (s == d) return v;
  if (d == TypeId::BOOL) {
    // 标量 -> bool：非零归一化为 1（CMP+MOVNE；浮点 -0.0 为 0）
    OpName dest = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(OpCode::CMP, ir::OpName(), v, ir::OpName(0));
    ir.back().ty = TypeInfo{s};
    ir.emplace_back(OpCode::MOVNE, dest, 1, 0);
    return dest;
  }
  if (is_floating(s) && is_floating(d)) {
    // FLOAT <-> DOUBLE
    OpName dest = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(OpCode::F2F, dest, v);
    ir.back().ty = TypeInfo{d};
    return dest;
  }
  if (is_floating(s)) {
    // 浮点 -> 整数：向零截断到 32 位；8 字节整数不支持（无直接指令）
    if (is_wide(d)) {
      throw std::runtime_error(
          "conversion between long long and floating is not supported yet");
    }
    OpName i32 = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(is_signed(d) ? OpCode::F2I : OpCode::F2U, i32, v);
    // dest 是 32 位整型；ty 记录源浮点宽度（位模式的解释方式），
    // 供常量折叠/编译期求值解码，后端据此选择 VCVT 的 F32/F64 变体
    ir.back().ty = TypeInfo{s};
    if (width_bytes(d) == 4) return i32;
    // 再按目标宽度截断规范化
    OpName dest = "%" + std::to_string(ctx.get_id());
    switch (d) {
      case TypeId::CHAR:
      case TypeId::UCHAR:
        ir.emplace_back(OpCode::TRUNC_U8, dest, i32);
        return dest;
      case TypeId::SCHAR:
        ir.emplace_back(OpCode::TRUNC_S8, dest, i32);
        return dest;
      case TypeId::SHORT:
        ir.emplace_back(OpCode::TRUNC_S16, dest, i32);
        return dest;
      case TypeId::USHORT:
        ir.emplace_back(OpCode::TRUNC_U16, dest, i32);
        return dest;
      default:
        return i32;
    }
  }
  if (is_floating(d)) {
    // 整数 -> 浮点：按源符号性转换；8 字节整数不支持（无直接指令）
    if (is_wide(s)) {
      throw std::runtime_error(
          "conversion between long long and floating is not supported yet");
    }
    OpName dest = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(is_signed(s) ? OpCode::SITOF : OpCode::UITOF, dest, v);
    ir.back().ty = TypeInfo{d};
    return dest;
  }
  if (is_wide(s) == is_wide(d)) {
    // 同宽度类（都窄或都是 64 位）：规范表示一致，无需动作
    return v;
  }
  if (is_wide(d)) {
    // 窄 -> 64 位：按源类型符号性扩展
    OpName dest = "%" + std::to_string(ctx.get_id());
    ir.emplace_back(is_signed(s) ? OpCode::SEXT : OpCode::ZEXT, dest, v);
    ir.back().ty = TypeInfo{d};
    return dest;
  }
  // 64 位 -> 窄：先取低 32 位（MOV，dest 宽度 <= 4 时后端只搬低字）
  OpName lo = "%" + std::to_string(ctx.get_id());
  ir.emplace_back(OpCode::MOV, lo, v);
  ir.back().ty = TypeInfo{d};
  if (width_bytes(d) == 4) return lo;
  // 再按目标宽度截断规范化
  OpName dest = "%" + std::to_string(ctx.get_id());
  switch (d) {
    case TypeId::CHAR:
    case TypeId::UCHAR:
      ir.emplace_back(OpCode::TRUNC_U8, dest, lo);
      return dest;
    case TypeId::SCHAR:
      ir.emplace_back(OpCode::TRUNC_S8, dest, lo);
      return dest;
    case TypeId::SHORT:
      ir.emplace_back(OpCode::TRUNC_S16, dest, lo);
      return dest;
    case TypeId::USHORT:
      ir.emplace_back(OpCode::TRUNC_U16, dest, lo);
      return dest;
    default:
      return lo;
  }
}
}  // namespace syc::ir
