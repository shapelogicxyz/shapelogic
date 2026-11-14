// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles: the tray model, the slide search, routes and the sliding
// solve. Ported from the original's test/test_sliding.cpp, every case in its
// order (the solve thread's cases run a SolveSession; its environment
// switches are the search's options; its scratch folders are this
// process's own), followed by new cases in test_sliding_session.cpp.
#include "../model/model_test_helpers.h"

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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bt {

// The seam of the original's "a shorter path keeps the solution number",
// which called the solve thread's callback with starts of its choosing.
struct SolveSessionTestAccess {
  // The session as a run with `options` would have it, without a run.
  static void prime(SolveSession& s, const SolveOptions& options) {
    s.runOptions_ = options;
    s.action_ = options.disassemble ? SolveSession::Action::SaveSolutions
                                    : SolveSession::Action::SaveAssemblies;
    s.runStop_ = StopSource();
  }
  static bool offer(SolveSession& s, Assembly a) { return s.onAssembly(std::move(a)); }
};

}  // namespace bt

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

TEST_CASE("sliding: removing a labelled cell clears start and goal", "[sliding]") {
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
// into the gap under the frame's ring or under a neighbour's top plate.
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
// bring to the bottom centre past four upright 1x2, one flat 2x1 and four
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
__extension__ typedef unsigned __int128 Wide;
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
// every level past the first two towards the disk.
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
                key += ";";
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

// The board W x H of pieces of these cells, each at its place.
GridBoard gridBoardOf(int W, int H, const std::vector<BoardCells>& shapes,
                      const std::vector<GridBoard::Place>& places) {
  GridBoard g;
  const unsigned int trayId =
      addStartGoalShape(g.puz, static_cast<unsigned int>(W), static_cast<unsigned int>(H));
  g.start = std::make_unique<Assembly>(g.puz.getGridType());
  g.places = places;
  for (std::size_t i = 0; i < shapes.size(); i++) {
    const GridBoard::Place& at = places[i];
    const unsigned int id = g.puz.addShape(3, 3, 1);
    for (const std::pair<int, int> c : shapes[i]) {
      g.puz.getShape(id).setState(static_cast<unsigned int>(c.first),
                                  static_cast<unsigned int>(c.second), 0, kFilled);
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
// the blocks stopping only on the grid's lines.
void checkGridPath(const GridBoard& g, const Separation& path) {
  REQUIRE(path.getPieceNumber() == g.places.size());
  const State& first = path.getState(0);
  const State& last = path.getState(path.getMoves());
  for (unsigned int i = 0; i < g.places.size(); i++) {
    const GridBoard::Place& p = g.places[i];
    CHECK(first.getX(i) == p.x);
    CHECK(first.getY(i) == p.y);
    if (p.goalX >= 0) {
      CHECK(last.getX(i) == p.goalX);
      CHECK(last.getY(i) == p.goalY);
    }
  }
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    INFO("step " << step);
    std::vector<unsigned int> movers;
    CHECK(slideRoute(g.puz, g.pr(), path, step, &movers).size() >= 2);
    for (unsigned int i = 0; i < g.places.size(); i++) {
      if (g.places[i].block) {
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
// column's one cell down, each one move and none to spare.
GridBoard blockBoard(int cw, int ch, const std::vector<int>& cups) {
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
  return gridBoardOf(3 * cw, 3 * ch, shapes, places);
}

// What a search's levels came to: "start level 3: 120 arrangements" each.
struct LevelSizes {
  std::vector<std::string> lines;
  void watch(SlideSearch& s) {
    s.onLevel = [this](const std::string& line) {
      lines.push_back(line.substr(0, line.find(", one-way")));
    };
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

// A board whose pieces fit the empty tray in more places than a key can
// number is searched level by level all the same when a grid leaves few
// enough stops; with every position numbered it goes to the search over
// text keys, which finds a path as short.
TEST_CASE("sliding: a board too big for its positions is searched by its stops", "[sliding]") {
  // 19 blocks at 130 places each and three units: 176 bits, or 113
  const GridBoard g = blockBoard(5, 4, {6, 10, 18});
  SlideSearch stops;
  stops.nested = true;
  stops.grid = 3;
  stops.threads = 2;
  LevelSizes levels;
  levels.watch(stops);
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, stops);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() == 7);
  CHECK(stops.looseStops);
  CHECK_FALSE(levels.lines.empty());
  checkGridPath(g, *path);

  SlideSearch apart = stops;
  apart.gridStopsApart = true;
  const std::optional<Separation> each = findSlidePath(g.puz, g.pr(), *g.start, apart);
  REQUIRE(each.has_value());
  CHECK(each->getMoves() == 7);

  // every position numbered: no key holds it, so the level search is not run
  SlideSearch plain;
  plain.nested = true;
  plain.grid = 3;
  plain.plainKeys = true;
  LevelSizes none;
  none.watch(plain);
  const std::optional<Separation> old = findSlidePath(g.puz, g.pr(), *g.start, plain);
  REQUIRE(old.has_value());
  CHECK(old->getMoves() == 7);
  CHECK(none.lines.empty());
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
  SlideSearch one;
  one.nested = true;
  one.grid = 3;
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
