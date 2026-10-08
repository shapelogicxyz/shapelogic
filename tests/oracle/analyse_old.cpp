// SPDX-License-Identifier: GPL-3.0-or-later
// analyse_old <in> <out> [--rotations] [--crowell]: takes every saved
// assembly of every problem apart again with the original BurrTools
// library, as "Add disassembly" for all solutions did
// (mainWindow_c::cb_AddAllDisasm in src/gui/mainwindow_solve.cpp: one
// disassembler per problem from createDisassembler, a separation found
// replacing what the solution had), the old trees dropped first, and saves
// the file. Built by `oracle.py record-analysed` against the original's
// build/libburr_core.a; tests/unit/app/test_analysis_files.cpp compares
// the new core's solution.analyse with what it writes.

#include "lib/disassembler.h"
#include "lib/disassembler_factory.h"
#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solution.h"

#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: analyse_old <in.xmpuzzle> <out.xmpuzzle> [--rotations] [--crowell]\n", stderr);
    return 2;
  }
  bool rotations = false;
  solverType_e solver = SOLVER_CLASSIC;
  for (int i = 3; i < argc; i++) {
    if (std::strcmp(argv[i], "--rotations") == 0) rotations = true;
    if (std::strcmp(argv[i], "--crowell") == 0) solver = SOLVER_CROWELL;
  }
  try {
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[1]);
    if (p->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE) {
      for (unsigned int prob = 0; prob < p->getNumberOfProblems(); prob++) {
        problem_c* pr = p->getProblem(prob);
        if (pr->getNumberOfSavedSolutions() == 0) continue;
        std::unique_ptr<disassembler_c> dis = createDisassembler(*pr, rotations, solver);
        for (unsigned int sol = 0; sol < pr->getNumberOfSavedSolutions(); sol++) {
          // the moves dropped (the level summary stays), then found again
          pr->getSavedSolution(sol)->removeDisassembly();
          std::unique_ptr<separation_c> d = dis->disassemble(pr->getSavedSolution(sol)->getAssembly());
          if (d) pr->getSavedSolution(sol)->setDisassembly(std::move(d));
        }
      }
    }
    p->save(std::filesystem::path(argv[2]));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
