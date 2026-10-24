// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles: the tray model, the slide search, routes and the sliding
// solve. Ported from the original's test/test_sliding.cpp, every case in its
// order (the solve thread's cases run a SolveSession; its environment
// switches are the search's options; its scratch folders are this
// process's own), followed by new cases in test_sliding_session.cpp.
#include "../model/model_test_helpers.h"

#include "shapelogic/base/stop.h"
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
#include <sstream>
#include <string>
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

// The solve thread's parameters: PAR_DISASSM and the fast search unless
// the flags say more.
SolveOptions findSolutions() {
  SolveOptions o;
  o.disassemble = true;
  o.reduce = false;
  o.sliding.solver = SlidingSolver::Fast;
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
  puz.getShape(trayId).setColor(0, 0, 0, mark);
  syncSlidingProblems(puz);
  Voxel& tray = puz.getShape(trayId);
  const Problem& pr = puz.getProblem(0);

  REQUIRE(stampOverflowMessage(puz, pr).empty());

  tray.setColor(1, 0, 0, mark);
  const std::string startMsg = stampOverflowMessage(puz, pr);
  REQUIRE(startMsg.find("S" + std::to_string(mark)) != std::string::npos);
  REQUIRE(startMsg.find("start") != std::string::npos);

  tray.setColor(1, 0, 0, 0);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), mark);
  tray.setGoalPiece(tray.getIndex(1, 0, 0), mark);
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
  tray.setColor(0, 0, 0, pieceId + 1);
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
  Voxel& tray2 = puz.getShape(trayId);
  tray2.setGoalPiece(tray2.getIndex(2, 0, 0), 0);
  tray2.setGoalPiece(tray2.getIndex(1, 0, 0), pieceId + 1);
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

  const std::optional<Separation> path = findSlidePath(puz, pr, start, kSearchStates, true);
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
  tray.setColor(0, 0, 0, pieceId + 1);
  tray.setGoalPiece(tray.getIndex(2, 0, 0), pieceId + 1);
  syncSlidingProblems(puz);

  REQUIRE(puz.colorNumber() >= pieceId + 1);

  const Problem& pr = puz.getProblem(0);
  const std::unique_ptr<Assembler> assm = makeAssembler(puz, pr, SolverType::Classic);
  MatrixOptions mo;
  mo.strictColors = true;
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
  o.sliding.solver = SlidingSolver::Fast;
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

  tray.setColor(0, 0, 0, 2);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), 2);
  REQUIRE(tray.getColor(0, 0, 0) == 2);
  REQUIRE(tray.getGoalPiece(0, 0, 0) == 2);

  tray.setState(0, 0, 0, kEmpty);
  REQUIRE(tray.getState(0, 0, 0) == kEmpty);
  REQUIRE(tray.getColor(0, 0, 0) == 0);
  REQUIRE(tray.getGoalPiece(0, 0, 0) == 0);

  // a mark written onto the empty cell must not come back when it is filled
  tray.setGoalPiece(tray.getIndex(0, 0, 0), 2);
  tray.setState(0, 0, 0, kFilled);
  REQUIRE(tray.getState(0, 0, 0) == kFilled);
  REQUIRE(tray.getColor(0, 0, 0) == 0);
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

  // The search proved it: every arrangement was visited well inside the
  // budget, so the deep solver finds nothing either.
  SolveOptions deep = findSolutions();
  deep.sliding.solver = SlidingSolver::Deep;
  CHECK_FALSE(solveNested(puzzle, deep).has_value());
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
  for (const SlidingSolver solver : {SlidingSolver::Fast, SlidingSolver::Full}) {
    INFO("solver " << static_cast<int>(solver));
    SolveOptions o = findSolutions();
    o.sliding.solver = solver;
    const std::optional<Separation> path = solveNested(puzzle, o);
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

TEST_CASE("sliding: a search says whether it finished, hit its limit or was stopped",
          "[sliding]") {
  Puzzle puzzle = nestedPuzzle();
  refreshStartLocks(puzzle, puzzle.getProblem(0));
  const Problem& pr = puzzle.getProblem(0);
  const Assembly start = nestedStart(puzzle);

  SlideSearch full;
  full.maxStates = kFullSearch;
  std::atomic<std::uint64_t> progress{0};
  full.progress = &progress;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, full).has_value());
  CHECK(full.outcome == SlideOutcome::NoPath);
  CHECK(full.visited > 0);
  CHECK(progress.load() == full.visited);

  SlideSearch small;
  small.maxStates = 10;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, small).has_value());
  CHECK(small.outcome == SlideOutcome::Limit);

  StopSource stop;
  stop.request(StopKind::Pause);
  SlideSearch stopped;
  stopped.maxStates = kFullSearch;
  stopped.stop = stop.token();
  CHECK_FALSE(findSlidePath(puzzle, pr, start, stopped).has_value());
  CHECK(stopped.outcome == SlideOutcome::Stopped);
}

TEST_CASE("sliding: a full search stops at its memory limit", "[sliding]") {
  Puzzle puzzle = nestedPuzzle();
  refreshStartLocks(puzzle, puzzle.getProblem(0));
  const Problem& pr = puzzle.getProblem(0);
  const Assembly start = nestedStart(puzzle);

  SlideSearch tight;
  tight.maxStates = kFullSearch;
  tight.maxMemoryStates = 10;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, tight).has_value());
  CHECK(tight.outcome == SlideOutcome::Memory);
  CHECK(tight.memoryStates == 10);

  // the memory budget: about 2 GB normally, never less with high memory
  CHECK(memoryStates(200, false) == 10000000);
  CHECK(memoryStates(200, true) >= memoryStates(200, false));

  SlideSearch normal;
  normal.maxStates = kFullSearch;
  CHECK_FALSE(findSlidePath(puzzle, pr, start, normal).has_value());
  CHECK(normal.outcome == SlideOutcome::NoPath);

  SlideSearch high;
  high.maxStates = kFullSearch;
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

  SolveOptions o = findSolutions();
  o.sliding.solver = SlidingSolver::Full;
  const std::unique_ptr<SolveSession> solver = solveRun(puzzle, o);
  REQUIRE(solver->state() == SolveState::Finished);
  CHECK(puzzle.getProblem(0).getNumSolutions() == 0);
  const std::string note = solver->note();
  CHECK(note.find("Every reachable arrangement was searched") != std::string::npos);
  CHECK(note.find("Allow Nested Slides") != std::string::npos);
}

// Not part of the suite (hidden tag): a throughput and memory benchmark for
// the position-table search. A 4x4 tray with fifteen unit pieces and the
// last two swapped in the goal, which no sliding can reach, so the search
// runs until its limit.
TEST_CASE("sliding: benchmark, fifteen pieces in a 4x4 tray", "[.bench][sliding]") {
  Puzzle puz = slidingPuzzle();
  const unsigned int trayId = addStartGoalShape(puz, 4, 4);
  for (unsigned int k = 0; k < 15; k++) {
    const unsigned int id = puz.addShape(1, 1, 1);
    puz.getShape(id).setState(0, 0, 0, kFilled);
    Voxel& tray = puz.getShape(trayId);
    tray.setColor(k % 4, k / 4, 0, id + 1);
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
  search.maxStates = 3000000;
  CHECK_FALSE(findSlidePath(puz, pr, start, search).has_value());
  // the legacy search runs out of memory first
  CHECK((search.outcome == SlideOutcome::Limit || search.outcome == SlideOutcome::Memory));
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
        tray.setColor(static_cast<unsigned int>(b.x) + x, static_cast<unsigned int>(b.y) + y, 0,
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
  search.maxStates = kFullSearch;
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
    search.maxStates = kFullSearch;
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
    search.maxStates = kFullSearch;
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
  search.maxStates = kFullSearch;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
  REQUIRE(path.has_value());
  CHECK(oneMoverPerStep(*path));
  CHECK(path->getMoves() == 3);
  CHECK(search.memoryStates == memoryStates(3 * 16, false));

  const SlideFixture g = slideFixture(8, 4, blocks);
  SlideSearch legacy;
  legacy.maxStates = kFullSearch;
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
    for (const std::uint64_t states : {kSearchStates, kFullSearch}) {
      INFO("legacy " << legacy << ", states " << states);
      SlideSearch plain;
      plain.maxStates = states;
      plain.legacy = legacy != 0;
      CHECK_FALSE(findSlidePath(puz, pr, start, plain).has_value());

      SlideSearch nested;
      nested.maxStates = states;
      nested.legacy = legacy != 0;
      nested.nested = true;
      CHECK_FALSE(findSlidePath(puz, pr, start, nested).has_value());

      // partially nested implies nested
      SlideSearch partial;
      partial.maxStates = states;
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
    for (const std::uint64_t states : {kSearchStates, kFullSearch}) {
      INFO("legacy " << legacy << ", states " << states);
      SlideSearch plain;
      plain.maxStates = states;
      plain.legacy = legacy != 0;
      std::optional<Separation> path = findSlidePath(puz, pr, start, plain);
      REQUIRE(path.has_value());
      CHECK(path->getMoves() == 2);

      SlideSearch nested;
      nested.maxStates = states;
      nested.legacy = legacy != 0;
      nested.nested = true;
      path = findSlidePath(puz, pr, start, nested);
      REQUIRE(path.has_value());
      CHECK(path->getMoves() == 1);

      SlideSearch partial;
      partial.maxStates = states;
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
      tray.setColor(static_cast<unsigned int>(pc.x + c.first),
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

// The full search looks for an arrangement reached by a move that cannot be
// made backwards in every level before, the others only in the last two. It
// must find exactly the arrangements the queue search does, which keeps
// them all.
TEST_CASE("sliding: the full search counts the same arrangements with one-way moves",
          "[sliding]") {
  const OneWay w = oneWayPuzzle();

  std::uint64_t visited[2] = {0, 0};
  for (int nested = 0; nested < 2; nested++) {
    for (int k = 0; k < 2; k++) {
      SlideSearch search;
      search.maxStates = kFullSearch;
      search.nested = nested != 0;
      search.fullThroughQueue = k == 1;
      CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, search).has_value());
      CHECK(search.outcome == SlideOutcome::NoPath);
      visited[k] = search.visited;
      // with nested slides the puzzle has one-way moves that lead back to
      // arrangements reached two or more moves before
      if (k == 0 && nested != 0) {
        CHECK(search.oneWayRepeats > 0);
      }
      if (k == 1) {
        CHECK(visited[0] == visited[1]);
        CHECK(visited[0] > 100);
      }
    }
  }
}

// Small levels stay in memory, and so do big ones with nowhere to write;
// with no disk to spare, the search says so. memoryLevelBytes = 1 sends
// every level past the first two towards the disk.
TEST_CASE("sliding: the full search keeps its levels where it can", "[sliding]") {
  const OneWay w = oneWayPuzzle();
  const std::filesystem::path dir = scratchDir("slidedir");

  SlideSearch onDisk;
  onDisk.maxStates = kFullSearch;
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
  inMemory.maxStates = kFullSearch;
  inMemory.nested = true;
  inMemory.workDir = dir.string();
  inMemory.memoryLevelBytes = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, inMemory).has_value());
  CHECK(inMemory.outcome == SlideOutcome::NoPath);
  CHECK(inMemory.diskBytes == 0);
  CHECK(inMemory.visited == onDisk.visited);
  std::filesystem::remove_all(dir);

  SlideSearch full;
  full.maxStates = kFullSearch;
  full.nested = true;
  full.workDir = dir.string();
  full.diskBudget = 1;
  full.memoryLevelBytes = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, full).has_value());
  CHECK(full.outcome == SlideOutcome::Disk);
  CHECK(!full.error.empty());
  std::filesystem::remove_all(dir);

  // a small search on a full disk: it never writes, nor makes its folder
  SlideSearch small;
  small.maxStates = kFullSearch;
  small.nested = true;
  small.workDir = dir.string();
  small.diskBudget = 1;
  CHECK_FALSE(findSlidePath(w.puz, w.pr(), *w.start, small).has_value());
  CHECK(small.outcome == SlideOutcome::NoPath);
  CHECK(small.diskBytes == 0);
  CHECK(small.visited == onDisk.visited);
  CHECK(!std::filesystem::exists(dir));
}

// The full search runs on every core, but finds the same path each time:
// as many moves as the queue search, through the same arrangements.
TEST_CASE("sliding: the full search finds the same path on any number of threads",
          "[sliding][parallel]") {
  std::vector<std::vector<int>> paths;
  for (const unsigned int threads : {1U, 3U, 8U}) {
    const SlideFixture f = slideFixture(4, 5, kKlotski);
    SlideSearch search;
    search.maxStates = kFullSearch;
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
// each searched to the end, or to the goal, by the queue search and the
// full search. They must agree on every arrangement there is, or on the
// moves to the goal.
TEST_CASE("sliding: fuzz, the full search agrees with the queue search", "[.fuzz][sliding]") {
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
        tray.setColor(static_cast<unsigned int>(pieces[i].x + c.first),
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
    level.maxStates = kFullSearch;
    level.nested = true;
    level.threads = 1 + static_cast<unsigned int>(rnd(4));
    level.maxMemoryStates = 3000000;
    const std::optional<Separation> levelPath = findSlidePath(puz, pr, start, level);
    if (level.outcome == SlideOutcome::Memory) {
      continue;
    }

    SlideSearch queue;
    queue.maxStates = kFullSearch;
    queue.nested = true;
    queue.fullThroughQueue = true;
    queue.maxMemoryStates = 3000000;
    const std::optional<Separation> queuePath = findSlidePath(puz, pr, start, queue);
    if (queue.outcome == SlideOutcome::Memory) {
      continue;
    }

    INFO("trial " << trial);
    CHECK(level.outcome == queue.outcome);
    if (level.outcome == SlideOutcome::NoPath) {
      CHECK(level.visited == queue.visited);
    }
    if (levelPath && queuePath) {
      CHECK(levelPath->getMoves() == queuePath->getMoves());
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
  const Voxel* l = &f.puz.getShape(L);
  const Voxel* u = &f.puz.getShape(U);
  const auto none = static_cast<unsigned int>(-1);

  CHECK(!startGoalError(f.puz, 0).empty());

  // the L's top-left cell goes where it is put: the cell above its corner
  CHECK(stampPiece(tray, l, L, 1, 1, 0, false).empty());
  CHECK(stampAt(tray, 1, 0, 0, false) == L);
  CHECK(stampAt(tray, 2, 0, 0, false) == L);
  CHECK(stampAt(tray, 1, 1, 0, false) == L);
  CHECK(stampAt(tray, 2, 1, 0, false) == none);
  // a start alone is not enough
  CHECK(!startGoalError(f.puz, 0).empty());

  // It is exactly there; one cell along it is not, and placing it there
  // moves it, over cells it covered itself.
  CHECK(stampIsAt(tray, l, L, 1, 1, 0, false));
  CHECK(!stampIsAt(tray, l, L, 2, 1, 0, false));
  CHECK(stampFits(tray, l, L, 2, 1, 0, false).empty());
  CHECK(stampPiece(tray, l, L, 2, 1, 0, false).empty());
  CHECK(stampAt(tray, 1, 0, 0, false) == none);
  CHECK(stampAt(tray, 3, 0, 0, false) == L);

  // placing it again moves it
  CHECK(stampPiece(tray, l, L, 0, 2, 0, false).empty());
  CHECK(stampAt(tray, 1, 0, 0, false) == none);
  CHECK(stampAt(tray, 0, 2, 0, false) == L);

  // off the tray, or onto another piece's start: nothing changes
  CHECK(!stampPiece(tray, l, L, 4, 0, 0, false).empty());
  CHECK(stampPiece(tray, u, U, 4, 1, 0, false).empty());
  CHECK(!stampPiece(tray, l, L, 3, 2, 0, false).empty());
  CHECK(stampAt(tray, 0, 1, 0, false) == L);

  // a goal on a variable cell is refused: no piece may stop there
  tray->setState(4, 0, 0, kVariable);
  CHECK(!stampPiece(tray, u, U, 4, 0, 0, true).empty());

  // a goal without a start, then both: valid
  CHECK(clearStamp(tray, U, false));
  CHECK(stampPiece(tray, u, U, 3, 0, 0, true).empty());
  CHECK(startGoalError(f.puz, 0).find("no start") != std::string::npos);
  CHECK(stampPiece(tray, u, U, 4, 2, 0, false).empty());
  CHECK(startGoalError(f.puz, 0).empty());

  // a start that is only part of the piece is not valid
  tray->setColor(1, 1, 0, 0);
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
  Voxel* tray = &puz.getShape(trayId);
  const Voxel* p = &puz.getShape(post);
  using Cells = std::vector<TrayCell>;

  // on the bottom layer: the foot's slice, the post standing on the floor
  CHECK(stampCells(tray, p, post, 1, 1, 0, false) == Cells{{1, 1, 0}, {2, 1, 0}, {1, 1, 1}});
  // on the middle layer: as low as it goes, its top under the mouse
  CHECK(stampCells(tray, p, post, 1, 1, 1, false) == Cells{{1, 1, 0}, {2, 1, 0}, {1, 1, 1}});
  // the foot sticks out on the right at either height: the lowest is shown
  CHECK(stampCells(tray, p, post, 2, 1, 1, false) == Cells{{2, 1, 0}, {3, 1, 0}, {2, 1, 1}});
  CHECK(!stampFits(tray, p, post, 2, 1, 1, false).empty());
  // on the top layer: the post's top there, so its foot on the middle layer
  REQUIRE(stampPiece(tray, p, post, 0, 0, 2, false).empty());
  CHECK(stampAt(tray, 0, 0, 1, false) == post);
  CHECK(stampAt(tray, 1, 0, 1, false) == post);
  CHECK(stampAt(tray, 0, 0, 2, false) == post);
  CHECK(stampAt(tray, 0, 0, 0, false) == static_cast<unsigned int>(-1));
  CHECK(stampIsAt(tray, p, post, 0, 0, 2, false));
  CHECK(stampIsAt(tray, p, post, 0, 0, 1, false) == false);
  // a piece no taller than the tray goes in from every layer
  REQUIRE(stampPiece(tray, p, post, 1, 1, 0, true).empty());
  CHECK(stampIsAt(tray, p, post, 1, 1, 1, true));
  CHECK(startGoalError(puz, trayId).empty());

  // a start that misses a layer of the piece is not the whole piece
  tray->setColor(0, 0, 2, 0);
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
  REQUIRE(stampPiece(&f.puz.getShape(0), &v, bar, 0, 0, 0, false).empty());
  REQUIRE(stampPiece(&f.puz.getShape(0), &v, bar, 2, 2, 0, true).empty());

  // moved one row up in its own grid: still the same shape
  v.setState(0, 0, 0, kEmpty);
  v.setState(1, 0, 0, kEmpty);
  v.setState(0, 1, 0, kFilled);
  v.setState(1, 1, 0, kFilled);
  CHECK(dropChangedStamps(f.puz, bar).empty());
  CHECK(stampAt(&f.puz.getShape(0), 0, 0, 0, false) == bar);

  // a cell longer: both go
  f.puz.getShape(bar).setState(2, 1, 0, kFilled);
  CHECK(dropChangedStamps(f.puz, bar) == std::vector<unsigned int>{0});
  CHECK(stampAt(&f.puz.getShape(0), 0, 0, 0, false) == static_cast<unsigned int>(-1));
  CHECK(stampAt(&f.puz.getShape(0), 2, 2, 0, true) == static_cast<unsigned int>(-1));
}
