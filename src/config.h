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
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace syc {
struct Config {
  int optimize_level = 0;
  bool print_ast = false;
  bool print_ir = false;
  bool print_log = false;
  bool enable_dwarf2 = false;
  FILE* input = stdin;
  std::ostream* output = &std::cout;
  std::string input_filename = "<stdin>";

  // 按命令行约定解析（argv[0] 为程序名，忽略）
  static Config parse_arg(int argc, char** argv);
};

}  // namespace syc
