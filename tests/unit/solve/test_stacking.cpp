// SPDX-License-Identifier: GPL-3.0-or-later
// Stacking puzzles: rod sets, discs, the stacks, the two searches and the
// packer of a saved search. Ported from the original's test/test_stacking.cpp,
// every case in its order. Its scratch folders are this process's own: a
// search the original left to BURRTOOLS_PANEX_DIR (pointed into the scratch
// folder) is given that folder as its workDir here; its environment
// switches are the search's options; the solve thread's case runs a
// SolveSession; its undo case is the document's (test_document_stacking.cpp).
#include "../model/model_test_helpers.h"

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/io/blockpack.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/stacking.h"
#include "bt/solve/panex_search.h"
#include "bt/solve/solve_session.h"
#include "bt/solve/stack_search.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::stacking;

namespace {

// Every folder these tests write goes under one scratch folder of this
// process, removed at exit. Fixed names in the temp folder let two test
// runs at once wipe each other's saved searches.
class ScratchRoot {
 public:
  ScratchRoot() {
    std::random_device rd;
    root = std::filesystem::temp_directory_path() /
           ("bt_test_stacking_" + std::to_string(rd()) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }
  ~ScratchRoot() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  ScratchRoot(const ScratchRoot&) = delete;
  ScratchRoot& operator=(const ScratchRoot&) = delete;
  std::filesystem::path root;
};

const ScratchRoot& scratchRoot() {
  static const ScratchRoot r;
  return r;
}

std::filesystem::path scratch(const char* name) { return scratchRoot().root / name; }

// Where the original's searches without a workDir went: BURRTOOLS_PANEX_DIR,
// which its tests pointed into their scratch folder.
std::string cacheDir() { return (scratchRoot().root / "cache").string(); }

Puzzle makeBoard() { return Puzzle(GridType(GridKind::Stacking)); }

void addSized(Puzzle& puz, Problem& prob, unsigned int size) {
  const unsigned int id = addDisk(puz, size);
  prob.setShapeMaximum(puz, id, 1);
  prob.setShapeMinimum(puz, id, 1);
}

}  // namespace

TEST_CASE("stacking: a disc is one voxel thick and scales with its size", "[stacking]") {
  Puzzle puz = makeBoard();
  const unsigned int id = addDisk(puz, 2);
  const Voxel& v = puz.getShape(id);
  REQUIRE(v.getDiskSize() == 2);
  REQUIRE(v.getZ() == 1);
  REQUIRE(v.getX() == 5);
  REQUIRE(v.getY() == 5);
  REQUIRE(!v.isFilled(2, 2, 0));
  REQUIRE(v.isFilled(0, 0, 0));
  REQUIRE(v.isFilled(1, 2, 0));

  generateDisk(puz.getShape(id), 1);
  REQUIRE(puz.getShape(id).getX() == 3);
  REQUIRE(puz.getShape(id).getY() == 3);
  REQUIRE(!puz.getShape(id).isFilled(1, 1, 0));
  REQUIRE(puz.getShape(id).isFilled(0, 0, 0));
  REQUIRE(puz.getShape(id).isFilled(0, 1, 0));

  generateDisk(puz.getShape(id), 2);
  const unsigned int other = addDisk(puz, 2);
  REQUIRE(puz.getShape(id).getX() == puz.getShape(other).getX());
  REQUIRE(puz.getShape(id).getDiskSize() == puz.getShape(other).getDiskSize());
}

TEST_CASE("stacking: three-disc Hanoi is seven transfers", "[stacking]") {
  Puzzle puz = makeBoard();
  REQUIRE(puz.rodSetCount() == 1);
  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 3);
  addSized(puz, pr, 2);
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);

  // Largest on the bottom of rod 0. Goal is the same tower on the last rod.
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, 1, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, 2, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, 0, 2).empty());
  REQUIRE(placeDisk(puz, pr, true, 1, 2).empty());
  REQUIRE(placeDisk(puz, pr, true, 2, 2).empty());
  REQUIRE(setupError(puz, pr).empty());

  const std::optional<Separation> path = findStackPath(puz, pr);
  REQUIRE(path);
  REQUIRE(logicalMoves(*path) == 7);
  REQUIRE(path->getMoves() == 21);
}

TEST_CASE("stacking: distance limits a jump to the neighbouring rod", "[stacking]") {
  Puzzle puz = makeBoard();
  RodSet& board = puz.getRodSet(0);
  board.sizeMatters = false;
  board.distanceMatters = true;
  board.canMoveOver = false;

  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, 0, 2).empty());

  std::optional<Separation> path = findStackPath(puz, pr);
  REQUIRE(path);
  REQUIRE(logicalMoves(*path) == 2);

  puz.getRodSet(0).canMoveOver = true;
  path = findStackPath(puz, pr);
  REQUIRE(path);
  REQUIRE(logicalMoves(*path) == 1);
}

TEST_CASE("stacking: a full rod and a larger disc are refused", "[stacking]") {
  Puzzle puz = makeBoard();
  {
    RodSet& board = puz.getRodSet(0);
    board.growHeight = false;
    board.definedHeight = 1;
    board.sizeMatters = false;
  }

  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 1);
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);

  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE_FALSE(placeDisk(puz, pr, false, 1, 0).empty());

  puz.getRodSet(0).growHeight = true;
  puz.getRodSet(0).sizeMatters = true;
  (void)liftTop(puz, pr, false, 0);
  const unsigned int small = addDisk(puz, 1);
  const unsigned int large = addDisk(puz, 3);
  pr.setShapeMaximum(puz, small, 1);
  pr.setShapeMinimum(puz, small, 1);
  pr.setShapeMaximum(puz, large, 1);
  pr.setShapeMinimum(puz, large, 1);
  REQUIRE(placeDisk(puz, pr, false, small, 0).empty());
  REQUIRE_FALSE(placeDisk(puz, pr, false, large, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, large, 1).empty());
}

TEST_CASE("stacking: an unenforced placement is kept and setupError names the rule",
          "[stacking]") {
  Puzzle puz = makeBoard();
  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 1);
  addSized(puz, pr, 3);
  pr.setRodSetId(0);
  syncMaps(puz, pr);

  // Size 3 on size 1 breaks the size rule, but the editor may build it.
  REQUIRE(placeDisk(puz, pr, false, 0, 0, false).empty());
  REQUIRE(placeDisk(puz, pr, false, 1, 0, false).empty());
  REQUIRE(pr.startStacks().rods[0].size() == 2);
  std::string err = setupError(puz, pr);
  REQUIRE(err.find("rod 1 of the start") != std::string::npos);
  REQUIRE(err.find("smaller") != std::string::npos);

  // Turning the rule off makes the start legal; the goal is still empty.
  puz.getRodSet(0).sizeMatters = false;
  err = setupError(puz, pr);
  REQUIRE(err.find("the goal is missing S1 and S2") != std::string::npos);

  REQUIRE(placeDisk(puz, pr, true, 0, 2).empty());
  REQUIRE(placeDisk(puz, pr, true, 1, 2).empty());
  REQUIRE(setupError(puz, pr).empty());

  // A fixed height lower than the stack is reported, not cleared.
  puz.getRodSet(0).growHeight = false;
  puz.getRodSet(0).definedHeight = 1;
  err = setupError(puz, pr);
  REQUIRE(err.find("height") != std::string::npos);
  REQUIRE(pr.startStacks().rods[0].size() == 2);
}

TEST_CASE("stacking: start and goal must use the same discs", "[stacking]") {
  Puzzle puz = makeBoard();
  puz.getRodSet(0).sizeMatters = false;
  Problem& pr = puz.getProblem(puz.addProblem());
  const unsigned int a = addDisk(puz, 1);
  const unsigned int b = addDisk(puz, 2);
  pr.setRodSetId(0);
  syncMaps(puz, pr);

  // Two copies of a on the start, one copy of a and one b on the goal.
  pr.setShapeMaximum(puz, a, 2);
  pr.setShapeMinimum(puz, a, 2);
  pr.setShapeMaximum(puz, b, 1);
  pr.setShapeMinimum(puz, b, 1);
  REQUIRE(placeDisk(puz, pr, false, a, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, a, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, a, 1).empty());
  REQUIRE(placeDisk(puz, pr, true, b, 1).empty());

  std::string err = setupError(puz, pr);
  REQUIRE(err ==
          "The start and goal use different discs: the goal is missing S1; "
          "the start is missing S2.");

  REQUIRE(placeDisk(puz, pr, true, a, 2).empty());
  REQUIRE(placeDisk(puz, pr, false, b, 2).empty());
  REQUIRE(setupError(puz, pr).empty());

  // Emptying both sides leaves discs counted in the problem but placed nowhere.
  for (auto& rod : pr.editStart().rods) {
    rod.clear();
  }
  for (auto& rod : pr.editGoal().rods) {
    rod.clear();
  }
  err = setupError(puz, pr);
  REQUIRE(err.find("on neither the start nor the goal") != std::string::npos);
}

TEST_CASE("stacking: lowering the rod count keeps the discs on the dropped rods", "[stacking]") {
  Puzzle puz = makeBoard();
  puz.getRodSet(0).sizeMatters = false;
  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, 0, 2).empty());
  REQUIRE(setupError(puz, pr).empty());

  puz.getRodSet(0).rodCount = 2;
  syncMaps(puz, pr);
  REQUIRE(pr.goalStacks().rods.size() == 3);
  REQUIRE(pr.startStacks().rods.size() == 2);
  const std::string err = setupError(puz, pr);
  REQUIRE(err.find("rod 3") != std::string::npos);
  REQUIRE_FALSE(findStackPath(puz, pr));

  puz.getRodSet(0).rodCount = 3;
  syncMaps(puz, pr);
  REQUIRE(setupError(puz, pr).empty());
  REQUIRE(pr.goalStacks().rods[2].size() == 1);
}

TEST_CASE("stacking: rod sets and disc size survive a save and load", "[stacking]") {
  Puzzle puz = makeBoard();
  puz.getRodSet(0).name = "Hanoi";
  puz.getRodSet(0).distanceMatters = true;
  const unsigned int id = addDisk(puz, 4);
  puz.getShape(id).setName("large");

  Result<Puzzle> loaded = io::parsePuzzle(io::writePuzzle(puz));
  REQUIRE(loaded);

  REQUIRE(loaded->getGridType().getType() == GridKind::Stacking);
  REQUIRE(loaded->rodSetCount() == 1);
  REQUIRE(loaded->getRodSet(0).name == "Hanoi");
  REQUIRE(loaded->getRodSet(0).distanceMatters);
  REQUIRE(loaded->getNumberOfShapes() == 1);
  REQUIRE(loaded->getShape(0).getDiskSize() == 4);
  REQUIRE(loaded->getShape(0).getName() == "large");
  REQUIRE(loaded->getShape(0).getZ() == 1);
}

TEST_CASE("stacking: the Hanoi fixture solves in seven transfers", "[stacking][solver]") {
  // test_stacking_hanoi.xmpuzzle: three discs, largest at the bottom, on
  // the first of three rods; the goal is the same tower on the last rod.
  Puzzle puzzle = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  REQUIRE(isStacking(puzzle));
  Problem& pr = puzzle.getProblem(0);
  REQUIRE(setupError(puzzle, pr).empty());

  SolveOptions o;
  o.disassemble = true;
  o.stacking.workDir = cacheDir();
  SolveSession solver(puzzle, pr, o);
  REQUIRE(solver.start().has_value());
  solver.wait();
  REQUIRE(solver.state() == SolveState::Finished);
  REQUIRE(pr.getNumSolutions() == 1);
  const Separation* path = pr.getSavedSolution(0).getDisassembly();
  REQUIRE(path != nullptr);
  CHECK(logicalMoves(*path) == 7);
  CHECK(path->getMoves() == 7 * kStepsPerMove);
}

namespace {

// Panex Jr: six discs on two Panex columns six deep, plus a one-deep pocket
// beside the first. A disc of size s goes at most s places down, so the
// tower starts with size 6 at the bottom and size 1 at the top. The same
// puzzle as a sliding tray takes 143 moves, and the stacking model reaches
// exactly the same resting arrangements.
Problem& makePanexJr(Puzzle& puz) {
  RodSet& board = puz.getRodSet(0);
  board.rodCount = 2;
  board.growHeight = false;
  board.definedHeight = 6;
  board.sizeMatters = false;
  board.panexColumns = true;
  board.pocketColumn = true;
  board.pocketHeight = 1;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int size = 6; size >= 1; size--) {
    addSized(puz, pr, size);
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int id = 0; id < 6; id++) {
    REQUIRE(placeDisk(puz, pr, false, id, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, id, 1).empty());
  }
  return pr;
}

}  // namespace

TEST_CASE("stacking: Panex Jr with a pocket column is 143 transfers", "[stacking][solver]") {
  Puzzle puz = makeBoard();
  Problem& pr = makePanexJr(puz);
  REQUIRE(totalRods(puz.getRodSet(0)) == 3);
  REQUIRE(isPocket(puz.getRodSet(0), 2));
  REQUIRE(setupError(puz, pr).empty());

  StackSearch search;
  const std::optional<Separation> path = findStackPath(puz, pr, search);
  REQUIRE(path);
  CHECK(search.outcome == StackOutcome::Found);
  CHECK(logicalMoves(*path) == 143);

  // Without the pocket the tower cannot move at all.
  puz.getRodSet(0).pocketColumn = false;
  syncMaps(puz, pr);
  REQUIRE(setupError(puz, pr).empty());
  StackSearch none;
  CHECK_FALSE(findStackPath(puz, pr, none));
  CHECK(none.outcome == StackOutcome::NoPath);
}

TEST_CASE("stacking: a Panex column limits how deep a disc goes", "[stacking]") {
  Puzzle puz = makeBoard();
  Problem& pr = makePanexJr(puz);

  // Ids 0..5 are sizes 6..1. Free the size 1 and size 2 discs.
  REQUIRE(liftTop(puz, pr, false, 0).empty());
  REQUIRE(liftTop(puz, pr, false, 0).empty());

  // The pocket takes any one disc; a second does not fit.
  REQUIRE(placeDisk(puz, pr, false, 4, 2).empty());
  REQUIRE(liftTop(puz, pr, false, 0).empty());
  CHECK(placeDisk(puz, pr, false, 3, 2) == "That rod is full");

  // On rod 2 the size 1 disc may go one down, with the size 3 disc raised
  // into the bridge above it, but not two down.
  REQUIRE(placeDisk(puz, pr, false, 5, 1).empty());
  REQUIRE(placeDisk(puz, pr, false, 3, 1).empty());
  REQUIRE(liftTop(puz, pr, false, 0).empty());
  CHECK_FALSE(placeDisk(puz, pr, false, 2, 1).empty());

  // An unenforced placement is kept, and setupError names the rule.
  REQUIRE(placeDisk(puz, pr, false, 2, 1, false).empty());
  CHECK(setupError(puz, pr).find("Panex column") != std::string::npos);
}

TEST_CASE("stacking: Panex discs rest as low as they can go", "[stacking]") {
  Puzzle puz = makeBoard();
  Problem& pr = makePanexJr(puz);

  // Ids 0..5 are sizes 6..1. Take the size 1 and size 2 discs off: the rest
  // stay at the bottom of rod 1 instead of rising to the bridge.
  REQUIRE(liftTop(puz, pr, false, 0).empty());
  REQUIRE(liftTop(puz, pr, false, 0).empty());
  // On rod 2 the size 2 disc falls two below the bridge, and the size 1
  // disc on it rests one below.
  REQUIRE(placeDisk(puz, pr, false, 4, 1).empty());
  REQUIRE(placeDisk(puz, pr, false, 5, 1).empty());

  const BoardLayout lay = layoutBoard(puz, pr, false);
  for (unsigned int id = 0; id < 4; id++) {
    CHECK(lay.disks[id].z == static_cast<int>(id));
  }
  CHECK(lay.disks[4].z == 4);
  CHECK(lay.disks[5].z == 5);
}

TEST_CASE("stacking: Panex settings survive a save and load", "[stacking]") {
  Puzzle puz = makeBoard();
  puz.getRodSet(0).panexColumns = true;
  puz.getRodSet(0).pocketColumn = true;
  puz.getRodSet(0).pocketHeight = 2;
  puz.getRodSet(0).outsideChannel = true;

  Result<Puzzle> loaded = io::parsePuzzle(io::writePuzzle(puz));
  REQUIRE(loaded);
  REQUIRE(loaded->rodSetCount() == 1);
  CHECK(loaded->getRodSet(0).panexColumns);
  CHECK(loaded->getRodSet(0).pocketColumn);
  CHECK(loaded->getRodSet(0).pocketHeight == 2);
  CHECK(loaded->getRodSet(0).outsideChannel);
}

namespace {

// The classic Panex swap: three Panex columns n deep, a tower of n discs on
// each outer column, and the goal is the two towers swapped.
Problem& makePanexSwap(Puzzle& puz, unsigned int n) {
  RodSet& board = puz.getRodSet(0);
  board.rodCount = 3;
  board.growHeight = false;
  board.definedHeight = n;
  board.sizeMatters = false;
  board.panexColumns = true;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = n; size >= 1; size--) {
      addSized(puz, pr, size);
    }
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int i = 0; i < n; i++) {
    REQUIRE(placeDisk(puz, pr, false, i, 0).empty());
    REQUIRE(placeDisk(puz, pr, false, n + i, 2).empty());
    REQUIRE(placeDisk(puz, pr, true, n + i, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, i, 2).empty());
  }
  REQUIRE(setupError(puz, pr).empty());
  return pr;
}

// A Panex search as the original's tests ran one: saved to the process's
// scratch cache (their BURRTOOLS_PANEX_DIR).
panex::PanexSearch panexSearch() {
  panex::PanexSearch s;
  s.workDir = cacheDir();
  return s;
}

}  // namespace

TEST_CASE("panex: the Panex Solver finds the published swap lengths",
          "[stacking][panex][slow]") {
  // 3, 4 and 5 discs a tower swap in 42, 128 and 343 transfers.
  const unsigned int moves[] = {42, 128, 343};
  for (unsigned int n = 3; n <= 5; n++) {
    Puzzle puz = makeBoard();
    Problem& pr = makePanexSwap(puz, n);
    REQUIRE(panex::unsupported(puz, pr).empty());
    panex::PanexSearch search = panexSearch();
    const std::optional<Separation> path = panex::solve(puz, pr, search);
    REQUIRE(path);
    CHECK(search.outcome == panex::Outcome::Found);
    CHECK(logicalMoves(*path) == moves[n - 3]);

    // Solving each half, instead of keeping every level, finds as short a path.
    panex::PanexSearch halves = panexSearch();
    halves.keepLimit = 10;
    const std::optional<Separation> halved = panex::solve(puz, pr, halves);
    REQUIRE(halved);
    CHECK(logicalMoves(*halved) == moves[n - 3]);
  }
}

TEST_CASE("panex: the Panex Solver agrees with the Stacking Solver", "[stacking][panex]") {
  // Panex Jr has a pocket, so the search runs from both ends.
  Puzzle puz = makeBoard();
  Problem& pr = makePanexJr(puz);
  const std::optional<Separation> stacked = findStackPath(puz, pr);
  REQUIRE(stacked);
  panex::PanexSearch search = panexSearch();
  search.keepLimit = 50;
  const std::optional<Separation> path = panex::solve(puz, pr, search);
  REQUIRE(path);
  CHECK(logicalMoves(*path) == logicalMoves(*stacked));
  CHECK(logicalMoves(*path) == 143);

  // The last frame is the goal: the same disc placements as the other solver's.
  const State& a = path->getState(path->getMoves());
  const State& b = stacked->getState(stacked->getMoves());
  for (unsigned int i = 0; i < path->getPieceNumber(); i++) {
    CHECK(a.getX(i) == b.getX(i));
    CHECK(a.getZ(i) == b.getZ(i));
  }
}

// The two solvers keep the Panex rules separately (stacking::moveAllowed and
// the Panex search's rules); on the swap, with no pocket, discs raised into
// the bridge block moves, so both rules are exercised.
TEST_CASE("panex: the two solvers agree on the Panex swap", "[stacking][panex]") {
  for (unsigned int n = 2; n <= 3; n++) {
    INFO(n << " discs a tower");
    Puzzle puz = makeBoard();
    Problem& pr = makePanexSwap(puz, n);
    const std::optional<Separation> stacked = findStackPath(puz, pr);
    REQUIRE(stacked);
    panex::PanexSearch search = panexSearch();
    const std::optional<Separation> path = panex::solve(puz, pr, search);
    REQUIRE(path);
    CHECK(logicalMoves(*path) == logicalMoves(*stacked));
  }
}

// The outside channel lets a disc go between the outer rods while one is
// raised on the middle rod, which makes the swap shorter; both solvers keep
// the rule.
TEST_CASE("panex: the outside channel shortens the Panex swap", "[stacking][panex]") {
  for (unsigned int n = 2; n <= 4; n++) {
    INFO(n << " discs a tower");
    Puzzle puz = makeBoard();
    Problem& pr = makePanexSwap(puz, n);
    panex::PanexSearch plain = panexSearch();
    const std::optional<Separation> without = panex::solve(puz, pr, plain);
    REQUIRE(without);

    puz.getRodSet(0).outsideChannel = true;
    REQUIRE(hasOutsideChannel(puz.getRodSet(0)));
    panex::PanexSearch search = panexSearch();
    const std::optional<Separation> path = panex::solve(puz, pr, search);
    REQUIRE(path);
    CHECK(logicalMoves(*path) < logicalMoves(*without));
    if (n <= 3) {
      const std::optional<Separation> stacked = findStackPath(puz, pr);
      REQUIRE(stacked);
      CHECK(logicalMoves(*path) == logicalMoves(*stacked));
    }
  }
}

TEST_CASE("stacking: the outside channel needs Panex columns and three rods", "[stacking]") {
  RodSet board;
  board.panexColumns = true;
  board.outsideChannel = true;
  board.rodCount = 4;
  CHECK(hasOutsideChannel(board));
  CHECK(viaOutsideChannel(board, 0, 3));
  CHECK(viaOutsideChannel(board, 3, 0));
  CHECK_FALSE(viaOutsideChannel(board, 0, 2));
  board.rodCount = 2;
  CHECK_FALSE(hasOutsideChannel(board));
  board.rodCount = 3;
  board.panexColumns = false;
  CHECK_FALSE(hasOutsideChannel(board));
}

TEST_CASE("panex: the Panex Solver needs Panex columns", "[stacking][panex]") {
  Puzzle puz = makeBoard();
  Problem& pr = makePanexSwap(puz, 3);
  puz.getRodSet(0).panexColumns = false;
  puz.getRodSet(0).sizeMatters = false;
  CHECK(panex::unsupported(puz, pr).find("Panex Style Columns") != std::string::npos);
  panex::PanexSearch search = panexSearch();
  CHECK_FALSE(panex::solve(puz, pr, search));
  CHECK(search.outcome == panex::Outcome::Error);
}

namespace {

// Every placement of a stacking path is one disc on one rod's top moving to
// another rod's top: the three frames of each transfer agree with the start
// and end of it.
void checkTransfers(const Separation& path) {
  const unsigned int n = path.getPieceNumber();
  for (unsigned int m = 0; m + 1 <= logicalMoves(path); m++) {
    const State& a = path.getState(m * kStepsPerMove);
    const State& b = path.getState((m + 1) * kStepsPerMove);
    unsigned int moved = 0;
    for (unsigned int i = 0; i < n; i++) {
      if (a.getX(i) != b.getX(i)) {
        moved++;
      }
    }
    CHECK(moved == 1);
  }
}

}  // namespace

TEST_CASE("panex: tracing back between saved levels", "[stacking][panex][slow]") {
  // A tiny memory share and disk budget: the levels go to disk, only every
  // few of them stay, and the path is traced back between those.
  const std::string dir = scratch("panex-test").string();
  Puzzle puz = makeBoard();
  Problem& pr = makePanexSwap(puz, 5);
  panex::PanexSearch search;
  search.workDir = dir;
  search.keepLimit = 10;
  search.diskBudget = 200000;
  const std::optional<Separation> path = panex::solve(puz, pr, search);
  REQUIRE(path);
  CHECK(logicalMoves(*path) == 343);
  CHECK(search.levelInterval > 1);
  checkTransfers(*path);
  // A finished search leaves nothing saved.
  CHECK(panex::savedSearch(puz, pr, dir).empty());

  // Panex Jr runs from both ends, and so saves both.
  Puzzle jr = makeBoard();
  Problem& jp = makePanexJr(jr);
  panex::PanexSearch both;
  both.workDir = dir;
  both.keepLimit = 10;
  both.diskBudget = 20000;
  const std::optional<Separation> jpath = panex::solve(jr, jp, both);
  REQUIRE(jpath);
  CHECK(logicalMoves(*jpath) == 143);
  checkTransfers(*jpath);
  std::filesystem::remove_all(dir);
}

TEST_CASE("panex: a stopped search carries on where it stopped", "[stacking][panex][slow]") {
  const std::string dir = scratch("panex-resume").string();
  std::filesystem::remove_all(dir);
  Puzzle puz = makeBoard();
  Problem& pr = makePanexSwap(puz, 5);

  panex::PanexSearch first;
  first.workDir = dir;
  first.keepLimit = 10;
  first.stopAtDepth = 200;
  CHECK_FALSE(panex::solve(puz, pr, first));
  CHECK(first.outcome == panex::Outcome::Stopped);
  CHECK(first.saved);
  CHECK(panex::savedSearch(puz, pr, dir).find("200 moves deep") != std::string::npos);

  // Saves from before Time used kept milliseconds (BTPANEX3, whole seconds)
  // still load: rewrite this one in that form and read it back.
  {
    std::filesystem::path state;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
      if (e.path().filename() == "state.bin") {
        state = e.path();
      }
    }
    REQUIRE_FALSE(state.empty());
    std::fstream f(state, std::ios::in | std::ios::out | std::ios::binary);
    const std::streamoff msAt = 8 + 8 + 4 + 4 + 4 + 8;
    f.seekp(7);
    f.put('3');
    f.seekp(msAt);
    const std::uint64_t seconds = 7;
    f.write(reinterpret_cast<const char*>(&seconds), sizeof(seconds));
    f.close();
    CHECK(panex::savedMs(puz, pr, dir) == 7000);
    CHECK(panex::savedSearch(puz, pr, dir).find("200 moves deep") != std::string::npos);
  }

  // Without resume it starts over; with it, it carries on to the same answer.
  panex::PanexSearch second;
  second.workDir = dir;
  second.resume = true;
  const std::optional<Separation> path = panex::solve(puz, pr, second);
  REQUIRE(path);
  CHECK(logicalMoves(*path) == 343);
  // It went on from the saved count, not from nothing.
  CHECK(second.found > first.found);
  checkTransfers(*path);
  CHECK(panex::savedSearch(puz, pr, dir).empty());

  // Discarding a saved search leaves none.
  panex::PanexSearch third;
  third.workDir = dir;
  third.keepLimit = 10;
  third.stopAtDepth = 100;
  CHECK_FALSE(panex::solve(puz, pr, third));
  CHECK_FALSE(panex::savedSearch(puz, pr, dir).empty());
  panex::discardSaved(puz, pr, dir);
  CHECK(panex::savedSearch(puz, pr, dir).empty());
  std::filesystem::remove_all(dir);
}

// The Stacking Solver runs on the Panex search when the puzzle fits it, so
// the two searches must agree on plain rods too: the size rule, the
// distance rule, and neither.
TEST_CASE("panex: the search agrees with findStackPath on plain rods", "[stacking][panex]") {
  for (int rules = 0; rules < 4; rules++) {
    for (unsigned int n = 3; n <= 6; n++) {
      INFO("rules " << rules << ", " << n << " discs");
      Puzzle puz = makeBoard();
      {
        RodSet& board = puz.getRodSet(0);
        board.sizeMatters = rules != 2;
        board.distanceMatters = rules == 1 || rules == 3;
      }
      Problem& pr = puz.getProblem(puz.addProblem());
      for (unsigned int size = n; size >= 1; size--) {
        addSized(puz, pr, size);
      }
      pr.setRodSetId(0);
      syncMaps(puz, pr);
      for (unsigned int i = 0; i < n; i++) {
        REQUIRE(placeDisk(puz, pr, false, i, 0).empty());
        REQUIRE(placeDisk(puz, pr, true, i, 2).empty());
      }
      REQUIRE(setupError(puz, pr).empty());
      REQUIRE(panex::unsupported(puz, pr, true).empty());

      const std::optional<Separation> plain = findStackPath(puz, pr);
      REQUIRE(plain);
      panex::PanexSearch search;
      search.anyRules = true;
      search.workDir = scratch("panex-plain").string();
      const std::optional<Separation> path = panex::solve(puz, pr, search);
      REQUIRE(path);
      CHECK(logicalMoves(*path) == logicalMoves(*plain));
      checkTransfers(*path);
    }
  }
}

// An export may leave the saved levels out: the search still carries on,
// and finds its path back by solving the rest by halves.
TEST_CASE("panex: a saved search without its levels still finds the path",
          "[stacking][panex][slow]") {
  const std::string dir = scratch("panex-nolevels").string();
  std::filesystem::remove_all(dir);
  Puzzle puz = makeBoard();
  Problem& pr = makePanexSwap(puz, 5);

  panex::PanexSearch first;
  first.workDir = dir;
  first.keepLimit = 10;
  first.stopAtDepth = 200;
  CHECK_FALSE(panex::solve(puz, pr, first));
  REQUIRE(first.saved);

  // Keep only the state, as an export without levels does.
  const std::filesystem::path folder = panex::searchFolder(puz, pr, dir);
  REQUIRE(std::filesystem::exists(folder / "state.bin"));
  for (const auto& e : std::filesystem::directory_iterator(folder)) {
    if (e.path().extension() == ".lvl") {
      std::filesystem::remove(e.path());
    }
  }

  panex::PanexSearch second;
  second.workDir = dir;
  second.resume = true;
  const std::optional<Separation> path = panex::solve(puz, pr, second);
  REQUIRE(path);
  CHECK(logicalMoves(*path) == 343);
  checkTransfers(*path);
  std::filesystem::remove_all(dir);
}

TEST_CASE("stacking: the board span covers every rod, the pocket too", "[stacking]") {
  Puzzle puz = makeBoard();
  Problem& pr = makePanexJr(puz);
  const BoardLayout lay = layoutBoard(puz, pr, false);
  REQUIRE(lay.rodX.size() == 3);
  // The pocket is the last rod but is drawn first, at x 0.
  CHECK(lay.rodX.back() == 0);
  CHECK(boardSpan(puz, pr) == static_cast<unsigned int>(lay.rodX[1] + lay.spacing));
}

TEST_CASE("stacking: a rod set with impossible numbers in the file loads sanely", "[stacking]") {
  // the original read the element alone; here it is a file's
  Result<Puzzle> puz = io::parsePuzzle(
      "<puzzle version=\"2\"><gridType type=\"6\"/>"
      "<rodSets><rodSet rods=\"-3\" grow=\"0\" height=\"-1\" pocket=\"x\"/></rodSets>"
      "</puzzle>");
  REQUIRE(puz);
  REQUIRE(puz->rodSetCount() == 1);
  const RodSet& r = puz->getRodSet(0);
  CHECK(r.rodCount == 3);
  CHECK(r.definedHeight == 8);
  CHECK_FALSE(r.pocketColumn);
  CHECK_FALSE(r.growHeight);
}

// The original's "undo of a disc edit keeps the stacks" is the document's:
// test_document_stacking.cpp.

// Hidden: the Panex swap of 6 discs a tower (881 transfers), at one thread
// and at all of them.   build/release/tests/bt_tests "[.bench][panex]"
TEST_CASE("panex: benchmark, the six-disc swap", "[.bench][panex]") {
  // BT_PANEX_BENCH_N=7 for the seven-disc swap (2189 transfers).
  const char* asked = std::getenv("BT_PANEX_BENCH_N");
  const auto n = asked != nullptr ? static_cast<unsigned int>(std::atoi(asked)) : 6U;
  for (const unsigned int threads : {1U, 0U}) {
    Puzzle puz = makeBoard();
    Problem& pr = makePanexSwap(puz, n);
    panex::PanexSearch search = panexSearch();
    search.threads = threads;
    const auto t0 = std::chrono::steady_clock::now();
    const std::optional<Separation> path = panex::solve(puz, pr, search);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE(path);
    std::printf("panex %u, %s: %u transfers, %llu stackings, peak %llu MB, %.2f s\n", n,
                threads != 0 ? "1 thread" : "all threads", logicalMoves(*path),
                static_cast<unsigned long long>(search.found),
                static_cast<unsigned long long>(search.peakMemory >> 20), s);
  }
}

// The classic tower is solved by its well-known recursion, with no search:
// 2^n - 1 transfers, each one legal, the same count the search finds.
TEST_CASE("panex: the classic tower needs no search", "[stacking][panex]") {
  for (const unsigned int n : {1U, 2U, 7U, 10U}) {
    INFO(n << " discs");
    Puzzle puz = makeBoard();
    puz.getRodSet(0).sizeMatters = true;
    Problem& pr = puz.getProblem(puz.addProblem());
    for (unsigned int size = n; size >= 1; size--) {
      addSized(puz, pr, size);
    }
    pr.setRodSetId(0);
    syncMaps(puz, pr);
    for (unsigned int i = 0; i < n; i++) {
      REQUIRE(placeDisk(puz, pr, false, i, 2).empty());
      REQUIRE(placeDisk(puz, pr, true, i, 1).empty());
    }
    REQUIRE(setupError(puz, pr).empty());

    panex::PanexSearch direct = panexSearch();
    direct.anyRules = true;
    const std::optional<Separation> path = panex::solve(puz, pr, direct);
    REQUIRE(path);
    CHECK(direct.outcome == panex::Outcome::Found);
    CHECK(logicalMoves(*path) == (1U << n) - 1);
    checkTransfers(*path);
    // No search: only the path's own stackings were made.
    CHECK(direct.found == (std::uint64_t{1} << n));

    // BURRTOOLS_NO_TOWER_RULE=1 in the original
    panex::PanexSearch searched;
    searched.anyRules = true;
    searched.towerRule = false;
    searched.workDir = scratch("panex-tower").string();
    const std::optional<Separation> found = panex::solve(puz, pr, searched);
    REQUIRE(found);
    CHECK(logicalMoves(*found) == logicalMoves(*path));
  }
}

namespace {

std::string fileBytes(const std::filesystem::path& f) {
  std::ifstream in(f, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Pack f, unpack it again, and check the bytes come back; returns the
// packed size.
std::uint64_t packRoundTrip(const std::filesystem::path& f, bool keys, std::size_t chunk) {
  std::stringstream packed;
  const std::uint64_t n = io::blockpack::pack(f, packed, keys, 4, chunk);
  REQUIRE(n == packed.str().size());
  const std::filesystem::path back = f.string() + ".back";
  std::stringstream in(packed.str());
  REQUIRE(io::blockpack::unpack(in, n, back, 4));
  const bool same = fileBytes(back) == fileBytes(f);
  CHECK(same);
  std::filesystem::remove(back);
  return n;
}

}  // namespace

TEST_CASE("blockpack: files come back unchanged, keys packed small",
          "[stacking][panex][export]") {
  const std::filesystem::path dir = scratch("blockpack");
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::mt19937_64 rng(7);

  // Sorted 16-byte keys, as a search level holds, over many chunks.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> keys(100000);
  for (auto& k : keys) {
    const std::uint64_t hi = rng() >> 40;
    k = {hi, rng()};
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  {
    std::ofstream out(dir / "sorted.lvl", std::ios::binary);
    for (const auto& k : keys) {
      out.write(reinterpret_cast<const char*>(&k.first), 8);
      out.write(reinterpret_cast<const char*>(&k.second), 8);
    }
  }
  const std::uint64_t raw = std::filesystem::file_size(dir / "sorted.lvl");
  const std::uint64_t asKeys = packRoundTrip(dir / "sorted.lvl", true, 16 * 997);
  const std::uint64_t plain = packRoundTrip(dir / "sorted.lvl", false, 16 * 997);
  INFO("raw " << raw << ", as keys " << asKeys << ", plain deflate " << plain);
  CHECK(asKeys < plain);

  // Unsorted keys and odd sizes fall back to plain deflate.
  std::shuffle(keys.begin(), keys.end(), rng);
  {
    std::ofstream out(dir / "unsorted.lvl", std::ios::binary);
    for (const auto& k : keys) {
      out.write(reinterpret_cast<const char*>(&k.first), 8);
      out.write(reinterpret_cast<const char*>(&k.second), 8);
    }
    std::ofstream odd(dir / "odd.bin", std::ios::binary);
    for (int i = 0; i < 12345; i++) {
      odd.put(static_cast<char>(rng() & 0xff));
    }
    std::ofstream empty(dir / "empty.bin", std::ios::binary);
  }
  packRoundTrip(dir / "unsorted.lvl", true, 16 * 997);
  packRoundTrip(dir / "odd.bin", true, 4096);
  packRoundTrip(dir / "empty.bin", true, 4096);

  // Damage is refused, not written out as data.
  std::stringstream packed;
  const std::uint64_t n = io::blockpack::pack(dir / "sorted.lvl", packed, true, 2, 16 * 997);
  std::string bytes = packed.str();
  bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x55);
  std::stringstream damaged(bytes);
  CHECK_FALSE(io::blockpack::unpack(damaged, n, dir / "damaged.lvl", 2));
  std::filesystem::remove_all(dir);
}

TEST_CASE("blockpack: a real search's saved levels", "[stacking][panex][export]") {
  const std::string dir = scratch("blockpack-panex").string();
  std::filesystem::remove_all(dir);
  Puzzle puz = makeBoard();
  Problem& pr = makePanexSwap(puz, 5);
  panex::PanexSearch search;
  search.workDir = dir;
  search.keepLimit = 10;
  search.stopAtDepth = 200;
  CHECK_FALSE(panex::solve(puz, pr, search));
  REQUIRE(search.saved);

  std::uint64_t raw = 0;
  std::uint64_t packedTotal = 0;
  std::uint64_t plainTotal = 0;
  for (const auto& e : std::filesystem::directory_iterator(panex::searchFolder(puz, pr, dir))) {
    const bool level = e.path().extension() == ".lvl";
    raw += std::filesystem::file_size(e.path());
    packedTotal += packRoundTrip(e.path(), level, 0);
    plainTotal += packRoundTrip(e.path(), false, 0);
  }
  INFO("saved search: raw " << raw << " bytes, packed " << packedTotal);
  CHECK(packedTotal * 2 < raw);
  CHECK(packedTotal < plainTotal);
  std::filesystem::remove_all(dir);
}
