// SPDX-License-Identifier: GPL-3.0-or-later
// The solve session. Ported from the solve-thread cases of the original's
// test/test_solver.cpp ("Solver: ..."), and from "solve progress: asked for
// all through a solve, it only goes forward" and the EnigmaTIC case of
// test/test_rotation_solvers.cpp, which ran the solve thread. The
// original's thread-limit case keeps what holds without a process-wide
// limit (splitThreads is tested in test_thread_pool.cpp). Its cases on
// SolidSixPieceBurrs are [slow] at -O0; each has a quick twin on Demo Piece
// Generator (928 assemblies, 927 solutions). The cases after the ported ones
// are new: random pauses, a pause-delay sweep, stops before a run starts,
// abort, errors, the order of the saved solutions, BurrTools 2 paused while
// it drains, and the [parallel] cases ThreadSanitizer runs.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "bt/base/solve_state.h"
#include "bt/base/stop.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/solve_progress.h"
#include "bt/solve/solve_session.h"
#include "bt/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace bt;

namespace {

// A fixture without what it was saved with: Solve starts from a clean slate.
Puzzle fresh(const std::string& name) {
  Puzzle p = test::loadFixture(name);
  p.getProblem(0).removeAllSolutions();
  return p;
}

// The puzzle saved and loaded again, as after quitting.
Puzzle reloaded(const Puzzle& p) {
  Result<Puzzle> r = io::parsePuzzle(io::writePuzzle(p));
  REQUIRE(r.has_value());
  return std::move(*r);
}

SolveOptions takeApart(unsigned int threads = 0) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = threads;
  return o;
}

// What a finished solve found: the counts and the saved solutions, each as
// its assembly and level text (a multiset; the order has a case of its own).
struct Totals {
  std::uint64_t assemblies = 0;
  std::uint64_t solutions = 0;
  /// The assemblies tried apart (recordSearchStats): a take-apart stopped
  /// by a pause and done again counts once.
  std::uint64_t triedApart = 0;
  std::multiset<std::string> saved;
  bool operator==(const Totals&) const = default;
};

Totals totals(const Problem& pr) {
  REQUIRE(pr.getSolveState() == ProblemState::Solved);
  CHECK(pr.getPending().empty());
  Totals t;
  t.assemblies = pr.getNumAssemblies();
  t.solutions = pr.getNumSolutions();
  t.triedApart = pr.getTriedApart();
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    const Solution& s = pr.getSavedSolution(i);
    const Disassembly* d = s.getDisassemblyInfo();
    t.saved.insert(test::fingerprint(s.getAssembly()) + " " +
                   (d != nullptr ? d->movesText() : std::string("-")));
  }
  return t;
}

// Carries the solve on until it has finished.
void solveToEnd(SolveSession& s) {
  for (unsigned int runs = 0; s.state() != SolveState::Finished; runs++) {
    REQUIRE(runs < 50);
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() != SolveState::Error);
  }
}

// One solve from the start to the end.
Totals straight(const std::string& name, const SolveOptions& o) {
  Puzzle p = fresh(name);
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  return totals(p.getProblem(0));
}

// Waits until the run has left the preparation.
void untilPrepared(const SolveSession& s) {
  while (s.state() == SolveState::Preparing) {
    std::this_thread::yield();
  }
}

// Starts a solve of `name` and waits until its search is through and the
// last take-aparts run (Draining), with `stop` called then; true when the
// run was still draining. For test_rotation_solvers, whose one assembly
// takes far longer to take apart (with turns) than to find.
bool stoppedWhileDraining(Puzzle& p, const SolveOptions& o,
                          const std::function<void(SolveSession&)>& stop) {
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  while (s.state() == SolveState::Preparing || s.state() == SolveState::Assembling) {
    std::this_thread::yield();
  }
  const bool draining = s.state() == SolveState::Draining;
  stop(s);
  s.wait();
  return draining;
}

// A solve paused `delay` after it started, then carried on: straight on,
// or with the puzzle saved and loaded again into a new session. True when
// the pause came before the end.
bool pauseAndContinue(const std::string& name, const SolveOptions& o,
                      std::chrono::microseconds delay, bool viaSave, bool soft,
                      const Totals& expected) {
  Puzzle p = fresh(name);
  bool paused = false;
  {
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    if (delay.count() < 0) {
      untilPrepared(s);
    } else {
      std::this_thread::sleep_for(delay);
    }
    if (soft) {
      s.pauseAtSavePoint();
    } else {
      s.pause();
    }
    s.wait();
    REQUIRE(s.state() != SolveState::Error);
    paused = s.state() != SolveState::Finished;
    if (soft && paused) {
      // a save point: the take-aparts under way and queued are done, and
      // the position is there to be saved
      CHECK(s.state() == SolveState::Paused);
      const Problem& pr = p.getProblem(0);
      CHECK(pr.getPending().empty());
      CHECK_FALSE(pr.getAssemblerState().empty());
    }
    if (!viaSave && paused) {
      solveToEnd(s);
    }
  }
  if (viaSave) {
    Puzzle q = reloaded(p);
    if (q.getProblem(0).getSolveState() != ProblemState::Solved) {
      SolveSession s(q, q.getProblem(0), o);
      solveToEnd(s);
    }
    CHECK(totals(q.getProblem(0)) == expected);
  } else {
    CHECK(totals(p.getProblem(0)) == expected);
  }
  return paused;
}

}  // namespace

// ---- ported from the original ------------------------------------------------------

// A whole solve with a thread count and without (the original's "the thread
// limit is kept by every stage", without its process-wide limit).
TEST_CASE("solve session: a solve with two threads finds what one with all finds",
          "[solve][session][slow]") {
  const Totals all = straight("SolidSixPieceBurrs.xmpuzzle", takeApart(0));
  CHECK(all.assemblies == 588);
  CHECK(all.solutions == 179);
  const Totals two = straight("SolidSixPieceBurrs.xmpuzzle", takeApart(2));
  CHECK(two.assemblies == all.assemblies);
  CHECK(two.solutions == all.solutions);
}

TEST_CASE("solve session: pausing and continuing finds every assembly and solution once",
          "[solve][session][resume][slow]") {
  // Pause and continue as the GUI does: wherever the pause lands (in the
  // assembler, with assemblies queued to be taken apart, or part way
  // through taking one apart) nothing may be lost or counted twice.
  // Continue follows straight on, or after saving and loading the puzzle.
  SolveOptions o = takeApart();
  o.solutionLimit = 0;
  const Totals expected = straight("SolidSixPieceBurrs.xmpuzzle", o);
  REQUIRE(expected.solutions > 0);

  int paused = 0;
  for (const int delayMs : {2, 6, 20, 100, 300}) {
    for (const bool viaSave : {false, true}) {
      INFO("pause after " << delayMs << " ms, " << (viaSave ? "saved and loaded" : "straight on"));
      paused += pauseAndContinue("SolidSixPieceBurrs.xmpuzzle", o,
                                 std::chrono::milliseconds(delayMs), viaSave, false, expected)
                    ? 1
                    : 0;
    }
  }
  // the premise: some pauses landed before the solve was over
  CHECK(paused >= 2);
}

TEST_CASE("solve session: a pause as assembling starts loses nothing",
          "[solve][session][resume][slow]") {
  // A pause just after the assembler starts lands while a parallel search
  // is still splitting the work into subtrees; those made so far are not
  // the whole search. The delays sweep that window.
  const Totals expected = straight("SolidSixPieceBurrs.xmpuzzle", takeApart());
  for (const int delayUs : {0, 100, 200, 400, 700, 1000, 1500, 2000, 3000, 5000}) {
    INFO("pause " << delayUs << " us into assembling");
    Puzzle p = fresh("SolidSixPieceBurrs.xmpuzzle");
    SolveSession s(p, p.getProblem(0), takeApart());
    REQUIRE(s.start().has_value());
    untilPrepared(s);
    std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
    s.pause();
    s.wait();
    solveToEnd(s);
    CHECK(totals(p.getProblem(0)).assemblies == expected.assemblies);
    CHECK(totals(p.getProblem(0)).solutions == expected.solutions);
  }
}

TEST_CASE("solve session: an autosave pause saves a solve that carries on to the same totals",
          "[solve][session][resume][slow]") {
  // Only the assembler stops, the take-aparts under way and queued finish,
  // and what is saved carries on. 0 stands for "as soon as the assembler
  // runs", the one sure to land inside the solve whatever the machine.
  SolveOptions o = takeApart();
  o.solutionLimit = 0;
  const Totals expected = straight("SolidSixPieceBurrs.xmpuzzle", o);
  int paused = 0;
  for (const int delayMs : {0, 0, 30, 120}) {
    INFO("autosave pause after " << delayMs << " ms");
    const auto delay = delayMs == 0 ? std::chrono::microseconds(-1)
                                    : std::chrono::microseconds(delayMs * 1000);
    paused += pauseAndContinue("SolidSixPieceBurrs.xmpuzzle", o, delay, true, true, expected) ? 1
                                                                                              : 0;
  }
  CHECK(paused >= 1);
}

TEST_CASE("solve session: no solution limit keeps every solution", "[solve][session]") {
  // No limit and a drop of 0, as a typed-in value can give: the solve
  // starts (the original once looped for ever) and keeps every solution.
  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveOptions o = takeApart();
  o.order = SolutionOrder::Unsorted;
  o.solutionLimit = 0;
  o.solutionDrop = 0;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  const Problem& pr = p.getProblem(0);
  CHECK(pr.getSolveState() == ProblemState::Solved);
  CHECK(pr.getNumSolutions() == 927);
  CHECK(pr.getNumberOfSavedSolutions() == pr.getNumSolutions());
}

TEST_CASE("solve session: a damaged saved position errors once, then solves from the start",
          "[solve][session][resume]") {
  std::string text;
  {
    Puzzle p = fresh("PelikanBurr.xmpuzzle");
    SolveSession s(p, p.getProblem(0), {});
    REQUIRE(s.prepare().has_value());  // prepare only: the problem is now solving
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    REQUIRE(p.getProblem(0).getSolveState() == ProblemState::Solving);
    text = io::writePuzzle(p);
  }
  const std::size_t at = text.find("<assembler version=\"");
  REQUIRE(at != std::string::npos);
  const std::size_t body = text.find('>', at) + 1;
  text.replace(body, text.find("</assembler>", body) - body, "x");

  Result<Puzzle> loaded = io::parsePuzzle(text);
  REQUIRE(loaded.has_value());
  Problem& pr = loaded->getProblem(0);
  REQUIRE(pr.getSolveState() == ProblemState::Solving);
  SolveSession s(*loaded, pr, {});
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Error);
  REQUIRE(s.failure().has_value());
  CHECK(s.failure()->assembler == AssemblerError::CannotRestoreSyntax);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);

  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK(pr.getNumAssemblies() == 12);
}

TEST_CASE("solve progress: asked for all through a solve, it only goes forward",
          "[solve][session][rotation][progress]") {
  // The picture of a running solve can be asked for at any time; the
  // overall figure never goes back and ends at 1. With EnigmaTIC's
  // separation of 3R1.1R1.3R0.2R1 (9 slides and 3 turns), whichever solver.
  for (const SolverType type : {SolverType::Classic, SolverType::Crowell, SolverType::Bt2}) {
    INFO(solverTypeLabel(type));
    Puzzle p = fresh("test_rotation_solvers.xmpuzzle");
    SolveOptions o = takeApart();
    o.checkRotations = true;
    o.solver = type;
    o.takeApart.levelThreadCostUs = 0;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());

    float last = 0;
    unsigned int asked = 0;
    while (isActive(s.state())) {
      const SolveProgress pg = s.progress();
      CHECK(pg.overall >= last);
      CHECK(pg.overall <= 1.0F);
      CHECK(pg.levelFraction <= 1.0F);
      // the line is made without trouble whatever the stage
      (void)pg.activity();
      (void)s.stats();
      last = pg.overall;
      asked++;
      std::this_thread::yield();
    }
    s.wait();

    REQUIRE(s.state() == SolveState::Finished);
    const SolveProgress end = s.progress();
    CHECK(end.stage == SolveStage::Done);
    CHECK(end.overall == 1.0F);
    CHECK(end.activity() == "finished");
    CHECK(end.running.empty());
    CHECK(asked > 0);

    const Problem& pr = p.getProblem(0);
    REQUIRE(pr.getNumberOfSavedSolutions() == 1);
    CHECK(pr.getSolutionsWithRotations());
    const Separation* tree = pr.getSavedSolution(0).getDisassembly();
    REQUIRE(tree != nullptr);
    CHECK(tree->movesText() == "3R1.1R1.3R0.2R1");
    CHECK(tree->sumMoves() == 9);
    CHECK(tree->sumRotations() == 3);
    CHECK(tree->sumSteps() == 12);
  }
}

// ---- new ------------------------------------------------------------------------------

TEST_CASE("solve session: quick twins of the pause cases", "[solve][session][resume]") {
  // Demo Piece Generator: assembler 1, 928 assemblies, 927 solutions, a
  // tenth of a second at -O0.
  SolveOptions o = takeApart();
  o.solutionLimit = 0;
  const Totals expected = straight("DemoPieceGenerator.xmpuzzle", o);
  REQUIRE(expected.assemblies == 928);
  REQUIRE(expected.solutions == 927);
  REQUIRE(expected.saved.size() == 927);

  // all threads (the split search), and one: assembler 1's plain search,
  // which goes on to where it can be saved after a pause and hands over
  // what it finds on the way (kept as pending)
  for (const unsigned int threads : {0U, 1U}) {
    o.threads = threads;
    int paused = 0;
    int softPaused = 0;
    for (const int delayUs : {-1, 0, 1000, 10000}) {
      for (const bool viaSave : {false, true}) {
        for (const bool soft : {false, true}) {
          INFO(threads << " threads, pause " << delayUs << " us in"
                       << (viaSave ? ", saved and loaded" : "") << (soft ? ", at a save point" : ""));
          const bool landed = pauseAndContinue("DemoPieceGenerator.xmpuzzle", o,
                                               std::chrono::microseconds(delayUs), viaSave, soft,
                                               expected);
          paused += landed ? 1 : 0;
          softPaused += landed && soft ? 1 : 0;
        }
      }
    }
    CHECK(paused >= 3);
    // the ones as the search starts land: a save point does stop it
    CHECK(softPaused >= 1);
  }
}

TEST_CASE("solve session: a pause-delay sweep over the whole solve loses nothing",
          "[solve][session][resume][slow]") {
  // Pauses spread evenly over the time a straight solve takes, continued in
  // memory and through a save in turn, for each assembler.
  struct Case {
    const char* file;
    SolverType solver;
    bool throughSaves;  // BurrTools 2 cannot carry on a saved search
  };
  for (const Case c : {Case{"DemoPieceGenerator.xmpuzzle", SolverType::Classic, true},
                       Case{"DraculasDentalDesaster.xmpuzzle", SolverType::Classic, true},
                       Case{"DraculasDentalDesaster.xmpuzzle", SolverType::Bt2, false}}) {
    INFO(c.file << ", " << solverTypeLabel(c.solver));
    SolveOptions o = takeApart(4);
    o.solver = c.solver;
    o.solutionLimit = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const Totals expected = straight(c.file, o);
    const auto whole = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0);
    constexpr int kSteps = 12;
    for (int k = 0; k < kSteps; k++) {
      const auto delay = whole * k / kSteps;
      INFO("pause after " << delay.count() << " us of " << whole.count());
      (void)pauseAndContinue(c.file, o, delay, c.throughSaves && k % 2 == 1, false, expected);
    }
  }
}

TEST_CASE("solve session: random pauses, in memory and through saves, find every solution once",
          "[solve][session][resume][parallel]") {
  // Many short runs: each is paused at a random moment of its search from
  // another thread (now and then at a save point), and carried on in the same
  // session or, every few runs, in a new one on the puzzle saved and
  // loaded again. Assembler 0 and 1, and BurrTools 2 in memory.
  struct Case {
    const char* file;
    SolverType solver;
    bool throughSaves;
  };
  std::mt19937 rng(20261007);
  for (const Case c : {Case{"DemoPieceGenerator.xmpuzzle", SolverType::Classic, true},
                       Case{"DraculasDentalDesaster.xmpuzzle", SolverType::Classic, true},
                       Case{"DraculasDentalDesaster.xmpuzzle", SolverType::Bt2, false}}) {
    INFO(c.file << ", " << solverTypeLabel(c.solver));
    SolveOptions o = takeApart(4);
    o.solver = c.solver;
    o.solutionLimit = 0;
    const Totals expected = straight(c.file, o);

    Puzzle p = fresh(c.file);
    auto session = std::make_unique<SolveSession>(p, p.getProblem(0), o);
    REQUIRE(session->start().has_value());
    for (unsigned int run = 0;; run++) {
      REQUIRE(run < 400);
      // a window that widens as the runs go on: a take-apart that takes
      // longer than every pause would otherwise never be done
      const auto delay = std::chrono::microseconds(
          std::uniform_int_distribution<int>(0, 4000 + 250 * static_cast<int>(run))(rng));
      const bool soft = std::uniform_int_distribution<int>(0, 4)(rng) == 0;
      // from when it assembles: pauses that always hit the preparation
      // (a slow build) would never let the solve get on
      untilPrepared(*session);
      std::this_thread::sleep_for(delay);
      if (soft) {
        session->pauseAtSavePoint();
      } else {
        session->pause();
      }
      session->wait();
      REQUIRE(session->state() != SolveState::Error);
      if (session->state() == SolveState::Finished) {
        break;
      }
      if (c.throughSaves && run % 3 == 2) {
        session.reset();
        p = reloaded(p);
        session = std::make_unique<SolveSession>(p, p.getProblem(0), o);
      }
      REQUIRE(session->resume().has_value());
    }
    CHECK(totals(p.getProblem(0)) == expected);
  }
}

TEST_CASE("solve session: a pause asked for as the run starts is not lost",
          "[solve][session][resume]") {
  // The lost pause of decision 0004: one asked for just as the run starts
  // must stop it, and the solve carries on to the same totals.
  SolveOptions o = takeApart();
  o.solutionLimit = 0;
  const Totals expected = straight("DemoPieceGenerator.xmpuzzle", o);

  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.pause();
  s.wait();
  CHECK(s.state() == SolveState::Paused);

  // and once prepared, a pause right as each run starts assembling
  REQUIRE(s.prepare().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  for (int i = 0; i < 3; i++) {
    REQUIRE(s.resume().has_value());
    s.pause();
    s.wait();
    CHECK(s.state() == SolveState::Paused);
  }
  solveToEnd(s);
  CHECK(totals(p.getProblem(0)) == expected);
}

TEST_CASE("solve session: abort throws the solve away", "[solve][session]") {
  SolveOptions o = takeApart();
  o.solutionLimit = 0;
  const Totals expected = straight("DemoPieceGenerator.xmpuzzle", o);

  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  Problem& pr = p.getProblem(0);
  SolveSession s(p, pr, o);

  SECTION("while it runs") {
    REQUIRE(s.start().has_value());
    untilPrepared(s);
    s.abort();
  }
  SECTION("while it is paused") {
    REQUIRE(s.start().has_value());
    untilPrepared(s);
    s.pause();
    s.wait();
    s.abort();
  }
  SECTION("when it is through") {
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    s.abort();
  }

  CHECK(s.state() == SolveState::Idle);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(pr.getNumberOfSavedSolutions() == 0);
  CHECK(pr.getPending().empty());
  CHECK(pr.getAssemblerState().empty());

  // and the next solve starts from scratch
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(totals(pr) == expected);
}

TEST_CASE("solve session: what it refuses", "[solve][session]") {
  SECTION("a grid without take-apart, asked to take apart") {
    Puzzle p = fresh("AugmentedSecondStellation.xmpuzzle");
    SolveSession s(p, p.getProblem(0), takeApart());
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::Unsupported);
    // it assembles all the same
    REQUIRE(s.setOptions({}).has_value());
    REQUIRE(s.start().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
  }
  SECTION("sliding and stacking puzzles") {
    for (const char* f : {"test_sliding_solver.xmpuzzle", "test_stacking_hanoi.xmpuzzle"}) {
      INFO(f);
      Puzzle p = test::loadFixture(f);
      SolveSession s(p, p.getProblem(0), {});
      const Result<void> r = s.start();
      REQUIRE_FALSE(r.has_value());
      CHECK(r.error().code == Errc::Unsupported);
    }
  }
  SECTION("running twice") {
    // the run is active from the moment start() returns, and this one
    // takes far longer than the calls below
    Puzzle p = fresh("SolidSixPieceBurrs.xmpuzzle");
    SolveSession s(p, p.getProblem(0), takeApart());
    REQUIRE(s.start().has_value());
    for (const Result<void>& r : {s.start(), s.prepare(), s.resume(), s.setOptions({})}) {
      REQUIRE_FALSE(r.has_value());
      CHECK(r.error().code == Errc::InvalidState);
    }
    CHECK(isActive(s.state()));
    s.abort();
    CHECK(s.state() == SolveState::Idle);
  }
  SECTION("carrying on a solved problem") {
    Puzzle p = fresh("PelikanBurr.xmpuzzle");
    SolveSession s(p, p.getProblem(0), {});
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    const Result<void> r = s.resume();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidState);
  }
  SECTION("a problem whose solve was saved without its position") {
    Puzzle p = fresh("PelikanBurr.xmpuzzle");
    p.getProblem(0).markSolving();
    SolveSession s(p, p.getProblem(0), {});
    const Result<void> r = s.resume();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidState);
  }
}

TEST_CASE("solve session: the preparation's errors end the run in Error", "[solve][session]") {
  // a result too small for the pieces
  Puzzle p = fresh("PelikanBurr.xmpuzzle");
  Problem& pr = p.getProblem(0);
  const unsigned int shape = pr.getShapeIdOfPart(0);
  pr.setShapeMaximum(p, shape, pr.getShapeMaximum(shape) + 1);
  pr.setShapeMinimum(p, shape, pr.getShapeMinimum(shape) + 1);
  SolveSession s(p, pr, {});
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Error);
  REQUIRE(s.failure().has_value());
  CHECK(s.failure()->assembler == AssemblerError::TooManyUnits);
  CHECK(s.failure()->message == errorMessage(AssemblerError::TooManyUnits));
  CHECK(s.progress().stage == SolveStage::Error);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
}

TEST_CASE("solve session: a broken invariant in a take-apart ends the solve in Error",
          "[solve][session]") {
  // A pending assembly of two pieces on a problem of six (a damaged file):
  // the take-apart's check fails on a worker thread. The solve stops, its
  // results are dropped (as the original after an assert in its solve
  // thread), and the session says what failed.
  Puzzle p = fresh("PelikanBurr.xmpuzzle");
  Problem& pr = p.getProblem(0);
  SolveSession s(p, pr, takeApart());
  REQUIRE(s.prepare().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  Assembly bad(p.getGridType());
  bad.addPlacement(0, 0, 0, 0);
  bad.addPlacement(0, 1, 0, 0);
  pr.addPending(PendingAssembly{bad, true, 0, 0});

  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Error);
  REQUIRE(s.failure().has_value());
  CHECK(s.failure()->assembler == AssemblerError::None);
  CHECK_FALSE(s.failure()->file.empty());
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(pr.getPending().empty());

  // after the error, the next solve starts over
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK(pr.getNumAssemblies() == 12);
}

TEST_CASE("solve session: what is kept of each solution", "[solve][session]") {
  SECTION("assemblies only: the limit, thinned evenly") {
    Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
    SolveSession s(p, p.getProblem(0), {});
    REQUIRE(s.start().has_value());
    s.wait();
    const Problem& q = p.getProblem(0);
    CHECK(q.getNumAssemblies() == 928);
    REQUIRE(q.getNumberOfSavedSolutions() == 10);
    for (unsigned int i = 0; i + 1 < 10; i++) {
      CHECK(q.getSavedSolution(i).getAssemblyNumber() <
            q.getSavedSolution(i + 1).getAssemblyNumber());
      CHECK(q.getSavedSolution(i).getDisassemblyInfo() == nullptr);
    }
  }
  SECTION("counts only") {
    Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
    SolveOptions o = takeApart();
    o.justCount = true;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    const Problem& q = p.getProblem(0);
    CHECK(q.getNumAssemblies() == 928);
    CHECK(q.getNumSolutions() == 927);
    CHECK(q.getNumberOfSavedSolutions() == 0);
  }
  SECTION("the summary of each take-apart instead of the tree") {
    Puzzle p = fresh("DraculasDentalDesaster.xmpuzzle");
    SolveOptions o = takeApart();
    o.dropDisassemblies = true;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    const Problem& q = p.getProblem(0);
    REQUIRE(q.getNumberOfSavedSolutions() == 1);
    CHECK(q.getSavedSolution(0).getDisassembly() == nullptr);
    REQUIRE(q.getSavedSolution(0).getSeparationInfo() != nullptr);
    CHECK(q.getSavedSolution(0).getSeparationInfo()->movesText() == "3.5.16.6.4.1.1.1");
  }
}

TEST_CASE("solve session: the kept solutions do not depend on which worker finished first",
          "[solve][session][parallel]") {
  // One assembly thread (so the assembly numbers are the same every time)
  // and four take-apart workers: the ten best by moves, equals by assembly
  // number, are the same list as one worker gives (the original kept equals
  // in the order its workers finished them).
  const auto keptList = [](unsigned int workers) {
    Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
    SolveOptions o = takeApart(1);
    o.takeApartWorkers = workers;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    const Problem& pr = p.getProblem(0);
    REQUIRE(pr.getNumberOfSavedSolutions() == 10);
    std::vector<std::string> list;
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      const Solution& sol = pr.getSavedSolution(i);
      list.push_back(std::to_string(sol.getAssemblyNumber()) + " " +
                     test::fingerprint(sol.getAssembly()) + " " +
                     sol.getDisassemblyInfo()->movesText());
      if (i > 0) {
        const Solution& before = pr.getSavedSolution(i - 1);
        const unsigned int m0 = before.getDisassemblyInfo()->sumMoves();
        const unsigned int m1 = sol.getDisassemblyInfo()->sumMoves();
        CHECK((m0 > m1 || (m0 == m1 && before.getAssemblyNumber() < sol.getAssemblyNumber())));
      }
    }
    return list;
  };
  const std::vector<std::string> one = keptList(1);
  for (int i = 0; i < 3; i++) {
    CHECK(keptList(4) == one);
  }
}

TEST_CASE("solve session: BurrTools 2 paused while the last take-aparts run survives a reload",
          "[solve][session][resume]") {
  // BurrTools 2 cannot carry on a saved search, but one that is through is
  // saved as through: a pause while what it found is still being taken
  // apart must keep it all when the puzzle is loaded again.
  SolveOptions o = takeApart(2);
  o.solver = SolverType::Bt2;
  o.checkRotations = true;
  const Totals expected = straight("test_rotation_solvers.xmpuzzle", o);

  bool landed = false;
  for (int attempt = 0; attempt < 5 && !landed; attempt++) {
    Puzzle p = fresh("test_rotation_solvers.xmpuzzle");
    {
      SolveSession s(p, p.getProblem(0), o);
      REQUIRE(s.start().has_value());
      while (s.state() == SolveState::Preparing || s.state() == SolveState::Assembling) {
        std::this_thread::yield();
      }
      s.pause();
      s.wait();
      landed = s.state() == SolveState::Paused;
    }
    if (!landed) {
      continue;
    }
    // BurrTools 2's search, saved as through: after the last piece
    const Problem& pr = p.getProblem(0);
    REQUIRE(pr.getPending().size() == 1);
    CHECK(pr.getAssemblerVersion() == AssemblerBt2::kVersion);
    CHECK(pr.getAssemblerState().starts_with(std::to_string(pr.getNumberOfPieces() + 1) + " "));
    Puzzle q = reloaded(p);
    SolveSession s(q, q.getProblem(0), o);
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(totals(q.getProblem(0)) == expected);
  }
  CHECK(landed);
}

TEST_CASE("solve session: take-apart workers, level threads and progress from another thread",
          "[solve][session][parallel]") {
  // For ThreadSanitizer: a solve with take-apart workers whose levels are
  // spread over the pool (every level of two positions or more), a split
  // assembly search with rotation checks on a symmetry-breaking puzzle
  // (Bermuda), progress and statistics read all the while, results equal
  // to one thread's.
  struct Case {
    const char* file;
    bool rotations;
    SolverType solver;
  };
  for (const Case c : {Case{"test_rotation_solvers.xmpuzzle", true, SolverType::Classic},
                       Case{"test_rotation_solvers.xmpuzzle", true, SolverType::Crowell},
                       Case{"Bermuda.xmpuzzle", false, SolverType::Classic},
                       Case{"Bermuda.xmpuzzle", false, SolverType::Bt2},
                       Case{"DemoPieceGenerator.xmpuzzle", false, SolverType::Classic}}) {
    INFO(c.file << ", " << solverTypeLabel(c.solver));
    SolveOptions o = takeApart(1);
    o.checkRotations = c.rotations;
    o.solver = c.solver;
    o.solutionLimit = 0;
    const Totals one = straight(c.file, o);

    o.threads = 4;
    o.takeApartWorkers = 2;
    o.takeApart.levelThreadCostUs = 0;
    Puzzle p = fresh(c.file);
    SolveSession s(p, p.getProblem(0), o);
    std::atomic<bool> done{false};
    std::thread watcher([&s, &done] {
      while (!done.load()) {
        const SolveProgress pg = s.progress();
        (void)pg.activity();
        (void)s.stats();
        (void)s.readProblem([](const Problem& pr) { return pr.getNumberOfSavedSolutions(); });
        std::this_thread::yield();
      }
    });
    REQUIRE(s.start().has_value());
    s.wait();
    done.store(true);
    watcher.join();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(totals(p.getProblem(0)) == one);
    const SolveStats st = s.stats();
    CHECK(st.disasmWorkers == 2);
    CHECK(st.assembliesFound == one.assemblies);
  }
}

TEST_CASE("solve session: a moved hotspot does not lose the solution", "[solve][session]") {
  // The original's solve thread reset the hotspots after the matrix: Cube
  // in Cage with one piece's hotspot elsewhere found 96 assemblies and no
  // solution through it (see test_disassembly.cpp). The session resets
  // them before the matrix, and on every run, so a hotspot moved while the
  // solve is paused does not part the take-apart from the matrix either.
  SECTION("before the solve") {
    Puzzle p = fresh("CubeInCage.xmpuzzle");
    p.getShape(2).setHotspot(1, 1, 0);
    SolveSession s(p, p.getProblem(0), takeApart());
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(p.getProblem(0).getNumAssemblies() == 96);
    CHECK(p.getProblem(0).getNumSolutions() == 1);
  }
  SECTION("while it is paused") {
    Puzzle p = fresh("CubeInCage.xmpuzzle");
    SolveSession s(p, p.getProblem(0), takeApart());
    REQUIRE(s.prepare().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    p.getShape(2).setHotspot(1, 1, 0);
    solveToEnd(s);
    CHECK(p.getProblem(0).getNumAssemblies() == 96);
    CHECK(p.getProblem(0).getNumSolutions() == 1);
  }
}

// ---- the stage 5 audit ----------------------------------------------------------------

TEST_CASE("solve session: assemblies counted for a take-apart keep the solve open",
          "[solve][session][resume]") {
  // Paused while the last assembly is taken apart, then continued without
  // take-apart: the search is through, but the assembly waits, so the run
  // ends Paused (not Solved with the solution missing). A run that takes
  // apart then finishes it, in memory or after a save.
  SolveOptions o = takeApart(2);
  o.checkRotations = true;
  const Totals expected = straight("test_rotation_solvers.xmpuzzle", o);
  REQUIRE(expected.solutions == 1);
  SolveOptions without = o;
  without.disassemble = false;

  for (const bool viaSave : {false, true}) {
    INFO((viaSave ? "saved and loaded" : "in memory"));
    bool landed = false;
    for (int attempt = 0; attempt < 5 && !landed; attempt++) {
      Puzzle p = fresh("test_rotation_solvers.xmpuzzle");
      SolveSession s(p, p.getProblem(0), o);
      REQUIRE(s.start().has_value());
      while (s.state() == SolveState::Preparing || s.state() == SolveState::Assembling) {
        std::this_thread::yield();
      }
      s.pause();
      s.wait();
      if (s.state() != SolveState::Paused) {
        continue;
      }
      landed = true;
      const Problem& pr = p.getProblem(0);
      REQUIRE(pr.getPending().size() == 1);
      REQUIRE(pr.getPending()[0].counted);

      REQUIRE(s.setOptions(without).has_value());
      REQUIRE(s.resume().has_value());
      s.wait();
      CHECK(s.state() == SolveState::Paused);
      CHECK(pr.getSolveState() == ProblemState::Solving);
      CHECK(pr.getPending().size() == 1);
      CHECK_FALSE(pr.getAssemblerState().empty());

      if (viaSave) {
        Puzzle q = reloaded(p);
        SolveSession t(q, q.getProblem(0), without);
        REQUIRE(t.resume().has_value());
        t.wait();
        CHECK(t.state() == SolveState::Paused);
        REQUIRE(t.setOptions(o).has_value());
        solveToEnd(t);
        CHECK(totals(q.getProblem(0)) == expected);
      } else {
        REQUIRE(s.setOptions(o).has_value());
        solveToEnd(s);
        CHECK(totals(pr) == expected);
      }
    }
    CHECK(landed);
  }
}

TEST_CASE("solve session: a save point while draining finishes the solve",
          "[solve][session][resume]") {
  // The search is through: only the take-aparts are left, and they finish.
  SolveOptions o = takeApart(2);
  o.checkRotations = true;
  const Totals expected = straight("test_rotation_solvers.xmpuzzle", o);
  bool landed = false;
  for (int attempt = 0; attempt < 5 && !landed; attempt++) {
    Puzzle p = fresh("test_rotation_solvers.xmpuzzle");
    SolveState end = SolveState::Idle;
    landed = stoppedWhileDraining(p, o, [&end](SolveSession& s) {
      s.pauseAtSavePoint();
      s.wait();
      end = s.state();
    });
    CHECK(end == SolveState::Finished);
    CHECK(totals(p.getProblem(0)) == expected);
  }
  CHECK(landed);
}

TEST_CASE("solve session: the mirror and rotation filter of each run applies",
          "[solve][session]") {
  // Demo Mirror Paradox: one assembly, two with the mirror kept. Prepared
  // with mirrors dropped, then continued with them kept (the original set
  // the filter on its kept assembler before every run).
  const auto assemblies = [](bool keep, bool prepareFirst) {
    Puzzle p = fresh("DemoMirrorParadox.xmpuzzle");
    SolveOptions o;
    o.solutionLimit = 0;
    SolveSession s(p, p.getProblem(0), o);
    if (prepareFirst) {
      REQUIRE(s.prepare().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
    }
    o.matrix.keepMirrors = keep;
    o.matrix.keepRotations = keep;
    REQUIRE(s.setOptions(o).has_value());
    solveToEnd(s);
    return p.getProblem(0).getNumAssemblies();
  };
  CHECK(assemblies(false, false) == 1);
  CHECK(assemblies(true, false) == 2);
  CHECK(assemblies(true, true) == 2);
}

TEST_CASE("solve session: a second solve's progress starts from nothing",
          "[solve][session][progress]") {
  // after a finished solve, the next one is not shown as almost done
  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveSession s(p, p.getProblem(0), takeApart());
  for (int i = 0; i < 3; i++) {
    REQUIRE(s.start().has_value());
    const SolveProgress first = s.progress();
    if (first.stage == SolveStage::Prepare || first.stage == SolveStage::Reduce) {
      CHECK(first.overall < 0.01F);
      CHECK(first.assemblyFraction < 0.01F);
    }
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
  }
  // and after an abort
  s.abort();
  REQUIRE(s.start().has_value());
  CHECK(s.progress().overall < 0.5F);
  s.wait();
}

TEST_CASE("solve session: sphere hotspots are reset to the sphere nearest the origin",
          "[solve][session]") {
  // Ball Room ships with its hotspots where initHotspot() puts them. With
  // every hotspot at the origin (as a shape just drawn has it), the
  // session resets them and finds the same assemblies, placed the same way.
  const auto found = [](bool atOrigin) {
    Puzzle p = fresh("BallRoom.xmpuzzle");
    if (atOrigin) {
      for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
        p.getShape(i).setHotspot(0, 0, 0);
      }
    }
    SolveOptions o;
    o.solutionLimit = 0;
    o.threads = 1;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
      CHECK(p.getShape(i).hasInitialHotspot());
    }
    return totals(p.getProblem(0));
  };
  const Totals shipped = found(false);
  CHECK(shipped.assemblies == 2);
  CHECK(found(true) == shipped);
}

TEST_CASE("solve session: equal solutions are kept in assembly order",
          "[solve][session][parallel]") {
  // Demo Piece Generator: nearly all of its 927 solutions are ties, and with
  // many workers some take-aparts finish out of order. One assembly thread
  // keeps the numbers in the order found; sorted by moves or by level,
  // equals are in assembly order.
  for (const SolutionOrder order : {SolutionOrder::Moves, SolutionOrder::Level}) {
    for (const unsigned int workers : {1U, 8U, 16U}) {
      INFO(workers << " workers, order " << static_cast<int>(order));
      Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
      SolveOptions o = takeApart(1);
      o.takeApartWorkers = workers;
      o.order = order;
      o.solutionLimit = 0;
      SolveSession s(p, p.getProblem(0), o);
      REQUIRE(s.start().has_value());
      s.wait();
      const Problem& pr = p.getProblem(0);
      REQUIRE(pr.getNumberOfSavedSolutions() == 927);
      // equal keys (moves; or the levels, as text) by assembly number
      const auto key = [order](const Solution& sol) {
        const Disassembly& d = *sol.getDisassemblyInfo();
        return order == SolutionOrder::Moves ? std::to_string(d.sumMoves()) : d.movesText();
      };
      unsigned int ties = 0;
      unsigned int outOfOrder = 0;
      for (unsigned int i = 1; i < pr.getNumberOfSavedSolutions(); i++) {
        const Solution& a = pr.getSavedSolution(i - 1);
        const Solution& b = pr.getSavedSolution(i);
        if (key(a) == key(b)) {
          ties++;
          outOfOrder += a.getAssemblyNumber() > b.getAssemblyNumber() ? 1 : 0;
        }
      }
      CHECK(ties > 900);
      CHECK(outOfOrder == 0);
    }
  }
}

TEST_CASE("solve session: the saved position belongs to the paused state",
          "[solve][session][resume]") {
  // Prepared: Paused with the position. While the next run assembles, the
  // problem holds none (it would be behind the search); a puzzle saved
  // then carries no position.
  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveSession s(p, p.getProblem(0), takeApart());
  REQUIRE(s.prepare().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK_FALSE(p.getProblem(0).getAssemblerState().empty());
  REQUIRE(s.resume().has_value());
  bool seen = false;
  while (isActive(s.state())) {
    seen = s.readProblem([](const Problem& pr) {
             if (pr.getSolveState() == ProblemState::Solving && pr.getNumAssemblies() > 0) {
               CHECK(pr.getAssemblerState().empty());
               return true;
             }
             return false;
           }) ||
           seen;
  }
  s.wait();
  CHECK(seen);
  CHECK(s.state() == SolveState::Finished);
}

TEST_CASE("solve session: the search statistics are recorded", "[solve][session]") {
  // recordSearchStats of the original GUI: the steps searched and the
  // assemblies tried apart (every one of more than one piece)
  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveSession s(p, p.getProblem(0), takeApart());
  REQUIRE(s.start().has_value());
  s.wait();
  const Problem& pr = p.getProblem(0);
  CHECK(pr.getSearched() > 0);
  CHECK(pr.getTriedApart() == 928);
}

TEST_CASE("solve session: a live sort keeps the saved solutions sorted as they come",
          "[solve][session]") {
  Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
  SolveOptions o = takeApart();
  o.order = SolutionOrder::Unsorted;
  o.solutionLimit = 0;
  SolveSession s(p, p.getProblem(0), o);
  constexpr int kByPiecesUsed = 3;
  s.setLiveSort(kByPiecesUsed);
  REQUIRE(s.start().has_value());
  s.wait();
  const auto order = [](const Problem& pr) {
    std::vector<std::string> v;
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      v.push_back(test::fingerprint(pr.getSavedSolution(i).getAssembly()));
    }
    return v;
  };
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 927);
  Problem sorted = pr;
  sorted.sortSolutions(kByPiecesUsed);
  CHECK(order(pr) == order(sorted));
  // the premise: the order found is not that order
  Problem found = pr;
  found.sortSolutions(0);
  CHECK(order(found) != order(sorted));
}

TEST_CASE("solve session: kept assemblies thin the same however the solve was paused",
          "[solve][session][resume]") {
  // Ten assemblies of 928 kept, thinned evenly. A continued solve works
  // out how far the thinning had gone from the counts (in memory and after
  // a save), so it keeps the same ten as a straight one. One thread: the
  // assemblies are found in one order.
  SolveOptions o;
  o.threads = 1;
  const auto kept = [](const Problem& pr) {
    REQUIRE(pr.getSolveState() == ProblemState::Solved);
    std::vector<std::string> v;
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      v.push_back(std::to_string(pr.getSavedSolution(i).getAssemblyNumber()) + " " +
                  test::fingerprint(pr.getSavedSolution(i).getAssembly()));
    }
    return v;
  };
  std::vector<std::string> expected;
  std::chrono::microseconds whole{0};
  {
    Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
    SolveSession s(p, p.getProblem(0), o);
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(s.start().has_value());
    s.wait();
    whole = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0);
    expected = kept(p.getProblem(0));
    REQUIRE(expected.size() == 10);
  }
  std::mt19937 rng(20261008);
  int paused = 0;
  for (int trial = 0; trial < 8; trial++) {
    Puzzle p = fresh("DemoPieceGenerator.xmpuzzle");
    auto session = std::make_unique<SolveSession>(p, p.getProblem(0), o);
    REQUIRE(session->start().has_value());
    for (unsigned int run = 0;; run++) {
      REQUIRE(run < 200);
      // within the time a whole solve takes, widening as the runs go on
      untilPrepared(*session);
      const auto window = static_cast<int>(whole.count()) * (2 + static_cast<int>(run)) / 4;
      std::this_thread::sleep_for(
          std::chrono::microseconds(std::uniform_int_distribution<int>(0, window)(rng)));
      session->pause();
      session->wait();
      REQUIRE(session->state() != SolveState::Error);
      if (session->state() == SolveState::Finished) {
        break;
      }
      paused++;
      if (trial % 2 == 1) {
        session.reset();
        p = reloaded(p);
        session = std::make_unique<SolveSession>(p, p.getProblem(0), o);
      }
      REQUIRE(session->resume().has_value());
    }
    CHECK(kept(p.getProblem(0)) == expected);
  }
  CHECK(paused >= 4);
}
