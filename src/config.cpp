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
#include "config.h"

extern int yydebug;

namespace SYC::config {

int optimize_level = 0;
std::ostream* out = &std::cout;
bool print_ast = false;
bool print_ir = false;
bool print_log = false;

void parse_arg(int argc, char** argv) {
  char* in = NULL;
  yydebug = 0;
  int s = 0;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (std::string("-o") == argv[i])
        s = 1;
      else if (std::string("-O0") == argv[i])
        optimize_level = 0;
      else if (std::string("-O1") == argv[i])
        optimize_level = 1;
      else if (std::string("-O2") == argv[i])
        optimize_level = 2;
      else if (std::string("-yydebug") == argv[i])
        yydebug = 1;
      else if (std::string("-print_ast") == argv[i])
        print_ast = true;
      else if (std::string("-print_ir") == argv[i])
        print_ir = true;
      else if (std::string("-print_log") == argv[i])
        print_log = true;
    } else {
      if (s == 1) {
        if (std::string("-") == argv[i])
          out = &std::cout;
        else
          out = new std::ofstream(argv[i], std::ofstream::out);
      } else if (s == 0) {
        if (std::string("-") == argv[i])
          in = NULL;
        else
          in = argv[i];
      }
      s = 0;
    }
  }
  if (in != NULL) freopen(in, "r", stdin);
}
}  // namespace SYC::config