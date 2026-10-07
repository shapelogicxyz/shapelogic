// SPDX-License-Identifier: GPL-3.0-or-later
// BurrTools 2: the dancing cells search and the assembler on it. The
// original's BurrTools 2 cases of test/test_solver.cpp come first (adapted
// to the new API: runs take a stop token and options, the work stealing is
// the assembler's own run with several threads); the cases after them test
// the rules of decision 0009 on it, and its saved positions.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "bt/base/thread_pool.h"
#include "bt/solve/assembler.h"
#include "bt/solve/assembler_0.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/dancing_cells.h"
#include "bt/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

using namespace bt;
using test::Fingerprints;
using test::runOnce;
using test::searchOptions;

namespace {

// A BurrTools 2 assembler for problem 0 of `p`, its matrix made.
std::unique_ptr<AssemblerBt2> made(const Puzzle& p, const MatrixOptions& options = {},
                                   bool reduce = false) {
  auto a = std::make_unique<AssemblerBt2>(p, p.getProblem(0));
  REQUIRE(a->createMatrix(options) == AssemblerError::None);
  if (reduce) {
    (void)a->reduce(StopToken{});
  }
  return a;
}

// What assembler 0 finds for problem 0 of `p`.
Fingerprints classic(const Puzzle& p) {
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  return runOnce(a, searchOptions(1, nullptr, false)).found;
}

}  // namespace

// ---- from the original's test_solver.cpp ---------------------------------------

// The twelve pentominoes fill a 3 x 20 tray in 2 ways and a 6 x 10 tray in
// 2339. A search split over threads must find each once: its parts must not
// overlap.
TEST_CASE("BurrTools 2: the pentominoes give the published counts at any thread count",
          "[solve][assembler][pentomino][parallel]") {
  ThreadPool pool(5);
  const Puzzle narrow = test::pentominoPuzzle(20, 3);
  CHECK(chooseAssembler(narrow.getProblem(0), SolverType::Bt2) == AssemblerKind::Bt2);
  for (const unsigned int threads : {1U, 2U, 5U}) {
    INFO(threads << " threads");
    auto a = made(narrow, {}, true);
    const test::RunResult r = runOnce(*a, searchOptions(threads, &pool));
    CHECK(r.end == RunEnd::Finished);
    CHECK(r.found.size() == 2);
    CHECK(a->finished() == 1.0F);
  }
}

TEST_CASE("BurrTools 2: the pentominoes fill 6 x 10 in 2339 ways",
          "[solve][assembler][pentomino][parallel][slow]") {
  ThreadPool pool(4);
  const Puzzle wide = test::pentominoPuzzle(10, 6);
  for (const unsigned int threads : {1U, 4U}) {
    auto a = made(wide, {}, true);
    CHECK(runOnce(*a, searchOptions(threads, &pool)).found.size() == 2339);
  }
}

namespace {

// Stops the search every `every` assemblies and continues it with the same
// assembler: it stops when told to, keeps the branches it had not finished,
// its progress never goes back, and every assembly is found once.
void stopsAndContinues(const Puzzle& p, std::size_t expected, unsigned int every,
                       unsigned int threads) {
  INFO(threads << " threads");
  ThreadPool pool(threads);
  auto a = made(p, {}, true);

  Fingerprints found;
  std::size_t reported = 0;
  float last = 0;
  unsigned int runs = 0;
  RunEnd end = RunEnd::Stopped;
  do {
    const std::size_t stopAt = reported + every;
    end = a->assemble(
        [&found, &reported, stopAt](Assembly asm_) {
          found.insert(test::fingerprint(asm_));
          return ++reported < stopAt;
        },
        searchOptions(threads, &pool), StopToken{});
    runs++;
    const float f = a->finished();
    CHECK(f >= last);
    last = f;
    REQUIRE(runs < 100);
  } while (end != RunEnd::Finished);

  CHECK(runs > 2);
  CHECK(reported == expected);
  CHECK(found.size() == expected);
  CHECK(a->finished() == 1.0F);
  CHECK(a->iterations() > 0);
}

}  // namespace

TEST_CASE("BurrTools 2: a search split over threads stops and continues",
          "[solve][assembler][parallel][resume]") {
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    stopsAndContinues(p, 84, 10, threads);
  }
}

TEST_CASE("BurrTools 2: the 6 x 10 pentominoes stop every 300 and continue",
          "[solve][assembler][pentomino][parallel][resume][slow]") {
  const Puzzle wide = test::pentominoPuzzle(10, 6);
  for (const unsigned int threads : {1U, 4U}) {
    stopsAndContinues(wide, 2339, 300, threads);
  }

  // a stop from another thread ends the run early
  ThreadPool pool(4);
  auto a = made(wide, {}, true);
  std::atomic<unsigned long> count{0};
  const StopSource stop;
  std::thread stopper([&] {
    while (count.load() < 50) {
      std::this_thread::yield();
    }
    stop.request(StopKind::Pause);
  });
  const RunEnd first = a->assemble(
      [&count](Assembly) {
        count++;
        return true;
      },
      searchOptions(4, &pool), stop.token());
  stopper.join();
  CHECK(first == RunEnd::Stopped);
  CHECK(count.load() < 2339);
  CHECK(a->finished() < 1.0F);

  const RunEnd rest = a->assemble(
      [&count](Assembly) {
        count++;
        return true;
      },
      searchOptions(4, &pool), StopToken{});
  CHECK(rest == RunEnd::Finished);
  CHECK(count.load() == 2339);
  CHECK(a->finished() == 1.0F);
}

// ---- the same answers as assembler 0 ------------------------------------------------

// BurrTools 2 searches assembler 0's matrix with another algorithm: the same
// assemblies, on one thread and on several, as made and reduced.
TEST_CASE("BurrTools 2: it finds what assembler 0 finds", "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  for (const char* file : {"PelikanBurr.xmpuzzle", "DraculasDentalDesaster.xmpuzzle",
                           "Prisgon.xmpuzzle", "BallRoom.xmpuzzle",
                           "AugmentedSecondStellation.xmpuzzle"}) {
    const Puzzle p = test::loadFixture(file);
    const Fingerprints expected = classic(p);
    REQUIRE(!expected.empty());
    for (const bool reduce : {false, true}) {
      for (const unsigned int threads : {1U, 4U}) {
        INFO(file << ", " << threads << " threads" << (reduce ? ", reduced" : ""));
        auto a = made(p, {}, reduce);
        const test::RunResult r = runOnce(*a, searchOptions(threads, &pool));
        CHECK(r.end == RunEnd::Finished);
        CHECK(r.found == expected);
        CHECK(a->finished() == 1.0F);
      }
    }
  }
}

// A split search on a puzzle with a symmetry breaker: the branches call
// smallerRotationExists() from several threads, which reads the shapes'
// lazily filled caches. Only such a puzzle exercises that (AGENTS.md); the
// premise is checked.
TEST_CASE("BurrTools 2: split over threads on a symmetry-breaking puzzle",
          "[solve][assembler][parallel][tsan]") {
  Fingerprints serial;
  {
    const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
    serial = runOnce(*made(p), searchOptions(1, nullptr)).found;
    CHECK(serial == classic(p));
  }
  REQUIRE(!serial.empty());

  {
    const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
    MatrixOptions keep;
    keep.keepRotations = true;
    INFO("symmetry breaking must be active for this test to mean anything");
    REQUIRE(runOnce(*made(p, keep), searchOptions(1, nullptr)).found.size() > serial.size());
  }

  // a fresh load: the caches start cold
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
  CHECK(runOnce(*made(p), searchOptions(4, &pool)).found == serial);
}

// ---- the run layer (decision 0009) ------------------------------------------------

// Pauses at random moments, from the callback and from another thread, and
// continues in memory: every assembly once. (A saved position of a started
// search cannot be continued, see "saved positions" below.)
TEST_CASE("BurrTools 2: random pauses lose nothing and repeat nothing",
          "[solve][assembler][parallel][resume]") {
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  test::randomPauses([&p] { return test::prepared<AssemblerBt2>(p); }, 84, 20, 20261008,
                     {{1, true}, {4, true}}, false);
}

TEST_CASE("BurrTools 2: random pauses on the 6 x 10 pentominoes",
          "[solve][assembler][parallel][resume][slow]") {
  const Puzzle p = test::pentominoPuzzle(10, 6);
  test::randomPauses([&p] { return test::prepared<AssemblerBt2>(p); }, 2339, 800, 4713,
                     {{1, true}, {4, true}}, false);
}

TEST_CASE("BurrTools 2: a stop made before the run starts is not lost",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  const Fingerprints all = classic(p);
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " threads");
    auto a = made(p);
    const StopSource early;
    early.request(StopKind::Pause);
    const test::RunResult stopped = runOnce(*a, searchOptions(threads, &pool), early.token());
    CHECK(stopped.end == RunEnd::Stopped);
    CHECK(stopped.found.empty());
    CHECK(a->finished() < 1.0F);

    const test::RunResult rest = runOnce(*a, searchOptions(threads, &pool));
    CHECK(rest.end == RunEnd::Finished);
    CHECK(rest.found == all);
  }
}

// finished() and iterations() are read by other threads while a run goes;
// ThreadSanitizer checks they are safe to read.
TEST_CASE("BurrTools 2: progress is read from another thread while it searches",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::pentominoPuzzle(20, 3);
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " threads");
    auto a = made(p);
    std::atomic<bool> done{false};
    bool inRange = true;
    std::thread reader([&] {
      while (!done.load()) {
        const float f = a->finished();
        inRange &= f >= 0.0F && f <= 1.0F;
        (void)a->iterations();
        std::this_thread::yield();
      }
    });
    const test::RunResult r = runOnce(*a, searchOptions(threads, &pool));
    done.store(true);
    reader.join();
    CHECK(inRange);
    CHECK(r.found.size() == 2);
    CHECK(a->finished() == 1.0F);
    CHECK(a->iterations() > 0);
  }
}

TEST_CASE("BurrTools 2: an exception in the search comes back to the caller",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " threads");
    auto a = made(p);
    std::atomic<unsigned int> seen{0};
    CHECK_THROWS_AS(a->assemble(
                        [&seen](Assembly) -> bool {
                          if (++seen == 3) {
                            throw std::runtime_error("callback failed");
                          }
                          return true;
                        },
                        searchOptions(threads, &pool), StopToken{}),
                    std::runtime_error);
    // nothing is handed over after the exception
    CHECK(seen.load() == 3);
  }
}

// The saved position is the original's dancing links position, which the
// dancing cells search never moves: "0 <iterations> (0 0)". Before the
// search starts it restores; once it has run it is refused, so that a solve
// starts over without what it had found (the original took it up as a fresh
// search and reported everything a second time).
TEST_CASE("BurrTools 2: saved positions", "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");

  SECTION("a search that has not started") {
    auto a = made(p);
    const SavedPosition fresh = a->savePosition();
    CHECK(fresh.version == "2.0-bt2");
    CHECK(fresh.text == "0 0 (0 0)");
    auto b = made(p);
    REQUIRE(b->restorePosition(fresh.text, fresh.version) == AssemblerError::None);
    CHECK(runOnce(*b, searchOptions(1, nullptr)).found.size() == 84);
  }

  SECTION("a paused search") {
    auto a = made(p);
    (void)runOnce(*a, searchOptions(1, nullptr), {}, 10);
    const SavedPosition pos = a->savePosition();
    CHECK(pos.text == "0 " + std::to_string(a->iterations()) + " (0 0)");
    auto b = made(p);
    CHECK(b->restorePosition(pos.text, pos.version) == AssemblerError::CannotRestoreInterrupted);
  }

  SECTION("other texts") {
    struct Case {
      const char* text;
      const char* version;
      AssemblerError error;
    };
    for (const Case c : {Case{"0 0 (0 0)", "1.5", AssemblerError::CannotRestoreVersion},
                         Case{"", "2.0-bt2", AssemblerError::CannotRestoreSyntax},
                         Case{"0", "2.0-bt2", AssemblerError::CannotRestoreSyntax},
                         Case{"0 0", "2.0-bt2", AssemblerError::CannotRestoreSyntax},
                         Case{"1 5 (99999 1) (0 0)", "2.0-bt2", AssemblerError::CannotRestoreSyntax},
                         Case{"2 9 (300 1) (400 2) (0 0)", "2.0-bt2",
                              AssemblerError::CannotRestoreInterrupted},
                         Case{"0 0 (5 2)", "2.0-bt2", AssemblerError::CannotRestoreInterrupted},
                         Case{"99 0 ", "2.0-bt2", AssemblerError::CannotRestoreInterrupted}}) {
      INFO(c.text << " (version " << c.version << ")");
      auto a = made(p);
      CHECK(a->restorePosition(c.text, c.version) == c.error);
    }
  }

  SECTION("a paused solve starts over") {
    Puzzle q = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
    Problem& pr = q.getProblem(0);
    pr.removeAllSolutions();
    {
      AssemblerBt2 a(q, pr);
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      REQUIRE(beginSolving(pr, a) == AssemblerError::None);
      (void)runOnce(a, searchOptions(1, nullptr), {}, 10);
      pr.incNumAssemblies();
      pr.addSolution(Assembly(q.getGridType()));
      storePosition(pr, a);
    }
    REQUIRE(pr.getAssemblerVersion() == "2.0-bt2");
    AssemblerBt2 a(q, pr);
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    CHECK(beginSolving(pr, a) == AssemblerError::CannotRestoreInterrupted);
    CHECK(pr.getSolveState() == ProblemState::Unsolved);
    CHECK(pr.getNumberOfSavedSolutions() == 0);
    CHECK(pr.getAssemblerState().empty());

    AssemblerBt2 b(q, pr);
    REQUIRE(b.createMatrix({}) == AssemblerError::None);
    REQUIRE(beginSolving(pr, b) == AssemblerError::None);
    CHECK(runOnce(b, searchOptions(1, nullptr)).found.size() == 84);
  }
}

TEST_CASE("BurrTools 2: the threads it assembles with", "[solve][assembler]") {
  CHECK(bt2Threads(3) == 3);
  CHECK(bt2Threads(1) == 1);
  CHECK(bt2Threads(1000000) == kMaxThreads);
  const unsigned int hw = hardwareThreads();
  CHECK(bt2Threads(0) == (hw <= 2 ? 1 : std::min(hw - 2, 16U)));
}

TEST_CASE("BurrTools 2: placements, the debugger and the current assembly",
          "[solve][assembler]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  auto a = made(p);
  REQUIRE(a->piecePlacementSupported());

  Assembler0 classicAssembler(p, p.getProblem(0));
  REQUIRE(classicAssembler.createMatrix({}) == AssemblerError::None);
  for (unsigned int piece = 0; piece < p.getProblem(0).getNumberOfPieces(); piece++) {
    // assembler 0's matrix
    CHECK(a->piecePlacementCount(piece) == classicAssembler.piecePlacementCount(piece));
  }

  const Assembly before = a->currentAssembly();
  CHECK(before.placementCount() == 7);
  for (unsigned int i = 0; i < 7; i++) {
    CHECK_FALSE(before.isPlaced(i));
  }

  a->debugStep(0);
  CHECK(a->iterations() == 0);
  a->debugStep(3);
  CHECK(a->iterations() == 3);
  // the debugger shows the last assembly the search found: none yet
  CHECK(a->currentAssembly() == before);
  a->debugStep(1000);
  const Assembly found = a->currentAssembly();
  unsigned int placed = 0;
  for (unsigned int i = 0; i < found.placementCount(); i++) {
    placed += found.isPlaced(i) ? 1 : 0;
  }
  CHECK(placed == 7);
}

// ---- dancing cells -------------------------------------------------------------------

namespace {

using Cover = std::multiset<std::vector<unsigned int>>;

// Every cover a search finds from where it stands, each sorted.
Cover solveAll(DancingCells& c) {
  Cover out;
  c.solve(0, StopToken{}, [&out](const std::vector<unsigned int>& rows) {
    std::vector<unsigned int> s = rows;
    std::sort(s.begin(), s.end());
    out.insert(s);
  });
  return out;
}

// A random exact cover problem with secondary items.
DancingCells randomCover(std::mt19937& rng, unsigned int primary, unsigned int secondary,
                         unsigned int options) {
  DancingCells c;
  c.prepare(primary, secondary);
  for (unsigned int o = 0; o < options; o++) {
    std::vector<unsigned int> items;
    for (unsigned int i = 0; i < primary + secondary; i++) {
      if (std::uniform_int_distribution<unsigned int>(0, 3)(rng) == 0) {
        items.push_back(i);
      }
    }
    if (!items.empty()) {
      c.addOption(100 + o, items);
    }
  }
  c.finishSetup();
  return c;
}

}  // namespace

TEST_CASE("Dancing cells: Knuth's example", "[solve][dancingcells]") {
  // TAOCP 7.2.2.1 (6): items a..g, the one cover is {a d f}, {b g}, {c e}
  DancingCells c;
  c.prepare(7, 0);
  c.addOption(1, {2, 4});
  c.addOption(2, {0, 3, 6});
  c.addOption(3, {1, 2, 5});
  c.addOption(4, {0, 3, 5});
  c.addOption(5, {1, 6});
  c.addOption(6, {3, 4, 6});
  c.finishSetup();
  REQUIRE(c.ready());
  CHECK(c.progress() == 0.0F);
  const Cover all = solveAll(c);
  CHECK(all == Cover{{1, 4, 5}});
  CHECK(c.finished());
  CHECK(c.progress() == 1.0F);
  CHECK(c.iterations() > 0);
}

// Split at every point of the search, the two parts find every cover of the
// whole exactly once between them.
TEST_CASE("Dancing cells: a split search finds every cover once", "[solve][dancingcells]") {
  std::mt19937 rng(2026);
  unsigned int splits = 0;
  for (int k = 0; k < 40; k++) {
    DancingCells whole = randomCover(rng, 10, 3, 40);
    DancingCells copy = whole;
    const Cover all = solveAll(copy);

    for (unsigned int stepsBefore = 1; stepsBefore < 60; stepsBefore += 3) {
      DancingCells first = whole;
      Cover found;
      first.solve(stepsBefore, StopToken{}, [&found](const std::vector<unsigned int>& rows) {
        std::vector<unsigned int> s = rows;
        std::sort(s.begin(), s.end());
        found.insert(s);
      });
      std::unique_ptr<DancingCells> second = first.split();
      if (second) {
        splits++;
        CHECK(first.lastSplitShare() >= 0.0F);
        CHECK(first.lastSplitShare() <= 1.0F);
        CHECK(second->iterations() == 0);
        const Cover b = solveAll(*second);
        found.insert(b.begin(), b.end());
      }
      const Cover a = solveAll(first);
      found.insert(a.begin(), a.end());
      CHECK(found == all);
    }
  }
  CHECK(splits > 100);
}

TEST_CASE("Dancing cells: a stop ends a slice and the search carries on",
          "[solve][dancingcells]") {
  std::mt19937 rng(7);
  DancingCells whole = randomCover(rng, 8, 2, 30);
  DancingCells copy = whole;
  const Cover all = solveAll(copy);

  Cover found;
  unsigned int slices = 0;
  while (!whole.finished()) {
    const StopSource stop;
    whole.solve(0, stop.token(), [&found, &stop](const std::vector<unsigned int>& rows) {
      std::vector<unsigned int> s = rows;
      std::sort(s.begin(), s.end());
      found.insert(s);
      stop.request(StopKind::Pause);
    });
    slices++;
    REQUIRE(slices < 10000);
  }
  CHECK(found == all);
}
