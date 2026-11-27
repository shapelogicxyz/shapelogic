// SPDX-License-Identifier: GPL-3.0-or-later
// The take-apart pool: assemblies taken apart on several threads give the
// results of one disassembler, in the order they were handed over or each
// as it is there; a stop gives back what was not taken apart; and the
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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bt;

namespace {

// Every assembly of problem 0, found on one thread (so in a fixed order).
std::vector<Assembly> allAssemblies(Puzzle& puzzle) {
  const Problem& pr = puzzle.getProblem(0);
  std::unique_ptr<Assembler> assm = makeAssembler(puzzle, pr, SolverType::Classic);
  REQUIRE(assm->createMatrix({}, StopToken()) == AssemblerError::None);
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
                     [&texts](TakeApartTask& t, TakeApartResult& r) {
                       texts.push_back(resultText(t.assembly, r));
                     });
  for (const Assembly& a : all) {
    pool.submit(a);
  }
  pool.finish();
  CHECK(pool.submitted() == all.size());
  CHECK(pool.delivered() == all.size());
  // the workers are gone; what the disassemblers know can still be asked
  CHECK(pool.progress().empty());
  return texts;
}

// What came back for one assembly: handed over as number `number` (and as
// solution 2 * number + 1, to tell the two numbers apart).
struct Back {
  std::uint64_t number = 0;
  std::string text;
  bool stopped = false;
  std::uint64_t ns = 0;
};

// Every assembly through a pool that hands the results on as they come
// (from several threads at once: the result function locks), sorted by
// their numbers. `stop`, when given, is asked for once `stopAfter` results
// are back.
std::vector<Back> asTheyCome(const Puzzle& puzzle, const std::vector<Assembly>& all,
                             TakeApartPoolOptions options, StopSource* stop = nullptr,
                             std::size_t stopAfter = 0) {
  std::mutex mutex;
  std::vector<Back> back;
  bool numbersKept = true;
  options.ordered = false;
  TakeApartPool pool(
      puzzle, puzzle.getProblem(0), options,
      [&](TakeApartTask& t, TakeApartResult& r) {
        const std::lock_guard<std::mutex> lock(mutex);
        numbersKept = numbersKept && t.solutionNumber == 2 * t.assemblyNumber + 1;
        back.push_back(
            Back{t.assemblyNumber, resultText(t.assembly, r), r.stopped, t.takeApartNs});
        if (stop != nullptr && back.size() == stopAfter) {
          stop->request(StopKind::Pause);
        }
      },
      stop != nullptr ? stop->token() : StopToken{});
  if (stop != nullptr && stopAfter == 0) {
    stop->request(StopKind::Pause);
  }
  for (std::size_t i = 0; i < all.size(); i++) {
    pool.submit(all[i], i, 2 * i + 1);
  }
  pool.finish();
  CHECK(numbersKept);
  CHECK(pool.submitted() == all.size());
  CHECK(pool.delivered() == all.size());
  std::sort(back.begin(), back.end(),
            [](const Back& a, const Back& b) { return a.number < b.number; });
  return back;
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

// The solve session's way: no result waits for a long take-apart handed
// over before it, so they come in any order, each once, with the numbers
// it was handed over with.
TEST_CASE("take-apart pool: handed on as they come, every result once, with its numbers",
          "[solve][disasm][parallel]") {
  for (const char* file : {"Bermuda.xmpuzzle", "SolidSixPieceBurrs.xmpuzzle",
                           "DemoPieceGenerator.xmpuzzle", "PelikanBurr.xmpuzzle"}) {
    INFO(file);
    Puzzle puzzle = test::loadFixture(file);
    puzzle.getProblem(0).removeAllSolutions();
    const std::vector<Assembly> all = allAssemblies(puzzle);
    REQUIRE(!all.empty());

    const std::vector<std::string> expected = oneAfterAnother(puzzle, all, {});

    for (const unsigned int workers : {1U, 3U, 8U}) {
      INFO(workers << " workers");
      ThreadPool levels(4);
      TakeApartPoolOptions options;
      options.workers = workers;
      options.besideSearch = workers > 1 ? 2 : 1;
      options.searchCores = 2;
      options.takeApart.pool = &levels;
      const std::vector<Back> back = asTheyCome(puzzle, all, options);
      REQUIRE(back.size() == all.size());
      bool same = true;
      for (std::size_t i = 0; i < back.size(); i++) {
        same = same && back[i].number == i && back[i].text == expected[i] && !back[i].stopped;
      }
      CHECK(same);
    }
  }
}

// A stop loses nothing: what was taken apart by then comes back as it is,
// everything else as stopped (a take-apart cut short, or one not begun:
// no time spent on it), each once, whichever way the results are handed
// on; and more assemblies than the pool holds unfinished do not leave
// submit() waiting.
TEST_CASE("take-apart pool: a stop gives back what was not taken apart, as stopped",
          "[solve][disasm][parallel]") {
  Puzzle puzzle = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> all = allAssemblies(puzzle);
  REQUIRE(all.size() > 500);
  const std::vector<std::string> expected = oneAfterAnother(puzzle, all, {});

  for (const std::size_t stopAfter : {std::size_t{0}, std::size_t{1}, std::size_t{100}}) {
    INFO("stopped after " << stopAfter << " results");
    TakeApartPoolOptions options;
    options.workers = 4;
    StopSource stop;
    const std::vector<Back> back = asTheyCome(puzzle, all, options, &stop, stopAfter);
    REQUIRE(back.size() == all.size());
    std::size_t done = 0;
    bool right = true;
    for (std::size_t i = 0; i < back.size(); i++) {
      right = right && back[i].number == i;
      if (back[i].stopped) {
        right = right && back[i].text == test::fingerprint(all[i]) + " stopped";
      } else {
        right = right && back[i].text == expected[i];
        done++;
      }
    }
    CHECK(right);
    CHECK(done >= stopAfter);
    CHECK(done < all.size());
    if (stopAfter == 0) {
      // stopped before the first was handed over: none was begun
      CHECK(done == 0);
      CHECK(std::all_of(back.begin(), back.end(), [](const Back& b) { return b.ns == 0; }));
    }
  }

  // in order, too: the stopped ones keep their places
  StopSource stop;
  stop.request(StopKind::Pause);
  std::vector<std::uint64_t> order;
  std::size_t stopped = 0;
  TakeApartPoolOptions options;
  options.workers = 3;
  TakeApartPool pool(
      puzzle, puzzle.getProblem(0), options,
      [&](TakeApartTask& t, TakeApartResult& r) {
        order.push_back(t.assemblyNumber);
        stopped += r.stopped && !r.separation ? std::size_t{1} : std::size_t{0};
      },
      stop.token());
  for (std::size_t i = 0; i < all.size(); i++) {
    pool.submit(all[i], i);
  }
  pool.finish();
  REQUIRE(order.size() == all.size());
  CHECK(std::is_sorted(order.begin(), order.end()));
  CHECK(stopped == all.size());
}

TEST_CASE("take-apart pool: an exception of the result function comes out of finish()",
          "[solve][disasm][parallel]") {
  Puzzle puzzle = test::loadFixture("DemoPieceGenerator.xmpuzzle");
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> all = allAssemblies(puzzle);
  REQUIRE(all.size() > 600);

  for (const bool ordered : {true, false}) {
    INFO((ordered ? "in order" : "as they come"));
    TakeApartPoolOptions options;
    options.workers = 4;
    options.ordered = ordered;
    // whoever feeds the pool is told, once, on the thread it happened on
    std::atomic<unsigned int> told{0};
    options.onFailure = [&told] { told.fetch_add(1); };
    std::atomic<std::size_t> seen{0};
    TakeApartPool pool(puzzle, puzzle.getProblem(0), options,
                       [&seen](TakeApartTask&, TakeApartResult&) {
                         if (seen.fetch_add(1) + 1 == 100) {
                           throw std::runtime_error("the hundredth");
                         }
                       });
    // more than the pool holds unfinished: a submit() that waits must wake
    for (const Assembly& a : all) {
      pool.submit(a);
    }
    CHECK_THROWS_AS(pool.finish(), std::runtime_error);
    CHECK(told.load() == 1);
    if (ordered) {
      // one thread hands on at a time: nothing after the one that threw
      CHECK(seen.load() == 100);
    } else {
      CHECK(seen.load() >= 100);
      CHECK(seen.load() < all.size());
    }
  }
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
                       [&seen](TakeApartTask&, TakeApartResult&) { seen++; });
    for (std::size_t i = 0; i < 50 && i < all.size(); i++) {
      pool.submit(all[i]);
    }
  }
  CHECK(seen <= 50);

  // close() does the same, and may come after finish() or twice
  TakeApartPoolOptions options;
  options.workers = 2;
  std::size_t back = 0;
  TakeApartPool pool(puzzle, puzzle.getProblem(0), options,
                     [&back](TakeApartTask&, TakeApartResult&) { back++; });
  for (std::size_t i = 0; i < 50 && i < all.size(); i++) {
    pool.submit(all[i]);
  }
  pool.close();
  const std::size_t atClose = back;
  pool.close();
  CHECK(back == atClose);
  CHECK(back <= 50);
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
