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
#include "ast/generate/generate.h"

extern int yyparse();
extern int yylex_destroy();
extern void yyset_lineno(int _line_number);
void yyset_in(FILE* _in_str);

namespace syc::ast {
syc::ast::node::Root* root = nullptr;
syc::ast::node::Root* generate(FILE* input) {
  yyset_in(input);
  yyset_lineno(1);
  yyparse();
  yylex_destroy();
  return root;
}
}  // namespace syc::ast