// SPDX-License-Identifier: GPL-3.0-or-later
// panex_resume_old: run, stop and carry on Panex searches with the
// original's library, for `oracle.py panex-resume`, which checks that a
// search stopped in one program carries on in the other and that both
// save the same files (decision 0015). The counterpart is
// tests/tools/panex_resume.cpp (bt_panex_resume); keep the two in step.
//
//   panex_resume_old full   <puzzle> <problem> <workDir> <panex|stacking> <keepLimit> <diskBudget> <threads>
//   panex_resume_old pause  ... <stopAtDepth>
//   panex_resume_old resume ...
//
// pause stops the search once both ends are that many moves deep (it
// saves itself in <workDir>); resume carries on what <workDir> holds.
// Prints OUTCOME, FOUND, DEPTH, SAVED, INTERVAL, DISK, FOLDER and, for a
// path, MOVES and a STATE line per frame (each disc's x,z).

#include "lib/disassembly.h"
#include "lib/panex.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/stacking.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

int main(int argc, char** argv) {
  if (argc < 9) {
    std::fputs("usage: panex_resume_old full|pause|resume <puzzle> <problem> <workDir> "
               "<panex|stacking> <keepLimit> <diskBudget> <threads> [stopAtDepth]\n",
               stderr);
    return 2;
  }
  const std::string mode = argv[1];
  std::unique_ptr<puzzle_c> puz = puzzle_c::load(argv[2]);
  if (!puz) {
    std::fputs("could not load the puzzle\n", stderr);
    return 2;
  }
  const unsigned int prob = (unsigned int)std::atoi(argv[3]);
  if (prob >= puz->getNumberOfProblems()) {
    std::fputs("no such problem\n", stderr);
    return 2;
  }
  const problem_c& pr = *puz->getProblem(prob);
  panex::panexSearch_c search;
  search.workDir = argv[4];
  search.anyRules = std::strcmp(argv[5], "panex") != 0;
  search.keepLimit = std::strtoull(argv[6], nullptr, 10);
  search.diskBudget = std::strtoull(argv[7], nullptr, 10);
  search.threads = (unsigned int)std::atoi(argv[8]);
  search.autosaveMinutes = 0;
  if (mode == "pause") {
    if (argc < 10) {
      std::fputs("pause needs a depth\n", stderr);
      return 2;
    }
    search.stopAtDepth = std::strtoul(argv[9], nullptr, 10);
  } else if (mode == "resume") {
    search.resume = true;
  } else if (mode != "full") {
    std::fputs("unknown mode\n", stderr);
    return 2;
  }
  std::unique_ptr<separation_c> path = panex::solve(pr, search);
  std::printf("OUTCOME %d\n", (int)search.outcome);
  std::printf("FOUND %llu\n", (unsigned long long)search.found);
  std::printf("DEPTH %lu\n", search.depthReached);
  std::printf("SAVED %d\n", search.saved ? 1 : 0);
  std::printf("INTERVAL %u\n", search.levelInterval);
  std::printf("DISK %llu\n", (unsigned long long)search.diskBytes);
  std::printf("FOLDER %s\n", panex::searchFolder(pr, search.workDir).c_str());
  if (path) {
    std::printf("MOVES %u\n", stacking::logicalMoves(*path));
    for (unsigned int s = 0; s <= path->getMoves(); s++) {
      const state_c* st = path->getState(s);
      std::printf("STATE");
      for (unsigned int i = 0; i < path->getPieceNumber(); i++)
        std::printf(" %d,%d", st->getX(i), st->getZ(i));
      std::printf("\n");
    }
  }
  return 0;
}
