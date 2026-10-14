// SPDX-License-Identifier: GPL-3.0-or-later
// Assembler 0 and the run layer. The original's assembler cases of
// test/test_solver.cpp come first (adapted to the new API: runs take a stop
// token and options, positions are strings); the cases after them test the
// rules of decision 0009.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/base/thread_pool.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/assembler_0.h"
#include "shapelogic/solve/dlx_matrix.h"
#include "shapelogic/solve/rotation_filter.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace bt;
using test::Fingerprints;
using test::runOnce;
using test::searchOptions;

namespace {

// All assemblies of problem 0, on one thread with the plain search.
Fingerprints allAssemblies(const std::string& fixture, bool reduce = false) {
  const Puzzle p = test::loadFixture(fixture);
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  if (reduce) {
    (void)a.reduce(StopToken{});
  }
  const test::RunResult r = runOnce(a, searchOptions(1, nullptr, false));
  REQUIRE(r.end == RunEnd::Finished);
  return r.found;
}

// The assembly count and iterations of a plain run of a fixture.
struct Regression {
  std::size_t assemblies;
  std::uint64_t iterations;
  float finished;
};

Regression solveFixture(const std::string& fixture) {
  const Puzzle p = test::loadFixture(fixture);
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  const test::RunResult r = runOnce(a, searchOptions(1, nullptr));
  CHECK(r.end == RunEnd::Finished);
  return {r.found.size(), a.iterations(), a.finished()};
}

}  // namespace

// ---- from the original's test_solver.cpp ---------------------------------------

TEST_CASE("Assembler 0: regression counts of the examples", "[solve][assembler]") {
  struct Case {
    const char* file;
    std::size_t assemblies;
  };
  // Pelikan, Dracula's Dental Desaster, Prisgon (bricks), Bermuda (prisms),
  // Augmented Second Stellation (spheres), BallRoom (spheres)
  for (const Case c : {Case{"PelikanBurr.xmpuzzle", 12}, Case{"DraculasDentalDesaster.xmpuzzle", 84},
                       Case{"Prisgon.xmpuzzle", 1}, Case{"Bermuda.xmpuzzle", 1},
                       Case{"AugmentedSecondStellation.xmpuzzle", 2},
                       Case{"BallRoom.xmpuzzle", 2}}) {
    INFO(c.file);
    const Regression r = solveFixture(c.file);
    CHECK(r.assemblies == c.assemblies);
    CHECK(r.iterations > 0);
    CHECK(r.finished >= 1.0F);
  }
}

TEST_CASE("Assembler 0: a callback that answers false stops the run", "[solve][assembler]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  int count = 0;
  const RunEnd end = a.assemble(
      [&count](Assembly) {
        count++;
        return false;
      },
      searchOptions(1, nullptr), StopToken{});
  CHECK(count == 1);
  CHECK(end == RunEnd::Stopped);
  CHECK(a.finished() < 1.0F);

  // the next run carries on
  const test::RunResult rest = runOnce(a, searchOptions(1, nullptr));
  CHECK(rest.end == RunEnd::Finished);
  CHECK(rest.found.size() == 11);
}

TEST_CASE("Assembler 0: split over threads it finds what one thread finds",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");

  Fingerprints one;
  {
    Assembler0 a(p, p.getProblem(0));
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    one = runOnce(a, searchOptions(1, &pool)).found;
    CHECK(one.size() == 12);
  }
  {
    Assembler0 a(p, p.getProblem(0));
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    const test::RunResult r = runOnce(a, searchOptions(4, &pool));
    CHECK(r.end == RunEnd::Finished);
    CHECK(r.found == one);
    CHECK(a.iterations() > 0);
    CHECK(a.finished() >= 1.0F);
  }
}

TEST_CASE("Assembler 0: a split search paused and continued reports each assembly once",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const Fingerprints all = allAssemblies("PelikanBurr.xmpuzzle");
  REQUIRE(all.size() == 12);

  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  const test::RunResult first = runOnce(a, searchOptions(4, &pool), StopToken{}, 5);
  CHECK(first.end == RunEnd::Stopped);
  CHECK(first.found.size() == 5);

  const test::RunResult rest = runOnce(a, searchOptions(4, &pool));
  CHECK(rest.end == RunEnd::Finished);
  Fingerprints both = first.found;
  both.insert(rest.found.begin(), rest.found.end());
  CHECK(both == all);
}

// The leading flag of the saved text must not break an ordinary restore. A
// not-yet-started assembler, as in the original.
TEST_CASE("Assembler 0: the position of a search not yet started restores",
          "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  const SavedPosition pos = a.savePosition();
  CHECK(pos.version == "1.5");
  CHECK(pos.text == "0 0 0 (0 0)");

  Assembler0 restored(p, p.getProblem(0));
  REQUIRE(restored.createMatrix({}) == AssemblerError::None);
  CHECK(restored.restorePosition(pos.text, pos.version) == AssemblerError::None);
  CHECK(runOnce(restored, searchOptions(1, nullptr)).found.size() == 12);
}

// Thread counts come from users and files; an absurd one is clamped before
// any thread is made.
TEST_CASE("Assembler 0: an absurd thread count is clamped", "[solve][assembler][parallel]") {
  CHECK(effectiveThreads(1000000, nullptr) == kMaxThreads);
  ThreadPool pool(3);
  CHECK(effectiveThreads(0, &pool) == 3);  // nothing asked for: the pool's size
  CHECK(effectiveThreads(2, &pool) == 2);  // asked for: that
  CHECK(effectiveThreads(0, nullptr) == 1);

  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  CHECK(runOnce(a, searchOptions(1000000, &pool)).found.size() == 12);
}

// A split search on a puzzle with a symmetry breaker: the workers call
// smallerRotationExists(), which reads the shapes' lazily filled caches.
// Only such a puzzle exercises that from several threads, so a TSan run
// without it reports nothing while races remain (AGENTS.md). Keep the
// puzzle; the premise is checked below.
TEST_CASE("Assembler 0: split over threads on a symmetry-breaking puzzle",
          "[solve][assembler][parallel][tsan]") {
  Fingerprints serial;
  {
    const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
    Assembler0 a(p, p.getProblem(0));
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    serial = runOnce(a, searchOptions(1, nullptr)).found;
  }
  REQUIRE(!serial.empty());

  // the premise: keeping rotations gives more, so the filter is active
  {
    const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
    Assembler0 a(p, p.getProblem(0));
    MatrixOptions keep;
    keep.keepRotations = true;
    REQUIRE(a.createMatrix(keep) == AssemblerError::None);
    INFO("symmetry breaking must be active for this test to mean anything");
    REQUIRE(runOnce(a, searchOptions(1, nullptr)).found.size() > serial.size());
  }

  // a fresh load: the caches start cold
  ThreadPool pool(4);
  for (const bool vector : {true, false}) {
    const Puzzle p = test::loadFixture("Bermuda.xmpuzzle");
    Assembler0 a(p, p.getProblem(0));
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    CHECK(runOnce(a, searchOptions(4, &pool, vector)).found == serial);
  }
}

namespace {

// Stops a search after `stopAt` assemblies (the callback asks), then
// finishes it: straight on with the same assembler, or after saving the
// position and restoring it into a fresh one. Every assembly is reported
// exactly once over both parts.
void checkStopAndResume(const std::string& fixture, unsigned int threads, bool vector,
                        unsigned int stopAt, bool viaSave) {
  INFO(fixture << ", " << threads << " threads" << (vector ? "" : ", plain") << ", stop at "
               << stopAt << (viaSave ? ", via save" : ""));
  ThreadPool pool(threads);
  const Fingerprints all = allAssemblies(fixture);
  REQUIRE(all.size() > stopAt);

  const Puzzle p = test::loadFixture(fixture);
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  const test::RunResult first = runOnce(a, searchOptions(threads, &pool, vector), {}, stopAt);
  REQUIRE(first.end == RunEnd::Stopped);
  REQUIRE(a.finished() < 1.0F);

  test::RunResult rest;
  if (viaSave) {
    const SavedPosition pos = a.savePosition();
    const Puzzle p2 = test::loadFixture(fixture);
    Assembler0 restored(p2, p2.getProblem(0));
    REQUIRE(restored.createMatrix({}) == AssemblerError::None);
    REQUIRE(restored.restorePosition(pos.text, pos.version) == AssemblerError::None);
    // restoring and saving again writes the same position (signatures come
    // from an unordered set, so only their number is compared)
    const SavedPosition again = restored.savePosition();
    const auto head = [](const std::string& s) { return s.substr(0, s.find(" S ")); };
    CHECK(head(again.text) == head(pos.text));
    CHECK(again.text.size() == pos.text.size());
    rest = runOnce(restored, searchOptions(threads, &pool, vector));
  } else {
    rest = runOnce(a, searchOptions(threads, &pool, vector));
  }
  CHECK(rest.end == RunEnd::Finished);

  Fingerprints both = first.found;
  both.insert(rest.found.begin(), rest.found.end());
  CHECK(both == all);
}

}  // namespace

TEST_CASE("Assembler 0: a stopped search resumes, saved or not, reporting each assembly once",
          "[solve][assembler][parallel][resume]") {
  for (const bool viaSave : {false, true}) {
    checkStopAndResume("PelikanBurr.xmpuzzle", 4, true, 1, viaSave);
    checkStopAndResume("PelikanBurr.xmpuzzle", 4, true, 5, viaSave);
    checkStopAndResume("PelikanBurr.xmpuzzle", 1, true, 5, viaSave);
    checkStopAndResume("PelikanBurr.xmpuzzle", 4, false, 5, viaSave);
    // The plain search on one thread stops right after placing the last
    // piece (pos == pieces): the original read past its row list when it
    // saved that position. Every stop point, saved and restored.
    for (unsigned int stopAt = 1; stopAt < 12; stopAt++) {
      checkStopAndResume("PelikanBurr.xmpuzzle", 1, false, stopAt, viaSave);
    }
  }
}

// The vector and the plain search find the same assemblies: on one thread,
// where the vector search does all of it, and on several, where every
// thread searches its tasks with it; from the matrix as made and from the
// reduced one. Compared as sets, not counts. The list has holes and
// variable voxels (Pelikan) and a symmetry breaker (Bermuda).
TEST_CASE("Assembler 0: the vector and the plain search find the same assemblies",
          "[solve][assembler][simd][parallel]") {
  ThreadPool pool(4);
  unsigned int tookTheVectorSearch = 0;
  const char* files[] = {"PelikanBurr.xmpuzzle",     "DraculasDentalDesaster.xmpuzzle",
                         "Bermuda.xmpuzzle",         "Prisgon.xmpuzzle",
                         "BallRoom.xmpuzzle",        "AugmentedSecondStellation.xmpuzzle"};
  for (const char* file : files) {
    const Fingerprints plain = allAssemblies(file);
    REQUIRE(!plain.empty());
    std::uint64_t plainIterations = 0;
    {
      const Puzzle p = test::loadFixture(file);
      Assembler0 a(p, p.getProblem(0));
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      (void)runOnce(a, searchOptions(1, nullptr, false));
      plainIterations = a.iterations();
    }
    for (const bool reduce : {false, true}) {
      for (const unsigned int threads : {1U, 4U}) {
        INFO(file << ", " << threads << " threads" << (reduce ? ", reduced" : ""));
        const Puzzle p = test::loadFixture(file);
        Assembler0 a(p, p.getProblem(0));
        REQUIRE(a.createMatrix({}) == AssemblerError::None);
        if (reduce) {
          (void)a.reduce(StopToken{});
        }
        const test::RunResult r = runOnce(a, searchOptions(threads, &pool, true));
        CHECK(r.end == RunEnd::Finished);
        CHECK(a.finished() >= 1.0F);
        CHECK(r.found == plain);
        if (threads == 1 && !reduce && a.iterations() != plainIterations) {
          tookTheVectorSearch++;
        }
      }
    }
  }
  // if no puzzle took the vector search, this compared the plain search with
  // itself
  CHECK(tookTheVectorSearch == 6);
}

namespace {

// The assemblies of problem 0 the way burrTxt counts them: reduced first.
std::size_t countPentominoes(const Puzzle& puz, unsigned int threads, ThreadPool* pool,
                             bool vector = true) {
  Assembler0 a(puz, puz.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  (void)a.reduce(StopToken{});
  const test::RunResult r = runOnce(a, searchOptions(threads, pool, vector));
  CHECK(r.end == RunEnd::Finished);
  return r.found.size();
}

}  // namespace

// The pentominoes fill a 3 x 20 tray in 2 ways and a 6 x 10 tray in 2339. A
// search split over threads must find each once: its parts must not overlap.
TEST_CASE("Assembler 0: the pentominoes give the published counts at any thread count",
          "[solve][assembler][pentomino][parallel]") {
  ThreadPool pool(5);
  const Puzzle narrow = test::pentominoPuzzle(20, 3);
  for (const unsigned int threads : {1U, 2U, 5U}) {
    INFO(threads << " threads");
    CHECK(countPentominoes(narrow, threads, &pool) == 2);
    CHECK(countPentominoes(narrow, threads, &pool, false) == 2);
  }
}

TEST_CASE("Assembler 0: the pentominoes fill 6 x 10 in 2339 ways",
          "[solve][assembler][pentomino][parallel][slow]") {
  ThreadPool pool(4);
  const Puzzle wide = test::pentominoPuzzle(10, 6);
  CHECK(countPentominoes(wide, 1, &pool) == 2339);
  CHECK(countPentominoes(wide, 4, &pool) == 2339);
  CHECK(countPentominoes(wide, 4, &pool, false) == 2339);
}

// ---- the run layer (decision 0009) ------------------------------------------------

// A stop made before a run starts is seen when it starts (the original lost
// it: its assembler cleared its abort flag on entry). The run ends at once,
// nothing is lost, and the next run with a fresh token does everything.
TEST_CASE("Assembler 0: a stop made before the run starts is not lost",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Fingerprints all = allAssemblies("DraculasDentalDesaster.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    for (const bool vector : {true, false}) {
      INFO(threads << " threads" << (vector ? "" : ", plain"));
      const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
      Assembler0 a(p, p.getProblem(0));
      REQUIRE(a.createMatrix({}) == AssemblerError::None);

      const StopSource early;
      early.request(StopKind::Pause);
      const test::RunResult stopped = runOnce(a, searchOptions(threads, &pool, vector),
                                              early.token());
      CHECK(stopped.end == RunEnd::Stopped);
      CHECK(stopped.found.empty());
      CHECK(a.finished() < 1.0F);

      const test::RunResult rest = runOnce(a, searchOptions(threads, &pool, vector));
      CHECK(rest.end == RunEnd::Finished);
      CHECK(rest.found == all);
    }
  }
}

TEST_CASE("Assembler 0: random pauses lose nothing and repeat nothing",
          "[solve][assembler][parallel][resume]") {
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  test::randomPauses([&p] { return test::prepared<Assembler0>(p); }, 84, 20, 20261006,
                     test::kAllPaths);
}

TEST_CASE("Assembler 0: random pauses on the 6 x 10 pentominoes",
          "[solve][assembler][parallel][resume][slow]") {
  const Puzzle p = test::pentominoPuzzle(10, 6);
  test::randomPauses([&p] { return test::prepared<Assembler0>(p); }, 2339, 800, 4711,
                     test::kAllPaths);
}

// finished() and iterations() are read by other threads while a run goes
// (a progress display); ThreadSanitizer checks they are safe to read.
TEST_CASE("Assembler 0: progress is read from another thread while it searches",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    for (const bool vector : {true, false}) {
      INFO(threads << " threads" << (vector ? "" : ", plain"));
      Assembler0 a(p, p.getProblem(0));
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      std::atomic<bool> done{false};
      bool inRange = true;
      unsigned int reads = 0;
      std::thread reader([&] {
        while (!done.load()) {
          const float f = a.finished();
          inRange &= f >= 0.0F && f <= 1.0F;
          (void)a.iterations();
          reads++;
          std::this_thread::yield();
        }
      });
      const test::RunResult r = runOnce(a, searchOptions(threads, &pool, vector));
      done.store(true);
      reader.join();
      CHECK(inRange);
      CHECK(r.found.size() == 84);
      CHECK(a.finished() >= 1.0F);
      CHECK(a.iterations() > 0);
    }
  }
}

// An exception in one thread (here from the callback) stops the others and
// comes back to the caller.
TEST_CASE("Assembler 0: an exception in the search comes back to the caller",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " threads");
    Assembler0 a(p, p.getProblem(0));
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    unsigned int seen = 0;
    CHECK_THROWS_AS(a.assemble(
                        [&seen](Assembly) -> bool {
                          if (++seen == 3) {
                            throw std::runtime_error("callback failed");
                          }
                          return true;
                        },
                        searchOptions(threads, &pool), StopToken{}),
                    std::runtime_error);
    CHECK(seen == 3);
  }
}

// The original's "a damaged saved position errors once, then solves from
// the start", at the problem level: a position that cannot be restored
// drops everything solving found, and the next solve starts clean.
TEST_CASE("beginSolving: a position that cannot be restored starts over",
          "[solve][assembler][resume]") {
  struct Case {
    const char* text;
    const char* version;
    ProblemState state;
    AssemblerError error;
  };
  const Case cases[] = {
      // a stopped split search of older versions, saved without its tasks:
      // the case that cost assemblies in the original
      {"1 0 0 (0 0)", "1.5", ProblemState::Solving, AssemblerError::CannotRestoreInterrupted},
      {"0 0 0 (0 0)", "1.4", ProblemState::Solving, AssemblerError::CannotRestoreVersion},
      {"x", "1.5", ProblemState::Solving, AssemblerError::CannotRestoreSyntax},
      {"2 0 0 (0 0) T 1 1 99999 99999 C 0 S 0", "1.5", ProblemState::Solving,
       AssemblerError::CannotRestoreSyntax},
      {"0 3 0 (99999 1) (0 0) (0 0) (0 0)", "1.5", ProblemState::Solving,
       AssemblerError::CannotRestoreSyntax},
      // a position on a problem that is not being solved (hand edited)
      {"0 0 0 (0 0)", "1.5", ProblemState::Unknown, AssemblerError::CannotRestoreSyntax},
  };
  for (const Case& c : cases) {
    INFO(c.text << " (version " << c.version << ")");
    Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.removeAllSolutions();
    pr.markSolving();
    pr.incNumAssemblies();
    pr.addSolution(Assembly(p.getGridType()));
    pr.setAssemblerState(c.text, c.version);
    if (c.state != ProblemState::Solving) {
      pr.makeUnknown();
      pr.setAssemblerState(c.text, c.version);
    }
    REQUIRE(pr.getSolveState() == c.state);

    {
      Assembler0 a(p, pr);
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      CHECK(beginSolving(pr, a) == c.error);
    }
    CHECK(pr.getSolveState() == ProblemState::Unsolved);
    CHECK(pr.getNumberOfSavedSolutions() == 0);
    CHECK(pr.getAssemblerState().empty());
    CHECK(pr.getAssemblerVersion().empty());
    CHECK(canStartSolving(pr));

    // the next solve starts from scratch and finds everything
    Assembler0 a(p, pr);
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    REQUIRE(beginSolving(pr, a) == AssemblerError::None);
    CHECK(pr.getSolveState() == ProblemState::Solving);
    CHECK(runOnce(a, searchOptions(1, nullptr)).found.size() == 12);
  }
}

// A paused solve goes into the puzzle file (storePosition, the writer), is
// read back, and carries on: every assembly once. Each kind of position:
// the plain search (flag 0), the vector search (3), the split search (2).
TEST_CASE("Assembler 0: a paused search saved in a puzzle file carries on after loading",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  const Fingerprints all = allAssemblies("DraculasDentalDesaster.xmpuzzle");
  struct Path {
    unsigned int threads;
    bool vector;
    char flag;
  };
  for (const Path path : {Path{1, false, '0'}, Path{1, true, '3'}, Path{4, true, '2'}}) {
    INFO(path.threads << " threads" << (path.vector ? "" : ", plain"));
    Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.removeAllSolutions();

    Fingerprints first;
    {
      Assembler0 a(p, pr);
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      (void)a.reduce(StopToken{});
      REQUIRE(beginSolving(pr, a) == AssemblerError::None);
      first = runOnce(a, searchOptions(path.threads, &pool, path.vector), {}, 30).found;
      REQUIRE(first.size() == 30);
      storePosition(pr, a);
    }
    CHECK(pr.getAssemblerVersion() == "1.5");
    CHECK(pr.getAssemblerState()[0] == path.flag);

    const std::string xml = io::writePuzzle(p);
    CHECK(xml.find("<assembler version=\"1.5\">") != std::string::npos);
    Result<Puzzle> loaded = io::parsePuzzle(xml);
    REQUIRE(loaded);
    Problem& pr2 = loaded->getProblem(0);
    REQUIRE(pr2.getSolveState() == ProblemState::Solving);
    REQUIRE(canStartSolving(pr2));

    Assembler0 a(*loaded, pr2);
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    (void)a.reduce(StopToken{});
    REQUIRE(beginSolving(pr2, a) == AssemblerError::None);
    CHECK(pr2.getAssemblerState().empty());
    const test::RunResult rest = runOnce(a, searchOptions(path.threads, &pool, path.vector));
    CHECK(rest.end == RunEnd::Finished);
    Fingerprints both = first;
    both.insert(rest.found.begin(), rest.found.end());
    CHECK(both == all);
  }
}

// A position saved after the search is through restores as through (the
// original read past its row list).
TEST_CASE("Assembler 0: a finished position restores as finished", "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  REQUIRE(runOnce(a, searchOptions(1, nullptr, false)).end == RunEnd::Finished);
  const SavedPosition pos = a.savePosition();

  Assembler0 b(p, p.getProblem(0));
  REQUIRE(b.createMatrix({}) == AssemblerError::None);
  REQUIRE(b.restorePosition(pos.text, pos.version) == AssemblerError::None);
  CHECK(b.finished() >= 1.0F);
  const test::RunResult r = runOnce(b, searchOptions(1, nullptr));
  CHECK(r.end == RunEnd::Finished);
  CHECK(r.found.empty());
}

// ---- from the audit of stage 3 ---------------------------------------------------

namespace {

std::unique_ptr<Assembler> madeA0(const Puzzle& p) {
  auto a = std::make_unique<Assembler0>(p, p.getProblem(0));
  REQUIRE(a->createMatrix({}) == AssemblerError::None);
  return a;
}

// The first entry of a plain search's position "0 0 <iterations> (r c)":
// the row at depth 0, or nothing when the position is not at depth 0.
std::optional<unsigned int> depthZeroRow(const std::string& text) {
  unsigned int pos = 1;
  unsigned long long it = 0;
  unsigned int row = 0;
  unsigned int col = 0;
  int used = 0;
  if (std::sscanf(text.c_str(), "0 %u %llu (%u %u)%n", &pos, &it, &row, &col, &used) != 4 ||
      pos != 0 || static_cast<std::size_t>(used) != text.size()) {
    return std::nullopt;
  }
  return row;
}

// The node count of Pelikan's matrix, and positions of its plain, vector
// and split searches to damage.
std::size_t pelikanNodes(const Puzzle& p) {
  DlxMatrix m;
  RotationFilter f;
  std::atomic<unsigned int> piece{0};
  int param = 0;
  REQUIRE(m.build(p, p.getProblem(0), false, f, piece, param) == AssemblerError::None);
  return m.nodes.left.size();
}

std::vector<std::string> pelikanPositions(const Puzzle& p, ThreadPool& pool) {
  std::vector<std::string> texts;
  for (const auto& [threads, vector] : {std::pair{1U, false}, std::pair{1U, true},
                                        std::pair{4U, false}}) {
    auto a = madeA0(p);
    (void)runOnce(*a, searchOptions(threads, &pool, vector), {}, 3);
    texts.push_back(a->savePosition().text);
  }
  REQUIRE(texts[0][0] == '0');
  REQUIRE(texts[1][0] == '3');
  REQUIRE(texts[2][0] == '2');
  return texts;
}

}  // namespace

// The plain search goes back to depth 0 between two first-level rows, with
// the last one still covered; a pause can land there. Continued with
// threads or with the vector search, it was taken for a search that had not
// started (the original's bug too) and reported assemblies of a matrix
// that was not whole.
TEST_CASE("Assembler 0: a plain search stopped anywhere continues the same on every path",
          "[solve][assembler][resume][parallel]") {
  ThreadPool pool(4);
  for (const char* file : {"DraculasDentalDesaster.xmpuzzle", "PelikanBurr.xmpuzzle"}) {
    INFO(file);
    const Puzzle p = test::loadFixture(file);
    auto make = [&p] { return madeA0(p); };
    const Fingerprints all = allAssemblies(file);

    // where depth 0 is revisited
    std::vector<unsigned long> steps;
    {
      auto a = make();
      for (unsigned long k = 1; k < 100000 && steps.size() < 2; k++) {
        a->debugStep(1);
        const std::optional<unsigned int> row = depthZeroRow(a->savePosition().text);
        if (row && *row != 0) {
          steps.push_back(k);
        }
      }
    }
    REQUIRE(!steps.empty());
    for (const unsigned long k : {0UL, 40UL, 5000UL}) {
      steps.push_back(k);
    }

    for (const unsigned long k : steps) {
      const Fingerprints rest = test::continueOnEveryPath(make, k, pool);
      CHECK(test::isSubMultiset(rest, all));
    }
  }
}

// A vector search stopped before it reported anything left its flag behind;
// a plain search after it then saved a flag 3 position away from the start,
// which no restore accepts, and beginSolving() dropped every result.
TEST_CASE("Assembler 0: a vector search stopped at once leaves no trace in a plain position",
          "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  test::vectorStoppedAtOnceThenPlain([&p] { return madeA0(p); },
                                     allAssemblies("DraculasDentalDesaster.xmpuzzle"));
}

TEST_CASE("Assembler 0: every search kind run to its end restores as finished",
          "[solve][assembler][resume][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DraculasDentalDesaster.xmpuzzle");
  test::finishedRestoresOnEveryPath([&p] { return madeA0(p); }, pool);

  // a stopped split search whose tasks are all done (a stop that came after
  // the last one) is through on the next run, and says so when saved
  auto a = madeA0(p);
  REQUIRE(a->restorePosition("2 0 0 (0 0) T 1 0 C 1 S 0", Assembler0::kVersion) ==
          AssemblerError::None);
  const test::RunResult r = runOnce(*a, searchOptions(1, nullptr, false));
  CHECK(r.end == RunEnd::Finished);
  CHECK(r.found.empty());
  const SavedPosition pos = a->savePosition();
  auto b = madeA0(p);
  CHECK(b->restorePosition(pos.text, pos.version) == AssemblerError::None);
  CHECK(b->finished() >= 1.0F);
}

// The original covered whatever a position named: a damaged one read and
// wrote out of bounds. Each level is now checked before it is covered, and
// a refused position leaves the assembler as it was.
TEST_CASE("Assembler 0: a damaged position is refused before it is used",
          "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const Fingerprints all = allAssemblies("PelikanBurr.xmpuzzle");
  const std::size_t nodes = pelikanNodes(p);

  {
    const std::string damaged[] = {
        "0 1 0 (5 4000000000) (0 0)",                    // a column that is no column
        "0 1 0 (3 1) (0 0)",                             // a header as the row
        "0 1 0 (" + std::to_string(nodes) + " 1) (0 0)",  // the row one past the last node
        "0 2 0 (0 0) (0 0) (0 0)",                       // a level below pos without a row
        "0 0 0 (4000000000 2)",                          // the level it stands on, no node
        "2 0 0 (0 0) T 10000000 1 1 1",                  // more tasks than the text holds
        "2 0 0 (0 0) T 1 4000000000 C 0 S 0",            // a prefix longer than the pieces
        "2 0 0 (0 0) T 1 1 5 4000000000 C 0 S 0",        // a prefix row that is no node
        "2 0 0 (0 0) T 1 1 1 3 C 0 S 0",                 // a prefix row that is a header
    };
    for (const std::string& text : damaged) {
      INFO(text);
      auto a = madeA0(p);
      CHECK(a->restorePosition(text, Assembler0::kVersion) == AssemblerError::CannotRestoreSyntax);
      // nothing changed: the search runs as from the start
      const test::RunResult r = runOnce(*a, searchOptions(1, nullptr, false));
      CHECK(r.end == RunEnd::Finished);
      CHECK(r.found == all);
    }
  }
}

TEST_CASE("Assembler 0: random damage to a position is refused, or searches in bounds",
          "[solve][assembler][resume][slow]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const Fingerprints all = allAssemblies("PelikanBurr.xmpuzzle");
  const std::size_t nodes = pelikanNodes(p);
  const std::vector<std::string> texts = pelikanPositions(p, pool);

  std::mt19937 rng(91);
  unsigned int refused = 0;
  for (const std::string& text : texts) {
    for (int k = 0; k < 200; k++) {
      std::string t = text;
      std::vector<std::size_t> starts;
      for (std::size_t i = 0; i < t.size(); i++) {
        if (std::isdigit(static_cast<unsigned char>(t[i])) != 0 &&
            (i == 0 || std::isdigit(static_cast<unsigned char>(t[i - 1])) == 0)) {
          starts.push_back(i);
        }
      }
      const std::size_t at =
          starts[std::uniform_int_distribution<std::size_t>(0, starts.size() - 1)(rng)];
      if (k % 5 == 0) {
        t.resize(at);
      } else {
        std::size_t end = at;
        while (end < t.size() && std::isdigit(static_cast<unsigned char>(t[end])) != 0) {
          end++;
        }
        const auto v = k % 2 == 0
                           ? std::uniform_int_distribution<std::size_t>(0, 10)(rng)
                           : std::uniform_int_distribution<std::size_t>(0, nodes + 10)(rng);
        t.replace(at, end - at, std::to_string(v));
      }
      INFO(t);
      auto a = madeA0(p);
      if (a->restorePosition(t, Assembler0::kVersion) != AssemblerError::None) {
        refused++;
        continue;
      }
      // another position the search could have stood at: it searches on
      // to its end and finds only assemblies of the puzzle
      const test::RunResult r = runOnce(*a, searchOptions(1, nullptr, false));
      CHECK(r.end == RunEnd::Finished);
      CHECK(test::isSubMultiset(r.found, all));
    }
  }
  CHECK(refused > 100);
}

TEST_CASE("Assembler 0: errors of createMatrix", "[solve][assembler]") {
  SECTION("ranges are for assembler 1") {
    Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.setShapeMaximum(p, pr.getShapeIdOfPart(0), 2);
    CHECK_FALSE(Assembler0::canHandle(pr));
    Assembler0 a(p, pr);
    CHECK(a.createMatrix({}) == AssemblerError::PuzzleUnhandable);
    CHECK(chooseAssembler(pr, SolverType::Classic) == AssemblerKind::Assembler1);
  }
  SECTION("too many units") {
    Puzzle p = test::pentominoPuzzle(10, 5);
    Assembler0 a(p, p.getProblem(0));
    CHECK(a.createMatrix({}) == AssemblerError::TooManyUnits);
    CHECK(a.errorParam() == 10);
  }
  SECTION("too few units") {
    Puzzle p = test::pentominoPuzzle(10, 7);
    Assembler0 a(p, p.getProblem(0));
    CHECK(a.createMatrix({}) == AssemblerError::TooFewUnits);
    CHECK(a.errorParam() == 10);
  }
  SECTION("a piece that fits nowhere") {
    Puzzle p = test::pentominoPuzzle(30, 2);
    Assembler0 a(p, p.getProblem(0));
    CHECK(a.createMatrix({}) == AssemblerError::CannotPlace);
  }
  CHECK(errorMessage(AssemblerError::CannotPlace) == "A piece cannot be placed in the target shape");
}

TEST_CASE("Assembler 0: placements, the debugger and the current assembly",
          "[solve][assembler]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  Assembler0 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  REQUIRE(a.piecePlacementSupported());

  for (unsigned int piece = 0; piece < p.getProblem(0).getNumberOfPieces(); piece++) {
    const unsigned int count = a.piecePlacementCount(piece);
    CHECK(count > 0);
    std::set<unsigned int> nodes;
    unsigned int node = 0;
    for (unsigned int i = 0; i < count; i++) {
      unsigned char tran = 0;
      int x = 0;
      int y = 0;
      int z = 0;
      node = a.piecePlacement(node, i == 0 ? 0 : 1, piece, &tran, &x, &y, &z);
      nodes.insert(node);
    }
    CHECK(nodes.size() == count);
  }

  const Assembly before = a.currentAssembly();
  CHECK(before.placementCount() == 7);
  for (unsigned int i = 0; i < 7; i++) {
    CHECK_FALSE(before.isPlaced(i));
  }

  a.debugStep(3);
  CHECK(a.iterations() == 3);
  const Assembly after = a.currentAssembly();
  unsigned int placed = 0;
  for (unsigned int i = 0; i < after.placementCount(); i++) {
    placed += after.isPlaced(i) ? 1 : 0;
  }
  CHECK(placed > 0);
}

TEST_CASE("solver types: names and the assembler each gets", "[solve]") {
  CHECK(solverTypeFromName("Complete (ShapeLogic)") == SolverType::Classic);
  CHECK(solverTypeFromName("BurrTools Classic") == SolverType::Classic);
  CHECK(solverTypeFromName("classic") == SolverType::Classic);
  CHECK(solverTypeFromName("Andrew-Crowell") == SolverType::Crowell);
  CHECK(solverTypeFromName("fortran") == SolverType::Crowell);
  CHECK(solverTypeFromName("Fast (Crowell)") == SolverType::Crowell);
  CHECK_FALSE(solverTypeFromName("bt2").has_value());
  CHECK_FALSE(solverTypeFromName("").has_value());
  CHECK_FALSE(solverTypeFromName("dlx").has_value());
  CHECK(solverTypeFromIndex(7) == SolverType::Classic);
  CHECK(solverTypeLabel(SolverType::Classic) == "Complete (ShapeLogic)");
  CHECK(solverTypeLabel(SolverType::Crowell) == "Fast (Crowell)");

  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  CHECK(chooseAssembler(p.getProblem(0), SolverType::Classic) == AssemblerKind::Assembler0);
  CHECK(chooseAssembler(p.getProblem(0), SolverType::Crowell) == AssemblerKind::Assembler0);
  CHECK(makeAssembler(p, p.getProblem(0), SolverType::Classic) != nullptr);
}
