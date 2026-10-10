// SPDX-License-Identifier: GPL-3.0-or-later
// slide_solve_old <in> <out> [-d] [--deep|--full] [--nested] [--partial]
//                 [--high-memory] [--limit n]:
// solves problem 0 of a sliding puzzle with the original's solve thread
// (solveThread_c, as the GUI's Solve ran it: PAR_REDUCE and the sliding
// flags; one thread, so the starts come in one order) and saves the
// puzzle. burrTxt2 refuses -d on the sliding grid, which has no take-apart;
// the solve thread is what solved such puzzles. Built by `oracle.py
// baseline-files --tier sliding` against the original's
// build/libburr_core.a; `bt --save` is the new side.

#include "lib/helperpool.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solvethread.h"
#include "lib/voxel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: slide_solve_old <in> <out> [-d] [--deep|--full] [--nested] [--partial]"
               " [--high-memory] [--limit n]\n",
               stderr);
    return 2;
  }
  int par = solveThread_c::PAR_REDUCE;
  unsigned int limit = 10;
  for (int i = 3; i < argc; i++) {
    if (std::strcmp(argv[i], "-d") == 0)
      par |= solveThread_c::PAR_DISASSM;
    else if (std::strcmp(argv[i], "--deep") == 0)
      par |= solveThread_c::PAR_DEEP_SEARCH;
    else if (std::strcmp(argv[i], "--full") == 0)
      par |= solveThread_c::PAR_FULL_SEARCH;
    else if (std::strcmp(argv[i], "--nested") == 0)
      par |= solveThread_c::PAR_NESTED_SLIDES;
    else if (std::strcmp(argv[i], "--partial") == 0)
      par |= solveThread_c::PAR_NESTED_SLIDES | solveThread_c::PAR_PARTIAL_NESTED;
    else if (std::strcmp(argv[i], "--high-memory") == 0)
      par |= solveThread_c::PAR_HIGH_MEMORY;
    else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
      limit = (unsigned int)std::atoi(argv[++i]);
    else {
      std::fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 2;
    }
  }
  try {
    setSolveThreadLimit(1);
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[1]);
    for (unsigned int i = 0; i < p->getNumberOfShapes(); i++)
      p->getShape(i)->initHotspot();
    problem_c* problem = p->getProblem(0);
    problem->removeAllSolutions();
    solveThread_c t(*problem, par);
    t.setSolutionLimits(limit, 1);
    if (!t.start(false)) {
      std::fputs("could not start the solver\n", stderr);
      return 1;
    }
    t.waitUntilFinished();
    if (t.currentAction() != solveThread_c::ACT_FINISHED) {
      std::fprintf(stderr, "the solver ended in action %u\n", t.currentAction());
      return 1;
    }
    // the solver's note, as the GUI showed it
    std::printf("%s\n", t.getSolverNote().c_str());
    p->save(std::filesystem::path(argv[2]));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
