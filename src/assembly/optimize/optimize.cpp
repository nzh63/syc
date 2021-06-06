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
#include "assembly/optimize/optimize.h"

#include <istream>
#include <ostream>
#include <sstream>

#include "assembly/optimize/passes.h"

using namespace std;

namespace {
void _optimize(istream& in, ostream& out) {
  using namespace syc::assembly::passes;
  std::stringstream buffer;
  peephole(in, buffer);
  reorder(buffer, out);
}
}  // namespace

namespace syc::assembly {
void optimize(istream& in, ostream& out) {
  constexpr auto N = 3;
  std::stringstream buffer[N];
  _optimize(in, buffer[0]);
  for (int i = 0; i < N - 1; i++) {
    _optimize(buffer[i], buffer[i + 1]);
  }
  _optimize(buffer[N - 1], out);
}
}  // namespace syc::assembly
