/*
 * armsim -- a tiny interpreter for the ARMv7-a assembly dialect emitted by syc.
 *
 * It parses the GNU-as style assembly produced by src/assembly/generate.cpp,
 * lays the data section into flat memory and executes the instruction stream,
 * implementing the SysY runtime library (the Windows counterpart of
 * test/libsysy.a) and the libgcc helpers (__aeabi_idiv/__aeabi_idivmod)
 * natively.
 *
 * Copyright (C) 2020-2021  nzh63, skywf21
 * Licensed under the GNU GPL version 3.
 */
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace armsim {

enum class Op : uint8_t {
  MOV,
  MOV32,
  ADD,
  SUB,
  MUL,
  SMULL,
  LSL,
  ASR,
  CMP,
  TST,
  AND,
  ORRS,
  SUBS,
  LDR,
  STR,
  B,
  BL,
  NOP,
  // VFPv3 (float/double)
  VMOV,
  VADD,
  VSUB,
  VMUL,
  VDIV,
  VNEG,
  VABS,
  VCMP,
  VCVT,
  VMRS,
  VLDR,
  VSTR,
};

// VCVT conversion kinds: [dst kind][src kind], dst/src in {S32, U32, F32, F64}
enum class Vcvt : uint8_t {
  F32_S32,
  F64_S32,
  F32_U32,
  F64_U32,
  S32_F32,
  U32_F32,
  S32_F64,
  U32_F64,
  F64_F32,
  F32_F64,
};

// Full ARM condition codes; only EQ/NE/LT/LE/GT/GE are emitted today but
// evaluating NZCV covers all of them cheaply.
enum class Cond : uint8_t {
  AL,
  EQ,
  NE,
  CS,
  CC,
  MI,
  PL,
  VS,
  VC,
  HI,
  LS,
  GE,
  LT,
  GT,
  LE,
};

struct MemOperand {
  int base = -1;      // register holding the base address
  int off_reg = -1;   // register offset, or -1
  long long off_imm = 0;  // immediate offset
  bool valid = false;
};

struct Inst {
  Op op;
  Cond cond = Cond::AL;
  bool S = false;          // sets flags (SUBS)
  int rd = -1, rn = -1, rm = -1, ra = -1;
  long long imm = 0;
  bool imm_op = false;     // third operand is #imm rather than rm
  MemOperand mem;
  std::string symbol;      // MOV32 constant / BL callee
  size_t target = 0;       // resolved branch destination (instruction index)
  // VFP extension: rd/rn/rm hold single-register indices (sN); dN is
  // represented by its even low single (s2N). dbl selects .f64 forms.
  bool dbl = false;
  Vcvt cvt = Vcvt::F32_S32;  // VCVT conversion kind
  int aux = 0;               // VMOV 3-reg: 1 = core->double, 2 = double->core
};

struct TextInst {
  Inst inst;
  int line;                // 1-based source line for diagnostics
};

struct Program {
  std::vector<TextInst> text;
  std::unordered_map<std::string, size_t> labels;       // -> text index
  std::unordered_map<std::string, uint32_t> data_symbols;  // -> memory address
  std::vector<uint8_t> memory;

  // BL targets equal to this denote native runtime calls (libsysy etc.)
  static constexpr size_t kNativeCall = ~(size_t)0;
};

// Parses assembly input. Throws std::runtime_error on malformed input.
Program parse(std::istream& in);

// Executes program starting at its "main" label. Returns the process exit
// code (main() return value masked to 8 bits, like POSIX _exit).
// Writes an execution statistic to stderr when options.stats is set.
struct RunOptions {
  bool stats = false;
  unsigned long long max_steps = 0;  // 0 = unlimited
};
int run(Program& prog, const RunOptions& opts);

// Result of a buffered run: process exit code plus everything the guest
// program wrote to stdout.
struct RunResult {
  int exit_code = 0;
  std::string out;
  unsigned long long step_count = 0;  // dynamic instruction count
  unsigned long long cycle_count = 0;
};

// In-process variant of run(): stdin/stdout are provided/captured via memory
// instead of the console, so embedders (syc-test) can run cases without
// spawning an armsim process. Throws on malformed runtime errors just like
// parse()/run() do.
RunResult run_buffered(Program& prog, const std::string& stdin_data,
                       const RunOptions& opts = {});

// Condition flags (NZCV).
struct Flags {
  bool N = false, Z = false, C = false, V = false;
};

// One interpreter run: registers plus the guest process's I/O and flat
// memory. All per-run state lives here (no file-scope globals), so several
// Machines can execute concurrently in one process.
struct Machine {
  Program* prog;
  uint32_t regs[16] = {};
  Flags flags;
  // VFP: 32 single registers (dN aliases s2N:s2N+1) plus FP compare flags
  uint32_t s[32] = {};
  Flags fp_flags;
  unsigned long long step_count = 0;
  unsigned long long cycle_count = 0;
  // guest stdout accumulated by the native runtime (putint/printf/...)
  std::string stdout_buf;
  // pending guest stdin, consumed by getint/getch/getarray
  std::string stdin_buf;
  size_t stdin_pos = 0;
  // when set, flush_stdout() appends to *out_sink instead of the console
  std::string* out_sink = nullptr;
  // guest flat memory (backs prog->memory)
  uint8_t* mem_base = nullptr;
  size_t mem_size = 0;
};

// Registered native handlers (libsysy replacement). Exposed so tests or the
// compiler driver can hook additional symbols.
using NativeFn = void (*)(Machine&);
const std::unordered_map<std::string, NativeFn>& natives();

}  // namespace armsim
