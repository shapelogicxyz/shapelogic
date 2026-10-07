// SPDX-License-Identifier: GPL-3.0-or-later
// bt_solve_file [options] <in> <out>: solves one problem of a puzzle file
// with a SolveSession and writes the solved file, as the original's
// burrTxt2 (which runs its solve thread) does, for `oracle.py
// compare-files`.
//
//   -d            take apart; only what comes apart is saved, sorted by
//                 moves (the solve thread's default sort), the most first
//   --rotations   with 90 degree turns
//   --solver T    classic, crowell or bt2
//   --restart     drop what the file found before (burrTxt2 -R)
//   -b N          the problem (default 0)
//   -t N          threads (default all)
//   --limit N     keep the N best solutions (the solve thread's 10, the
//                 default); 0 keeps all
//
// Without --restart a paused solve in the file carries on, as burrTxt2
// does. A solve that ends in an error prints "error in solver" and exits
// with 1 (burrTxt2's ACT_ERROR); the file is written all the same.

#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/solve_session.h"
#include "bt/solve/solver_type.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

bool parseCount(const char* s, unsigned int* out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != 0 || v < 0) {
    return false;
  }
  *out = static_cast<unsigned int>(v);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool restart = false;
  unsigned int problemNr = 0;
  bt::SolveOptions options;  // reduce, sorted by moves, the 10 best: burrTxt2's
  const char* in = nullptr;
  const char* out = nullptr;

  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    if (a == "-d") {
      options.disassemble = true;
    } else if (a == "--rotations") {
      options.checkRotations = true;
    } else if (a == "--restart") {
      restart = true;
    } else if (a == "--solver" && i + 1 < argc) {
      const auto t = bt::solverTypeFromName(argv[++i]);
      if (!t) {
        std::fprintf(stderr, "unknown solver %s\n", argv[i]);
        return 2;
      }
      options.solver = *t;
    } else if ((a == "-b" || a == "-t" || a == "--limit") && i + 1 < argc) {
      unsigned int v = 0;
      if (!parseCount(argv[i + 1], &v)) {
        std::fprintf(stderr, "%s needs a number\n", argv[i]);
        return 2;
      }
      (a == "-b" ? problemNr : a == "-t" ? options.threads : options.solutionLimit) = v;
      i++;
    } else if (in == nullptr) {
      in = argv[i];
    } else if (out == nullptr) {
      out = argv[i];
    } else {
      std::fputs("usage: bt_solve_file [options] <in> <out>\n", stderr);
      return 2;
    }
  }
  if (in == nullptr || out == nullptr) {
    std::fputs("usage: bt_solve_file [options] <in> <out>\n", stderr);
    return 2;
  }

  bt::Result<bt::Puzzle> loaded = bt::io::loadPuzzle(in);
  if (!loaded) {
    std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
    return 2;
  }
  bt::Puzzle& puzzle = *loaded;
  if (problemNr >= puzzle.getNumberOfProblems()) {
    std::fprintf(stderr, "there is no problem %u\n", problemNr);
    return 2;
  }

  bt::Problem& problem = puzzle.getProblem(problemNr);
  int exitCode = 0;

  if (restart) {
    problem.removeAllSolutions();
  }

  if (!bt::canStartSolving(problem)) {
    std::printf("problem %u is solved already or has nothing to go on from\n", problemNr);
  } else {
    bt::SolveSession session(puzzle, problem, options);
    if (const bt::Result<void> r = session.resume(); !r) {
      std::fprintf(stderr, "%s\n", r.error().message.c_str());
      return 2;
    }
    session.wait();
    if (session.state() == bt::SolveState::Error) {
      std::puts("error in solver");
      if (const auto f = session.failure()) {
        std::fprintf(stderr, "%s\n", f->message.c_str());
      }
      exitCode = 1;
    }
  }

  const bt::Result<void> saved = bt::io::savePuzzle(puzzle, out);
  if (!saved) {
    std::fprintf(stderr, "save failed: %s\n", saved.error().message.c_str());
    return 2;
  }
  return exitCode;
}
