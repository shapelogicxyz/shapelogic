// SPDX-License-Identifier: GPL-3.0-or-later
// The take-apart pool: assemblies taken apart on several threads give the
// results of one disassembler, in the order they were handed over; and the
// analysis's vector kernels give the moves of its scalar loops.

#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/base/stop.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/simd_bitset.h"
#include "shapelogic/solve/solver_type.h"
#include "shapelogic/solve/take_apart_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bt;

namespace {

// Every assembly of problem 0, found on one thread (so in a fixed order).
std::vector<Assembly> allAssemblies(Puzzle& puzzle) {
  const Problem& pr = puzzle.getProblem(0);
  std::unique_ptr<Assembler> assm = makeAssembler(puzzle, pr, SolverType::Classic);
  REQUIRE(assm->createMatrix({}) == AssemblerError::None);
  (void)assm->reduce(StopToken{});
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }
  std::vector<Assembly> res;
  SearchOptions o;
  o.threads = 1;
  REQUIRE(assm->assemble(
              [&res](const Assembly& a) {
                res.push_back(a);
                return true;
              },
              o, StopToken{}) == RunEnd::Finished);
  return res;
}

// An assembly and what its take-apart found, as text.
std::string resultText(const Assembly& a, const TakeApartResult& r) {
  std::string text = test::fingerprint(a) + " ";
  if (!r.separation) {
    return text + (r.stopped ? "stopped" : "none");
  }
  return text + r.separation->movesText();
}

std::vector<std::string> oneAfterAnother(const Puzzle& puzzle, const std::vector<Assembly>& all,
                                         const DisassemblerOptions& options) {
  Disassembler d(puzzle, puzzle.getProblem(0), options);
  std::vector<std::string> texts;
  texts.reserve(all.size());
  for (const Assembly& a : all) {
    texts.push_back(resultText(a, d.disassemble(a)));
  }
  return texts;
}

std::vector<std::string> sideBySide(const Puzzle& puzzle, const std::vector<Assembly>& all,
                                    const TakeApartPoolOptions& options) {
  std::vector<std::string> texts;
  texts.reserve(all.size());
  TakeApartPool pool(puzzle, puzzle.getProblem(0), options,
                     [&texts](const Assembly& a, TakeApartResult& r) {
                       texts.push_back(resultText(a, r));
                     });
  for (const Assembly& a : all) {
    pool.submit(a);
  }
  pool.finish();
  CHECK(pool.submitted() == all.size());
  CHECK(pool.delivered() == all.size());
  return texts;
}

}  // namespace

// Bermuda's result has a symmetry breaker (AGENTS.md: the parallel cases
// need one); Solid Six Piece Burrs and Demo Piece Generator have hundreds
// of small take-aparts, more than the pool holds unfinished at once;
// Pelikan Burr has a long one among short ones (the others wait for it);
// Hex Sticks is taken apart with the derived checks.
TEST_CASE("take-apart pool: the results of one disassembler, in the order handed over",
          "[solve][disasm][parallel]") {
  for (const char* file : {"Bermuda.xmpuzzle", "SolidSixPieceBurrs.xmpuzzle",
                           "DemoPieceGenerator.xmpuzzle", "PelikanBurr.xmpuzzle",
                           "HexSticks.xmpuzzle"}) {
    INFO(file);
    Puzzle puzzle = test::loadFixture(file);
    puzzle.getProblem(0).removeAllSolutions();
    const std::vector<Assembly> all = allAssemblies(puzzle);
    REQUIRE(!all.empty());

    const std::vector<std::string> expected = oneAfterAnother(puzzle, all, {});

    for (const unsigned int workers : {1U, 3U, 8U}) {
      INFO(workers << " workers");
      TakeApartPoolOptions plain;
      plain.workers = workers;
      CHECK(sideBySide(puzzle, all, plain) == expected);

      // beside a search with a thread limit, and with threads for the levels
      ThreadPool levels(4);
      TakeApartPoolOptions limited;
      limited.workers = workers;
      limited.besideSearch = 1;
      limited.searchCores = 3;
      limited.takeApart.pool = &levels;
      limited.takeApart.levelThreadCostUs = 0;
      CHECK(sideBySide(puzzle, all, limited) == expected);
    }
  }
}

TEST_CASE("take-apart pool: an exception of the result function comes out of finish()",
          "[solve][disasm][parallel]") {
  Puzzle puzzle = test::loadFixture("DemoPieceGenerator.xmpuzzle");
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> all = allAssemblies(puzzle);
  REQUIRE(all.size() > 600);

  TakeApartPoolOptions options;
  options.workers = 4;
  std::size_t seen = 0;
  TakeApartPool pool(puzzle, puzzle.getProblem(0), options,
                     [&seen](const Assembly&, TakeApartResult&) {
                       if (++seen == 100) {
                         throw std::runtime_error("the hundredth");
                       }
                     });
  // more than the pool holds unfinished: a submit() that waits must wake
  for (const Assembly& a : all) {
    pool.submit(a);
  }
  CHECK_THROWS_AS(pool.finish(), std::runtime_error);
  CHECK(seen == 100);
}

TEST_CASE("take-apart pool: destroyed without finish(), it drops what is left",
          "[solve][disasm][parallel]") {
  Puzzle puzzle = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> all = allAssemblies(puzzle);

  std::size_t seen = 0;
  {
    TakeApartPoolOptions options;
    options.workers = 2;
    TakeApartPool pool(puzzle, puzzle.getProblem(0), options,
                       [&seen](const Assembly&, TakeApartResult&) { seen++; });
    for (std::size_t i = 0; i < 50 && i < all.size(); i++) {
      pool.submit(all[i]);
    }
  }
  CHECK(seen <= 50);
}

// The fast analysis closes its matrix and makes its bit masks with vector
// instructions where there are any; the scalar loops give the same trees.
TEST_CASE("take-apart switches: the analysis's vector kernels change nothing",
          "[solve][disasm]") {
  for (const char* file : {"PelikanBurr.xmpuzzle", "SolidSixPieceBurrs.xmpuzzle",
                           "CubeInCage.xmpuzzle", "Prisgon.xmpuzzle", "HexSticks.xmpuzzle"}) {
    INFO(file);
    Puzzle puzzle = test::loadFixture(file);
    puzzle.getProblem(0).removeAllSolutions();
    std::vector<Assembly> all = allAssemblies(puzzle);
    if (all.size() > 60) {
      all.erase(all.begin() + 60, all.end());
    }

    DisassemblerOptions scalar;
    scalar.analysis.kernels = SimdKernels::none();
    DisassemblerOptions reference;
    reference.analysis.fast = false;

    const std::vector<std::string> expected = oneAfterAnother(puzzle, all, reference);
    CHECK(oneAfterAnother(puzzle, all, {}) == expected);
    CHECK(oneAfterAnother(puzzle, all, scalar) == expected);
  }
}
