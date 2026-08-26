/*
 * syc-test -- minimal test fixture for syc.
 *
 * Discovers .sy cases in test/functional_test / test/performance_test,
 * compiles each with the syc-core compiler and executes the generated ARM
 * assembly with the in-process armsim-core interpreter, comparing the program
 * output plus exit code with the .out file.
 *
 * Usage:
 *   syc-test [functional|performance] [options]
 *
 * Options:
 *   --gcc           don't use the built-in armsim; run each case through the
 *                   external arm-linux-gnueabihf-gcc + qemu-arm pipeline
 *                   instead
 *   --jobs N        worker threads          (default: functional=8,
 *                                            performance=22, matching the
 *                                            former node driver)
 *   --timeout S     per-command timeout     (default: 15 functional,
 *                                            300 performance; applies to
 *                                            the external gcc/qemu pipeline
 *                                            only -- the in-process
 *                                            interpreter has no timeout)
 *
 * Output format matches the former node drivers: one green check or red cross
 * per case plus a "<N> pass"/"<N> fail" summary. Exit code is 0 iff every
 * case passed. Generated assembly is kept in %TEMP%/syc-test-* for debugging
 * whenever a case fails.
 *
 * Copyright (C) 2020-2021  nzh63, skywf21
 * Licensed under the GNU GPL version 3.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "asm.h"
#include "driver.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// small utilities
// ---------------------------------------------------------------------------
static const char* kGreen = "\x1B[32m";
static const char* kRed = "\x1B[31m";
static const char* kGray = "\x1B[38;5;242m";
static const char* kReset = "\x1B[0m";
static std::mutex g_io_mutex;

std::string read_file_or(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// JS String.prototype.trim() equivalent.
std::string trim_copy(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n\f\v");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n\f\v");
  return s.substr(a, b - a + 1);
}

// The repo is checked out with different EOL conventions per platform.
std::string norm(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    if (c != '\r') out.push_back(c);
  return trim_copy(out);
}

fs::path exe_directory() {
#ifdef _WIN32
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return fs::path(buf).parent_path();
#else
  std::error_code ec;
  auto self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return self.parent_path();
  return fs::current_path();
#endif
}

long get_pid() {
#ifdef _WIN32
  return (long)GetCurrentProcessId();
#else
  return (long)getpid();
#endif
}

void print_indented(const std::string& text, const char* pad) {
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) printf("%s%s\n", pad, line.c_str());
}

// ---------------------------------------------------------------------------
// child process plumbing
// ---------------------------------------------------------------------------
struct ChildResult {
  int exit_code = -1;
  bool timed_out = false;
  std::string out;
  std::string err;
};

#ifdef _WIN32

static void read_pipe_to_string(HANDLE h, std::string* dst) {
  char buf[4096];
  DWORD got = 0;
  while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0)
    dst->append(buf, got);
  CloseHandle(h);
}

ChildResult run_child(const std::vector<std::string>& args,
                      const std::string& input, const fs::path& cwd,
                      unsigned timeout_ms) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE in_r{}, in_w{}, out_r{}, out_w{}, err_r{}, err_w{};
  CreatePipe(&in_r, &in_w, &sa, 0);
  CreatePipe(&out_r, &out_w, &sa, 0);
  CreatePipe(&err_r, &err_w, &sa, 0);
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  PROCESS_INFORMATION pi{};

  std::vector<char> cmdline_buf;  // CreateProcessA may write to its lpCommandLine
  {
    std::string cmdline;
    for (size_t i = 0; i < args.size(); i++) {
      if (i) cmdline += ' ';
      cmdline += '"' + args[i] + '"';
    }
    cmdline_buf.assign(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');
  }
  std::string cwd_s = cwd.string();
  ChildResult res;
  if (!CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, cwd.empty() ? nullptr : cwd_s.c_str(), &si,
                      &pi)) {
    // no child was created: we own every pipe end
    CloseHandle(in_r);
    CloseHandle(in_w);
    CloseHandle(out_r);
    CloseHandle(out_w);
    CloseHandle(err_r);
    CloseHandle(err_w);
    res.err = "cannot spawn " + args[0];
    return res;
  }

  // parent's copies of the child-side ends are no longer needed
  CloseHandle(in_r);
  CloseHandle(out_w);
  CloseHandle(err_w);

  std::thread out_t(read_pipe_to_string, out_r, &res.out);
  std::thread err_t(read_pipe_to_string, err_r, &res.err);
  std::thread in_t([&] {  // feed stdin asynchronously
    const char* p = input.data();
    size_t left = input.size();
    while (left > 0) {
      DWORD wrote = 0;
      if (!WriteFile(in_w, p, (DWORD)std::min<size_t>(left, 32 << 10), &wrote,
                     nullptr) ||
          wrote == 0)
        break;
      p += wrote;
      left -= wrote;
    }
    CloseHandle(in_w);
  });

  DWORD wait_ms = timeout_ms == 0 ? INFINITE : (DWORD)timeout_ms;
  if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    res.timed_out = true;
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  res.exit_code = (int)(code & 0xFF);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  in_t.join();   // closes in_w
  out_t.join();  // closes out_r
  err_t.join();  // closes err_r
  return res;
}

void enable_ansi_colors() {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode))
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

#else  // POSIX

namespace {
void read_fd_to_string(int fd, std::string* dst) {
  char buf[4096];
  ssize_t got;
  while ((got = read(fd, buf, sizeof(buf))) > 0) dst->append(buf, got);
  close(fd);
}
}  // namespace

ChildResult run_child(const std::vector<std::string>& args,
                      const std::string& input, const fs::path& cwd,
                      unsigned timeout_ms) {
  int in_p[2], out_p[2], err_p[2];
  pipe(in_p);
  pipe(out_p);
  pipe(err_p);

  ChildResult res;
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = fork();
  if (pid < 0) {
    res.err = "fork failed";
    return res;
  }
  if (pid == 0) {
    dup2(in_p[0], STDIN_FILENO);
    dup2(out_p[1], STDOUT_FILENO);
    dup2(err_p[1], STDERR_FILENO);
    signal(SIGPIPE, SIG_DFL);
    if (!cwd.empty()) chdir(cwd.string().c_str());
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(in_p[0]);
  close(out_p[1]);
  close(err_p[1]);

  std::thread out_t(read_fd_to_string, out_p[0], &res.out);
  std::thread err_t(read_fd_to_string, err_p[0], &res.err);
  std::thread in_t([&] {
    size_t off = 0;
    while (off < input.size()) {
      ssize_t w = write(in_p[1], input.data() + off, input.size() - off);
      if (w <= 0) break;
      off += (size_t)w;
    }
    close(in_p[1]);
  });

  auto deadline =
      timeout_ms == 0 ? std::chrono::steady_clock::time_point::max()
                      : std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(timeout_ms);
  int status = 0;
  bool done = false;
  for (;;) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      done = true;
      break;
    }
    if (r < 0) break;
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      res.timed_out = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!done && !res.timed_out) waitpid(pid, &status, 0);

  res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 255;
  close(in_p[1]);
  in_t.join();
  out_t.join();
  err_t.join();
  close(out_p[0]);
  close(err_p[0]);
  return res;
}

void enable_ansi_colors() {}

#endif

// ---------------------------------------------------------------------------
// test harness
// ---------------------------------------------------------------------------
struct CaseResult {
  std::string opt;
  bool pass = false;
  std::string expect;
  std::string got;
  std::string stderr_text;
};

struct ModeConfig {
  fs::path dir;
  std::vector<std::string> opts;
  int jobs;
  unsigned timeout_ms;
};

ModeConfig config_for(const std::string& mode) {
  if (mode == "performance")
    return {"performance_test", {"-O2"}, 22, 300000};
  return {"functional_test", {"-O0", "-O0 -g", "-O2", "-O2 -g"}, 8, 15000};
}

int main(int argc, char** argv) {
  enable_ansi_colors();

  std::string mode = "functional";
  ModeConfig cfg{};
  bool use_gcc = false;
  long jobs_override = -1, timeout_override = -1;

  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next_value = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        fprintf(stderr, "syc-test: %s needs an argument\n", name);
        exit(2);
      }
      return argv[++i];
    };
    if (a == "--gcc")
      use_gcc = true;
    else if (a == "--jobs")
      jobs_override = atol(next_value("--jobs").c_str());
    else if (a == "--timeout")
      timeout_override = atol(next_value("--timeout").c_str());
    else if (a == "-h" || a == "--help") {
      fprintf(stderr, "usage: syc-test [functional|performance]\n"
                      "                [--gcc] [--jobs N] [--timeout SECONDS]\n");
      return 0;
    } else {
      positional.push_back(a);
    }
  }
  if (!positional.empty()) mode = positional[0];
  cfg = config_for(mode);

  // walk up from the executable until we find the repo's test directory
  fs::path repo_dir = exe_directory();
  while (!repo_dir.empty() &&
         !fs::exists(repo_dir / "test" / config_for("functional").dir))
    repo_dir = repo_dir.parent_path();
  fs::path tests_root = repo_dir / "test";
  fs::path test_dir = tests_root / cfg.dir;
  if (!fs::exists(test_dir)) {
    fprintf(stderr, "syc-test: test directory not found: %s\n",
            test_dir.string().c_str());
    fprintf(stderr, "syc-test: (looked upward from %s)\n",
            exe_directory().string().c_str());
    return 2;
  }

  if (jobs_override >= 0) cfg.jobs = (int)jobs_override;
  if (timeout_override >= 0) cfg.timeout_ms = (unsigned)timeout_override * 1000u;

  // scratch directory for generated assembly; kept when something fails so
  // the reported asm-file paths remain inspectable
  fs::path tmp_dir =
      fs::temp_directory_path() /
      ("syc-test-" + std::to_string(get_pid()));
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  // discover cases sorted by name; keep full paths without the .sy suffix
  std::vector<fs::path> bases;
  for (auto& e : fs::directory_iterator(test_dir)) {
    if (e.path().extension() != ".sy") continue;
    fs::path base = e.path();
    base.replace_extension("");
    bases.push_back(base);
  }
  std::sort(bases.begin(), bases.end());

  std::atomic<size_t> cursor{0};
  std::atomic<int> pass_count{0}, fail_count{0};
  std::atomic<bool> any_fail{false};

  auto worker = [&]() {
    for (;;) {
      size_t i = cursor.fetch_add(1);
      if (i >= bases.size()) break;
      const fs::path base = bases[i];
      auto begin_time = std::chrono::steady_clock::now();

      std::string expect_out = read_file_or(base.string() + ".out");
      std::string stdin_data;
      if (fs::exists(base.string() + ".in"))
        stdin_data = read_file_or(base.string() + ".in") + "\n";

      std::vector<CaseResult> results;
      results.reserve(cfg.opts.size());
      for (const auto& opt : cfg.opts) {
        CaseResult cr;
        cr.opt = opt;
        cr.expect = norm(expect_out);

        std::string asm_file = (tmp_dir / (base.filename().string() + opt + ".s")).string();

        auto fail_now = [&]() {
          cr.pass = false;
          results.push_back(cr);
        };

        // 1. compile SysY -> ARM assembly in-process with syc-core
        // (serialized: the compiler uses global config/parser state).
        int comp_code = 0;
        {
          std::vector<std::string> cargs = {"syc", base.string() + ".sy"};
          {  // split e.g. "-O0 -g" into real argv tokens
            std::istringstream opt_ss(opt);
            std::string tok;
            while (opt_ss >> tok) cargs.push_back(tok);
          }
          cargs.push_back("-o");
          cargs.push_back(asm_file);
          std::vector<char*> cargv;
          for (auto& a : cargs) cargv.push_back(const_cast<char*>(a.c_str()));
          cargv.push_back(nullptr);
          fflush(stdout);
          comp_code = syc::compile_main((int)cargv.size() - 1, cargv.data());
          fflush(stderr);
        }
        if (comp_code != 0) {
          cr.got = std::to_string(comp_code & 0xFF);
          fail_now();
          continue;
        }

        // 2. execute
        ChildResult run;
        if (use_gcc) {
          ChildResult link = run_child(
              {"arm-linux-gnueabihf-gcc", "-march=armv7-a", asm_file, "-L.",
               "-lsysy", "-o", asm_file + ".out", "-static", "-g"},
              "", test_dir, cfg.timeout_ms);
          if (link.timed_out || link.exit_code != 0) {
            cr.got = trim_copy(norm(link.out) + '\n' +
                               std::to_string(link.exit_code & 0xFF));
            cr.stderr_text = link.err;
            fail_now();
            continue;
          }
          run = run_child({asm_file + ".out"}, stdin_data, {}, cfg.timeout_ms);
        } else {
          try {
            std::ifstream in(asm_file, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open " + asm_file);
            armsim::Program prog = armsim::parse(in);
            armsim::RunResult rr =
                armsim::run_buffered(prog, stdin_data);
            run.out = rr.out;
            run.exit_code = rr.exit_code;
          } catch (const std::exception& e) {
            run.exit_code = 125;
            run.err = e.what();
          }
        }

        // 3. compare "<stdout>\n<exit code>" with the .out expectation
        cr.got =
            trim_copy(norm(run.out) + '\n' + std::to_string(run.exit_code & 0xFF));
        cr.stderr_text = run.err;
        cr.pass = cr.expect == cr.got;
        results.push_back(cr);
      }

      long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin_time)
                    .count();
      bool pass = std::all_of(results.begin(), results.end(),
                              [](const CaseResult& c) { return c.pass; });

      std::lock_guard<std::mutex> lock(g_io_mutex);
      if (pass) {
        ++pass_count;
        // 用例耗时：灰色显示，超过 1 秒标红
        std::string ms_note =
            (ms >= 1000 ? std::string(kRed) : std::string(kGray)) + " (" +
            std::to_string(ms) + "ms)";
        printf(" \x1B[32m\xe2\x9c\x93%s %s.sy%s%s\n", kReset,
               base.string().c_str(), ms_note.c_str(), kReset);
        fflush(stdout);
      } else {
        ++fail_count;
        any_fail = true;
        printf(" \x1B[31m\xe2\x9c\x97 %s.sy\x1B[0m\n",
               base.string().c_str());
        for (const auto& cr : results) {
          if (cr.pass) continue;
          printf("  %s\n", cr.opt.c_str());
          printf("    asm file: %s\n",
                 (tmp_dir / (base.filename().string() + cr.opt + ".s"))
                     .string()
                     .c_str());
          printf("    expect:\n");
          print_indented(cr.expect, "      ");
          printf("    get:\n");
          print_indented(cr.got, "      ");
          std::string serr = trim_copy(cr.stderr_text);
          if (!serr.empty()) {
            printf("    stderr:\n");
            print_indented(serr, "      ");
          }
        }
        fflush(stdout);
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve((size_t)std::max(1, cfg.jobs));
  for (int i = 0; i < std::max(1, cfg.jobs); i++) threads.emplace_back(worker);
  for (auto& t : threads) t.join();

  if (!any_fail) {
    std::error_code e;
    fs::remove_all(tmp_dir, e);
  }

  int n_pass = pass_count.load(), n_fail = fail_count.load();
  printf("\n");
  if (n_pass > 0) printf("    \x1B[32m%d pass\x1B[0m\n", n_pass);
  if (n_fail > 0) printf("    \x1B[31m%d fail\x1B[0m\n", n_fail);
  return n_fail == 0 ? 0 : 1;
}
