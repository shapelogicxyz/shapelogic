// SPDX-License-Identifier: GPL-3.0-or-later
// stack_solve_old <in> <out> [-d] [--panex] [--count-only] [--high-memory]
//                 [--problem n]:
// solves a problem of a stacking puzzle with the original's solve thread
// (solveThread_c, as the GUI's Solve ran it: the hotspots set as Solve
// set them, PAR_PANEX_RESUME, the Panex Solver when asked) and saves the
// puzzle. burrTxt2 has no stacking solver: the solve thread is what solved
// such puzzles. The harness points BURRTOOLS_PANEX_DIR at a folder of its
// own. Built by `oracle.py baseline-files --tier stacking` against the
// original's build/libburr_core.a; `bt --save` is the new side.

#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solvethread.h"
#include "lib/stacking.h"
#include "lib/voxel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: stack_solve_old <in> <out> [-d] [--panex] [--count-only] [--high-memory]"
               " [--problem n]\n",
               stderr);
    return 2;
  }
  int par = solveThread_c::PAR_REDUCE | solveThread_c::PAR_PANEX_RESUME |
            solveThread_c::PAR_PANEX_NO_AUTOSAVE;
  unsigned int prob = 0;
  for (int i = 3; i < argc; i++) {
    if (std::strcmp(argv[i], "-d") == 0)
      par |= solveThread_c::PAR_DISASSM;
    else if (std::strcmp(argv[i], "--panex") == 0)
      par |= solveThread_c::PAR_PANEX_SOLVER;
    else if (std::strcmp(argv[i], "--count-only") == 0)
      par |= solveThread_c::PAR_JUST_COUNT;
    else if (std::strcmp(argv[i], "--high-memory") == 0)
      par |= solveThread_c::PAR_HIGH_MEMORY;
    else if (std::strcmp(argv[i], "--problem") == 0 && i + 1 < argc)
      prob = (unsigned int)std::atoi(argv[++i]);
    else {
      std::fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 2;
    }
  }
  try {
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[1]);
    if (prob >= p->getNumberOfProblems()) {
      std::fputs("no such problem\n", stderr);
      return 2;
    }
    // cb_BtnStart, then cb_BtnCont's stacking branch
    for (unsigned int i = 0; i < p->getNumberOfShapes(); i++)
      p->getShape(i)->initHotspot();
    for (unsigned int i = 0; i < p->getNumberOfShapes(); i++)
      stacking::ensureHotspot(p->getShape(i));
    problem_c* problem = p->getProblem(prob);
    problem->removeAllSolutions();
    solveThread_c t(*problem, par);
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
