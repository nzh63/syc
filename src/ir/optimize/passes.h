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

#include "ir/optimize/passes/branch_normalize_elimination.h"
#include "ir/optimize/passes/compile_time_evaluation.h"
#include "ir/optimize/passes/constant_propagation.h"
#include "ir/optimize/passes/dead_code_elimination.h"
#include "ir/optimize/passes/inline_small_functions.h"
#include "ir/optimize/passes/invariant_code_motion.h"
#include "ir/optimize/passes/local_common_constexpr_function_elimination.h"
#include "ir/optimize/passes/local_common_subexpression_elimination.h"
#include "ir/optimize/passes/loop_unrolling.h"
#include "ir/optimize/passes/multiply_to_shift.h"
#include "ir/optimize/passes/optimize_phi_var.h"
#include "ir/optimize/passes/pure_function_analysis.h"
#include "ir/optimize/passes/tail_call_elimination.h"
#include "ir/optimize/passes/unreachable_code_elimination.h"