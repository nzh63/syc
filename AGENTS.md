# AGENTS.md — syc 编译器项目指南

本文件为 AI 编码代理（以及新加入的开发者）提供本仓库的构建、测试与架构要点。

syc 是一个教学用 **SysY → ARMv7a** 编译器，自带 ARMv7 汇编解释器 `armsim`
作为测试用例的执行器。

## 仓库结构

```
src/
  parser/        tokens.l + parser.y（WinFlexBison）→ AST
  ast/           AST 节点（node.h/.cpp）、类型系统（type.h）
  ast/generate/  AST → IR（eval.cpp、generate_ir.cpp、convert.cpp、context）
  ir/            IR 定义（ir.h/.cpp：OpCode、OpName、IR::ty）
  ir/generate/   IR 生成
  ir/optimize/   DCE、ICM、phi 重命名等 pass（optimize.cpp 是总入口）
  assembly/      IR → ARMv7 汇编（generate/generate.cpp 指令选择、
                 generate/context.cpp 线性扫描寄存器分配）
armsim/          ARMv7 汇编解释器 + SysY 运行时（native_table）+ VFP 支持
test/            syc-test 测试驱动、functional_test/、performance_test/
tools/           get-winflexbison.sh（手动兜底；CMake 会在配置期自动
                 下载 WinFlexBison 到 external/winflexbison/）
external/        vendored 依赖（winflexbison），不要手动改动
```

## 构建

Windows：

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
# （首次配置时 CMake 自动下载 WinFlexBison；tools/get-winflexbison.sh
#  为手动兜底）
cmake --build build --config Release --target syc armsim syc-test
```

产物：`build/Release/syc.exe`、`build/Release/armsim.exe`、
`build/Release/syc-test.exe`。

Linux：`cmake -B build && cmake --build build`（需要 flex/bison）。

注意事项：

- bison/flex 的生成物在 `build/parser/`，改了 grammar 后直接重新构建即可。

## 测试

```bash
cmake --build build --config Release
./build/Release/syc-test.exe            # functional：-O0 / -O0 -g / -O2 / -O2 -g 四组
./build/Release/syc-test.exe performance   # 仅 -O2（较慢，mm/bitset 约几分钟）
```

- 每个用例是 `test/functional_test/*.sy` + `*.out`（可选 `*.in`）。`.out`
  格式 = 程序 stdout + **最后一行为退出码**（比较前会去掉 `\r` 并 trim）。
- 改了编译器后必须全量跑 functional；动过后端/优化 pass 还应跑 performance。
- 新增测试的期望输出：写一个等价的 C 参考程序，用 MSVC cl 编译运行生成
  （与 armsim 的 `printf` native 同宿主 CRT，`%f/%e/%g` 格式保证一致；
  链接时需 `/utf-8` 并 `/link legacy_stdio_definitions.lib`）。
- 单用例调试：`syc file.sy -O0 -print_ir -o out.s` 看 IR，
  `armsim out.s` 手动执行；`ARMSIM_TRACE=1 armsim out.s` 可逐指令跟踪。

## 架构要点（改动前必读）

管线：**词法/文法 → AST →（constexpr 折叠 +）IR 生成 → IR 优化 → ARM32
指令选择 + 线性扫描分配 → 汇编文本 → armsim 执行**。

核心设计决策：

1. **原生 IR 类型**：64 位（`long long`）与浮点（`float`/`double`）都是
   原生 IR 类型，通过 `IR::ty`（`TypeInfo`）承载，而不是前端降级成
   32 位拼接。宽度 8 的整数运算在后端用寄存器对（低半用本名、高半用
   `name + "#h"` 的独立分配区间）；将来 ARM64 直接映射原生指令。
2. **值规范表示**：宽度 ≤4 的整型一律按「扩展到 32 位」的规范形式保存在
   寄存器/栈槽；窄化发生在赋值/初始化/传参/返回边界（`convert_runtime`）。
   浮点常量在编译期以 IEEE 位模式复用 `int64_t` 通道（float = 低 32 位
   零扩展的位模式，double = 全 64 位），见 `type.h` 的 `convert_const`
   三参重载与 `const_to_double`/`double_to_const_bits`。
3. **浮点 = VFP 独立寄存器池**（`assembly/generate/context.h`）：s0–s15
   caller-saved、s16–s27 callee-saved、s28–s31 草稿；double 占偶对齐的
   连续两个 s 寄存器。与核心池平行的分配函数都以 `freg`/`fp` 命名。
4. **双池调用约定**：整型参数走 r0–r3/栈；浮点参数走 s 槽位（float 1 槽、
   double 2 个连续偶对齐槽，≥16 溢出到 `stack_size[0]` 区域）。
   `SET_ARG` 用 `ir.label == "vfp"` 标记 VFP 池；形参入口相应为
   `$argN` / `$fargN`。变参调用（printf/putf）按 C 默认实参提升，
   float → double 后以位模式走核心池。
5. **armsim 的 native_table 就是 Windows 下的 SysY 运行时**（getint/
   putint/putf/printf、`__aeabi_*` 等）。刻意**没有** `getlong`/`putlong`
   与 `getfloat`/`putfloat`——64 位/浮点 I/O 一律用两个 `getint` 拼接或
   `printf("%f")`；测试也遵守该约束。
6. 浮点比较的语义（NaN 无序）由后端把 `CMP` 与相邻的 `Jcc`/`MOVcc`
   **融合**实现（见 generate.cpp 的 `ir_fp(ir)` 分支）；IR 层的浮点条件
   统一先归一化为 0/1 谓词再跳转（eval.cpp `_eval_cond_runtime` 的 fp
   分支）。改动 CMP/Jcc 相邻性时务必保持这一约定。

`OpName` 命名前缀：`%` 局部变量、`@` 全局变量、`$argN`/`$fargN` 实参、
`%&`/`@&` 数组基址、`^` 导出不重命名；`#h` 后缀是 64 位值的高半。

## 已知文法/语义限制（不是 bug，测试时绕开）

- 无移位、无位运算、无强制转换语法。
- `long long` ↔ 浮点互转不支持（ARM32 无直接指令，需 `__aeabi_l2d` 类
  辅助，编译器会显式报错）。
- `float` 运算遵循 IEEE 单精度舍入，`double` 遵循双精度；整数除零在
  运行时 fatal，浮点除零按 IEEE 得 inf/nan。

## 新增语言特性的常规路径

1. `src/ast/type.h`：TypeId、`width_bytes`、`canonical`、`usual_arithmetic`、
   `convert_const`。
2. `src/parser/tokens.l` + `parser.y`：token、PrimSpec/TypeSpecAcc/
   typespec_finalize、AST 节点构造。
3. `src/ir/ir.h/.cpp`：新 OpCode（或复用 + ty）、print。
4. `src/ir/generate/convert.cpp`：`convert_runtime` 的转换边。
5. `src/ir/generate/eval.cpp` / `generate_ir.cpp`：表达式/语句/声明的
   IR 生成路径（注意 constexpr 折叠 `_eval` 与运行期 `_eval_runtime`
   两条路径都要覆盖）。
6. `src/assembly/`：预扫描（var_width/var_fp 收集）、分配器、指令选择、
   ABI（SET_ARG/CALL/RET/形参入口）。
7. `armsim/`：对应指令解析与执行、必要时 native。
8. 新增 `test/functional_test/*.sy/.out`，四组选项全过 + 全量回归 +
   performance 回归。
9. 更新 README 的类型表与实现说明。

## 代码风格

- 2 空格缩进、LLVM 风格花括号；注释用中文（文件头为 GPLv3 声明）。
- 跟随周边代码的密度与命名习惯；不要为改动加「讲解性」注释。
- git 提交信息用英文短句描述行为（如 `support C-style integer types`），
  用户未要求时不要主动提交。
