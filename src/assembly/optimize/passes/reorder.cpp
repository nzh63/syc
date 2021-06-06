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
#include "assembly/optimize/passes/reorder.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"

using namespace std;
namespace {
class AsmOpName {
 protected:
  enum Type {
    Var,     // 寄存器变量，例如r0
    MemVar,  // 内存变量,例如[sp,#48]
    Imm,     // 立即数，例如#0
    Null,    // 啥都没有
  };
  Type type;

 public:
  std::string name;
  int value;
  int length;
  AsmOpName();
  AsmOpName(std::string name);
  AsmOpName(int value);
  // 产生内存变量,length代表[]内变量个数
  AsmOpName(std::string name, int length);
  bool operator==(AsmOpName const& p1) {
    if (this->name == p1.name)
      return true;
    else
      return false;
  }
  bool is_var() const { return this->type == AsmOpName::Type::Var; }
  bool is_mem_var() const { return this->type == AsmOpName::Type::MemVar; }
  bool is_imm() const { return this->type == AsmOpName::Type::Imm; }
  bool is_null() const { return this->type == AsmOpName::Type::Null; }
};

class AsmInst {
 public:
  bool isjump;  // 是否为跳转指令,默认不是
  std::string op_code;
  std::string label;
  AsmOpName op1, op2, op3, dest;
  AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1, AsmOpName op2,
          AsmOpName op3, std::string label = "", bool isjump = false);
  AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1, AsmOpName op2,
          std::string label = "", bool isjump = false);
  AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1,
          std::string label = "", bool isjump = false);
  AsmInst(std::string op_code, AsmOpName dest, std::string label = "",
          bool isjump = false);
  AsmInst(std::string op_code, std::string label = "", bool isjump = false);
  bool use(const AsmOpName& op) const {
    assert(op.is_var() || op.is_mem_var());
#define F(op1)                                          \
  if (this->op1.is_var()) {                             \
    if (this->op1.name == op.name) {                    \
      return true;                                      \
    }                                                   \
  } else if (this->op1.is_mem_var()) {                  \
    if (this->op1.name.find(op.name) != string::npos) { \
      return true;                                      \
    }                                                   \
  }
    F(op1);
    F(op2);
    F(op3);
#undef F
    return false;
  }
};

// 我们的想法其实很简单，就是类似于
// c = c+d;
// e = c+d;
// 对应汇编是
//  ADD r0, r11, r4
//  ADD r0, r0, r4
// 这两句肯定数据相关了，如果后面可以的话，我们就把第二句话往后移动一点
// 这里有个问题，如果遇到跳转指令怎么办？ => 返回的AsmInst中带有特殊值，外面
// 接收到了就对当前指令矩阵进行重排，然后输出前面的指令，再输出本条指令

AsmOpName::AsmOpName() : type(AsmOpName::Type::Null) {}
AsmOpName::AsmOpName(std::string name)
    : type(AsmOpName::Type::Var), name(name) {}
AsmOpName::AsmOpName(int value) : type(AsmOpName::Type::Imm), value(value) {}
AsmOpName::AsmOpName(std::string name, int length)
    : type(AsmOpName::Type::MemVar), name(name), length(length) {}

AsmInst::AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1,
                 AsmOpName op2, AsmOpName op3, std::string label, bool isjump)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(op2),
      op3(op3),
      isjump(isjump),
      label(label) {}
AsmInst::AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1,
                 AsmOpName op2, std::string label, bool isjump)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(op2),
      op3(AsmOpName()),
      isjump(isjump),
      label(label) {}
AsmInst::AsmInst(std::string op_code, AsmOpName dest, AsmOpName op1,
                 std::string label, bool isjump)
    : op_code(op_code),
      dest(dest),
      op1(op1),
      op2(AsmOpName()),
      op3(AsmOpName()),
      isjump(isjump),
      label(label) {}
AsmInst::AsmInst(std::string op_code, AsmOpName dest, std::string label,
                 bool isjump)
    : op_code(op_code),
      dest(dest),
      op1(AsmOpName()),
      op2(AsmOpName()),
      op3(AsmOpName()),
      isjump(isjump),
      label(label) {}
AsmInst::AsmInst(std::string op_code, std::string label, bool isjump)
    : op_code(op_code),
      dest(AsmOpName()),
      op1(AsmOpName()),
      op2(AsmOpName()),
      op3(AsmOpName()),
      isjump(isjump),
      label(label) {}

AsmInst Create_AsmInst(std::string line1) {
  stringstream ss(line1);
  std::string token;
  std::vector<AsmOpName> cisu;
  int isop = 1;
  int count = 0;
  bool isbreak = false;
  bool isstr = false;
  std::string opcode;
  while (ss >> token) {
    // out << token << "\t";
    // 第一步，先把最后面那个逗号去了
    // 请注意，这里没有处理 smull
    // （因为两个dest不好搞定，正好有4个op所以够用，放在模板里搞）
    if (token[token.size() - 1] == ',')
      token = token.substr(0, token.size() - 1);
    if (isop) {  // 如果是操作符
      if (token.starts_with("B") || token == "SMULL") {
        // 如果是跳转
        isbreak = true;
        opcode = token;
        isop = 0;

      } else if (token == "STR") {
        // 如果是STR
        isstr = true;
        opcode = token;
        isop = 0;

      } else {
        // 如果不是跳转
        opcode = token;
        isop = 0;
      }
    } else {                  // 不是操作符
      if (token[0] == '[') {  // 内存变量
        int length = 1;
        if (token.find(',') != token.npos) {
          length = 2;
          // [sp,#1] => sp,#1   由于只会记录相关性，所以只加入sp
          token = token.substr(1, token.find(',') - 1);
        } else {
          token = token.substr(1, token.length() - 2);
        }

        cisu.emplace_back(AsmOpName(token, length));
      } else if (token[0] == '#') {  // 立即数
        token = token.substr(1, token.length() - 1);
        istringstream is(token);
        int i;
        is >> i;
        cisu.emplace_back(AsmOpName(i));

      } else {  // 正常寄存器
        cisu.emplace_back(AsmOpName(token));
      }
    }
    count++;  // 包括操作符一共几个操作数
  }
  // 构造AsmInst
  if (isbreak) {  // 如果是跳转语句,token应该是标号
    return AsmInst(opcode, token, true);
  } else if (isstr) {  // 如果是str 则第一个是op1,第二个是dest
    return AsmInst(opcode, cisu[1], cisu[0]);

  } else if (opcode.starts_with("#")) {  // 注释
    return AsmInst(opcode, AsmOpName());
  } else {
    switch (count) {
      case 2:
        return AsmInst(opcode, cisu[0]);
        break;
      case 3:
        return AsmInst(opcode, cisu[0], cisu[1]);
        break;
      case 4:
        return AsmInst(opcode, cisu[0], cisu[1], cisu[2]);
        break;
      case 5:
        return AsmInst(opcode, cisu[0], cisu[1], cisu[2], cisu[3]);
        break;
      default:
        throw std::runtime_error("too many argument");
        break;
    }
  }
}

// 输出两个指令的相关性，如果不相关输出-1
int Calcu_Correlation(AsmInst inst1, AsmInst inst2) {
  // 写后读相关
  if ((inst1.dest.is_mem_var() || inst1.dest.is_var()) &&
      inst2.use(inst1.dest)) {
    // 相关情况1.2：写后读相关(1.3合并成功)
    // LDR r1, [r2,#4]
    // ADD r0, r0, r1
    // 相关情况1.3：写后读相关
    // LDR r1, [r2,#4]
    // MOV r0, r1
    if (inst1.op_code.starts_with("LDR")) {
      return 4;
    }
    // 相关情况1.1：写后读相关
    // ADD r0, r11, r4
    // ADD r0, r0, r4
    // z=这种情况要写回,2个周期以后才行
    // 相关情况1.4：写后读相关
    // ADD r12, r11, r12
    // LDR r6, [r12]           => xxx  r6,[r12,#12]
    // 相关情况1.5：写后读相关
    // ADD r6, r11, r12 (不一定是ADD，其他的也有可能)
    // CMP r6, r12
    else if (inst1.op_code == "ADD" || inst1.op_code == "SUB") {
      return 2;
    } else if (inst1.op_code == "MUL") {
      return 3;
    }
    // 相关情况9.9：写后读相关
    // 这是最后一个，相当于最长前缀匹配
    // MOV r12, r0
    // ADD r12, r11, r12   (实际上这里不只是ADD，所有操作都是这样)
    // 只要指令2的源寄存器和指令1的目的寄存器一样，就会有至少为0的间隔
    // 或者指令2的目的寄存器和指令1的源寄存器一样
    return 0;
  }
  // 读后写相关
  // 指令2写寄存器之前指令1必须完成读操作
  if ((inst2.dest.is_var() || inst2.dest.is_mem_var()) &&
      inst1.use(inst2.dest)) {
    return 0;
  }
  // 写后写相关
  if ((inst1.dest.is_var() || inst1.dest.is_mem_var()) &&
      (inst2.dest.is_var() || inst2.dest.is_mem_var()) &&
      inst1.dest.name == inst2.dest.name) {
    return 0;
  }
  // 访存相同指针
  if (inst1.op_code.starts_with("LDR") && inst2.op_code.starts_with("STR")) {
    assert(inst1.op1.is_mem_var());
    assert(inst2.dest.is_mem_var());
    if (inst1.op1.name != "sp" && inst2.dest.name != "sp") {
      return 0;
    }
  }
  if (inst1.op_code.starts_with("STR") && inst2.op_code.starts_with("LDR")) {
    assert(inst1.dest.is_mem_var());
    assert(inst2.op1.is_mem_var());
    if (inst1.dest.name != "sp" && inst2.op1.name != "sp") {
      return 0;
    }
  }
  // CMP会影响标志位
  if (inst1.op_code == "CMP" || inst2.op_code == "CMP") {
    return 0;
  }
  // return 指令
  if (inst2.op_code == "MOV" && inst2.dest.name == "pc") {
    assert(false);
    return 0;
  }
  // call
  if (inst2.dest.is_var() && inst2.dest.name == "lr" && inst1.op_code == "BL") {
    assert(false);
    return 1;
  }
  return -1;
}

// 显示链表内容
void print_list(std::list<int> l0) {
  std::list<int>::iterator itList;
  for (itList = l0.begin(); itList != l0.end();) {
    // std::cout<< *itList <<std::endl;
    printf("%d->", *itList);
    itList++;
  }
}

// 将指令Im插入到指令In之后
void insert_list(std::list<int>& l0, int m, int n) {
  std::list<int>::iterator itList;
  for (itList = l0.begin(); itList != l0.end();) {
    if (*itList == m) {
      itList = l0.erase(itList);
      break;
    } else
      itList++;
  }
  // 找出val=m的节点,删除它

  for (itList = l0.begin(); itList != l0.end();) {
    if (*itList == n) {
      itList++;
      itList = l0.insert(itList, m);
      break;
    } else
      itList++;
  }
  // 找出val=n的节点,将val=m的节点插入至它之后
}

std::list<int> Initial_list(int list_size) {
  std::list<int>::iterator itList;
  std::list<int> l0;
  int i = 0;
  for (itList = l0.begin(); i < list_size;) {
    l0.insert(itList, i);
    i++;
  }
  return l0;
  // 初始化链表 链表结构为head->0->1->2->3->.....->NULL
}

int** makearray(int m) {
  int** p = new int*[m];  // 开辟行
  int* array = new int[m * m];

  for (int i = 0; i < m; i++) {
    p[i] = array + i * m;  // 开辟列
  }
  return p;
}

void freearray(int** array) {
  delete[] array[0];
  delete[] array;
}

// 输出stl等中间数组
void testarray(int* array, int array_size) {
  for (int i = 0; i < array_size; i++) {
    printf("%d ", array[i]);
  }
  printf("\n");
}

// 计算临时变量t表 t(n,k)表示第n条指令最多被第k条阻碍的时钟周期长度
// 它每行实际上是步长为1递增的
int** tcal(int** array, int array_size) {
  int** t = NULL;
  t = makearray(array_size);

  int n, k;
  for (n = 0; n < array_size; n++) {
    for (k = 0; k < array_size; k++) {
      t[n][k] = array[n][k] - (n - k - 1);
    }
  }

  return t;
}

// 找到array中最大元素的下标
void max_lift2right(int* array, int array_size, int* max_tabel) {
  int i;
  int max = array[0];
  int tabel = 0;
  for (i = 0; i < array_size; i++) {
    if (max < array[i]) {
      tabel = i;
      max = array[i];
    }
  }
  *max_tabel = tabel;
  return;
}

// 计算ceil表 其第n项表示能最大阻碍第n条指令的指令的下标
void ceilcal(int** array, int array_size, int** t, int* ceil) {
  int n, k;
  int max_in_t;

  ceil[0] = -1;
  for (n = 1; n < array_size; n++) {
    max_lift2right(t[n], n, &max_in_t);
    if (t[n][max_in_t] >= 0) {
      ceil[n] = max_in_t;
    } else {
      ceil[n] = -1;
    }
  }

  return;
}

// 计算stl表 其第n项表示第n条指令最多被其他指令阻碍多少个时间周期
void stlcal(int** array, int array_size, int** t, int* stl) {
  int n, k;
  int max_in_t;

  stl[0] = 0;
  for (n = 1; n < array_size; n++) {
    max_lift2right(t[n], n, &max_in_t);
    if (t[n][max_in_t] >= 0) {
      stl[n] = t[n][max_in_t];
    } else {
      stl[n] = 0;
    }
  }

  return;
}

// 计算cyl表 第n项表明从块开始到执行完第n条指令用的时钟周期
void cylcal(int** array, int array_size, int* stl, int* cyl) {
  int n = 0;
  cyl[0] = 1;
  for (n = 1; n < array_size; n++) {
    cyl[n] = cyl[n - 1] + stl[n] + 1;
  }
  return;
}

// 计算up表 第n项为第n条指令能合法上移的距离
void upcal(int** array, int array_size, int** t, int* up) {
  int n = 0;
  int i;
  for (n = 0; n < array_size; n++) {
    for (i = n - 1; i >= 0; i--) {
      if (array[n][i] >= 0) {
        up[n] = i;
        break;
      }
    }
    if (i == -1) {
      up[n] = -1;
    }
  }
  return;
}

// 计算down表 第n项为第n条指令能合法下移的距离
void downcal(int** array, int array_size, int** t, int* down) {
  int n = 0;
  int i;
  for (n = 0; n < array_size; n++) {
    for (i = n + 1; i < array_size; i++) {
      if (array[i][n] >= 0) {
        down[n] = i;
        break;
      }
    }
    if (i == array_size) {
      down[n] = array_size;
    }
  }
  return;
}

// 计算block表 它有aray_size*2项 其相邻两项表示一个关联指令子集
void creat_block_tabel(int* stl, int* ceil, int* block, int array_size) {
  memset(block, -1, sizeof(int) * 2 * array_size);
  int n;
  int test;
  int i = 0;
  for (n = 0; n < array_size; n++) {
    test = stl[n];
    if (stl[n] > 0) {
      block[2 * i] = ceil[n];
      block[2 * i + 1] = n;
      i++;
    }
  }
}

// 判断指令Im是否在任何一个关联指令子集内
bool im_in_block(int im, int* block, int array_size) {
  int i;
  for (i = 0; i < array_size; i++) {
    if ((im >= block[2 * i]) && (im <= block[2 * i + 1])) {
      return true;
    }
  }

  return false;
}

void asm_swap_node(int* stl, int* ceil, int* down, int* up, int* block,
                   int array_size, std::list<int>& l0) {
  int n, m;
  int t;
  auto* move = new bool[array_size];
  memset(move, 0, array_size * sizeof(bool));
  for (n = 0; n < array_size; n++) {
    // 遍历stl,找到大于0的才处理
    if (stl[n] <= 0) {
      continue;
    }

    // 找到了In,它有优化的可能

    // 移动过多条指令后,In就与其他指令没有相关性了,也没有必要再移动了
    t = stl[n];

    // 从In开始向下找
    for (m = n + 1; (m < array_size) && (t > 0); m++) {
      if (im_in_block(m, block, array_size)) {
        break;
      }
      if (move[m] != false) {
        continue;
      }

      if (up[m] < n) {
        assert(m < array_size);
        assert(n - 1 < array_size);
        insert_list(l0, m, n - 1);
        // printf("now(%d,%d):", m, n - 1);
        // print_list(l0);
        // printf("\n");
        t = t - 1;
        move[m] = true;
      }
    }

    // 从与In关联的指令开始向上找
    for (m = ceil[n] - 1; (m >= 0) && (t > 0); m--) {
      // 如果Im在任何一个关联指令子集中就说明所有在其之上的指令不能调整,否则可能引起冲突
      if (im_in_block(m, block, array_size)) {
        break;
      }
      // 已经移动过的指令不用考虑
      if (move[m] != false) {
        continue;
      }

      // 满足移动条件
      if (down[m] > ceil[n]) {
        assert(m < array_size);
        assert(ceil[n] < array_size);
        insert_list(l0, m, ceil[n]);

        // printf("now(%d,%d):", m, ceil[n]);
        // print_list(l0);
        // printf("\n");
        // 实际上要减去Im的时钟周期,这里认为它是1
        t = t - 1;
        // Im已经被移动
        move[m] = true;
      }
    }
  }
  delete[] move;
}

// 判断某个块是否全是-1，也就是所有指令都没有相关性，如果是的话，不优化了
bool IfBlockAllNR(int** array, int length) {
  bool flag = true;
  for (int i = 0; i < length; i++) {
    for (int j = 0; j < length; j++) {
      if (array[i][j] != -1) {
        return false;
      }
    }
  }
  return true;
}

// 根据二维数组和指令的字符串集，输出优化以后的指令集
// 2个问题：
//  1. 还有没有没有提到的跳转块？除了下面这群B开头的
//  2. 还有哪些可能的相关
void Opt_Asm_blk_print(int** array, int block_size,
                       std::vector<std::string> InstBlk, ostream& out) {
  bool debug_print = false;
  int count = 0;
  int tttt = 0;

  int* stl = new int[block_size];
  int* cyl = new int[block_size];
  int* ceil = new int[block_size];
  int* up = new int[block_size];
  int* down = new int[block_size];
  int* block = new int[2 * block_size];

  // start

  int** t = tcal(array, block_size);

  ceilcal(array, block_size, t, ceil);
  stlcal(array, block_size, t, stl);
  cylcal(array, block_size, stl, cyl);
  upcal(array, block_size, t, up);
  downcal(array, block_size, t, down);

  creat_block_tabel(stl, ceil, block, block_size);
  if (debug_print) {
    testarray(ceil, block_size);
    testarray(stl, block_size);
    testarray(cyl, block_size);
    testarray(up, block_size);
    testarray(down, block_size);
    testarray(block, block_size * 2);
  }

  std::list<int> l0 = Initial_list(block_size);
  // print_list(head);

  asm_swap_node(stl, ceil, down, up, block, block_size, l0);

  if (debug_print) print_list(l0);

  std::list<int>::iterator itList;
  for (itList = l0.begin(); itList != l0.end();) {
    out << InstBlk[*itList] << endl;
    *itList++;
  }

  // 打印
  delete[] stl;
  delete[] cyl;
  delete[] ceil;
  delete[] up;
  delete[] down;
  delete[] block;
  freearray(t);
}
}  // namespace

namespace syc::assembly::passes {
void reorder(std::istream& in, std::ostream& out) {
  string line1;
  string line1_orin;
  std::stringstream in2;
  bool isfirst = true;
  std::vector<int> blk_linenum;
  // optblk_linenum用于表示潜在代码可重排的界限(3,8,15,26,......)
  std::vector<int> optblk_linenum;
  // 存储AsmInst
  std::vector<AsmInst> AsmBlock;
  // 用于储存潜在可优化代码块的原本字符串，方便后续输出，以免重新根据AsmInst去构造
  std::vector<std::string> LineBlock;
  int length = 0;
  int linenum = 0;
  while (getline(in, line1)) {
    in2 << line1 << endl;
    // 第一遍扫描，直接记录块的号数
    stringstream ss(line1);
    string token;
    if (ss >> token) {
      if (token.ends_with(":") || token.starts_with(".") ||
          token.starts_with("B") || token == "SMULL" ||
          (token == "MOV" && line1.find("pc") != string::npos)) {
        blk_linenum.emplace_back(linenum);
        linenum++;
      } else {
        linenum++;
      }
    }
  }
  // 确认潜在opt的linenum
  for (int i = 0; i < (int)blk_linenum.size() - 1; i++) {
    if ((blk_linenum[i + 1] - blk_linenum[i]) > 5) {
      optblk_linenum.emplace_back(blk_linenum[i]);
      optblk_linenum.emplace_back(blk_linenum[i + 1]);
    }
  }
  linenum = 0;
  int optnum = 0;
  int temp = 0;
  int head = 0, tail = 0;
  bool flag = false;
  in.seekg(ios::beg);
  if (optblk_linenum.empty()) {
    // 如果很短，
    out << in2.str() << std::endl;
  } else {
    head = optblk_linenum[optnum];
    tail = optblk_linenum[optnum + 1];
    int** blk_array = nullptr;
    int temp_inst_blk = 0;  // 在指令vector中的数目
    while (getline(in2, line1)) {
      // 第二遍扫描，某一块指令数目>5才会去优化
      if (linenum < head || (linenum == head)) {
        out << line1 << std::endl;
        linenum++;
      } else if (linenum == (head + 1)) {
        // std::cout<< "新建块" << std::endl;
        // 新建一个矩阵 和 vector<指令>
        int block_size = tail - head - 1;
        blk_array = new int*[block_size];
        for (int i = 0; i < block_size; ++i) {
          blk_array[i] = new int[block_size];
        }
        // 默认初始化为-1
        for (int i = 0; i < block_size; i++) {
          for (int j = 0; j < block_size; j++) {
            blk_array[i][j] = -1;
          }
        }
        AsmInst temp_AsmInst = Create_AsmInst(line1);
        LineBlock.emplace_back(line1);
        AsmBlock.emplace_back(temp_AsmInst);
        linenum++;
      } else if ((linenum > (head + 1) && (linenum < tail))) {
        AsmInst temp_AsmInst = Create_AsmInst(line1);

        // 计算矩阵
        int size = AsmBlock.size();
        for (int i = 0; i < size; i++) {
          int temp1;
          // cout << LineBlock[i] << endl;
          // cout << line1 << endl;
          // cout << endl;
          temp1 = Calcu_Correlation(AsmBlock[i], temp_AsmInst);
          blk_array[size][i] = temp1;
        }
        // 加入优化框
        // std::cout<< "加入优化框" << std::endl;
        // std::cout<< line1 << std::endl;

        LineBlock.emplace_back(line1);
        AsmBlock.emplace_back(temp_AsmInst);
        linenum++;
      } else if (linenum == tail) {
        // 当Linenum = tail数目的时候,通过矩阵算出四个的值
        // 对矩阵/AsmBlock进行销毁处理,然后执行重排函数
        int nn = tail - head;

        // 判断是否需要进行优化，如果不需要的话，直接就输出了
        if (IfBlockAllNR(blk_array, (nn - 1))) {
          // 无关，直接输出
          // std::cout<< "无关，直接输出" << std::endl;

          for (int i = 0; i < LineBlock.size(); i++) {
            out << LineBlock[i] << std::endl;
          }

        } else {
          // 有关，优化输出,这里遍历那个列表即可
          // std::cout<< "有关，优化输出" << std::endl;

          Opt_Asm_blk_print(blk_array, LineBlock.size(), LineBlock, out);
        }
        for (int i = 0; i < nn - 1; i++) {
          delete[] blk_array[i];
        }
        delete[] blk_array;
        AsmBlock.clear();
        LineBlock.clear();
        // 为了防止溢出
        if (((optnum + 2) < optblk_linenum.size())) {
          optnum += 2;
          head = optblk_linenum[optnum];
          tail = optblk_linenum[optnum + 1];
        }

        linenum++;
        out << line1 << std::endl;
      } else if (linenum > tail) {  // 到最后了，全部输出,最后一块就不优化了
        out << line1 << std::endl;
        linenum++;
      } else {
        assert(false);
      }
    }
  }
}
}  // namespace syc::assembly::passes
