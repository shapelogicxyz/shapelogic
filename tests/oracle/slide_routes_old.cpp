// SPDX-License-Identifier: GPL-3.0-or-later
// slide_routes_old <file>...: for each saved solution of problem 0 of each
// sliding puzzle, the route of every move (sliding::slideRoute: the pieces
// that move and the corner points) and where the solution slider put the
// pieces at quarter steps once the routes were applied (disasmToMoves_c
// with applySlideRoutes, as the GUI's player drove it). Built by
// `oracle.py record-slide-routes` against the original's
// build/libburr_core.a; its output is tests/data/slide_routes.txt, which
// tests/unit/solve/test_slide_routes.cpp compares with the new core.

#include "lib/disasmtomoves.h"
#include "lib/disassembly.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/sliding.h"
#include "lib/solution.h"
#include "lib/voxel.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
  try {
    for (int f = 1; f < argc; f++) {
      std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[f]);
      const problem_c* pr = p->getProblem(0);
      std::printf("FILE %s\n", std::filesystem::path(argv[f]).filename().string().c_str());
      for (unsigned int s = 0; s < pr->getNumberOfSavedSolutions(); s++) {
        const separation_c* sep = pr->getSavedSolution(s)->getDisassembly();
        if (!sep)
          continue;
        std::printf("SOLUTION %u moves %u\n", s, sep->getMoves());
        for (unsigned int step = 0; step < sep->getMoves(); step++) {
          std::vector<unsigned int> movers;
          const std::vector<std::pair<int, int>> route =
              sliding::slideRoute(*pr, *sep, step, &movers);
          std::printf("ROUTE %u movers", step);
          for (unsigned int m : movers)
            std::printf(" %u", m);
          std::printf(" corners");
          for (const auto& c : route)
            std::printf(" %d,%d", c.first, c.second);
          std::printf("\n");
        }
        // the player's driver: the size and settings of the solution slider
        disasmToMoves_c mv(sep, 2 * getResultShape(*pr)->getBiggestDimension(),
                           pr->getNumberOfPieces());
        sliding::applySlideRoutes(*pr, *sep, mv);
        for (unsigned int q = 0; q <= 4 * sep->getMoves(); q++) {
          mv.setStep((float)q / 4.0f, false, true);
          std::printf("AT %u/4", q);
          for (unsigned int i = 0; i < pr->getNumberOfPieces(); i++)
            std::printf(" %.3f,%.3f,%.3f,%.3f", mv.getX(i), mv.getY(i), mv.getZ(i), mv.getA(i));
          std::printf("\n");
        }
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
