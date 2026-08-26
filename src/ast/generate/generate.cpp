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
#include "ast/generate/generate.h"

#include <atomic>
#include <chrono>
#include <stdexcept>

// 可重入 flex/bison（api.pure + parse/lex-param）：全部状态经
// scanner/Parser 引用传递，多个线程可以同时解析
extern int yyparse(syc::ast::Parser& parser, void* scanner);
extern int yylex_init(void **scanner);
extern int yylex_destroy(void *scanner);
extern void yyset_in(FILE* _in_str, void* scanner);
extern void yyset_extra(int _user_data, void *scanner);

namespace syc::ast {
Parser::~Parser() {
  if (scanner) yylex_destroy(scanner);
}

std::unique_ptr<node::Root> Parser::parse(FILE* input,
                                          const std::string& input_filename) {
  this->input_filename = input_filename;
  if (yylex_init(&scanner) != 0)
    throw std::runtime_error("yylex_init failed");
  root = nullptr;
  yyset_in(input, scanner);
  // yylineno 初始即为 1（yyset_lineno 需要已分配的缓冲区，此处不可用）
  yyset_extra(1, scanner);  // 列号计数器
  try {
    yyparse(*this, scanner);
  } catch (std::exception& e) {
    yylex_destroy(scanner);
    scanner = nullptr;
    throw std::runtime_error(input_filename + ": " + e.what());
  } catch (...) {
    yylex_destroy(scanner);
    scanner = nullptr;
    throw;
  }
  yylex_destroy(scanner);
  scanner = nullptr;
  return std::unique_ptr<node::Root>(root);
}

std::unique_ptr<syc::ast::node::Root> generate(
    FILE* input, const std::string& input_filename) {
  Parser parser;
  return parser.parse(input, input_filename);
}

}  // namespace syc::ast
