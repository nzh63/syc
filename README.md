ARMv7a上的SysY编译器

## Try it in your browser

Syc works with browsers that support WebAssembly. Click [here](https://nzh63.github.io/syc/) to it out.

## Build

```bash
mkdir build
cd build
cmake ..
make
```

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

1. Install [node.js](https://nodejs.org), `arm-linux-gnueabihf-gcc` and`qemu-arm` .
2. `cd build && make test`

## IR操作数符号说明

|符号|          说明          |
|----|------------------------|
| %  |        局部符号        |
| @  |        全局符号        |
| &  |          数组          |
| ^  |导出符号（不进行重命名）|
