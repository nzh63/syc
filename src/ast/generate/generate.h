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

#include <atomic>
#include <cstdlib>
#include <memory>

#include "ast/node.h"

namespace syc::ast {

class Parser {
 public:
  Parser() = default;
  ~Parser();
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  // 解析 SysY 源文件，返回 AST；语法/词法错误时抛 std::runtime_error
  // （消息带 input_filename 前缀）
  std::unique_ptr<node::Root> parse(
      FILE* input = stdin, const std::string& input_filename = "<stdin>");

  // —— 以下供 bison 动作（parser.y）访问 ——
  node::Root* root = nullptr;  // CompUnit 归约时写入
  void* scanner = nullptr;     // flex scanner（yyscan_t）
  std::string input_filename;
};

std::unique_ptr<syc::ast::node::Root> generate(
    FILE* input = stdin, const std::string& input_filename = "<stdin>");
}  // namespace syc::ast