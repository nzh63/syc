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
#include "ir/optimize/passes/multiply_to_shift.h"

namespace syc::ir::passes {
void multiply_to_shift(IRList &ir) {
  for (auto &inst : ir) {
    if (inst.op_code != OpCode::IMUL) continue;
    // 只处理 32 位整型；64 位/浮点乘法不适用
    auto c = types::canonical(inst.ty.id);
    if (types::is_floating(c) || types::width_bytes(c) != 4) continue;
    // 恰有一个立即数操作数且为 2 的幂（1 <= k <= 31）
    OpName factor;
    if (inst.op1.is_imm() && !inst.op2.is_imm())
      factor = inst.op1;
    else if (inst.op2.is_imm() && !inst.op1.is_imm())
      factor = inst.op2;
    else
      continue;
    std::uint64_t v = (std::uint64_t)factor.value;
    if (v == 0 || (v & (v - 1)) != 0) continue;
    int k = 0;
    while ((v >> k) != 1) k++;
    if (k == 0 || k > 31) continue;
    // 改写为 SAL：SAL 后端要求 op2 为立即数
    inst.op_code = OpCode::SAL;
    if (inst.op1.is_imm()) {
      inst.op1 = inst.op2;
      inst.op2 = OpName((std::int64_t)k);
    } else {
      inst.op2 = OpName((std::int64_t)k);
    }
  }
}
}  // namespace syc::ir::passes
