/*
 * armsim -- assembly parser and program layout.
 * See asm.h for the overall description.
 */
#include "asm.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <sstream>
#include <stdexcept>

namespace armsim {
namespace {

struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

[[noreturn]] void err(int line, const std::string& what) {
  throw ParseError("line " + std::to_string(line) + ": " + what);
}

std::string lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

// "r12", "sp", "lr", "pc", case-insensitive -> register number, or -1.
int parse_reg(const std::string& tok) {
  if (tok.empty()) return -1;
  std::string r = lower(tok);
  if (r == "sp") return 13;
  if (r == "lr") return 14;
  if (r == "pc") return 15;
  if (r[0] == 'r' && r.size() >= 2) {
    char* end = nullptr;
    long n = strtol(r.c_str() + 1, &end, 10);
    if (end && *end == '\0' && n >= 0 && n < 16) return (int)n;
  }
  return -1;
}

// VFP register: "s12" -> single index 12; "d5" -> its even low single (10).
// dbl_out reports whether a d-register was named.
int parse_freg(const std::string& tok, bool& dbl_out) {
  dbl_out = false;
  if (tok.empty()) return -1;
  std::string r = lower(tok);
  char* end = nullptr;
  if (r[0] == 's' && r.size() >= 2) {
    long n = strtol(r.c_str() + 1, &end, 10);
    if (end && *end == '\0' && n >= 0 && n < 32) return (int)n;
  }
  if (r[0] == 'd' && r.size() >= 2) {
    long n = strtol(r.c_str() + 1, &end, 10);
    if (end && *end == '\0' && n >= 0 && n < 16) {
      dbl_out = true;
      return (int)(2 * n);
    }
  }
  return -1;
}

// "#0.0"-style float immediate (VCMP only accepts #0.0 in this dialect).
bool parse_fimm0(const std::string& tok) {
  std::string r = lower(tok);
  if (!r.empty() && r[0] == '#') r = r.substr(1);
  if (r == "0.0" || r == "0" || r == "0.") return true;
  return false;
}

bool parse_imm(const std::string& tok, long long& out) {
  size_t i = 0;
  if (i < tok.size() && tok[i] == '#') i++;
  if (i >= tok.size()) return false;
  char* end = nullptr;
  // strtoll cannot be pointed at const char* offset on MSVC safely? It can.
  long long v = strtoll(tok.c_str() + i, &end, 0);
  if (end && *end == '\0') {
    out = v;
    return true;
  }
  return false;
}

// Splits a line into whitespace-separated tokens; brackets may contain
// spaces, so re-join tokens while a '[' is unclosed.
std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> toks;
  std::istringstream ss(line);
  std::string t;
  while (ss >> t) {
    bool bracketed = false;
    for (char c : t) {
      if (c == '[') bracketed = true;
      else if (c == ']') bracketed = false;
    }
    while (bracketed && (ss >> t)) {
      for (char c : t) {
        if (c == ']') bracketed = false;
      }
    }
    toks.push_back(t);
  }
  return toks;
}

MemOperand parse_mem(const std::string& tok, int line) {
  MemOperand m;
  auto open = tok.find('[');
  auto close = tok.rfind(']');
  if (open == std::string::npos || close == std::string::npos || close < open)
    err(line, "bad memory operand: " + tok);
  std::string inner = tok.substr(open + 1, close - open - 1);
  // split inner by ','
  std::vector<std::string> parts;
  size_t pos = 0;
  while (true) {
    size_t comma = inner.find(',', pos);
    std::string part =
        inner.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    // strip spaces
    size_t a = part.find_first_not_of(" \t");
    size_t b = part.find_last_not_of(" \t");
    if (a != std::string::npos) parts.push_back(part.substr(a, b - a + 1));
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  if (parts.empty()) err(line, "empty memory operand");
  m.base = parse_reg(parts[0]);
  if (m.base < 0) err(line, "bad base register: " + parts[0]);
  if (parts.size() == 2) {
    long long imm;
    int reg = parse_reg(parts[1]);
    if (reg >= 0) {
      m.off_reg = reg;
    } else if (parse_imm(parts[1], imm)) {
      m.off_imm = imm;
    } else {
      err(line, "bad memory offset: " + parts[1]);
    }
  } else if (parts.size() > 2) {
    err(line, "too many memory operands: " + tok);
  }
  m.valid = true;
  return m;
}

Cond cond_from_suffix(const std::string& suffix_lower) {
  // the parsed mnemonic is lowercased; the table is keyed by upper-case
  std::string s = suffix_lower;
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  static const std::unordered_map<std::string, Cond> kConds = {
      {"EQ", Cond::EQ}, {"NE", Cond::NE}, {"CS", Cond::CS}, {"HS", Cond::CS},
      {"CC", Cond::CC}, {"LO", Cond::CC}, {"MI", Cond::MI}, {"PL", Cond::PL},
      {"VS", Cond::VS}, {"VC", Cond::VC}, {"HI", Cond::HI}, {"LS", Cond::LS},
      {"GE", Cond::GE}, {"LT", Cond::LT}, {"GT", Cond::GT}, {"LE", Cond::LE},
      {"AL", Cond::AL},
  };
  auto it = kConds.find(s);
  return it == kConds.end() ? Cond::AL : it->second;
}

// Parses one instruction (already tokenized). Returns the Inst.
Inst parse_inst(const std::vector<std::string>& toks, int line) {
  Inst in;
  std::string mn = lower(toks[0]);

  // Peel a condition suffix off the mnemonic (BEQ, MOVLT, SUBS, ...).
  std::string stem = mn;
  if (mn.size() > 2) {
    Cond c = cond_from_suffix(mn.substr(mn.size() - 2));
    if (c != Cond::AL && mn != "smull") {
      in.cond = c;
      stem = mn.substr(0, mn.size() - 2);
    }
  }
  if (!stem.empty() && stem.back() == 's' && stem != "subs" &&
      stem.substr(0, 3) == "mov") {
    // e.g. MOVS -- not emitted by syc; treat S flag generically below.
  }

  auto operand_strs = [&](size_t from) {
    std::vector<std::string> ops(toks.begin() + from, toks.end());
    // drop trailing commas
    for (auto& o : ops)
      if (!o.empty() && o.back() == ',') o.pop_back();
    return ops;
  };

  if (stem == "nop") {
    in.op = Op::NOP;
  } else if (stem == "mov" || stem == "mov32") {
    in.op = stem == "mov" ? Op::MOV : Op::MOV32;
    auto ops = operand_strs(1);
    if (ops.size() < 2) err(line, "MOV needs two operands");
    in.rd = parse_reg(ops[0]);
    if (in.rd < 0) err(line, "bad dest register: " + ops[0]);
    long long imm;
    int src = parse_reg(ops[1]);
    if (stem == "mov32") {
      if (!parse_imm(ops[1], imm)) {
        // symbolic constant, resolved after layout
        in.symbol = ops[1];
      } else {
        in.imm = imm;
        in.imm_op = true;
      }
    } else if (src >= 0) {
      in.rm = src;
      in.imm_op = false;
    } else if (parse_imm(ops[1], imm)) {
      in.imm = imm;
      in.imm_op = true;
    } else {
      err(line, "bad MOV source: " + ops[1]);
    }
  } else if (stem == "subs") {
    // like SUB but sets flags (used by the O2 division sequences)
    in.op = Op::SUBS;
    in.S = true;
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, "SUBS needs three operands");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    if (in.rd < 0 || in.rn < 0) err(line, "bad SUBS operands");
    int rm = parse_reg(ops[2]);
    if (rm >= 0) {
      in.rm = rm;
    } else if (parse_imm(ops[2], in.imm)) {
      in.imm_op = true;
    } else {
      err(line, "bad SUBS last operand: " + ops[2]);
    }
  } else if (stem == "add" || stem == "sub") {
    in.op = stem == "add" ? Op::ADD : Op::SUB;
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, "ADD/SUB needs three operands");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    if (in.rd < 0 || in.rn < 0) err(line, "bad ADD/SUB operands");
    int rm = parse_reg(ops[2]);
    if (rm >= 0) {
      in.rm = rm;
    } else if (parse_imm(ops[2], in.imm)) {
      in.imm_op = true;
    } else {
      err(line, "bad ADD/SUB last operand: " + ops[2]);
    }
  } else if (stem == "mul") {
    in.op = Op::MUL;
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, "MUL needs three operands");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    in.rm = parse_reg(ops[2]);
    if (in.rd < 0 || in.rn < 0 || in.rm < 0) err(line, "bad MUL operands");
  } else if (stem == "smull") {
    in.op = Op::SMULL;
    auto ops = operand_strs(1);
    if (ops.size() != 4) err(line, "SMULL needs four operands");
    in.rd = parse_reg(ops[0]);
    in.ra = parse_reg(ops[1]);
    in.rn = parse_reg(ops[2]);
    in.rm = parse_reg(ops[3]);
    if (in.rd < 0 || in.ra < 0 || in.rn < 0 || in.rm < 0)
      err(line, "bad SMULL operands");
  } else if (stem == "lsl" || stem == "asr") {
    in.op = stem == "lsl" ? Op::LSL : Op::ASR;
    auto ops = operand_strs(1);
    if (ops.size() != 3 || !parse_imm(ops[2], in.imm))
      err(line, "LSL/ASR expects register, register, #imm");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    if (in.rd < 0 || in.rn < 0) err(line, "bad LSL/ASR operands");
  } else if (stem == "cmp" || stem == "tst") {
    in.op = stem == "cmp" ? Op::CMP : Op::TST;
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, "CMP/TST needs two operands");
    in.rn = parse_reg(ops[0]);
    if (in.rn < 0) err(line, "bad CMP/TST register");
    int rm = parse_reg(ops[1]);
    if (rm >= 0) {
      in.rm = rm;
    } else if (parse_imm(ops[1], in.imm)) {
      in.imm_op = true;
    } else {
      err(line, "bad CMP/TST second operand: " + ops[1]);
    }
  } else if (stem == "orr") {
    in.op = Op::ORRS;
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, "ORR needs three operands");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    in.rm = parse_reg(ops[2]);
    if (in.rd < 0 || in.rn < 0 || in.rm < 0) err(line, "bad ORR operands");
  } else if (stem == "and") {
    in.op = Op::AND;
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, "AND needs three operands");
    in.rd = parse_reg(ops[0]);
    in.rn = parse_reg(ops[1]);
    if (in.rd < 0 || in.rn < 0) err(line, "bad AND operands");
    int rm = parse_reg(ops[2]);
    if (rm >= 0) {
      in.rm = rm;
    } else if (parse_imm(ops[2], in.imm)) {
      in.imm_op = true;
    } else {
      err(line, "bad AND third operand: " + ops[2]);
    }
  } else if (stem == "ldr" || stem == "str") {
    in.op = stem == "ldr" ? Op::LDR : Op::STR;
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, "LDR/STR needs two operands");
    in.rd = parse_reg(ops[0]);
    if (in.rd < 0) err(line, "bad LDR/STR register: " + ops[0]);
    in.mem = parse_mem(ops[1], line);
  } else if (stem == "b") {
    in.op = Op::B;
    auto ops = operand_strs(1);
    if (ops.size() != 1) err(line, "B needs one label");
    in.symbol = ops[0];
  } else if (stem == "vmrs") {
    // VMRS [APSR_nzcv,] FPSCR: copy FP N/Z/C/V into CPSR (no operand form).
    in.op = Op::VMRS;
  } else if (stem.rfind("vadd", 0) == 0 || stem.rfind("vsub", 0) == 0 ||
             stem.rfind("vmul", 0) == 0 || stem.rfind("vdiv", 0) == 0) {
    auto ops = operand_strs(1);
    if (ops.size() != 3) err(line, toks[0] + " needs three operands");
    bool d1 = false, d2 = false, d3 = false;
    in.rd = parse_freg(ops[0], d1);
    in.rn = parse_freg(ops[1], d2);
    in.rm = parse_freg(ops[2], d3);
    if (in.rd < 0 || in.rn < 0 || in.rm < 0) err(line, "bad VFP operands");
    if (d1 != d2 || d1 != d3) err(line, "mixed s/d operands: " + toks[0]);
    in.dbl = d1;
    if (stem.rfind("vadd", 0) == 0) in.op = Op::VADD;
    else if (stem.rfind("vsub", 0) == 0) in.op = Op::VSUB;
    else if (stem.rfind("vmul", 0) == 0) in.op = Op::VMUL;
    else in.op = Op::VDIV;
  } else if (stem.rfind("vneg", 0) == 0 || stem.rfind("vabs", 0) == 0) {
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, toks[0] + " needs two operands");
    bool d1 = false, d2 = false;
    in.rd = parse_freg(ops[0], d1);
    in.rn = parse_freg(ops[1], d2);
    if (in.rd < 0 || in.rn < 0 || d1 != d2) err(line, "bad VNEG/VABS operands");
    in.dbl = d1;
    in.op = stem.rfind("vneg", 0) == 0 ? Op::VNEG : Op::VABS;
  } else if (stem.rfind("vcmp", 0) == 0 || stem.rfind("vcmpe", 0) == 0) {
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, "VCMP needs two operands");
    bool d1 = false, d2 = false;
    in.rn = parse_freg(ops[0], d1);
    in.dbl = d1;
    in.rm = parse_freg(ops[1], d2);
    if (in.rn < 0) err(line, "bad VCMP operand: " + ops[0]);
    if (in.rm >= 0 && d2 != d1) err(line, "mixed s/d VCMP operands");
    if (in.rm < 0) {
      if (!parse_fimm0(ops[1])) err(line, "bad VCMP operand: " + ops[1]);
      in.imm_op = true;  // only #0.0 supported; imm left 0 (= +0.0 bits)
    }
    in.op = Op::VCMP;
  } else if (stem.rfind("vcvt", 0) == 0) {
    // vcvt.<dst>.<src> dD|sD, sN|dN
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, "VCVT needs two operands");
    std::string kind = stem.size() > 4 ? stem.substr(5) : "";
    bool d1 = false, d2 = false;
    in.rd = parse_freg(ops[0], d1);
    in.rn = parse_freg(ops[1], d2);
    if (in.rd < 0 || in.rn < 0) err(line, "bad VCVT operands");
    if (kind == "f64.f32") {
      in.cvt = Vcvt::F64_F32; in.dbl = true;
      if (d1 != true || d2 != false) err(line, "bad VCVT.F64.F32 operands");
    } else if (kind == "f32.f64") {
      in.cvt = Vcvt::F32_F64; in.dbl = false;
      if (d1 != false || d2 != true) err(line, "bad VCVT.F32.F64 operands");
    } else if (kind == "s32.f32") {
      in.cvt = Vcvt::S32_F32;
      if (d1 || d2) err(line, "bad VCVT.S32.F32 operands");
    } else if (kind == "u32.f32") {
      in.cvt = Vcvt::U32_F32;
      if (d1 || d2) err(line, "bad VCVT.U32.F32 operands");
    } else if (kind == "s32.f64") {
      in.cvt = Vcvt::S32_F64;
      if (d1 || !d2) err(line, "bad VCVT.S32.F64 operands");
    } else if (kind == "u32.f64") {
      in.cvt = Vcvt::U32_F64;
      if (d1 || !d2) err(line, "bad VCVT.U32.F64 operands");
    } else if (kind == "f32.s32") {
      in.cvt = Vcvt::F32_S32;
      if (d1 || d2) err(line, "bad VCVT.F32.S32 operands");
    } else if (kind == "f64.s32") {
      in.cvt = Vcvt::F64_S32;
      if (!d1 || d2) err(line, "bad VCVT.F64.S32 operands");
    } else if (kind == "f32.u32") {
      in.cvt = Vcvt::F32_U32;
      if (d1 || d2) err(line, "bad VCVT.F32.U32 operands");
    } else if (kind == "f64.u32") {
      in.cvt = Vcvt::F64_U32;
      if (!d1 || d2) err(line, "bad VCVT.F64.U32 operands");
    } else {
      err(line, "unsupported VCVT kind: " + toks[0]);
    }
    in.op = Op::VCVT;
  } else if (stem.rfind("vmov", 0) == 0) {
    in.op = Op::VMOV;
    auto ops = operand_strs(1);
    if (ops.size() == 2) {
      bool d1 = false, d2 = false;
      int f0 = parse_freg(ops[0], d1);
      int f1 = parse_freg(ops[1], d2);
      int c0 = parse_reg(ops[0]);
      int c1 = parse_reg(ops[1]);
      if (f0 >= 0 && c1 >= 0) {
        // vmov sD, rN : core -> fp (aux = 1)
        in.rd = f0;
        in.rm = c1;
        in.dbl = d1;
        in.aux = 1;
      } else if (c0 >= 0 && f1 >= 0 && !d2) {
        // vmov rN, sM : fp -> core (aux = 2)
        in.rd = c0;
        in.rm = f1;
        in.aux = 2;
      } else if (f0 >= 0 && f1 >= 0) {
        // vmov sD, sM  /  vmov dD, dM (aux = 0)
        if (d1 != d2) err(line, "mixed s/d VMOV operands");
        in.rd = f0;
        in.rm = f1;
        in.dbl = d1;
        in.aux = 0;
      } else {
        err(line, "bad VMOV operands");
      }
    } else if (ops.size() == 3) {
      // vmov dD, rN, rM  (two core -> double)
      // vmov rN, rM, dD  (double -> two core)
      bool d0 = false, d2 = false;
      int f0 = parse_freg(ops[0], d0);
      int f2 = parse_freg(ops[2], d2);
      int c1 = parse_reg(ops[0]);
      int c2 = parse_reg(ops[1]);
      int c3 = parse_reg(ops[2]);
      if (f0 >= 0 && d0 && c1 < 0 && c2 >= 0 && c3 >= 0) {
        in.rd = f0;
        in.rn = c2;
        in.rm = c3;
        in.dbl = true;
        in.aux = 1;
      } else if (c1 >= 0 && c2 >= 0 && f2 >= 0 && d2) {
        in.rd = c1;
        in.rm = f2;
        in.rn = c2;
        in.dbl = true;
        in.aux = 2;
      } else {
        err(line, "bad VMOV operands");
      }
    } else {
      err(line, "VMOV needs two or three operands");
    }
  } else if (stem.rfind("vldr", 0) == 0 || stem.rfind("vstr", 0) == 0) {
    in.op = stem.rfind("vldr", 0) == 0 ? Op::VLDR : Op::VSTR;
    auto ops = operand_strs(1);
    if (ops.size() != 2) err(line, "VLDR/VSTR needs two operands");
    bool d1 = false;
    in.rd = parse_freg(ops[0], d1);
    if (in.rd < 0) err(line, "bad VLDR/VSTR register: " + ops[0]);
    in.dbl = d1;
    in.mem = parse_mem(ops[1], line);
  } else if (stem == "bl") {
    // BL takes no condition suffix in the emitted dialect.
    if (in.cond != Cond::AL) err(line, "unexpected conditional BL");
    in.op = Op::BL;
    in.cond = Cond::AL;
    auto ops = operand_strs(1);
    if (ops.size() != 1) err(line, "BL needs one label");
    in.symbol = ops[0];
  } else {
    err(line, "unknown instruction: " + toks[0]);
  }
  return in;
}

}  // namespace

Program parse(std::istream& in) {
  Program prog;
  enum class Section { Text, Data };
  Section section = Section::Text;
  uint32_t data_top = 0x10000;  // data section base address
  bool skip_macro = false;

  prog.memory.assign(64u << 20, 0);  // 64 MiB flat address space

  std::string line_buf;
  int lineno = 0;
  auto align4 = [&]() { data_top = (data_top + 3u) & ~3u; };
  auto write_word = [&](long long v) {
    align4();
    uint32_t u = (uint32_t)(int64_t)v;
    prog.memory[data_top] = (uint8_t)(u & 0xFF);
    prog.memory[data_top + 1] = (uint8_t)((u >> 8) & 0xFF);
    prog.memory[data_top + 2] = (uint8_t)((u >> 16) & 0xFF);
    prog.memory[data_top + 3] = (uint8_t)((u >> 24) & 0xFF);
    data_top += 4;
  };

  while (std::getline(in, line_buf)) {
    ++lineno;
    if (!line_buf.empty() && line_buf.back() == '\r') line_buf.pop_back();
    // strip '#' comments: comment lines emitted by syc start at column zero
    // ("# <log>"), while immediates (#4) are never the first token.
    {
      size_t first = line_buf.find_first_not_of(" \t");
      if (first != std::string::npos && line_buf[first] == '#')
        line_buf.clear();
    }

    auto toks = tokenize(line_buf);
    if (toks.empty()) continue;

    if (skip_macro) {
      if (lower(toks[0]) == ".endm") skip_macro = false;
      continue;
    }

    const std::string d0 = lower(toks[0]);

    // Labels (including .Lxxx:) come first -- they may start with a dot.
    if (d0.back() == ':') {
      if (section == Section::Data) {
        align4();
        prog.data_symbols[toks[0].substr(0, toks[0].size() - 1)] = data_top;
      } else {
        std::string name = toks[0].substr(0, toks[0].size() - 1);
        if (!prog.labels.emplace(name, prog.text.size()).second)
          err(lineno, "duplicate label: " + name);
        // allow trailing instruction on the same line (not emitted today)
        if (toks.size() > 1) {
          Inst inst = parse_inst(toks, lineno);
          prog.text.push_back({inst, lineno});
        }
      }
      continue;
    }

    if (d0[0] == '.') {
      // Most dot-directives: some switch section, the rest are noise.
      if (d0 == ".text") {
        section = Section::Text;
      } else if (d0 == ".data") {
        section = Section::Data;
      } else if (d0 == ".section") {
        // .rodata holds string constants emitted via .asciz
        std::string name = lower(toks.size() > 1 ? toks[1] : "");
        if (name.find(".text") != std::string::npos)
          section = Section::Text;
        else
          section = Section::Data;
      } else if (d0 == ".macro") {
        skip_macro = true;
      } else if (d0 == ".asciz" || d0 == ".ascii") {
        if (section != Section::Data) err(lineno, d0 + " outside .data");
        auto a = line_buf.find('"');
        auto b = line_buf.rfind('"');
        if (a == std::string::npos || b == std::string::npos || b < a)
          err(lineno, "malformed string literal");
        for (size_t i = a + 1; i < b; i++) {
          char c = line_buf[i];
          if (c == '\\' && i + 1 < b) {
            char e = line_buf[++i];
            switch (e) {
              case 'n': c = '\n'; break;
              case 't': c = '\t'; break;
              case 'r': c = '\r'; break;
              case '0': c = '\0'; break;
              case '\\': c = '\\'; break;
              case '"': c = '"'; break;
              default: err(lineno, std::string("unknown escape \\") + e);
            }
          }
          prog.memory[data_top++ % prog.memory.size()] = (uint8_t)c;
        }
        if (d0 == ".asciz")
          prog.memory[data_top++ % prog.memory.size()] = 0;  // NUL terminator
      } else if (d0 == ".word") {
        if (section != Section::Data) err(lineno, ".word outside .data");
        for (size_t i = 1; i < toks.size(); i++) {
          std::string v = toks[i];
          while (!v.empty() && (v.back() == ',')) v.pop_back();
          if (v.empty()) continue;
          long long w;
          if (!parse_imm(v, w)) err(lineno, "unsupported .word value: " + v);
          write_word(w);
        }
      } else if (d0 == ".space") {
        if (section != Section::Data) err(lineno, ".space outside .data");
        long long n = 0;
        if (toks.size() < 2 || !parse_imm(toks[1], n) || n < 0)
          err(lineno, "bad .space size");
        data_top += (uint32_t)n;
      }
      // .global/.type/.file/.loc/.ltorg/.align/.end are ignored.
      continue;
    }

    Inst inst = parse_inst(toks, lineno);
    if (getenv("ARMSIM_DEBUG"))
      fprintf(stderr, "[parse L%d] %s -> cond=%d\n", lineno, toks[0].c_str(),
              (int)inst.cond);
    prog.text.push_back({inst, lineno});
  }

  // Globals referenced by generated code but provided by the runtime
  // (timing bookkeeping arrays injected by src/ir/generate/context.cpp).
  static const std::unordered_map<std::string, long long> kRuntimeGlobals = {
      {"_sysy_l1", 1024}, {"_sysy_l2", 1024}, {"_sysy_h", 1024},
      {"_sysy_m", 1024},  {"_sysy_s", 1024},  {"_sysy_us", 1024},
      {"_sysy_idx", 1},
  };
  for (const auto& [name, count] : kRuntimeGlobals) {
    if (prog.data_symbols.count(name)) continue;
    align4();
    prog.data_symbols[name] = data_top;
    data_top += (uint32_t)(count * 4);
  }

  // Resolve branch targets, BL callees and MOV32 symbol constants.
  for (size_t i = 0; i < prog.text.size(); i++) {
    Inst& inst = prog.text[i].inst;
    if (inst.symbol.empty()) continue;
    if (inst.op == Op::B || inst.op == Op::BL) {
      auto lit = prog.labels.find(inst.symbol);
      if (lit != prog.labels.end()) {
        inst.target = lit->second;
        continue;
      }
      auto nit = natives().find(inst.symbol);
      if (inst.op == Op::B || nit == natives().end()) {
        err(prog.text[i].line,
            std::string(inst.op == Op::B ? "unknown branch target: "
                                         : "unknown function: ") +
                inst.symbol);
      }
      inst.target = Program::kNativeCall;
    } else if (inst.op == Op::MOV32) {
      auto dit = prog.data_symbols.find(inst.symbol);
      if (dit != prog.data_symbols.end()) {
        inst.imm = (long long)(int64_t)(int32_t)dit->second;
        inst.imm_op = true;
        inst.symbol.clear();
      } else {
        err(prog.text[i].line, "unknown data symbol: " + inst.symbol);
      }
    }
  }
  return prog;
}

}  // namespace armsim
