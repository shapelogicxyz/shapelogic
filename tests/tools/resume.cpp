// SPDX-License-Identifier: GPL-3.0-or-later
// bt_resume: pause and continue assembler 0 searches with the new core, for
// `oracle.py resume-check`. The counterpart of tests/oracle/resume_old.cpp,
// which does the same with the original's library; see there for the
// commands and the output. Keep the two in step.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/assembly.h"
#include "bt/solve/assembler.h"
#include "bt/solve/solver_type.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

std::string fingerprint(const bt::Assembly& a) {
  std::string s;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (a.isPlaced(i)) {
      s += std::to_string(a.getTransformation(i)) + "," + std::to_string(a.getX(i)) + "," +
           std::to_string(a.getY(i)) + "," + std::to_string(a.getZ(i)) + ";";
    } else {
      s += "-;";
    }
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fputs("usage: bt_resume full|pause|resume <puzzle> <threads> <plain> <reduce> [arg]\n",
               stderr);
    return 2;
  }
  const std::string cmd = argv[1];
  const auto threads = static_cast<unsigned int>(std::atoi(argv[3]));
  const bool plain = std::atoi(argv[4]) != 0;
  const bool reduce = std::atoi(argv[5]) != 0;

  bt::Result<bt::Puzzle> p = bt::io::loadPuzzle(argv[2]);
  if (!p) {
    std::fprintf(stderr, "cannot load %s: %s\n", argv[2], p.error().message.c_str());
    return 2;
  }
  const bt::Problem& problem = p->getProblem(0);
  auto assm = bt::makeAssembler(*p, problem, bt::SolverType::Classic);
  if (!assm || assm->createMatrix({}) != bt::AssemblerError::None) {
    std::fputs("createMatrix failed\n", stderr);
    return 2;
  }
  if (reduce) {
    (void)assm->reduce(bt::StopToken{});
  }

  if (cmd == "resume") {
    if (argc < 7) {
      return 2;
    }
    std::ifstream in(argv[6]);
    std::string version;
    std::string text;
    std::getline(in, version);
    std::getline(in, text);
    const bt::AssemblerError err = assm->restorePosition(text, version);
    if (err != bt::AssemblerError::None) {
      std::printf("ERR %d\n", static_cast<int>(err));
      return 0;
    }
  }

  unsigned long stopAfter = 0;
  if (cmd == "pause") {
    if (argc < 7) {
      return 2;
    }
    stopAfter = std::strtoul(argv[6], nullptr, 10);
  }

  bt::ThreadPool pool(threads > 0 ? threads : 1);
  bt::SearchOptions options;
  options.threads = threads;
  options.pool = &pool;
  options.vectorSearch = !plain;

  unsigned long count = 0;
  const bt::RunEnd end = assm->assemble(
      [&count, stopAfter](bt::Assembly a) {
        std::printf("A %s\n", fingerprint(a).c_str());
        count++;
        return stopAfter == 0 || count < stopAfter;
      },
      options, bt::StopToken{});

  if (cmd == "pause") {
    const bt::SavedPosition pos = assm->savePosition();
    std::printf("VERSION %s\n", pos.version.c_str());
    std::printf("TEXT %s\n", pos.text.c_str());
  }
  std::printf("FINISHED %d\n", end == bt::RunEnd::Finished ? 1 : 0);
  return 0;
}
