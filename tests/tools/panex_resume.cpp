// SPDX-License-Identifier: GPL-3.0-or-later
// bt_panex_resume: run, stop and carry on Panex searches with the new core,
// for `oracle.py panex-resume`. The counterpart of
// tests/oracle/panex_resume_old.cpp (the same arguments, the same output);
// keep the two in step.
//
//   bt_panex_resume full   <puzzle> <problem> <workDir> <panex|stacking> <keepLimit> <diskBudget> <threads>
//   bt_panex_resume pause  ... <stopAtDepth>
//   bt_panex_resume resume ...

#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

int main(int argc, char** argv) {
  if (argc < 9) {
    std::fputs("usage: bt_panex_resume full|pause|resume <puzzle> <problem> <workDir> "
               "<panex|stacking> <keepLimit> <diskBudget> <threads> [stopAtDepth]\n",
               stderr);
    return 2;
  }
  const std::string mode = argv[1];
  bt::Result<bt::Puzzle> puz = bt::io::loadPuzzle(argv[2]);
  if (!puz) {
    std::fprintf(stderr, "could not load the puzzle: %s\n", puz.error().message.c_str());
    return 2;
  }
  const auto prob = static_cast<unsigned int>(std::atoi(argv[3]));
  if (prob >= puz->getNumberOfProblems()) {
    std::fputs("no such problem\n", stderr);
    return 2;
  }
  const bt::Problem& pr = puz->getProblem(prob);
  bt::panex::PanexSearch search;
  search.workDir = argv[4];
  search.anyRules = std::strcmp(argv[5], "panex") != 0;
  search.keepLimit = std::strtoull(argv[6], nullptr, 10);
  search.diskBudget = std::strtoull(argv[7], nullptr, 10);
  search.threads = static_cast<unsigned int>(std::atoi(argv[8]));
  search.autosaveMinutes = 0;
  if (mode == "pause") {
    if (argc < 10) {
      std::fputs("pause needs a depth\n", stderr);
      return 2;
    }
    search.stopAtDepth = std::strtoull(argv[9], nullptr, 10);
  } else if (mode == "resume") {
    search.resume = true;
  } else if (mode != "full") {
    std::fputs("unknown mode\n", stderr);
    return 2;
  }
  const std::optional<bt::Separation> path = bt::panex::solve(*puz, pr, search);
  std::printf("OUTCOME %d\n", static_cast<int>(search.outcome));
  std::printf("FOUND %llu\n", static_cast<unsigned long long>(search.found));
  std::printf("DEPTH %llu\n", static_cast<unsigned long long>(search.depthReached));
  std::printf("SAVED %d\n", search.saved ? 1 : 0);
  std::printf("INTERVAL %u\n", search.levelInterval);
  std::printf("DISK %llu\n", static_cast<unsigned long long>(search.diskBytes));
  std::printf("FOLDER %s\n", bt::panex::searchFolder(*puz, pr, search.workDir).c_str());
  if (path) {
    std::printf("MOVES %u\n", bt::stacking::logicalMoves(*path));
    for (unsigned int s = 0; s <= path->getMoves(); s++) {
      const bt::State& st = path->getState(s);
      std::printf("STATE");
      for (unsigned int i = 0; i < path->getPieceNumber(); i++) {
        std::printf(" %d,%d", st.getX(i), st.getZ(i));
      }
      std::printf("\n");
    }
  }
  return 0;
}
