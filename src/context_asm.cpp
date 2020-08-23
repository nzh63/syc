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
#include "context_asm.h"

using namespace std;
ContextASM::ContextASM(IRList* irs, IRList::iterator function_begin_it,
                       ostream& log_out)
    : irs(irs), function_begin_it(function_begin_it), log_out(log_out) {}

string ContextASM::rename(string name) {
  if (name.size() > 3 && name[1] == '&' && name[2] == '^')
    return name.substr(3);
  else if (name.size() > 2 && name[1] == '^')
    return name.substr(2);
  else if (name.size() > 2 && name[1] == '&')
    return "__Var__" + to_string(name.size() - 2) + name.substr(2);
  else
    return "__Var__" + to_string(name.size() - 1) + name.substr(1);
}

int ContextASM::resolve_stack_offset(string name) {
  if (name.substr(0, 4) == "$arg") {
    auto id = stoi(name.substr(4));
    if (id < 4) {
      throw runtime_error(name + " is not in mem.");
    } else {
      return stack_size[1] + stack_size[2] + stack_size[3] + (id - 4) * 4 +
             (has_function_call ? 4 : 0);
    }
  } else if (name == "$ra") {
    return stack_size[1] + stack_size[2] + stack_size[3];
  }
  return stack_offset_map[name] + stack_size[3];
}

void ContextASM::set_ir_timestamp(IR& cur) {
  ir_to_time.insert({&cur, ++time});
}
void ContextASM::set_var_latest_use_timestamp(IR& cur) {
  if (cur.op_code != IR::OpCode::MALLOC_IN_STACK) {
#define F(op1)                                                           \
  if (cur.op1.type == OpName::Type::Var) {                               \
    if (!var_latest_use_timestamp.count(cur.op1.name)) {                 \
      var_latest_use_timestamp.insert({cur.op1.name, ir_to_time[&cur]}); \
      var_latest_use_timestamp_heap.insert(                              \
          make_pair(ir_to_time[&cur], cur.op1.name));                    \
    }                                                                    \
  }
    F(dest);
    F(op1);
    F(op2);
    F(op3);
#undef F
  }
  if (cur.op_code == IR::OpCode::SET_ARG && cur.dest.value < 4) {
    string name = "$arg:" + to_string(cur.dest.value) + ":" +
                  to_string(ir_to_time[&*cur.phi_block]);
    var_latest_use_timestamp.insert({name, ir_to_time[&*cur.phi_block]});
    var_latest_use_timestamp_heap.insert(
        make_pair(ir_to_time[&*cur.phi_block], name));
  }
  if (cur.op_code == IR::OpCode::IMUL && cur.op1.type == OpName::Type::Var) {
    var_latest_use_timestamp[cur.op1.name]++;
  }
  // 易失寄存器
  if (cur.op_code == IR::OpCode::CALL || cur.op_code == IR::OpCode::IDIV ||
      cur.op_code == IR::OpCode::MOD) {
    for (int i = 0; i < 4; i++) {
      string name = "$arg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_latest_use_timestamp.insert({name, ir_to_time[&cur]});
      var_latest_use_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
}

void ContextASM::set_var_define_timestamp(IR& cur) {
  if (cur.op_code != IR::OpCode::MALLOC_IN_STACK &&
      cur.op_code != IR::OpCode::PHI_MOV) {
    if (cur.dest.type == OpName::Type::Var) {
      if (!var_define_timestamp.count(cur.dest.name)) {
        var_define_timestamp.insert({cur.dest.name, ir_to_time[&cur]});
        var_define_timestamp_heap.insert(
            make_pair(ir_to_time[&cur], cur.dest.name));
      }
    }
  } else if (cur.op_code == IR::OpCode::PHI_MOV) {
    if (cur.dest.type == OpName::Type::Var) {
      if (!var_define_timestamp.count(cur.dest.name)) {
        int time = min(ir_to_time[&*cur.phi_block], ir_to_time[&cur]);
        var_define_timestamp.insert({cur.dest.name, time});
        var_define_timestamp_heap.insert(make_pair(time, cur.dest.name));
      }
    }
  }
  if (cur.op_code == IR::OpCode::SET_ARG && cur.dest.value < 4) {
    string name = "$arg:" + to_string(cur.dest.value) + ":" +
                  to_string(ir_to_time[&*cur.phi_block]);
    var_define_timestamp.insert({name, ir_to_time[&cur]});
    var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
  }
  if (cur.op_code == IR::OpCode::CALL || cur.op_code == IR::OpCode::IDIV ||
      cur.op_code == IR::OpCode::MOD) {
    for (int i = 0; i < 4; i++) {
      string name = "$arg:" + to_string(i) + ":" + to_string(ir_to_time[&cur]);
      var_define_timestamp.insert({name, ir_to_time[&cur]});
      var_define_timestamp_heap.insert(make_pair(ir_to_time[&cur], name));
    }
  }
}

void ContextASM::expire_old_intervals(int cur_time) {
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

bool ContextASM::var_in_reg(string name) { return var_to_reg.count(name); }

void ContextASM::move_reg(string name, int reg_dest) {
  assert(used_reg[reg_dest] == 0);
  int old_reg = var_to_reg[name];
  used_reg[old_reg] = 0;
  get_specified_reg(reg_dest);
  reg_to_var.erase(old_reg);
  var_to_reg[name] = reg_dest;
  reg_to_var.insert({reg_dest, name});
}

void ContextASM::overflow_var(string name) {
  if (var_to_reg.count(name)) {
    const int reg_id = var_to_reg[name];
    used_reg[reg_id] = 0;
    var_to_reg.erase(name);
    reg_to_var.erase(reg_id);
  }
  stack_offset_map[name] = stack_size[2];
  stack_size[2] += 4;
}

void ContextASM::overflow_reg(int reg_id) {
  if (!used_reg[reg_id]) return;
  overflow_var(reg_to_var[reg_id]);
}

string ContextASM::select_var_to_overflow(int begin) {
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

int ContextASM::find_free_reg(int begin) {
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

int ContextASM::get_new_reg(int begin) {
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

void ContextASM::get_specified_reg(int reg_id) {
  assert(used_reg[reg_id] == 0);
  if (savable_reg[reg_id] == 1) {
    savable_reg[reg_id] = 0;
    stack_size[1] += 4;
  }
  used_reg[reg_id] = 1;
}

void ContextASM::bind_var_to_reg(string name, int reg_id) {
  if (name.empty()) {
    throw runtime_error("Var name is empty.");
  }
  assert(used_reg[reg_id] == 1);
  if (reg_to_var.find(reg_id) != reg_to_var.end())
    var_to_reg.erase(reg_to_var[reg_id]);
  reg_to_var.insert({reg_id, name});
  var_to_reg.insert({name, reg_id});
}

int ContextASM::get_new_reg_for(string name) {
  if (var_to_reg.find(name) != var_to_reg.end()) {
    return var_to_reg[name];
  }
  int reg_id = get_new_reg();
  bind_var_to_reg(name, reg_id);
  return reg_id;
}

int ContextASM::get_specified_reg_for(string name, int reg_id) {
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

void ContextASM::load_imm(string reg, int value, ostream& out) {
  if (value >= 0 && value < 65536)
    out << "    MOV " << reg << ", #" << value << endl;
  else
    out << "    MOV32 " << reg << ", " << value << endl;
};

void ContextASM::load(string reg, OpName op, ostream& out) {
  if (op.type == OpName::Type::Imm) {
    load_imm(reg, op.value, out);
  } else if (op.type == OpName::Type::Var) {
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
              out << "    MOV32 r11, " << offset << endl;
              out << "    ADD r11, sp, r11" << endl;
              out << "    LDR " << reg << ", [r11,#0]" << endl;
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
          out << "    MOV32 r11, " << offset << endl;
          out << "    ADD r11, sp, r11" << endl;
          out << "    LDR " << reg << ", [r11,#0]" << endl;
        }
      }
    }
  }
}

void ContextASM::store_to_stack_offset(string reg, int offset, ostream& out,
                                       string op) {
  if (!(offset > -4096 && offset < 4096)) {
    string tmp_reg = reg == "r11" ? "r12" : "r11";
    load_imm(tmp_reg, offset, out);
    out << "    ADD " << tmp_reg << ", sp, " << tmp_reg << endl;
    out << "    " << op << " " << reg << ", [" << tmp_reg << ",#0]" << endl;
  } else {
    out << "    " << op << " " << reg << ", [sp,#" << offset << "]" << endl;
  }
}

void ContextASM::load_from_stack_offset(string reg, int offset, ostream& out,
                                        string op) {
  if (!(offset > -4096 && offset < 4096)) {
    string tmp_reg = reg == "r11" ? "r12" : "r11";
    load_imm(tmp_reg, offset, out);
    out << "    ADD " << tmp_reg << ", sp, " << tmp_reg << endl;
    out << "    " << op << " " << reg << ", [" << tmp_reg << ",#0]" << endl;
  } else {
    out << "    " << op << " " << reg << ", [sp,#" << offset << "]" << endl;
  }
}

void ContextASM::store_to_stack(string reg, OpName op, ostream& out,
                                string op_code) {
  if (op.type != OpName::Type::Var) throw runtime_error("WTF");
  if (op.name[1] == '&') return;
  if (op.name[0] == '%') {
    store_to_stack_offset(reg, resolve_stack_offset(op.name), out, op_code);
  } else if (op.name[0] == '@') {
    out << "    MOV32 r11, " << rename(op.name) << endl;
    out << "    " << op_code << " " << reg << ", [r11,#0]" << endl;
  } else if (op.name[0] == '$') {
    int offset = resolve_stack_offset(op.name);
    store_to_stack_offset(reg, offset, out, op_code);
  }
}