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
// IR 层运算求值：常量传播与编译期解释器共用。
// 语义与后端/armsim 对齐：值用 int64 规范表示（浮点为位模式），
// 不可折叠（除零、移位越界等）返回 nullopt，留给运行时保持原语义。
#pragma once
#include <cstdint>
#include <cstring>
#include <optional>

#include "ast/type.h"
#include "ir/ir.h"

namespace syc::ir::passes::ir_fold {

// ---------------------------------------------------------------------------
// ARM NZCV 标志与条件码判定（与 armsim 的 cond_holds / set_flags 一致）
// ---------------------------------------------------------------------------
struct NZCV {
  bool N, Z, C, V;
};

// CMP 语义（与 armsim 的 set_flags_sub / set_flags_vcmp 一致）
inline NZCV cmp_flags(std::int64_t a, std::int64_t b, TypeId ty) {
  if (types::is_floating(ty)) {
    double x = types::const_bits_to_double(a, ty);
    double y = types::const_bits_to_double(b, ty);
    bool un = !(x == y) && !(x < y) && !(x > y);
    if (un) return {false, true, true, true};
    return {x < y, x == y, x >= y, false};
  }
  int w = types::width_bytes(types::canonical(ty));
  if (w >= 8) {
    std::uint64_t ua = (std::uint64_t)a, ub = (std::uint64_t)b;
    std::uint64_t r = ua - ub;
    bool V = (((ua ^ ub) & (ua ^ r)) >> 63) & 1;
    return {(bool)((r >> 63) & 1), r == 0, ua >= ub, (bool)V};
  }
  std::uint32_t ua = (std::uint32_t)a, ub = (std::uint32_t)b;
  std::uint32_t r = ua - ub;
  bool V = (((ua ^ ub) & (ua ^ r)) >> 31) & 1;
  return {(bool)((r >> 31) & 1), r == 0, ua >= ub, (bool)V};
}

inline bool cond_holds(OpCode cc, const NZCV& f) {
  switch (cc) {
    case OpCode::JEQ: return f.Z;
    case OpCode::JNE: return !f.Z;
    case OpCode::JLT: return f.N != f.V;
    case OpCode::JGE: return f.N == f.V;
    case OpCode::JLE: return f.Z || f.N != f.V;
    case OpCode::JGT: return !f.Z && f.N == f.V;
    case OpCode::JMPHS: return f.C;
    case OpCode::JMPLO: return !f.C;
    case OpCode::JMPLS: return f.C || f.Z;
    case OpCode::JMPI: return !f.C && !f.Z;
    case OpCode::MOVEQ: return f.Z;
    case OpCode::MOVNE: return !f.Z;
    case OpCode::MOVLT: return f.N != f.V;
    case OpCode::MOVGE: return f.N == f.V;
    case OpCode::MOVLE: return f.Z || f.N != f.V;
    case OpCode::MOVGT: return !f.Z && f.N == f.V;
    case OpCode::MOVHS: return f.C;
    case OpCode::MOVLO: return !f.C;
    case OpCode::MOVLS: return f.C || f.Z;
    case OpCode::MOVHI: return !f.C && !f.Z;
    default: return false;
  }
}

inline bool is_unary_op(OpCode op) {
  switch (op) {
    case OpCode::TRUNC_U8:
    case OpCode::TRUNC_S8:
    case OpCode::TRUNC_U16:
    case OpCode::TRUNC_S16:
    case OpCode::SEXT:
    case OpCode::ZEXT:
    case OpCode::FNEG:
    case OpCode::SITOF:
    case OpCode::UITOF:
    case OpCode::F2I:
    case OpCode::F2U:
    case OpCode::F2F:
      return true;
    default:
      return false;
  }
}

inline bool is_foldable_op(OpCode op) {
  if (is_unary_op(op)) return true;
  switch (op) {
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::IMUL:
    case OpCode::IDIV:
    case OpCode::MOD:
    case OpCode::UDIV:
    case OpCode::UMOD:
    case OpCode::FDIV:
    case OpCode::AND:
    case OpCode::OR:
    case OpCode::SAL:
    case OpCode::SAR:
    case OpCode::LSR:
      return true;
    default:
      return false;
  }
}

inline std::optional<std::int64_t> fold(OpCode op, TypeId ty, std::int64_t a,
                                        std::int64_t b) {
  using types::width_bytes;
  using types::is_signed;
  int w = width_bytes(types::canonical(ty));
  switch (op) {
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::IMUL: {
      // 浮点加/减/乘（内联展开后会出现带浮点 ty 的这三类运算）：
      // 按元素精度计算再编码回位模式
      if (types::is_floating(ty)) {
        if (ty == TypeId::FLOAT) {
          float fa = (float)types::const_bits_to_double(a, ty);
          float fb = (float)types::const_bits_to_double(b, ty);
          float r = op == OpCode::ADD ? fa + fb
                                      : op == OpCode::SUB ? fa - fb : fa * fb;
          std::uint32_t bits;
          std::memcpy(&bits, &r, sizeof(bits));
          return (std::int64_t)bits;
        }
        double da = types::const_bits_to_double(a, ty);
        double db = types::const_bits_to_double(b, ty);
        double r = op == OpCode::ADD ? da + db
                                     : op == OpCode::SUB ? da - db : da * db;
        return types::double_to_const_bits(r, TypeId::DOUBLE);
      }
      if (op == OpCode::ADD)
        return (std::int64_t)((std::uint64_t)a + (std::uint64_t)b);
      if (op == OpCode::SUB)
        return (std::int64_t)((std::uint64_t)a - (std::uint64_t)b);
      return (std::int64_t)((std::uint64_t)a * (std::uint64_t)b);
    }
    case OpCode::IDIV:
    case OpCode::MOD: {
      if (b == 0) return std::nullopt;
      if (a == INT64_MIN && b == -1) return std::nullopt;
      if (w == 4 && a == INT32_MIN && b == -1) return std::nullopt;
      return op == OpCode::IDIV ? a / b : a % b;
    }
    case OpCode::UDIV:
    case OpCode::UMOD: {
      std::uint64_t ub = (std::uint64_t)b;
      if (ub == 0) return std::nullopt;
      if (w == 4) {
        std::uint32_t ua = (std::uint32_t)a, ub32 = (std::uint32_t)b;
        if (ub32 == 0) return std::nullopt;
        return op == OpCode::UDIV ? (std::int64_t)(ua / ub32)
                                  : (std::int64_t)(ua % ub32);
      }
      return op == OpCode::UDIV ? (std::int64_t)((std::uint64_t)a / ub)
                                : (std::int64_t)((std::uint64_t)a % ub);
    }
    case OpCode::FDIV: {
      if (ty == TypeId::FLOAT) {
        float fa = (float)types::const_bits_to_double(a, ty);
        float fb = (float)types::const_bits_to_double(b, ty);
        float r = fa / fb;
        std::uint32_t bits;
        std::memcpy(&bits, &r, sizeof(bits));
        return (std::int64_t)bits;
      }
      double r = types::const_bits_to_double(a, ty) /
                 types::const_bits_to_double(b, ty);
      return types::double_to_const_bits(r, TypeId::DOUBLE);
    }
    case OpCode::AND: return (a != 0 && b != 0) ? 1 : 0;
    case OpCode::OR: return (a != 0 || b != 0) ? 1 : 0;
    case OpCode::SAL: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) {
        std::uint32_t r = (std::uint32_t)a << b;
        return is_signed(ty) ? (std::int64_t)(std::int32_t)r : (std::int64_t)r;
      }
      return (std::int64_t)((std::uint64_t)a << b);
    }
    case OpCode::SAR: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) return (std::int64_t)((std::int32_t)a >> b);
      return a >> b;
    }
    case OpCode::LSR: {
      if (b < 0 || b >= w * 8) return std::nullopt;
      if (w == 4) return (std::int64_t)((std::uint32_t)a >> b);
      return (std::int64_t)((std::uint64_t)a >> b);
    }
    case OpCode::TRUNC_U8: return (std::int64_t)(std::uint8_t)a;
    case OpCode::TRUNC_S8: return (std::int64_t)(std::int8_t)a;
    case OpCode::TRUNC_U16: return (std::int64_t)(std::uint16_t)a;
    case OpCode::TRUNC_S16: return (std::int64_t)(std::int16_t)a;
    case OpCode::SEXT: return (std::int64_t)(std::int32_t)a;
    case OpCode::ZEXT: return (std::int64_t)(std::uint64_t)(std::uint32_t)a;
    case OpCode::FNEG: {
      if (ty == TypeId::FLOAT) {
        float f = (float)types::const_bits_to_double(a, ty);
        float r = -f;
        std::uint32_t bits;
        std::memcpy(&bits, &r, sizeof(bits));
        return (std::int64_t)bits;
      }
      return types::double_to_const_bits(
          -types::const_bits_to_double(a, ty), TypeId::DOUBLE);
    }
    case OpCode::SITOF: {
      double x = (double)(std::int32_t)a;
      return types::double_to_const_bits(x, ty);
    }
    case OpCode::UITOF: {
      double x = (double)(std::uint32_t)a;
      return types::double_to_const_bits(x, ty);
    }
    case OpCode::F2I: {
      // 与 armsim 的 VCVT 一致：宿主 static_cast 语义
      double x = types::const_bits_to_double(a, ty);
      return (std::int64_t)(std::int32_t)x;
    }
    case OpCode::F2U: {
      double x = types::const_bits_to_double(a, ty);
      return (std::int64_t)(std::uint32_t)x;
    }
    case OpCode::F2F: {
      TypeId src = (ty == TypeId::FLOAT) ? TypeId::DOUBLE : TypeId::FLOAT;
      return types::convert_const(a, src, ty);
    }
    default:
      return std::nullopt;
  }
}

}  // namespace syc::ir::passes::ir_fold
