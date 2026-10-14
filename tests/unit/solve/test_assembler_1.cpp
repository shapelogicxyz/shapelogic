// SPDX-License-Identifier: GPL-3.0-or-later
// Assembler 1. The original's assembler-1 cases of test/test_solver.cpp
// come first (adapted to the new API: runs take a stop token and options,
// positions are strings, the vector search's switches are options); the
// cases after them test the rules of decision 0009 on assembler 1, and that
// a damaged position is refused before it is used.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/base/thread_pool.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/assembler_1.h"
#include "shapelogic/solve/simd_huang_cover.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iterator>
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

// An assembler 1 for problem `prob` of `p` with its matrix made.
std::unique_ptr<Assembler1> made(const Puzzle& p, unsigned int prob = 0,
                                 const MatrixOptions& options = {}) {
  auto a = std::make_unique<Assembler1>(p, p.getProblem(prob));
  REQUIRE(a->createMatrix(options) == AssemblerError::None);
  return a;
}

// All assemblies of a problem, on one thread with the plain search.
Fingerprints allAssemblies(const std::string& fixture, unsigned int prob = 0,
                           bool reduce = false) {
  const Puzzle p = test::loadFixture(fixture);
  auto a = made(p, prob);
  if (reduce) {
    (void)a->reduce(StopToken{});
  }
  const test::RunResult r = runOnce(*a, searchOptions(1, nullptr, false));
  REQUIRE(r.end == RunEnd::Finished);
  return r.found;
}

}  // namespace

// ---- from the original's test_solver.cpp ---------------------------------------

TEST_CASE("Assembler 1: regression counts of the examples", "[solve][assembler]") {
  struct Case {
    const char* file;
    std::size_t assemblies;
  };
  // Cube in Cage and Demo Mirror Paradox were the original's assembler 1
  // regressions; the others are the examples that need assembler 1
  for (const Case c :
       {Case{"CubeInCage.xmpuzzle", 96}, Case{"DemoMirrorParadox.xmpuzzle", 1},
        Case{"PiecesOfEight.xmpuzzle", 6}, Case{"DemoPieceGenerator.xmpuzzle", 928},
        Case{"AlPackino.xmpuzzle", 1}, Case{"12PieceSeparation.xmpuzzle", 1},
        Case{"FourPieceTetrahedron.xmpuzzle", 1}}) {
    INFO(c.file);
    const Puzzle p = test::loadFixture(c.file);
    CHECK(chooseAssembler(p.getProblem(0), SolverType::Classic) == AssemblerKind::Assembler1);
    auto a = made(p);
    const test::RunResult r = runOnce(*a, searchOptions(1, nullptr));
    CHECK(r.end == RunEnd::Finished);
    CHECK(r.found.size() == c.assemblies);
    CHECK(a->iterations() > 0);
    CHECK(a->finished() >= 1.0F);
  }
}

TEST_CASE("Assembler 1: Solid Six Piece Burrs (ranges)", "[solve][assembler][slow]") {
  const Puzzle p = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
  auto a = made(p);
  (void)a->reduce(StopToken{});
  CHECK(runOnce(*a, searchOptions(1, nullptr)).found.size() == 588);
}

TEST_CASE("Assembler 1: split over threads it finds what one thread finds",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Fingerprints serial = allAssemblies("CubeInCage.xmpuzzle");
  CHECK(serial.size() == 96);

  for (const bool vector : {true, false}) {
    INFO((vector ? "vector" : "plain"));
    const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
    auto a = made(p);
    const test::RunResult r = runOnce(*a, searchOptions(4, &pool, vector));
    // the multiset, not the size: a lost assembly paired with a duplicated
    // one would pass a count comparison
    CHECK(r.end == RunEnd::Finished);
    CHECK(r.found == serial);
    CHECK(a->iterations() > 0);
    CHECK(a->finished() >= 1.0F);
  }
}

// Forces the task generation past its first pass: with many threads, depth
// 1 cannot supply enough tasks and the search is cut again deeper, walking
// the top of the tree once more. That walk used to report assemblies again.
TEST_CASE("Assembler 1: task generation that goes deeper repeats nothing",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Fingerprints serial = allAssemblies("CubeInCage.xmpuzzle");
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  // 256 tasks wanted, unreachable at depth 1
  CHECK(runOnce(*a, searchOptions(64, &pool)).found == serial);
}

// Stopping and continuing on the same assembler must not report the first
// run's assemblies again.
TEST_CASE("Assembler 1: a split search paused and continued reports each assembly once",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  const Fingerprints serial = allAssemblies("CubeInCage.xmpuzzle");
  REQUIRE(serial.size() > 1);

  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  const test::RunResult first = runOnce(*a, searchOptions(4, &pool), {}, 1);
  REQUIRE(first.found.size() == 1);
  CHECK(first.end == RunEnd::Stopped);

  const test::RunResult rest = runOnce(*a, searchOptions(4, &pool));
  CHECK(rest.end == RunEnd::Finished);
  Fingerprints both = first.found;
  both.insert(rest.found.begin(), rest.found.end());
  CHECK(both == serial);
}

TEST_CASE("Assembler 1: a callback that answers false stops a split search at once",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  int count = 0;
  const RunEnd end = a->assemble(
      [&count](Assembly) {
        count++;
        return false;
      },
      searchOptions(4, &pool), StopToken{});
  CHECK(count == 1);
  CHECK(end == RunEnd::Stopped);
  CHECK(a->finished() < 1.0F);
}

// A split search on a puzzle with a symmetry breaker: the workers call
// smallerRotationExists(), which reads the shapes' lazily filled caches.
// Cube in Cage has no symmetry breaker, so its workers never go there; Demo
// Mirror Paradox (problem 1) does, and is the only fixture that covers that
// path for assembler 1. Keep the puzzle (AGENTS.md); the premise is checked
// below.
TEST_CASE("Assembler 1: split over threads on a symmetry-breaking puzzle",
          "[solve][assembler][parallel][tsan]") {
  const Fingerprints serial = allAssemblies("DemoMirrorParadox.xmpuzzle", 1);
  REQUIRE(!serial.empty());

  // the premise: keeping rotations gives more, so the filter is active
  {
    const Puzzle p = test::loadFixture("DemoMirrorParadox.xmpuzzle");
    REQUIRE(p.getNumberOfProblems() > 1);
    MatrixOptions keep;
    keep.keepRotations = true;
    auto a = made(p, 1, keep);
    INFO("symmetry breaking must be active for this test to mean anything");
    REQUIRE(runOnce(*a, searchOptions(1, nullptr)).found.size() > serial.size());
  }

  // a fresh load: the caches start cold
  ThreadPool pool(4);
  for (const bool vector : {true, false}) {
    const Puzzle p = test::loadFixture("DemoMirrorParadox.xmpuzzle");
    auto a = made(p, 1);
    CHECK(runOnce(*a, searchOptions(4, &pool, vector)).found == serial);
  }
}

// The leading flag of the saved text must not break an ordinary restore: a
// plain search stopped part way, the state the application saves.
TEST_CASE("Assembler 1: the position of a stopped plain search restores",
          "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  const test::RunResult first = runOnce(*a, searchOptions(1, nullptr, false), {}, 1);
  REQUIRE(first.found.size() == 1);
  REQUIRE(a->finished() < 1.0F);

  const SavedPosition pos = a->savePosition();
  CHECK(pos.version == "2.1");
  CHECK(pos.text[0] == '0');

  auto restored = made(p);
  CHECK(restored->restorePosition(pos.text, pos.version) == AssemblerError::None);
  CHECK(restored->savePosition().text == pos.text);
}

// finished() must not claim a search is through because an earlier run
// left its tasks all done.
TEST_CASE("Assembler 1: a later run does not report the progress of an earlier one",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  REQUIRE(runOnce(*a, searchOptions(4, &pool)).end == RunEnd::Finished);
  REQUIRE(a->finished() >= 1.0F);

  // a fresh assembler on the same problem starts at 0
  auto again = made(p);
  CHECK(again->finished() < 1.0F);
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
  auto a = made(p);
  const test::RunResult first = runOnce(*a, searchOptions(threads, &pool, vector), {}, stopAt);
  REQUIRE(first.end == RunEnd::Stopped);
  REQUIRE(a->finished() < 1.0F);

  test::RunResult rest;
  if (viaSave) {
    const SavedPosition pos = a->savePosition();
    const Puzzle p2 = test::loadFixture(fixture);
    auto restored = made(p2);
    REQUIRE(restored->restorePosition(pos.text, pos.version) == AssemblerError::None);
    // restoring and saving again writes the same position (signatures come
    // from an unordered set, so only their number is compared)
    const SavedPosition again = restored->savePosition();
    const auto head = [](const std::string& s) { return s.substr(0, s.find(" S ")); };
    CHECK(head(again.text) == head(pos.text));
    CHECK(again.text.size() == pos.text.size());
    rest = runOnce(*restored, searchOptions(threads, &pool, vector));
  } else {
    rest = runOnce(*a, searchOptions(threads, &pool, vector));
  }
  CHECK(rest.end == RunEnd::Finished);

  Fingerprints both = first.found;
  both.insert(rest.found.begin(), rest.found.end());
  CHECK(both == all);
}

}  // namespace

TEST_CASE("Assembler 1: a stopped search resumes, saved or not, reporting each assembly once",
          "[solve][assembler][parallel][resume]") {
  for (const bool viaSave : {false, true}) {
    checkStopAndResume("CubeInCage.xmpuzzle", 4, true, 1, viaSave);
    checkStopAndResume("CubeInCage.xmpuzzle", 1, true, 1, viaSave);
    checkStopAndResume("CubeInCage.xmpuzzle", 4, false, 30, viaSave);
    // the plain search on one thread, at every stop point
    for (unsigned int stopAt = 1; stopAt < 96; stopAt += 7) {
      checkStopAndResume("CubeInCage.xmpuzzle", 1, false, stopAt, viaSave);
    }
    checkStopAndResume("DemoPieceGenerator.xmpuzzle", 1, false, 500, viaSave);
    checkStopAndResume("DemoPieceGenerator.xmpuzzle", 4, false, 500, viaSave);
  }
}

namespace {

// The assemblies of problem 0 of a fixture, and the iterations it took.
Fingerprints searched(const std::string& fixture, unsigned int threads, ThreadPool* pool,
                      bool reduce, bool vector, std::uint64_t* iterations,
                      std::uint64_t memoryBudget = SearchOptions{}.vectorMemoryBudget) {
  const Puzzle p = test::loadFixture(fixture);
  auto a = made(p);
  if (reduce) {
    (void)a->reduce(StopToken{});
  }
  SearchOptions o = searchOptions(threads, pool, vector);
  o.vectorMemoryBudget = memoryBudget;
  const test::RunResult r = runOnce(*a, o);
  CHECK(r.end == RunEnd::Finished);
  CHECK(a->finished() >= 1.0F);
  *iterations = a->iterations();
  return r.found;
}

// The vector and the plain search find the same assemblies, on one thread
// and on several, from the matrix as made and from the reduced one.
void vectorMatchesPlain(const std::vector<std::string>& files, bool referenceReduced,
                        unsigned int expectVector) {
  ThreadPool pool(4);
  unsigned int tookTheVectorSearch = 0;
  for (const std::string& file : files) {
    std::uint64_t plainIterations = 0;
    std::uint64_t vectorIterations = 0;
    const Fingerprints plain =
        searched(file, 1, nullptr, referenceReduced, false, &plainIterations);
    REQUIRE(!plain.empty());
    for (const bool reduce : {false, true}) {
      for (const unsigned int threads : {1U, 4U}) {
        INFO(file << ", " << threads << " threads" << (reduce ? ", reduced" : ""));
        CHECK(searched(file, threads, &pool, reduce, true, &vectorIterations) == plain);
        if (threads == 1 && reduce == referenceReduced && vectorIterations != plainIterations) {
          tookTheVectorSearch++;
        }
      }
    }
  }
  // if no puzzle took the vector search, this compared the plain search
  // with itself
  CHECK(tookTheVectorSearch == expectVector);
}

}  // namespace

// The assembler-1 rows of the original's "vector and plain search find the
// same assemblies": piece ranges (Pieces of Eight), ranges with variable
// voxels (Demo Piece Generator, which keeps the vector search out), and the
// rest; Solid Six Piece Burrs is the [slow] case below.
TEST_CASE("Assembler 1: the vector and the plain search find the same assemblies",
          "[solve][assembler][simd][parallel]") {
  vectorMatchesPlain({"PiecesOfEight.xmpuzzle", "DemoPieceGenerator.xmpuzzle",
                      "AlPackino.xmpuzzle", "12PieceSeparation.xmpuzzle",
                      "FourPieceTetrahedron.xmpuzzle", "CubeInCage.xmpuzzle",
                      "DemoMirrorParadox.xmpuzzle"},
                     false, 6);
}

// A range puzzle, and the one large enough to keep several threads busy;
// the plain search needs seconds for it as made, so the reference is taken
// from the reduced matrix.
TEST_CASE("Assembler 1: the vector and the plain search on Solid Six Piece Burrs",
          "[solve][assembler][simd][parallel][slow]") {
  vectorMatchesPlain({"SolidSixPieceBurrs.xmpuzzle"}, true, 1);
}

// A split search stopped and saved by an older version carries signatures
// taken from the rows in the order the plain search placed them (flag 2);
// today's are taken from the sorted assembly (flag 4). Such a file goes on
// with the kind it has: loaded, it is saved again as it was, and continued
// it reports every assembly once.
TEST_CASE("Assembler 1: a split search saved by an older version carries on",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  const Fingerprints all = allAssemblies("CubeInCage.xmpuzzle");
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");

  // What the older version wrote: loading a flag 2 position is what switches
  // an assembler to that kind, so start from a search that has not reported
  // anything yet.
  std::string older;
  {
    auto a = made(p);
    (void)a->assemble([](Assembly) { return false; }, searchOptions(4, &pool), StopToken{});
    older = a->savePosition().text;
    REQUIRE(older.compare(0, 2, "4 ") == 0);
    // no task finished and nothing reported: valid for either kind
    const std::size_t c = older.find(" C ");
    const std::size_t sg = older.find(" S ");
    REQUIRE(c != std::string::npos);
    REQUIRE(sg != std::string::npos);
    for (std::size_t i = c + 3; i < sg; i++) {
      if (older[i] == '1') {
        older[i] = '0';
      }
    }
    older = "2 " + older.substr(2, sg - 2) + " S 0";
  }

  // stop it part way, save, load, finish
  test::RunResult first;
  std::string again;
  {
    auto a = made(p);
    REQUIRE(a->restorePosition(older, "2.1") == AssemblerError::None);
    first = runOnce(*a, searchOptions(4, &pool), {}, 20);
    REQUIRE(a->finished() < 1.0F);
    again = a->savePosition().text;
  }
  // still the older kind
  CHECK(again.compare(0, 2, "2 ") == 0);

  test::RunResult rest;
  {
    auto a = made(p);
    REQUIRE(a->restorePosition(again, "2.1") == AssemblerError::None);
    rest = runOnce(*a, searchOptions(4, &pool));
    CHECK(rest.end == RunEnd::Finished);
    CHECK(a->finished() >= 1.0F);
  }
  Fingerprints both = first.found;
  both.insert(rest.found.begin(), rest.found.end());
  CHECK(both == all);
}

// The memory the vector search would need decides whether it is used: a
// row's mask is as wide as the solver's size class.
TEST_CASE("Assembler 1: the vector search is refused past its memory budget",
          "[solve][assembler][simd]") {
  const std::uint64_t budget = SearchOptions{}.vectorMemoryBudget;
  CHECK(budget == std::uint64_t{256} << 20);
  // 100000 rows of 20 nodes: small masks fit easily, 4 KB masks do not
  CHECK(simdHuangFitsMemory(200, 100000, 2000000, budget));
  CHECK(simdHuangFitsMemory(2048, 100000, 2000000, budget));
  CHECK_FALSE(simdHuangFitsMemory(32768, 100000, 2000000, budget));
  // the mask doubles with the size class
  CHECK(simdHuangMemoryEstimate(256, 1000, 0) < simdHuangMemoryEstimate(257, 1000, 0));
  CHECK(simdHuangMemoryEstimate(32768, 1000, 0) - simdHuangMemoryEstimate(16384, 1000, 0) ==
        1000U * 2048U);
  // more nodes, more memory
  CHECK(simdHuangMemoryEstimate(256, 1000, 20000) > simdHuangMemoryEstimate(256, 1000, 0));

  // and through the option: with no budget the plain search does the work,
  // with the same answer
  std::uint64_t withBudget = 0;
  std::uint64_t without = 0;
  const Fingerprints a = searched("CubeInCage.xmpuzzle", 1, nullptr, false, true, &withBudget);
  const Fingerprints b = searched("CubeInCage.xmpuzzle", 1, nullptr, false, true, &without, 0);
  CHECK(a == b);
  CHECK(withBudget != without);
}

// ---- the run layer (decision 0009) ------------------------------------------------

TEST_CASE("Assembler 1: random pauses lose nothing and repeat nothing",
          "[solve][assembler][parallel][resume]") {
  const Puzzle cage = test::loadFixture("CubeInCage.xmpuzzle");
  test::randomPauses([&cage] { return test::prepared<Assembler1>(cage); }, 96, 10, 20261007,
                     test::kAllPaths);
  // ranges with variable voxels: the plain search only
  const Puzzle gen = test::loadFixture("DemoPieceGenerator.xmpuzzle");
  test::randomPauses([&gen] { return test::prepared<Assembler1>(gen); }, 928, 150, 9,
                     {{1, false}, {4, false}});
}

TEST_CASE("Assembler 1: random pauses on Solid Six Piece Burrs",
          "[solve][assembler][parallel][resume][slow]") {
  const Puzzle p = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
  test::randomPauses([&p] { return test::prepared<Assembler1>(p); }, 588, 120, 4712,
                     test::kAllPaths);
}

// A stop made before a run starts is seen when it starts; the next run with
// a fresh token does the rest. The plain search, as the original's, stops
// only where its position can be saved (states 1, 2 and 5), and hands over
// what it finds on the way there: those it does not find again.
TEST_CASE("Assembler 1: a stop made before the run starts is not lost",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Fingerprints all = allAssemblies("CubeInCage.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    for (const bool vector : {true, false}) {
      INFO(threads << " threads" << (vector ? "" : ", plain"));
      const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
      auto a = made(p);

      const StopSource early;
      early.request(StopKind::Pause);
      const test::RunResult stopped =
          runOnce(*a, searchOptions(threads, &pool, vector), early.token());
      CHECK(stopped.end == RunEnd::Stopped);
      if (threads == 1 && !vector) {
        CHECK(stopped.found.size() < 5);
      } else {
        CHECK(stopped.found.empty());
      }
      CHECK(a->finished() < 1.0F);

      const test::RunResult rest = runOnce(*a, searchOptions(threads, &pool, vector));
      CHECK(rest.end == RunEnd::Finished);
      Fingerprints both = stopped.found;
      both.insert(rest.found.begin(), rest.found.end());
      CHECK(both == all);
    }
  }
}

// finished() and iterations() are read by other threads while a run goes;
// ThreadSanitizer checks they are safe to read (the original read the
// progress stacks under a mutex while the search pushed and popped them).
TEST_CASE("Assembler 1: progress is read from another thread while it searches",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("DemoPieceGenerator.xmpuzzle");
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
    CHECK(r.found.size() == 928);
    CHECK(a->finished() >= 1.0F);
    CHECK(a->iterations() > 0);
  }
}

// The plain search publishes its progress as it goes: a paused search
// reports how far it is, between 0 and 1, and more after a later pause.
TEST_CASE("Assembler 1: the plain search's progress grows", "[solve][assembler]") {
  const Puzzle p = test::loadFixture("DemoPieceGenerator.xmpuzzle");
  auto a = made(p);
  float last = 0;
  for (int run = 0; run < 4; run++) {
    (void)runOnce(*a, searchOptions(1, nullptr, false), {}, 200);
    const float f = a->finished();
    CHECK(f > last);
    CHECK(f < 1.0F);
    last = f;
  }
  CHECK(runOnce(*a, searchOptions(1, nullptr, false)).end == RunEnd::Finished);
  CHECK(a->finished() == 1.0F);
}

TEST_CASE("Assembler 1: an exception in the search comes back to the caller",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " threads");
    auto a = made(p);
    unsigned int seen = 0;
    CHECK_THROWS_AS(a->assemble(
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

// A paused solve goes into the puzzle file (storePosition, the writer), is
// read back, and carries on: every assembly once. Each kind of position:
// the plain search (flag 0), the vector search (3), the split search (4).
TEST_CASE("Assembler 1: a paused search saved in a puzzle file carries on after loading",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  const Fingerprints all = allAssemblies("CubeInCage.xmpuzzle");
  struct Path {
    unsigned int threads;
    bool vector;
    char flag;
  };
  for (const Path path : {Path{1, false, '0'}, Path{1, true, '3'}, Path{4, true, '4'}}) {
    INFO(path.threads << " threads" << (path.vector ? "" : ", plain"));
    Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.removeAllSolutions();

    Fingerprints first;
    {
      Assembler1 a(p, pr);
      REQUIRE(a.createMatrix({}) == AssemblerError::None);
      (void)a.reduce(StopToken{});
      REQUIRE(beginSolving(pr, a) == AssemblerError::None);
      first = runOnce(a, searchOptions(path.threads, &pool, path.vector), {}, 30).found;
      REQUIRE(first.size() == 30);
      storePosition(pr, a);
    }
    CHECK(pr.getAssemblerVersion() == "2.1");
    CHECK(pr.getAssemblerState()[0] == path.flag);

    const std::string xml = io::writePuzzle(p);
    CHECK(xml.find("<assembler version=\"2.1\">") != std::string::npos);
    Result<Puzzle> loaded = io::parsePuzzle(xml);
    REQUIRE(loaded);
    Problem& pr2 = loaded->getProblem(0);
    REQUIRE(pr2.getSolveState() == ProblemState::Solving);
    REQUIRE(canStartSolving(pr2));

    Assembler1 a(*loaded, pr2);
    REQUIRE(a.createMatrix({}) == AssemblerError::None);
    (void)a.reduce(StopToken{});
    REQUIRE(beginSolving(pr2, a) == AssemblerError::None);
    const test::RunResult rest = runOnce(a, searchOptions(path.threads, &pool, path.vector));
    CHECK(rest.end == RunEnd::Finished);
    Fingerprints both = first;
    both.insert(rest.found.begin(), rest.found.end());
    CHECK(both == all);
  }
}

TEST_CASE("Assembler 1: a finished position restores as finished", "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  REQUIRE(runOnce(*a, searchOptions(1, nullptr, false)).end == RunEnd::Finished);
  const SavedPosition pos = a->savePosition();

  auto b = made(p);
  REQUIRE(b->restorePosition(pos.text, pos.version) == AssemblerError::None);
  CHECK(b->finished() >= 1.0F);
  const test::RunResult r = runOnce(*b, searchOptions(1, nullptr));
  CHECK(r.end == RunEnd::Finished);
  CHECK(r.found.empty());
}

// The original replayed whatever it read; the new core checks a position
// first, so a damaged one is refused instead of being read out of bounds,
// and beginSolving() then starts over.
TEST_CASE("Assembler 1: a damaged position is refused before it is used",
          "[solve][assembler][resume]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");

  // positions of each kind to damage
  std::vector<std::string> texts;
  for (const auto& [threads, vector] : {std::pair{1U, false}, std::pair{1U, true},
                                        std::pair{4U, true}}) {
    auto a = made(p);
    (void)runOnce(*a, searchOptions(threads, &pool, vector), {}, 40);
    texts.push_back(a->savePosition().text);
  }
  REQUIRE(texts[0][0] == '0');
  REQUIRE(texts[1][0] == '3');
  REQUIRE(texts[2][0] == '4');

  SECTION("hand-made damage") {
    const char* damaged[] = {
        "",                                       // nothing
        "7 0 1 0 1 0 0 0 0 0 ",                   // no such flag
        "0 0 1 0 1 0 0 0 0",                      // the last vector missing
        "0 0 2 0 0 1 0 0 0 0 0 ",                 // two frames, one next row
        "0 0 1 3 1 0 0 0 0 0 ",                   // a state that is never saved
        "0 0 2 1 0 2 0 0 0 0 0 0 ",               // state 1 without its column
        "0 0 1 2 1 0 0 0 0 0 ",                   // state 2 without a column
        "0 1 99999 1 5 1 0 0 2 0 0 1 0 1 1 ",     // a row that is no node
        "0 0 1 0 1 99999 0 0 0 0 ",               // a next row that is no node
        "0 0 1 0 1 0 0 0 1 5 0 ",                 // progress entries that do not pair
        "0 4000000000 1 2 3 ",                    // a count longer than the text
        "3 1 600 1 0 1 0 0 0 0 0  K 5",           // a vector search not at its start
        "4 0 1 0 1 0 0 0 0 0  T 1 1 5 1 0 0 0 0 C 0 S 0",  // a task in state 5 with no rows
        "4 0 1 0 1 0 0 0 0 0  T 1 1 0 1 0 0 0 0 C 0 S 0 ",  // fine, but: see below
    };
    for (std::size_t i = 0; i + 1 < std::size(damaged); i++) {
      INFO(damaged[i]);
      auto a = made(p);
      CHECK(a->restorePosition(damaged[i], "2.1") == AssemblerError::CannotRestoreSyntax);
    }
    // a task at the start of the search is a valid position
    auto a = made(p);
    CHECK(a->restorePosition(damaged[std::size(damaged) - 1], "2.1") == AssemblerError::None);
    // and an older version's interrupted split search cannot go on
    auto b = made(p);
    CHECK(b->restorePosition("1 0 1 0 1 0 0 0 0 0 ", "2.1") ==
          AssemblerError::CannotRestoreInterrupted);
  }

  SECTION("random damage is refused or restores, never read out of bounds") {
    std::mt19937 rng(77);
    unsigned int refused = 0;
    unsigned int searched = 0;
    unsigned int broken = 0;
    for (const std::string& text : texts) {
      for (int k = 0; k < 300; k++) {
        // replace one number by another, or cut the text short
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
          const unsigned int v =
              k % 2 == 0 ? std::uniform_int_distribution<unsigned int>(0, 10)(rng)
                         : std::uniform_int_distribution<unsigned int>(0, 2000000)(rng);
          t.replace(at, end - at, std::to_string(v));
        }
        INFO(t);
        auto a = made(p);
        if (a->restorePosition(t, "2.1") != AssemblerError::None) {
          refused++;
          continue;
        }
        // Accepted: the stacks fit the matrix, so the search runs in bounds
        // (the sanitizer build watches). A position that fits but is no
        // place the search could have been may break one of its
        // invariants, which stops it with a report rather than wrong
        // answers.
        try {
          const test::RunResult r = runOnce(*a, searchOptions(1, nullptr, false));
          CHECK(r.end == RunEnd::Finished);
          searched++;
        } catch (const InternalError&) {
          broken++;
        }
      }
    }
    CHECK(refused > 100);
    CHECK(searched > 0);
    INFO(broken << " broke an invariant");
  }
}

// ---- from the audit of stage 3 ---------------------------------------------------

namespace {

// The vectors of a plain search's position: rows, taskStack, nextRowStack,
// columnStack, hiddenRows, finishedA, finishedB (each written as its size
// and its values).
std::vector<std::vector<unsigned int>> stacksOf(const std::string& text) {
  std::istringstream in(text);
  unsigned int flag = 0;
  in >> flag;
  std::vector<std::vector<unsigned int>> v(7);
  for (std::vector<unsigned int>& x : v) {
    std::size_t n = 0;
    in >> n;
    x.resize(n);
    for (unsigned int& e : x) {
      in >> e;
    }
  }
  return v;
}

std::string positionOf(const std::vector<std::vector<unsigned int>>& v) {
  std::string out = "0 ";
  for (const std::vector<unsigned int>& x : v) {
    out += std::to_string(x.size()) + " ";
    for (const unsigned int e : x) {
      out += std::to_string(e) + " ";
    }
  }
  return out;
}

}  // namespace

// At its end the plain search stands on one frame with the first column
// still closed, a state it can be paused in. Continued with threads or the
// vector search it was taken for a search that had not started and reported
// everything again.
TEST_CASE("Assembler 1: a plain search stopped anywhere continues the same on every path",
          "[solve][assembler][resume][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto make = [&p] { return std::unique_ptr<Assembler>(made(p)); };
  const Fingerprints all = allAssemblies("CubeInCage.xmpuzzle");

  // the last state, and the first with a frame in state 2 and in state 5
  std::vector<unsigned long> steps;
  {
    auto a = made(p);
    bool two = false;
    bool five = false;
    bool end = false;
    for (unsigned long k = 1; k < 100000; k++) {
      a->debugStep(1);
      const std::vector<unsigned int> tasks = stacksOf(a->savePosition().text)[1];
      if (tasks.empty()) {
        break;
      }
      const bool hasTwo = std::find(tasks.begin(), tasks.end(), 2U) != tasks.end();
      const bool hasFive = std::find(tasks.begin(), tasks.end(), 5U) != tasks.end();
      const bool atEnd = tasks.size() == 1 && tasks[0] == 1;
      if ((hasTwo && !two) || (hasFive && !five) || (atEnd && !end)) {
        steps.push_back(k);
      }
      two = two || hasTwo;
      five = five || hasFive;
      end = end || atEnd;
    }
    REQUIRE(two);
    REQUIRE(five);
    REQUIRE(end);
  }
  for (const unsigned long k : {0UL, 40UL, 2000UL}) {
    steps.push_back(k);
  }

  for (const unsigned long k : steps) {
    const Fingerprints rest = test::continueOnEveryPath(make, k, pool);
    CHECK(test::isSubMultiset(rest, all));
  }
}

TEST_CASE("Assembler 1: a vector search stopped at once leaves no trace in a plain position",
          "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  test::vectorStoppedAtOnceThenPlain([&p] { return std::unique_ptr<Assembler>(made(p)); },
                                     allAssemblies("CubeInCage.xmpuzzle"));
}

// The vector search left the last solution in the rows when it was through,
// which no restore accepts.
TEST_CASE("Assembler 1: every search kind run to its end restores as finished",
          "[solve][assembler][resume][parallel]") {
  ThreadPool pool(4);
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  test::finishedRestoresOnEveryPath([&p] { return std::unique_ptr<Assembler>(made(p)); }, pool);

  auto a = made(p);
  REQUIRE(runOnce(*a, searchOptions(1, nullptr, true)).end == RunEnd::Finished);
  // and the current assembly is an empty one, as after the plain search
  const Assembly current = a->currentAssembly();
  for (unsigned int i = 0; i < current.placementCount(); i++) {
    CHECK_FALSE(current.isPlaced(i));
  }
}

// A frame past its column choice stands on a row of that column; the check
// took any node number, a header too, whose count the search then used as
// a column.
TEST_CASE("Assembler 1: a frame's row must be a row of its column", "[solve][assembler][resume]") {
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  for (const unsigned int state : {2U, 5U}) {
    INFO("a frame in state " << state);
    auto a = made(p);
    std::vector<std::vector<unsigned int>> v;
    for (int k = 0; k < 100000; k++) {
      a->debugStep(1);
      v = stacksOf(a->savePosition().text);
      if (std::find(v[1].begin(), v[1].end(), state) != v[1].end()) {
        break;
      }
    }
    const auto at = static_cast<std::size_t>(
        std::find(v[1].begin(), v[1].end(), state) - v[1].begin());
    REQUIRE(at < v[1].size());

    // as it is, it restores
    CHECK(made(p)->restorePosition(positionOf(v), "2.1") == AssemblerError::None);
    // a header in its place does not
    v[2][at] = 1;
    CHECK(made(p)->restorePosition(positionOf(v), "2.1") == AssemblerError::CannotRestoreSyntax);
  }
}

TEST_CASE("Assembler 1: errors of createMatrix", "[solve][assembler]") {
  SECTION("too many units") {
    Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.setShapeMinimum(p, pr.getShapeIdOfPart(0), 2);
    pr.setShapeMaximum(p, pr.getShapeIdOfPart(0), 2);
    Assembler1 a(p, pr);
    CHECK(a.createMatrix({}) == AssemblerError::TooManyUnits);
    CHECK(a.errorParam() > 0);
  }
  SECTION("too few units") {
    Puzzle p = test::pentominoPuzzle(10, 7);
    Problem& pr = p.getProblem(0);
    pr.setShapeMaximum(p, pr.getShapeIdOfPart(0), 2);
    Assembler1 a(p, pr);
    CHECK(a.createMatrix({}) == AssemblerError::TooFewUnits);
    CHECK(a.errorParam() == 5);
  }
  SECTION("a piece that fits nowhere") {
    Puzzle p = test::pentominoPuzzle(30, 2);
    Problem& pr = p.getProblem(0);
    pr.setShapeMaximum(p, pr.getShapeIdOfPart(1), 2);
    Assembler1 a(p, pr);
    CHECK(a.createMatrix({}) == AssemblerError::CannotPlace);
  }
  SECTION("ranges get assembler 1") {
    Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
    Problem& pr = p.getProblem(0);
    pr.setShapeMaximum(p, pr.getShapeIdOfPart(0), 2);
    CHECK(dynamic_cast<Assembler1*>(makeAssembler(p, pr, SolverType::Classic).get()) != nullptr);
    CHECK(dynamic_cast<Assembler1*>(makeAssembler(p, pr, SolverType::Crowell).get()) != nullptr);
  }
}

TEST_CASE("Assembler 1: placements, the debugger and the current assembly",
          "[solve][assembler]") {
  const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  auto a = made(p);
  REQUIRE(a->piecePlacementSupported());

  const unsigned int pieces = p.getProblem(0).getNumberOfPieces();
  for (unsigned int piece = 0; piece < pieces; piece++) {
    const unsigned int count = a->piecePlacementCount(piece);
    CHECK(count > 0);
    std::set<unsigned int> nodes;
    unsigned int node = 0;
    for (unsigned int i = 0; i < count; i++) {
      unsigned char tran = 0;
      int x = 0;
      int y = 0;
      int z = 0;
      node = a->piecePlacement(node, i == 0 ? 0 : 1, piece, &tran, &x, &y, &z);
      nodes.insert(node);
    }
    // a shape's pieces share its placements
    CHECK(nodes.size() <= count);
    CHECK(!nodes.empty());
  }

  const Assembly before = a->currentAssembly();
  CHECK(before.placementCount() == pieces);
  for (unsigned int i = 0; i < pieces; i++) {
    CHECK_FALSE(before.isPlaced(i));
  }

  a->debugStep(10);
  CHECK(a->iterations() >= 10);
  const Assembly after = a->currentAssembly();
  unsigned int placed = 0;
  for (unsigned int i = 0; i < after.placementCount(); i++) {
    placed += after.isPlaced(i) ? 1 : 0;
  }
  CHECK(placed > 0);
}
