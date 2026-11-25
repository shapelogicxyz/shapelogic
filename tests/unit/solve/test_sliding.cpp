// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles: the tray model, the slide search, routes and the sliding
// solve. Ported from the original's test/test_sliding.cpp, every case in its
// order (the solve thread's cases run a SolveSession; its environment
// switches are the search's options; its scratch folders are this
// process's own), followed by new cases in test_sliding_session.cpp.
#include "../model/model_test_helpers.h"
#include "file_size_limit.h"
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
#include <optional>
#include <random>
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

namespace {

constexpr int kFilled = Voxel::kFilled;
constexpr int kVariable = Voxel::kVariable;
constexpr int kEmpty = Voxel::kEmpty;

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

// A scratch folder of this process's own, removed again (never a fixed
// name, never the user's cache).
std::filesystem::path scratchDir(const std::string& what) {
  std::random_device rd;
  const std::filesystem::path p =
      std::filesystem::temp_directory_path() /
      ("bt_test_" + what + "_" + std::to_string(rd()) + "_" + std::to_string(rd()));
  std::filesystem::remove_all(p);
  return p;
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
  REQUIRE(assm->createMatrix(mo) == AssemblerError::None);

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
  REQUIRE(assm->createMatrix(mo) == AssemblerError::None);

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
    const unsigned int n = pr.getSavedSolution(i).getSolutionNumber();
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

namespace {

// A sliding puzzle built from rectangles: each piece is its own shape, w x h,
// with its top-left cell at (x, y); goal says where it must end, or is
// negative when it may end anywhere.
struct SlideBlock {
  unsigned int w;
  unsigned int h;
  int x;
  int y;
  int goalX;
  int goalY;
};

struct SlideFixture {
  Puzzle puz{GridType(GridKind::Sliding)};
  std::unique_ptr<Assembly> start;
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
};

SlideFixture slideFixture(unsigned int trayW, unsigned int trayH,
                          const std::vector<SlideBlock>& blocks) {
  SlideFixture f;
  const unsigned int trayId = addStartGoalShape(f.puz, trayW, trayH);
  f.start = std::make_unique<Assembly>(f.puz.getGridType());
  for (const SlideBlock& b : blocks) {
    const unsigned int id = f.puz.addShape(b.w, b.h, 1);
    for (unsigned int y = 0; y < b.h; y++) {
      for (unsigned int x = 0; x < b.w; x++) {
        f.puz.getShape(id).setState(x, y, 0, kFilled);
        Voxel& tray = f.puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(b.x) + x, static_cast<unsigned int>(b.y) + y, 0,
                      id + 1);
        if (b.goalX >= 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(b.goalX) + x,
                                          static_cast<unsigned int>(b.goalY) + y, 0),
                            id + 1);
        }
      }
    }
    f.start->addPlacement(0, b.x, b.y, 0);
  }
  syncSlidingProblems(f.puz);
  return f;
}

// Every step of a slide path moves exactly one piece.
bool oneMoverPerStep(const Separation& path) {
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    unsigned int moved = 0;
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      if (path.getState(step).getX(i) != path.getState(step + 1).getX(i) ||
          path.getState(step).getY(i) != path.getState(step + 1).getY(i)) {
        moved++;
      }
    }
    if (moved != 1) {
      return false;
    }
  }
  return true;
}

// Klotski, the classic "Heng Dao Li Ma" start: a 4x5 tray, the 2x2 block to
// bring to the bottom center past four upright 1x2, one flat 2x1 and four
// unit pieces.
const std::vector<SlideBlock> kKlotski = {
    {2, 2, 1, 0, 1, 3},   {1, 2, 0, 0, -1, -1}, {1, 2, 3, 0, -1, -1}, {1, 2, 0, 2, -1, -1},
    {1, 2, 3, 2, -1, -1}, {2, 1, 1, 2, -1, -1}, {1, 1, 1, 3, -1, -1}, {1, 1, 2, 3, -1, -1},
    {1, 1, 0, 4, -1, -1}, {1, 1, 3, 4, -1, -1},
};

}  // namespace

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

#ifdef __SIZEOF_INT128__
namespace {
__extension__ using Wide = unsigned __int128;
}  // namespace

TEST_CASE("sliding: a key set finds what was put in it, on disk and in memory", "[sliding]") {
  const std::filesystem::path dir = scratchDir("keyset");
  std::filesystem::create_directories(dir);
  for (int onDisk = 0; onDisk < 2; onDisk++) {
    // keys spread over all 128 bits, a few close together, sorted
    std::vector<Wide> keys;
    std::uint64_t x = 88172645463325252ULL;
    auto rnd = [&x]() {
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      return x;
    };
    for (int i = 0; i < 5000; i++) {
      Wide k = (static_cast<Wide>(rnd()) << 64) | rnd();
      if (i % 3 == 0) {
        k = keys.empty() ? 0 : keys.back() + 1 + rnd() % 300;
      }
      keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    levels::KeySet<Wide> set;
    REQUIRE(set.create(onDisk != 0 ? dir / "keys" : std::filesystem::path()));
    std::vector<Wide> in;
    std::vector<Wide> out;
    for (std::size_t i = 0; i < keys.size(); i++) {
      (i % 2 != 0 ? out : in).push_back(keys[i]);
    }
    REQUIRE(set.addAll(in.begin(), in.end()));
    REQUIRE(set.finish());
    CHECK(set.size() == in.size());

    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == in);

    // every key looked up at once: those put in are found, the others not
    std::vector<char> found(keys.size(), 0);
    levels::KeySet<Wide>::Reader reader(set);
    REQUIRE(set.find(keys.data(), keys.size(), found.data(), reader));
    for (std::size_t i = 0; i < keys.size(); i++) {
      CHECK((found[i] != 0) == (i % 2 == 0));
    }
  }
  std::filesystem::remove_all(dir);
}
#endif

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
  const std::filesystem::path dir = scratchDir("slidedir");

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
  const std::filesystem::path dir = scratchDir("slideshare");

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

// The Start/Goal tab places a whole piece at a time, and says the shape is
// valid once a piece has both a start and a goal, each the whole piece.
TEST_CASE("sliding: a whole piece is placed as a start or a goal", "[sliding]") {
  // a 5x3 tray, an L of three cells and a unit piece, no stamps yet
  SlideFixture f = slideFixture(5, 3, {});
  REQUIRE(isStartGoalShape(&f.puz.getShape(0)));
  const unsigned int L = f.puz.addShape(2, 2, 1);
  f.puz.getShape(L).setState(0, 0, 0, kFilled);
  f.puz.getShape(L).setState(1, 0, 0, kFilled);
  f.puz.getShape(L).setState(0, 1, 0, kFilled);
  const unsigned int U = f.puz.addShape(1, 1, 1);
  f.puz.getShape(U).setState(0, 0, 0, kFilled);
  Voxel* tray = &f.puz.getShape(0);
  // the starts and goals are the problem's on the tray
  SlideMarks* m = &f.puz.getProblem(0).editSlideMarks();
  const Voxel* l = &f.puz.getShape(L);
  const Voxel* u = &f.puz.getShape(U);
  const auto none = static_cast<unsigned int>(-1);

  CHECK(!startGoalError(f.puz, 0).empty());

  // the L's top-left cell goes where it is put: the cell above its corner
  CHECK(stampPiece(tray, m, l, L, 1, 1, 0, false).empty());
  CHECK(stampAt(tray, m, 1, 0, 0, false) == L);
  CHECK(stampAt(tray, m, 2, 0, 0, false) == L);
  CHECK(stampAt(tray, m, 1, 1, 0, false) == L);
  CHECK(stampAt(tray, m, 2, 1, 0, false) == none);
  // a start alone is not enough
  CHECK(!startGoalError(f.puz, 0).empty());

  // It is exactly there; one cell along it is not, and placing it there
  // moves it, over cells it covered itself.
  CHECK(stampIsAt(tray, m, l, L, 1, 1, 0, false));
  CHECK(!stampIsAt(tray, m, l, L, 2, 1, 0, false));
  CHECK(stampFits(tray, m, l, L, 2, 1, 0, false).empty());
  CHECK(stampPiece(tray, m, l, L, 2, 1, 0, false).empty());
  CHECK(stampAt(tray, m, 1, 0, 0, false) == none);
  CHECK(stampAt(tray, m, 3, 0, 0, false) == L);

  // placing it again moves it
  CHECK(stampPiece(tray, m, l, L, 0, 2, 0, false).empty());
  CHECK(stampAt(tray, m, 1, 0, 0, false) == none);
  CHECK(stampAt(tray, m, 0, 2, 0, false) == L);

  // off the tray, or onto another piece's start: nothing changes
  CHECK(!stampPiece(tray, m, l, L, 4, 0, 0, false).empty());
  CHECK(stampPiece(tray, m, u, U, 4, 1, 0, false).empty());
  CHECK(!stampPiece(tray, m, l, L, 3, 2, 0, false).empty());
  CHECK(stampAt(tray, m, 0, 1, 0, false) == L);

  // a goal on a variable cell is refused: no piece may stop there
  tray->setState(4, 0, 0, kVariable);
  CHECK(!stampPiece(tray, m, u, U, 4, 0, 0, true).empty());

  // a goal without a start (the piece starts anywhere) is not enough, a
  // piece at both is: valid
  CHECK(clearStamp(m, U, false));
  CHECK(stampPiece(tray, m, u, U, 3, 0, 0, true).empty());
  CHECK(startGoalError(f.puz, 0).find("at its Start and at its Goal") != std::string::npos);
  CHECK(stampPiece(tray, m, u, U, 4, 2, 0, false).empty());
  CHECK(startGoalError(f.puz, 0).empty());
  // the L's goal without a start stays valid: it starts anywhere
  CHECK(stampPiece(tray, m, l, L, 1, 1, 0, true).empty());
  CHECK(startGoalError(f.puz, 0).empty());
  CHECK(clearStamp(m, L, true));

  // a start that is only part of the piece is not valid
  m->set(tray->getIndex(1, 1, 0), false, 0);
  CHECK(startGoalError(f.puz, 0).find("not the whole piece") != std::string::npos);
}

// A piece of several copies has a start and a goal for each: a stamp is one
// more copy's, beside the others, until every copy has one; the marks split
// into whole copies, a right click takes one off, and the shape is valid
// once some piece has every copy at a start and at a goal.
TEST_CASE("sliding: copies of a piece are placed one by one", "[sliding]") {
  SlideFixture f = slideFixture(5, 3, {});
  const unsigned int D = f.puz.addShape(2, 1, 1);
  f.puz.getShape(D).setState(0, 0, 0, kFilled);
  f.puz.getShape(D).setState(1, 0, 0, kFilled);
  Voxel* tray = &f.puz.getShape(0);
  SlideMarks* m = &f.puz.getProblem(0).editSlideMarks();
  const Voxel* d = &f.puz.getShape(D);
  const auto none = static_cast<unsigned int>(-1);

  // three copies: side by side, never over one another
  CHECK(stampPiece(tray, m, d, D, 0, 0, 0, false, 3).empty());
  CHECK(stampPiece(tray, m, d, D, 2, 0, 0, false, 3).empty());
  CHECK(stampPiece(tray, m, d, D, 1, 0, 0, false, 3).find("another copy") != std::string::npos);
  CHECK(stampPiece(tray, m, d, D, 0, 2, 0, false, 3).empty());
  CHECK(stampFits(tray, m, d, D, 2, 2, 0, false, 3).find("All 3 copies") != std::string::npos);
  auto copies = stampCopies(*tray, *m, *d, D, false);
  REQUIRE(copies);
  CHECK(copies->size() == 3);
  CHECK(stampIsAt(tray, m, d, D, 2, 0, 0, false));
  CHECK_FALSE(stampIsAt(tray, m, d, D, 1, 0, 0, false));

  // the right click takes off the copy under it alone
  CHECK(clearCopyAt(tray, m, d, D, 3, 0, 0, false));
  CHECK(stampAt(tray, m, 2, 0, 0, false) == none);
  CHECK(stampAt(tray, m, 1, 0, 0, false) == D);
  CHECK(stampCopies(*tray, *m, *d, D, false)->size() == 2);

  // the problem has the copies; a goal for one, then for every copy
  syncSlidingProblems(f.puz);
  Problem& pr = f.puz.getProblem(0);
  CHECK(pr.getShapeMaximum(D) == 2);
  pr.setShapeMaximum(f.puz, D, 3);
  pr.setShapeMinimum(f.puz, D, 3);
  CHECK(stampedCopies(f.puz, pr, D) == 2);
  CHECK(stampPiece(tray, m, d, D, 3, 2, 0, true, 3).empty());
  CHECK(startGoalError(f.puz, 0).find("every copy") != std::string::npos);
  CHECK(stampPiece(tray, m, d, D, 2, 0, 0, false, 3).empty());
  CHECK(stampPiece(tray, m, d, D, 3, 1, 0, true, 3).empty());
  CHECK(stampPiece(tray, m, d, D, 3, 0, 0, true, 3).empty());
  CHECK(startGoalError(f.puz, 0).empty());
  // the copies without a start start anywhere: the part may go on unmarked
  // cells (a part with a start for every copy may not)
  const auto partOf = [&pr](unsigned int shape) {
    for (unsigned int part = 0; part < pr.getNumberOfParts(); part++) {
      if (pr.getShapeIdOfPart(part) == shape) {
        return part;
      }
    }
    return pr.getNumberOfParts();
  };
  CHECK_FALSE(freeStartParts(f.puz, pr)[partOf(D)]);
  CHECK(clearCopyAt(tray, m, d, D, 0, 2, 0, false));
  CHECK(freeStartParts(f.puz, pr)[partOf(D)]);

  // marks that are not whole copies are not valid
  m->set(tray->getIndex(4, 2, 0), false, D + 1);
  CHECK_FALSE(stampCopies(*tray, *m, *d, D, false));
  CHECK(startGoalError(f.puz, 0).find("not the whole piece") != std::string::npos);
}

// On a tray of layers a stamp marks every cell of its piece, in every
// layer. A click on a layer puts the piece's slice on that layer under the
// mouse, top-left cell first, at the lowest height that fits.
TEST_CASE("sliding: a stamp on a tray of layers marks the whole piece", "[sliding]") {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 3, 2, 3);
  REQUIRE(puz.getShape(trayId).getZ() == 3);
  REQUIRE(floorCells(puz.getShape(trayId)) == 18);
  // a post two layers high with a foot to the right at the bottom
  const unsigned int post = puz.addShape(2, 1, 2);
  puz.getShape(post).setState(0, 0, 0, kFilled);
  puz.getShape(post).setState(1, 0, 0, kFilled);
  puz.getShape(post).setState(0, 0, 1, kFilled);
  syncSlidingProblems(puz);
  Voxel* tray = &puz.getShape(trayId);
  SlideMarks* m = &puz.getProblem(0).editSlideMarks();
  const Voxel* p = &puz.getShape(post);
  using Cells = std::vector<TrayCell>;

  // on the bottom layer: the foot's slice, the post standing on the floor
  CHECK(stampCells(tray, m, p, post, 1, 1, 0, false) == Cells{{1, 1, 0}, {2, 1, 0}, {1, 1, 1}});
  // on the middle layer: as low as it goes, its top under the mouse
  CHECK(stampCells(tray, m, p, post, 1, 1, 1, false) == Cells{{1, 1, 0}, {2, 1, 0}, {1, 1, 1}});
  // the foot sticks out on the right at either height: the lowest is shown
  CHECK(stampCells(tray, m, p, post, 2, 1, 1, false) == Cells{{2, 1, 0}, {3, 1, 0}, {2, 1, 1}});
  CHECK(!stampFits(tray, m, p, post, 2, 1, 1, false).empty());
  // on the top layer: the post's top there, so its foot on the middle layer
  REQUIRE(stampPiece(tray, m, p, post, 0, 0, 2, false).empty());
  CHECK(stampAt(tray, m, 0, 0, 1, false) == post);
  CHECK(stampAt(tray, m, 1, 0, 1, false) == post);
  CHECK(stampAt(tray, m, 0, 0, 2, false) == post);
  CHECK(stampAt(tray, m, 0, 0, 0, false) == static_cast<unsigned int>(-1));
  CHECK(stampIsAt(tray, m, p, post, 0, 0, 2, false));
  CHECK(stampIsAt(tray, m, p, post, 0, 0, 1, false) == false);
  // a piece no taller than the tray goes in from every layer
  REQUIRE(stampPiece(tray, m, p, post, 1, 1, 0, true).empty());
  CHECK(stampIsAt(tray, m, p, post, 1, 1, 1, true));
  CHECK(startGoalError(puz, trayId).empty());

  // a start that misses a layer of the piece is not the whole piece
  m->set(tray->getIndex(0, 0, 2), false, 0);
  CHECK(startGoalError(puz, trayId).find("not the whole piece") != std::string::npos);
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

// A piece whose shape changes loses its start and goal; one that is only
// moved in its grid keeps them.
TEST_CASE("sliding: a piece that changes shape loses its start and goal", "[sliding]") {
  SlideFixture f = slideFixture(4, 3, {});
  const unsigned int bar = f.puz.addShape(3, 2, 1);
  Voxel& v = f.puz.getShape(bar);
  v.setState(0, 0, 0, kFilled);
  v.setState(1, 0, 0, kFilled);
  REQUIRE(stampPiece(&f.puz.getShape(0), &f.puz.getProblem(0).editSlideMarks(), &v, bar, 0, 0, 0, false).empty());
  REQUIRE(stampPiece(&f.puz.getShape(0), &f.puz.getProblem(0).editSlideMarks(), &v, bar, 2, 2, 0, true).empty());

  // moved one row up in its own grid: still the same shape
  v.setState(0, 0, 0, kEmpty);
  v.setState(1, 0, 0, kEmpty);
  v.setState(0, 1, 0, kFilled);
  v.setState(1, 1, 0, kFilled);
  CHECK(dropChangedStamps(f.puz, bar).empty());
  CHECK(stampAt(&f.puz.getShape(0), &f.puz.getProblem(0).editSlideMarks(), 0, 0, 0, false) == bar);

  // a cell longer: both go
  f.puz.getShape(bar).setState(2, 1, 0, kFilled);
  CHECK(dropChangedStamps(f.puz, bar) == std::vector<unsigned int>{0});
  CHECK(stampAt(&f.puz.getShape(0), &f.puz.getProblem(0).editSlideMarks(), 0, 0, 0, false) == static_cast<unsigned int>(-1));
  CHECK(stampAt(&f.puz.getShape(0), &f.puz.getProblem(0).editSlideMarks(), 2, 2, 0, true) == static_cast<unsigned int>(-1));
}

// ---- the streamed level search: saved searches, both ends, thinned levels

namespace {

// The 8-puzzle: a 3x3 tray of eight unit pieces, each with its own goal,
// so the goal is one arrangement. The start is the goal with the pieces
// slid round the hole eight times.
SlideFixture eightPuzzle() {
  const int gx[8] = {0, 1, 2, 0, 1, 2, 0, 1};
  const int gy[8] = {0, 0, 0, 1, 1, 1, 2, 2};
  const int sx[8] = {0, 0, 1, 1, 1, 2, 0, 2};
  const int sy[8] = {1, 0, 0, 1, 2, 0, 2, 1};
  std::vector<SlideBlock> blocks;
  for (int i = 0; i < 8; i++) {
    blocks.push_back({1, 1, sx[i], sy[i], gx[i], gy[i]});
  }
  return slideFixture(3, 3, blocks);
}

// The places of every piece along a path, for comparing paths.
std::vector<int> placesAlong(const Separation& path) {
  std::vector<int> places;
  for (unsigned int s = 0; s <= path.getMoves(); s++) {
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      places.push_back(path.getState(s).getX(i));
      places.push_back(path.getState(s).getY(i));
    }
  }
  return places;
}

}  // namespace

// A search stopped by its token is saved in its folder, and a search of the
// same start carries it on to the same path, counting what was searched
// once; without `resume` the saved search is thrown away.
TEST_CASE("sliding: a stopped search is saved and carried on", "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slideresume");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  straight.threads = 2;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // stopped once it has visited a few thousand arrangements
  StopSource stop;
  std::atomic<std::uint64_t> progress{0};
  SlideSearch first;
  first.threads = 2;
  first.workDir = dir.string();
  first.progress = &progress;
  first.stop = stop.token();
  std::atomic<bool> done{false};
  std::thread watcher([&] {
    while (!done.load()) {
      if (progress.load() >= 4000) {
        stop.request(StopKind::Pause);
        return;
      }
      std::this_thread::yield();
    }
  });
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  done.store(true);
  watcher.join();
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  CHECK(first.saved);
  CHECK(first.visited >= 4000);
  CHECK(first.visited < straight.visited);
  // the progress was told what the search kept (a level cut short is not)
  CHECK(progress.load() == first.visited);
  CHECK(std::filesystem::is_directory(dir));
  CHECK_FALSE(std::filesystem::is_empty(dir));

  SlideSearch second;
  second.threads = 3;
  second.workDir = dir.string();
  // carried on, it adds only what it visits now
  second.progress = &progress;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK_FALSE(second.saved);
  CHECK(second.outcome == SlideOutcome::Found);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(progress.load() == straight.visited);
  // the folder goes with the search
  CHECK(std::filesystem::is_empty(dir));

  // saved again, and thrown away by a search that does not resume
  StopSource again;
  again.request(StopKind::Pause);
  SlideSearch third;
  third.workDir = dir.string();
  third.stop = again.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, third).has_value());
  CHECK(third.outcome == SlideOutcome::Stopped);
  CHECK(third.saved);
  SlideSearch fresh;
  fresh.workDir = dir.string();
  fresh.resume = false;
  REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, fresh).has_value());
  CHECK_FALSE(fresh.resumed);
  CHECK(fresh.visited == straight.visited);

  // discardSaved throws the problem's saved searches away
  StopSource once;
  once.request(StopKind::Pause);
  SlideSearch other;
  other.workDir = dir.string();
  other.stop = once.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, other).has_value());
  CHECK(other.saved);
  CHECK(std::filesystem::exists(dir));
  // savedFolders names its folder (what the app's cache manager links to the puzzle)
  const std::vector<std::string> folders = savedFolders(f.puz, f.pr(), dir.string());
  REQUIRE(folders.size() == 1);
  CHECK(std::filesystem::path(folders[0]).parent_path() == dir);
  CHECK(std::filesystem::path(folders[0]).filename().string().starts_with("slide-"));
  CHECK(savedFolders(f.puz, f.pr(), "").empty());
  discardSaved(f.puz, f.pr(), dir.string());
  CHECK(savedFolders(f.puz, f.pr(), dir.string()).empty());
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A paused search tells what it writes as it saves itself (its levels
// still in memory); an aborted one writes nothing, and its folder goes.
TEST_CASE("sliding: a paused search tells its save, an aborted one saves nothing",
          "[sliding][resume]") {
  const std::filesystem::path dir = scratchDir("slidestop");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  StopSource pause;
  pause.request(StopKind::Pause);
  SlideStopProgress told;
  SlideSearch paused;
  paused.workDir = dir.string();
  paused.stop = pause.token();
  paused.stopProgress = &told;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, paused).has_value());
  REQUIRE(paused.outcome == SlideOutcome::Stopped);
  CHECK(paused.saved);
  CHECK(told.saving.load());
  CHECK(told.totalBytes.load() > 0);
  CHECK(told.doneBytes.load() == told.totalBytes.load());
  CHECK_FALSE(std::filesystem::is_empty(dir));

  // an abort carries nothing on: the saved search is not written again, and goes
  StopSource abort;
  abort.request(StopKind::Abort);
  SlideStopProgress none;
  SlideSearch aborted;
  aborted.workDir = dir.string();
  aborted.stop = abort.token();
  aborted.stopProgress = &none;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, aborted).has_value());
  CHECK(aborted.outcome == SlideOutcome::Stopped);
  CHECK_FALSE(aborted.saved);
  CHECK_FALSE(none.saving.load());
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A save point stops the level search between two levels: the level under
// way is finished, nothing is searched twice when the search is carried
// on, and a run that is asked at once still gets a level further, so a
// search saved after every level reaches its end all the same (a stop
// cuts the level short, and a stop a level could never get through).
TEST_CASE("sliding: a save point stops a search between two levels",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidesavepoint");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  straight.threads = 2;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // asked for before every run begins: each run searches one level
  StopSource point;
  point.request(StopKind::Pause);
  std::atomic<std::uint64_t> progress{0};
  std::uint64_t depth = 0;
  std::uint64_t visited = 0;
  unsigned int runs = 0;
  std::optional<Separation> path;
  for (;;) {
    SlideStopProgress told;
    SlideSearch run;
    run.threads = 2;
    run.workDir = dir.string();
    run.progress = &progress;
    run.savePoint = point.token();
    run.stopProgress = &told;
    path = findSlidePath(f.puz, f.pr(), *f.start, run);
    runs++;
    CHECK(run.resumed == (runs > 1));
    if (path.has_value()) {
      CHECK(run.outcome == SlideOutcome::Found);
      CHECK_FALSE(run.saved);
      visited = run.visited;
      break;
    }
    REQUIRE(run.outcome == SlideOutcome::Stopped);
    REQUIRE(run.saved);
    // it tells its save as a paused search does
    CHECK(told.saving.load());
    // exactly a level further, and what it visited is kept: no level was
    // cut short
    REQUIRE(run.depth == depth + 1);
    CHECK(run.visited > visited);
    CHECK(progress.load() == run.visited);
    depth = run.depth;
    visited = run.visited;
    REQUIRE(runs < 200);
  }
  CHECK(runs > 40);
  CHECK(path->getMoves() == 81);
  CHECK(placesAlong(*path) == placesAlong(*whole));
  // every arrangement was visited once, whatever the stops in between
  CHECK(visited == straight.visited);
  CHECK(progress.load() == straight.visited);
  CHECK(std::filesystem::is_empty(dir));

  // with nowhere to save, a save point lets the search run to its end
  SlideSearch nowhere;
  nowhere.threads = 2;
  nowhere.savePoint = point.token();
  const std::optional<Separation> through = findSlidePath(f.puz, f.pr(), *f.start, nowhere);
  REQUIRE(through.has_value());
  CHECK(nowhere.outcome == SlideOutcome::Found);
  CHECK(nowhere.visited == straight.visited);
  CHECK(placesAlong(*through) == placesAlong(*whole));

  // a stop asked for with it wins: the search stops at once, as paused
  StopSource stop;
  stop.request(StopKind::Pause);
  SlideSearch both;
  both.workDir = dir.string();
  both.savePoint = point.token();
  both.stop = stop.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, both).has_value());
  CHECK(both.outcome == SlideOutcome::Stopped);
  CHECK(both.saved);
  CHECK(both.depth == 0);
  discardSaved(f.puz, f.pr(), dir.string());
  std::filesystem::remove_all(dir);
}

// A disk nearly full (here: a reserve larger than any disk) stops the
// search while it can still save itself, and once there is room again a
// search of the same start carries it on to the same path.
TEST_CASE("sliding: a search stops with room to save itself", "[sliding][resume]") {
  const std::filesystem::path dir = scratchDir("slidereserve");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  SlideSearch full;
  full.workDir = dir.string();
  full.memoryLevelBytes = 1;
  full.diskReserve = std::uint64_t{1} << 62;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, full).has_value());
  REQUIRE(full.outcome == SlideOutcome::Disk);
  CHECK(full.saved);
  CHECK(full.diskHeldToFree);
  CHECK(full.error.find("The disk is nearly full") != std::string::npos);
  CHECK_FALSE(std::filesystem::is_empty(dir));

  SlideSearch room;
  room.workDir = dir.string();
  room.memoryLevelBytes = 1;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, room);
  REQUIRE(rest.has_value());
  CHECK(room.resumed);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(room.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
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

#ifdef __SIZEOF_INT128__
// Sorted parts join into one set without decoding; a cursor starts at any
// key; a set written to a file opens again.
TEST_CASE("sliding: key sets join, seek and reopen", "[sliding]") {
  const std::filesystem::path dir = scratchDir("keyset2");
  std::filesystem::create_directories(dir);
  std::vector<Wide> keys;
  for (int i = 0; i < 1000; i++) {
    keys.push_back(static_cast<Wide>(i) * 37 + (i % 7 == 0 ? (static_cast<Wide>(1) << 70) : 0));
  }
  std::sort(keys.begin(), keys.end());
  for (int onDisk = 0; onDisk < 2; onDisk++) {
    const std::filesystem::path a = onDisk != 0 ? dir / "a" : std::filesystem::path();
    const std::filesystem::path b = onDisk != 0 ? dir / "b" : std::filesystem::path();
    levels::KeySet<Wide> first;
    levels::KeySet<Wide> second;
    REQUIRE(first.create(a));
    REQUIRE(second.create(b));
    // the cut leaves a part block in the first set
    REQUIRE(first.addAll(keys.begin(), keys.begin() + 333));
    REQUIRE(second.addAll(keys.begin() + 333, keys.end()));
    REQUIRE(first.finish());
    REQUIRE(second.finish());
    levels::KeySet<Wide> joined;
    REQUIRE(joined.create(onDisk != 0 ? dir / "joined" : std::filesystem::path()));
    REQUIRE(joined.appendSet(first));
    REQUIRE(joined.appendSet(second));
    REQUIRE(joined.finish());
    CHECK(joined.size() == keys.size());
    CHECK(joined.lastKey() == keys.back());
    std::vector<Wide> back;
    REQUIRE(joined.load(back));
    CHECK(back == keys);

    // a cursor from a key in the set, from one between two, and past the end
    for (const std::size_t from :
         {std::size_t{0}, std::size_t{128}, std::size_t{500}, keys.size() - 1}) {
      levels::KeySet<Wide>::Cursor c(joined, keys[from]);
      std::vector<Wide> rest;
      Wide k = 0;
      while (c.next(k)) {
        rest.push_back(k);
      }
      CHECK(rest ==
            std::vector<Wide>(keys.begin() + static_cast<std::ptrdiff_t>(from), keys.end()));
    }
    levels::KeySet<Wide>::Cursor between(joined, keys[10] + 1);
    Wide k = 0;
    REQUIRE(between.next(k));
    CHECK(k == keys[11]);
    levels::KeySet<Wide>::Cursor past(joined, keys.back() + 1);
    CHECK_FALSE(past.next(k));

    if (onDisk != 0) {
      // the file opens again by itself, every key found
      joined.release();
      levels::KeySet<Wide> reopened;
      REQUIRE(reopened.open(dir / "joined"));
      CHECK(reopened.size() == keys.size());
      std::vector<char> found(keys.size(), 0);
      levels::KeySet<Wide>::Reader reader(reopened);
      REQUIRE(reopened.find(keys.data(), keys.size(), found.data(), reader));
      CHECK(std::count(found.begin(), found.end(), 1) == static_cast<long>(keys.size()));
      // the reopened set owns the file now: the first does not delete it
      reopened.keepFile();
    }
  }
  std::filesystem::remove_all(dir);
}
#endif

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
        REQUIRE(assm->createMatrix(mo) == AssemblerError::None);
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

// Loose stops (decision 0031): a board of 3x3 blocks on a grid of 3, some
// of them cups, with small pieces in their pockets and in the empty cells.
// Every piece has a start; `places` says where the pieces with a goal must
// end.
struct GridBoard {
  Puzzle puz{GridType(GridKind::Sliding)};
  std::unique_ptr<Assembly> start;
  struct Place {
    int x;
    int y;
    int goalX;  // negative: it may end anywhere
    int goalY;
    bool block;
  };
  std::vector<Place> places;
  // What it was made of (gridBoardOf), for a board like it with other goals.
  int W = 0;
  int H = 0;
  std::vector<std::vector<std::pair<int, int>>> shapes;
  // For each piece the first of the copies it is one of: itself, but for
  // the copies of a piece.
  std::vector<std::size_t> copyOf;
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
};

using BoardCells = std::vector<std::pair<int, int>>;

// A 3x3 block without the cells of `hole`.
BoardCells blockWithout(const BoardCells& hole) {
  BoardCells cells;
  for (int cy = 0; cy < 3; cy++) {
    for (int cx = 0; cx < 3; cx++) {
      if (std::find(hole.begin(), hole.end(), std::pair<int, int>{cx, cy}) == hole.end()) {
        cells.emplace_back(cx, cy);
      }
    }
  }
  return cells;
}

// The board W x H of pieces of these cells, each at its place. With
// `copies` the whole blocks are the copies of one piece and the unit pieces
// of another, each kind after the first of it: its starts and its goals are
// any copy's.
GridBoard gridBoardOf(int W, int H, const std::vector<BoardCells>& shapes,
                      const std::vector<GridBoard::Place>& places, bool copies = false) {
  GridBoard g;
  const unsigned int trayId =
      addStartGoalShape(g.puz, static_cast<unsigned int>(W), static_cast<unsigned int>(H));
  g.start = std::make_unique<Assembly>(g.puz.getGridType());
  g.W = W;
  g.H = H;
  std::vector<char> taken(shapes.size(), 0);
  for (std::size_t i = 0; i < shapes.size(); i++) {
    if (taken[i] != 0) {
      continue;
    }
    int w = 3;
    int h = 3;
    for (const std::pair<int, int> c : shapes[i]) {
      w = std::max(w, c.first + 1);
      h = std::max(h, c.second + 1);
    }
    const unsigned int id =
        g.puz.addShape(static_cast<unsigned int>(w), static_cast<unsigned int>(h), 1);
    for (const std::pair<int, int> c : shapes[i]) {
      g.puz.getShape(id).setState(static_cast<unsigned int>(c.first),
                                  static_cast<unsigned int>(c.second), 0, kFilled);
    }
    const bool alike = copies && (shapes[i].size() == 1 || shapes[i].size() == 9);
    const std::size_t first = g.places.size();
    for (std::size_t j = i; j < shapes.size(); j++) {
      if (taken[j] != 0 || (j != i && !(alike && shapes[j] == shapes[i]))) {
        continue;
      }
      taken[j] = 1;
      const GridBoard::Place& at = places[j];
      for (const std::pair<int, int> c : shapes[j]) {
        Voxel& tray = g.puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(at.x + c.first),
                           static_cast<unsigned int>(at.y + c.second), 0, id + 1);
        if (at.goalX >= 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(at.goalX + c.first),
                                          static_cast<unsigned int>(at.goalY + c.second), 0),
                            id + 1);
        }
      }
      g.start->addPlacement(0, at.x, at.y, 0);
      g.places.push_back(at);
      g.shapes.push_back(shapes[j]);
      g.copyOf.push_back(first);
    }
  }
  syncSlidingProblems(g.puz);
  return g;
}

// A 9x6 board, three cells by two. U is a cup open at the top and L one
// open to the left; p, q and r are unit pieces, which stop at three places
// in a cell of the grid (where a pocket of U or L would hold them):
//   .p. .q. .r.
//   ... ... ...
//   ... ... ...
//   U.U ... LLL
//   U.U ... ..L
//   UUU ... LLL
// U must end top right with p in its pocket and L bottom left with q in
// its own; r may end anywhere. With `reachable` off U's goal takes in a
// cell where no piece may stop, so the search goes through everything
// (a smaller everything without r, `units` 2).
GridBoard cupsAndUnits(bool reachable, int units = 3) {
  const BoardCells unit = {{0, 0}};
  std::vector<BoardCells> shapes = {blockWithout({{1, 0}, {1, 1}}), blockWithout({{0, 1}, {1, 1}}),
                                    unit, unit};
  std::vector<GridBoard::Place> places = {
      {0, 3, 6, 0, true}, {6, 3, 0, 3, true}, {1, 0, 7, 1, false}, {4, 0, 1, 4, false}};
  if (units > 2) {
    shapes.push_back(unit);
    places.push_back({7, 0, -1, -1, false});
  }
  GridBoard g = gridBoardOf(9, 6, shapes, places);
  if (!reachable) {
    g.puz.getShape(0).setState(8, 2, 0, kVariable);
    syncSlidingProblems(g.puz);
  }
  return g;
}

// A board made from `seed`: blocks of several kinds in cells of the grid,
// one to three small pieces anywhere they fit, and goals for some of the
// pieces from a second layout made the same way.
GridBoard gridBoard(std::uint64_t seed) {
  std::uint64_t x = seed * 0x9e3779b97f4a7c15ULL + 88172645463325252ULL;
  auto rnd = [&x](int n) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return static_cast<int>((x >> 11) % static_cast<std::uint64_t>(n));
  };
  // the cells a block leaves empty: none, the cups open to each side, a
  // ring, a corner cut out, a notch
  const std::vector<BoardCells> holes = {
      {},
      {{1, 0}, {1, 1}},
      {{0, 1}, {1, 1}},
      {{1, 1}, {2, 1}},
      {{1, 1}, {1, 2}},
      {{1, 1}},
      {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
      {{1, 0}},
  };
  const std::vector<BoardCells> smalls = {{{0, 0}}, {{0, 0}, {1, 0}}, {{0, 0}, {0, 1}}};

  const int cw = 2 + rnd(2);
  const int ch = cw == 3 ? 2 + rnd(2) : 2;
  const int W = 3 * cw;
  const int H = 3 * ch;
  const int blocks = cw * ch - 1 - rnd(cw * ch > 4 ? 3 : 2);
  const int small = 1 + rnd(3);

  std::vector<BoardCells> shapes;
  for (int b = 0; b < blocks; b++) {
    shapes.push_back(blockWithout(holes[static_cast<std::size_t>(rnd(2) == 0 ? rnd(5) : rnd(8))]));
  }
  for (int s = 0; s < small; s++) {
    shapes.push_back(smalls[static_cast<std::size_t>(rnd(2) == 0 ? 0 : rnd(3))]);
  }

  // a layout: the blocks in cells of the grid, then the small pieces where
  // they fit (negative: nowhere)
  auto layout = [&](std::vector<std::pair<int, int>>& at) {
    std::vector<int> cell(static_cast<std::size_t>(cw * ch));
    for (std::size_t i = 0; i < cell.size(); i++) {
      cell[i] = static_cast<int>(i);
    }
    for (std::size_t i = cell.size(); i > 1; i--) {
      std::swap(cell[i - 1], cell[static_cast<std::size_t>(rnd(static_cast<int>(i)))]);
    }
    std::vector<int> occ(static_cast<std::size_t>(W * H), 0);
    at.assign(shapes.size(), {-1, -1});
    for (std::size_t i = 0; i < shapes.size(); i++) {
      for (int tries = 0; tries < 50 && at[i].first < 0; tries++) {
        const bool block = i < static_cast<std::size_t>(blocks);
        const int px = block ? 3 * (cell[i] % cw) : rnd(W);
        const int py = block ? 3 * (cell[i] / cw) : rnd(H);
        bool fits = true;
        for (const std::pair<int, int> c : shapes[i]) {
          if (px + c.first >= W || py + c.second >= H ||
              occ[static_cast<std::size_t>((py + c.second) * W + px + c.first)] != 0) {
            fits = false;
          }
        }
        if (!fits) {
          continue;
        }
        for (const std::pair<int, int> c : shapes[i]) {
          occ[static_cast<std::size_t>((py + c.second) * W + px + c.first)] = 1;
        }
        at[i] = {px, py};
      }
    }
  };
  std::vector<std::pair<int, int>> from;
  std::vector<std::pair<int, int>> to;
  layout(from);
  layout(to);

  std::vector<BoardCells> placed;
  std::vector<GridBoard::Place> places;
  for (std::size_t i = 0; i < shapes.size(); i++) {
    if (from[i].first < 0) {
      continue;
    }
    // the first piece always has a goal, the others half the time
    const bool goal = to[i].first >= 0 && (places.empty() || rnd(2) == 0);
    placed.push_back(shapes[i]);
    places.push_back({from[i].first, from[i].second, goal ? to[i].first : -1,
                      goal ? to[i].second : -1, i < static_cast<std::size_t>(blocks)});
  }
  return gridBoardOf(W, H, placed, places);
}

// A path of the board: from its start to its goals, every step a slide,
// the blocks stopping only on the grid's lines (not when the board was
// searched without the grid).
void checkGridPath(const GridBoard& g, const Separation& path, bool onGrid = true) {
  REQUIRE(path.getPieceNumber() == g.places.size());
  const State& first = path.getState(0);
  const State& last = path.getState(path.getMoves());
  // the copies of a piece take its starts and its goals in any order
  for (unsigned int i = 0; i < g.places.size(); i++) {
    if (g.copyOf[i] != i) {
      continue;
    }
    std::vector<std::pair<int, int>> starts;
    std::vector<std::pair<int, int>> began;
    std::vector<std::pair<int, int>> goals;
    std::vector<std::pair<int, int>> ended;
    for (unsigned int j = i; j < g.places.size() && g.copyOf[j] == i; j++) {
      const GridBoard::Place& p = g.places[j];
      starts.emplace_back(p.x, p.y);
      began.emplace_back(first.getX(j), first.getY(j));
      ended.emplace_back(last.getX(j), last.getY(j));
      if (p.goalX >= 0) {
        goals.emplace_back(p.goalX, p.goalY);
      }
    }
    std::sort(starts.begin(), starts.end());
    std::sort(began.begin(), began.end());
    std::sort(goals.begin(), goals.end());
    std::sort(ended.begin(), ended.end());
    CHECK(began == starts);
    if (!goals.empty()) {
      CHECK(ended == goals);
    }
  }
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    INFO("step " << step);
    std::vector<unsigned int> movers;
    CHECK(slideRoute(g.puz, g.pr(), path, step, &movers).size() >= 2);
    for (unsigned int i = 0; i < g.places.size(); i++) {
      if (onGrid && g.places[i].block) {
        CHECK(path.getState(step + 1).getX(i) % 3 == 0);
        CHECK(path.getState(step + 1).getY(i) % 3 == 0);
      }
    }
  }
}

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

// A search with the stops of a grid cell as one is saved and carried on as
// any other; one saved with every stop told apart (or before there were
// loose stops) is carried on as it was begun.
TEST_CASE("sliding: a search with the stops of a grid cell as one is carried on",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slideloose");
  std::filesystem::remove_all(dir);
  auto gridSearch = [] {
    SlideSearch s;
    s.nested = true;
    s.grid = 3;
    s.threads = 2;
    return s;
  };
  auto stoppedAt = [&](const GridBoard& g, std::uint64_t depth, bool apart = false) {
    StopSource stop;
    SlideSearch first = gridSearch();
    first.gridStopsApart = apart;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    CHECK(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    CHECK(first.looseStops == !apart);
    return first.visited;
  };

  const GridBoard g = cupsAndUnits(true);
  SlideSearch straight = gridSearch();
  const std::optional<Separation> whole = findSlidePath(g.puz, g.pr(), *g.start, straight);
  REQUIRE(whole.has_value());
  const std::uint64_t before = stoppedAt(g, 3);
  CHECK(before < straight.visited);
  SlideSearch second = gridSearch();
  second.workDir = dir.string();
  const std::optional<Separation> rest = findSlidePath(g.puz, g.pr(), *g.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(second.looseStops);
  CHECK(rest->getMoves() == whole->getMoves());
  checkGridPath(g, *rest);
  // the level the path is found on ends where the threads happen to be, so
  // the two counts are only near each other; the search below, with no
  // path, counts every arrangement once
  CHECK(second.visited > before);

  // a search with no path: carried on, the arrangements of the whole one
  const GridBoard none = cupsAndUnits(false, 2);
  SlideSearch all = gridSearch();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, all).has_value());
  REQUIRE(all.outcome == SlideOutcome::NoPath);
  CHECK(stoppedAt(none, 6) < all.visited);
  SlideSearch on = gridSearch();
  on.workDir = dir.string();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, on).has_value());
  CHECK(on.resumed);
  CHECK(on.outcome == SlideOutcome::NoPath);
  CHECK(on.visited == all.visited);

  // The same with the stops told apart. (A search stopped between two
  // levels is saved with one still to go into those the one-way moves are
  // looked for in; carried on, it puts the level there.)
  SlideSearch allApart = gridSearch();
  allApart.gridStopsApart = true;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, allApart).has_value());
  CHECK(stoppedAt(none, 6, true) < allApart.visited);
  SlideSearch onApart = gridSearch();
  onApart.gridStopsApart = true;
  onApart.workDir = dir.string();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, onApart).has_value());
  CHECK(onApart.resumed);
  CHECK_FALSE(onApart.looseStops);
  CHECK(onApart.outcome == SlideOutcome::NoPath);
  CHECK(onApart.visited == allApart.visited);

  // saved with the stops told apart, it is carried on so
  StopSource once;
  once.request(StopKind::Pause);
  SlideSearch old = gridSearch();
  old.gridStopsApart = true;
  old.workDir = dir.string();
  old.stop = once.token();
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, old).has_value());
  CHECK(old.saved);
  SlideSearch later = gridSearch();
  later.workDir = dir.string();
  const std::optional<Separation> same = findSlidePath(g.puz, g.pr(), *g.start, later);
  REQUIRE(same.has_value());
  CHECK(later.resumed);
  CHECK_FALSE(later.looseStops);
  CHECK(same->getMoves() == whole->getMoves());
  // and the next search of the start has them as one again
  SlideSearch next = gridSearch();
  next.workDir = dir.string();
  REQUIRE(findSlidePath(g.puz, g.pr(), *g.start, next).has_value());
  CHECK_FALSE(next.resumed);
  CHECK(next.looseStops);
  std::filesystem::remove_all(dir);
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

// What a search's levels came to: "start level 3: 120 arrangements" each.
struct LevelSizes {
  std::vector<std::string> lines;
  void watch(SlideSearch& s) {
    s.onLevel = [this](const std::string& line) {
      lines.push_back(line.substr(0, line.find(", one-way")));
    };
  }
  // Whether the search went from the goal as well.
  [[nodiscard]] bool fromGoal() const {
    return std::any_of(lines.begin(), lines.end(),
                       [](const std::string& line) { return line.starts_with("goal"); });
  }
};

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

// A grid search saved with every position numbered, as the builds before
// the stops were numbered saved them, is carried on as it was begun. The
// A/B switch itself does not take up a search saved without it.
TEST_CASE("sliding: a grid search saved with every position numbered is carried on",
          "[sliding][resume]") {
  const std::filesystem::path dir = scratchDir("slideplain");
  std::filesystem::remove_all(dir);
  const GridBoard g = cupsAndUnits(true);
  for (const bool nested : {true, false}) {
    INFO("nested " << nested);
    // one thread, so that the level the path is found on counts the same
    // arrangements each time
    auto gridSearch = [&] {
      SlideSearch s;
      s.nested = nested;
      s.grid = 3;
      s.threads = 1;
      return s;
    };
    auto stopped = [&](bool plainKeys) {
      StopSource stop;
      SlideSearch first = gridSearch();
      first.plainKeys = plainKeys;
      first.workDir = dir.string();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= 3) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
      CHECK(first.outcome == SlideOutcome::Stopped);
      CHECK(first.saved);
    };
    SlideSearch straight = gridSearch();
    const std::optional<Separation> whole = findSlidePath(g.puz, g.pr(), *g.start, straight);

    stopped(true);
    SlideSearch later = gridSearch();
    later.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(g.puz, g.pr(), *g.start, later);
    CHECK(later.resumed);
    CHECK(later.looseStops == straight.looseStops);
    CHECK(later.outcome == straight.outcome);
    CHECK(later.visited == straight.visited);
    REQUIRE(rest.has_value() == whole.has_value());
    if (rest) {
      CHECK(placesAlong(*rest) == placesAlong(*whole));
    }

    // saved with the stops numbered: the switch begins its own search
    stopped(false);
    SlideSearch other = gridSearch();
    other.plainKeys = true;
    other.workDir = dir.string();
    (void)findSlidePath(g.puz, g.pr(), *g.start, other);
    CHECK_FALSE(other.resumed);
    CHECK(other.visited == straight.visited);
    SlideSearch own = gridSearch();
    own.workDir = dir.string();
    (void)findSlidePath(g.puz, g.pr(), *g.start, own);
    CHECK(own.resumed);
    CHECK(own.visited == straight.visited);
  }

  // without a grid every position is a stop: the keys, and so the saved
  // searches, are the same with the switch and without
  const SlideFixture f = eightPuzzle();
  StopSource stop;
  SlideSearch first;
  first.plainKeys = true;
  first.workDir = dir.string();
  first.stop = stop.token();
  first.onDepth = [&](std::uint64_t at) {
    if (at >= 4) {
      stop.request(StopKind::Pause);
    }
  };
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  CHECK(first.saved);
  SlideSearch second;
  second.workDir = dir.string();
  CHECK(findSlidePath(f.puz, f.pr(), *f.start, second).has_value());
  CHECK(second.resumed);
  std::filesystem::remove_all(dir);
}

namespace {

// The board with every piece's goal where a path left it: a goal that is
// one arrangement, and one the board comes to.
GridBoard endAsGoal(const GridBoard& g, const Separation& path) {
  std::vector<GridBoard::Place> places = g.places;
  const State& last = path.getState(path.getMoves());
  for (unsigned int i = 0; i < places.size(); i++) {
    places[i].goalX = last.getX(i);
    places[i].goalY = last.getY(i);
  }
  return gridBoardOf(g.W, g.H, g.shapes, places);
}

// The board of `seed` with its goal made one arrangement: where the fewest
// moves from the start alone leave every piece.
GridBoard settledBoard(std::uint64_t seed) {
  const GridBoard g = gridBoard(seed);
  SlideSearch one;
  one.nested = true;
  one.grid = 3;
  one.oneEnd = true;
  one.maxMemoryStates = 30000;
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
  REQUIRE(path.has_value());
  return endAsGoal(g, *path);
}

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

// Three blocks in the four cells of a 6x6 board, the first a cup with a
// unit piece in its pocket. The goal has the cup and the block beside it
// swapped: no sliding does that.
GridBoard swappedBlocks() {
  return gridBoardOf(
      6, 6, {blockWithout({{1, 0}, {1, 1}}), blockWithout({}), blockWithout({}), {{0, 0}}},
      {{0, 0, 3, 0, true}, {3, 0, 0, 0, true}, {0, 3, 0, 3, true}, {1, 1, 4, 1, false}});
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

// The board searched, and whether the search went from the goal as well.
std::optional<Separation> searched(const GridBoard& g, SlideSearch& s, bool& fromGoal) {
  LevelSizes levels;
  levels.watch(s);
  std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, s);
  s.onLevel = nullptr;
  fromGoal = levels.fromGoal();
  return path;
}

// A search with nested slides (grid 0: every position a stop).
SlideSearch nestedSearch(int grid) {
  SlideSearch s;
  s.nested = true;
  s.grid = grid;
  return s;
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

// A search from both ends with nested slides is saved with what each end
// has been through and carried on to the fewest moves, stopped wherever.
TEST_CASE("sliding: a search from both ends with nested slides is carried on",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slideends");
  std::filesystem::remove_all(dir);
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };
  // stopped `depth` moves deep (both ends together) and saved
  auto stoppedAt = [&](const GridBoard& g, std::uint64_t depth, SlideSearch first) {
    StopSource stop;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    CHECK(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    return first;
  };

  const GridBoard g = settledBoard(37);
  bool fromGoal = false;
  SlideSearch straight = gridSearch();
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  REQUIRE(fromGoal);
  const unsigned int moves = whole->getMoves();
  REQUIRE(moves >= 14);
  for (const std::uint64_t depth : {1U, 2U, 5U, 8U, moves - 3U, moves - 1U}) {
    INFO("stopped " << depth << " moves deep");
    const SlideSearch first = stoppedAt(g, depth, gridSearch());
    CHECK_FALSE(first.looseStops);
    SlideSearch second = gridSearch();
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, second, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK_FALSE(second.looseStops);
    CHECK(rest->getMoves() == moves);
    checkGridPath(g, *rest);
    if (depth + 6 < moves) {
      CHECK(fromGoal);
    }
    CHECK(std::filesystem::is_empty(dir));
  }

  // no path: carried on, the arrangements of the whole search
  const GridBoard none = swappedBlocks();
  SlideSearch all = gridSearch();
  CHECK_FALSE(searched(none, all, fromGoal).has_value());
  REQUIRE(all.outcome == SlideOutcome::NoPath);
  CHECK(fromGoal);
  for (const std::uint64_t depth : {2U, 3U, 6U}) {
    INFO("stopped " << depth << " moves deep");
    CHECK(stoppedAt(none, depth, gridSearch()).visited < all.visited);
    SlideSearch on = gridSearch();
    on.workDir = dir.string();
    CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, on).has_value());
    CHECK(on.resumed);
    CHECK(on.outcome == SlideOutcome::NoPath);
    CHECK(on.visited == all.visited);
  }

  // saved from both ends, it is carried on so by the A/B switch too
  (void)stoppedAt(g, 3, gridSearch());
  SlideSearch switched = gridSearch();
  switched.oneEnd = true;
  switched.workDir = dir.string();
  const std::optional<Separation> same = searched(g, switched, fromGoal);
  REQUIRE(same.has_value());
  CHECK(switched.resumed);
  CHECK(fromGoal);
  CHECK(same->getMoves() == moves);

  // Saved from the start alone with the stops of a cell as one (as the
  // builds before the two ends saved it), it is carried on as it was begun:
  // those arrangements have no moves backwards.
  SlideSearch alone = gridSearch();
  alone.oneEnd = true;
  CHECK(stoppedAt(g, 3, alone).looseStops);
  SlideSearch later = gridSearch();
  later.workDir = dir.string();
  const std::optional<Separation> loose = searched(g, later, fromGoal);
  REQUIRE(loose.has_value());
  CHECK(later.resumed);
  CHECK(later.looseStops);
  CHECK_FALSE(fromGoal);
  CHECK(loose->getMoves() == moves);
  checkGridPath(g, *loose);

  // Saved from the start alone with the stops apart, it takes up the goal
  // end as it is carried on.
  alone.gridStopsApart = true;
  CHECK_FALSE(stoppedAt(g, 3, alone).looseStops);
  SlideSearch joined = gridSearch();
  joined.workDir = dir.string();
  const std::optional<Separation> apart = searched(g, joined, fromGoal);
  REQUIRE(apart.has_value());
  CHECK(joined.resumed);
  CHECK_FALSE(joined.looseStops);
  CHECK(fromGoal);
  CHECK(apart->getMoves() == moves);
  checkGridPath(g, *apart);

  // and a search with nothing saved goes from both ends again
  SlideSearch next = gridSearch();
  next.workDir = dir.string();
  REQUIRE(searched(g, next, fromGoal).has_value());
  CHECK_FALSE(next.resumed);
  CHECK(fromGoal);
  std::filesystem::remove_all(dir);
}

// The parts of a move that visit no arrangement are told as they run
// (SlideSearch::phase): the merges of each level whole by the time the
// move is told (onDepth), the merge into the visited sets too, and nothing
// once the search has ended; whatever is told while it runs has no more
// done than its total.
TEST_CASE("sliding: a search with nested slides tells the parts of a move that visit nothing",
          "[sliding][parallel]") {
  const std::filesystem::path dir = scratchDir("slidephase");
  std::filesystem::remove_all(dir);
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
      const SlidePhase part = phase.phase.load(std::memory_order_acquire);
      const bool visited = part == SlidePhase::Visited;
      const std::uint64_t d = (visited ? phase.visitedDone : phase.done).load();
      const std::uint64_t t = (visited ? phase.visitedTotal : phase.total).load();
      if (part != SlidePhase::None && d > t) {
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

// The Solver's run of a long search with nested slides from both ends: its
// levels on disk, saved now and then as it goes (autosave), at save points
// (the crash journal's) and when paused in the middle of a level, stopped
// when it fills its disk budget and carried on with a larger one, again
// and again. Every run carries the saved search on, never deeper than the
// path, and the path is the one a search with no stops finds.
TEST_CASE("sliding: a search from both ends with nested slides is carried on past its disk budget",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidebudget");
  std::filesystem::remove_all(dir);
  const GridBoard g = settledBoard(37);

  // levels in memory up to `memory` bytes; autosaved after every level or,
  // with `autosave` off, at the stops alone
  auto carriedOn = [&](std::uint64_t memory, bool autosave) {
    INFO("memory " << memory << ", autosave " << autosave);
    auto runSearch = [&] {
      SlideSearch s = nestedSearch(3);
      s.threads = 2;
      s.workDir = dir.string();
      s.memoryLevelBytes = memory;
      s.autosave = autosave;
      s.autosaveFloorSeconds = 0;
      return s;
    };
    bool fromGoal = false;
    SlideSearch straight = runSearch();
    const std::optional<Separation> whole = searched(g, straight, fromGoal);
    REQUIRE(whole.has_value());
    REQUIRE(fromGoal);
    const unsigned int moves = whole->getMoves();
    REQUIRE(moves >= 14);
    CHECK(std::filesystem::is_empty(dir));
    if (straight.diskBytes == 0) {
      return;  // all of it in memory: no disk budget to reach
    }

    const std::uint64_t step = std::max<std::uint64_t>(straight.diskBytes / 16, 1);
    std::uint64_t budget = step;
    std::uint64_t depth = 0;
    unsigned int diskStops = 0;
    bool pointNext = false;
    bool sawPicking = false;
    std::optional<Separation> path;
    for (unsigned int runs = 1;; runs++) {
      INFO("run " << runs << ", disk budget " << budget << " of " << straight.diskBytes);
      REQUIRE(runs < 200);
      SlideSearch s = runSearch();
      s.diskBudget = budget;
      std::vector<LogLine> told;
      // taking the saved search up is told while it lasts (pickingUpSince): on
      // from its state read (still on when the visited sets are back), off
      // before the search says it was carried on
      std::atomic<std::int64_t> picking{0};
      s.pickingUpSince = &picking;
      s.onLog = [&](LogLevel level, const std::string& text) {
        if (text.starts_with("The visited sets of the saved search were read back in ")) {
          // since a moment ago, by the clock its time is told on
          CHECK(picking.load() != 0);
          CHECK(picking.load() <= slideClockMs());
          sawPicking = true;
        }
        if (text.starts_with("Carried on the saved search in ")) {
          CHECK(picking.load() == 0);
        }
        // a debug log's lines aside (the debug log's own test has those),
        // and the key the search runs in, told before each run
        if (level != LogLevel::Debug && !text.starts_with("The slide search runs ")) {
          told.push_back(LogLine{level, text});
        }
      };
      // after a disk stop, the run carried on with room for more stops at
      // a save point (the crash journal's) before the budget is reached
      StopSource point;
      if (pointNext) {
        point.request(StopKind::Pause);
        s.savePoint = point.token();
      }
      pointNext = false;
      // every third run paused in the middle of a level (Pause, File ▸ Save)
      StopSource pause;
      std::atomic<std::uint64_t> progress{0};
      std::atomic<bool> done{false};
      std::thread watcher;
      if (runs % 3 == 0) {
        s.stop = pause.token();
        s.progress = &progress;
        const std::uint64_t after = std::max<std::uint64_t>(straight.visited / 40, 1);
        watcher = std::thread([&, after] {
          while (!done.load()) {
            if (progress.load() >= after) {
              pause.request(StopKind::Pause);
              return;
            }
            std::this_thread::yield();
          }
        });
      }
      path = searched(g, s, fromGoal);
      done.store(true);
      if (watcher.joinable()) {
        watcher.join();
      }
      if (runs > 1) {
        CHECK(s.resumed);
        // the log tells how deep each end was carried on from
        REQUIRE_FALSE(told.empty());
        CHECK(told.front().level == LogLevel::Info);
        CHECK(told.front().text.starts_with("Carried on the saved search in "));
        // and what taking it up cost
        CHECK(told.front().text.find(" Taken up in ") != std::string::npos);
        CHECK(told.front().text.ends_with(" s for the visited sets."));
      }
      CHECK(picking.load() == 0);
      // both ends together never deeper than the path
      CHECK(s.depth <= moves);
      CHECK(s.depth >= depth);
      depth = s.depth;
      if (path.has_value()) {
        CHECK(s.outcome == SlideOutcome::Found);
        CHECK(s.visited == straight.visited);
        break;
      }
      REQUIRE(s.saved);
      if (s.outcome == SlideOutcome::Disk) {
        CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
          return l.level == LogLevel::Warning && l.text.find("The search is saved") != std::string::npos;
        }));
        diskStops++;
        budget += 2 * step;
        pointNext = true;
      } else {
        REQUIRE(s.outcome == SlideOutcome::Stopped);
      }
    }
    CHECK(diskStops >= 3);
    CHECK(sawPicking);
    CHECK(path->getMoves() == moves);
    checkGridPath(g, *path);
    CHECK(std::filesystem::is_empty(dir));
  };
  for (const std::uint64_t memory : {1ULL, 512ULL}) {
    carriedOn(memory, true);
    carriedOn(memory, false);
  }
  std::filesystem::remove_all(dir);
}

// A saved search that cannot be carried on (a file of one of its levels
// damaged or gone) is kept as it is, every file of it, and nothing is
// searched: a saved search is days of work, and what could not be read now
// may be read later (decision 0041). With the file back the search is
// carried on; a search that does not carry on throws the saved one away and
// finds the path a search with no stops finds.
TEST_CASE("sliding: a saved search with a damaged level is kept, and nothing is searched",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidedamaged");
  const std::filesystem::path spareDir = scratchDir("slidedamaged-spare");
  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(spareDir);
  std::filesystem::create_directories(spareDir);
  const std::filesystem::path spare = spareDir / "spare.keys";
  auto runSearch = [&] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    return s;
  };
  const GridBoard g = settledBoard(37);
  bool fromGoal = false;
  SlideSearch straight = runSearch();
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  REQUIRE(fromGoal);
  const unsigned int moves = whole->getMoves();
  REQUIRE(moves >= 14);

  // stopped 10 moves deep (both ends together), saved
  auto stopped = [&] {
    StopSource stop;
    SlideSearch first = runSearch();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= 10) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    REQUIRE(first.saved);
  };
  auto levelFiles = [&] {
    std::vector<std::filesystem::path> files;
    for (const auto& folder : std::filesystem::directory_iterator(dir)) {
      for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
        if (e.path().extension() == ".keys") {
          files.push_back(e.path());
        }
      }
    }
    std::sort(files.begin(), files.end());
    return files;
  };
  // every file of the saved search, with its size
  auto listing = [&] {
    std::map<std::string, std::uintmax_t> files;
    for (const auto& folder : std::filesystem::directory_iterator(dir)) {
      for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
        files[e.path().filename().string()] = e.file_size();
      }
    }
    return files;
  };

  stopped();
  const std::size_t count = levelFiles().size();
  REQUIRE(count >= 10);
  // every other file: the ends' newest levels, their kept levels and the
  // visited sets are among them
  unsigned int tried = 0;
  for (std::size_t i = 0; i < count; i += 2, tried++) {
    stopped();
    const std::vector<std::filesystem::path> files = levelFiles();
    REQUIRE(files.size() == count);
    const std::string name = files[i].filename().string();
    const bool gone = tried % 2 == 1;
    INFO((gone ? "gone: " : "cut short: ") << name);
    std::filesystem::copy_file(files[i], spare, std::filesystem::copy_options::overwrite_existing);
    if (gone) {
      std::filesystem::remove(files[i]);
    } else {
      std::filesystem::resize_file(files[i], std::filesystem::file_size(files[i]) / 2);
    }
    const std::map<std::string, std::uintmax_t> before = listing();
    // twice: the second try finds what the first found, and leaves it too
    for (int again = 0; again < 2; again++) {
      SlideSearch on = runSearch();
      std::vector<LogLine> told;
      on.onLog = [&told](LogLevel level, const std::string& text) {
        // a debug log's lines aside (what state.bin names comes first), and
        // the key the search runs in, told before it
        if (level != LogLevel::Debug && !text.starts_with("The slide search runs ")) {
          told.push_back(LogLine{level, text});
        }
      };
      const std::optional<Separation> path = searched(g, on, fromGoal);
      CHECK_FALSE(path.has_value());
      CHECK(on.outcome == SlideOutcome::Disk);
      CHECK(on.savedUnread);
      CHECK_FALSE(on.saved);
      CHECK_FALSE(on.resumed);
      CHECK_FALSE(on.diskHeldToFree);
      // the error tells which file and what is wrong with it, and that
      // the saved search is still there
      CHECK(on.error.find("could not be carried on") != std::string::npos);
      CHECK(on.error.find(name) != std::string::npos);
      CHECK(on.error.find(gone ? "is missing" : "is not a whole level file") !=
            std::string::npos);
      CHECK(on.error.find("It is kept as it is.") != std::string::npos);
      REQUIRE_FALSE(told.empty());
      CHECK(told.front().level == LogLevel::Error);
      CHECK(told.front().text == on.error);
      CHECK(listing() == before);
    }
    if (tried % 4 < 2) {
      // the file is back (a drive that had dropped out): carried on
      std::filesystem::copy_file(spare, files[i],
                                 std::filesystem::copy_options::overwrite_existing);
      SlideSearch back = runSearch();
      const std::optional<Separation> path = searched(g, back, fromGoal);
      REQUIRE(path.has_value());
      CHECK(back.resumed);
      CHECK_FALSE(back.savedUnread);
      CHECK(path->getMoves() == moves);
      checkGridPath(g, *path);
    } else {
      // a search that does not carry on throws the saved one away
      SlideSearch fresh = runSearch();
      fresh.resume = false;
      const std::optional<Separation> path = searched(g, fresh, fromGoal);
      REQUIRE(path.has_value());
      CHECK_FALSE(fresh.resumed);
      CHECK(path->getMoves() == moves);
      checkGridPath(g, *path);
      CHECK(fresh.visited == straight.visited);
    }
    CHECK(std::filesystem::is_empty(dir));
  }
  CHECK(tried >= 5);

  // A folder with files and no state is no saved search (a search that
  // was killed before it ever saved): cleared, and searched from the start.
  {
    stopped();
    const std::filesystem::path folder = levelFiles().front().parent_path();
    std::filesystem::remove(folder / "state.bin");
    SlideSearch on = runSearch();
    std::vector<LogLine> told;
    on.onLog = [&told](LogLevel level, const std::string& text) {
      told.push_back(LogLine{level, text});
    };
    const std::optional<Separation> path = searched(g, on, fromGoal);
    REQUIRE(path.has_value());
    CHECK_FALSE(on.resumed);
    CHECK_FALSE(on.savedUnread);
    CHECK(path->getMoves() == moves);
    CHECK(on.visited == straight.visited);
    CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
      return l.level == LogLevel::Info &&
             l.text.find("holds no saved search") != std::string::npos;
    }));
    CHECK(std::filesystem::is_empty(dir));
  }
  // A state that is there and cannot be read is a saved search still.
  {
    stopped();
    const std::filesystem::path folder = levelFiles().front().parent_path();
    std::filesystem::resize_file(folder / "state.bin", 5);
    const std::map<std::string, std::uintmax_t> before = listing();
    SlideSearch on = runSearch();
    CHECK_FALSE(searched(g, on, fromGoal).has_value());
    CHECK(on.outcome == SlideOutcome::Disk);
    CHECK(on.savedUnread);
    CHECK(on.error.find("state.bin") != std::string::npos);
    CHECK(listing() == before);
    SlideSearch fresh = runSearch();
    fresh.resume = false;
    const std::optional<Separation> path = searched(g, fresh, fromGoal);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == moves);
    CHECK(std::filesystem::is_empty(dir));
  }
  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(spareDir);
}

// A debug log (the app's Settings ▸ General): a line for each level done,
// how deep both ends are and the disk; each save, with the files the state
// names; the saved search read back, what its state.bin names.
TEST_CASE("sliding: the debug log tells each level, each save and the search read back",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidedebuglog");
  std::filesystem::remove_all(dir);
  auto runSearch = [&](std::vector<LogLine>& told) {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.onLog = [&told](LogLevel level, const std::string& text) {
      told.push_back(LogLine{level, text});
    };
    return s;
  };
  auto debugWith = [](const std::vector<LogLine>& told, const std::string& text) {
    return static_cast<std::size_t>(std::count_if(told.begin(), told.end(), [&](const LogLine& l) {
      return l.level == LogLevel::Debug && l.text.find(text) != std::string::npos;
    }));
  };
  const GridBoard g = settledBoard(37);
  std::vector<LogLine> firstTold;
  StopSource stop;
  SlideSearch first = runSearch(firstTold);
  first.stop = stop.token();
  std::uint64_t levels = 0;
  first.onDepth = [&](std::uint64_t at) {
    levels = at;
    if (at >= 10) {
      stop.request(StopKind::Pause);
    }
  };
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
  REQUIRE(first.saved);
  // a line for each level done: both ends' depths, the disk against its budget
  CHECK(debugWith(firstTold, " moves deep (the ") == levels);
  CHECK(debugWith(firstTold, "in all): the level has ") == levels);
  CHECK(debugWith(firstTold, "The disk the search may take in ") == 1);
  // the save, with the files its state names
  CHECK(debugWith(firstTold, "Saved the search's state in ") >= 1);
  CHECK(debugWith(firstTold, ".keys") >= 1);

  std::vector<LogLine> told;
  bool fromGoal = false;
  SlideSearch on = runSearch(told);
  CHECK(searched(g, on, fromGoal).has_value());
  CHECK(on.resumed);
  REQUIRE_FALSE(told.empty());
  // the key the search runs in; then what state.bin names, before the
  // search is carried on
  REQUIRE(told.size() >= 2);
  CHECK(told[0].level == LogLevel::Info);
  CHECK(told[0].text.starts_with("The slide search runs in 64-bit keys (its arrangements take "));
  CHECK(told[0].text.find("the narrowest key") != std::string::npos);
  CHECK(told[1].level == LogLevel::Debug);
  CHECK(told[1].text.find("Reading the saved search in ") != std::string::npos);
  CHECK(told[1].text.find("the start end ") != std::string::npos);
  CHECK(told[1].text.find(" arrangements visited, interval ") != std::string::npos);
  CHECK(debugWith(told, " moves deep (the ") >= 1);
  std::filesystem::remove_all(dir);
}

// What the Solver asks before Continue (decision 0033): a search saved with
// the stops of a cell as one goes on from the start alone, and where a new
// search would go from both ends that is worth saying.
TEST_CASE("sliding: a search saved from the start alone is told where both ends would do",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidealone");
  std::filesystem::remove_all(dir);
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };
  // stopped `depth` moves deep and saved
  auto stopped = [&](const GridBoard& b, SlideSearch first, std::uint64_t depth) {
    StopSource stop;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(b.puz, b.pr(), *b.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    REQUIRE(first.saved);
    return first;
  };
  auto alone = [&](const GridBoard& b, SlideSearch s) {
    s.workDir = dir.string();
    return savedFromStartAlone(b.puz, b.pr(), *b.start, s);
  };
  auto discard = [&](const GridBoard& b) {
    discardSaved(b.puz, b.pr(), dir.string());
    CHECK(std::filesystem::is_empty(dir));
  };

  GridBoard g = settledBoard(37);
  // nothing saved; saved from both ends, as this build saves it
  CHECK_FALSE(alone(g, gridSearch()));
  CHECK_FALSE(stopped(g, gridSearch(), 3).looseStops);
  CHECK_FALSE(alone(g, gridSearch()));
  discard(g);

  // Saved from the start alone with the stops of a cell as one, as the
  // builds before the two ends saved it: with codes for the stops only
  // (decision 0032's) and for every position (v1.8.1's).
  for (const bool plain : {false, true}) {
    INFO("plain keys " << plain);
    SlideSearch first = gridSearch();
    first.oneEnd = true;
    first.plainKeys = plain;
    CHECK(stopped(g, first, 3).looseStops);
    CHECK(alone(g, gridSearch()));
    // not for a search that would go from the start alone anyway, or that
    // is not this saved one
    SlideSearch other = gridSearch();
    other.oneEnd = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.gridStopsApart = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.partialNested = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.nested = false;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.grid = 0;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.resume = false;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.workDir = (dir / "elsewhere").string();
    CHECK_FALSE(savedFromStartAlone(g.puz, g.pr(), *g.start, other));

    // the paused solve's question: of the starts it keeps for its next run
    SlidingOptions options;
    options.nested = true;
    options.grid = 3;
    options.workDir = dir.string();
    Problem& pr = g.puz.getProblem(0);
    CHECK_FALSE(pendingFromStartAlone(g.puz, pr, options));
    pr.addPending(PendingAssembly{*g.start, false, 0, 0});
    CHECK(pendingFromStartAlone(g.puz, pr, options));
    options.oneEnd = true;
    CHECK_FALSE(pendingFromStartAlone(g.puz, pr, options));
    (void)pr.takePending();

    // asking changed nothing: it is carried on as it was begun, to the
    // moves of the search from both ends
    bool fromGoal = false;
    SlideSearch whole = gridSearch();
    const std::optional<Separation> both = searched(g, whole, fromGoal);
    REQUIRE(both.has_value());
    SlideSearch on = gridSearch();
    on.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, on, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(on.resumed);
    CHECK(on.looseStops);
    CHECK_FALSE(fromGoal);
    CHECK(rest->getMoves() == both->getMoves());
    CHECK(std::filesystem::is_empty(dir));
    CHECK_FALSE(alone(g, gridSearch()));
  }

  // saved from the start alone with the stops apart: carried on, it takes
  // up the goal end
  SlideSearch apart = gridSearch();
  apart.oneEnd = true;
  apart.gridStopsApart = true;
  CHECK_FALSE(stopped(g, apart, 3).looseStops);
  CHECK_FALSE(alone(g, gridSearch()));
  discard(g);

  // a piece that may end anywhere: searched from the start alone, saved or
  // not
  const GridBoard many = cupsAndUnits(true);
  CHECK(stopped(many, gridSearch(), 1).looseStops);
  CHECK_FALSE(alone(many, gridSearch()));
  discard(many);
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

// A search with packed keys, or in a key wider than 128 bits, is saved and
// carried on as any other; a search with other keys does not take it up.
TEST_CASE("sliding: a stopped search with packed or wide keys is saved and carried on",
          "[sliding][resume][parallel]") {
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  const std::pair<SlidePacking, unsigned int> forms[] = {
      {SlidePacking::Always, 0},
      {SlidePacking::Never, 256},
      {SlidePacking::Auto, 192},
  };
  for (const auto& [packing, bits] : forms) {
    INFO("bits " << bits);
    const std::filesystem::path dir = scratchDir("slidekeys");
    std::filesystem::remove_all(dir);
    SlideSearch form;
    form.packing = packing;
    form.minKeyBits = bits;
    form.threads = 2;

    SlideSearch straight = form;
    const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
    REQUIRE(whole.has_value());
    CHECK(straight.packedKeys == (packing != SlidePacking::Never));

    // stopped once it has visited a few thousand arrangements
    StopSource stop;
    std::atomic<std::uint64_t> progress{0};
    SlideSearch first = form;
    first.workDir = dir.string();
    first.progress = &progress;
    first.stop = stop.token();
    std::atomic<bool> done{false};
    std::thread watcher([&] {
      while (!done.load()) {
        if (progress.load() >= 4000) {
          stop.request(StopKind::Pause);
          return;
        }
        std::this_thread::yield();
      }
    });
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    done.store(true);
    watcher.join();
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    CHECK(first.visited < straight.visited);

    // a search with a code for each piece in the narrow key is another search
    SlideSearch other;
    other.workDir = dir.string();
    REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, other).has_value());
    CHECK_FALSE(other.resumed);
    CHECK(savedFolders(f.puz, f.pr(), dir.string()).size() == 1);

    SlideSearch second = form;
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(second.outcome == SlideOutcome::Found);
    CHECK(rest->getMoves() == 81);
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    CHECK(second.visited == straight.visited);
    CHECK(second.keyWidth == straight.keyWidth);
    CHECK(second.packedKeys == straight.packedKeys);
    CHECK(std::filesystem::is_empty(dir));
    std::filesystem::remove_all(dir);
  }
}

// ---------------------------------------------------------------------------
// The levels' files compressed, and a search that cannot write one
// (decision 0040).

#ifdef __SIZEOF_INT128__
namespace {

// Sorted keys with the gaps of a level: a few sizes of gap over and over,
// so that zstd has something to find, and now and then a leap.
std::vector<Wide> levelLikeKeys(std::size_t count, std::uint64_t seed) {
  std::vector<Wide> keys;
  keys.reserve(count);
  std::uint64_t x = seed;
  auto rnd = [&x]() {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
  };
  const std::uint64_t gaps[5] = {1, 1, 6, 320, 70001};
  Wide k = (static_cast<Wide>(0x1234) << 80) | 77;
  for (std::size_t i = 0; i < count; i++) {
    k += gaps[rnd() % 5];
    if (rnd() % 997 == 0) {
      k += static_cast<Wide>(rnd()) << 40;
    }
    keys.push_back(k);
  }
  return keys;
}

}  // namespace

// A file's chunks compressed at any level or not at all, large chunks and
// small: the same keys back, read from end to end, from any key on and
// looked up a few at a time; the file's size is what the set says it
// takes, and smaller the better it is compressed.
TEST_CASE("sliding: a key set's file is compressed in chunks", "[sliding]") {
  const std::filesystem::path dir = scratchDir("keychunks");
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(60000, 88172645463325252ULL);
  std::vector<Wide> in;
  for (std::size_t i = 0; i < keys.size(); i++) {
    if (i % 3 != 0) {
      in.push_back(keys[i]);
    }
  }
  std::uintmax_t plain = 0;
  std::uintmax_t fast = 0;
  std::uintmax_t well = 0;
  for (const int compression : {0, 1, 9, 19}) {
    for (const std::size_t chunk : {std::size_t{48}, std::size_t{4096}, levels::kChunkBytes}) {
      INFO("compression " << compression << ", chunks of " << chunk);
      levels::KeySet<Wide> set;
      REQUIRE(set.create(dir / "keys", levels::FileOptions{compression, chunk, nullptr}));
      REQUIRE(set.addAll(in.begin(), in.end()));
      REQUIRE(set.finish());
      CHECK(set.size() == in.size());
      const std::uintmax_t size = std::filesystem::file_size(dir / "keys");
      CHECK(set.bytes() == size);
      CHECK(set.rawBytes() > 0);
      if (chunk == levels::kChunkBytes) {
        (compression == 0 ? plain : compression == 1 ? fast : well) = size;
      }

      std::vector<Wide> back;
      REQUIRE(set.load(back));
      CHECK(back == in);

      // the index let go and read back: the same set
      set.release();
      CHECK_FALSE(set.indexed());
      REQUIRE(set.reload());
      CHECK(set.size() == in.size());
      CHECK(set.bytes() == size);
      CHECK(set.lastKey() == in.back());

      // from a key in the middle on, one that is in the set and one that is not
      for (const std::size_t from : {in.size() / 2, in.size() - 3}) {
        levels::KeySet<Wide>::Cursor c(set, in[from] - 1);
        Wide k = 0;
        std::size_t at = from;
        while (c.next(k)) {
          REQUIRE(at < in.size());
          REQUIRE(k == in[at]);
          at++;
        }
        CHECK_FALSE(c.failed());
        CHECK(at == in.size());
      }

      // a few keys looked up, far apart: those put in are found, the others not
      std::vector<Wide> look;
      std::vector<bool> isIn;
      for (std::size_t i = 0; i < keys.size(); i += 131) {
        look.push_back(keys[i]);
        isIn.push_back(i % 3 != 0);
      }
      std::vector<char> found(look.size(), 0);
      levels::KeySet<Wide>::Reader reader(set, std::size_t{1} << 12);
      REQUIRE(set.find(look.data(), look.size(), found.data(), reader));
      for (std::size_t i = 0; i < look.size(); i++) {
        CHECK((found[i] != 0) == isIn[i]);
      }
    }
  }
  // gaps that repeat: a good part of the file goes, more at the higher level
  CHECK(fast * 10 < plain * 8);
  CHECK(well <= fast);
  std::filesystem::remove_all(dir);
}

// Sets joined without decoding their keys, whatever each is kept in: a
// file's chunks go into a file as they are (compressed at another level,
// in chunks of another size, or not at all), blocks in memory are gathered
// into chunks, and a file's chunks are decompressed into memory.
TEST_CASE("sliding: key sets are joined whatever they are kept in", "[sliding]") {
  const std::filesystem::path dir = scratchDir("keyjoin");
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(9000, 4242424242ULL);
  // a few keys added first, then three parts
  const std::size_t cuts[5] = {0, 200, 3000, 3001, keys.size()};
  struct Kind {
    bool file;
    int compression;
    std::size_t chunk;
  };
  const Kind kinds[4] = {{false, 0, 0}, {true, 0, 300}, {true, 1, 64}, {true, 9, levels::kChunkBytes}};
  int name = 0;
  for (const Kind& target : kinds) {
    for (int mix = 0; mix < 4; mix++) {
      INFO("target " << (target.file ? "file" : "memory") << " " << target.compression << ", mix "
                     << mix);
      std::vector<std::unique_ptr<levels::KeySet<Wide>>> parts;
      for (int p = 1; p < 4; p++) {
        const Kind& kind = kinds[(mix + p) % 4];
        parts.push_back(std::make_unique<levels::KeySet<Wide>>());
        REQUIRE(parts.back()->create(
            kind.file ? dir / ("part" + std::to_string(name++)) : std::filesystem::path(),
            levels::FileOptions{kind.compression, kind.chunk, nullptr}));
        REQUIRE(parts.back()->addAll(keys.begin() + static_cast<std::ptrdiff_t>(cuts[p]),
                                     keys.begin() + static_cast<std::ptrdiff_t>(cuts[p + 1])));
        REQUIRE(parts.back()->finish());
      }
      levels::KeySet<Wide> joined;
      REQUIRE(joined.create(
          target.file ? dir / ("joined" + std::to_string(name++)) : std::filesystem::path(),
          levels::FileOptions{target.compression, target.chunk, nullptr}));
      REQUIRE(joined.addAll(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(cuts[1])));
      for (const std::unique_ptr<levels::KeySet<Wide>>& part : parts) {
        REQUIRE(joined.appendSet(*part));
      }
      REQUIRE(joined.finish());
      CHECK(joined.size() == keys.size());
      CHECK(joined.lastKey() == keys.back());
      std::vector<Wide> back;
      REQUIRE(joined.load(back));
      CHECK(back == keys);
      if (target.file) {
        CHECK(joined.bytes() == std::filesystem::file_size(joined.path()));
        // read back from the file alone
        levels::KeySet<Wide> again;
        const std::filesystem::path kept = dir / "kept";
        std::filesystem::copy_file(joined.path(), kept,
                                   std::filesystem::copy_options::overwrite_existing);
        REQUIRE(again.open(kept));
        back.clear();
        REQUIRE(again.load(back));
        CHECK(back == keys);
      }
    }
  }
  std::filesystem::remove_all(dir);
}

// A file cut short, one whose chunk is damaged, one of a later version and
// one that is not there: none is read as keys, and each says what it is.
TEST_CASE("sliding: a damaged key set file is told", "[sliding]") {
  const std::filesystem::path dir = scratchDir("keydamage");
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(20000, 31337);
  const std::filesystem::path whole = dir / "whole";
  {
    levels::KeySet<Wide> set;
    REQUIRE(set.create(whole));
    REQUIRE(set.addAll(keys.begin(), keys.end()));
    REQUIRE(set.finish());
    set.keepFile();
  }
  const std::uintmax_t size = std::filesystem::file_size(whole);
  auto copyOf = [&](const char* name) {
    const std::filesystem::path p = dir / name;
    std::filesystem::copy_file(whole, p);
    return p;
  };
  auto poke = [](const std::filesystem::path& p, std::uintmax_t at, const std::string& bytes) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(at));
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(f.good());
  };
  {
    levels::KeySet<Wide> set;
    REQUIRE(set.open(whole));
    CHECK_FALSE(set.older());
    CHECK(set.fault() == levels::SetFault::None);
    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == keys);
    set.keepFile();
  }
  {
    // cut short: no footer where one should be
    const std::filesystem::path p = copyOf("short");
    std::filesystem::resize_file(p, size - 1);
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Damaged);
    // not opened, not deleted
    CHECK(std::filesystem::file_size(p) == size - 1);
  }
  {
    // a byte of a chunk changed: the index reads, the keys do not (each
    // chunk carries a checksum)
    const std::filesystem::path p = copyOf("chunk");
    poke(p, 100, std::string(1, '\x5a'));
    poke(p, 101, std::string(1, '\xa5'));
    levels::KeySet<Wide> set;
    REQUIRE(set.open(p));
    std::vector<Wide> back;
    CHECK_FALSE(set.load(back));
    set.keepFile();
  }
  {
    // the ending of a format after this one: told apart from a damaged file
    const std::filesystem::path p = copyOf("later");
    poke(p, size - 8, "BTSLKEY4");
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Newer);
  }
  {
    // the ending of the format before on a file that is not one
    const std::filesystem::path p = copyOf("notolder");
    poke(p, size - 8, "BTSLKEY2");
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Damaged);
  }
  {
    // no file: the system's reason is kept
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(dir / "nothing"));
    CHECK(set.fault() == levels::SetFault::Unread);
    CHECK(set.faultError() == ENOENT);
  }
  std::filesystem::remove_all(dir);
}

namespace {

// A key set's file as the versions up to 1.9 wrote it (BTSLKEY2): the
// blocks as they are, the first keys, the offsets, three numbers, the last
// key and the magic.
template <class Key>
void writeOlderSet(const std::filesystem::path& p, const std::vector<Key>& keys) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  std::vector<Key> firsts;
  std::vector<std::uint64_t> offsets{0};
  std::vector<std::uint8_t> block;
  for (std::size_t i = 0; i < keys.size(); i += levels::kBlockKeys) {
    const std::size_t n = std::min<std::size_t>(levels::kBlockKeys, keys.size() - i);
    block.clear();
    std::uint8_t head[levels::kMaxVarint<unsigned int>];
    unsigned int len = levels::putVarint(head, static_cast<unsigned int>(n));
    block.insert(block.end(), head, head + len);
    for (std::size_t k = 1; k < n; k++) {
      std::uint8_t gap[levels::kMaxVarint<Key>];
      len = levels::putVarint(gap, static_cast<Key>(keys[i + k] - keys[i + k - 1]));
      block.insert(block.end(), gap, gap + len);
    }
    out.write(reinterpret_cast<const char*>(block.data()),
              static_cast<std::streamsize>(block.size()));
    firsts.push_back(keys[i]);
    offsets.push_back(offsets.back() + block.size());
  }
  out.write(reinterpret_cast<const char*>(firsts.data()),
            static_cast<std::streamsize>(firsts.size() * sizeof(Key)));
  out.write(reinterpret_cast<const char*>(offsets.data()),
            static_cast<std::streamsize>(offsets.size() * sizeof(std::uint64_t)));
  const std::uint64_t tail[3] = {keys.size(), firsts.size(), offsets.back()};
  out.write(reinterpret_cast<const char*>(tail), sizeof(tail));
  const Key last = keys.empty() ? Key{0} : keys.back();
  out.write(reinterpret_cast<const char*>(&last), sizeof(Key));
  out.write(levels::kOlderSetMagic, sizeof(levels::kOlderSetMagic));
  REQUIRE(out.good());
}

// The last eight bytes of a file: a key set's say which version wrote it.
std::string fileEnding(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  in.seekg(-8, std::ios::end);
  std::string ending(8, '\0');
  in.read(ending.data(), 8);
  return in.good() ? ending : std::string();
}

// A key set's file of this version written again as the versions up to 1.9
// wrote theirs.
template <class Key>
void asOlderVersion(const std::filesystem::path& p) {
  std::vector<Key> keys;
  {
    levels::KeySet<Key> set;
    REQUIRE(set.open(p));
    REQUIRE(set.load(keys));
    set.keepFile();
  }
  writeOlderSet(p, keys);
}

// Every level file of the saved searches in `dir` written again as the
// versions up to 1.9 wrote theirs (the search ran in keys of `keyWidth`
// bits); how many there were.
std::size_t asOlderVersion(const std::filesystem::path& dir, unsigned int keyWidth) {
  std::size_t files = 0;
  for (const auto& folder : std::filesystem::directory_iterator(dir)) {
    for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
      if (e.path().extension() != ".keys") {
        continue;
      }
      if (keyWidth == 64) {
        asOlderVersion<std::uint64_t>(e.path());
      } else {
        REQUIRE(keyWidth == 128);
        asOlderVersion<Wide>(e.path());
      }
      files++;
    }
  }
  return files;
}

// The endings of the level files of the saved searches in `dir`.
std::map<std::string, std::string> levelEndings(const std::filesystem::path& dir) {
  std::map<std::string, std::string> endings;
  for (const auto& folder : std::filesystem::directory_iterator(dir)) {
    for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
      if (e.path().extension() == ".keys") {
        endings[e.path().filename().string()] = fileEnding(e.path());
      }
    }
  }
  return endings;
}

}  // namespace

// A file of the versions up to 1.9 (its blocks not compressed, no chunks)
// is read as it is: every key, a key looked up, a cursor from the middle.
// Appended to a file of this version it is written as that file is, in
// compressed chunks, the same keys.
TEST_CASE("sliding: a key set file of an earlier version is read, and written again",
          "[sliding]") {
  const std::filesystem::path dir = scratchDir("keyolder");
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{128},
                                  std::size_t{129}, std::size_t{200000}}) {
    INFO(count << " keys");
    const std::vector<Wide> keys = levelLikeKeys(count, 2463534242ULL + count);
    const std::filesystem::path old = dir / "old.keys";
    writeOlderSet(old, keys);
    const std::uintmax_t oldSize = std::filesystem::file_size(old);
    levels::KeySet<Wide> set;
    REQUIRE(set.open(old));
    CHECK(set.older());
    CHECK(set.size() == keys.size());
    CHECK(set.bytes() == oldSize);
    CHECK(set.lastKey() == (keys.empty() ? Wide{0} : keys.back()));
    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == keys);
    // the index let go and read back
    set.release();
    REQUIRE(set.reload());
    CHECK(set.older());
    if (count > 1000) {
      // keys looked up, there and not there
      std::vector<Wide> cand = {keys[3], keys[3] + 1, keys[count / 2], keys.back()};
      std::sort(cand.begin(), cand.end());
      std::vector<char> found(cand.size(), 0);
      levels::KeySet<Wide>::Reader reader(set, std::size_t{1} << 12);
      REQUIRE(set.find(cand.data(), cand.size(), found.data(), reader));
      for (std::size_t i = 0; i < cand.size(); i++) {
        CHECK((found[i] != 0) == std::binary_search(keys.begin(), keys.end(), cand[i]));
      }
      // a cursor from the middle on
      levels::KeySet<Wide>::Cursor cursor(set, keys[count / 2]);
      Wide k = 0;
      for (std::size_t i = count / 2; i < count; i++) {
        REQUIRE(cursor.next(k));
        REQUIRE(k == keys[i]);
      }
      CHECK_FALSE(cursor.next(k));
      CHECK_FALSE(cursor.failed());
    }
    // written again as this version writes: after keys of its own, too
    for (const bool after : {false, true}) {
      const std::filesystem::path fresh = dir / "fresh.keys";
      levels::KeySet<Wide> made;
      REQUIRE(made.create(fresh));
      std::vector<Wide> want;
      if (after && !keys.empty() && keys.front() > 5) {
        want = {1, 2, 5};
        REQUIRE(made.addAll(want.begin(), want.end()));
      }
      want.insert(want.end(), keys.begin(), keys.end());
      std::uint64_t heard = 0;
      REQUIRE(made.appendOlder(set, [&heard](std::uint64_t bytes) {
        heard += bytes;
        return true;
      }));
      REQUIRE(made.finish());
      CHECK_FALSE(made.older());
      CHECK(made.size() == want.size());
      CHECK(fileEnding(fresh) == std::string(levels::kSetMagic, sizeof(levels::kSetMagic)));
      if (count > 0) {
        CHECK(heard > 0);
      }
      if (count > 1000) {
        CHECK(made.bytes() < oldSize);
      }
      std::vector<Wide> again;
      REQUIRE(made.load(again));
      CHECK(again == want);
      // and opened anew from its file
      made.keepFile();
      levels::KeySet<Wide> opened;
      REQUIRE(opened.open(fresh));
      CHECK_FALSE(opened.older());
      REQUIRE(opened.load(again));
      CHECK(again == want);
    }
    if (count > 1000) {
      // the work ended from outside: nothing more is read
      levels::KeySet<Wide> made;
      REQUIRE(made.create(dir / "cut.keys"));
      unsigned int steps = 0;
      CHECK_FALSE(made.appendOlder(set, [&steps](std::uint64_t) { return ++steps < 2; }));
      CHECK(steps == 2);
      // a file cut short is not opened as an older set either
      set.keepFile();
      std::filesystem::resize_file(old, oldSize - 9);
      levels::KeySet<Wide> cut;
      CHECK_FALSE(cut.open(old));
    } else {
      set.keepFile();
    }
  }
  std::filesystem::remove_all(dir);
}

// A search saved by a version up to 1.9 (its level files not compressed)
// is carried on by this one: every file of it is written again as this
// version writes its files, under its own name, and the search goes on to
// the path a search with no stops finds. With nested slides (the visited
// sets) and without (the levels kept), from both ends and from one.
TEST_CASE("sliding: a search saved by an earlier version is carried on, its files compressed",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slideolder");
  std::filesystem::remove_all(dir);
  const GridBoard g = settledBoard(37);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  struct Kind {
    const char* name;
    std::function<SlideSearch()> make;
    std::function<std::optional<Separation>(SlideSearch&)> run;
    std::uint64_t stopAt;
  };
  const std::vector<Kind> kinds = {
      {"nested slides, both ends",
       [&] {
         SlideSearch s = nestedSearch(3);
         s.threads = 2;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(g.puz, g.pr(), *g.start, s); }, 10},
      {"the levels kept, one end",
       [&] {
         SlideSearch s;
         s.oneEnd = true;
         s.threads = 3;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(f.puz, f.pr(), *f.start, s); }, 40},
      {"the levels kept, both ends",
       [&] {
         SlideSearch s;
         s.threads = 3;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(f.puz, f.pr(), *f.start, s); }, 40},
  };
  const std::string older(levels::kOlderSetMagic, sizeof(levels::kOlderSetMagic));
  const std::string current(levels::kSetMagic, sizeof(levels::kSetMagic));
  for (const Kind& kind : kinds) {
    INFO(kind.name);
    SlideSearch straight = kind.make();
    const std::optional<Separation> whole = kind.run(straight);
    REQUIRE(whole.has_value());
    REQUIRE(whole->getMoves() > kind.stopAt);
    REQUIRE(std::filesystem::is_empty(dir));
    // stopped part of the way, saved; then its files as an earlier
    // version wrote them
    auto savedByOlder = [&] {
      StopSource stop;
      SlideSearch first = kind.make();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= kind.stopAt) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(kind.run(first).has_value());
      REQUIRE(first.outcome == SlideOutcome::Stopped);
      REQUIRE(first.saved);
      const std::size_t files = asOlderVersion(dir, first.keyWidth);
      for (const auto& [name, ending] : levelEndings(dir)) {
        REQUIRE(ending == older);
      }
      return files;
    };
    // what the search stopped there and carried on comes to, its files as
    // this version wrote them
    std::uint64_t visited = 0;
    {
      StopSource stop;
      SlideSearch first = kind.make();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= kind.stopAt) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(kind.run(first).has_value());
      REQUIRE(first.saved);
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      REQUIRE(on.resumed);
      CHECK(on.convertedFiles == 0);
      visited = on.visited;
      REQUIRE(std::filesystem::is_empty(dir));
    }

    // carried on to the end
    {
      const std::size_t files = savedByOlder();
      REQUIRE(files >= 3);
      SlideSearch on = kind.make();
      std::vector<LogLine> told;
      on.onLog = [&told](LogLevel level, const std::string& text) {
        told.push_back(LogLine{level, text});
      };
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == files);
      CHECK(on.unconvertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(on.visited == visited);
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Info &&
               l.text.find("was written by an earlier version of the program") !=
                   std::string::npos;
      }));
      CHECK(std::none_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning || l.level == LogLevel::Error;
      }));
      CHECK(std::filesystem::is_empty(dir));
    }

    // carried on for one level and saved again: every file in the folder
    // is this version's, under the names the state had
    {
      const std::size_t files = savedByOlder();
      const std::map<std::string, std::string> before = levelEndings(dir);
      StopSource point;
      point.request(StopKind::Pause);
      SlideSearch second = kind.make();
      second.savePoint = point.token();
      CHECK_FALSE(kind.run(second).has_value());
      REQUIRE(second.outcome == SlideOutcome::Stopped);
      REQUIRE(second.saved);
      CHECK(second.resumed);
      CHECK(second.convertedFiles == files);
      const std::map<std::string, std::string> after = levelEndings(dir);
      REQUIRE_FALSE(after.empty());
      for (const auto& [name, ending] : after) {
        INFO(name);
        CHECK(ending == current);
      }
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }

    // stopped before the files are written again: they stay as they
    // were, the search is saved as it was, and the next carry-on writes
    // them (with nested slides a level that waited to go into the visited
    // sets has gone in: a new file, this version's, for some of the old)
    {
      const std::size_t files = savedByOlder();
      const std::map<std::string, std::string> before = levelEndings(dir);
      StopSource stop;
      stop.request(StopKind::Pause);
      SlideSearch second = kind.make();
      second.stop = stop.token();
      CHECK_FALSE(kind.run(second).has_value());
      REQUIRE(second.outcome == SlideOutcome::Stopped);
      REQUIRE(second.saved);
      CHECK(second.convertedFiles == 0);
      CHECK(second.unconvertedFiles == files);
      std::size_t left = 0;
      for (const auto& [name, ending] : levelEndings(dir)) {
        INFO(name);
        if (before.contains(name)) {
          CHECK(ending == older);
          left++;
        } else {
          CHECK(ending == current);
        }
      }
      REQUIRE(left > 0);
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == left);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }

    // no room to write them again (every write fails): they are read as
    // they are, nothing of the saved search is lost, and the next carry-on
    // with room writes them
    {
      const std::size_t files = savedByOlder();
      SlideSearch second = kind.make();
      second.failWriteAt = 1;
      std::vector<LogLine> told;
      second.onLog = [&told](LogLevel level, const std::string& text) {
        told.push_back(LogLine{level, text});
      };
      CHECK_FALSE(kind.run(second).has_value());
      CHECK(second.outcome == SlideOutcome::Disk);
      // carried on and stopped at its first write; or, with nested slides,
      // not carried on at all (a level waits to be written into the
      // visited sets) and kept
      CHECK(second.resumed != second.savedUnread);
      CHECK(second.convertedFiles == 0);
      if (second.resumed) {
        CHECK(second.unconvertedFiles == files);
      }
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning &&
               l.text.find("The search reads them as they are") != std::string::npos;
      }));
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles > 0);
      CHECK(on.unconvertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }
  }
  std::filesystem::remove_all(dir);
}
#endif

// The files of the levels compressed well, fast or not at all: the same
// search, level for level, and a search saved with one is carried on with
// another.
TEST_CASE("sliding: the levels' files compressed or not, the same search",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidecompress");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  straight.oneEnd = true;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());
  CHECK(straight.diskBytes == 0);

  std::uint64_t plain = 0;
  for (const int compression : {0, 1, 9}) {
    INFO("compression " << compression);
    SlideSearch s;
    s.oneEnd = true;
    s.threads = 3;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.compression = compression;
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, s);
    REQUIRE(path.has_value());
    CHECK(placesAlong(*path) == placesAlong(*whole));
    CHECK(s.visited == straight.visited);
    CHECK(s.diskBytes > 0);
    if (compression == 0) {
      plain = s.diskBytes;
    } else {
      CHECK(s.diskBytes < plain);
    }
    CHECK(std::filesystem::is_empty(dir));
  }

  // stopped between two levels with its files as they are, carried on
  // with them compressed
  StopSource point;
  point.request(StopKind::Pause);
  SlideSearch first;
  first.oneEnd = true;
  first.workDir = dir.string();
  first.memoryLevelBytes = 1;
  first.compression = 0;
  first.savePoint = point.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  REQUIRE(first.saved);
  SlideSearch second;
  second.oneEnd = true;
  second.workDir = dir.string();
  second.memoryLevelBytes = 1;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A write that fails while a level is made (a disk that fills) does not
// cost the search: the files of the level being made go, there is room
// again, and the search is saved at its last whole level for a search of
// the same start to carry on, wherever in its work the write failed.
TEST_CASE("sliding: a search that cannot write a file is saved at its last whole level",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidewrite");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  unsigned int failures = 0;
  unsigned int finished = 0;
  for (std::uint64_t at = 3000; finished == 0; at += at / 2) {
    INFO("the write past " << at << " bytes fails");
    REQUIRE(at < (std::uint64_t{1} << 40));
    std::vector<LogLine> told;
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.memoryLevelBytes = 1;
    first.failWriteAt = at;
    first.failWriteOnce = true;
    first.onLog = [&told](LogLevel level, const std::string& text) {
      if (level != LogLevel::Debug) {
        told.push_back(LogLine{level, text});
      }
    };
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, first);
    if (path.has_value()) {
      // the search wrote less than that: nothing failed
      CHECK(first.outcome == SlideOutcome::Found);
      CHECK(first.visited == straight.visited);
      finished++;
    } else {
      failures++;
      REQUIRE(first.outcome == SlideOutcome::Disk);
      REQUIRE(first.saved);
      CHECK(first.diskHeldToFree);
      CHECK(first.error.find("Could not write the search's files") != std::string::npos);
      CHECK(first.visited < straight.visited);
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning &&
               l.text.find("The search is saved") != std::string::npos;
      }));
      CHECK_FALSE(std::filesystem::is_empty(dir));

      SlideSearch second;
      second.threads = 3;
      second.workDir = dir.string();
      second.memoryLevelBytes = 1;
      const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
      REQUIRE(rest.has_value());
      CHECK(second.resumed);
      CHECK(second.outcome == SlideOutcome::Found);
      CHECK(rest->getMoves() == 81);
      CHECK(placesAlong(*rest) == placesAlong(*whole));
      CHECK(second.visited == straight.visited);
    }
    CHECK(std::filesystem::is_empty(dir));
  }
  // the write failed at many places of the search's work before it got through
  CHECK(failures >= 8);
  std::filesystem::remove_all(dir);
}

// When the disk stays full, so that the search cannot save itself either,
// it is left as it was last saved while it ran: the files that save named
// are all still there. Without such a save there is nothing to keep.
TEST_CASE("sliding: a search that cannot save itself is left as it was last saved",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidefull");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // its levels in memory, so that only its saves write, one after every
  // level whatever a save takes (with SaveCadence's wait, 50 times the
  // last save, a machine with a slower disk ended the search before its
  // second save and never filled the disk); the disk fills in the middle
  // of one of them
  {
    std::vector<LogLine> told;
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.autosaveFloorSeconds = 0;
    first.autosaveEveryLevel = true;
    first.failWriteAt = 60000;
    first.onLog = [&told](LogLevel level, const std::string& text) {
      if (level != LogLevel::Debug) {
        told.push_back(LogLine{level, text});
      }
    };
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    REQUIRE(first.saved);
    CHECK(first.diskHeldToFree);
    CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
      return l.level == LogLevel::Warning &&
             l.text.find("The search is saved") != std::string::npos &&
             l.text.find("as it was last saved while it ran") != std::string::npos;
    }));
    CHECK_FALSE(std::filesystem::is_empty(dir));

    SlideSearch second;
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(rest->getMoves() == 81);
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    CHECK(second.visited == straight.visited);
    CHECK(std::filesystem::is_empty(dir));
  }

  // the disk full from the first save on: no save to go back to, and the
  // levels in memory cannot be written, so nothing is kept
  {
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.autosaveFloorSeconds = 0;
    first.failWriteAt = 1;
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    CHECK_FALSE(first.saved);
    CHECK(first.error.find("Could not write the search's files") != std::string::npos);
    CHECK(std::filesystem::is_empty(dir));
  }
  // and when not even the first file can be written
  {
    SlideSearch first;
    first.workDir = dir.string();
    first.memoryLevelBytes = 1;
    first.failWriteAt = 1;
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    CHECK_FALSE(first.saved);
    CHECK(std::filesystem::is_empty(dir));
  }
  std::filesystem::remove_all(dir);
}

#ifndef _WIN32
// A save whose write fails only once its file is open (the disk full when
// the bytes still buffered go out at the close) is a failed save (cleanup
// item 4.1): the last good state.bin stays as it was, never replaced by a
// file cut short, and the search is left as that save has it, to be
// carried on to the same path. Before, the error at the close was lost:
// the cut file took state.bin's place, the run said it was saved, and the
// next Continue could not read it.
TEST_CASE("sliding: a state.bin whose write fails leaves the last good one",
          "[sliding][resume][parallel]") {
  const std::filesystem::path dir = scratchDir("slidestatefull");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  auto stateFile = [&dir] {
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
      if (e.path().filename() == "state.bin") {
        return e.path();
      }
    }
    return std::filesystem::path();
  };
  auto bytesOf = [](const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };

  // Every set in a file, so that a save writes its state.bin alone, and a
  // save after every level. After a few levels the disk fills (files may
  // hold 16 bytes, fewer than any state.bin) and a save point is asked
  // for: the save of the stop is the write that fails.
  constexpr unsigned int kFullAt = 12;
  std::optional<test::FileSizeLimit> full;
  std::string lastGood;
  StopSource point;
  unsigned int levels = 0;
  std::vector<LogLine> told;
  SlideSearch first;
  first.threads = 2;
  first.workDir = dir.string();
  first.memoryLevelBytes = 1;
  first.autosaveFloorSeconds = 0;
  first.autosaveEveryLevel = true;
  first.savePoint = point.token();
  first.onLevel = [&](const std::string&) {
    if (++levels == kFullAt) {
      lastGood = bytesOf(stateFile());
      full.emplace(16);
      point.request(StopKind::Pause);
    }
  };
  first.onLog = [&told](LogLevel level, const std::string& text) {
    told.push_back(LogLine{level, text});
  };
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  full.reset();
  REQUIRE(levels == kFullAt);
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  REQUIRE(lastGood.size() > 16);
  // the save failed, and the search is left as the one before it saved it
  CHECK(first.saved);
  CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
    return l.text.find("The search's state could not be written") != std::string::npos;
  }));
  CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
    return l.text.find("as it was last saved while it ran") != std::string::npos;
  }));
  const std::filesystem::path state = stateFile();
  REQUIRE_FALSE(state.empty());
  CHECK(bytesOf(state) == lastGood);
  // and the failed save's temporary file is gone
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    CHECK(e.path().extension() != ".tmp");
  }

  // carried on from that save to the same path
  SlideSearch second;
  second.threads = 3;
  second.workDir = dir.string();
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}
#endif

// What making a level took on the disk at its most is measured, for what
// the next will take; and the levels kept for tracing the path back are
// thinned once they take half the disk the search may use, the files of
// those thinned out gone at once though the last save named them.
TEST_CASE("sliding: the levels kept are thinned at half the disk budget",
          "[sliding][parallel]") {
  const std::filesystem::path dir = scratchDir("slidethin");
  std::filesystem::remove_all(dir);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  auto search = [&] {
    SlideSearch s;
    s.oneEnd = true;
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.autosaveFloorSeconds = 0;
    return s;
  };
  SlideSearch all = search();
  all.measureLevelBytes = 1;
  std::string levelLine;
  all.onLog = [&levelLine](LogLevel level, const std::string& text) {
    if (level == LogLevel::Debug && text.find("making it took") != std::string::npos) {
      levelLine = text;
    }
  };
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, all);
  REQUIRE(whole.has_value());
  CHECK(all.levelInterval == 1);
  // a level's runs and merges take more than the level
  CHECK(all.levelWorkRatio > 1);
  CHECK(levelLine.find("times a level's bytes") != std::string::npos);
  // too small to measure unless told: the search goes by the fixed share
  SlideSearch unmeasured = search();
  REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, unmeasured).has_value());
  CHECK(unmeasured.levelWorkRatio == 0);

  SlideSearch tight = search();
  tight.diskBudget = all.diskBytes;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, tight);
  REQUIRE(path.has_value());
  CHECK(tight.outcome == SlideOutcome::Found);
  CHECK(tight.levelInterval >= 2);
  // as short a path; the levels searched again across the gaps count too
  CHECK(path->getMoves() == whole->getMoves());
  CHECK(tight.visited >= all.visited);
  CHECK(tight.diskBytes < all.diskBytes);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

#ifdef __SIZEOF_INT128__
// A set of more keys than 2^33, as a level of the largest searches has,
// written as the threads of a merge write its parts and joined; and one
// read of more than 2 GB (macOS reads no more than that at a time). About
// 4 GB of disk, as much memory and a few minutes: hidden.
TEST_CASE("sliding: a key set of more than 2^33 keys", "[.huge][sliding]") {
  const std::filesystem::path dir = scratchDir("keyhuge");
  std::filesystem::create_directories(dir);
  using Set = levels::KeySet<Wide>;

  {
    // a file with nothing in it but a mark near its end, read in one call
    const std::filesystem::path big = dir / "big";
    const std::uint64_t size = 2600000000ULL;
    const std::uint64_t markAt = size - 1000;
    {
      std::ofstream f(big, std::ios::binary);
      f.seekp(static_cast<std::streamoff>(markAt));
      f.write("mark", 4);
      REQUIRE(f.good());
    }
    std::filesystem::resize_file(big, size);
    levels::ReadFile f;
    REQUIRE(f.open(big));
    std::vector<char> all(size);
    REQUIRE(f.readAt(0, all.data(), all.size()));
    CHECK(std::string(all.data() + markAt, 4) == "mark");
    CHECK(all[markAt - 1] == 0);
    std::filesystem::remove(big);
  }

  const std::uint64_t half = (std::uint64_t{1} << 32) + 300000000ULL;
  const std::uint64_t total = 2 * half;
  REQUIRE(total > (std::uint64_t{1} << 33));
  const Wide base = (static_cast<Wide>(1) << 100) + 5;
  // gaps of 3 and 1 in turn
  auto keyAt = [&](std::uint64_t i) { return base + static_cast<Wide>(2) * i + (i & 1); };

  Set joined;
  {
    Set parts[2];
    std::atomic<bool> ok{true};
    std::vector<std::thread> writers;
    for (int p = 0; p < 2; p++) {
      writers.emplace_back([&, p] {
        Set& set = parts[p];
        bool good = set.create(dir / ("part" + std::to_string(p)),
                               levels::FileOptions{p == 0 ? 1 : 9, levels::kChunkBytes, nullptr});
        const std::uint64_t from = p == 0 ? 0 : half;
        for (std::uint64_t i = from; good && i < from + half; i++) {
          good = set.add(keyAt(i));
        }
        if (!good || !set.finish()) {
          ok.store(false);
        }
      });
    }
    for (std::thread& t : writers) {
      t.join();
    }
    REQUIRE(ok.load());
    CHECK(parts[0].size() == half);
    REQUIRE(joined.create(dir / "joined"));
    REQUIRE(joined.appendSet(parts[0]));
    REQUIRE(joined.appendSet(parts[1]));
    REQUIRE(joined.finish());
  }
  CHECK(joined.size() == total);
  CHECK(joined.lastKey() == keyAt(total - 1));
  CHECK(joined.bytes() == std::filesystem::file_size(joined.path()));
  joined.release();
  REQUIRE(joined.reload());
  CHECK(joined.size() == total);
  CHECK(joined.lastKey() == keyAt(total - 1));
  const std::size_t blocks = joined.blocks();
  CHECK(blocks == 2 * ((half + levels::kBlockKeys - 1) / levels::kBlockKeys));

  // from keys on either side of 2^32 and 2^33, of the join and of the end
  for (const std::uint64_t from :
       {std::uint64_t{0}, (std::uint64_t{1} << 32) - 70, half - 3, (std::uint64_t{1} << 33) - 70,
        total - 500}) {
    INFO("from key " << from);
    Set::Cursor c(joined, keyAt(from));
    Wide k = 0;
    for (std::uint64_t i = from; i < std::min(total, from + 1000); i++) {
      REQUIRE(c.next(k));
      REQUIRE(k == keyAt(i));
    }
    CHECK_FALSE(c.failed());
  }
  {
    // looked up: keys are found, the numbers between two keys are not
    const std::uint64_t at[4] = {10, (std::uint64_t{1} << 32) + 10, (std::uint64_t{1} << 33) + 10,
                                 total - 2};
    std::vector<Wide> look;
    for (const std::uint64_t i : at) {
      look.push_back(keyAt(i));
      look.push_back(keyAt(i) + 1);
    }
    std::vector<char> found(look.size(), 0);
    Set::Reader reader(joined, std::size_t{1} << 12);
    REQUIRE(joined.find(look.data(), look.size(), found.data(), reader));
    for (std::size_t i = 0; i < look.size(); i++) {
      CHECK((found[i] != 0) == (i % 2 == 0));
    }
  }

  // every key, each thread a range of blocks: none missing, none wrong
  const unsigned int threads = 8;
  std::atomic<std::uint64_t> counted{0};
  std::atomic<std::uint64_t> wrong{0};
  std::vector<std::thread> readers;
  for (unsigned int t = 0; t < threads; t++) {
    readers.emplace_back([&, t] {
      Set::Reader reader(joined);
      std::vector<Wide> keys;
      std::uint64_t n = 0;
      std::uint64_t bad = 0;
      for (std::size_t b = blocks * t / threads; b < blocks * (t + 1) / threads; b++) {
        if (!reader.block(b, keys)) {
          bad++;
          continue;
        }
        // a key says which it is: twice its number, and one more when odd
        std::uint64_t i = static_cast<std::uint64_t>((keys.front() - base) / 2);
        for (const Wide k : keys) {
          bad += k != keyAt(i++) ? 1 : 0;
        }
        n += keys.size();
      }
      counted.fetch_add(n);
      wrong.fetch_add(bad);
    });
  }
  for (std::thread& t : readers) {
    t.join();
  }
  CHECK(counted.load() == total);
  CHECK(wrong.load() == 0);
  joined.remove();
  std::filesystem::remove_all(dir);
}
#endif
