// SPDX-License-Identifier: GPL-3.0-or-later
// Stacking solves in the solve session (new cases beside the port of the
// original's test_stacking.cpp): threads, pauses carried on from the saved
// search, abort, Solve starting afresh, the refusals and notes of
// cb_BtnCont and runStacking, the Stacking Solver on a puzzle the Panex
// search cannot hold, Just Count, and the progress.
#include "../model/model_test_helpers.h"

#include "shapelogic/base/solve_state.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace bt;
using namespace bt::stacking;

namespace {

// A work folder of this process's own for the saved searches (never a
// fixed name, never the user's cache), removed at exit.
class WorkDir {
 public:
  WorkDir() {
    std::random_device rd;
    path = std::filesystem::temp_directory_path() /
           ("bt_test_stacking_session_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::filesystem::create_directories(path);
  }
  ~WorkDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  WorkDir(const WorkDir&) = delete;
  WorkDir& operator=(const WorkDir&) = delete;
  std::filesystem::path path;
};

std::string workDir() {
  static const WorkDir w;
  return w.path.string();
}

void addSized(Puzzle& puz, Problem& prob, unsigned int size) {
  const unsigned int id = addDisk(puz, size);
  prob.setShapeMaximum(puz, id, 1);
  prob.setShapeMinimum(puz, id, 1);
}

// The classic Panex swap of n discs a tower (test_stacking.cpp's).
Puzzle panexSwap(unsigned int n) {
  Puzzle puz{GridType(GridKind::Stacking)};
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
  return puz;
}

SolveOptions findSolutions(unsigned int threads = 0) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = threads;
  o.stacking.workDir = workDir();
  return o;
}

// The frames of the problem's one saved solution, as text, to compare paths.
std::string framesOf(const Problem& pr) {
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation* sep = pr.getSavedSolution(0).getDisassembly();
  REQUIRE(sep != nullptr);
  std::string out;
  for (unsigned int s = 0; s < sep->getNumStates(); s++) {
    const State& st = sep->getState(s);
    for (unsigned int i = 0; i < st.getPiecenumber(); i++) {
      out += std::to_string(st.getX(i)) + "," + std::to_string(st.getZ(i)) + " ";
    }
    out += "\n";
  }
  return out;
}

// The problem's one solution is a path of `moves` transfers, each one disc
// from one rod to another, that ends in the goal stacking.
void checkPath(const Puzzle& puz, const Problem& pr, unsigned int moves) {
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation& path = *pr.getSavedSolution(0).getDisassembly();
  REQUIRE(logicalMoves(path) == moves);
  for (unsigned int m = 0; m < moves; m++) {
    const State& a = path.getState(m * kStepsPerMove);
    const State& b = path.getState((m + 1) * kStepsPerMove);
    unsigned int moved = 0;
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      if (a.getX(i) != b.getX(i)) {
        moved++;
      }
    }
    CHECK(moved == 1);
  }
  const BoardLayout goal = layoutBoard(puz, pr, true);
  const State& last = path.getState(path.getMoves());
  for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
    CHECK(last.getX(i) == goal.disks[i].x);
    CHECK(last.getZ(i) == goal.disks[i].z);
  }
}

// Waits until the run is past `depth` moves (both ends together) or over.
void waitForDepth(const SolveSession& s, std::uint64_t depth) {
  while (isActive(s.state()) && s.progress().searchDepth < depth) {
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
}

}  // namespace

TEST_CASE("stacking session: one thread and four find the same path", "[stacking][parallel]") {
  std::string one;
  for (const unsigned int threads : {1U, 4U}) {
    Puzzle puz = panexSwap(4);
    Problem& pr = puz.getProblem(0);
    SolveOptions o = findSolutions(threads);
    o.stacking.solver = StackingSolver::Panex;
    SolveSession s(puz, pr, o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().empty());
    CHECK(pr.getSolveState() == ProblemState::Solved);
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 1);
    checkPath(puz, pr, 128);
    // the solution's assembly is the start stacking
    const Assembly& a = pr.getSavedSolution(0).getAssembly();
    const BoardLayout lay = layoutBoard(puz, pr, false);
    REQUIRE(a.placementCount() == lay.disks.size());
    for (unsigned int i = 0; i < a.placementCount(); i++) {
      CHECK(a.getX(i) == lay.disks[i].x);
      CHECK(a.getZ(i) == lay.disks[i].z);
    }
    if (threads == 1) {
      one = framesOf(pr);
    } else {
      CHECK(framesOf(pr) == one);
    }
  }
}

TEST_CASE("stacking session: a paused Panex search carries on to a shortest path",
          "[stacking][parallel][slow]") {
  // A search carried on from its saved levels traces its path back through
  // them, as the original did, and may take another of the shortest paths
  // than one that kept every level in memory; wherever it was paused, it
  // takes the same one.
  std::string resumed;
  // paused at a sweep of depths (0: before the first level), carried on
  for (const std::uint64_t depth : {0ULL, 40ULL, 120ULL, 200ULL, 300ULL}) {
    INFO("paused at depth " << depth);
    Puzzle puz = panexSwap(5);
    Problem& pr = puz.getProblem(0);
    SolveSession s(puz, pr, findSolutions(4));
    REQUIRE(s.start().has_value());
    waitForDepth(s, depth);
    s.pause();
    s.wait();
    if (s.state() == SolveState::Paused) {
      // as the solve thread left it: solved, one start, no solution, a note
      CHECK(pr.getSolveState() == ProblemState::Solved);
      CHECK(pr.getNumAssemblies() == 1);
      CHECK(pr.getNumSolutions() == 0);
      CHECK(pr.getNumberOfSavedSolutions() == 0);
      CHECK(s.note().starts_with("Paused "));
      CHECK(s.note().ends_with(" moves deep. The search is saved: press Continue to carry on, "
                               "even after quitting ShapeLogic."));
      CHECK_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
      REQUIRE(s.resume().has_value());
      s.wait();
    }
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().empty());
    CHECK(pr.getNumSolutions() == 1);
    checkPath(puz, pr, 343);
    if (resumed.empty()) {
      resumed = framesOf(pr);
    } else {
      CHECK(framesOf(pr) == resumed);
    }
    // a finished search keeps nothing
    CHECK(panex::savedSearch(puz, pr, workDir()).empty());
  }
}

TEST_CASE("stacking session: a stopped search continues in a new session",
          "[stacking][parallel][slow]") {
  // as after a reload: the session is gone, the saved search is not
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  {
    SolveSession s(puz, pr, findSolutions(2));
    REQUIRE(s.start().has_value());
    waitForDepth(s, 100);
    // the session's end pauses a running solve (the original's destructor)
  }
  if (panex::savedSearch(puz, pr, workDir()).empty()) {
    SKIP("the search was through before the session ended");
  }
  const std::uint64_t ms = panex::savedMs(puz, pr, workDir());
  SolveSession again(puz, pr, findSolutions(2));
  REQUIRE(again.resume().has_value());
  again.wait();
  REQUIRE(again.state() == SolveState::Finished);
  CHECK(logicalMoves(*pr.getSavedSolution(0).getDisassembly()) == 343);
  // the time of both runs: what the saved search had used, and this run
  CHECK(pr.getUsedMs() >= ms);
}

TEST_CASE("stacking session: abort throws the saved search away", "[stacking][parallel]") {
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  SolveSession s(puz, pr, findSolutions(2));
  REQUIRE(s.start().has_value());
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  REQUIRE_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(panex::savedSearch(puz, pr, workDir()).empty());

  // and while it runs
  REQUIRE(s.start().has_value());
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(panex::savedSearch(puz, pr, workDir()).empty());
}

// A saved search's state is trusted no further than its file: a damaged or
// crafted state.bin (one imported from a .slsolve) claimed a layer of 2^60
// keys, which the search set out to make room for (an audit finding; a
// claim of a few terabytes had the process killed). It is no saved search:
// the search starts afresh.
TEST_CASE("stacking session: a saved search that claims more than its file holds starts afresh",
          "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  panex::PanexSearch first;
  first.workDir = workDir();
  first.threads = 1;
  first.stopAtDepth = 20;
  REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
  REQUIRE(first.saved);
  const std::filesystem::path state =
      std::filesystem::path(panex::searchFolder(puz, pr, workDir())) / "state.bin";
  {
    // the first layer's count, after the 72 bytes of the header
    std::fstream f(state, std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(f.good());
    const std::uint64_t claim = std::uint64_t{1} << 60;
    f.seekp(72);
    f.write(reinterpret_cast<const char*>(&claim), sizeof(claim));
    REQUIRE(f.good());
  }
  // the header alone still describes it ...
  CHECK_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
  // ... but it is not carried on
  panex::PanexSearch again;
  again.workDir = workDir();
  again.threads = 1;
  again.resume = true;
  const std::optional<Separation> path = panex::solve(puz, pr, again);
  REQUIRE(path.has_value());
  CHECK(again.outcome == panex::Outcome::Found);
  CHECK(logicalMoves(*path) == 128);
  CHECK(panex::savedSearch(puz, pr, workDir()).empty());
}

TEST_CASE("stacking session: Solve starts afresh, Continue carries on", "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  SolveSession s(puz, pr, findSolutions(1));
  REQUIRE(s.start().has_value());
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  REQUIRE_FALSE(panex::savedSearch(puz, pr, workDir()).empty());

  // Solve throws the saved search away (the GUI asked first) and solves
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);

  // Continue on a solved stacking problem starts a search too: there is no
  // saved one to carry on
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  CHECK(pr.getNumAssemblies() == 1);
}

TEST_CASE("stacking session: what Solve refuses, and the notes", "[stacking]") {
  SECTION("no rod set") {
    Puzzle puz = panexSwap(3);
    puz.getProblem(0).clearRodSet();
    SolveSession s(puz, puz.getProblem(0), findSolutions());
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(r.error().message == "Choose a rod set with Set Start/Goal Rods");
  }
  SECTION("Find Solutions off") {
    Puzzle puz = panexSwap(3);
    SolveOptions o = findSolutions();
    o.disassemble = false;
    SolveSession s(puz, puz.getProblem(0), o);
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message == "Turn on Find Solutions");
  }
  SECTION("a start that breaks the rules") {
    Puzzle puz = panexSwap(3);
    (void)liftTop(puz, puz.getProblem(0), true, 0);
    SolveSession s(puz, puz.getProblem(0), findSolutions());
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(r.error().message == setupError(puz, puz.getProblem(0)));
  }
  SECTION("the Panex Solver without Panex columns") {
    Puzzle puz = panexSwap(3);
    puz.getRodSet(0).panexColumns = false;
    SolveOptions o = findSolutions();
    o.stacking.solver = StackingSolver::Panex;
    SolveSession s(puz, puz.getProblem(0), o);
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::Unsupported);
    CHECK(r.error().message.find("Panex Style Columns") != std::string::npos);
    // the Stacking Solver takes it
    o.stacking.solver = StackingSolver::Shortest;
    REQUIRE(s.setOptions(o).has_value());
    REQUIRE(s.start().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
  }
  SECTION("no path") {
    // Panex Jr without its pocket cannot move at all
    Puzzle puz{GridType(GridKind::Stacking)};
    RodSet& board = puz.getRodSet(0);
    board.rodCount = 2;
    board.growHeight = false;
    board.definedHeight = 3;
    board.sizeMatters = false;
    board.panexColumns = true;
    Problem& pr = puz.getProblem(puz.addProblem());
    for (unsigned int size = 3; size >= 1; size--) {
      addSized(puz, pr, size);
    }
    pr.setRodSetId(0);
    syncMaps(puz, pr);
    for (unsigned int id = 0; id < 3; id++) {
      REQUIRE(placeDisk(puz, pr, false, id, 0).empty());
      REQUIRE(placeDisk(puz, pr, true, id, 1).empty());
    }
    SolveSession s(puz, pr, findSolutions());
    REQUIRE(s.start().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
    CHECK(s.note() == "Every reachable stacking was searched: this puzzle has no solution.");
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 0);
  }
}

TEST_CASE("stacking session: the Stacking Solver on a board the Panex search cannot hold",
          "[stacking]") {
  // nine rods: more than the Panex search packs
  Puzzle puz{GridType(GridKind::Stacking)};
  puz.getRodSet(0).rodCount = 9;
  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 2);
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, 1, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, 0, 8).empty());
  REQUIRE(placeDisk(puz, pr, true, 1, 8).empty());
  REQUIRE_FALSE(panex::unsupported(puz, pr, true).empty());

  SolveSession s(puz, pr, findSolutions());
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(logicalMoves(*pr.getSavedSolution(0).getDisassembly()) == 3);
  CHECK_FALSE(s.progress().panexSearch);

  // the Panex Solver refuses it, with Panex columns too
  puz.getRodSet(0).panexColumns = true;
  REQUIRE(setupError(puz, pr).empty());
  SolveOptions o = findSolutions();
  o.stacking.solver = StackingSolver::Panex;
  REQUIRE(s.setOptions(o).has_value());
  const Result<void> r = s.start();
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().message == "The Panex Solver handles at most 8 rods.");
}

TEST_CASE("stacking session: Just Count keeps no solution", "[stacking]") {
  Puzzle puz = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  Problem& pr = puz.getProblem(0);
  SolveOptions o = findSolutions();
  o.justCount = true;
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumAssemblies() == 1);
  CHECK(pr.getNumSolutions() == 1);
  CHECK(pr.getNumberOfSavedSolutions() == 0);
  // the stackings it went through are recorded (recordSearchStats)
  CHECK(pr.getSearched() > 0);
}

TEST_CASE("stacking session: the progress of a Panex search", "[stacking][parallel]") {
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  SolveOptions o = findSolutions(2);
  o.stacking.solver = StackingSolver::Panex;
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  CHECK(isActive(s.state()));
  bool sawOther = false;
  while (isActive(s.state())) {
    const SolveProgress p = s.progress();
    if (p.stage == SolveStage::Other) {
      sawOther = true;
      CHECK(p.panexSearch);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  s.wait();
  const SolveProgress p = s.progress();
  CHECK(p.stage == SolveStage::Done);
  // both ends met 343 moves apart; the depth shown is where they stood
  // before the last level (342 here, as the original's GUI showed)
  CHECK(p.searchDepth >= 342);
  CHECK(p.searchDepth <= 343);
  CHECK(p.searched == pr.getSearched());
  CHECK(pr.getSearchDepth() == p.searchDepth);
  // the run may be over before a look sees it searching
  (void)sawOther;
}
