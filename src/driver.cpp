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
#include "driver.h"

#include <iostream>
#include <sstream>

#include "assembly/generate/generate.h"
#include "assembly/optimize/optimize.h"
#include "ast/generate/generate.h"
#include "config.h"
#include "ir/generate/generate.h"
#include "ir/optimize/optimize.h"

namespace syc {

int compile_main(int argc, char** argv) {
  using namespace syc;
  const Config cfg = Config::parse_arg(argc, argv);

  try {
    auto root = syc::ast::generate(cfg.input, cfg.input_filename);
    if (cfg.print_ast) root->print();

    auto ir = syc::ir::generate(root.get(), cfg);
    if (cfg.optimize_level > 0) {
      syc::ir::optimize(ir);
    }
    if (cfg.print_ir)
      for (auto& i : ir) i.print(std::cerr, true);

    std::stringstream buffer;
    if (cfg.print_log) {
      syc::assembly::generate(ir, buffer, buffer, cfg);
    } else {
      syc::assembly::generate(ir, buffer, cfg);
    }
    if (cfg.optimize_level > 0) {
      syc::assembly::optimize(buffer, *cfg.output);
    } else {
      *cfg.output << buffer.str();
    }

    if (cfg.output != &std::cout) delete cfg.output;
  } catch (const std::exception& e) {
    std::cerr << "syc: error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

}  // namespace syc
