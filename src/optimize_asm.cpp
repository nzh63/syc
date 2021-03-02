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
#include "optimize_asm.h"

#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

namespace {
std::string opt_asm1(std::string line1) {
  std::vector<string> temp1;
  std::string tar_line;
  stringstream s0(line1);
  string token;
  while (s0 >> token) {
    if (token[token.length() - 1] == ',') token.pop_back();

    temp1.emplace_back(token);
  }
  // ADD R0,R1,#0  => MOV R0,R1
  if (temp1[0] == "ADD" && temp1[3].compare("#0") == 0) {
    tar_line.append("    MOV ");
    tar_line.append(temp1[1]);
    tar_line.append(", ");
    tar_line.append(temp1[2]);
    // out << "tar_line is "<< tar_line <<std::endl;
    return tar_line;
  }
  // MOV r0, r0 => <empty-string>
  if (temp1[0] == "MOV" && temp1[1] == temp1[2]) {
    return "";
  }
  return line1;
}

/*
传入的Line1，line2 产出tar_line，若匹配成功，返回优化后结果，并置length=0
若匹配失败，则按兵不动
*/
bool opt_asm2(std::string line1, std::string line2, std::string& tar_line,
              int& length) {
  // out<< "--------------调用函数" <<std::endl;
  // out<< "line1:"<<line1 <<std::endl;
  // out<< "line2:"<<line2 <<std::endl;
  // out<< "--------------" <<std::endl;

  std::vector<string> temp1;
  std::vector<string> temp2;
  stringstream s0(line1);
  stringstream s1(line2);
  string token;
  while (s0 >> token) {
    if (token[token.length() - 1] == ',')
      token = token.substr(0, token.length() - 1);
    temp1.emplace_back(token);
  }
  while (s1 >> token) {
    if (token[token.length() - 1] == ',')
      token = token.substr(0, token.length() - 1);
    temp2.emplace_back(token);
  }
  // case1  ld r0,a / st a,r0  => ld r0,a
  if (temp1[0].compare("LDR") == 0 && (temp2[0].compare("STR") == 0) &&
      (temp1[1].compare(temp2[1]) == 0) && (temp1[2].compare(temp2[2]) == 0)) {
    length = 0;
    tar_line = line1;
    return true;
  }
  // case2 STR R0,[SP,#0] , LDR R1,[SP,#0] => STR R0,[SP,#0], MOV R1,R0
  else if (temp1[0].compare("STR") == 0 && (temp2[0].compare("LDR") == 0) &&
           (temp1[2].compare(temp2[2]) == 0)) {
    length = 0;
    tar_line = line1;
    tar_line.append("\n    MOV ");
    tar_line.append(temp2[1]);
    tar_line.append(", ");
    tar_line.append(temp1[1]);

    return true;
  }

  return false;
}
}  // namespace

void optimize_asm(istream& in, ostream& out) {
  string line1;
  string line1_orin;
  string line0;
  string line_target;
  bool isfirst = true;

  int length = 0;
  while (getline(in, line1)) {
    //如果是注释，直接输出
    stringstream ss(line1);
    string token;
    if (ss >> token) {
      if (token[0] == ('#')) {
        out << line1 << std::endl;
        continue;
      } else {
        //先窗口为1优化下
        line1 = opt_asm1(line1);
        if (line1.empty()) continue;
        // out << line1 << std::endl;
        //然后是窗口2
        if (length == 0) {
          length++;
          line0 = line1;
          continue;
        } else {
          if (opt_asm2(line0, line1, line_target, length)) {
            out << line_target << std::endl;
            continue;
          } else {
            out << line0 << std::endl;
            line0 = line1;
          }
        }
      }
    }
  }
  if (length == 1) {
    length = 0;
    out << line0 << std::endl;
  }
}
