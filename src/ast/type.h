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
#include <cstring>
#include <stdexcept>
#include <string>

namespace syc::ast::types {

enum class TypeId : uint8_t {
  VOID,    // void，仅用于函数返回类型
  CHAR,    // char，1 字节，遵循 AAPCS 视为无符号
  SCHAR,   // signed char，1 字节
  UCHAR,   // unsigned char，1 字节
  BOOL,    // bool，1 字节，值域归一化为 0/1
  SHORT,   // short [int]，2 字节
  USHORT,  // unsigned short [int]，2 字节
  INT,     // int / signed int，4 字节
  UINT,    // unsigned [int]，4 字节
  LONG,    // long [int]，ARMv7a 下为 4 字节
  ULONG,   // unsigned long [int]，4 字节
  LLONG,   // long long [int]，8 字节
  ULLONG,  // unsigned long long [int]，8 字节
  FLOAT,   // float，4 字节，IEEE 单精度
  DOUBLE,  // double，8 字节，IEEE 双精度
};

// TypeInfo 是值语义的可复用类型描述
struct TypeInfo {
  TypeId id = TypeId::INT;
};

constexpr bool is_signed(TypeId t) {
  switch (t) {
    case TypeId::SCHAR:
    case TypeId::SHORT:
    case TypeId::INT:
    case TypeId::LONG:
    case TypeId::LLONG:
      return true;
    default:  // CHAR 按 AAPCS 为无符号；UCHAR/USHORT/UINT/ULONG/ULLONG 无符号
      return false;
  }
}

// 宽度：1/2/4/8 字节。char 在本目标上按 AAPCS 为无符号，
// 这里统一用 width + signedness 描述存储与解释方式。
constexpr int width_bytes(TypeId t) {
  switch (t) {
    case TypeId::VOID:
      return 0;
    case TypeId::CHAR:
      return 1;  // 存储语义上 char 按无符号截取，见 convert 说明
    case TypeId::SCHAR:
    case TypeId::UCHAR:
    case TypeId::BOOL:
      return 1;
    case TypeId::SHORT:
    case TypeId::USHORT:
      return 2;
    case TypeId::INT:
    case TypeId::UINT:
    case TypeId::LONG:
    case TypeId::ULONG:
      return 4;
    case TypeId::LLONG:
    case TypeId::ULLONG:
      return 8;
    case TypeId::FLOAT:
      return 4;
    case TypeId::DOUBLE:
      return 8;
  }
  return 4;
}

constexpr bool is_unsigned(TypeId t) { return !is_signed(t); }

// 浮点类型的判断先于符号性判断：is_signed 对浮点类型无意义
constexpr bool is_floating(TypeId t) {
  return t == TypeId::FLOAT || t == TypeId::DOUBLE;
}

inline const char* name_of(TypeId t) {
  switch (t) {
    case TypeId::VOID: return "void";
    case TypeId::CHAR: return "char";
    case TypeId::SCHAR: return "signed char";
    case TypeId::UCHAR: return "unsigned char";
    case TypeId::BOOL: return "bool";
    case TypeId::SHORT: return "short";
    case TypeId::USHORT: return "unsigned short";
    case TypeId::INT: return "int";
    case TypeId::UINT: return "unsigned";
    case TypeId::LONG: return "long";
    case TypeId::ULONG: return "unsigned long";
    case TypeId::LLONG: return "long long";
    case TypeId::ULLONG: return "unsigned long long";
    case TypeId::FLOAT: return "float";
    case TypeId::DOUBLE: return "double";
  }
  return "?";
}

// 同宽度下唯一化：long 在 ARM32 上等同 int
inline TypeId canonical(TypeId t) {
  switch (t) {
    case TypeId::LONG: return TypeId::INT;
    case TypeId::ULONG: return TypeId::UINT;
    default: return t;
  }
}

inline bool same_type(TypeId a, TypeId b) {
  return canonical(a) == canonical(b);
}

// 编译期整数转换（模拟 C 隐式转换的取值效果）：
// 先按目标宽度截断，再按符号性扩展回 int64 的规范表示。
inline std::int64_t convert_const(std::int64_t v, TypeId t);

namespace detail {
// 整型提升后参与运算的类型：宽度 < 4 一律提升为 int（本平台 int 可覆盖全部小类型的值域）
inline TypeId promote(TypeId t) {
  TypeId c = canonical(t);
  if (width_bytes(c) < 4) return TypeId::INT;
  return c;
}
}  // namespace detail

// C 常用算术转换的结果类型
inline TypeId usual_arithmetic(TypeId a, TypeId b) {
  using namespace detail;
  // 浮点规则先于整型提升：任一操作数为浮点时，有 double 则 double，
  // 否则 float；整型操作数一律转入该浮点类型
  if (is_floating(a) || is_floating(b)) {
    return (a == TypeId::DOUBLE || b == TypeId::DOUBLE) ? TypeId::DOUBLE
                                                        : TypeId::FLOAT;
  }
  TypeId x = promote(a), y = promote(b);
  if (x == y) return x;
  bool ux = is_unsigned(x), uy = is_unsigned(y);
  if (ux == uy) {  // 同符号性：取较宽者
    return width_bytes(x) >= width_bytes(y) ? x : y;
  }
  TypeId u = ux ? x : y;  // 无符号操作数
  TypeId s = ux ? y : x;  // 有符号操作数
  if (width_bytes(u) >= width_bytes(s)) {
    // 无符号侧等级不低于有符号侧：取该无符号类型
    return u;
  }
  // 有符号侧更宽且可覆盖无符号操作数整个值域（此处即 long long 覆盖 unsigned）
  return s;
}

// 浮点常量在编译器内部的规范表示（复用 int64 通道）：
//  FLOAT  -> 低 32 位零扩展的 bit_cast<int32>(float)
//  DOUBLE -> bit_cast<int64>(double)
inline double const_bits_to_double(std::int64_t v, TypeId src) {
  if (src == TypeId::FLOAT) {
    float f;
    std::uint32_t b = static_cast<std::uint32_t>(v);
    memcpy(&f, &b, sizeof(f));
    return static_cast<double>(f);
  }
  double d;
  std::uint64_t b = static_cast<std::uint64_t>(v);
  memcpy(&d, &b, sizeof(d));
  return d;
}

inline std::int64_t double_to_const_bits(double d, TypeId dst) {
  if (dst == TypeId::FLOAT) {
    float f = static_cast<float>(d);
    std::uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return static_cast<std::int64_t>(b);
  }
  std::uint64_t b;
  memcpy(&b, &d, sizeof(b));
  return static_cast<std::int64_t>(b);
}

inline double const_to_double(std::int64_t v, TypeId src) {
  if (is_floating(src)) return const_bits_to_double(v, src);
  if (is_unsigned(src))
    return static_cast<double>(static_cast<std::uint64_t>(v));
  return static_cast<double>(v);
}

// 编译期整数转换（模拟 C 隐式转换的取值效果）：
// 先按目标宽度截断，再按符号性扩展回 int64 的规范表示。
inline std::int64_t convert_const(std::int64_t v, TypeId t);

// 带源类型信息的常量转换：v 按 src 解释（浮点类型为位模式，
// 整数类型为规范 int64 表示），转换为 dst 的规范表示。
inline std::int64_t convert_const(std::int64_t v, TypeId src, TypeId dst) {
  using namespace detail;
  TypeId s = canonical(src), d = canonical(dst);
  if (d == TypeId::BOOL) {
    // 任意标量 -> bool：非零为 1（浮点 -0.0 亦为 0）
    if (s == TypeId::BOOL) return v != 0;
    bool b = is_floating(s) ? (const_bits_to_double(v, s) != 0.0) : (v != 0);
    return b ? 1 : 0;
  }
  if (is_floating(s) || is_floating(d)) {
    if (is_floating(s) && is_floating(d)) {
      // 浮点 ↔ 浮点：经 double 中转（float -> double 精确，double -> float 舍入）
      double x = const_bits_to_double(v, s);
      if (d == TypeId::FLOAT) {
        float f = static_cast<float>(x);
        std::uint32_t b;
        memcpy(&b, &f, sizeof(b));
        return static_cast<std::int64_t>(b);
      }
      return double_to_const_bits(x, TypeId::DOUBLE);
    }
    if (is_floating(d)) {
      // 整数 -> 浮点
      double x = const_to_double(v, s);
      return double_to_const_bits(x, d);
    }
    // 浮点 -> 整数：向零截断后按目标宽度规范化
    double x = const_bits_to_double(v, s);
    if (x >= 9223372036854775808.0 || !(x == x) ||
        x <= -9223372036854775809.0) {
      // 溢出/NaN：结果未定义，取 0 避免 UB（测试应避免触发）
      return 0;
    }
    std::int64_t iv = static_cast<std::int64_t>(x);
    return convert_const(iv, TypeId::INT, d);
  }
  // 整数 -> 整数：源已是规范表示，直接按目标宽度截断
  TypeId c = canonical(dst);
  int w = width_bytes(c);
  std::uint64_t bits = static_cast<std::uint64_t>(v);
  if (w == 1) {
    bits = static_cast<std::uint8_t>(bits);
    if (is_signed(c)) bits = static_cast<std::uint64_t>(static_cast<std::int8_t>(bits));
  } else if (w == 2) {
    bits = static_cast<std::uint16_t>(bits);
    if (is_signed(c)) bits = static_cast<std::uint64_t>(static_cast<std::int16_t>(bits));
  } else if (w == 4) {
    bits = static_cast<std::uint32_t>(bits);
    if (is_signed(c)) bits = static_cast<std::uint64_t>(static_cast<std::int32_t>(bits));
  }
  return static_cast<std::int64_t>(bits);
}

// 旧签名（无源类型信息）：按整数规范表示处理；目标为浮点类型时
// 应使用 3 参重载（浮点位模式需要显式的源类型）。
inline std::int64_t convert_const(std::int64_t v, TypeId t) {
  if (t == TypeId::BOOL) return v != 0 ? 1 : 0;
  if (is_floating(t))
    throw std::runtime_error("convert_const: floating target requires source type");
  // 复用 3 参版本的整数路径：以 INT 作为源，规范表示不变
  int w = width_bytes(canonical(t));
  std::uint64_t bits = static_cast<std::uint64_t>(v);
  if (w == 1) {
    bits = static_cast<std::uint8_t>(bits);
    if (is_signed(t)) bits = static_cast<std::uint64_t>(static_cast<std::int8_t>(bits));
  } else if (w == 2) {
    bits = static_cast<std::uint16_t>(bits);
    if (is_signed(t)) bits = static_cast<std::uint64_t>(static_cast<std::int16_t>(bits));
  } else if (w == 4) {
    bits = static_cast<std::uint32_t>(bits);
    if (is_signed(t)) bits = static_cast<std::uint64_t>(static_cast<std::int32_t>(bits));
  }
  return static_cast<std::int64_t>(bits);
}

}  // namespace syc::ast::types

// 常用便捷引入（位于 syc 作用域）
namespace syc {
using ast::types::TypeId;
using ast::types::TypeInfo;
namespace types = ast::types;
}  // namespace syc
