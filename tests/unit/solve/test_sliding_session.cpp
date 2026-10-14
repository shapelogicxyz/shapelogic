// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding solves in the solve session (new cases beside the port of the
// original's test_sliding.cpp): threads, pauses and save points, abort, the
// notes, Find Solutions off and Just Count, and the playback's routes.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/app/playback.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace bt;
using namespace bt::sliding;

namespace {

constexpr int kFilled = Voxel::kFilled;

unsigned int addBlock(Puzzle& puz, unsigned int w, unsigned int h) {
  const unsigned int id = puz.addShape(w, h, 1);
  for (unsigned int y = 0; y < h; y++) {
    for (unsigned int x = 0; x < w; x++) {
      puz.getShape(id).setState(x, y, 0, kFilled);
    }
  }
  return id;
}

// A 4x4 tray: piece A starts at (0,0) and must reach (3,3); a 1x2 bar and a
// unit piece have no stamps, so every layout of them is a start: a few
// hundred starts, most of which slide to the goal, many to the same picture.
Puzzle manyStarts() {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 4, 4);
  const unsigned int a = addBlock(puz, 1, 1);
  const unsigned int b = addBlock(puz, 1, 2);
  const unsigned int c = addBlock(puz, 1, 1);
  Voxel& tray = puz.getShape(trayId);
  tray.setColor(0, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(3, 3, 0), a + 1);
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  for (const unsigned int s : {b, c}) {
    pr.setShapeMaximum(puz, s, 1);
    pr.setShapeMinimum(puz, s, 1);
  }
  return puz;
}

// The benchmark's tray of the ported tests, smaller: a 3x3 tray with eight
// unit pieces and the last two swapped in the goal, which no sliding can
// reach. One start; its search visits all 181,440 arrangements (the fast
// search's 250,000 suffice) or, on a 4x4 tray with fifteen, stops at a
// limit.
Puzzle swapped(unsigned int side) {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, side, side);
  const unsigned int n = side * side - 1;
  for (unsigned int k = 0; k < n; k++) {
    const unsigned int id = addBlock(puz, 1, 1);
    Voxel& tray = puz.getShape(trayId);
    tray.setColor(k % side, k / side, 0, id + 1);
    const unsigned int g = k == n - 2 ? n - 1 : k == n - 1 ? n - 2 : k;
    tray.setGoalPiece(tray.getIndex(g % side, g / side, 0), id + 1);
  }
  syncSlidingProblems(puz);
  return puz;
}

SolveOptions findSolutions(SlidingSolver solver, unsigned int threads) {
  SolveOptions o;
  o.disassemble = true;
  o.sliding.solver = solver;
  o.threads = threads;
  o.solutionLimit = 0;
  return o;
}

// What a finished sliding solve found: the counts, each saved solution's
// finished picture with its moves, and (one assembly thread) its start.
struct SlideTotals {
  std::uint64_t assemblies = 0;
  std::uint64_t solutions = 0;
  std::multiset<std::string> endings;
  std::multiset<std::string> starts;
  bool operator==(const SlideTotals&) const = default;
};

SlideTotals slideTotals(const Problem& pr, bool withStarts) {
  REQUIRE(pr.getSolveState() == ProblemState::Solved);
  CHECK(pr.getPending().empty());
  SlideTotals t;
  t.assemblies = pr.getNumAssemblies();
  t.solutions = pr.getNumSolutions();
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    const Solution& s = pr.getSavedSolution(i);
    REQUIRE(s.getDisassembly() != nullptr);
    t.endings.insert(finalPlacementKey(*s.getDisassembly()) + " " +
                     std::to_string(s.getDisassembly()->getMoves()));
    if (withStarts) {
      t.starts.insert(test::fingerprint(s.getAssembly()));
    }
  }
  return t;
}

SlideTotals straightSolve(Puzzle p, const SolveOptions& o, bool withStarts) {
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  return slideTotals(p.getProblem(0), withStarts);
}

// Waits until the run searches a start (the sliding stage, an arrangement
// visited).
void untilSearching(const SolveSession& s) {
  for (;;) {
    const SolveProgress p = s.progress();
    if (p.stage == SolveStage::Other && p.searched > 0) {
      return;
    }
    REQUIRE(isActive(s.state()));
    std::this_thread::yield();
  }
}

}  // namespace

TEST_CASE("sliding session: four threads find what one thread finds",
          "[sliding][session][parallel]") {
  for (const SlidingSolver solver : {SlidingSolver::Fast, SlidingSolver::Full}) {
    INFO("solver " << static_cast<int>(solver));
    const SlideTotals one = straightSolve(manyStarts(), findSolutions(solver, 1), true);
    CHECK(one.assemblies > 100);
    CHECK(one.solutions > 10);
    CHECK(one.solutions == one.endings.size());
    // which of two starts with as many moves to one picture is kept depends
    // on the order the assembly threads hand them over: endings only
    SlideTotals four = straightSolve(manyStarts(), findSolutions(solver, 4), false);
    SlideTotals oneEndings = one;
    oneEndings.starts.clear();
    CHECK(four == oneEndings);
  }

  // a full search spreading its levels over the session's threads, with the
  // progress read meanwhile
  Puzzle p = test::loadFixture("sliding/test_sliding_nested.xmpuzzle");
  p.getProblem(0).removeAllSolutions();
  SolveOptions o = findSolutions(SlidingSolver::Full, 4);
  o.sliding.nested = true;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  while (isActive(s.state())) {
    (void)s.progress();
    (void)s.stats();
    std::this_thread::yield();
  }
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  CHECK(pr.getSavedSolution(0).getDisassembly()->getMoves() == 47);
  CHECK(s.note().empty());
}

TEST_CASE("sliding session: pausing and continuing finds every solution once",
          "[sliding][session][resume][parallel]") {
  std::mt19937 rng(20261010);
  for (const unsigned int threads : {1U, 4U}) {
    INFO(threads << " thread(s)");
    const SolveOptions o = findSolutions(SlidingSolver::Full, threads);
    const SlideTotals expected = straightSolve(manyStarts(), o, threads == 1);

    Puzzle p = manyStarts();
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    unsigned int paused = 0;
    for (unsigned int run = 0;; run++) {
      REQUIRE(run < 400);
      const auto delay = std::chrono::microseconds(
          std::uniform_int_distribution<int>(0, 2000 + 200 * static_cast<int>(run))(rng));
      std::this_thread::sleep_for(delay);
      if (std::uniform_int_distribution<int>(0, 3)(rng) == 0) {
        s.pauseAtSavePoint();
      } else {
        s.pause();
      }
      s.wait();
      REQUIRE(s.state() != SolveState::Error);
      if (s.state() == SolveState::Finished) {
        break;
      }
      paused++;
      REQUIRE(s.resume().has_value());
    }
    CHECK(paused > 0);
    CHECK(slideTotals(p.getProblem(0), threads == 1) == expected);
  }
}

TEST_CASE("sliding session: a save point lets the search under way finish, a pause does not",
          "[sliding][session]") {
  // A 4x4 tray of fifteen with a swap no sliding makes: the fast search
  // stops at its limit of 250,000 arrangements.
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), findSolutions(SlidingSolver::Fast, 2));
    REQUIRE(s.start().has_value());
    untilSearching(s);
    s.pauseAtSavePoint();
    s.wait();
    // the one start was searched to its limit and counted; the assembler
    // stopped after it (its position saved), so the solve is paused
    REQUIRE(s.state() == SolveState::Paused);
    const Problem& pr = p.getProblem(0);
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 0);
    CHECK(pr.getPending().empty());
    CHECK(s.note() ==
          "1 start layout was not searched completely: the search reached its limit of "
          "250,000 arrangements. There may be solutions the solver did not find. Try the "
          "Sliding Deep or Full Solver.");
    // carried on, the assembler finds no other start
    REQUIRE(s.resume().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
    CHECK(pr.getNumAssemblies() == 1);
  }
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), findSolutions(SlidingSolver::Fast, 2));
    REQUIRE(s.start().has_value());
    untilSearching(s);
    s.pause();
    s.wait();
    // stopped in the middle of the start's search: kept to be searched
    // again, not counted, no note
    REQUIRE(s.state() == SolveState::Paused);
    const Problem& pr = p.getProblem(0);
    CHECK(pr.getNumAssemblies() == 0);
    REQUIRE(pr.getPending().size() == 1);
    CHECK_FALSE(pr.getPending()[0].counted);
    CHECK(s.note().empty());
  }
}

TEST_CASE("sliding session: a search that searched everything says so", "[sliding][session]") {
  // the 3x3 tray of eight: every arrangement searched, no path
  Puzzle p = swapped(3);
  SolveSession s(p, p.getProblem(0), findSolutions(SlidingSolver::Fast, 1));
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  CHECK(pr.getNumAssemblies() == 1);
  CHECK(pr.getNumSolutions() == 0);
  CHECK(s.note() ==
        "Every reachable arrangement was searched: this puzzle has no solution with the "
        "current settings. If a piece can carry pieces that sit inside it, try Allow "
        "Nested Slides.");
  // the arrangements searched are recorded with the problem
  CHECK(pr.getSearched() == 181440);

  // with nested slides the hint is the partial nesting
  SolveOptions nested = findSolutions(SlidingSolver::Fast, 1);
  nested.sliding.nested = true;
  REQUIRE(s.setOptions(nested).has_value());
  REQUIRE(s.start().has_value());
  s.wait();
  CHECK(s.note() ==
        "Every reachable arrangement was searched: this puzzle has no solution with the "
        "current settings. If a piece can carry pieces that stick out of it, try Allow "
        "Partially Nested.");
}

TEST_CASE("sliding session: abort throws the search away", "[sliding][session]") {
  Puzzle p = swapped(4);
  SolveSession s(p, p.getProblem(0), findSolutions(SlidingSolver::Full, 2));
  REQUIRE(s.start().has_value());
  untilSearching(s);
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  const Problem& pr = p.getProblem(0);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(pr.getPending().empty());
  CHECK(pr.getNumberOfSavedSolutions() == 0);
}

TEST_CASE("sliding session: without Find Solutions the starts are kept, with Just Count counted",
          "[sliding][session]") {
  const SlideTotals solved = straightSolve(manyStarts(), findSolutions(SlidingSolver::Fast, 1),
                                           true);
  {
    Puzzle p = manyStarts();
    SolveOptions o;
    o.solutionLimit = 0;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    const Problem& pr = p.getProblem(0);
    CHECK(pr.getNumAssemblies() == solved.assemblies);
    CHECK(pr.getNumSolutions() == 0);
    CHECK(pr.getNumberOfSavedSolutions() == solved.assemblies);
    CHECK(pr.getSavedSolution(0).getDisassembly() == nullptr);
    CHECK(s.note().empty());
  }
  {
    Puzzle p = manyStarts();
    SolveOptions o = findSolutions(SlidingSolver::Fast, 1);
    o.justCount = true;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    const Problem& pr = p.getProblem(0);
    // the solve thread's sliding branch counts the starts and nothing else
    CHECK(pr.getNumAssemblies() == solved.assemblies);
    CHECK(pr.getNumSolutions() == 0);
    CHECK(pr.getNumberOfSavedSolutions() == 0);
  }
}

TEST_CASE("sliding session: the solve's limit thins the kept solutions", "[sliding][session]") {
  Puzzle p = manyStarts();
  SolveOptions o = findSolutions(SlidingSolver::Fast, 1);
  o.solutionLimit = 4;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  CHECK(pr.getNumSolutions() > 4);
  CHECK(pr.getNumberOfSavedSolutions() <= 4);
}

TEST_CASE("sliding session: the start locks and the holes are set up as a solve starts",
          "[sliding][session]") {
  // a tray whose pieces lost their colours (an older file, an edit): the
  // solve paints them again, and the holes follow the floor
  Puzzle p = test::loadFixture("test_sliding_solver.xmpuzzle");
  Problem& pr = p.getProblem(0);
  pr.removeAllSolutions();
  for (unsigned int s = 1; s < p.getNumberOfShapes(); s++) {
    Voxel& v = p.getShape(s);
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      if (v.getState(i) != Voxel::kEmpty) {
        v.setColor(i, 0);
      }
    }
  }
  pr.setMaxHoles(9);
  const Puzzle before = p;
  REQUIRE(prepareForSolving(p, pr));
  CHECK_FALSE(prepareForSolving(p, pr));
  CHECK(pr.getMaxHoles() == 2);
  CHECK(pieceColor(p, 1) == 2);

  Puzzle q = before;
  SolveOptions o = findSolutions(SlidingSolver::Fast, 1);
  SolveSession s(q, q.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(q.getProblem(0).getNumSolutions() == 1);
  CHECK(q.getProblem(0).getMaxHoles() == 2);
}

TEST_CASE("sliding playback: a move round a corner follows its route", "[sliding][app]") {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 2, 2);
  const unsigned int a = addBlock(puz, 1, 1);
  Voxel& tray = puz.getShape(trayId);
  tray.setColor(0, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(1, 1, 0), a + 1);
  syncSlidingProblems(puz);

  SolveSession s(puz, puz.getProblem(0), findSolutions(SlidingSolver::Full, 1));
  REQUIRE(s.start().has_value());
  s.wait();
  const Problem& pr = puz.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation& path = *pr.getSavedSolution(0).getDisassembly();
  REQUIRE(path.getMoves() == 1);
  std::vector<unsigned int> movers;
  const std::vector<std::pair<int, int>> route = slideRoute(puz, pr, path, 0, &movers);
  REQUIRE(route.size() == 3);

  Result<app::PlaybackSampler> pb = app::PlaybackSampler::make(puz, 0, 0);
  REQUIRE(pb.has_value());
  CHECK(pb->stepCount() == 1);
  const Result<std::vector<app::PiecePose>> p0 = pb->sample(0);
  const Result<std::vector<app::PiecePose>> half = pb->sample(0.5F);
  const Result<std::vector<app::PiecePose>> p1 = pb->sample(1);
  REQUIRE((p0 && half && p1));
  // half way along the L the piece stands at the corner, a whole cell along
  // one axis (a cell is one unit on this grid), not half way across
  const float dx = (*half)[0].translation[0] - (*p0)[0].translation[0];
  const float dy = (*half)[0].translation[1] - (*p0)[0].translation[1];
  CHECK(dx == static_cast<float>(route[1].first));
  CHECK(dy == static_cast<float>(route[1].second));
  CHECK((*p1)[0].translation[0] - (*p0)[0].translation[0] == 1.0F);
  CHECK((*p1)[0].translation[1] - (*p0)[0].translation[1] == 1.0F);
}
