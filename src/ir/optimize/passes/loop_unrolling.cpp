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
#include "ir/optimize/passes/loop_unrolling.h"

#include <map>
#include <string>

#include "ir/optimize/passes/ir_fold.h"

namespace syc::ir::passes {
namespace {

// 展开规模上限：迭代次数模拟上限与展开后指令数上限
constexpr long long kMaxTrips = 4096;
constexpr long long kMaxUnrollInsts = 300;

bool is_jcc(OpCode op) {
  return (op >= OpCode::JEQ && op <= OpCode::JGT) ||
         (op >= OpCode::JMPHS && op <= OpCode::JMPI);
}

bool is_movcc(OpCode op) {
  return op >= OpCode::MOVEQ && op <= OpCode::MOVHI;
}

// while 循环在扁平 IR 中的固定骨架（见 generate_ir 的单遍生成）：
//   [PHI_MOV %phi, %init]           预头贡献（紧贴头标签之前）
//   LABEL L                         头
//   CMP  %phi, <imm 边界>
//   J<cc> LEND                      条件不满足时退出
//   LABEL LDO
//   <body>                          无任何控制流指令
//   ADD/SUB %next, %phi, <imm 步长>
//   LABEL LCONT
//   PHI_MOV %phi, %next             回边贡献
//   JMP  L
struct Skeleton {
  IRList::iterator pre_phi, cmp, body_begin, body_end, inc, latch_phi, latch;
  std::string phi, next, init;
  std::int64_t start = 0, step = 0, bound = 0;
  TypeId cmp_ty = TypeId::INT;
  OpCode exit_cc = OpCode::JEQ;
  TypeId inc_ty = TypeId::INT;
};

bool match_loop(IRList::iterator header, IRList::iterator jmp,
                Skeleton& s) {
  // 预头：恰好一条 PHI_MOV，dest 为归纳变量
  auto p = std::prev(header);
  if (p->op_code != OpCode::PHI_MOV) return false;
  if (!p->dest.is_var() || !p->op1.is_var()) return false;
  s.pre_phi = p;
  s.phi = p->dest.name;
  s.init = p->op1.name;
  // 头部：CMP %phi, imm + J<cc>
  auto cmp = std::next(header);
  if (cmp->op_code != OpCode::CMP) return false;
  if (!cmp->op1.is_var() || cmp->op1.name != s.phi) return false;
  if (!cmp->op2.is_imm()) return false;
  auto jx = std::next(cmp);
  if (!is_jcc(jx->op_code)) return false;
  s.cmp = cmp;
  s.bound = cmp->op2.value;
  s.cmp_ty = cmp->ty.id;
  s.exit_cc = jx->op_code;
  // body：DO 标签之后、递增指令之前
  auto ldo = std::next(jx);
  if (ldo->op_code != OpCode::LABEL) return false;
  // 回边侧：JMP <- PHI_MOV %phi, %next <- LABEL <- ADD/SUB %next, %phi, imm
  auto lp = std::prev(jmp);
  if (lp->op_code != OpCode::PHI_MOV) return false;
  if (!lp->dest.is_var() || lp->dest.name != s.phi || !lp->op1.is_var())
    return false;
  s.next = lp->op1.name;
  s.latch_phi = lp;
  auto lc = std::prev(lp);
  if (lc->op_code != OpCode::LABEL) return false;
  auto inc = std::prev(lc);
  if (inc->op_code != OpCode::ADD && inc->op_code != OpCode::SUB) return false;
  if (!inc->dest.is_var() || inc->dest.name != s.next) return false;
  if (!inc->op1.is_var() || inc->op1.name != s.phi || !inc->op2.is_imm())
    return false;
  // 浮点归纳变量不处理（位模式不能按整数回绕模拟）
  if (types::is_floating(types::canonical(inc->ty.id))) return false;
  s.latch = jmp;
  s.inc = inc;
  s.inc_ty = inc->ty.id;
  s.step = inc->op_code == OpCode::ADD ? inc->op2.value : -inc->op2.value;
  s.body_begin = std::next(ldo);
  s.body_end = inc;
  if (s.body_begin == s.body_end) return false;
  // body 内不允许任何控制流，也不允许重定义归纳变量/初值变量
  for (auto b = s.body_begin; b != s.body_end; b++) {
    if (b->op_code == OpCode::LABEL || b->op_code == OpCode::JMP ||
        b->op_code == OpCode::PHI_MOV || is_jcc(b->op_code) ||
        is_movcc(b->op_code))
      return false;
    if (b->dest.is_var() &&
        (b->dest.name == s.phi || b->dest.name == s.init))
      return false;
  }
  return true;
}

// %next 只能由递增指令定值、被回边 PHI 使用（否则副本间的值链会断）
bool next_only_latch(IRList::iterator fn, IRList::iterator fe,
                     const Skeleton& s) {
  for (auto it = fn; it != fe; it++) {
    if (it == s.inc) continue;  // 定值点
    if (it->dest.is_var() && it->dest.name == s.next) return false;
    if (it == s.pre_phi) continue;
    bool used = (it->op1.is_var() && it->op1.name == s.next) ||
                (it->op2.is_var() && it->op2.name == s.next) ||
                (it->op3.is_var() && it->op3.name == s.next);
    if (used && it != s.latch_phi) return false;
  }
  return true;
}

// 归纳变量初值：预头直线块内 %init 的唯一定值（MOV imm）
bool find_start(IRList& ir, const Skeleton& s, std::int64_t& start) {
  auto it = s.pre_phi;
  bool found = false;
  while (it != ir.begin()) {
    it--;
    if (it->op_code == OpCode::LABEL || it->op_code == OpCode::FUNCTION_BEGIN)
      break;
    if (it->op_code == OpCode::JMP || is_jcc(it->op_code) ||
        is_movcc(it->op_code))
      return false;
    if (it->dest.is_var() && it->dest.name == s.init) {
      if (found || it->op_code != OpCode::MOV || !it->op1.is_imm())
        return false;
      start = it->op1.value;
      found = true;
    }
  }
  return found;
}

// 按运行时语义模拟迭代次数：循环在第 k 次迭代前值为 v_k，
// 退出条件成立时停止。宽度 w<8 时按 32 位回绕（与后端/armsim 一致）。
bool count_trips(const Skeleton& s, std::int64_t start, long long& trips,
                 std::int64_t& final_v) {
  int w = types::width_bytes(types::canonical(s.inc_ty));
  auto wrap = [w](std::int64_t x) {
    return w >= 8 ? x : (std::int64_t)(std::int32_t)(std::uint32_t)x;
  };
  std::int64_t v = wrap(start);
  trips = 0;
  while (!ir_fold::cond_holds(
      s.exit_cc, ir_fold::cmp_flags(v, s.bound, s.cmp_ty))) {
    trips++;
    if (trips > kMaxTrips) return false;
    v = wrap((std::int64_t)((std::uint64_t)v + (std::uint64_t)s.step));
  }
  final_v = v;
  return true;
}

// 展开一份：trips 份 body（归纳变量替换为立即数）+ 循环后终值。
// LABEL L/LEND/LDO/LCONT 全部保留，phi_block 等迭代器不失效；
// 展开后不再有指向 L 的跳转， stray 标签由后续 pass 清理或保留（无害）。
bool unroll_one(IRList& ir, IRList::iterator fn, IRList::iterator fe,
                IRList::iterator header, IRList::iterator jmp) {
  Skeleton s;
  if (!match_loop(header, jmp, s)) return false;
  if (!next_only_latch(fn, fe, s)) return false;
  std::int64_t start = 0;
  if (!find_start(ir, s, start)) return false;
  long long trips = 0;
  std::int64_t final_v = 0;
  if (!count_trips(s, start, trips, final_v)) return false;
  long long body_size = 0;
  for (auto b = s.body_begin; b != s.body_end; b++) body_size++;
  if (trips * body_size > kMaxUnrollInsts) return false;

  IRList repl;
  std::int64_t v = start;
  int w = types::width_bytes(types::canonical(s.inc_ty));
  auto wrap = [w](std::int64_t x) {
    return w >= 8 ? x : (std::int64_t)(std::int32_t)(std::uint32_t)x;
  };
  for (long long k = 0; k < trips; k++) {
    for (auto b = s.body_begin; b != s.body_end; b++) {
      IR c = *b;
      if (c.op1.is_var() && c.op1.name == s.phi) c.op1 = OpName(v);
      if (c.op2.is_var() && c.op2.name == s.phi) c.op2 = OpName(v);
      if (c.op3.is_var() && c.op3.name == s.phi) c.op3 = OpName(v);
      repl.push_back(std::move(c));
    }
    v = wrap((std::int64_t)((std::uint64_t)v + (std::uint64_t)s.step));
  }
  // 循环后归纳变量的终值（0 次迭代时为初值）
  IR tail(OpCode::MOV, OpName(s.phi), OpName(final_v), OpName());
  tail.ty.id = s.inc_ty;
  repl.push_back(std::move(tail));

  ir.erase(s.pre_phi);
  auto after = ir.erase(std::next(header), std::next(jmp));
  ir.splice(after, repl);
  return true;
}

bool unroll_function(IRList& ir, IRList::iterator fn, IRList::iterator fe) {
  std::map<std::string, IRList::iterator> labels;
  std::map<std::string, long long> label_idx;
  long long idx = 0;
  for (auto it = fn; it != fe; it++, idx++) {
    if (it->op_code == OpCode::LABEL) {
      labels[it->label] = it;
      label_idx[it->label] = idx;
    }
  }
  idx = 0;
  for (auto it = fn; it != fe; it++, idx++) {
    if (it->op_code != OpCode::JMP) continue;
    auto t = labels.find(it->label);
    if (t == labels.end()) continue;
    if (label_idx[t->first] >= idx) continue;  // 只处理回边
    if (unroll_one(ir, fn, fe, t->second, it)) return true;
  }
  return false;
}

}  // namespace

void loop_unrolling(IRList& ir) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto fn = ir.begin(); fn != ir.end(); fn++) {
      if (fn->op_code != OpCode::FUNCTION_BEGIN) continue;
      auto fe = std::next(fn);
      while (fe != ir.end() && fe->op_code != OpCode::FUNCTION_END) fe++;
      if (fe == ir.end()) break;
      if (unroll_function(ir, fn, fe)) {
        changed = true;
        break;  // 迭代器已失效，重新扫描
      }
      fn = fe;
    }
  }
}

}  // namespace syc::ir::passes
