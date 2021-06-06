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
#include <iostream>
#include <sstream>

#include "assembly/generate/generate.h"
#include "assembly/optimize/optimize.h"
#include "ast/generate/generate.h"
#include "ast/node.h"
#include "config.h"
#include "ir/generate/generate.h"
#include "ir/optimize/optimize.h"

int main(int argc, char** argv) {
  using namespace syc;
  config::parse_arg(argc, argv);

  auto* root = syc::ast::generate();
  if (config::print_ast) root->print();

  auto ir = syc::ir::generate(root);
  if (config::optimize_level > 0) {
    syc::ir::optimize(ir);
  }
  if (config::print_ir)
    for (auto& i : ir) i.print(std::cerr, true);

  std::stringstream buffer;
  if (config::print_log) {
    syc::assembly::generate(ir, buffer, buffer);
  } else {
    syc::assembly::generate(ir, buffer);
  }
  if (config::optimize_level > 0) {
    syc::assembly::optimize(buffer, *config::out);
  } else {
    *config::out << buffer.str();
  }

  if (config::out != &std::cout) delete config::out;
};
