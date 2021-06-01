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

#include "config.h"
#include "generate_asm.h"
#include "node.h"
#include "optimize_asm.h"
#include "optimize_ir.h"

SYC::Node::Root* root;
extern int yyparse();
extern int yylex_destroy();
extern void yyset_lineno(int _line_number);

constexpr auto N = 3;

int main(int argc, char** argv) {
  using namespace SYC;
  config::parse_arg(argc, argv);
  yyset_lineno(1);
  yyparse();
  yylex_destroy();
  if (config::print_ast) root->print();
  ContextIR ctx;
  IRList ir;
  root->generate_ir(ctx, ir);
  if (config::optimize_level > 0) optimize_ir(ir);
  if (config::print_ir)
    for (auto& i : ir) i.print(std::cerr, true);
  std::stringstream buffer[N];
  if (config::print_log)
    generate_asm(ir, buffer[0], buffer[0]);
  else
    generate_asm(ir, buffer[0]);
  if (config::optimize_level > 0) {
    for (int i = 0; i < N - 1; i++) {
      optimize_asm(buffer[i], buffer[i + 1]);
    }
    optimize_asm(buffer[N - 1], *config::out);
  } else {
    *config::out << buffer[0].str();
  }
  if (config::out != &std::cout) delete config::out;
};
