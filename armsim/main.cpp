/*
 * armsim -- command line entry point.
 *
 * Usage: armsim [--stats] [--max-steps N] program.s
 */
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "asm.h"

int main(int argc, char** argv) {
  armsim::RunOptions opts;
  const char* path = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--stats")) {
      opts.stats = true;
    } else if (!std::strcmp(argv[i], "--max-steps") && i + 1 < argc) {
      opts.max_steps = std::strtoull(argv[++i], nullptr, 0);
    } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
      std::cerr << "usage: armsim [--stats] [--max-steps N] program.s\n";
      return 2;
    } else {
      path = argv[i];
    }
  }

  try {
    armsim::Program prog;
    if (path == nullptr || !std::strcmp(path, "-")) {
      std::cin >> std::noskipws;
      prog = armsim::parse(std::cin);
    } else {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        std::cerr << "armsim: cannot open " << path << "\n";
        return 2;
      }
      prog = armsim::parse(in);
    }
    return armsim::run(prog, opts);
  } catch (const std::exception& e) {
    std::cerr << "armsim: " << e.what() << "\n";
    return 125;
  }
}
