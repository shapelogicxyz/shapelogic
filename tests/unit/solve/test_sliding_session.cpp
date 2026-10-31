// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding solves in the solve session (new cases beside the port of the
// original's test_sliding.cpp): threads, pauses and save points, abort, the
// notes, Find Solutions off and Just Count, the playback's routes, and the
// orientations the pieces are placed in.
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

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <utility>
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
// reach. One start; its search visits all 181,440 arrangements or, on a
// 4x4 tray with fifteen, stops at its memory limit.
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

SolveOptions findSolutions(unsigned int threads) {
  SolveOptions o;
  o.disassemble = true;
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
  const SlideTotals one = straightSolve(manyStarts(), findSolutions(1), true);
  CHECK(one.assemblies > 100);
  CHECK(one.solutions > 10);
  CHECK(one.solutions == one.endings.size());
  // which of two starts with as many moves to one picture is kept depends
  // on the order the assembly threads hand them over: endings only
  SlideTotals four = straightSolve(manyStarts(), findSolutions(4), false);
  SlideTotals oneEndings = one;
  oneEndings.starts.clear();
  CHECK(four == oneEndings);

  // a search spreading its levels over the session's threads, with the
  // progress read meanwhile
  Puzzle p = test::loadFixture("sliding/test_sliding_nested.xmpuzzle");
  p.getProblem(0).removeAllSolutions();
  SolveOptions o = findSolutions(4);
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
    const SolveOptions o = findSolutions(threads);
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

// The engine sets the memory and names where its user sets it: the note
// says that place instead of Enable High Memory.
TEST_CASE("sliding session: a search out of memory names the program's settings",
          "[sliding][session]") {
  Puzzle p = swapped(3);
  SolveOptions o = findSolutions(1);
  o.sliding.memoryBytes = 1;
  o.settingsPlace = "Settings ▸ Solver";
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  CHECK(s.note() ==
        "1 start layout was not searched completely: the search reached its memory limit of 1 "
        "arrangements. There may be solutions the solver did not find. Settings ▸ Solver can "
        "give it more Search memory.");

  // without a place, the original's words
  Puzzle q = swapped(3);
  o.settingsPlace.clear();
  SolveSession t(q, q.getProblem(0), o);
  REQUIRE(t.start().has_value());
  t.wait();
  CHECK(t.note().ends_with(" Try Enable High Memory."));
}

TEST_CASE("sliding session: a save point lets the search under way finish, a pause does not",
          "[sliding][session]") {
  // A 4x4 tray of fifteen with a swap no sliding makes: the search stops at
  // its memory limit, kept small here.
  SolveOptions o = findSolutions(2);
  o.sliding.memoryBytes = 8000000;
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), o);
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
    CHECK(s.note().starts_with(
        "1 start layout was not searched completely: the search reached its memory limit of "));
    CHECK(s.note().ends_with(
        " arrangements. There may be solutions the solver did not find. Try Enable High "
        "Memory."));
    // carried on, the assembler finds no other start
    REQUIRE(s.resume().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
    CHECK(pr.getNumAssemblies() == 1);
  }
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), o);
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
  SolveSession s(p, p.getProblem(0), findSolutions(1));
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
  SolveOptions nested = findSolutions(1);
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
  SolveSession s(p, p.getProblem(0), findSolutions(2));
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
  const SlideTotals solved = straightSolve(manyStarts(), findSolutions(1), true);
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
    SolveOptions o = findSolutions(1);
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
  SolveOptions o = findSolutions(1);
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
  SolveOptions o = findSolutions(1);
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

  SolveSession s(puz, puz.getProblem(0), findSolutions(1));
  REQUIRE(s.start().has_value());
  s.wait();
  const Problem& pr = puz.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation& path = *pr.getSavedSolution(0).getDisassembly();
  REQUIRE(path.getMoves() == 1);
  std::vector<unsigned int> movers;
  const std::vector<SlideShift> route = slideRoute(puz, pr, path, 0, &movers);
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
  CHECK(dx == static_cast<float>(route[1][0]));
  CHECK(dy == static_cast<float>(route[1][1]));
  CHECK((*p1)[0].translation[0] - (*p0)[0].translation[0] == 1.0F);
  CHECK((*p1)[0].translation[1] - (*p0)[0].translation[1] == 1.0F);
}

namespace {

// A 4x1 tray: A (a unit piece) starts and ends at (0,0); B is a unit piece
// the problem has two copies of, with the starts and goals given.
Puzzle twoCopies(const std::vector<int>& starts, const std::vector<int>& goals) {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 4, 1);
  const unsigned int a = addBlock(puz, 1, 1);
  const unsigned int b = addBlock(puz, 1, 1);
  Voxel& tray = puz.getShape(trayId);
  tray.setColor(0, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), a + 1);
  for (const int x : starts) {
    tray.setColor(static_cast<unsigned int>(x), 0, 0, b + 1);
  }
  for (const int x : goals) {
    tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(x), 0, 0), b + 1);
  }
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, b, 2);
  pr.setShapeMinimum(puz, b, 2);
  syncSlidingProblems(puz);
  return puz;
}

}  // namespace

TEST_CASE("sliding session: copies of a piece share their starts and goals",
          "[sliding][session]") {
  // both copies placed: one start (the copies are alike), any copy on any goal
  {
    Puzzle p = twoCopies({1, 2}, {2, 3});
    CHECK(startGoalError(p, 0).empty());
    CHECK(p.getProblem(0).getNeutralFitColors() == 0);
    const SlideTotals t = straightSolve(p, findSolutions(1), false);
    CHECK(t.assemblies == 1);
    CHECK(t.solutions == 1);
  }
  // a start for one copy: the other starts on any free cell (2 or 3), never
  // with the start left empty
  {
    Puzzle p = twoCopies({1}, {2, 3});
    CHECK(startGoalError(p, 0).empty());
    CHECK(p.getProblem(0).getNeutralFitColors() == std::uint64_t{1} << 2);
    const SlideTotals t = straightSolve(p, findSolutions(1), false);
    CHECK(t.assemblies == 2);
    CHECK(t.solutions == 1);
  }
  // a goal for one copy: one copy on it is the goal, the other ends anywhere
  {
    Puzzle p = twoCopies({1, 2}, {3});
    const SlideTotals t = straightSolve(p, findSolutions(1), false);
    CHECK(t.assemblies == 1);
    CHECK(t.solutions == 1);
    REQUIRE(t.endings.size() == 1);
    // one move: the copy at 2 to 3
    CHECK(t.endings.begin()->ends_with(" 1"));
  }
}

namespace {

// Two copies of a vertical domino whose starts and goals are one 2x2 block
// of a 3x2 tray: two horizontal dominoes would cover the block as well.
Puzzle dominoBlock() {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 3, 2);
  const unsigned int d = addBlock(puz, 1, 2);
  Voxel& tray = puz.getShape(trayId);
  for (unsigned int y = 0; y < 2; y++) {
    for (unsigned int x = 0; x < 2; x++) {
      tray.setColor(x, y, 0, d + 1);
      tray.setGoalPiece(tray.getIndex(x, y, 0), d + 1);
    }
  }
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, d, 2);
  pr.setShapeMinimum(puz, d, 2);
  syncSlidingProblems(puz);
  return puz;
}

// A 4x4 tray: a unit piece with a start and a goal, and an L of four cells
// (chiral: its mirror image is no turn of it) with neither.
Puzzle freeL() {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 4, 4);
  const unsigned int a = addBlock(puz, 1, 1);
  const unsigned int l = puz.addShape(2, 3, 1);
  for (const auto& [x, y] : {std::pair{0U, 0U}, {0U, 1U}, {0U, 2U}, {1U, 2U}}) {
    puz.getShape(l).setState(x, y, 0, kFilled);
  }
  Voxel& tray = puz.getShape(trayId);
  tray.setColor(0, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(3, 3, 0), a + 1);
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, l, 1);
  pr.setShapeMinimum(puz, l, 1);
  return puz;
}

// The starts a solve without Find Solutions keeps.
std::vector<Assembly> keptStarts(Puzzle& p, bool anyOrientation, bool mirrored) {
  SolveOptions o;
  o.solutionLimit = 0;
  o.sliding.anyOrientation = anyOrientation;
  o.sliding.mirroredOrientations = mirrored;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  if (s.state() == SolveState::Error) {
    FAIL("solver error: " << s.failure()->message);
  }
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  std::vector<Assembly> out;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    out.push_back(pr.getSavedSolution(i).getAssembly());
  }
  CHECK(out.size() == pr.getNumAssemblies());
  return out;
}

}  // namespace

TEST_CASE("sliding session: a piece with a start or a goal is placed as drawn",
          "[sliding][session]") {
  for (const bool any : {false, true}) {
    Puzzle p = dominoBlock();
    const std::vector<Assembly> starts = keptStarts(p, any, any);
    REQUIRE(starts.size() == 1);
    for (unsigned int pc = 0; pc < 2; pc++) {
      CHECK(starts[0].getTransformation(pc) == 0);
    }
  }
}

TEST_CASE("sliding session: a piece with neither turns only when the options allow it",
          "[sliding][session]") {
  // the L is piece 1, after the unit piece
  auto turns = [](const std::vector<Assembly>& starts) {
    std::set<unsigned int> t;
    for (const Assembly& a : starts) {
      t.insert(a.getTransformation(1));
    }
    return t;
  };
  Puzzle drawn = freeL();
  Puzzle any = freeL();
  Puzzle mirrored = freeL();
  const std::vector<Assembly> d = keptStarts(drawn, false, false);
  const std::vector<Assembly> a = keptStarts(any, true, false);
  const std::vector<Assembly> m = keptStarts(mirrored, true, true);

  CHECK(turns(d) == std::set<unsigned int>{0});
  // four quarter turns, none of them mirrored
  const std::set<unsigned int> ta = turns(a);
  CHECK(ta.size() == 4);
  CHECK(*ta.rbegin() < 24);
  // and their four mirror images
  const std::set<unsigned int> tm = turns(m);
  CHECK(tm.size() == 8);
  CHECK(std::count_if(tm.begin(), tm.end(), [](unsigned int t) { return t >= 24; }) == 4);
  CHECK(d.size() < a.size());
  CHECK(a.size() < m.size());

  // a mirrored start slides to the goal like any other
  SolveOptions o = findSolutions(1);
  o.sliding.anyOrientation = true;
  o.sliding.mirroredOrientations = true;
  const SlideTotals t = straightSolve(freeL(), o, false);
  CHECK(t.assemblies == m.size());
  CHECK(t.solutions >= 1);
}

TEST_CASE("sliding: the orientations each part may take", "[sliding]") {
  const Puzzle p = freeL();
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getNumberOfParts() == 2);
  CHECK(partTransforms(p, pr, false, true) == std::vector<std::uint64_t>{1, 1});
  const std::vector<std::uint64_t> any = partTransforms(p, pr, true, false);
  CHECK(any[0] == 1);
  CHECK(std::popcount(any[1]) == 4);
  CHECK((any[1] & 1U) == 1);
  const std::vector<std::uint64_t> mirrored = partTransforms(p, pr, true, true);
  CHECK(mirrored[0] == 1);
  CHECK(std::popcount(mirrored[1]) == 8);
  CHECK((mirrored[1] & any[1]) == any[1]);
}

// With a work folder, a start's search that a pause cuts short is saved
// there and carried on by the next run, and nothing is left behind once
// the solve is through.
TEST_CASE("sliding session: a paused search is carried on from its folder",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_resume";
  std::filesystem::remove_all(dir);
  // the 4x4 tray of fifteen with the swap: one start, searched long enough
  // to be paused in the middle, and never to its end here
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 8000000;
  Puzzle p = swapped(4);
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  untilSearching(s);
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getPending().size() == 1);
  // the saved search, in its own folder
  REQUIRE(std::filesystem::is_directory(dir));
  CHECK_FALSE(std::filesystem::is_empty(dir));
  // carried on: the sets live in the folder now and the memory limit no
  // longer stops the search, so it is paused once more rather than run to
  // its end
  REQUIRE(s.resume().has_value());
  untilSearching(s);
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK_FALSE(std::filesystem::is_empty(dir));
  // a solve from the start throws the saved search away
  s.abort();
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}
