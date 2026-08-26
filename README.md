ARMv7a上的SysY编译器

## Try it in your browser

Syc works with browsers that support WebAssembly. Click [here](https://nzh63.github.io/syc/) to it out.

## Build

### Linux / macOS (or MSYS2)

```bash
mkdir build
cd build
cmake ..
make
```

Requires: CMake ≥ 3.0, a C++20 compiler, flex and bison.

### Windows (MSVC)

Requires: Visual Studio 2022 with the C++ toolset and CMake.
flex/bison are not needed — the vendored WinFlexBison is downloaded and
extracted into `external/winflexbison/` automatically at configure time
(mirrored before GitHub; `tools/get-winflexbison.sh` remains as a manual
fallback):

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces `build/Release/syc.exe` (the compiler), `build/Release/armsim.exe`
(see below) and `build/Release/syc-test.exe` (the test fixture, see Test).

## Usage
```
syc [options] file

OPTIONS:
  -o <file>     Place the output into <file>.
  -O<number>    Set optimization level to <number>.
  -g            Produce debugging information in DWARF2 format.
  -yydebug      Enable debug of yacc parsers.
  -print_ast    Print the AST to stderr.
  -print_ir     Print the IR to stderr.
  -print_log    Print logs to assembly comment.

```

## Test

The test fixture is `syc-test`, a small C++ program built as part of the
project (no node.js required anymore):

* Default: compiles each case in-process with the `syc-core` static library
  and executes the generated assembly in-process with the `armsim-core`
  interpreter (which also provides a native implementation of the SysY
  runtime, the counterpart of `test/libsysy.a`). No external tools or paths
  involved.
* `--gcc`: instead run each case through the classic pipeline — link the
  generated assembly against `test/libsysy.a` with `arm-linux-gnueabihf-gcc`
  and execute it under `qemu-arm`. Install those two tools.

Run it via CMake:

```bash
cmake --build build --target test             # functional_test/
cmake --build build --target test_performance # performance_test/
```

or directly: `build/Release/syc-test.exe [functional|performance]
[--gcc] [--jobs N] [--timeout S]`. Each `.sy` case has an
expected output file `.out` (stdout plus the process exit code on the last
line) and an optional stdin file `.in`; generated assembly of failing cases is
kept under `%TEMP%/syc-test-*`. Note `--timeout` only applies to the external
gcc/qemu pipeline; the in-process interpreter has no timeout.

## IR操作数符号说明

|符号|          说明          |
|----|------------------------|
| %  |        局部符号        |
| @  |        全局符号        |
| &  |          数组          |
| ^  |导出符号（不进行重命名）|
