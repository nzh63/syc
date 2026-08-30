/*
 * armsim -- instruction interpreter plus the SysY runtime library and the
 * libgcc division helpers, implemented natively for Windows.
 * See asm.h for the overall description.
 */
#include "asm.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace armsim {

namespace {

struct ExecError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Process state shared with the native runtime handlers.
// ---------------------------------------------------------------------------
constexpr long long SENTINEL_LR = -1;

bool cond_holds(Cond c, const Flags& f) {
  switch (c) {
    case Cond::EQ: return f.Z;
    case Cond::NE: return !f.Z;
    case Cond::CS: return f.C;
    case Cond::CC: return !f.C;
    case Cond::MI: return f.N;
    case Cond::PL: return !f.N;
    case Cond::VS: return f.V;
    case Cond::VC: return !f.V;
    case Cond::HI: return f.C && !f.Z;
    case Cond::LS: return !f.C || f.Z;
    case Cond::GE: return f.N == f.V;
    case Cond::LT: return f.N != f.V;
    case Cond::GT: return !f.Z && f.N == f.V;
    case Cond::LE: return f.Z || f.N != f.V;
    default: return true;
  }
}

void flush_stdout(Machine& M) {
  if (M.out_sink) {
    *M.out_sink += M.stdout_buf;
    M.stdout_buf.clear();
    return;
  }
  fwrite(M.stdout_buf.data(), 1, M.stdout_buf.size(), stdout);
  M.stdout_buf.clear();
  fflush(stdout);
}

[[noreturn]] void fatal(Machine& M, const std::string& what) {
  flush_stdout(M);
  if (M.out_sink) throw ExecError(what);
  fprintf(stderr, "armsim: %s\n", what.c_str());
  std::exit(126);
}

// Hot path: raw pointer plus a word-bounds check instead of modular
// arithmetic -- this dominates emulator throughput.
inline void mem_write(Machine& M, uint32_t addr, uint32_t value) {
  // little-endian, unaligned allowed
  size_t start = (size_t)addr;
  if (start + 4 > M.mem_size) fatal(M, "memory access out of range");
  memcpy(M.mem_base + start, &value, 4);
}

inline uint32_t mem_read(Machine& M, uint32_t addr) {
  size_t start = (size_t)addr;
  if (start + 4 > M.mem_size) fatal(M, "memory access out of range");
  uint32_t v;
  memcpy(&v, M.mem_base + start, 4);
  return v;
}

// VFP helpers: s/d registers are raw IEEE bit patterns; doubles alias the
// even/odd single pairs (dN = s2N low, s2N+1 high).
inline uint32_t f32_bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
inline float bits_f32(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
inline uint64_t f64_bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
inline double bits_f64(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }
inline double get_d(const Machine& m, int n) {
  return bits_f64((uint64_t)m.s[n] | ((uint64_t)m.s[n + 1] << 32));
}
inline void set_d(Machine& m, int n, double v) {
  uint64_t b = f64_bits(v);
  m.s[n] = (uint32_t)b;
  m.s[n + 1] = (uint32_t)(b >> 32);
}
// FP compare: N = a<b, Z = a==b, C = a>=b (also set when unordered), V = unordered
inline void set_flags_vcmp(Flags& f, bool unordered, double a, double b) {
  if (unordered) {
    f.N = false; f.Z = true; f.C = true; f.V = true;
  } else {
    f.N = a < b; f.Z = a == b; f.C = a >= b; f.V = false;
  }
}

// ---------------------------------------------------------------------------
// Integer input parsing (SysY getint/getch/getarray read plain integers)
// ---------------------------------------------------------------------------
int next_byte(Machine& M) {
  return M.stdin_pos < M.stdin_buf.size()
             ? (unsigned char)M.stdin_buf[M.stdin_pos++]
             : -1;
}

bool read_int(Machine& M, int& out) {
  int c = next_byte(M);
  while (c != -1 && isspace(c)) c = next_byte(M);
  if (c == -1) return false;
  int sign = 1;
  if (c == '-' || c == '+') {
    if (c == '-') sign = -1;
    c = next_byte(M);
  }
  bool any = false;
  long long v = 0;
  while (c >= '0' && c <= '9') {
    v = v * 10 + (c - '0');
    any = true;
    c = next_byte(M);
  }
  if (!any) return false;
  out = (int)(sign * v);
  return true;
}

// ---------------------------------------------------------------------------
// Native implementations: SysY runtime + libgcc helpers
// ---------------------------------------------------------------------------
void rt_getint(Machine& M) {
  int v = 0;
  read_int(M, v);  // EOF leaves 0, matching unchecked usage in the tests
  M.regs[0] = (uint32_t)v;
}

void rt_getch(Machine& M) {
  int c = next_byte(M);
  M.regs[0] =
      (uint32_t)(int32_t)(c == -1 ? -1 : (signed char)c);
}

void rt_getarray(Machine& M) {
  int n = 0;
  read_int(M, n);
  uint32_t a = M.regs[0];
  for (int i = 0; i < n; i++) {
    int v = 0;
    read_int(M, v);
    mem_write(M, (uint32_t)(a + 4u * i), (uint32_t)v);
  }
  M.regs[0] = (uint32_t)n;
}

void append_dec(std::string& s, int v) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", v);
  s += tmp;
}

void rt_putint(Machine& M) {
  append_dec(M.stdout_buf, (int32_t)M.regs[0]);
}

void rt_putch(Machine& M) { M.stdout_buf.push_back((char)(M.regs[0] & 0xFF)); }

void rt_putarray(Machine& M) {
  int n = (int32_t)M.regs[0];
  uint32_t a = M.regs[1];
  append_dec(M.stdout_buf, n);
  M.stdout_buf.push_back(':');
  for (int i = 0; i < n; i++) {
    M.stdout_buf.push_back(' ');
    append_dec(M.stdout_buf, (int32_t)mem_read(M, (uint32_t)(a + 4u * i)));
  }
  M.stdout_buf.push_back('\n');
}

void rt_starttime(Machine& M) {}
void rt_stoptime(Machine& M) {}

// Minimal printf-style formatter: supports %d, %c and %s with the first
// variadic argument in r1, then r2/r3, then the stack.
void emit_format(Machine& M, uint32_t fmt) {
  int arg_idx = 0;
  auto next_arg = [&]() -> int32_t {
    if (arg_idx < 3) return (int32_t)M.regs[1 + arg_idx++];
    // extra arguments live at [sp], [sp,#4]... (AAPCS)
    int32_t v = (int32_t)mem_read(M, M.regs[13] + 4u * (arg_idx - 3));
    arg_idx++;
    return v;
  };
  // 64-bit variadic argument (double bit pattern): two consecutive slots,
  // possibly straddling r3 and the stack under the soft-float convention
  auto next_arg64 = [&]() -> uint64_t {
    auto slot = [&](int k) -> uint32_t {
      if (k < 3) return M.regs[1 + k];
      return mem_read(M, M.regs[13] + 4u * (k - 3));
    };
    uint64_t v = slot(arg_idx) | ((uint64_t)slot(arg_idx + 1) << 32);
    arg_idx += 2;
    return v;
  };
  const size_t size = M.prog->memory.size();
  while (true) {
    uint8_t c = M.prog->memory[fmt++ % size];
    if (c == 0) break;
    if (c != '%') {
      M.stdout_buf.push_back((char)c);
      continue;
    }
    // 通用规格解析：%[flags][width][.precision]conv（变长规格重建后
    // 交由宿主 snprintf 输出，保证与本地 C 运行时格式一致）
    char spec[32];
    spec[0] = '%';
    size_t spec_len = 1;
    while (spec_len + 1 < sizeof(spec)) {
      uint8_t c = M.prog->memory[fmt % size];
      if (c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' ||
          (c >= '0' && c <= '9') || c == '.') {
        spec[spec_len++] = (char)c;
        fmt++;
      } else {
        break;
      }
    }
    uint8_t t = M.prog->memory[fmt++ % size];
    char tmp[512];
    switch (t) {
      case 'd': case 'i': {
        spec[spec_len++] = 'd';
        spec[spec_len] = 0;
        snprintf(tmp, sizeof(tmp), spec, next_arg());
        M.stdout_buf += tmp;
        break;
      }
      case 'u': case 'x': case 'X': case 'o': {
        spec[spec_len++] = (char)t;
        spec[spec_len] = 0;
        snprintf(tmp, sizeof(tmp), spec, (uint32_t)next_arg());
        M.stdout_buf += tmp;
        break;
      }
      case 'f': case 'e': case 'g': case 'E': case 'G': {
        spec[spec_len++] = (char)t;
        spec[spec_len] = 0;
        snprintf(tmp, sizeof(tmp), spec, bits_f64(next_arg64()));
        M.stdout_buf += tmp;
        break;
      }
      case 'l': {
        // only %lf (same as %f here) appears in this dialect
        uint8_t t2 = M.prog->memory[fmt++ % size];
        if (t2 == 'f' || t2 == 'e' || t2 == 'g') {
          spec[spec_len++] = (char)t2;
          spec[spec_len] = 0;
          snprintf(tmp, sizeof(tmp), spec, bits_f64(next_arg64()));
          M.stdout_buf += tmp;
        } else {
          M.stdout_buf += "%l";
          M.stdout_buf.push_back((char)t2);
        }
        break;
      }
      case 'c':
        spec[spec_len++] = 'c';
        spec[spec_len] = 0;
        snprintf(tmp, sizeof(tmp), spec, (int)(next_arg() & 0xFF));
        M.stdout_buf += tmp;
        break;
      case 's': {
        spec[spec_len++] = 's';
        spec[spec_len] = 0;
        uint32_t p = (uint32_t)next_arg();
        // flat memory 上的字符串按宿主 %s 无法直接读取，读入本地缓冲
        char strbuf[256];
        size_t n = 0;
        while (n + 1 < sizeof(strbuf)) {
          uint8_t sc = M.prog->memory[p++ % size];
          if (sc == 0) break;
          strbuf[n++] = (char)sc;
        }
        strbuf[n] = 0;
        snprintf(tmp, sizeof(tmp), spec, strbuf);
        M.stdout_buf += tmp;
        break;
      }
      default:
        M.stdout_buf.push_back('%');
        M.stdout_buf.push_back((char)t);
    }
  }
}

void rt_putf(Machine& M) { emit_format(M, M.regs[0]); }
void rt_printf(Machine& M) { emit_format(M, M.regs[0]); }

void rt_memset(Machine& M) {
  uint32_t dst = M.regs[0];
  int32_t value = (int32_t)M.regs[1];
  uint32_t n = M.regs[2];
  const size_t size = M.prog->memory.size();
  for (uint32_t i = 0; i < n; i++)
    (*M.prog).memory[(dst + i) % size] = (uint8_t)value;
}

void rt_memcpy(Machine& M) {
  uint32_t dst = M.regs[0], src = M.regs[1],
           n = M.regs[2];
  const size_t size = M.prog->memory.size();
  for (uint32_t i = 0; i < n; i++)
    (*M.prog).memory[(dst + i) % size] =
        M.prog->memory[(src + i) % size];
}

void rt_aeabi_idiv(Machine& M) {
  int a = (int32_t)M.regs[0], b = (int32_t)M.regs[1];
  if (b == 0) fatal(M, "division by zero (__aeabi_idiv)");
  M.regs[0] = (uint32_t)(a / b);
}

void rt_aeabi_idivmod(Machine& M) {
  int a = (int32_t)M.regs[0], b = (int32_t)M.regs[1];
  if (b == 0) fatal(M, "division by zero (__aeabi_idivmod)");
  M.regs[0] = (uint32_t)(a / b);
  M.regs[1] = (uint32_t)(a % b);
}

void rt_aeabi_uidiv(Machine& M) {
  uint32_t a = M.regs[0], b = M.regs[1];
  if (b == 0) fatal(M, "division by zero (__aeabi_uidiv)");
  M.regs[0] = a / b;
}

void rt_aeabi_uidivmod(Machine& M) {
  uint32_t a = M.regs[0], b = M.regs[1];
  if (b == 0) fatal(M, "division by zero (__aeabi_uidivmod)");
  M.regs[0] = a / b;
  M.regs[1] = a % b;
}

// 64 位寄存器对约定：低 32 位在偶序号（较小）寄存器，高位紧随其后
inline int64_t reg_pair64(uint32_t lo, uint32_t hi) {
  return (int64_t)((uint64_t)lo | ((uint64_t)hi << 32));
}
inline void set_reg_pair64(uint32_t& lo, uint32_t& hi, int64_t v) {
  lo = (uint32_t)(uint64_t)v;
  hi = (uint32_t)((uint64_t)v >> 32);
}

void rt_aeabi_lmul(Machine& M) {
  int64_t a = reg_pair64(M.regs[0], M.regs[1]);
  int64_t b = reg_pair64(M.regs[2], M.regs[3]);
  set_reg_pair64(M.regs[0], M.regs[1], a * b);
}

void rt_aeabi_ldivmod(Machine& M) {
  int64_t a = reg_pair64(M.regs[0], M.regs[1]);
  int64_t b = reg_pair64(M.regs[2], M.regs[3]);
  if (b == 0) fatal(M, "division by zero (__aeabi_ldivmod)");
  int64_t q, r;
  if (b == -1) {
    // 避免 INT64_MIN / -1 的有符号溢出 UB
    q = (a == INT64_MIN) ? INT64_MIN : -a;
    r = 0;
  } else {
    q = a / b;
    r = a % b;
  }
  set_reg_pair64(M.regs[0], M.regs[1], q);
  set_reg_pair64(M.regs[2], M.regs[3], r);
}

void rt_aeabi_uldivmod(Machine& M) {
  uint64_t a = ((uint64_t)M.regs[0] |
                ((uint64_t)M.regs[1] << 32));
  uint64_t b = ((uint64_t)M.regs[2] |
                ((uint64_t)M.regs[3] << 32));
  if (b == 0) fatal(M, "division by zero (__aeabi_uldivmod)");
  uint64_t q = a / b, r = a % b;
  M.regs[0] = (uint32_t)q;
  M.regs[1] = (uint32_t)(q >> 32);
  M.regs[2] = (uint32_t)r;
  M.regs[3] = (uint32_t)(r >> 32);
}

const std::unordered_map<std::string, NativeFn>& native_table() {
  static const std::unordered_map<std::string, NativeFn> table = {
      {"getint", rt_getint},
      {"getch", rt_getch},
      {"getchar", rt_getch},
      {"getarray", rt_getarray},
      {"putint", rt_putint},
      {"putch", rt_putch},
      {"putchar", rt_putch},
      {"putarray", rt_putarray},
      {"starttime", rt_starttime},
      {"stoptime", rt_stoptime},
      {"_sysy_starttime", rt_starttime},
      {"_sysy_stoptime", rt_stoptime},
      {"__aeabi_idiv", rt_aeabi_idiv},
      {"__aeabi_idivmod", rt_aeabi_idivmod},
      {"__aeabi_uidiv", rt_aeabi_uidiv},
      {"__aeabi_uidivmod", rt_aeabi_uidivmod},
      {"__aeabi_lmul", rt_aeabi_lmul},
      {"__aeabi_ldivmod", rt_aeabi_ldivmod},
      {"__aeabi_uldivmod", rt_aeabi_uldivmod},
      {"memset", rt_memset},
      {"memcpy", rt_memcpy},
      {"putf", rt_putf},
      {"printf", rt_printf},
  };
  return table;
}

void set_flags_logic(Flags& f, uint32_t v) {
  f.N = (v >> 31) != 0;
  f.Z = v == 0;
}

void set_flags_sub(Flags& f, uint32_t a, uint32_t b, uint32_t r) {
  f.N = (r >> 31) != 0;
  f.Z = r == 0;
  f.C = a >= b;
  f.V = ((((a ^ b) & (a ^ r)) >> 31) & 1) != 0;
}

// Estimated cycle cost per instruction, calibrated on Cortex-A9 class
// ARMv7-A hardware (DDI0388 Appendix B; measured tables on
// hardwarebug.org): 1-cycle ALU, 4-stage multiplier (MUL/SMULL), 3-cycle
// load-to-use latency, and the VFP latencies (VDIV being the extreme).
// A condition-false instruction still costs one fetch/dispatch cycle.
unsigned inst_cycles(const Inst& I) {
  // MOV32 is a pseudo-instruction: on real hardware it expands to
  // MOVW+MOVT (or a literal-pool LDR), so it costs two cycles.
  switch (I.op) {
    case Op::MOV32: return 2;
    case Op::MUL: return 5;
    case Op::SMULL: return 6;
    case Op::LDR:
      return 3;
    case Op::VMOV: return 4;
    case Op::VADD:
    case Op::VSUB:
    case Op::VMUL:
    case Op::VCMP:
    case Op::VCVT: return I.dbl ? 7 : 4;
    case Op::VDIV: return I.dbl ? 32 : 18;
    case Op::VLDR: return 3;
    case Op::VSTR: return 2;
    case Op::B:
    case Op::BL: return 2;
    default: return 1;
  }
}

}  // namespace

const std::unordered_map<std::string, NativeFn>& natives() {
  return native_table();
}

static int run_impl(Machine& M, const RunOptions& opts) {
  Program& prog = *M.prog;

  auto entry = prog.labels.find("main");
  if (entry == prog.labels.end()) {
    throw ExecError("program has no 'main' label");
  }

  M.regs[13] = (uint32_t)(prog.memory.size() - 64);
  M.regs[14] = (uint32_t)(int32_t)SENTINEL_LR;

  long long pc = (long long)entry->second;
  const auto& text = prog.text;
  auto start_time = std::chrono::steady_clock::now();

  auto rd_reg = [&](int i) -> uint32_t { return M.regs[i & 15]; };
  auto wr_reg = [&](int i, uint32_t v) { M.regs[i & 15] = v; };

  try {
    while (true) {
      if (pc < 0) {
        // returned through the sentinel LR: process exit
        break;
      }
      if ((size_t)pc >= text.size())
        throw ExecError("pc out of range (" + std::to_string(pc) + ")");
      ++M.step_count;
      if (opts.max_steps && M.step_count > opts.max_steps)
        throw ExecError("step limit exceeded (" +
                        std::to_string(opts.max_steps) + ")");

      const Inst& I = text[(size_t)pc].inst;
      M.cycle_count += inst_cycles(I);
      long long next_pc = pc + 1;
      static const bool trace_enabled = getenv("ARMSIM_TRACE") != nullptr;
      if (trace_enabled)
        fprintf(stderr,
                "[exec pc=%lld op=%d cond=%d imm_op=%d sym=%s] r0=%u r1=%08x r2=%08x "
                "r13=%u r14=%u NZCV=%d%d%d%d\n",
                pc, (int)I.op, (int)I.cond, (int)I.imm_op, I.symbol.c_str(),
                M.regs[0], M.regs[1], M.regs[2], M.regs[13], M.regs[14],
                (int)M.flags.N,
                (int)M.flags.Z, (int)M.flags.C, (int)M.flags.V);
      if (!cond_holds(I.cond, M.flags)) {
        pc = next_pc;
        continue;
      }

      switch (I.op) {
        case Op::NOP:
          break;
        case Op::MOV:
        case Op::MOV32: {
          uint32_t v = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          if (I.rd == 15) {
            next_pc = (long long)(int32_t)v;
          } else {
            wr_reg(I.rd, v);
          }
          break;
        }
        case Op::ADD: {
          uint32_t a = rd_reg(I.rn),
                   b = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          wr_reg(I.rd, a + b);
          break;
        }
        case Op::SUB: {
          uint32_t a = rd_reg(I.rn),
                   b = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          wr_reg(I.rd, a - b);
          break;
        }
        case Op::MUL: {
          wr_reg(I.rd, rd_reg(I.rn) * rd_reg(I.rm));
          break;
        }
        case Op::SMULL: {
          int64_t p = (int64_t)(int32_t)rd_reg(I.rn) *
                      (int64_t)(int32_t)rd_reg(I.rm);
          wr_reg(I.rd, (uint32_t)(p & 0xFFFFFFFFll));
          wr_reg(I.ra, (uint32_t)((uint64_t)p >> 32));
          break;
        }
        case Op::LSL: {
          wr_reg(I.rd, rd_reg(I.rn) << ((unsigned)I.imm & 31u));
          break;
        }
        case Op::ASR: {
          wr_reg(I.rd,
                 (uint32_t)((int32_t)rd_reg(I.rn) >> ((unsigned)I.imm & 31u)));
          break;
        }
        case Op::CMP: {
          uint32_t a = rd_reg(I.rn),
                   b = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          set_flags_sub(M.flags, a, b, a - b);
          break;
        }
        case Op::TST: {
          uint32_t v = rd_reg(I.rn) & rd_reg(I.rm);
          set_flags_logic(M.flags, v);
          break;
        }
        case Op::AND: {
          uint32_t b = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          wr_reg(I.rd, rd_reg(I.rn) & b);
          break;
        }
        case Op::ORRS: {
          uint32_t v = rd_reg(I.rn) | rd_reg(I.rm);
          wr_reg(I.rd, v);
          set_flags_logic(M.flags, v);
          break;
        }
        case Op::SUBS: {
          uint32_t a = rd_reg(I.rn),
                   b = I.imm_op ? (uint32_t)(int32_t)I.imm : rd_reg(I.rm);
          uint32_t r = a - b;
          wr_reg(I.rd, r);
          set_flags_sub(M.flags, a, b, r);
          break;
        }
        case Op::LDR: {
          uint32_t addr =
              rd_reg(I.mem.base) +
              (I.mem.off_reg >= 0 ? rd_reg(I.mem.off_reg)
                                  : (uint32_t)(int32_t)I.mem.off_imm);
          wr_reg(I.rd, mem_read(M, addr));
          break;
        }
        case Op::STR: {
          uint32_t addr =
              rd_reg(I.mem.base) +
              (I.mem.off_reg >= 0 ? rd_reg(I.mem.off_reg)
                                  : (uint32_t)(int32_t)I.mem.off_imm);
          mem_write(M, addr, rd_reg(I.rd));
          break;
        }
        case Op::B:
          next_pc = (long long)I.target;
          break;
        case Op::BL:
          M.regs[14] = (uint32_t)(int32_t)(pc + 1);
          if (I.target == Program::kNativeCall) {
            // native runtime call: lr is preserved across the handler
            (*natives().at(I.symbol))(M);
          } else {
            next_pc = (long long)I.target;
          }
          break;
        case Op::VMOV: {
          // aux: 0 = fp -> fp, 1 = core -> fp, 2 = fp -> core;
          // dbl 3-reg forms: 1 = two core -> double, 2 = double -> two core
          if (I.dbl && I.aux == 1) {
            M.s[I.rd] = rd_reg(I.rn);
            M.s[I.rd + 1] = rd_reg(I.rm);
          } else if (I.dbl && I.aux == 2) {
            wr_reg(I.rd, M.s[I.rm]);
            wr_reg(I.rn, M.s[I.rm + 1]);
          } else if (I.aux == 1) {
            M.s[I.rd] = rd_reg(I.rm);
          } else if (I.aux == 2) {
            wr_reg(I.rd, M.s[I.rm]);
          } else if (I.dbl) {
            set_d(M, I.rd, get_d(M, I.rm));
          } else {
            M.s[I.rd] = M.s[I.rm];
          }
          break;
        }
        case Op::VADD:
        case Op::VSUB:
        case Op::VMUL:
        case Op::VDIV: {
          if (I.dbl) {
            double a = get_d(M, I.rn), b = get_d(M, I.rm), r;
            switch (I.op) {
              case Op::VADD: r = a + b; break;
              case Op::VSUB: r = a - b; break;
              case Op::VMUL: r = a * b; break;
              default: r = a / b; break;
            }
            set_d(M, I.rd, r);
          } else {
            float a = bits_f32(M.s[I.rn]), b = bits_f32(M.s[I.rm]), r;
            switch (I.op) {
              case Op::VADD: r = a + b; break;
              case Op::VSUB: r = a - b; break;
              case Op::VMUL: r = a * b; break;
              default: r = a / b; break;
            }
            M.s[I.rd] = f32_bits(r);
          }
          break;
        }
        case Op::VNEG: {
          if (I.dbl) set_d(M, I.rd, -get_d(M, I.rn));
          else M.s[I.rd] = f32_bits(-bits_f32(M.s[I.rn]));
          break;
        }
        case Op::VABS: {
          if (I.dbl) set_d(M, I.rd, std::fabs(get_d(M, I.rn)));
          else M.s[I.rd] = f32_bits(std::fabs(bits_f32(M.s[I.rn])));
          break;
        }
        case Op::VCMP: {
          if (I.dbl) {
            double a = get_d(M, I.rn);
            double b = I.imm_op ? 0.0 : get_d(M, I.rm);
            set_flags_vcmp(M.fp_flags, !(a == b) && !(a < b) && !(a > b), a, b);
          } else {
            float a = bits_f32(M.s[I.rn]);
            float b = I.imm_op ? 0.0f : bits_f32(M.s[I.rm]);
            bool un = !(a == b) && !(a < b) && !(a > b);
            set_flags_vcmp(M.fp_flags, un, (double)a, (double)b);
          }
          break;
        }
        case Op::VMRS: {
          M.flags = M.fp_flags;
          break;
        }
        case Op::VCVT: {
          // all VCVT operands are VFP registers; integer values are held
          // as raw bit patterns in the single registers
          switch (I.cvt) {
            case Vcvt::F32_S32:
              M.s[I.rd] = f32_bits((float)(int32_t)M.s[I.rn]);
              break;
            case Vcvt::F64_S32:
              set_d(M, I.rd, (double)(int32_t)M.s[I.rn]);
              break;
            case Vcvt::F32_U32:
              M.s[I.rd] = f32_bits((float)M.s[I.rn]);
              break;
            case Vcvt::F64_U32:
              set_d(M, I.rd, (double)M.s[I.rn]);
              break;
            case Vcvt::S32_F32:
              M.s[I.rd] = (uint32_t)(int32_t)bits_f32(M.s[I.rn]);
              break;
            case Vcvt::U32_F32:
              M.s[I.rd] = (uint32_t)bits_f32(M.s[I.rn]);
              break;
            case Vcvt::S32_F64:
              M.s[I.rd] = (uint32_t)(int32_t)get_d(M, I.rn);
              break;
            case Vcvt::U32_F64:
              M.s[I.rd] = (uint32_t)get_d(M, I.rn);
              break;
            case Vcvt::F64_F32:
              set_d(M, I.rd, (double)bits_f32(M.s[I.rn]));
              break;
            case Vcvt::F32_F64:
              M.s[I.rd] = f32_bits((float)get_d(M, I.rn));
              break;
          }
          break;
        }
        case Op::VLDR:
        case Op::VSTR: {
          uint32_t addr =
              rd_reg(I.mem.base) +
              (I.mem.off_reg >= 0 ? rd_reg(I.mem.off_reg)
                                  : (uint32_t)(int32_t)I.mem.off_imm);
          if (I.op == Op::VLDR) {
            M.s[I.rd] = mem_read(M, addr);
            if (I.dbl) M.s[I.rd + 1] = mem_read(M, addr + 4);
          } else {
            mem_write(M, addr, M.s[I.rd]);
            if (I.dbl) mem_write(M, addr + 4, M.s[I.rd + 1]);
          }
          break;
        }
        default:
          throw ExecError("unhandled opcode " + std::to_string((int)I.op));
      }
      pc = next_pc;
    }
  } catch (const std::exception& e) {
    flush_stdout(M);
    int line = (pc >= 0 && (size_t)pc < text.size()) ? text[(size_t)pc].line
                                                     : -1;
    std::string where =
        line > 0 ? "near assembly line " + std::to_string(line) + ": "
                 : "";
    throw ExecError(where + e.what());
  }

  flush_stdout(M);

  if (opts.stats) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start_time)
                  .count();
    std::cerr << "[armsim] executed " << M.step_count << " instructions in "
              << ms << " ms (~" << M.cycle_count << " cycles)" << std::endl;
  }
  return (int)(M.regs[0] & 0xFF);
}

int run(Program& prog, const RunOptions& opts) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  Machine M{&prog};
  M.mem_base = prog.memory.data();
  M.mem_size = prog.memory.size();
  {
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), stdin)) > 0)
      M.stdin_buf.append(buf, got);
  }
  return run_impl(M, opts);
}

RunResult run_buffered(Program& prog, const std::string& stdin_data,
                       const RunOptions& opts) {
  RunResult res;
  Machine M{&prog};
  M.mem_base = prog.memory.data();
  M.mem_size = prog.memory.size();
  M.stdin_buf = stdin_data;
  M.out_sink = &res.out;
  res.exit_code = run_impl(M, opts);
  res.step_count = M.step_count;
  res.cycle_count = M.cycle_count;
  return res;
}

}  // namespace armsim
