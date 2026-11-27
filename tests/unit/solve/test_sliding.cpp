// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles: the tray model, the slide search, routes and the sliding
// solve. Ported from the original's test/test_sliding.cpp, every case in its
// order (the solve thread's cases run a SolveSession; its environment
// switches are the search's options; its scratch folders are this
// process's own), followed by new cases in test_sliding_session.cpp. The
// saved searches' cases are in test_sliding_saved.cpp, the key sets' in
// test_slide_levels.cpp, the stamps' in model/test_slide_marks.cpp
// (cleanup item 21.2); their helpers in sliding_test_helpers.h.
#include "../model/model_test_helpers.h"
#include "../scratch_dir.h"
#include "slide_levels_test_helpers.h"
#include "sliding_test_helpers.h"
#include "solve_session_test_access.h"

#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/slide_levels.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The seam of the original's "a shorter path keeps the solution number",
// which called the solve thread's callback with starts of its choosing, is
// SolveSessionTestAccess (solve_session_test_access.h).

using namespace bt;
using namespace bt::sliding;
using namespace bt::test;

namespace {

Puzzle slidingPuzzle() { return Puzzle(GridType(GridKind::Sliding)); }

// The solve thread's run on problem 0 of `puzzle`: start, wait, and the
// session for its note.
std::unique_ptr<SolveSession> solveRun(Puzzle& puzzle, const SolveOptions& o) {
  auto s = std::make_unique<SolveSession>(puzzle, puzzle.getProblem(0), o);
  REQUIRE(s->start().has_value());
  s->wait();
  if (s->state() == SolveState::Error) {
    FAIL("solver error: " << s->failure()->message);
  }
  return s;
}

// The solve thread's parameters: PAR_DISASSM unless the flags say more.
SolveOptions findSolutions() {
  SolveOptions o;
  o.disassemble = true;
  o.reduce = false;
  return o;
}

}  // namespace

TEST_CASE("sliding: place start and find a one-cell path", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 3, 1);

  REQUIRE(puz.getNumberOfProblems() == 1);
  REQUIRE(puz.getProblem(0).resultValid());
  REQUIRE(puz.getProblem(0).goalValid());

  // one 1x1 piece
  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, pieceId, 1);

  REQUIRE(placeStart(puz, pr, pieceId, 0, 0));
  REQUIRE(hasStart(puz, pr, pieceId));
  REQUIRE(placeGoal(puz, pr, pieceId, 2, 0));
  REQUIRE(hasGoal(puz, pr, pieceId));

  syncMaxHoles(puz, pr);

  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 0, 0);  // the identity at (0,0,0)

  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  // two cells in one direction are a single move
  REQUIRE(path->getMoves() == 1);
  REQUIRE(path->getState(0).getX(0) == 0);
  REQUIRE(path->getState(1).getX(0) == 2);

  DisasmToMoves mv(&*path, 5, 1);
  mv.setStep(0, false, true);
  REQUIRE(mv.getX(0) == 0.0F);
  mv.setStep(0.5F, false, true);
  REQUIRE(mv.getX(0) == 1.0F);
  mv.setStep(1, false, true);
  REQUIRE(mv.getX(0) == 2.0F);

  // a shorter straight slide to the same cell is also one move
  Assembly closer(puz.getGridType());
  closer.addPlacement(0, 1, 0, 0);
  const std::optional<Separation> shorter = findSlidePath(puz, pr, closer);
  REQUIRE(shorter.has_value());
  REQUIRE(shorter->getMoves() == 1);
  REQUIRE(finalPlacementKey(*shorter) == finalPlacementKey(*path));
}

TEST_CASE("sliding: more restriction cells than the piece has voxels", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 3, 1);
  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  const unsigned int mark = pieceId + 1;
  puz.getShape(trayId).setStartPiece(0, 0, 0, mark);
  syncSlidingProblems(puz);
  Voxel& tray = puz.getShape(trayId);
  const Problem& pr = puz.getProblem(0);

  REQUIRE(stampOverflowMessage(puz, pr).empty());

  SlideMarks& marks = puz.getProblem(0).editSlideMarks();
  marks.set(tray.getIndex(1, 0, 0), false, mark);
  const std::string startMsg = stampOverflowMessage(puz, pr);
  REQUIRE(startMsg.find("S" + std::to_string(mark)) != std::string::npos);
  REQUIRE(startMsg.find("start") != std::string::npos);

  marks.set(tray.getIndex(1, 0, 0), false, 0);
  marks.set(tray.getIndex(0, 0, 0), true, mark);
  marks.set(tray.getIndex(1, 0, 0), true, mark);
  const std::string goalMsg = stampOverflowMessage(puz, pr);
  REQUIRE(goalMsg.find("goal") != std::string::npos);
  REQUIRE(goalMsg.find("start") == std::string::npos);
}

TEST_CASE("sliding: a variable cell is not a legal place to finish", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 3, 1);
  puz.getShape(trayId).setState(1, 0, 0, kVariable);

  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  Voxel& tray = puz.getShape(trayId);
  tray.setStartPiece(0, 0, 0, pieceId + 1);
  // the goal is on the far normal cell; the piece crosses the variable one
  tray.setGoalPiece(tray.getIndex(2, 0, 0), pieceId + 1);
  syncSlidingProblems(puz);

  const Problem& pr = puz.getProblem(0);
  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 0, 0);

  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 1);

  // a goal stamp on a variable cell is still not a place to stop
  const Voxel& tray2 = puz.getShape(trayId);
  SlideMarks& marks = puz.getProblem(0).editSlideMarks();
  marks.set(tray2.getIndex(2, 0, 0), true, 0);
  marks.set(tray2.getIndex(1, 0, 0), true, pieceId + 1);
  const std::optional<Separation> blocked = findSlidePath(puz, puz.getProblem(0), start);
  REQUIRE_FALSE(blocked.has_value());
}

TEST_CASE("sliding: a turn is one fluid move", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 2, 2);

  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, pieceId, 1);
  REQUIRE(placeStart(puz, pr, pieceId, 0, 0));
  REQUIRE(placeGoal(puz, pr, pieceId, 1, 1));

  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 0, 0);
  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  // round the corner without stopping: one move
  REQUIRE(path->getMoves() == 1);
  REQUIRE(path->getState(1).getX(0) == 1);
  REQUIRE(path->getState(1).getY(0) == 1);

  // the route has one corner, and the animation follows it rather than
  // cutting across the diagonal
  std::vector<unsigned int> movers;
  const std::vector<SlideShift> route = slideRoute(puz, pr, *path, 0, &movers);
  REQUIRE(movers == std::vector<unsigned int>{0});
  REQUIRE(route.size() == 3);
  CHECK(route.front() == SlideShift{0, 0, 0});
  CHECK(route.back() == SlideShift{1, 1, 0});

  DisasmToMoves mv(&*path, 5, 1);
  applySlideRoutes(puz, pr, *path, mv);
  mv.setStep(0.5F, false, true);
  // halfway along a two-cell L the piece is at the corner, not (0.5, 0.5)
  CHECK(mv.getX(0) == static_cast<float>(route[1][0]));
  CHECK(mv.getY(0) == static_cast<float>(route[1][1]));
  mv.setStep(1, false, true);
  CHECK(mv.getX(0) == 1.0F);
  CHECK(mv.getY(0) == 1.0F);
}

TEST_CASE("sliding: a piece following another is not a caterpillar", "[sliding]") {
  // A 6x1 corridor: b at 1 slides to 5, then a, behind it at 0, slides to 3.
  // Counting unit steps, every order in which a stays behind b is equally
  // short, so the search could alternate them a cell at a time (7 moves).
  // Counting whole moves of one piece, each goes the whole way once.
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 6, 1);

  const unsigned int a = puz.addShape(1, 1, 1);
  puz.getShape(a).setState(0, 0, 0, kFilled);
  const unsigned int b = puz.addShape(1, 1, 1);
  puz.getShape(b).setState(0, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, a, 1);
  pr.setShapeMaximum(puz, b, 1);
  REQUIRE(placeStart(puz, pr, a, 0, 0));
  REQUIRE(placeStart(puz, pr, b, 1, 0));
  REQUIRE(placeGoal(puz, pr, a, 3, 0));
  REQUIRE(placeGoal(puz, pr, b, 5, 0));

  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 0, 0);
  start.addPlacement(0, 1, 0, 0);
  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 2);
  // b goes the full four cells, then a the full three
  CHECK(path->getState(1).getX(1) == 5);
  CHECK(path->getState(1).getX(0) == 0);
  CHECK(path->getState(2).getX(0) == 3);
}

TEST_CASE("sliding: a piece nested in a pocket rides along only with nested slides",
          "[sliding]") {
  // A 2x4 tray. The outer piece is a C, open to the tray's left edge:
  //   ##
  //   .#
  //   ##
  // The inner 1x1 sits in its pocket. Both must rise one row. Alone,
  // neither can move: the outer piece's bottom arm hits the inner piece,
  // and the inner piece hits the top arm. Together they rise in one move.
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 2, 4);

  const unsigned int outer = puz.addShape(2, 3, 1);
  for (const std::pair<unsigned int, unsigned int> c :
       std::vector<std::pair<unsigned int, unsigned int>>{{0, 0}, {1, 0}, {1, 1}, {0, 2}, {1, 2}}) {
    puz.getShape(outer).setState(c.first, c.second, 0, kFilled);
  }
  const unsigned int inner = puz.addShape(1, 1, 1);
  puz.getShape(inner).setState(0, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, outer, 1);
  pr.setShapeMaximum(puz, inner, 1);
  // goals first: a goal stamp may not cover a cell already stamped as a start
  REQUIRE(placeGoal(puz, pr, outer, 0, 0));
  REQUIRE(placeGoal(puz, pr, inner, 0, 1));
  REQUIRE(placeStart(puz, pr, outer, 0, 1));
  REQUIRE(placeStart(puz, pr, inner, 0, 2));

  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 1, 0);
  start.addPlacement(0, 0, 2, 0);

  REQUIRE_FALSE(findSlidePath(puz, pr, start).has_value());

  const std::optional<Separation> path = findSlidePath(puz, pr, start, true);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 1);
  CHECK(path->getState(1).getY(0) == 0);
  CHECK(path->getState(1).getY(1) == 1);

  std::vector<unsigned int> movers;
  const std::vector<SlideShift> route = slideRoute(puz, pr, *path, 0, &movers);
  CHECK(movers == std::vector<unsigned int>{0, 1});
  CHECK(route.size() == 2);
}

TEST_CASE("sliding: the start stamp is the only initial placement", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 3, 1);
  for (unsigned int x = 0; x < 3; x++) {
    puz.getShape(trayId).setState(x, 0, 0, kVariable);
  }

  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  Voxel& tray = puz.getShape(trayId);
  tray.setStartPiece(0, 0, 0, pieceId + 1);
  tray.setGoalPiece(tray.getIndex(2, 0, 0), pieceId + 1);
  syncSlidingProblems(puz);

  const Problem& pr = puz.getProblem(0);
  const std::unique_ptr<Assembler> assm = makeAssembler(puz, pr, SolverType::Classic);
  MatrixOptions mo;
  mo.startLocks = true;
  mo.freeStarts = freeStartParts(puz, pr);
  REQUIRE(assm->createMatrix(mo, StopToken()) == AssemblerError::None);

  unsigned int n = 0;
  int x = -1;
  int y = -1;
  (void)assm->assemble(
      [&](Assembly a) {
        n++;
        x = a.getX(0);
        y = a.getY(0);
        return true;
      },
      SearchOptions{}, StopToken{});
  REQUIRE(n == 1);
  REQUIRE(x == 0);
  REQUIRE(y == 0);
}

TEST_CASE("sliding: normal cells may stay empty", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  (void)addStartGoalShape(puz, 3, 1);

  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMinimum(puz, pieceId, 1);
  pr.setShapeMaximum(puz, pieceId, 1);

  const std::unique_ptr<Assembler> assm = makeAssembler(puz, pr, SolverType::Classic);
  MatrixOptions mo;
  mo.strictColors = true;
  REQUIRE(assm->createMatrix(mo, StopToken()) == AssemblerError::None);

  unsigned int n = 0;
  (void)assm->assemble(
      [&](const Assembly&) {
        n++;
        return true;
      },
      SearchOptions{}, StopToken{});
  REQUIRE(n >= 1);
}

TEST_CASE("sliding: variable cells are corridors beside normal floor", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 2, 1);
  puz.getShape(trayId).setState(0, 0, 0, kVariable);

  const unsigned int pieceId = puz.addShape(1, 1, 1);
  puz.getShape(pieceId).setState(0, 0, 0, kFilled);
  // no goal stamp: the piece starts on the corridor and must leave it
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, pieceId, 1);

  Assembly start(puz.getGridType());
  start.addPlacement(0, 0, 0, 0);

  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 1);
}

TEST_CASE("sliding: a shorter path keeps the solution number", "[sliding]") {
  // A 2x2 tray, piece p and a blocker q. A piece goes anywhere it can reach
  // in one move, so a longer path needs q to step aside first.
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 2, 2);

  const unsigned int p = puz.addShape(1, 1, 1);
  puz.getShape(p).setState(0, 0, 0, kFilled);
  const unsigned int q = puz.addShape(1, 1, 1);
  puz.getShape(q).setState(0, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, p, 1);
  pr.setShapeMaximum(puz, q, 1);
  REQUIRE(placeGoal(puz, pr, p, 1, 1));
  REQUIRE(placeGoal(puz, pr, q, 0, 1));

  // A solution needs a solve under way; the callback is reached directly
  // so the test can offer starts in a known order.
  pr.markSolving();
  SolveOptions o;
  o.disassemble = true;
  SolveSession solver(puz, pr, o);
  SolveSessionTestAccess::prime(solver, o);
  const GridType gt = puz.getGridType();

  auto at = [gt](int px, int py, int qx, int qy) {
    Assembly a(gt);
    a.addPlacement(0, px, py, 0);
    a.addPlacement(0, qx, qy, 0);
    return a;
  };

  // p (0,0), q (1,1): q moves to (0,1), then p goes round to (1,1). Two
  // moves. That is solution 0.
  REQUIRE(SolveSessionTestAccess::offer(solver, at(0, 0, 1, 1)));
  REQUIRE(pr.getNumSolutions() == 1);
  REQUIRE(pr.getSavedSolution(0).getSolutionNumber() == 0);
  REQUIRE(pr.getSavedSolution(0).getDisassembly()->getMoves() == 2);

  // p (1,0), q (0,1) is the same picture in one move. The count stays 1 and
  // the stored number stays 0. The Solver shows that number plus one.
  REQUIRE(SolveSessionTestAccess::offer(solver, at(1, 0, 0, 1)));
  REQUIRE(pr.getNumSolutions() == 1);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  REQUIRE(pr.getSavedSolution(0).getSolutionNumber() == 0);
  REQUIRE(pr.getSavedSolution(0).getDisassembly()->getMoves() == 1);

  // A different picture is the next solution. A shorter path to it keeps
  // that second number, and does not collide with the first.
  clearGoal(puz, pr, p);
  REQUIRE(placeGoal(puz, pr, p, 0, 0));
  REQUIRE(SolveSessionTestAccess::offer(solver, at(1, 1, 1, 0)));
  REQUIRE(pr.getNumSolutions() == 2);
  REQUIRE(SolveSessionTestAccess::offer(solver, at(1, 0, 0, 1)));
  REQUIRE(pr.getNumSolutions() == 2);
  REQUIRE(pr.getNumberOfSavedSolutions() == 2);

  bool saw0 = false;
  bool saw1 = false;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    const std::uint64_t n = pr.getSavedSolution(i).getSolutionNumber();
    REQUIRE(pr.getSavedSolution(i).getDisassembly()->getMoves() == 1);
    if (n == 0) {
      saw0 = true;
    }
    if (n == 1) {
      saw1 = true;
    }
  }
  REQUIRE(saw0);
  REQUIRE(saw1);
}

TEST_CASE("sliding: removing a labeled cell clears start and goal", "[sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 2, 1);
  Voxel& tray = puz.getShape(trayId);

  tray.setStartPiece(0, 0, 0, 2);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), 2);
  REQUIRE(tray.getStartPiece(0, 0, 0) == 2);
  REQUIRE(tray.getGoalPiece(0, 0, 0) == 2);

  tray.setState(0, 0, 0, kEmpty);
  REQUIRE(tray.getState(0, 0, 0) == kEmpty);
  REQUIRE(tray.getStartPiece(0, 0, 0) == 0);
  REQUIRE(tray.getGoalPiece(0, 0, 0) == 0);

  // a mark written onto the empty cell must not come back when it is filled
  tray.setGoalPiece(tray.getIndex(0, 0, 0), 2);
  tray.setState(0, 0, 0, kFilled);
  REQUIRE(tray.getState(0, 0, 0) == kFilled);
  REQUIRE(tray.getStartPiece(0, 0, 0) == 0);
  REQUIRE(tray.getGoalPiece(0, 0, 0) == 0);
}

TEST_CASE("sliding: the test sliding puzzle solves to one 81-move path", "[sliding][solver]") {
  Puzzle puzzle = test::loadFixture("test_sliding_solver.xmpuzzle");
  REQUIRE(puzzle.getGridType().getType() == GridKind::Sliding);
  Problem& pr = puzzle.getProblem(0);

  // the file holds the answer: cleared, so this run is the solver
  pr.removeAllSolutions();

  SolveOptions o = findSolutions();
  o.reduce = true;
  const std::unique_ptr<SolveSession> solver = solveRun(puzzle, o);
  REQUIRE(solver->state() == SolveState::Finished);

  REQUIRE(pr.getNumSolutions() == 1);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation* path = pr.getSavedSolution(0).getDisassembly();
  REQUIRE(path != nullptr);
  CHECK(path->getMoves() == 81);
}

namespace {

// tests/data/puzzles/sliding/test_sliding_nested.xmpuzzle is a 12x8 tray with
// four 3x3 blocks, a C-shaped "Cave" open to the left, and a 3x3 "Smile" in
// the Cave's pocket. The blocks start along the top and the Cave and Smile
// along the bottom; the goal swaps them. The Smile cannot leave the pocket
// (holes beside the opening), and the Cave cannot rise or fall past it
// alone, so the puzzle needs the Cave to carry what sits in its pocket.
Puzzle nestedPuzzle() { return test::loadFixture("sliding/test_sliding_nested.xmpuzzle"); }

std::optional<Separation> solveNested(Puzzle& puzzle, const SolveOptions& o) {
  Problem& pr = puzzle.getProblem(0);
  pr.removeAllSolutions();
  const std::unique_ptr<SolveSession> solver = solveRun(puzzle, o);
  REQUIRE(solver->state() == SolveState::Finished);
  if (pr.getNumberOfSavedSolutions() == 0) {
    return std::nullopt;
  }
  return *pr.getSavedSolution(0).getDisassembly();
}

}  // namespace

TEST_CASE("sliding: the nested puzzle has no solution without nested slides",
          "[sliding][solver]") {
  Puzzle puzzle = nestedPuzzle();
  REQUIRE(puzzle.getGridType().getType() == GridKind::Sliding);
  const Problem& pr = puzzle.getProblem(0);

  CHECK_FALSE(solveNested(puzzle, findSolutions()).has_value());
  CHECK(pr.getNumAssemblies() == 1);
  CHECK(pr.getNumSolutions() == 0);
}

// tests/data/puzzles/sliding/slide_layers_blocks.xmpuzzle, after Number
// Blocks: eight blocks in a frame of three layers, each a 2x2 plate below
// and above joined by a hollow middle with nubs that stick out sideways
// into the gap under the frame's ring or under a neighbor's top plate.
// The nubs make it: the same start with them cut off takes 18 moves.
// Generated by a search back from the goal for its farthest start.
TEST_CASE("sliding: blocks whose middle layers catch on each other", "[sliding][solver]") {
  Puzzle puzzle = test::loadFixture("sliding/slide_layers_blocks.xmpuzzle");
  REQUIRE(puzzle.getNumberOfShapes() == 9);
  REQUIRE(puzzle.getShape(8).getZ() == 3);
  CHECK(startGoalError(puzzle, 8).empty());
  const Problem& pr = puzzle.getProblem(0);
  const std::optional<Separation> path = solveNested(puzzle, findSolutions());
  REQUIRE(path.has_value());
  CHECK(pr.getNumSolutions() == 1);
  CHECK(path->getMoves() == 104);
  // one block a move, and the blocks fill the frame's three layers: none
  // changes layer
  for (unsigned int step = 0; step < path->getMoves(); step++) {
    unsigned int moved = 0;
    for (unsigned int i = 0; i < path->getPieceNumber(); i++) {
      const State& a = path->getState(step);
      const State& b = path->getState(step + 1);
      CHECK(a.getZ(i) == b.getZ(i));
      moved += a.getX(i) != b.getX(i) || a.getY(i) != b.getY(i) ? 1 : 0;
    }
    CHECK(moved == 1);
  }
}

TEST_CASE("sliding: the nested puzzle solves with nested slides", "[sliding][solver]") {
  Puzzle puzzle = nestedPuzzle();
  const Problem& pr = puzzle.getProblem(0);

  SolveOptions o = findSolutions();
  o.sliding.nested = true;
  const std::optional<Separation> path = solveNested(puzzle, o);
  REQUIRE(path.has_value());
  CHECK(pr.getNumSolutions() == 1);
  // the fewest moves when a move is one piece, or a piece and what is
  // nested in it, going anywhere it can reach
  CHECK(path->getMoves() == 47);

  // Every move is one rigid group with a route, and at least one carries
  // more than one piece. The animation needs the route; a move whose pieces
  // shift by different amounts would have none.
  unsigned int carried = 0;
  for (unsigned int step = 0; step < path->getMoves(); step++) {
    std::vector<unsigned int> movers;
    const std::vector<SlideShift> route = slideRoute(puzzle, pr, *path, step, &movers);
    INFO("step " << step);
    REQUIRE(route.size() >= 2);
    if (movers.size() > 1) {
      carried++;
    }
  }
  CHECK(carried > 0);

  // the Smile ends in the Cave's pocket, as the goal map asks
  const State& last = path->getState(path->getMoves());
  const State& first = path->getState(0);
  CHECK((last.getX(4) - first.getX(4)) == (last.getX(5) - first.getX(5)));
  CHECK((last.getY(4) - first.getY(4)) == (last.getY(5) - first.getY(5)));
}

namespace {

// A 3x2 tray, two layers deep, and a 1x1 piece two layers tall. The bottom
// layer is all floor; the top layer has a wall at (1,0), and with `blocked`
// also at (1,1). Checking only the bottom layer, the piece would slide
// straight from (0,0) to its goal at (2,0). With both layers it must go
// round through row 1, or cannot go at all.
Puzzle twoLayerTray(bool blocked) {
  const std::string top1 = blocked ? "#_#" : "###";
  const std::string xml =
      "<?xml version=\"1.0\"?>\n"
      "<puzzle version=\"2\"><gridType type=\"5\"/>"
      "<colors><color red=\"40\" green=\"70\" blue=\"100\"/>"
      "<color red=\"70\" green=\"100\" blue=\"40\"/></colors><shapes>"
      "<voxel x=\"3\" y=\"2\" z=\"2\" name=\"sg:1:\" goal=\"0,0,2,0,0,0,0,0,2,0,0,0\" "
      "type=\"0\">"
      "#2######2_#" +
      top1 +
      "</voxel>"
      "<voxel x=\"1\" y=\"1\" z=\"2\" name=\"A\" type=\"0\">#2#2</voxel>"
      "</shapes><problems><problem name=\"START/GOAL 1\" state=\"0\"><shapes>"
      "<shape id=\"1\" count=\"1\"/></shapes><result id=\"0\"/><bitmap/>"
      "</problem></problems><comment/></puzzle>";
  Result<Puzzle> p = io::parsePuzzle(xml);
  REQUIRE(p.has_value());
  return std::move(*p);
}

}  // namespace

TEST_CASE("sliding: every layer of a deeper tray blocks a move", "[sliding]") {
  const Puzzle open = twoLayerTray(false);
  const Problem& pr = open.getProblem(0);
  REQUIRE(pr.resultValid());
  Assembly start(open.getGridType());
  start.addPlacement(0, 0, 0, 0);

  const std::optional<Separation> path = findSlidePath(open, pr, start);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 1);
  CHECK(path->getState(1).getX(0) == 2);
  CHECK(path->getState(1).getY(0) == 0);
  // round the top layer's wall: down, across, up
  std::vector<unsigned int> movers;
  const std::vector<SlideShift> route = slideRoute(open, pr, *path, 0, &movers);
  CHECK(route.size() == 4);

  const Puzzle shut = twoLayerTray(true);
  CHECK_FALSE(findSlidePath(shut, shut.getProblem(0), start).has_value());
}

namespace {

// The start layout from the file: four blocks along the top, the Cave and
// the Smile in its pocket below. The parts are the blocks, Cave, Smile.
Assembly nestedStart(const Puzzle& puzzle) {
  Assembly start(puzzle.getGridType());
  for (const std::pair<int, int> p :
       std::vector<std::pair<int, int>>{{0, 0}, {3, 0}, {6, 0}, {9, 0}, {3, 3}, {3, 4}}) {
    start.addPlacement(0, p.first, p.second, 0);
  }
  return start;
}

}  // namespace

TEST_CASE("sliding: a search says whether it finished or was stopped", "[sliding]") {
  Puzzle puzzle = nestedPuzzle();
  const Problem& pr = puzzle.getProblem(0);
  const Assembly start = nestedStart(puzzle);

  SlideSearch full;
  std::atomic<std::uint64_t> progress{0};
  full.progress = &progress;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, full).has_value());
  CHECK(full.outcome == SlideOutcome::NoPath);
  CHECK(full.visited > 0);
  CHECK(progress.load() == full.visited);

  StopSource stop;
  stop.request(StopKind::Pause);
  SlideSearch stopped;
  stopped.stop = stop.token();
  CHECK_FALSE(findSlidePath(puzzle, pr, start, stopped).has_value());
  CHECK(stopped.outcome == SlideOutcome::Stopped);
}

TEST_CASE("sliding: a search stops at its memory limit", "[sliding]") {
  Puzzle puzzle = nestedPuzzle();
  const Problem& pr = puzzle.getProblem(0);
  const Assembly start = nestedStart(puzzle);

  SlideSearch tight;
  tight.maxMemoryStates = 10;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, tight).has_value());
  CHECK(tight.outcome == SlideOutcome::Memory);
  CHECK(tight.memoryStates == 10);

  // the memory budget: about 2 GB normally, never less with high memory
  CHECK(memoryStates(200, false) == 10000000);
  CHECK(memoryStates(200, true) >= memoryStates(200, false));

  SlideSearch normal;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, normal).has_value());
  CHECK(normal.outcome == SlideOutcome::NoPath);

  SlideSearch high;
  high.highMemory = true;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, high).has_value());
  CHECK(high.outcome == SlideOutcome::NoPath);
  CHECK(high.memoryStates >= normal.memoryStates);
  // the position-table search packs more than 10 million into 2 GB
  CHECK(normal.memoryStates > 10000000);
}

TEST_CASE("sliding: the solver says when no solution is proven", "[sliding][solver]") {
  Puzzle puzzle = nestedPuzzle();
  puzzle.getProblem(0).removeAllSolutions();

  const std::unique_ptr<SolveSession> solver = solveRun(puzzle, findSolutions());
  REQUIRE(solver->state() == SolveState::Finished);
  CHECK(puzzle.getProblem(0).getNumSolutions() == 0);
  const std::string note = solver->note();
  CHECK(note.find("Every reachable arrangement was searched") != std::string::npos);
  CHECK(note.find("Allow Nested Slides") != std::string::npos);
}

// Not part of the suite (hidden tag): a throughput and memory benchmark for
// the position-table search. A 4x4 tray with fifteen unit pieces and the
// last two swapped in the goal, which no sliding can reach, so the search
// runs until its memory limit.
TEST_CASE("sliding: benchmark, fifteen pieces in a 4x4 tray", "[.bench][sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 4, 4);
  for (unsigned int k = 0; k < 15; k++) {
    const unsigned int id = puz.addShape(1, 1, 1);
    puz.getShape(id).setState(0, 0, 0, kFilled);
    Voxel& tray = puz.getShape(trayId);
    tray.setStartPiece(k % 4, k / 4, 0, id + 1);
    const unsigned int g = k == 13 ? 14 : k == 14 ? 13 : k;
    tray.setGoalPiece(tray.getIndex(g % 4, g / 4, 0), id + 1);
  }
  syncSlidingProblems(puz);
  const Problem& pr = puz.getProblem(0);

  Assembly start(puz.getGridType());
  for (unsigned int k = 0; k < 15; k++) {
    start.addPlacement(0, static_cast<int>(k % 4), static_cast<int>(k / 4), 0);
  }

  SlideSearch search;
  search.maxMemoryStates = 3000000;
  CHECK_FALSE(findSlidePath(puz, pr, start, search).has_value());
  CHECK(search.outcome == SlideOutcome::Memory);
}

// The published answer for Klotski, counting one piece going anywhere it
// can reach as one move, is 81; the puzzle has 25,955 arrangements when like
// pieces are not told apart.
TEST_CASE("sliding: Klotski takes 81 moves, like pieces searched as one", "[sliding]") {
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch search;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() == 81);
  CHECK(oneMoverPerStep(*path));
  CHECK(search.visited <= 25955);
  // the path starts where the pieces were put and ends at the goal
  for (std::size_t i = 0; i < kKlotski.size(); i++) {
    const auto pc = static_cast<unsigned int>(i);
    CHECK(path->getState(0).getX(pc) == kKlotski[i].x);
    CHECK(path->getState(0).getY(pc) == kKlotski[i].y);
  }
  CHECK(path->getState(81).getX(0) == 1);
  CHECK(path->getState(81).getY(0) == 3);
}

// Searching like pieces as one must not change the answer: as many moves as
// telling them apart, and as the legacy search.
TEST_CASE("sliding: like pieces searched as one give the same shortest path",
          "[sliding][slow]") {
  // A 4x3 tray: a 2x1 bar to take from the top left to the bottom right
  // through six unit pieces, two of them with goals of their own.
  const std::vector<SlideBlock> blocks = {
      {2, 1, 0, 0, 2, 2},  {1, 1, 2, 0, -1, -1}, {1, 1, 3, 0, -1, -1}, {1, 1, 0, 1, -1, -1},
      {1, 1, 1, 1, -1, -1}, {1, 1, 2, 1, 0, 0},  {1, 1, 0, 2, 3, 0},
  };
  unsigned int moves[3] = {0, 0, 0};
  std::uint64_t visited[3] = {0, 0, 0};
  for (int k = 0; k < 3; k++) {
    const SlideFixture f = slideFixture(4, 3, blocks);
    SlideSearch search;
    search.likePiecesApart = k == 1;
    search.legacy = k == 2;
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    REQUIRE(path.has_value());
    CHECK(oneMoverPerStep(*path));
    moves[k] = path->getMoves();
    visited[k] = search.visited;
  }
  CHECK(moves[0] == moves[1]);
  CHECK(moves[0] == moves[2]);
  CHECK(visited[0] < visited[1]);
}

// Hidden: Klotski searched to the end, with like pieces as one and told
// apart.
TEST_CASE("sliding: benchmark, every Klotski arrangement", "[.bench][sliding][klotski]") {
  for (int k = 0; k < 2; k++) {
    const SlideFixture f = slideFixture(4, 5, kKlotski);
    SlideSearch search;
    search.likePiecesApart = k == 1;
    const auto t0 = std::chrono::steady_clock::now();
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    WARN("klotski " << (k != 0 ? "pieces told apart" : "like pieces as one") << ": "
                    << (path ? path->getMoves() : 0) << " moves, " << search.visited
                    << " arrangements visited, " << s << " s");
  }
}

// Thirteen pieces with 32 places each need 65 bits to number an
// arrangement: one more than a 64-bit key holds. The table search then runs
// on 128-bit keys and must agree with the legacy search.
TEST_CASE("sliding: a tray too big for 64-bit keys is searched with 128-bit ones", "[sliding]") {
  std::vector<SlideBlock> blocks = {
      {1, 1, 0, 0, 7, 3},
      {1, 1, 7, 3, 0, 0},
  };
  for (int k = 0; k < 11; k++) {
    blocks.push_back({1, 1, 1 + k % 6, 1 + k / 6, -1, -1});
  }

  const SlideFixture f = slideFixture(8, 4, blocks);
  SlideSearch search;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
  REQUIRE(path.has_value());
  CHECK(oneMoverPerStep(*path));
  CHECK(path->getMoves() == 3);
  CHECK(search.memoryStates == memoryStates(3 * 16, false));

  const SlideFixture g = slideFixture(8, 4, blocks);
  SlideSearch legacy;
  legacy.legacy = true;
  const std::optional<Separation> old = findSlidePath(g.puz, g.pr(), *g.start, legacy);
  REQUIRE(old.has_value());
  CHECK(old->getMoves() == path->getMoves());
}

TEST_CASE("sliding: a piece sticking out of a pocket rides along only when partially nested",
          "[sliding]") {
  // A 3x4 tray. The outer piece is a C, open to the left, in columns 1-2:
  //   ##
  //   .#
  //   ##
  // The inner piece is 2x1, one cell in the pocket and one sticking out to
  // the left, so it is not wholly in the outer piece's outline. Both must
  // rise one row. Alone, neither can move; together they rise in one move.
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 3, 4);

  const unsigned int outer = puz.addShape(2, 3, 1);
  for (const std::pair<unsigned int, unsigned int> c :
       std::vector<std::pair<unsigned int, unsigned int>>{{0, 0}, {1, 0}, {1, 1}, {0, 2}, {1, 2}}) {
    puz.getShape(outer).setState(c.first, c.second, 0, kFilled);
  }
  const unsigned int inner = puz.addShape(2, 1, 1);
  puz.getShape(inner).setState(0, 0, 0, kFilled);
  puz.getShape(inner).setState(1, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, outer, 1);
  pr.setShapeMaximum(puz, inner, 1);
  REQUIRE(placeGoal(puz, pr, outer, 1, 0));
  REQUIRE(placeGoal(puz, pr, inner, 0, 1));
  REQUIRE(placeStart(puz, pr, outer, 1, 1));
  REQUIRE(placeStart(puz, pr, inner, 0, 2));

  Assembly start(puz.getGridType());
  start.addPlacement(0, 1, 1, 0);
  start.addPlacement(0, 0, 2, 0);

  for (int legacy = 0; legacy < 2; legacy++) {
    INFO("legacy " << legacy);
    SlideSearch plain;
    plain.legacy = legacy != 0;
    CHECK_FALSE(findSlidePath(puz, pr, start, plain).has_value());

    SlideSearch nested;
    nested.legacy = legacy != 0;
    nested.nested = true;
    CHECK_FALSE(findSlidePath(puz, pr, start, nested).has_value());

    // partially nested implies nested
    SlideSearch partial;
    partial.legacy = legacy != 0;
    partial.partialNested = true;
    const std::optional<Separation> path = findSlidePath(puz, pr, start, partial);
    REQUIRE(path.has_value());
    REQUIRE(path->getMoves() == 1);
    CHECK(path->getState(1).getY(0) == 0);
    CHECK(path->getState(1).getY(1) == 1);

    std::vector<unsigned int> movers;
    (void)slideRoute(puz, pr, *path, 0, &movers);
    CHECK(movers == std::vector<unsigned int>{0, 1});
  }
}

TEST_CASE("sliding: partial nesting still lets a piece leave a partly nested one behind",
          "[sliding]") {
  // A 5x3 tray. The outer piece is a C, open to the left, in columns 1-3,
  // with a two-cell pocket:
  //   ###
  //   ..#
  //   ###
  // A 1x1 sits wholly in the pocket at column 2; a 2x1 has one cell in the
  // pocket at column 1 and one sticking out at column 0. The goal moves the
  // C and the 1x1 one column right and leaves the 2x1 where it is: one move
  // with nesting. Partial nesting must keep that move, not only the one that
  // also takes the 2x1 along (which then has to come back).
  Puzzle puz = slidingPuzzle();
  ensureSetup(puz, 5, 3);

  const unsigned int outer = puz.addShape(3, 3, 1);
  for (const std::pair<unsigned int, unsigned int> c :
       std::vector<std::pair<unsigned int, unsigned int>>{
           {0, 0}, {1, 0}, {2, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}) {
    puz.getShape(outer).setState(c.first, c.second, 0, kFilled);
  }
  const unsigned int inner = puz.addShape(1, 1, 1);
  puz.getShape(inner).setState(0, 0, 0, kFilled);
  const unsigned int stub = puz.addShape(2, 1, 1);
  puz.getShape(stub).setState(0, 0, 0, kFilled);
  puz.getShape(stub).setState(1, 0, 0, kFilled);
  Problem& pr = puz.getProblem(0);
  pr.setShapeMaximum(puz, outer, 1);
  pr.setShapeMaximum(puz, inner, 1);
  pr.setShapeMaximum(puz, stub, 1);
  REQUIRE(placeGoal(puz, pr, outer, 2, 0));
  REQUIRE(placeGoal(puz, pr, inner, 3, 1));
  REQUIRE(placeGoal(puz, pr, stub, 0, 1));
  REQUIRE(placeStart(puz, pr, outer, 1, 0));
  REQUIRE(placeStart(puz, pr, inner, 2, 1));
  REQUIRE(placeStart(puz, pr, stub, 0, 1));

  Assembly start(puz.getGridType());
  start.addPlacement(0, 1, 0, 0);
  start.addPlacement(0, 2, 1, 0);
  start.addPlacement(0, 0, 1, 0);

  for (int legacy = 0; legacy < 2; legacy++) {
    INFO("legacy " << legacy);
    SlideSearch plain;
    plain.legacy = legacy != 0;
    std::optional<Separation> path = findSlidePath(puz, pr, start, plain);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 2);

    SlideSearch nested;
    nested.legacy = legacy != 0;
    nested.nested = true;
    path = findSlidePath(puz, pr, start, nested);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 1);

    SlideSearch partial;
    partial.legacy = legacy != 0;
    partial.partialNested = true;
    path = findSlidePath(puz, pr, start, partial);
    REQUIRE(path.has_value());
    REQUIRE(path->getMoves() == 1);
    std::vector<unsigned int> movers;
    (void)slideRoute(puz, pr, *path, 0, &movers);
    CHECK(movers == std::vector<unsigned int>{0, 1});
  }
}

namespace {

// A 5x5 tray with a C-shaped piece open to the right, a unit piece A in its
// pocket, three more unit pieces and two 2x2 blocks:
//   CCCu.
//   CA..u
//   CCC..
//   BBDDu
//   BBDD.
// Carrying A, the C can close round a unit piece it passes, and then the
// three move together: the C cannot move back without it. The C's goal
// takes in the variable bottom right cell, where no piece may stop, so no
// search finds a path and every one searches all there is.
struct OneWay {
  Puzzle puz{GridType(GridKind::Sliding)};
  std::unique_ptr<Assembly> start;
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
};

OneWay oneWayPuzzle() {
  OneWay w;
  const unsigned int trayId = addStartGoalShape(w.puz, 5, 5);
  w.start = std::make_unique<Assembly>(w.puz.getGridType());
  struct Piece {
    std::vector<std::pair<int, int>> cells;
    int x;
    int y;
  };
  const std::vector<std::pair<int, int>> unit = {{0, 0}};
  const std::vector<std::pair<int, int>> block = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  const std::vector<Piece> pieces = {
      {{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}, 0, 0},
      {unit, 1, 1},
      {unit, 4, 1},
      {block, 2, 3},
      {unit, 3, 0},
      {unit, 4, 3},
      {block, 0, 3},
  };
  for (std::size_t k = 0; k < pieces.size(); k++) {
    const Piece& pc = pieces[k];
    const unsigned int id = w.puz.addShape(3, 3, 1);
    for (const std::pair<int, int> c : pc.cells) {
      w.puz.getShape(id).setState(static_cast<unsigned int>(c.first),
                                  static_cast<unsigned int>(c.second), 0, kFilled);
      Voxel& tray = w.puz.getShape(trayId);
      tray.setStartPiece(static_cast<unsigned int>(pc.x + c.first),
                    static_cast<unsigned int>(pc.y + c.second), 0, id + 1);
      // the C's goal: the bottom right corner
      if (k == 0) {
        tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(2 + c.first),
                                        static_cast<unsigned int>(2 + c.second), 0),
                          id + 1);
      }
    }
    w.start->addPlacement(0, pc.x, pc.y, 0);
  }
  w.puz.getShape(trayId).setState(4, 4, 0, kVariable);
  syncSlidingProblems(w.puz);
  return w;
}

}  // namespace

// The search looks for an arrangement reached by a move that cannot be made
// backwards in every level before, the others only in the last two.
TEST_CASE("sliding: the search finds arrangements one-way moves lead back to", "[sliding]") {
  const OneWay w = oneWayPuzzle();

  std::uint64_t visited[2] = {0, 0};
  for (int nested = 0; nested < 2; nested++) {
    INFO("nested " << nested);
    SlideSearch search;
    search.nested = nested != 0;
    CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, search).has_value());
    CHECK(search.outcome == SlideOutcome::NoPath);
    CHECK(search.visited > 100);
    visited[nested] = search.visited;
  }
  // with nested slides the puzzle has one-way moves that lead back to
  // arrangements reached two or more moves before
  SlideSearch nested;
  nested.nested = true;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, nested).has_value());
  CHECK(nested.oneWayRepeats > 0);
  CHECK(visited[1] > visited[0]);
}

// It must find exactly the arrangements the legacy search does, which keeps
// them all (like pieces told apart, as the legacy search tells them).
TEST_CASE("sliding: the search counts the arrangements the legacy search does",
          "[sliding][slow]") {
  const OneWay w = oneWayPuzzle();

  for (int nested = 0; nested < 2; nested++) {
    INFO("nested " << nested);
    SlideSearch search;
    search.nested = nested != 0;
    search.likePiecesApart = true;
    CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, search).has_value());
    CHECK(search.outcome == SlideOutcome::NoPath);

    SlideSearch legacy;
    legacy.nested = nested != 0;
    legacy.legacy = true;
    CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, legacy).has_value());
    CHECK(legacy.outcome == SlideOutcome::NoPath);
    CHECK(search.visited == legacy.visited);
  }
}

// Small levels stay in memory, and so do big ones with nowhere to write;
// with no disk to spare, the search says so. memoryLevelBytes = 1 sends
// every level past the first two toward the disk.
TEST_CASE("sliding: the search keeps its levels where it can", "[sliding]") {
  const OneWay w = oneWayPuzzle();
  const ScratchDir scratch("slidedir");
  const std::filesystem::path dir = scratch.path() / "work";

  SlideSearch onDisk;
  onDisk.nested = true;
  onDisk.workDir = dir.string();
  onDisk.memoryLevelBytes = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, onDisk).has_value());
  CHECK(onDisk.outcome == SlideOutcome::NoPath);
  CHECK(onDisk.diskBytes > 0);
  CHECK(onDisk.depth > 2);
  // the search's own folder goes when it ends
  CHECK(std::filesystem::is_empty(dir));

  // a file where the folder should be: nowhere to write
  std::filesystem::remove_all(dir);
  { std::ofstream(dir.string()) << "x"; }
  SlideSearch inMemory;
  inMemory.nested = true;
  inMemory.workDir = dir.string();
  inMemory.memoryLevelBytes = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, inMemory).has_value());
  CHECK(inMemory.outcome == SlideOutcome::NoPath);
  CHECK(inMemory.diskBytes == 0);
  CHECK(inMemory.visited == onDisk.visited);
  std::filesystem::remove_all(dir);

  SlideSearch full;
  full.nested = true;
  full.workDir = dir.string();
  full.diskBudget = 1;
  full.memoryLevelBytes = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, full).has_value());
  CHECK(full.outcome == SlideOutcome::Disk);
  CHECK(!full.error.empty());
  // it names the work folder (its own folder in there is gone), and the
  // budget asked for, which the free space did not cut
  CHECK(full.error.find("(0.0 GB in " + dir.string() + ")") != std::string::npos);
  CHECK_FALSE(full.diskHeldToFree);
  std::filesystem::remove_all(dir);

  // a small search on a full disk: it never writes, nor makes its folder
  SlideSearch small;
  small.nested = true;
  small.workDir = dir.string();
  small.diskBudget = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, small).has_value());
  CHECK(small.outcome == SlideOutcome::NoPath);
  CHECK(small.diskBytes == 0);
  CHECK(small.visited == onDisk.visited);
  CHECK(!std::filesystem::exists(dir));
}

// The search runs on every core, but finds the same path each time, through
// the same arrangements.

// Decision 0028: a search reads its share of the memory searches running
// at once share as it goes. With room it keeps every level in memory; its
// share cut short as it runs (searches starting beside it), its older
// levels go to its files, and it searches what it would have alone.
TEST_CASE("sliding: a search keeps to its share of shared memory as it changes",
          "[sliding][memory][parallel]") {
  const OneWay w = oneWayPuzzle();
  const ScratchDir scratch("slideshare");
  const std::filesystem::path dir = scratch.path() / "work";

  MemoryShare roomy(1000000000);
  SlideSearch alone;
  alone.nested = true;
  alone.workDir = dir.string();
  alone.memoryShare = &roomy;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, alone).has_value());
  CHECK(alone.outcome == SlideOutcome::NoPath);
  CHECK(alone.diskBytes == 0);

  MemoryShare shared(1000000000);
  SlideSearch cut;
  cut.nested = true;
  cut.workDir = dir.string();
  cut.memoryShare = &shared;
  unsigned int levels = 0;
  cut.onLevel = [&](const std::string&) {
    if (++levels == 2) {
      shared.setTotal(1);
    }
  };
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, cut).has_value());
  CHECK(cut.outcome == SlideOutcome::NoPath);
  CHECK(cut.diskBytes > 0);
  CHECK(cut.visited == alone.visited);
  CHECK(cut.depth == alone.depth);
  // the limit it ended at, not the one it began with
  CHECK(cut.memoryStates < alone.memoryStates);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

TEST_CASE("sliding: the search finds the same path on any number of threads",
          "[sliding][parallel]") {
  std::vector<std::vector<int>> paths;
  for (const unsigned int threads : {1U, 3U, 8U}) {
    const SlideFixture f = slideFixture(4, 5, kKlotski);
    SlideSearch search;
    search.threads = threads;
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 81);
    CHECK(oneMoverPerStep(*path));
    std::vector<int> places;
    for (unsigned int s = 0; s <= path->getMoves(); s++) {
      for (unsigned int i = 0; i < path->getPieceNumber(); i++) {
        places.push_back(path->getState(s).getX(i));
        places.push_back(path->getState(s).getY(i));
      }
    }
    paths.push_back(places);
  }
  CHECK(paths[0] == paths[1]);
  CHECK(paths[0] == paths[2]);
}

// Hidden: random trays with a C-shaped piece that can close round others,
// each searched to the end, or to the goal, by the legacy search and the
// level search. They must agree on every arrangement there is, or on the
// moves to the goal.
TEST_CASE("sliding: fuzz, the level search agrees with the legacy search", "[.fuzz][sliding]") {
  const int trials = 300;
  std::uint64_t x = 1234567;
  auto rnd = [&x](int n) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return static_cast<int>(x % static_cast<std::uint64_t>(n));
  };
  int compared = 0;
  int found = 0;
  int repeats = 0;
  for (int trial = 0; trial < trials; trial++) {
    const int W = 5 + rnd(3);
    const int H = 4 + rnd(3);
    const bool reachable = rnd(2) == 0;
    Puzzle puz = slidingPuzzle();
    const unsigned int trayId =
        addStartGoalShape(puz, static_cast<unsigned int>(W), static_cast<unsigned int>(H));
    Assembly start(puz.getGridType());
    std::vector<std::vector<int>> occ(static_cast<std::size_t>(W),
                                      std::vector<int>(static_cast<std::size_t>(H), 0));
    struct Piece {
      std::vector<std::pair<int, int>> cells;
      int x;
      int y;
    };
    std::vector<Piece> pieces = {
        {{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}, 0, 0},
        {{{0, 0}}, 1, 1},
    };
    for (const Piece& pc : pieces) {
      for (const std::pair<int, int> c : pc.cells) {
        occ[static_cast<std::size_t>(pc.x + c.first)][static_cast<std::size_t>(pc.y + c.second)] =
            1;
      }
    }
    const std::vector<std::vector<std::pair<int, int>>> shapes = {
        {{0, 0}}, {{0, 0}, {1, 0}}, {{0, 0}, {0, 1}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    for (int extra = 2 + rnd(5); extra > 0; extra--) {
      const std::vector<std::pair<int, int>>& shape = shapes[static_cast<std::size_t>(rnd(4))];
      for (int tries = 0; tries < 50; tries++) {
        const int px = rnd(W);
        const int py = rnd(H);
        bool fits = true;
        for (const std::pair<int, int> c : shape) {
          if (px + c.first >= W || py + c.second >= H ||
              occ[static_cast<std::size_t>(px + c.first)][static_cast<std::size_t>(py + c.second)] !=
                  0) {
            fits = false;
          }
        }
        if (!fits) {
          continue;
        }
        for (const std::pair<int, int> c : shape) {
          occ[static_cast<std::size_t>(px + c.first)][static_cast<std::size_t>(py + c.second)] = 1;
        }
        pieces.push_back({shape, px, py});
        break;
      }
    }
    for (std::size_t i = 0; i < pieces.size(); i++) {
      const unsigned int id = puz.addShape(3, 3, 1);
      for (const std::pair<int, int> c : pieces[i].cells) {
        puz.getShape(id).setState(static_cast<unsigned int>(c.first),
                                  static_cast<unsigned int>(c.second), 0, kFilled);
        Voxel& tray = puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(pieces[i].x + c.first),
                      static_cast<unsigned int>(pieces[i].y + c.second), 0, id + 1);
        if (i == 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(W - 3 + c.first),
                                          static_cast<unsigned int>(H - 3 + c.second), 0),
                            id + 1);
        }
      }
      start.addPlacement(0, pieces[i].x, pieces[i].y, 0);
    }
    // a variable cell in the C's goal: no path, and both search it all
    if (!reachable) {
      if (occ[static_cast<std::size_t>(W - 1)][static_cast<std::size_t>(H - 1)] != 0) {
        continue;
      }
      puz.getShape(trayId).setState(static_cast<unsigned int>(W - 1),
                                    static_cast<unsigned int>(H - 1), 0, kVariable);
    }
    syncSlidingProblems(puz);
    const Problem& pr = puz.getProblem(0);

    SlideSearch level;
    level.nested = true;
    level.likePiecesApart = true;
    level.threads = 1 + static_cast<unsigned int>(rnd(4));
    level.maxMemoryStates = 3000000;
    const std::optional<Separation> levelPath = findSlidePath(puz, pr, start, level);
    if (level.outcome == SlideOutcome::Memory) {
      continue;
    }

    SlideSearch legacy;
    legacy.nested = true;
    legacy.legacy = true;
    legacy.maxMemoryStates = 3000000;
    const std::optional<Separation> legacyPath = findSlidePath(puz, pr, start, legacy);
    if (legacy.outcome == SlideOutcome::Memory) {
      continue;
    }

    INFO("trial " << trial);
    CHECK(level.outcome == legacy.outcome);
    if (level.outcome == SlideOutcome::NoPath) {
      CHECK(level.visited == legacy.visited);
    }
    if (levelPath && legacyPath) {
      CHECK(levelPath->getMoves() == legacyPath->getMoves());
      found++;
    }
    compared++;
    if (level.oneWayRepeats > 0) {
      repeats++;
    }
  }
  WARN("slide fuzz: " << compared << " compared, " << found << " with a path, " << repeats
                      << " with one-way repeats");
}

// On a tray of layers, with vertical moves, a piece may move up and down
// too: here a cube is lifted over another and put down on the far side, one
// move. Without them it stays in its layer and cannot get past.
TEST_CASE("sliding: a piece is lifted over another on a tray of layers", "[sliding]") {
  const auto build = [](unsigned int layers) {
    SlideFixture f = slideFixture(3, 1, {{1, 1, 0, 0, 2, 0}, {1, 1, 1, 0, 1, 0}});
    f.puz.getShape(0).resize(3, 1, layers, kFilled);
    return f;
  };
  for (int legacy = 0; legacy < 2; legacy++) {
    INFO("legacy " << legacy);
    SlideFixture f = build(2);
    SlideSearch inLayer;
    inLayer.legacy = legacy != 0;
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, inLayer).has_value());
    CHECK(inLayer.outcome == SlideOutcome::NoPath);

    SlideSearch search;
    search.legacy = legacy != 0;
    search.verticalMoves = true;
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    REQUIRE(path.has_value());
    REQUIRE(path->getMoves() == 1);
    const State& end = path->getState(1);
    CHECK((end.getX(0) == 2 && end.getY(0) == 0 && end.getZ(0) == 0));
    CHECK((end.getX(1) == 1 && end.getZ(1) == 0));
    // up, across, down
    std::vector<unsigned int> movers;
    const std::vector<SlideShift> route = slideRoute(f.puz, f.pr(), *path, 0, &movers);
    CHECK(route == std::vector<SlideShift>{{0, 0, 0}, {0, 0, 1}, {2, 0, 1}, {2, 0, 0}});
    CHECK(movers == std::vector<unsigned int>{0});

    // the player follows the route: half way the cube is over the other
    DisasmToMoves mv(&*path, 6, 2);
    applySlideRoutes(f.puz, f.pr(), *path, mv);
    mv.setStep(0.5F, false, true);
    CHECK(mv.getZ(0) - static_cast<float>(path->getState(0).getZ(0)) == 1.0F);
    CHECK(mv.getX(0) - static_cast<float>(path->getState(0).getX(0)) == 1.0F);
  }

  // one layer: no way past
  SlideFixture flat = build(1);
  SlideSearch vertical;
  vertical.verticalMoves = true;
  CHECK_FALSE(findSlidePath(flat.puz, flat.pr(), *flat.start, vertical).has_value());
}

// Searched from both ends, a puzzle whose goal is one arrangement gets a
// path as short as the search from the start alone finds, through fewer
// arrangements; a goal no sliding reaches is still proven out of reach.
TEST_CASE("sliding: searching from both ends finds a path as short", "[sliding]") {
  const SlideFixture f = eightPuzzle();
  SlideSearch oneEnd;
  const std::optional<Separation> fromStart = findSlidePath(f.puz, f.pr(), *f.start, oneEnd);
  REQUIRE(fromStart.has_value());
  CHECK(oneMoverPerStep(*fromStart));

  SlideSearch bothEnds;
  bothEnds.meetFrom = 1;
  const std::optional<Separation> met = findSlidePath(f.puz, f.pr(), *f.start, bothEnds);
  REQUIRE(met.has_value());
  CHECK(met->getMoves() == fromStart->getMoves());
  CHECK(oneMoverPerStep(*met));
  CHECK(bothEnds.visited < oneEnd.visited);
  // the path's ends are the start and the goal
  const State& last = met->getState(met->getMoves());
  const int gx[8] = {0, 1, 2, 0, 1, 2, 0, 1};
  const int gy[8] = {0, 0, 0, 1, 1, 1, 2, 2};
  for (unsigned int i = 0; i < 8; i++) {
    CHECK(last.getX(i) == gx[i]);
    CHECK(last.getY(i) == gy[i]);
  }

  // on several threads, the same path
  SlideSearch threaded;
  threaded.meetFrom = 1;
  threaded.threads = 4;
  const std::optional<Separation> again = findSlidePath(f.puz, f.pr(), *f.start, threaded);
  REQUIRE(again.has_value());
  CHECK(placesAlong(*again) == placesAlong(*met));

  // two pieces swapped: no sliding reaches the goal
  std::vector<SlideBlock> swapped;
  for (int i = 0; i < 8; i++) {
    const int x = i % 3;
    const int y = i / 3;
    const int g = i == 6 ? 7 : i == 7 ? 6 : i;
    swapped.push_back({1, 1, x, y, g % 3, g / 3});
  }
  const SlideFixture s = slideFixture(3, 3, swapped);
  SlideSearch none;
  none.meetFrom = 1;
  CHECK_FALSE(findSlidePath(s.puz, s.pr(), *s.start, none).has_value());
  CHECK(none.outcome == SlideOutcome::NoPath);
}

// With only every few levels kept for tracing the path back, the gaps are
// searched when the path is traced: the path is as short.
TEST_CASE("sliding: the path is traced across thinned levels", "[sliding][parallel]") {
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  for (const unsigned int every : {2U, 3U, 7U, 100U}) {
    for (const unsigned int threads : {1U, 3U}) {
      INFO("every " << every << " on " << threads << " thread(s)");
      SlideSearch search;
      search.keepEvery = every;
      search.threads = threads;
      const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
      REQUIRE(path.has_value());
      CHECK(path->getMoves() == 81);
      CHECK(oneMoverPerStep(*path));
      CHECK(search.levelInterval == every);
    }
  }
  // both ends and thinned levels together
  SlideSearch both;
  both.keepEvery = 3;
  both.meetFrom = 1;
  const SlideFixture e = eightPuzzle();
  const std::optional<Separation> path = findSlidePath(e.puz, e.pr(), *e.start, both);
  REQUIRE(path.has_value());
  CHECK(oneMoverPerStep(*path));
  SlideSearch plain;
  const std::optional<Separation> straight = findSlidePath(e.puz, e.pr(), *e.start, plain);
  REQUIRE(straight.has_value());
  CHECK(path->getMoves() == straight->getMoves());
}

// The older model with pieces the problem has 0 or 1 of (the original's
// generated fixture): every layout counts, a piece left out included, and
// the floor may stay empty wherever no piece is. The same four layouts
// from the plain search, the vector search and the split search, after
// reduce() as the session runs it (which merged the start cells into the
// piece columns and once lost the holes with them).
TEST_CASE("sliding: optional pieces give the same layouts on every search",
          "[sliding][assembler][parallel]") {
  const Puzzle puz = test::loadFixture("sliding/slide_pocket_partial.xmpuzzle");
  const Problem& pr = puz.getProblem(0);
  REQUIRE(pr.getNumberOfParts() == 2);
  ThreadPool pool(4);
  std::vector<std::set<std::string>> seen;
  for (const bool reduce : {false, true}) {
    for (const unsigned int threads : {1U, 4U}) {
      for (const bool vector : {false, true}) {
        INFO("reduce " << reduce << ", " << threads << " thread(s), vector " << vector);
        const std::unique_ptr<Assembler> assm = makeAssembler(puz, pr, SolverType::Classic);
        MatrixOptions mo;
        mo.strictColors = true;
        mo.keepMirrors = true;
        mo.keepRotations = true;
        REQUIRE(assm->createMatrix(mo, StopToken()) == AssemblerError::None);
        if (reduce) {
          (void)assm->reduce(StopToken{});
        }
        std::set<std::string> layouts;
        SearchOptions so;
        so.threads = threads;
        so.pool = &pool;
        so.vectorSearch = vector;
        (void)assm->assemble(
            [&](Assembly a) {
              std::string key;
              for (unsigned int i = 0; i < a.placementCount(); i++) {
                key += a.isPlaced(i) ? std::to_string(a.getX(i)) + "," + std::to_string(a.getY(i))
                                     : std::string("x");
                key += ';';
              }
              layouts.insert(key);
              return true;
            },
            so, StopToken{});
        CHECK(layouts.size() == 4);
        CHECK(layouts.contains("1,1;0,2;"));
        seen.push_back(layouts);
      }
    }
  }
  for (const std::set<std::string>& s : seen) {
    CHECK(s == seen[0]);
  }
}

namespace {

// What the boards of seeds [from, to) come to, searched with the stops of
// a grid cell as one and told apart: the two must agree on which boards
// have a path and on its moves. A board either search has no memory for
// (maxStates) is left out.
struct LooseTally {
  int compared = 0;
  int found = 0;
  int loose = 0;
  int longest = 0;
};

LooseTally compareLoose(std::uint64_t from, std::uint64_t to, std::uint64_t maxStates,
                        bool withLegacy) {
  LooseTally t;
  for (std::uint64_t seed = from; seed < to; seed++) {
    const GridBoard g = gridBoard(seed);
    INFO("seed " << seed);
    SlideSearch one;
    one.nested = true;
    one.partialNested = seed % 3 == 0;
    one.grid = 3;
    one.threads = 1 + static_cast<unsigned int>(seed % 4);
    one.maxMemoryStates = maxStates;
    SlideSearch apart = one;
    apart.gridStopsApart = true;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    const std::optional<Separation> each = findSlidePath(g.puz, g.pr(), *g.start, apart);
    CHECK_FALSE(apart.looseStops);
    if (one.outcome == SlideOutcome::Memory || apart.outcome == SlideOutcome::Memory) {
      continue;
    }
    t.compared++;
    CHECK(one.outcome == apart.outcome);
    CHECK(path.has_value() == each.has_value());
    if (path && each) {
      CHECK(path->getMoves() == each->getMoves());
      checkGridPath(g, *path);
      checkGridPath(g, *each);
      t.found++;
      t.longest = std::max(t.longest, static_cast<int>(path->getMoves()));
    }
    if (one.looseStops) {
      t.loose++;
    }
    if (withLegacy) {
      SlideSearch legacy;
      legacy.nested = true;
      legacy.partialNested = one.partialNested;
      legacy.grid = 3;
      legacy.legacy = true;
      legacy.maxMemoryStates = maxStates;
      const std::optional<Separation> old = findSlidePath(g.puz, g.pr(), *g.start, legacy);
      if (legacy.outcome != SlideOutcome::Memory) {
        CHECK(legacy.outcome == one.outcome);
        if (old && path) {
          CHECK(old->getMoves() == path->getMoves());
        }
      }
    }
  }
  return t;
}

}  // namespace

// On a grid with nested slides the stops a piece has inside one cell of
// the grid are searched as one (loose stops, decision 0031): the same
// search in fewer arrangements. The path has the moves of the search that
// tells the stops apart, and is a real one; a search with no path goes
// through fewer arrangements.
TEST_CASE("sliding: the stops of a grid cell are searched as one", "[sliding]") {
  const GridBoard g = cupsAndUnits(true);
  SlideSearch one;
  one.nested = true;
  one.grid = 3;
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
  REQUIRE(path.has_value());
  CHECK(one.looseStops);
  checkGridPath(g, *path);

  SlideSearch apart;
  apart.nested = true;
  apart.grid = 3;
  apart.gridStopsApart = true;
  const std::optional<Separation> each = findSlidePath(g.puz, g.pr(), *g.start, apart);
  REQUIRE(each.has_value());
  CHECK_FALSE(apart.looseStops);
  checkGridPath(g, *each);
  CHECK(path->getMoves() == each->getMoves());

  // loose stops are for the grid's searches with nested slides only (a
  // search stopped as it begins says how it would go)
  StopSource stop;
  stop.request(StopKind::Pause);
  SlideSearch plain;
  plain.grid = 3;
  plain.stop = stop.token();
  (void)findSlidePath(g.puz, g.pr(), *g.start, plain);
  CHECK_FALSE(plain.looseStops);
  SlideSearch full;
  full.nested = true;
  full.stop = stop.token();
  (void)findSlidePath(g.puz, g.pr(), *g.start, full);
  CHECK_FALSE(full.looseStops);

  const GridBoard none = cupsAndUnits(false, 2);
  SlideSearch all;
  all.nested = true;
  all.grid = 3;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, all).has_value());
  CHECK(all.outcome == SlideOutcome::NoPath);
  SlideSearch allApart = apart;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, allApart).has_value());
  CHECK(allApart.outcome == SlideOutcome::NoPath);
  CHECK(all.visited < allApart.visited);
}

// The same on boards of every kind: whole blocks, cups, rings and blocks
// with a corner cut out, small pieces that lie across the grid's lines,
// pieces with and without goals, with and without partial nesting.
TEST_CASE("sliding: the stops of a grid cell searched as one give the same fewest moves",
          "[sliding][parallel]") {
  const LooseTally t = compareLoose(0, 16, 6000, false);
  // the boards are worth the comparison: most collapse, some have a path
  CHECK(t.compared > 8);
  CHECK(t.loose > 6);
  CHECK(t.found > 3);
}

// More of them, and larger searches: with three small pieces loose at once,
// and a board searched to the end.
TEST_CASE("sliding: the stops of a grid cell searched as one, more boards",
          "[sliding][parallel][slow]") {
  const LooseTally t = compareLoose(0, 60, 20000, false);
  // the boards are worth the comparison: most collapse, many have a path
  CHECK(t.compared > 40);
  CHECK(t.loose > 30);
  CHECK(t.found > 15);
  CHECK(t.longest >= 8);

  const GridBoard none = cupsAndUnits(false);
  SlideSearch all;
  all.nested = true;
  all.grid = 3;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, all).has_value());
  CHECK(all.outcome == SlideOutcome::NoPath);
  SlideSearch allApart = all;
  allApart.gridStopsApart = true;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, allApart).has_value());
  CHECK(allApart.outcome == SlideOutcome::NoPath);
  CHECK(all.visited < allApart.visited);
}

TEST_CASE("sliding: fuzz, the stops of a grid cell searched as one agree with them apart",
          "[.fuzz][sliding]") {
  const LooseTally t = compareLoose(1000, 4000, 300000, true);
  WARN("loose stops fuzz: " << t.compared << " compared, " << t.found << " with a path (longest "
                            << t.longest << "), " << t.loose << " collapsed");
}

namespace {

// A board cw x ch cells of 3x3 blocks with one cell empty, bottom right.
// The blocks in the cells of `cups` (numbered row by row) are cups open at
// the top with a unit piece in the pocket. The goal has the empty cell top
// left: the bottom row's blocks one cell to the right, then the left
// column's one cell down, each one move and none to spare. With `copies`
// the whole blocks are copies of one piece and the unit pieces of another.
GridBoard blockBoard(int cw, int ch, const std::vector<int>& cups, bool copies = false) {
  std::vector<BoardCells> shapes;
  std::vector<GridBoard::Place> places;
  std::vector<GridBoard::Place> units;
  for (int cell = 0; cell < cw * ch - 1; cell++) {
    const int cx = cell % cw;
    const int cy = cell / cw;
    const int gx = cy == ch - 1 ? cx + 1 : cx;
    const int gy = cx == 0 && cy < ch - 1 ? cy + 1 : cy;
    const bool cup = std::find(cups.begin(), cups.end(), cell) != cups.end();
    shapes.push_back(blockWithout(cup ? BoardCells{{1, 0}, {1, 1}} : BoardCells{}));
    places.push_back({3 * cx, 3 * cy, 3 * gx, 3 * gy, true});
    if (cup) {
      units.push_back({3 * cx + 1, 3 * cy + 1, 3 * gx + 1, 3 * gy + 1, false});
    }
  }
  for (const GridBoard::Place& u : units) {
    shapes.push_back({{0, 0}});
    places.push_back(u);
  }
  return gridBoardOf(3 * cw, 3 * ch, shapes, places, copies);
}

}  // namespace

// With a grid a key holds a number for each stop of a piece, not for every
// position it fits (the A/B switch plainKeys): the same search, level for
// level and step for step, in fewer bits.
TEST_CASE("sliding: a grid's keys number only the stops", "[sliding][parallel]") {
  std::vector<GridBoard> boards;
  boards.push_back(cupsAndUnits(true));
  boards.push_back(cupsAndUnits(false, 2));
  boards.push_back(blockBoard(3, 2, {1, 3}));
  for (std::uint64_t seed = 0; seed < 10; seed++) {
    boards.push_back(gridBoard(seed));
  }
  int found = 0;
  int loose = 0;
  for (std::size_t b = 0; b < boards.size(); b++) {
    const GridBoard& g = boards[b];
    // nested slides with loose stops and with the stops apart, on one thread
    // and on several; then the grid without nested slides
    for (int kind = 0; kind < 5; kind++) {
      INFO("board " << b << " kind " << kind);
      SlideSearch compact;
      compact.nested = kind < 4;
      compact.partialNested = compact.nested && b % 3 == 0;
      compact.gridStopsApart = kind == 1 || kind == 3;
      compact.threads = kind >= 2 ? 3 : 1;
      compact.grid = 3;
      compact.maxMemoryStates = 6000;
      SlideSearch plain = compact;
      plain.plainKeys = true;
      LevelSizes compactLevels;
      LevelSizes plainLevels;
      compactLevels.watch(compact);
      plainLevels.watch(plain);
      const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, compact);
      const std::optional<Separation> same = findSlidePath(g.puz, g.pr(), *g.start, plain);
      CHECK(compact.looseStops == plain.looseStops);
      loose += compact.looseStops ? 1 : 0;
      if (compact.outcome == SlideOutcome::Memory || plain.outcome == SlideOutcome::Memory) {
        // a wider key, and the threads' own buffers, fill the memory a level
        // sooner or later: the levels both finished are the same
        const std::size_t both = std::min(compactLevels.lines.size(), plainLevels.lines.size());
        CHECK(both > 0);
        compactLevels.lines.resize(both);
        plainLevels.lines.resize(both);
        CHECK(compactLevels.lines == plainLevels.lines);
        continue;
      }
      CHECK(compact.outcome == plain.outcome);
      CHECK(compactLevels.lines == plainLevels.lines);
      REQUIRE(path.has_value() == same.has_value());
      if (path) {
        CHECK(path->getMoves() == same->getMoves());
        checkGridPath(g, *path);
        found++;
      }
      if (compact.threads == 1) {
        // several threads end the last level where they happen to be
        CHECK(compact.visited == plain.visited);
        if (path) {
          CHECK(placesAlong(*path) == placesAlong(*same));
        }
      }
    }
  }
  // the boards are worth the comparison
  CHECK(found > 15);
  CHECK(loose > 10);
}

// A board whose pieces fit the empty tray in more places than a key of 128
// bits can number is searched in one all the same when a grid leaves few
// enough stops; with every position numbered it goes to a wider key
// (decision 0037), which finds a path as short.
TEST_CASE("sliding: a board too big for its positions is searched by its stops", "[sliding]") {
  // 19 blocks at 130 places each and three units: 176 bits, or 113
  const GridBoard g = blockBoard(5, 4, {6, 10, 18});
  // from the start alone, where the stops are loose
  SlideSearch stops;
  stops.nested = true;
  stops.grid = 3;
  stops.threads = 2;
  stops.oneEnd = true;
  SlideSearch apart = stops;
  apart.gridStopsApart = true;
  SlideSearch ends = stops;
  ends.oneEnd = false;
  LevelSizes levels;
  levels.watch(stops);
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, stops);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() == 7);
  CHECK(stops.looseStops);
  CHECK_FALSE(levels.lines.empty());
  CHECK_FALSE(levels.fromGoal());
  checkGridPath(g, *path);

  const std::optional<Separation> each = findSlidePath(g.puz, g.pr(), *g.start, apart);
  REQUIRE(each.has_value());
  CHECK(each->getMoves() == 7);

  // its goal is one arrangement: from both ends, in the wide key
  LevelSizes both;
  both.watch(ends);
  const std::optional<Separation> met = findSlidePath(g.puz, g.pr(), *g.start, ends);
  REQUIRE(met.has_value());
  CHECK(met->getMoves() == 7);
  CHECK(both.fromGoal());
  CHECK_FALSE(ends.looseStops);
  checkGridPath(g, *met);

  CHECK(stops.keyWidth == 128);
  CHECK(ends.keyWidth == 128);

  // every position numbered: 176 bits, the key of 192
  SlideSearch plain;
  plain.nested = true;
  plain.grid = 3;
  plain.plainKeys = true;
  LevelSizes wide;
  wide.watch(plain);
  const std::optional<Separation> old = findSlidePath(g.puz, g.pr(), *g.start, plain);
  REQUIRE(old.has_value());
  CHECK(old->getMoves() == 7);
  CHECK_FALSE(wide.lines.empty());
  CHECK(plain.keyWidth == 192);
  CHECK(plain.keyBits == 176);
  checkGridPath(g, *old);
}

// Loose stops may take a bit more a piece than the stops apart do. A board
// that fits the narrow key only with the stops apart is searched with loose
// stops in the wide key: the wide key is tried before the loose stops are
// given up.
TEST_CASE("sliding: loose stops take the wide key before they are given up", "[sliding]") {
  // 11 blocks at 12 stops and 4 units at 24 stops, or 36 with the loose
  // ones: 64 bits apart, 68 loose
  const GridBoard g = blockBoard(4, 3, {1, 2, 5, 6});
  // from the start alone: a search from both ends has the stops apart
  SlideSearch one;
  one.nested = true;
  one.grid = 3;
  one.oneEnd = true;
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
  REQUIRE(path.has_value());
  CHECK(one.looseStops);
  CHECK(path->getMoves() == 5);
  checkGridPath(g, *path);

  SlideSearch apart = one;
  apart.gridStopsApart = true;
  const std::optional<Separation> each = findSlidePath(g.puz, g.pr(), *g.start, apart);
  REQUIRE(each.has_value());
  CHECK_FALSE(apart.looseStops);
  CHECK(each->getMoves() == 5);
  // the wide key shows in what an arrangement costs the search
  CHECK(one.memoryStates < apart.memoryStates);
}

namespace {

// The pieces of oneWayPuzzle as a board, for a search without a grid: the
// C carrying A can close round a unit piece it passes, and cannot leave it
// behind again. The C must end at (cx, cy), the others anywhere.
GridBoard oneWayBoard(int cx, int cy) {
  const BoardCells unit = {{0, 0}};
  const BoardCells block = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  const BoardCells c = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}};
  return gridBoardOf(5, 5, {c, unit, unit, block, unit, unit, block},
                     {{0, 0, cx, cy, false},
                      {1, 1, -1, -1, false},
                      {4, 1, -1, -1, false},
                      {2, 3, -1, -1, false},
                      {3, 0, -1, -1, false},
                      {4, 3, -1, -1, false},
                      {0, 3, -1, -1, false}});
}

// A 7x5 board (searched without a grid): a cup five cells wide with a
// pocket of nine, open at the top, `units` copies of a unit piece in the
// pocket, and a bar in the cup's way:
//   .......
//   C...C..
//   C...C..
//   CuuuC..
//   CCCCCBB
// The cup is to go two cells to the right with what it holds, each unit
// where it lies in it, and the bar to the bottom left: three moves.
GridBoard fullCup(int units) {
  BoardCells cup;
  for (int y = 0; y < 4; y++) {
    cup.emplace_back(0, y);
    cup.emplace_back(4, y);
  }
  for (int x = 1; x < 4; x++) {
    cup.emplace_back(x, 3);
  }
  std::vector<BoardCells> shapes = {cup, {{0, 0}, {1, 0}}};
  std::vector<GridBoard::Place> places = {{0, 1, 2, 1, false}, {5, 4, 0, 4, false}};
  for (int k = 0; k < units; k++) {
    const int x = 1 + k % 3;
    const int y = 3 - k / 3;
    shapes.push_back({{0, 0}});
    places.push_back({x, y, x + 2, y, false});
  }
  return gridBoardOf(7, 5, shapes, places, true);
}

// A 4x3 board (searched without a grid, and without nested slides): a bar
// and eight copies of a unit piece. The bar is to go to the bottom right,
// with the units over the cells they lie on now, whichever on which:
//   BBuu
//   uuuu
//   uu..
GridBoard barAmongUnits() {
  std::vector<BoardCells> shapes = {{{0, 0}, {1, 0}}};
  std::vector<GridBoard::Place> places = {{0, 0, 2, 2, false}};
  const BoardCells at = {{2, 0}, {3, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {0, 2}, {1, 2}};
  for (const std::pair<int, int> c : at) {
    shapes.push_back({{0, 0}});
    places.push_back({c.first, c.second, c.first, c.second, false});
  }
  return gridBoardOf(4, 3, shapes, places, true);
}

// What boards searched from the start alone and from both ends came to.
struct EndsTally {
  int compared = 0;
  int found = 0;
  int fromGoal = 0;
  int longest = 0;
};

// The board searched from the start alone and from both ends, on one thread
// and on three: the two agree on whether there is a path and on its moves,
// and each path is one of the board. A search from both ends has the stops
// of a grid cell apart. A board either search has no memory for is left out.
void compareEnds(const GridBoard& g, int grid, std::uint64_t maxStates, EndsTally& t) {
  for (const unsigned int threads : {1U, 3U}) {
    INFO("threads " << threads);
    SlideSearch one = nestedSearch(grid);
    one.threads = threads;
    one.maxMemoryStates = maxStates;
    SlideSearch both = one;
    one.oneEnd = true;
    bool fromGoal = false;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    const std::optional<Separation> met = searched(g, both, fromGoal);
    if (one.outcome == SlideOutcome::Memory || both.outcome == SlideOutcome::Memory) {
      continue;
    }
    t.compared++;
    CHECK(one.outcome == both.outcome);
    REQUIRE(path.has_value() == met.has_value());
    if (fromGoal) {
      // (a path met before the goal's first level is counted says nothing)
      t.fromGoal++;
      CHECK_FALSE(both.looseStops);
    }
    if (path) {
      CHECK(met->getMoves() == path->getMoves());
      checkGridPath(g, *path, grid != 0);
      checkGridPath(g, *met, grid != 0);
      t.found++;
      t.longest = std::max(t.longest, static_cast<int>(path->getMoves()));
    }
  }
}

// What checkSlideMovesBack found over boards.
struct BackTally {
  int checked = 0;
  std::uint64_t arrangements = 0;
  std::uint64_t oneWay = 0;
};

// The moves made backwards on the board are exactly its moves.
void checkBack(const Puzzle& puz, const Problem& pr, const Assembly& start, int grid,
               std::uint64_t limit, BackTally& t) {
  const SlideBackCheck c = checkSlideMovesBack(puz, pr, start, nestedSearch(grid), limit);
  if (!c.checked) {
    return;
  }
  t.checked++;
  t.arrangements += c.arrangements;
  t.oneWay += c.oneWay;
  CHECK(c.moves >= c.arrangements - 1);
  CHECK(c.missing == 0);
  CHECK(c.extra == 0);
  CHECK(c.wronglyTwoWay == 0);
}

void checkBack(const GridBoard& g, int grid, std::uint64_t limit, BackTally& t) {
  checkBack(g.puz, g.pr(), *g.start, grid, limit, t);
}

}  // namespace

// With nested slides a group may close round a piece it passes, and that
// move cannot be made backwards as it came. The search from the goal makes
// the moves backwards (decision 0033): for every arrangement a board can
// come to, the arrangements it gives are exactly those one move before, and
// a move it calls two-way is one the pieces can undo.
TEST_CASE("sliding: the moves made backwards are the moves", "[sliding]") {
  BackTally t;
  {
    // every position a stop; the C closes round the unit pieces
    const OneWay w = oneWayPuzzle();
    checkBack(w.puz, w.pr(), *w.start, 0, 100000, t);
    CHECK(t.checked == 1);
    CHECK(t.arrangements > 1000);
    CHECK(t.oneWay > 0);
  }
  checkBack(cupsAndUnits(false, 2), 3, 100000, t);
  checkBack(blockBoard(3, 2, {1, 3}), 3, 100000, t);
  checkBack(blockBoard(3, 2, {1, 3}, true), 3, 100000, t);
  checkBack(swappedBlocks(), 3, 100000, t);
  checkBack(fullCup(2), 0, 100000, t);
  CHECK(t.checked == 6);

  BackTally boards;
  for (std::uint64_t seed = 0; seed < 40; seed++) {
    INFO("seed " << seed);
    checkBack(gridBoard(seed), 3, 20000, boards);
  }
  // the boards are worth the check: most are small enough, some have
  // moves that cannot be undone
  CHECK(boards.checked > 15);
  CHECK(boards.oneWay > 0);
}

TEST_CASE("sliding: fuzz, the moves made backwards are the moves", "[.fuzz][sliding]") {
  BackTally t;
  for (std::uint64_t seed = 1000; seed < 3000; seed++) {
    INFO("seed " << seed);
    checkBack(gridBoard(seed), 3, 300000, t);
  }
  WARN("moves backwards fuzz: " << t.checked << " boards, " << t.arrangements
                                << " arrangements, " << t.oneWay << " one-way moves");
}

// With nested slides the search goes from both ends when the goal is one
// arrangement (decision 0033): the fewest moves of the search from the
// start alone, and a path of the board, on one thread and on several.
TEST_CASE("sliding: nested slides searched from both ends give the same fewest moves",
          "[sliding][parallel]") {
  EndsTally t;
  {
    INFO("blocks");
    compareEnds(blockBoard(3, 2, {1, 3}), 3, 20000, t);
    compareEnds(blockBoard(3, 3, {0, 4}), 3, 20000, t);
    // whole blocks that are copies of a piece, and unit pieces of another
    compareEnds(blockBoard(3, 3, {0, 4, 7}, true), 3, 20000, t);
    // no path
    compareEnds(swappedBlocks(), 3, 20000, t);
    CHECK(t.compared == 8);
    CHECK(t.fromGoal == 8);
    CHECK(t.found == 6);
  }
  for (std::uint64_t seed = 0; seed < 24; seed++) {
    INFO("seed " << seed);
    const GridBoard g = gridBoard(seed);
    // as it is made: few have a goal that is one arrangement
    compareEnds(g, 3, 20000, t);
    SlideSearch one = nestedSearch(3);
    one.oneEnd = true;
    one.maxMemoryStates = 20000;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    if (path && path->getMoves() >= 2) {
      INFO("with every piece's goal where its path ends");
      compareEnds(endAsGoal(g, *path), 3, 20000, t);
    }
  }
  {
    // without a grid: the C that closes round a unit piece, to where it
    // gets with moves that cannot be undone; and blocks a cell at a time
    INFO("no grid");
    for (const int cx : {1, 2}) {
      const GridBoard g = oneWayBoard(cx, 2);
      SlideSearch one = nestedSearch(0);
      one.oneEnd = true;
      const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
      REQUIRE(path.has_value());
      const int before = t.fromGoal;
      compareEnds(endAsGoal(g, *path), 0, 100000, t);
      CHECK(t.fromGoal == before + 2);
    }
    const int before = t.fromGoal;
    compareEnds(blockBoard(3, 2, {1, 3}), 0, 100000, t);
    CHECK(t.fromGoal == before + 2);
  }
  // the boards are worth the comparison
  CHECK(t.compared > 60);
  CHECK(t.found > 40);
  CHECK(t.fromGoal > 20);
  CHECK(t.longest >= 10);
}

// More of them, and larger searches.
TEST_CASE("sliding: nested slides searched from both ends, more boards",
          "[sliding][parallel][slow]") {
  EndsTally t;
  for (std::uint64_t seed = 24; seed < 80; seed++) {
    INFO("seed " << seed);
    const GridBoard g = gridBoard(seed);
    SlideSearch one = nestedSearch(3);
    one.oneEnd = true;
    one.maxMemoryStates = 60000;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    if (path && path->getMoves() >= 2) {
      compareEnds(endAsGoal(g, *path), 3, 60000, t);
    }
  }
  CHECK(t.compared > 40);
  CHECK(t.fromGoal > 40);
  CHECK(t.longest >= 14);
}

TEST_CASE("sliding: fuzz, nested slides searched from both ends", "[.fuzz][sliding]") {
  EndsTally t;
  for (std::uint64_t seed = 1000; seed < 3000; seed++) {
    INFO("seed " << seed);
    const GridBoard g = gridBoard(seed);
    compareEnds(g, 3, 300000, t);
    SlideSearch one = nestedSearch(3);
    one.oneEnd = true;
    one.maxMemoryStates = 300000;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    if (path && path->getMoves() >= 2) {
      compareEnds(endAsGoal(g, *path), 3, 300000, t);
    }
  }
  WARN("both ends fuzz: " << t.compared << " compared, " << t.found << " with a path (longest "
                          << t.longest << "), " << t.fromGoal << " from the goal as well");
}

// Where the search cannot make its moves backwards, or the goal is not one
// arrangement, it goes from the start alone as before, and still solves.
TEST_CASE("sliding: nested slides are searched from the start alone where they must be",
          "[sliding]") {
  bool fromGoal = false;
  {
    // a piece that may end anywhere: the goal is many arrangements
    const GridBoard g = cupsAndUnits(true);
    SlideSearch s = nestedSearch(3);
    const std::optional<Separation> path = searched(g, s, fromGoal);
    REQUIRE(path.has_value());
    CHECK_FALSE(fromGoal);
    CHECK(s.looseStops);
    checkGridPath(g, *path);
  }
  {
    // partial nesting has no moves backwards
    const GridBoard g = blockBoard(3, 2, {1, 3});
    SlideSearch s = nestedSearch(3);
    s.partialNested = true;
    const std::optional<Separation> path = searched(g, s, fromGoal);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 3);
    CHECK_FALSE(fromGoal);
    CHECK(s.looseStops);
    checkGridPath(g, *path);
    SlideSearch partial = nestedSearch(3);
    partial.partialNested = true;
    CHECK_FALSE(checkSlideMovesBack(g.puz, g.pr(), *g.start, partial, 100000).checked);
  }
  {
    // the A/B switch
    const GridBoard g = blockBoard(3, 2, {1, 3});
    SlideSearch s = nestedSearch(3);
    s.oneEnd = true;
    const std::optional<Separation> path = searched(g, s, fromGoal);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 3);
    CHECK_FALSE(fromGoal);
    CHECK(s.looseStops);
    SlideSearch both = nestedSearch(3);
    REQUIRE(searched(g, both, fromGoal).has_value());
    CHECK(fromGoal);
    CHECK_FALSE(both.looseStops);
  }
  // A piece that could hold more pieces than the backward moves list the
  // subsets of: five units and the bar could be in the cup together, and
  // that is the most.
  for (const int units : {5, 6}) {
    INFO("units " << units);
    const GridBoard g = fullCup(units);
    SlideSearch s = nestedSearch(0);
    const std::optional<Separation> path = searched(g, s, fromGoal);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == 3);
    CHECK(fromGoal == (units == 5));
    checkGridPath(g, *path, false);
    if (units == 6) {
      CHECK_FALSE(checkSlideMovesBack(g.puz, g.pr(), *g.start, nestedSearch(0), 100000).checked);
    }
    SlideSearch one = nestedSearch(0);
    one.oneEnd = true;
    const std::optional<Separation> same = findSlidePath(g.puz, g.pr(), *g.start, one);
    REQUIRE(same.has_value());
    CHECK(same->getMoves() == 3);
  }
}

// The goal end drops a one-way arrangement that one of its own older levels
// holds, as the start end does; without that a search with no path would go
// round such arrangements and never end. It takes a goal where a piece with
// another already nested in it can close round a third: undoing that is two
// moves, so going back from the goal the arrangement before the closing
// comes up again two levels after it was first reached.
TEST_CASE("sliding: the goal end looks for its one-way moves in its own older levels",
          "[sliding][parallel]") {
  const BoardCells unit = {{0, 0}};
  const BoardCells c = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}};
  const BoardCells ring = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
  const GridBoard g = gridBoardOf(6, 6, {c, unit, unit, ring, unit},
                                  {{0, 0, 0, 0, false},    // the C, open to the right
                                   {1, 1, 1, 1, false},    // a unit in its pocket
                                   {5, 1, 5, 1, false},    // one the two can close round
                                   {0, 3, 0, 3, false},    // a closed ring
                                   {5, 4, 1, 4, false}});  // a unit to end inside it: no path
  // From the start alone the board is some 400,000 arrangements, more than
  // the searches here may hold: the goal end is the one that runs out. (And
  // a goal end going round would come to the same limit, not go on for ever.)
  const std::uint64_t limit = 100000;
  SlideSearch one = nestedSearch(0);
  one.oneEnd = true;
  one.maxMemoryStates = limit;
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, one).has_value());
  REQUIRE(one.outcome == SlideOutcome::Memory);

  std::uint64_t visited = 0;
  for (const unsigned int threads : {1U, 3U}) {
    INFO("threads " << threads);
    SlideSearch both = nestedSearch(0);
    both.threads = threads;
    both.maxMemoryStates = limit;
    unsigned long long reached = 0;
    unsigned long long fresh = 0;
    both.onLevel = [&](const std::string& line) {
      unsigned int level = 0;
      unsigned long long size = 0;
      unsigned long long r = 0;
      unsigned long long n = 0;
      if (std::sscanf(line.c_str(), "goal level %u: %llu arrangements, one-way %llu, %llu new",
                      &level, &size, &r, &n) == 4) {
        reached += r;
        fresh += n;
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, both).has_value());
    REQUIRE(both.outcome == SlideOutcome::NoPath);
    CHECK(fresh > 0);
    CHECK(fresh < reached);
    if (threads == 1) {
      visited = both.visited;
    } else {
      CHECK(both.visited == visited);
    }
  }
}

// Copies of a piece with a goal for each are one arrangement at the goal,
// whichever copy ends where: such a puzzle is searched from both ends, with
// nested slides and without.
TEST_CASE("sliding: copies of a piece with a goal each are searched from both ends",
          "[sliding]") {
  const GridBoard g = barAmongUnits();
  SlideSearch one;
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() > 3);
  checkGridPath(g, *path, false);

  SlideSearch both;
  both.meetFrom = 1;
  bool fromGoal = false;
  const std::optional<Separation> met = searched(g, both, fromGoal);
  REQUIRE(met.has_value());
  CHECK(fromGoal);
  CHECK(met->getMoves() == path->getMoves());
  checkGridPath(g, *met, false);

  // whole blocks side by side at the goal: the cells they are to cover fit
  // one of them at every shift between, and still hold them in one way
  const GridBoard blocks = blockBoard(3, 3, {0, 4, 7}, true);
  SlideSearch nested = nestedSearch(3);
  const std::optional<Separation> moved = searched(blocks, nested, fromGoal);
  REQUIRE(moved.has_value());
  CHECK(fromGoal);
  CHECK(moved->getMoves() == 4);
  checkGridPath(blocks, *moved);
}

// The parts of a move that visit no arrangement are told as they run
// (SlideSearch::phase): the merges of each level whole by the time the
// move is told (onDepth), the merge into the visited sets too, and nothing
// once the search has ended; whatever is told while it runs has no more
// done than its total.
TEST_CASE("sliding: a search with nested slides tells the parts of a move that visit nothing",
          "[sliding][parallel]") {
  const ScratchDir scratch("slidephase");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = settledBoard(37);
  SlideSearch s = nestedSearch(3);
  s.threads = 2;
  s.workDir = dir.string();
  s.autosave = true;
  s.autosaveFloorSeconds = 0;
  SlidePhaseProgress phase;
  s.phase = &phase;
  std::uint64_t levels = 0;
  std::uint64_t visitedWork = 0;
  bool whole = true;
  s.onDepth = [&](std::uint64_t depth) {
    if (depth == 0) {
      return;
    }
    levels++;
    // the level just made: its merges done
    whole = whole && phase.phase.load() == SlidePhase::Merging && phase.total.load() != 0 &&
            phase.done.load() == phase.total.load() &&
            phase.visitedDone.load() == phase.visitedTotal.load() && phase.since.load() != 0;
    visitedWork = std::max(visitedWork, phase.visitedTotal.load());
  };
  std::atomic<bool> done{false};
  std::atomic<bool> over{false};
  std::thread watcher([&] {
    while (!done.load()) {
      // One part's numbers: its done is read between two looks at the part,
      // when it began and its total, and counts only when those stayed the
      // same. (The loads are not one: a part that began between them set
      // the old part's done against its own total, which failed this case
      // now and then under load.)
      const SlidePhase part = phase.phase.load(std::memory_order_acquire);
      const std::int64_t since = phase.since.load();
      const bool visited = part == SlidePhase::Visited;
      const std::uint64_t t = (visited ? phase.visitedTotal : phase.total).load();
      const std::uint64_t d = (visited ? phase.visitedDone : phase.done).load();
      const bool same = phase.phase.load(std::memory_order_acquire) == part &&
                        phase.since.load() == since &&
                        (visited ? phase.visitedTotal : phase.total).load() == t;
      if (same && part != SlidePhase::None && d > t) {
        over.store(true);
      }
      std::this_thread::yield();
    }
  });
  bool fromGoal = false;
  const std::optional<Separation> path = searched(g, s, fromGoal);
  done.store(true);
  watcher.join();
  REQUIRE(path.has_value());
  CHECK(levels >= 10);
  CHECK(whole);
  // the levels went into the visited sets, counted
  CHECK(visitedWork > 0);
  CHECK_FALSE(over.load());
  CHECK(phase.phase.load() == SlidePhase::None);
  CHECK(phase.since.load() == 0);
  std::filesystem::remove_all(dir);
}

namespace {

// A W x H board with two cells to spare (searched without a grid, and
// without nested slides): a bar, top left, and a copy of a unit piece on
// every other cell but the last two of the bottom row. The bar is to go to
// the bottom right; with `goals` the units are to end over the cells they
// lie on now, whichever on which, so the goal is one arrangement.
GridBoard crowdedBoard(int W, int H, bool goals) {
  std::vector<BoardCells> shapes = {{{0, 0}, {1, 0}}};
  std::vector<GridBoard::Place> places = {{0, 0, W - 2, H - 1, false}};
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if ((y == 0 && x < 2) || (y == H - 1 && x >= W - 2)) {
        continue;
      }
      shapes.push_back({{0, 0}});
      places.push_back({x, y, goals ? x : -1, goals ? y : -1, false});
    }
  }
  return gridBoardOf(W, H, shapes, places, true);
}

// A search as `like`, its copies kept as `packing` says, in a key of
// `bits` or wider.
SlideSearch keyed(const SlideSearch& like, SlidePacking packing, unsigned int bits = 0) {
  SlideSearch s = like;
  s.packing = packing;
  s.minKeyBits = bits;
  return s;
}

// What two searches of a board that differ only in their keys came to: the
// same levels and the same fewest moves. False when one ran out of memory
// (a wider key, and the threads' own buffers, fill it a level sooner or
// later: then the levels both finished are the same).
bool sameSearch(const SlideSearch& a, const LevelSizes& aLevels,
                const std::optional<Separation>& aPath, const SlideSearch& b,
                const LevelSizes& bLevels, const std::optional<Separation>& bPath) {
  CHECK(a.looseStops == b.looseStops);
  if (a.outcome == SlideOutcome::Memory || b.outcome == SlideOutcome::Memory) {
    const std::size_t both = std::min(aLevels.lines.size(), bLevels.lines.size());
    CHECK(std::vector<std::string>(aLevels.lines.begin(),
                                   aLevels.lines.begin() + static_cast<std::ptrdiff_t>(both)) ==
          std::vector<std::string>(bLevels.lines.begin(),
                                   bLevels.lines.begin() + static_cast<std::ptrdiff_t>(both)));
    return false;
  }
  CHECK(a.outcome == b.outcome);
  CHECK(aLevels.lines == bLevels.lines);
  CHECK(aPath.has_value() == bPath.has_value());
  if (aPath && bPath) {
    CHECK(aPath->getMoves() == bPath->getMoves());
  }
  if (a.threads == 1 && b.threads == 1 &&
      (a.packedKeys == b.packedKeys || a.outcome == SlideOutcome::NoPath)) {
    // several threads end the last level where they happen to be, and so
    // do keys in another order, which packed keys are
    CHECK(a.visited == b.visited);
  }
  return true;
}

// What boards searched with a code for each copy and with packed keys came
// to.
struct PackedTally {
  int packedRuns = 0;
  int fromGoalRuns = 0;
  int found = 0;
};

// The board searched with a code for each copy and with the copies of a
// piece as one number, from both ends where it can be and from the start
// alone, on one thread and on three: the same levels and the same fewest
// moves, and the packed path is one of the board. On a grid the stops are
// apart: loose stops are never packed.
void comparePacking(const GridBoard& g, int grid, bool nested, bool partial, PackedTally& t) {
  for (int kind = 0; kind < 4; kind++) {
    INFO("kind " << kind);
    SlideSearch plain;
    plain.nested = nested;
    plain.partialNested = nested && partial;
    plain.grid = grid;
    plain.gridStopsApart = true;
    plain.oneEnd = kind % 2 == 1;
    plain.threads = kind >= 2 ? 3 : 1;
    plain.maxMemoryStates = 6000;
    if (!nested) {
      plain.meetFrom = 1;
    }
    SlideSearch packed = keyed(plain, SlidePacking::Always);
    LevelSizes plainLevels;
    LevelSizes packedLevels;
    plainLevels.watch(plain);
    packedLevels.watch(packed);
    const std::optional<Separation> a = findSlidePath(g.puz, g.pr(), *g.start, plain);
    const std::optional<Separation> c = findSlidePath(g.puz, g.pr(), *g.start, packed);
    CHECK_FALSE(plain.packedKeys);
    CHECK(plainLevels.fromGoal() == packedLevels.fromGoal());
    t.packedRuns += packed.packedKeys ? 1 : 0;
    t.fromGoalRuns += packed.packedKeys && packedLevels.fromGoal() ? 1 : 0;
    if (!sameSearch(plain, plainLevels, a, packed, packedLevels, c)) {
      continue;
    }
    if (c) {
      checkGridPath(g, *c, grid != 0);
      t.found++;
    }
  }
}

}  // namespace

// A key holds a code for each piece, in 64 bits or 128. A board that needs
// more keeps the copies of each piece as one number, which of the piece's
// places they hold (decision 0037), and so stays on the level search; with
// a code for each copy it takes the next key up, 192 bits. The same search
// in every key, and the search over text keys agrees.
TEST_CASE("sliding: a board too big for a code a piece keeps its copies as one number",
          "[sliding][parallel]") {
  // a bar with 25 places and 26 units with 30: 135 bits, or 58 with the
  // units as two numbers (16 of them, then 10)
  const GridBoard g = crowdedBoard(6, 5, false);
  SlideSearch packed;
  packed.threads = 1;
  LevelSizes packedLevels;
  packedLevels.watch(packed);
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, packed);
  REQUIRE(path.has_value());
  CHECK(packed.keyWidth == 128);
  CHECK(packed.keyBits == 58);
  CHECK(packed.packedKeys);
  CHECK_FALSE(packedLevels.lines.empty());
  CHECK_FALSE(packedLevels.fromGoal());
  CHECK(packed.memoryStates == memoryStates(3 * 16, false));
  checkGridPath(g, *path, false);

  SlideSearch each = keyed(packed, SlidePacking::Never);
  LevelSizes eachLevels;
  eachLevels.watch(each);
  const std::optional<Separation> same = findSlidePath(g.puz, g.pr(), *g.start, each);
  REQUIRE(same.has_value());
  CHECK(each.keyWidth == 192);
  CHECK(each.keyBits == 135);
  CHECK_FALSE(each.packedKeys);
  CHECK(each.memoryStates == memoryStates(3 * 24, false));
  CHECK(sameSearch(packed, packedLevels, path, each, eachLevels, same));
  checkGridPath(g, *same, false);

  // the search over text keys, which such a board went to before, tells
  // the copies apart: it agrees on a board small enough for it
  const GridBoard few = crowdedBoard(3, 3, false);
  SlideSearch fewPacked;
  fewPacked.packing = SlidePacking::Always;
  const std::optional<Separation> fewPath = findSlidePath(few.puz, few.pr(), *few.start, fewPacked);
  REQUIRE(fewPath.has_value());
  CHECK(fewPacked.packedKeys);
  SlideSearch old;
  old.legacy = true;
  LevelSizes none;
  none.watch(old);
  const std::optional<Separation> text = findSlidePath(few.puz, few.pr(), *few.start, old);
  REQUIRE(text.has_value());
  CHECK(text->getMoves() == fewPath->getMoves());
  CHECK(old.keyWidth == 0);
  CHECK(none.lines.empty());

  // the wider keys, packed, on several threads
  for (const unsigned int bits : {192U, 256U, 512U}) {
    INFO("bits " << bits);
    SlideSearch wide = keyed(packed, SlidePacking::Auto, bits);
    wide.threads = 3;
    LevelSizes wideLevels;
    wideLevels.watch(wide);
    const std::optional<Separation> far = findSlidePath(g.puz, g.pr(), *g.start, wide);
    REQUIRE(far.has_value());
    CHECK(wide.keyWidth == bits);
    CHECK(wide.keyBits == 58);
    CHECK(wide.packedKeys);
    CHECK(sameSearch(packed, packedLevels, path, wide, wideLevels, far));
    checkGridPath(g, *far, false);
  }

  // every unit with a goal: one arrangement, searched from both ends
  const GridBoard ends = crowdedBoard(6, 5, true);
  SlideSearch one;
  one.oneEnd = true;
  const std::optional<Separation> there = findSlidePath(ends.puz, ends.pr(), *ends.start, one);
  REQUIRE(there.has_value());
  CHECK(one.packedKeys);
  checkGridPath(ends, *there, false);
  for (const SlidePacking packing : {SlidePacking::Auto, SlidePacking::Never}) {
    SlideSearch both;
    both.packing = packing;
    both.meetFrom = 1;
    both.threads = 2;
    bool fromGoal = false;
    const std::optional<Separation> met = searched(ends, both, fromGoal);
    REQUIRE(met.has_value());
    CHECK(fromGoal);
    CHECK(both.packedKeys == (packing == SlidePacking::Auto));
    CHECK(both.keyWidth == (packing == SlidePacking::Auto ? 128U : 192U));
    CHECK(met->getMoves() == there->getMoves());
    checkGridPath(ends, *met, false);
  }
}

// Packed keys are the same search as a code for each copy, level for level:
// with and without nested slides, from one end and from both, on one thread
// and on several, and each path is one the pieces can make.
TEST_CASE("sliding: copies of a piece kept as one number give the same search",
          "[sliding][parallel]") {

  // without nested slides: Klotski's four upright pieces and four units
  for (const unsigned int threads : {1U, 3U}) {
    INFO("klotski, threads " << threads);
    const SlideFixture f = slideFixture(4, 5, kKlotski);
    SlideSearch plain;
    plain.threads = threads;
    SlideSearch packed = keyed(plain, SlidePacking::Always);
    LevelSizes plainLevels;
    LevelSizes packedLevels;
    plainLevels.watch(plain);
    packedLevels.watch(packed);
    const std::optional<Separation> a = findSlidePath(f.puz, f.pr(), *f.start, plain);
    const std::optional<Separation> b = findSlidePath(f.puz, f.pr(), *f.start, packed);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(b->getMoves() == 81);
    CHECK_FALSE(plain.packedKeys);
    CHECK(packed.packedKeys);
    CHECK(packed.keyWidth == 64);
    CHECK(packed.keyBits < plain.keyBits);
    CHECK(sameSearch(plain, plainLevels, a, packed, packedLevels, b));
    CHECK(oneMoverPerStep(*b));
    for (unsigned int step = 0; step < b->getMoves(); step++) {
      CHECK(slideRoute(f.puz, f.pr(), *b, step, nullptr).size() >= 2);
    }
    for (std::size_t i = 0; i < kKlotski.size(); i++) {
      const auto pc = static_cast<unsigned int>(i);
      CHECK(b->getState(0).getX(pc) == kKlotski[i].x);
      CHECK(b->getState(0).getY(pc) == kKlotski[i].y);
    }
    CHECK(b->getState(81).getX(0) == 1);
    CHECK(b->getState(81).getY(0) == 3);
  }

  // boards with nested slides, on a grid of 3 (the stops apart: loose stops
  // are never packed) and with every position a stop
  struct Board {
    GridBoard g;
    int grid;
    bool nested;
  };
  std::vector<Board> boards;
  boards.push_back({cupsAndUnits(true), 3, true});
  boards.push_back({cupsAndUnits(false, 2), 3, true});
  boards.push_back({blockBoard(3, 2, {1, 3}), 3, true});
  boards.push_back({blockBoard(3, 2, {1, 3}, true), 3, true});
  boards.push_back({blockBoard(3, 3, {0, 4, 7}, true), 3, true});
  boards.push_back({swappedBlocks(), 3, true});
  boards.push_back({fullCup(2), 0, true});
  boards.push_back({fullCup(4), 0, true});
  boards.push_back({barAmongUnits(), 0, false});
  boards.push_back({crowdedBoard(4, 4, true), 0, false});
  for (std::uint64_t seed = 0; seed < 12; seed++) {
    boards.push_back({gridBoard(seed), 3, true});
  }
  PackedTally t;
  for (std::size_t b = 0; b < boards.size(); b++) {
    INFO("board " << b);
    comparePacking(boards[b].g, boards[b].grid, boards[b].nested, b % 5 == 4, t);
  }
  // the boards are worth the comparison
  CHECK(t.packedRuns > 30);
  CHECK(t.fromGoalRuns > 5);
  CHECK(t.found > 40);
}

// Hidden: many more boards searched with a code for each copy and packed.
TEST_CASE("sliding: fuzz, copies of a piece kept as one number give the same search",
          "[.fuzz][sliding]") {
  PackedTally t;
  for (std::uint64_t seed = 1000; seed < 1600; seed++) {
    INFO("seed " << seed);
    const GridBoard g = gridBoard(seed);
    comparePacking(g, 3, true, seed % 4 == 0, t);
    // its goal made one arrangement, for the search from both ends
    SlideSearch one = nestedSearch(3);
    one.oneEnd = true;
    one.maxMemoryStates = 30000;
    const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
    if (path && path->getMoves() >= 2) {
      // with its whole blocks the copies of one piece and its units of
      // another, each copy with a goal
      const GridBoard settled = endAsGoal(g, *path);
      comparePacking(gridBoardOf(settled.W, settled.H, settled.shapes, settled.places, true), 3,
                     true, false, t);
    }
  }
  WARN("packed fuzz: " << t.packedRuns << " packed runs, " << t.fromGoalRuns
                       << " from both ends, " << t.found << " paths checked");
  CHECK(t.packedRuns > 500);
  CHECK(t.fromGoalRuns > 100);
}

// The keys wider than 128 bits give the same search as the narrow ones,
// with a code for each copy and packed, with loose stops and without.
TEST_CASE("sliding: the wider keys give the same search", "[sliding][parallel]") {
  std::vector<GridBoard> boards;
  boards.push_back(cupsAndUnits(true));
  boards.push_back(blockBoard(3, 2, {1, 3}, true));
  boards.push_back(fullCup(3));
  for (std::uint64_t seed = 0; seed < 5; seed++) {
    boards.push_back(gridBoard(seed));
  }
  int loose = 0;
  int found = 0;
  for (std::size_t b = 0; b < boards.size(); b++) {
    const GridBoard& g = boards[b];
    // nested slides with loose stops, with the stops apart on several
    // threads, and the grid without nested slides
    for (int kind = 0; kind < 3; kind++) {
      SlideSearch narrow;
      narrow.nested = kind < 2;
      narrow.grid = b == 2 ? 0 : 3;
      narrow.gridStopsApart = kind == 1;
      narrow.threads = kind == 1 ? 3 : 1;
      narrow.maxMemoryStates = 6000;
      LevelSizes narrowLevels;
      narrowLevels.watch(narrow);
      const std::optional<Separation> a = findSlidePath(g.puz, g.pr(), *g.start, narrow);
      CHECK(narrow.keyWidth == 64);
      loose += narrow.looseStops ? 1 : 0;
      const std::pair<unsigned int, SlidePacking> keys[] = {
          {128, SlidePacking::Always},
          {192, SlidePacking::Never},
          {256, SlidePacking::Auto},
          {512, SlidePacking::Never},
      };
      for (const auto& [bits, packing] : keys) {
        INFO("board " << b << " kind " << kind << " bits " << bits);
        SlideSearch wide = keyed(narrow, packing, bits);
        LevelSizes wideLevels;
        wideLevels.watch(wide);
        const std::optional<Separation> c = findSlidePath(g.puz, g.pr(), *g.start, wide);
        CHECK(wide.keyWidth == bits);
        CHECK(wide.keyBits <= narrow.keyBits);
        if (packing == SlidePacking::Never || wide.looseStops) {
          CHECK_FALSE(wide.packedKeys);
          CHECK(wide.keyBits == narrow.keyBits);
        }
        if (sameSearch(narrow, narrowLevels, a, wide, wideLevels, c) && c) {
          checkGridPath(g, *c, narrow.grid != 0);
          found++;
        }
      }
    }
  }
  CHECK(loose > 4);
  CHECK(found > 20);

  // Klotski in the widest key, a code for each piece
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch widest;
  widest.minKeyBits = 512;
  widest.packing = SlidePacking::Never;
  widest.threads = 2;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, widest);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() == 81);
  CHECK(widest.keyWidth == 512);
  CHECK(oneMoverPerStep(*path));
}

// The moves made backwards (decision 0033) are the moves with packed keys
// too, where a key does not say which copy a group carries.
TEST_CASE("sliding: the moves made backwards are the moves with packed keys", "[sliding]") {
  int checked = 0;
  std::uint64_t oneWay = 0;
  const auto both = [&](const Puzzle& puz, const Problem& pr, const Assembly& start, int grid,
                        std::uint64_t limit) {
    const SlideBackCheck plain = checkSlideMovesBack(puz, pr, start, nestedSearch(grid), limit);
    const SlideBackCheck packed = checkSlideMovesBack(
        puz, pr, start, keyed(nestedSearch(grid), SlidePacking::Always), limit);
    CHECK(packed.checked == plain.checked);
    if (!packed.checked) {
      return;
    }
    checked++;
    oneWay += packed.oneWay;
    CHECK(packed.arrangements == plain.arrangements);
    CHECK(packed.moves == plain.moves);
    CHECK(packed.oneWay == plain.oneWay);
    CHECK(packed.missing == 0);
    CHECK(packed.extra == 0);
    CHECK(packed.wronglyTwoWay == 0);
  };
  const OneWay w = oneWayPuzzle();
  both(w.puz, w.pr(), *w.start, 0, 100000);
  for (const int units : {2, 3, 4}) {
    const GridBoard cup = fullCup(units);
    both(cup.puz, cup.pr(), *cup.start, 0, 100000);
  }
  const GridBoard blocks = blockBoard(3, 2, {1, 3}, true);
  both(blocks.puz, blocks.pr(), *blocks.start, 3, 100000);
  const GridBoard cups = cupsAndUnits(false, 2);
  both(cups.puz, cups.pr(), *cups.start, 3, 100000);
  // (a cup that could hold too many pieces has no moves backwards)
  CHECK(checked >= 4);
  for (std::uint64_t seed = 0; seed < 30; seed++) {
    INFO("seed " << seed);
    const GridBoard g = gridBoard(seed);
    both(g.puz, g.pr(), *g.start, 3, 20000);
  }
  CHECK(checked > 15);
  CHECK(oneWay > 0);
}

// Where levels were thinned out the path is traced by a search out from
// the arrangement found, as many moves as the gap is wide: with a wide gap
// as long as the search was. An abort ends it (the outcome is the stop's,
// not a failure's, and nothing is saved); a pause waits for the path.
TEST_CASE("sliding: an abort ends the trace across thinned levels and a pause waits for it",
          "[sliding][parallel]") {
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  // only the start is kept: the trace searches the whole way out again
  auto thinned = [] {
    SlideSearch s;
    s.keepEvery = 100;
    s.threads = 2;
    return s;
  };
  // the levels the whole trace makes
  std::atomic<std::uint64_t> whole{0};
  SlideSearch straight = thinned();
  straight.traced = &whole;
  REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, straight).has_value());
  REQUIRE(whole.load() >= 70);
  for (const StopKind kind : {StopKind::Abort, StopKind::Pause}) {
    INFO((kind == StopKind::Abort ? "abort" : "pause"));
    const StopSource stop;
    SlideSearch search = thinned();
    search.stop = stop.token();
    std::atomic<std::uint64_t> traced{0};
    search.traced = &traced;
    // asked for as the trace begins its search across the gap
    int gaps = 0;
    search.onLog = [&](LogLevel, const std::string& line) {
      if (line.find("The path is traced across") != std::string::npos) {
        gaps++;
        stop.request(kind);
      }
    };
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    CHECK(gaps == 1);
    if (kind == StopKind::Abort) {
      CHECK_FALSE(path.has_value());
      CHECK(search.outcome == SlideOutcome::Stopped);
      CHECK_FALSE(search.saved);
      CHECK(search.error.empty());
      // ended at once: none of the levels the whole trace makes
      CHECK(traced.load() == 0);
    } else {
      REQUIRE(path.has_value());
      CHECK(search.outcome == SlideOutcome::Found);
      CHECK(path->getMoves() == 81);
      CHECK(oneMoverPerStep(*path));
      CHECK(traced.load() == whole.load());
    }
  }

  // the two ends met, and levels thinned: an abort ends that trace too
  const SlideFixture e = eightPuzzle();
  const StopSource stop;
  SlideSearch both;
  both.keepEvery = 3;
  both.meetFrom = 1;
  both.stop = stop.token();
  int gaps = 0;
  both.onLog = [&](LogLevel, const std::string& line) {
    if (line.find("The path is traced across") != std::string::npos) {
      gaps++;
      stop.request(StopKind::Abort);
    }
  };
  CHECK_FALSE(findSlidePath(e.puz, e.pr(), *e.start, both).has_value());
  CHECK(gaps == 1);
  CHECK(both.outcome == SlideOutcome::Stopped);
  CHECK_FALSE(both.saved);
  CHECK(both.error.empty());
}
