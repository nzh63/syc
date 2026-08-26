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
#include "assembly/generate/context.h"

#include <algorithm>
#include <cassert>
#include <exception>

namespace syc::assembly {
using namespace std;
Context::Context(ir::IRList* irs, ir::IRList::iterator function_begin_it,
                 ostream& log_out)
    : irs(irs), function_begin_it(function_begin_it), log_out(log_out) {
  // VFP 实参预着色：$fargN（N<16）固定占用 sN
  for (int i = 0; i < 16; i++) var_to_freg["$farg" + to_string(i)] = i;
}

string Context::rename(string name) {
  if (name.size() > 3 && name[1] == '&' && name[2] == '^')
    return name.substr(3);
  else if (name.size() > 2 && name[1] == '^')
    return name.substr(2);
  else if (name.size() > 2 && name[1] == '&')
    return "__Var__" + to_string(name.size() - 2) + name.substr(2);
  else
    return "__Var__" + to_string(name.size() - 1) + name.substr(1);
}

// 64 位名字的高半部分：低半与高半作为两个独立的 32 位区间参与分配
string Context::hi_name(const string& name) { return name + "#h"; }

bool Context::is_wide(const string& name) const {
  return var_width.count(name) != 0;
}

void Context::mark_wide(const string& name) {
  if (!name.empty()) var_width.insert({name, 8});
}

int Context::resolve_stack_offset(string name) {
  if (name.size() > 5 && name.compare(0, 5, "$farg") == 0) {
    auto id = stoi(name.substr(5));
    if (id < 16) {
      throw runtime_error(name + " is not in mem.");
    } else {
      // VFP 栈参数在核心栈参数区之后（调用方先放核心实参再放浮点实参）
      return stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] +
             stack_size[3] + (id - 16) * 4;
    }
  } else if (name.substr(0, 4) == "$arg") {
    auto id = stoi(name.substr(4));
    if (id < 4) {
      throw runtime_error(name + " is not in mem.");
    } else {
      return stack_size[0] + stack_size[1] + stack_size[2] + stack_size[3] +
             (id - 4) * 4;
    }
  } else if (name == "$ra") {
    return stack_size[1] + stack_size[2] + stack_size[3] - 4;
  }
  return stack_offset_map[name] + stack_size[3];
}

void Context::set_ir_timestamp(ir::IR& cur) {
  ir_to_time.insert({&cur, ++time});
}
// IR 结果类型的浮点类别
Context::FpClass fp_class_of_ty(types::TypeId ty) {
  using T = syc::ast::types::TypeId;
  switch (syc::ast::types::canonical(ty)) {
    case T::FLOAT: return Context::FpClass::F32;
    case T::DOUBLE: return Context::FpClass::F64;
    default: return Context::FpClass::NONE;
  }
}

// 该 IR 是否通过 BL 调用辅助函数（破坏 r0-r3）
static bool ir_clobbers_args(const ir::IR& cur) {
  if (cur.op_code == ir::OpCode::CALL || cur.op_code == ir::OpCode::IDIV ||
      cur.op_code == ir::OpCode::MOD || cur.op_code == ir::OpCode::UDIV ||
      cur.op_code == ir::OpCode::UMOD)
    return true;
  // 64 位乘法走 __aeabi_lmul
  if (cur.op_code == ir::OpCode::IMUL &&
      syc::ast::types::width_bytes(
          syc::ast::types::canonical(cur.ty.id)) >= 8)
    return true;
  return false;
}

void Context::set_var_latest_use_timestamp(ir::IR& cur) {
  if (cur.op_code != ir::OpCode::MALLOC_IN_STACK) {
    cur.forEachOp([&, this](ir::OpName op) {
      if (op.is_var()) {
        if (!var_latest_use_timestamp.count(op.name)) {
          var_latest_use_timestamp.insert({op.name, ir_to_time[&cur]});
          var_latest_use_timestamp_heap.insert({ir_to_time[&cur], op.name});
        }
        // 64 位值的高半与低半同步其生命周期
        if (op.name[0] == '%' && is_wide(op.name)) {
          string hi = Context::hi_name(op.name);
          if (!var_latest_use_timestamp.count(hi)) {
            var_latest_use_timestamp.insert({hi, ir_to_time[&cur]});
            var_latest_use_timestamp_heap.insert({ir_to_time[&cur], hi});
          }
        }
      }
    });
  }
  if (cur.op_code == ir::OpCode::SET_ARG && cur.dest.value < 4) {
    string name = "$arg:" + to_string(cur.dest.value) + ":0";
    var_latest_use_timestamp.insert({name, 0});
    var_latest_use_timestamp_heap.insert(make_pair(0, name));
    // 宽度 8 的实参还占用下一号寄存器/槽位
    if (syc::ast::types::width_bytes(
            syc::ast::types::canonical(cur.ty.id)) >= 8 &&
        cur.dest.value + 1 < 4) {
      string name2 = "$arg:" + to_string(cur.dest.value + 1) + ":0";
      var_latest_use_timestamp.insert({name2, 0});
      var_latest_use_timestamp_heap.insert(make_pair(0, name2));
    }
  }
  if (cur.op_code == ir::OpCode::IMUL && cur.op1.is_var()) {
    var_latest_use_timestamp[cur.op1.name]++;
  }
  // 易失寄存器
  if (ir_clobbers_args(cur)) {
    for (int i = 0; i < 4; i++) {
      string name = "$arg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_latest_use_timestamp.insert({name, ir_to_time[&cur]});
      var_latest_use_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
  // CALL 破坏 VFP caller-saved s0-s15
  if (cur.op_code == ir::OpCode::CALL) {
    for (int i = 0; i < 16; i++) {
      string name = "$fsarg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_latest_use_timestamp.insert({name, ir_to_time[&cur]});
      var_latest_use_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
  // VFP 实参销定：SET_ARG(vfp) 到调用点占用 s 槽位
  if (cur.op_code == ir::OpCode::SET_ARG && cur.label == "vfp") {
    int n_slot = syc::ast::types::width_bytes(
                     syc::ast::types::canonical(cur.ty.id)) >= 8
                     ? 2
                     : 1;
    for (int k = 0; k < n_slot; k++) {
      string name = "$fsarg:" + to_string((int)cur.dest.value + k) + ":0";
      var_latest_use_timestamp.insert({name, 0});
      var_latest_use_timestamp_heap.insert(make_pair(0, name));
    }
  }
}

void Context::set_var_define_timestamp(ir::IR& cur) {
  if (cur.op_code != ir::OpCode::MALLOC_IN_STACK &&
      cur.op_code != ir::OpCode::PHI_MOV) {
    if (cur.dest.is_var()) {
      if (!var_define_timestamp.count(cur.dest.name)) {
        var_define_timestamp.insert({cur.dest.name, ir_to_time[&cur]});
        var_define_timestamp_heap.insert(
            make_pair(ir_to_time[&cur], cur.dest.name));
      }
      // 64 位 dest 的高半同步登记
      if (cur.dest.name[0] == '%' && is_wide(cur.dest.name)) {
        string hi = Context::hi_name(cur.dest.name);
        if (!var_define_timestamp.count(hi)) {
          var_define_timestamp.insert({hi, ir_to_time[&cur]});
          var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], hi));
        }
      }
    }
  } else if (cur.op_code == ir::OpCode::PHI_MOV) {
    if (cur.dest.is_var()) {
      if (!var_define_timestamp.count(cur.dest.name)) {
        int time = min(ir_to_time[&*cur.phi_block], ir_to_time[&cur]);
        var_define_timestamp.insert({cur.dest.name, time});
        var_define_timestamp_heap.insert(make_pair(time, cur.dest.name));
      }
      // 64 位 PHI_MOV dest 的高半同步登记
      if (cur.dest.name[0] == '%' && is_wide(cur.dest.name)) {
        string hi = Context::hi_name(cur.dest.name);
        if (!var_define_timestamp.count(hi)) {
          int time = min(ir_to_time[&*cur.phi_block], ir_to_time[&cur]);
          var_define_timestamp.insert({hi, time});
          var_define_timestamp_heap.insert(make_pair(time, hi));
        }
      }
    }
  }
  if (cur.op_code == ir::OpCode::SET_ARG && cur.dest.value < 4) {
    string name = "$arg:" + to_string(cur.dest.value) + ":" +
                  to_string(ir_to_time[&cur]);
    var_define_timestamp.insert({name, ir_to_time[&cur]});
    var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    if (syc::ast::types::width_bytes(
            syc::ast::types::canonical(cur.ty.id)) >= 8 &&
        cur.dest.value + 1 < 4) {
      string name2 = "$arg:" + to_string(cur.dest.value + 1) + ":" +
                     to_string(ir_to_time[&cur]);
      var_define_timestamp.insert({name2, ir_to_time[&cur]});
      var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name2));
    }
  }
  if (ir_clobbers_args(cur)) {
    for (int i = 0; i < 4; i++) {
      string name = "$arg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_define_timestamp.insert({name, ir_to_time[&cur]});
      var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
  if (cur.op_code == ir::OpCode::CALL) {
    for (int i = 0; i < 16; i++) {
      string name = "$fsarg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_define_timestamp.insert({name, ir_to_time[&cur]});
      var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
  if (cur.op_code == ir::OpCode::SET_ARG && cur.label == "vfp") {
    int n_slot = syc::ast::types::width_bytes(
                     syc::ast::types::canonical(cur.ty.id)) >= 8
                     ? 2
                     : 1;
    for (int k = 0; k < n_slot; k++) {
      string name = "$fsarg:" + to_string((int)cur.dest.value + k) + ":" +
                    to_string(ir_to_time[&cur]);
      var_define_timestamp.insert({name, ir_to_time[&cur]});
      var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
}

void Context::expire_old_intervals(int cur_time) {
  for (int i = 0; i < reg_count; i++) {
    if (used_reg[i])
      if (reg_to_var.find(i) == reg_to_var.end() ||
          var_latest_use_timestamp.find(reg_to_var[i]) ==
              var_latest_use_timestamp.end() ||
          cur_time >= var_latest_use_timestamp[reg_to_var[i]]) {
        used_reg[i] = 0;
        log_out << "# [log] " << cur_time << " expire " << reg_to_var[i] << " r"
                << i << endl;
        reg_to_var.erase(i);
      }
  }
}

bool Context::var_in_reg(string name) { return var_to_reg.count(name); }

void Context::move_reg(string name, int reg_dest) {
  assert(used_reg[reg_dest] == 0);
  int old_reg = var_to_reg[name];
  used_reg[old_reg] = 0;
  get_specified_reg(reg_dest);
  reg_to_var.erase(old_reg);
  var_to_reg[name] = reg_dest;
  reg_to_var.insert({reg_dest, name});
}

void Context::overflow_var(string name) {
  if (var_to_reg.count(name)) {
    const int reg_id = var_to_reg[name];
    used_reg[reg_id] = 0;
    var_to_reg.erase(name);
    reg_to_var.erase(reg_id);
  }
  stack_offset_map[name] = stack_size[2];
  stack_size[2] += 4;
}

void Context::overflow_reg(int reg_id) {
  if (!used_reg[reg_id]) return;
  overflow_var(reg_to_var[reg_id]);
}

string Context::select_var_to_overflow(int begin) {
  string var = "";
  int end = -1;
  for (const auto& i : reg_to_var) {
    if (i.first < begin) continue;
    if (var_latest_use_timestamp[i.second] > end) {
      var = i.second;
      end = var_latest_use_timestamp[i.second];
    }
  }
  return var;
}

int Context::find_free_reg(int begin) {
  // 检测可直接使用的
  for (int i = begin; i < reg_count; i++)
    if ((savable_reg | used_reg)[i] == 0) {
      return i;
    }
  // 保护现场后可用的
  for (int i = begin; i < reg_count; i++)
    if (used_reg[i] == 0 && savable_reg[i] == 1) {
      return i;
    }
  return -1;
}

int Context::get_new_reg(int begin) {
  // 检测可直接使用的
  for (int i = begin; i < reg_count; i++)
    if ((savable_reg | used_reg)[i] == 0) {
      used_reg[i] = 1;
      return i;
    }
  // 保护现场后可用的
  for (int i = begin; i < reg_count; i++)
    if (used_reg[i] == 0 && savable_reg[i] == 1) {
      used_reg[i] = 1;
      stack_size[1] += 4;
      savable_reg[i] = 0;
      return i;
    }
  // 选一个溢出
  int id = begin;
  for (int i = begin; i < reg_count; i++) {
    if (var_latest_use_timestamp[reg_to_var[i]] >
        var_latest_use_timestamp[reg_to_var[id]])
      id = i;
  }
  overflow_reg(id);
  used_reg[id] = 1;
  return id;
}

void Context::get_specified_reg(int reg_id) {
  assert(used_reg[reg_id] == 0);
  if (savable_reg[reg_id] == 1) {
    savable_reg[reg_id] = 0;
    stack_size[1] += 4;
  }
  used_reg[reg_id] = 1;
}

void Context::bind_var_to_reg(string name, int reg_id) {
  if (name.empty()) {
    throw runtime_error("Var name is empty.");
  }
  assert(used_reg[reg_id] == 1);
  if (reg_to_var.find(reg_id) != reg_to_var.end())
    var_to_reg.erase(reg_to_var[reg_id]);
  reg_to_var.insert({reg_id, name});
  var_to_reg.insert({name, reg_id});
}

int Context::get_new_reg_for(string name) {
  if (var_to_reg.find(name) != var_to_reg.end()) {
    return var_to_reg[name];
  }
  int reg_id = get_new_reg();
  bind_var_to_reg(name, reg_id);
  return reg_id;
}

int Context::get_specified_reg_for(string name, int reg_id) {
  if (var_to_reg.find(name) != var_to_reg.end()) {
    if (var_to_reg[name] == reg_id) {
      return var_to_reg[name];
    }
  }
  if (used_reg[reg_id] == 1) {
    overflow_reg(reg_id);
  }
  get_specified_reg(reg_id);
  bind_var_to_reg(name, reg_id);
  return reg_id;
}

// ── VFP 浮点寄存器分配 ────────────────────────────────────────────
Context::FpClass Context::fp_class(const string& name) const {
  auto it = var_fp.find(name);
  return it == var_fp.end() ? FpClass::NONE : it->second;
}

bool Context::is_fpreg(const string& name) const {
  return fp_class(name) != FpClass::NONE;
}

void Context::mark_fp(const string& name, types::TypeId ty) {
  FpClass c = fp_class_of_ty(ty);
  if (c != FpClass::NONE && !name.empty()) var_fp.insert({name, c});
}

bool Context::var_in_freg(string name) { return var_to_freg.count(name) != 0; }

void Context::expire_old_freg_intervals(int cur_time) {
  for (int i = 0; i < freg_count; i++) {
    if (used_freg[i]) {
      auto it = freg_to_var.find(i);
      if (it == freg_to_var.end() ||
          var_latest_use_timestamp.find(it->second) ==
              var_latest_use_timestamp.end() ||
          cur_time >= var_latest_use_timestamp[it->second]) {
        used_freg[i] = 0;
        freg_to_var.erase(i);
      }
    }
  }
}

void Context::overflow_fvar(string name) {
  FpClass cls = fp_class(name);
  if (var_to_freg.count(name)) {
    int lo = var_to_freg[name];
    int n = cls == FpClass::F64 ? 2 : 1;
    for (int k = 0; k < n; k++) {
      used_freg[lo + k] = 0;
      freg_to_var.erase(lo + k);
    }
    var_to_freg.erase(name);
  }
  if (cls == FpClass::F64) {
    // double 溢出槽 8 字节且 8 对齐（为真机 VLDR.64 做准备）
    stack_size[2] = (stack_size[2] + 7) & ~7;
    stack_offset_map[name] = stack_size[2];
    stack_size[2] += 8;
  } else {
    stack_offset_map[name] = stack_size[2];
    stack_size[2] += 4;
  }
}

// 空闲单个/成对（偶对齐）s 寄存器；begin 之后的 caller-saved 优先，
// 其次 callee-saved（需保护现场）
int Context::find_free_freg(bool dbl, int begin) {
  auto clear = [&](int i) { return (savable_freg | used_freg)[i] == 0; };
  if (!dbl) {
    for (int i = begin; i < freg_allocatable; i++)
      if (clear(i) && !savable_freg[i]) return i;
    for (int i = begin; i < freg_allocatable; i++)
      if (clear(i) && savable_freg[i]) return i;
    return -1;
  }
  for (int i = begin & ~1; i + 1 < freg_allocatable; i += 2)
    if (clear(i) && clear(i + 1)) return i;
  return -1;
}

int Context::get_new_freg(bool dbl, int begin) {
  int r = find_free_freg(dbl, begin);
  int n = dbl ? 2 : 1;
  if (r != -1) {
    for (int k = 0; k < n; k++) {
      if (savable_freg[r + k]) {
        savable_freg[r + k] = 0;
        stack_size[1] += 4;
      }
      used_freg[r + k] = 1;
    }
    return r;
  }
  // 无空闲：溢出占用者（选最晚使用者；成对时一并溢出）
  if (!dbl) {
    int best = -1, best_time = -1;
    for (int i = begin; i < freg_allocatable; i++) {
      if (!used_freg[i]) continue;
      auto it = freg_to_var.find(i);
      int t = it != freg_to_var.end() &&
                      var_latest_use_timestamp.count(it->second)
                  ? var_latest_use_timestamp[it->second]
                  : 0;
      if (t > best_time) { best_time = t; best = i; }
    }
    if (best != -1) overflow_fvar(freg_to_var[best]);
    used_freg[best] = 1;
    return best;
  }
  int best = -1, best_time = -1;
  for (int i = begin & ~1; i + 1 < freg_allocatable; i += 2) {
    int t = 0;
    for (int k = 0; k < 2; k++) {
      if (!used_freg[i + k]) continue;
      auto it = freg_to_var.find(i + k);
      if (it != freg_to_var.end() && var_latest_use_timestamp.count(it->second))
        t = max(t, var_latest_use_timestamp[it->second]);
    }
    if (t > best_time) { best_time = t; best = i; }
  }
  if (best != -1) {
    if (used_freg[best]) overflow_fvar(freg_to_var[best]);
    if (used_freg[best + 1]) overflow_fvar(freg_to_var[best + 1]);
  }
  for (int k = 0; k < 2; k++) {
    if (savable_freg[best + k]) {
      savable_freg[best + k] = 0;
      stack_size[1] += 4;
    }
    used_freg[best + k] = 1;
  }
  return best;
}

int Context::get_new_freg_for(string name, int begin) {
  if (var_to_freg.find(name) != var_to_freg.end()) return var_to_freg[name];
  bool dbl = fp_class(name) == FpClass::F64;
  int r = get_new_freg(dbl, begin);
  var_to_freg[name] = r;
  freg_to_var[r] = name;
  if (dbl) freg_to_var[r + 1] = name;
  return r;
}

int Context::get_specified_freg_for(string name, int reg_id) {
  if (var_to_freg.find(name) != var_to_freg.end() &&
      var_to_freg[name] == reg_id)
    return reg_id;
  if (used_freg[reg_id]) overflow_fvar(freg_to_var[reg_id]);
  bool dbl = fp_class(name) == FpClass::F64;
  int n = dbl ? 2 : 1;
  for (int k = 0; k < n; k++) {
    if (used_freg[reg_id + k] && freg_to_var[reg_id + k] != name)
      overflow_fvar(freg_to_var[reg_id + k]);
    if (savable_freg[reg_id + k]) {
      savable_freg[reg_id + k] = 0;
      stack_size[1] += 4;
    }
    used_freg[reg_id + k] = 1;
    freg_to_var[reg_id + k] = name;
  }
  var_to_freg[name] = reg_id;
  return reg_id;
}

// 浮点装载：op1 → 目标 s/d 寄存器
void Context::load_freg(int sidx, bool dbl, ir::OpName op, ostream& out) {
  string dn = dbl ? "d" + to_string(sidx / 2) : "s" + to_string(sidx);
  if (op.is_imm()) {
    // 浮点立即数：位模式经核心寄存器装入
    if (dbl) {
      load_imm("r12", (int32_t)(uint32_t)(uint64_t)op.value, out);
      load_imm("r14", (int32_t)((uint64_t)op.value >> 32), out);
      out << "    VMOV " << dn << ", r12, r14" << endl;
    } else {
      load_imm("r12", (int32_t)(uint32_t)op.value, out);
      out << "    VMOV " << dn << ", r12" << endl;
    }
    return;
  }
  const string& nm = op.name;
  if (var_in_freg(nm)) {
    int src = var_to_freg[nm];
    string sd = fp_class(nm) == FpClass::F64 ? "d" + to_string(src / 2)
                                             : "s" + to_string(src);
    if (sd != dn) out << "    VMOV " << dn << ", " << sd << endl;
    return;
  }
  if (nm.size() > 5 && nm.compare(0, 5, "$farg") == 0) {
    int id = stoi(nm.substr(5));
    if (id < 16) {
      string sd = dbl ? "d" + to_string(id / 2) : "s" + to_string(id);
      if (sd != dn) out << "    VMOV " << dn << ", " << sd << endl;
      return;
    }
    // VFP 栈参数
    load_from_stack_offset(dn, resolve_stack_offset(nm), out, "VLDR");
    return;
  }
  if (nm[0] == '@' && nm[1] != '&') {
    // 全局标量浮点
    out << "    MOV32 r12, " << rename(nm) << endl;
    out << "    VLDR " << dn << ", [r12,#0]" << endl;
    return;
  }
  // 局部溢出槽
  int offset = resolve_stack_offset(nm);
  load_from_stack_offset(dn, offset, out, "VLDR");
}

// 浮点存放：sidx 寄存器 → dest 落点
void Context::store_freg_dest(int sidx, bool dbl, const string& name,
                              ostream& out) {
  string dn = dbl ? "d" + to_string(sidx / 2) : "s" + to_string(sidx);
  if (name[0] == '@') {
    out << "    MOV32 r12, " << rename(name) << endl;
    out << "    VSTR " << dn << ", [r12,#0]" << endl;
    return;
  }
  if (var_in_freg(name)) {
    int dreg = var_to_freg[name];
    string dd = fp_class(name) == FpClass::F64 ? "d" + to_string(dreg / 2)
                                               : "s" + to_string(dreg);
    if (dd != dn) out << "    VMOV " << dd << ", " << dn << endl;
    return;
  }
  if (name.size() > 5 && name.compare(0, 5, "$farg") == 0) {
    int id = stoi(name.substr(5));
    if (id < 16) {
      string sd = dbl ? "d" + to_string(id / 2) : "s" + to_string(id);
      if (sd != dn) out << "    VMOV " << sd << ", " << dn << endl;
      return;
    }
    store_to_stack_offset(dn, resolve_stack_offset(name), out, "VSTR");
    return;
  }
  store_to_stack_offset(dn, resolve_stack_offset(name), out, "VSTR");
}

void Context::load_imm(string reg, int64_t value, ostream& out) {
  if (value >= 0 && value < 65536)
    out << "    MOV " << reg << ", #" << value << endl;
  else
    out << "    MOV32 " << reg << ", " << value << endl;
};

void Context::load(string reg, ir::OpName op, ostream& out) {
  if (op.is_imm()) {
    load_imm(reg, op.value, out);
  } else if (op.is_var()) {
    if (var_to_reg.find(op.name) != var_to_reg.end()) {
      if (stoi(reg.substr(1)) != var_to_reg[op.name])
        out << "    MOV " << reg << ", r" << var_to_reg[op.name] << endl;
    } else {
      if (op.name[0] == '%') {
        if (op.name[1] == '&') {
          int offset = resolve_stack_offset(op.name);
          if (offset >= 0 && offset < 256) {
            out << "    ADD " << reg << ", sp, #" << offset << endl;
          } else {
            load_imm(reg, resolve_stack_offset(op.name), out);
            out << "    ADD " << reg << ", sp, " << reg << endl;
          }
        } else {
          if (var_in_reg(op.name)) {
            out << "    MOV " << reg << ", r" << var_to_reg[op.name] << endl;
          } else {
            int offset = resolve_stack_offset(op.name);
            if (offset > -4096 && offset < 4096) {
              out << "    LDR " << reg << ", [sp,#" << offset << ']' << endl;
            } else {
              out << "    MOV32 " << reg << ", " << offset << endl;
              out << "    LDR " << reg << ", [sp, " << reg << "]" << endl;
            }
          }
        }
      } else if (op.name[0] == '@') {
        if (op.name[1] != '&') {
          out << "    MOV32 " << reg << ", " << rename(op.name) << endl;
          out << "    LDR " << reg << ", [" << reg << ",#0]" << endl;
        } else {
          out << "    MOV32 " << reg << ", " << rename(op.name) << endl;
        }
      } else if (op.name[0] == '$') {
        int offset = resolve_stack_offset(op.name);
        if (offset > -4096 && offset < 4096) {
          out << "    LDR " << reg << ", [sp,#" << offset << ']' << endl;
        } else {
          out << "    MOV32 " << reg << ", " << offset << endl;
          out << "    LDR " << reg << ", [sp," << reg << "]" << endl;
        }
      }
    }
  }
}

void Context::load_half(string reg, ir::OpName op, int half, ostream& out) {
  if (op.is_imm()) {
    int32_t h = half ? (int32_t)((uint64_t)op.value >> 32)
                     : (int32_t)(uint32_t)(uint64_t)op.value;
    load_imm(reg, h, out);
    return;
  }
  if (half == 0) {
    // 低半：常规路径（$argN(N<4) 经 var_to_reg 直接映射到寄存器）
    load(reg, op, out);
    return;
  }
  const string& nm = op.name;
  if (nm[0] == '@') {
    // 全局标量：高半位于 +4 地址
    out << "    MOV32 " << reg << ", " << rename(nm) << endl;
    out << "    LDR " << reg << ", [" << reg << ",#4]" << endl;
  } else if (nm.size() > 4 && nm.compare(0, 4, "$arg") == 0) {
    int id = stoi(nm.substr(4));
    if (id + 1 < 4) {
      string src = "r" + to_string(id + 1);
      if (reg != src) out << "    MOV " << reg << ", " << src << endl;
    } else {
      load_from_stack_offset(reg, resolve_stack_offset(nm) + 4, out);
    }
  } else {
    // 局部变量：高半是独立参与分配的 #h 名字
    load(reg, ir::OpName(Context::hi_name(nm)), out);
  }
}

void Context::store_to_stack_offset(string reg, int offset, ostream& out,
                                    string op) {
  if (!(offset > -4096 && offset < 4096)) {
    string tmp_reg = reg == "r14" ? "r12" : "r14";
    load_imm(tmp_reg, offset, out);
    out << "    " << op << " " << reg << ", [sp," << tmp_reg << "]" << endl;
  } else {
    out << "    " << op << " " << reg << ", [sp,#" << offset << "]" << endl;
  }
}

void Context::load_from_stack_offset(string reg, int offset, ostream& out,
                                     string op) {
  if (offset > -4096 && offset < 4096) {
    out << "    " << op << " " << reg << ", [sp,#" << offset << "]" << endl;
  } else {
    load_imm(reg, offset, out);
    out << "    " << op << " " << reg << ", [sp," << reg << "]" << endl;
  }
}

void Context::store_to_stack(string reg, ir::OpName op, ostream& out,
                             string op_code) {
  if (!op.is_var()) throw runtime_error("WTF");
  if (op.name[1] == '&') return;
  if (op.name[0] == '%') {
    store_to_stack_offset(reg, resolve_stack_offset(op.name), out, op_code);
  } else if (op.name[0] == '@') {
    string tmp_reg = reg == "r14" ? "r12" : "r14";
    out << "    MOV32 " << tmp_reg << ", " << rename(op.name) << endl;
    out << "    " << op_code << " " << reg << ", [" << tmp_reg << ",#0]"
        << endl;
  } else if (op.name[0] == '$') {
    int offset = resolve_stack_offset(op.name);
    store_to_stack_offset(reg, offset, out, op_code);
  }
}
}  // namespace syc::assembly
