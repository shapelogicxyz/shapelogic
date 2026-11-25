// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding solves in the solve session (new cases beside the port of the
// original's test_sliding.cpp): threads, pauses and save points, abort, the
// notes, Find Solutions off and Just Count, the playback's routes, and the
// orientations the pieces are placed in.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"
#include "solve_session_test_access.h"

#include "shapelogic/app/playback.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/puzzle_file.h"
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
#include <fstream>
#include <map>
#include <memory>
#include <optional>
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
  tray.setStartPiece(0, 0, 0, a + 1);
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
    tray.setStartPiece(k % side, k / side, 0, id + 1);
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

// The same for a test that tries again when the run is through first:
// false when the run ended before it searched a start.
bool searchingOrEnded(const SolveSession& s) {
  for (;;) {
    const SolveProgress p = s.progress();
    if (p.stage == SolveStage::Other && p.searched > 0) {
      return true;
    }
    if (!isActive(s.state())) {
      return false;
    }
    std::this_thread::yield();
  }
}

// Asks for a save point as soon as the run takes one (it does nothing while
// the run prepares), and until the run has ended.
void askSavePoint(SolveSession& s) {
  while (isActive(s.state())) {
    s.pauseAtSavePoint();
    std::this_thread::yield();
  }
}

// Waits until the run's search goes deeper than `depth` moves.
void untilDeeper(const SolveSession& s, std::uint64_t depth) {
  while (s.progress().searchDepth <= depth) {
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
  // progress and live preview pictures read meanwhile
  Puzzle p = test::loadFixture("sliding/test_sliding_nested.xmpuzzle");
  p.getProblem(0).removeAllSolutions();
  SolveOptions o = findSolutions(4);
  o.sliding.nested = true;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  std::vector<SearchSnapshot> shots;
  while (isActive(s.state())) {
    (void)s.progress();
    (void)s.stats();
    if (std::optional<SearchSnapshot> shot = s.takeSample()) {
      shots.push_back(std::move(*shot));
    }
    s.requestSample();
    std::this_thread::yield();
  }
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  CHECK(pr.getSavedSolution(0).getDisassembly()->getMoves() == 47);
  CHECK(s.note().empty());
  // every piece placed, turned and in the layer the start has it (a
  // piece slides, it does not turn)
  CHECK_FALSE(shots.empty());
  const Assembly& start = pr.getSavedSolution(0).getAssembly();
  for (const SearchSnapshot& shot : shots) {
    CHECK(shot.source == SampleSource::Search);
    REQUIRE(shot.placements.size() == start.placementCount());
    for (unsigned int i = 0; i < start.placementCount(); i++) {
      CHECK(shot.placements[i].transformation == start.getTransformation(i));
      CHECK(shot.placements[i].zpos == start.getZ(i));
    }
  }
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

TEST_CASE("sliding session: a save point lets a search with no folder finish, a pause does not",
          "[sliding][session]") {
  // A 4x4 tray of fifteen with a swap no sliding makes: the search stops at
  // its memory limit, kept small here. With no folder it has nowhere to
  // save itself at a save point.
  SolveOptions o = findSolutions(2);
  o.sliding.memoryBytes = 8000000;
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    untilSearching(s);
    s.pauseAtSavePoint();
    s.wait();
    // The one start was searched to its limit and counted. The assembler
    // had no other start to look for, so its search was through when it
    // came to the stop, and the solve is finished (with anything left of
    // the assembly search, it would be paused there, its position saved).
    REQUIRE(s.state() == SolveState::Finished);
    const Problem& pr = p.getProblem(0);
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 0);
    CHECK(pr.getPending().empty());
    CHECK(s.note().starts_with(
        "1 start layout was not searched completely: the search reached its memory limit of "));
    CHECK(s.note().ends_with(
        " arrangements. There may be solutions the solver did not find. Try Enable High "
        "Memory."));
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

// A search with a folder stops at a save point between two levels, saved,
// its start kept: the run is Paused and says it paused there, and the next
// run, asked at once, still ends a level further. A pause, and a search
// that ran out of disk meanwhile, do not say so.
TEST_CASE("sliding session: a save point pauses a search with a folder between two levels",
          "[sliding][session][resume][parallel]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_savepoint";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 8000000;
  {
    Puzzle p = swapped(4);
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    untilSearching(s);
    untilDeeper(s, 2);
    CHECK_FALSE(s.progress().atSavePoint);
    s.pauseAtSavePoint();
    // the search goes on to the level's end: it is not told as stopping
    CHECK(s.progress().stage != SolveStage::Stopping);
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    const Problem& pr = p.getProblem(0);
    CHECK(s.progress().atSavePoint);
    CHECK(pr.getNumAssemblies() == 0);
    REQUIRE(pr.getPending().size() == 1);
    CHECK_FALSE(pr.getPending()[0].counted);
    CHECK(s.note().empty());
    CHECK_FALSE(std::filesystem::is_empty(dir));
    const std::uint64_t depth = pr.getSearchDepth();
    const std::uint64_t searched = pr.getSearched();
    CHECK(depth > 2);
    CHECK(searched > 0);

    // carried on and asked again as soon as it searches: a level further
    for (int i = 0; i < 3; i++) {
      const std::uint64_t before = pr.getSearchDepth();
      const std::uint64_t count = pr.getSearched();
      REQUIRE(s.resume().has_value());
      CHECK_FALSE(s.progress().atSavePoint);
      // asked for before the start's search has begun
      askSavePoint(s);
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
      CHECK(s.progress().atSavePoint);
      CHECK(pr.getSearchDepth() > before);
      // what the level visited is kept, none of it taken back
      CHECK(pr.getSearched() > count);
      REQUIRE(pr.getPending().size() == 1);
    }

    // a pause is not a save point
    REQUIRE(s.resume().has_value());
    untilDeeper(s, pr.getSearchDepth());
    s.pauseAtSavePoint();
    s.pause();
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    CHECK_FALSE(s.progress().atSavePoint);
    s.abort();
    CHECK(std::filesystem::is_empty(dir));
  }
  {
    // out of disk with a save point asked for: paused for the disk, which
    // the user is to see, not for the save
    SolveOptions d = o;
    d.sliding.memoryBytes = 1;
    d.sliding.diskBudget = 1;
    d.settingsPlace = "Settings";
    Puzzle p = swapped(3);
    SolveSession s(p, p.getProblem(0), d);
    REQUIRE(s.start().has_value());
    // asked for once the start is being searched (before that the
    // assembly search would stop, with no start found)
    while (isActive(s.state())) {
      if (s.progress().searched > 0) {
        s.pauseAtSavePoint();
      }
      std::this_thread::yield();
    }
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    if (s.progress().atSavePoint) {
      // stopped after its first level, before it had files to count
      // against the disk: the next level's end finds the disk full
      CHECK(p.getProblem(0).getPending().size() == 1);
      REQUIRE(s.resume().has_value());
      askSavePoint(s);
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
    }
    CHECK_FALSE(s.progress().atSavePoint);
    CHECK(s.note().find("press Continue") != std::string::npos);
    s.abort();
  }
  std::filesystem::remove_all(dir);
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

TEST_CASE("sliding session: the holes are set up as a solve starts", "[sliding][session]") {
  // the holes follow the floor; the pieces need no colors (an older file's
  // start marks were its cell colors: converted as it loads, none left)
  Puzzle p = test::loadFixture("test_sliding_solver.xmpuzzle");
  Problem& pr = p.getProblem(0);
  pr.removeAllSolutions();
  CHECK(p.colorNumber() == 0);
  for (unsigned int s = 1; s < p.getNumberOfShapes(); s++) {
    CHECK(pieceColor(p, s) == 0);
  }
  CHECK(p.getProblem(0).slideMarks().any(false));
  pr.setMaxHoles(9);
  const Puzzle before = p;
  REQUIRE(prepareForSolving(p, pr));
  CHECK_FALSE(prepareForSolving(p, pr));
  CHECK(pr.getMaxHoles() == 2);

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
  tray.setStartPiece(0, 0, 0, a + 1);
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
  tray.setStartPiece(0, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), a + 1);
  for (const int x : starts) {
    tray.setStartPiece(static_cast<unsigned int>(x), 0, 0, b + 1);
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
    const std::vector<bool> free = freeStartParts(p, p.getProblem(0));
    CHECK(std::none_of(free.begin(), free.end(), [](bool f) { return f; }));
    const SlideTotals t = straightSolve(p, findSolutions(1), false);
    CHECK(t.assemblies == 1);
    CHECK(t.solutions == 1);
  }
  // a start for one copy: the other starts on any free cell (2 or 3), never
  // with the start left empty
  {
    Puzzle p = twoCopies({1}, {2, 3});
    CHECK(startGoalError(p, 0).empty());
    // b (shape 2), with a start for one of its two copies, may start elsewhere
    const Problem& pr = p.getProblem(0);
    const std::vector<bool> free = freeStartParts(p, pr);
    for (unsigned int part = 0; part < pr.getNumberOfParts(); part++) {
      CHECK(free[part] == (pr.getShapeIdOfPart(part) == 2));
    }
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

TEST_CASE("sliding: more than 64 pieces with goals for some of their copies", "[sliding]") {
  // 70 unit pieces, two copies each, in a 71x2 tray: both copies of piece
  // k start in column k, a goal for one of them at its top. The goal check
  // counts each piece's copies on its goals, one count per piece: more
  // than 64 of them (marks go to 255). Piece A, at the top of the last
  // column, has its goal below. The search starts from the starts (a
  // solve's assembler would place all 141 pieces first, one level of its
  // recursion each).
  constexpr unsigned int kParts = 70;
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, kParts + 1, 2);
  const unsigned int a = addBlock(puz, 1, 1);
  std::vector<unsigned int> unit;
  for (unsigned int k = 0; k < kParts; k++) {
    unit.push_back(addBlock(puz, 1, 1));
  }
  Voxel& tray = puz.getShape(trayId);
  for (unsigned int k = 0; k < kParts; k++) {
    tray.setStartPiece(k, 0, 0, unit[k] + 1);
    tray.setStartPiece(k, 1, 0, unit[k] + 1);
    tray.setGoalPiece(tray.getIndex(k, 0, 0), unit[k] + 1);
  }
  tray.setStartPiece(kParts, 0, 0, a + 1);
  tray.setGoalPiece(tray.getIndex(kParts, 1, 0), a + 1);
  syncSlidingProblems(puz);
  Problem& pr = puz.getProblem(0);
  for (const unsigned int u : unit) {
    pr.setShapeMaximum(puz, u, 2);
    pr.setShapeMinimum(puz, u, 2);
  }
  syncSlidingProblems(puz);
  REQUIRE(startGoalError(puz, 0).empty());

  std::string why;
  const std::optional<std::vector<TrayCell>> at = startLayout(puz, pr, &why);
  INFO(why);
  REQUIRE(at.has_value());
  const std::vector<LayoutPiece> pieces = layoutPieces(pr);
  REQUIRE(pieces.size() == 2 * kParts + 1);
  Assembly start(puz.getGridType());
  for (std::size_t i = 0; i < pieces.size(); i++) {
    const Voxel& v = puz.getShape(pieces[i].shapeId);
    start.addPlacement(0, (*at)[i][0] + v.getHx(), (*at)[i][1] + v.getHy(),
                       (*at)[i][2] + v.getHz());
  }
  // one move: A down
  const std::optional<Separation> path = findSlidePath(puz, pr, start);
  REQUIRE(path.has_value());
  CHECK(path->getMoves() == 1);
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
      tray.setStartPiece(x, y, 0, d + 1);
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
  tray.setStartPiece(0, 0, 0, a + 1);
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
// A paused sliding solve whose searches are saved is carried on as the
// search it is: options that name another search (they would find nothing
// saved under their name, search from the first move and leave the saved
// search where it is) are told before the run, for the owner to refuse.
TEST_CASE("sliding session: a paused search is not carried on as another search",
          "[sliding][session][resume][parallel]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_other_search";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 8000000;
  SolveOptions nested = o;
  nested.sliding.nested = true;
  Puzzle p = swapped(4);
  SolveSession s(p, p.getProblem(0), o);
  // nothing run yet: nothing to carry on
  CHECK(s.resumeRefusal(nested).empty());
  REQUIRE(s.start().has_value());
  untilSearching(s);
  untilDeeper(s, 2);
  // not while it runs either
  CHECK(s.resumeRefusal(nested).empty());
  s.pauseAtSavePoint();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  REQUIRE_FALSE(std::filesystem::is_empty(dir));

  // its own options, and others that name the same search
  CHECK(s.resumeRefusal(o).empty());
  SolveOptions same = o;
  same.solutionLimit = 3;
  same.threads = 1;
  same.justCount = true;
  same.sliding.memoryBytes = 4000000;
  // (these two play a part only with the options they hang on)
  same.sliding.partialNested = true;
  same.sliding.mirroredOrientations = true;
  CHECK(s.resumeRefusal(same).empty());

  // another search: each option that names it is told
  const std::string why = s.resumeRefusal(nested);
  CHECK(why.find("(nested slides)") != std::string::npos);
  CHECK(why.find("Abort, then Solve") != std::string::npos);
  SolveOptions other = nested;
  other.sliding.partialNested = true;
  other.sliding.verticalMoves = true;
  other.sliding.anyOrientation = true;
  other.sliding.mirroredOrientations = true;
  other.sliding.grid = 2;
  CHECK(s.resumeRefusal(other).find(
            "(nested slides, partially nested slides, vertical moves, any orientation for "
            "unplaced pieces, mirrored orientations, optimized paths)") != std::string::npos);

  // the saved search gone (deleted from the cache): nothing is left behind
  std::filesystem::remove_all(dir);
  CHECK(s.resumeRefusal(nested).empty());
  s.abort();
  std::filesystem::remove_all(dir);
}

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
  untilDeeper(s, 2);
  s.pause();
  // stopping, it says it pauses, and that a slide search is under way
  const SolveProgress stopping = s.progress();
  if (stopping.stage == SolveStage::Stopping) {
    CHECK(stopping.pausing);
    CHECK(stopping.slideStopping);
  }
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getPending().size() == 1);
  // the one start is not searched through: the bar does not say it is
  // (the assembly search, the start found, is)
  CHECK(s.progress().overall == 0.0F);
  // what the run counted is what the problem keeps
  const std::uint64_t searched = pr.getSearched();
  CHECK(searched > 0);
  CHECK(s.progress().searched == searched);
  CHECK(pr.getSearchDepth() > 0);
  CHECK(s.progress().searchDepth == pr.getSearchDepth());
  // the saved search, in its own folder
  REQUIRE(std::filesystem::is_directory(dir));
  CHECK_FALSE(std::filesystem::is_empty(dir));
  // carried on: the sets live in the folder now and the memory limit no
  // longer stops the search, so it is paused once more rather than run to
  // its end
  REQUIRE(s.resume().has_value());
  // the count goes on from the problem's, counting the search's saved
  // arrangements once
  CHECK(s.progress().searched >= searched);
  untilDeeper(s, pr.getSearchDepth());
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK_FALSE(std::filesystem::is_empty(dir));
  CHECK(pr.getSearched() > searched);
  CHECK(s.progress().searched == pr.getSearched());
  CHECK(s.progress().searchDepth == pr.getSearchDepth());
  // a solve from the start throws the saved search away
  s.abort();
  CHECK(std::filesystem::is_empty(dir));
  CHECK(pr.getSearched() == 0);
  std::filesystem::remove_all(dir);
}

// A start's search that runs out of disk is saved and the run pauses with
// that start pending, so Continue carries it on (the original's message
// said to solve again, which threw the search away).
TEST_CASE("sliding session: a search out of disk pauses for Continue",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_disk";
  std::filesystem::remove_all(dir);
  // one start whose search visits every one of 181,440 arrangements, its
  // levels on disk from the first and the disk it may take one byte
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 1;
  o.sliding.diskBudget = 1;
  o.settingsPlace = "Settings";
  Puzzle p = swapped(3);
  {
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    CHECK(p.getProblem(0).getPending().size() == 1);
    CHECK(s.note().find("press Continue") != std::string::npos);
    CHECK_FALSE(std::filesystem::is_empty(dir));
    // carried on with no more room: paused again, still pending
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    CHECK(p.getProblem(0).getPending().size() == 1);
  }
  // given room, Continue searches it to its end: no path, proven
  o.sliding.memoryBytes = 0;
  o.sliding.diskBudget = 0;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(p.getProblem(0).getPending().empty());
  CHECK(p.getProblem(0).getNumSolutions() == 0);
  CHECK(s.note().find("Every reachable arrangement was searched") != std::string::npos);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A search that cannot write a file (a disk that fills while a level is
// made) pauses the run as one that filled its disk budget does, its start
// waiting: saved when it could be, and when not, to be searched again from
// the beginning. Either way the solve is not carried to its end with that
// start passed over, and it does not say it paused at a save point, which
// would carry it on by itself.
TEST_CASE("sliding session: a search that cannot write its files pauses for Continue",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_write";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 1;
  o.settingsPlace = "Settings";
  for (const bool saved : {false, true}) {
    INFO((saved ? "saved" : "not saved"));
    // not even the first file written; or one write failed with the
    // search well under way, and room again after
    o.sliding.failWriteAt = saved ? 200000 : 1;
    o.sliding.failWriteOnce = saved;
    Puzzle p = swapped(3);
    {
      SolveSession s(p, p.getProblem(0), o);
      REQUIRE(s.start().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
      // not a pause at a save point, which whoever asked would carry on
      CHECK_FALSE(s.progress().atSavePoint);
      CHECK(p.getProblem(0).getPending().size() == 1);
      CHECK(p.getProblem(0).getNumAssemblies() == 0);
      CHECK(s.note().find("1 start layout was not searched completely. Could not write the "
                          "search's files in ") != std::string::npos);
      CHECK(s.note().find("Free some disk space to let it go further.") != std::string::npos);
      CHECK((s.note().find("The search is saved: press Continue to carry it on.") !=
             std::string::npos) == saved);
      CHECK((s.note().find("The search could not be saved: Continue searches this start again "
                           "from the beginning.") != std::string::npos) == !saved);
    }
    // with room, Continue searches it to its end: no path, proven
    o.sliding.failWriteAt = 0;
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(p.getProblem(0).getPending().empty());
    CHECK(p.getProblem(0).getNumAssemblies() == 1);
    CHECK(p.getProblem(0).getNumSolutions() == 0);
    CHECK(s.note().find("Every reachable arrangement was searched") != std::string::npos);
  }
  std::filesystem::remove_all(dir);
}

// A start whose saved search cannot be read back (a level file cut short,
// or its state) is not searched afresh: the saved search is kept, the run
// pauses with the start waiting, and says so. Continue tries it again;
// Solve throws it away and searches from the beginning (decision 0041).
TEST_CASE("sliding session: a saved search that cannot be read pauses the solve and is kept",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_unread";
  for (const bool state : {false, true}) {
    INFO((state ? "the state cut short" : "a level file cut short"));
    std::filesystem::remove_all(dir);
    SolveOptions o = findSolutions(2);
    o.sliding.workDir = dir.string();
    o.sliding.memoryBytes = 1;
    o.sliding.diskBudget = 1;
    o.settingsPlace = "Settings";
    Puzzle p = swapped(3);
    {
      SolveSession s(p, p.getProblem(0), o);
      REQUIRE(s.start().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
    }
    const std::vector<std::string> saved = savedFolders(p, p.getProblem(0), dir.string());
    REQUIRE(saved.size() == 1);
    const std::filesystem::path folder = saved[0];
    std::filesystem::path cut = folder / "state.bin";
    if (!state) {
      for (const auto& e : std::filesystem::directory_iterator(folder)) {
        if (e.path().extension() == ".keys") {
          cut = e.path();
          break;
        }
      }
      REQUIRE(cut.extension() == ".keys");
    }
    std::filesystem::resize_file(cut, std::filesystem::file_size(cut) / 2);
    auto listing = [&] {
      std::map<std::string, std::uintmax_t> files;
      for (const auto& e : std::filesystem::directory_iterator(folder)) {
        files[e.path().filename().string()] = e.file_size();
      }
      return files;
    };
    const std::map<std::string, std::uintmax_t> before = listing();
    o.sliding.memoryBytes = 0;
    o.sliding.diskBudget = 0;
    SolveSession s(p, p.getProblem(0), o);
    // Continue, twice: the saved search stays, the start waits
    for (int again = 0; again < 2; again++) {
      REQUIRE(s.resume().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
      CHECK_FALSE(s.progress().atSavePoint);
      CHECK(p.getProblem(0).getPending().size() == 1);
      CHECK(p.getProblem(0).getNumAssemblies() == 0);
      const std::string note = s.note();
      INFO(note);
      CHECK(note.starts_with("1 start layout was not searched completely. The saved search in "));
      CHECK(note.find(" could not be carried on: ") != std::string::npos);
      CHECK(note.find(cut.filename().string()) != std::string::npos);
      CHECK(note.find("It is kept as it is.") != std::string::npos);
      CHECK(note.find("Continue tries to carry the saved search on again; Solve throws it "
                      "away and searches from the beginning.") != std::string::npos);
      CHECK(note.find("Free some disk space") == std::string::npos);
      CHECK(note.find("Every reachable arrangement") == std::string::npos);
      CHECK(listing() == before);
    }
    // Solve: thrown away, searched to its end
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(p.getProblem(0).getPending().empty());
    CHECK(p.getProblem(0).getNumAssemblies() == 1);
    CHECK(s.note().find("Every reachable arrangement was searched") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(folder));
  }
  std::filesystem::remove_all(dir);
}

TEST_CASE("sliding session: a run after a resume that threw is not picking up a search",
          "[sliding][session][resume]") {
  // The slide search sets the "picking up" clock in resume() and clears it
  // once resume() returns: one that threw left it set, and every run after
  // it said it was picking up a saved search.
  Puzzle p = swapped(3);
  SolveSession s(p, p.getProblem(0), findSolutions(1));
  SolveSessionTestAccess::resumeThrew(s);
  REQUIRE(s.start().has_value());
  // the run is active from start() on (Preparing), so the first look sees it
  bool pickingUp = false;
  do {
    pickingUp = pickingUp || s.progress().slidePickingUp;
    std::this_thread::yield();
  } while (isActive(s.state()));
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK_FALSE(pickingUp);
}

// The starts a search gave up on are the solve's, not the run's: a solve
// paused at a save point and carried on (as the crash recovery copy does by
// itself) still says so at its end, not "every reachable arrangement was
// searched".
TEST_CASE("sliding session: starts not searched completely stay counted over a pause",
          "[sliding][session][resume]") {
  const SolveOptions o = findSolutions(1);
  for (unsigned int attempt = 0;; attempt++) {
    REQUIRE(attempt < 200);
    Puzzle p = manyStarts();
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    if (!searchingOrEnded(s)) {
      s.wait();
      continue;  // through before a start was seen searched: again
    }
    s.pauseAtSavePoint();
    s.wait();
    if (s.state() != SolveState::Paused) {
      continue;  // through before the save point: again
    }
    // as if two of the starts searched so far had been given up on
    SolveSessionTestAccess::gaveUpAtMemory(s, 2, 5);
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().starts_with("2 start layouts were not searched completely: the search "
                               "reached its memory limit of 5 arrangements."));
    // a solve begun again counts afresh
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().find("not searched completely") == std::string::npos);
    break;
  }
}

// A saved search the next run cannot carry on (its folder named for a
// build or options that name the search differently) is started afresh;
// once the problem is solved, the saved one goes too.
TEST_CASE("sliding session: a finished solve leaves no saved search of its problem",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_stale";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 1;
  o.sliding.diskBudget = 1;
  Puzzle p = swapped(3);
  {
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
  }
  const std::vector<std::string> saved = savedFolders(p, p.getProblem(0), dir.string());
  REQUIRE(saved.size() == 1);
  const std::filesystem::path stale = dir / "slide-0000000000000000";
  std::filesystem::rename(saved[0], stale);
  REQUIRE(savedFolders(p, p.getProblem(0), dir.string()) ==
          std::vector<std::string>{stale.string()});
  o.sliding.memoryBytes = 0;
  o.sliding.diskBudget = 0;
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK_FALSE(std::filesystem::exists(stale));
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A sliding run whose search folder cannot be written pauses before its
// first search, the starts untouched (decision 0028); Continue tries again,
// and with a folder it can write solves as a run that never paused.
TEST_CASE("sliding session: a folder that cannot be written pauses the run",
          "[sliding][session][parallel]") {
  const std::filesystem::path base =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_folder";
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  const std::filesystem::path file = base / "not-a-folder";
  { std::ofstream(file) << "x"; }
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = file.string();
  const SlideTotals straight = straightSolve(manyStarts(), findSolutions(2), false);
  Puzzle p = manyStarts();
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  const std::optional<FolderFailure> f = s.folderFailure();
  REQUIRE(f);
  CHECK(f->folder == file.string());
  CHECK(p.getProblem(0).getNumAssemblies() == 0);
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK(s.folderFailure());
  o.sliding.workDir = (base / "cache").string();
  REQUIRE(s.setOptions(o).has_value());
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK_FALSE(s.folderFailure());
  CHECK(slideTotals(p.getProblem(0), false) == straight);
  std::filesystem::remove_all(base);
}

// A sliding run that takes apart holds a lease on the shared memory while
// it runs (decision 0028), and gives it back when it pauses.
TEST_CASE("sliding session: a run shares the memory while it runs",
          "[sliding][session][parallel]") {
  SolveOptions o = findSolutions(2);
  o.memoryShare = std::make_shared<MemoryShare>(1000000000);
  Puzzle p = swapped(4);
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  untilSearching(s);
  CHECK(o.memoryShare->holders() == 1);
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK(o.memoryShare->holders() == 0);
  s.abort();
}

// The cells' colors are the user's restriction colors again (the
// original kept the start marks in them): a free piece painted a color
// starts only where the rules let that color go.
TEST_CASE("sliding session: color rules restrict where a free piece starts",
          "[sliding][session]") {
  // a 3x3 tray: A starts at (0,0) and must reach (2,2); B is free
  auto puzzle = [](bool painted) {
    Puzzle puz{GridType(GridKind::Sliding)};
    const unsigned int trayId = addStartGoalShape(puz, 3, 3);
    const unsigned int a = addBlock(puz, 1, 1);
    const unsigned int b = addBlock(puz, 1, 1);
    Voxel& tray = puz.getShape(trayId);
    tray.setStartPiece(0, 0, 0, a + 1);
    tray.setGoalPiece(2, 2, 0, a + 1);
    if (painted) {
      // B is red; the middle cell is blue, and red may not go on blue
      const unsigned int red = puz.addColor(255, 0, 0) + 1;
      const unsigned int blue = puz.addColor(0, 0, 255) + 1;
      puz.getShape(b).setColor(0, 0, 0, red);
      tray.setColor(1, 1, 0, blue);
    }
    syncSlidingProblems(puz);
    Problem& pr = puz.getProblem(0);
    pr.setShapeMaximum(puz, b, 1);
    pr.setShapeMinimum(puz, b, 1);
    return puz;
  };
  Puzzle plain = puzzle(false);
  Puzzle painted = puzzle(true);
  REQUIRE(painted.colorNumber() == 2);
  const SlideTotals all = straightSolve(plain, findSolutions(1), false);
  const SlideTotals ruled = straightSolve(painted, findSolutions(1), false);
  // B starts on any of the eight free cells, or on seven when the blue one
  // is barred to it
  CHECK(all.assemblies == 8);
  CHECK(ruled.assemblies == 7);
  CHECK(ruled.solutions > 0);
  // allowed, the blue cell is a start again
  Puzzle allowed = puzzle(true);
  allowed.getProblem(0).allowPlacement(allowed, 1, 2);
  CHECK(straightSolve(allowed, findSolutions(1), false).assemblies == 8);
}

namespace {

// Optimized paths (decision 0029): a 6x6 board of 3x3 blocks, one cell
// empty. U is a cup, open at y = 0, holding the unit piece p in its pocket;
// F1 and F2 are whole. The blocks go round the empty cell: F1 down, U right
// (carrying p, with nested slides), F2 up, three moves.
Puzzle cupAndBlocks() {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 6, 6);
  const unsigned int u = addBlock(puz, 3, 3);
  puz.getShape(u).setState(1, 0, 0, Voxel::kEmpty);
  puz.getShape(u).setState(1, 1, 0, Voxel::kEmpty);
  const unsigned int f1 = addBlock(puz, 3, 3);
  const unsigned int f2 = addBlock(puz, 3, 3);
  const unsigned int p = addBlock(puz, 1, 1);
  Voxel& tray = puz.getShape(trayId);
  auto stamp = [&tray](unsigned int shape, const Voxel& piece, unsigned int x0, unsigned int y0,
                       bool goal) {
    for (unsigned int y = 0; y < piece.getY(); y++) {
      for (unsigned int x = 0; x < piece.getX(); x++) {
        if (piece.getState(x, y, 0) == Voxel::kEmpty) {
          continue;
        }
        if (goal) {
          tray.setGoalPiece(tray.getIndex(x0 + x, y0 + y, 0), shape + 1);
        } else {
          tray.setStartPiece(x0 + x, y0 + y, 0, shape + 1);
        }
      }
    }
  };
  stamp(u, puz.getShape(u), 0, 0, false);
  stamp(f1, puz.getShape(f1), 3, 0, false);
  stamp(f2, puz.getShape(f2), 0, 3, false);
  stamp(p, puz.getShape(p), 1, 1, false);
  stamp(u, puz.getShape(u), 3, 0, true);
  stamp(f1, puz.getShape(f1), 3, 3, true);
  stamp(f2, puz.getShape(f2), 0, 0, true);
  stamp(p, puz.getShape(p), 4, 1, true);
  syncSlidingProblems(puz);
  return puz;
}

// A corridor of four cells with a pocket above its second: units a and b
// swap places only by b waiting in the pocket, which is off a grid of 2.
Puzzle corridorSwap() {
  Puzzle puz{GridType(GridKind::Sliding)};
  const unsigned int trayId = addStartGoalShape(puz, 4, 2);
  const unsigned int a = addBlock(puz, 1, 1);
  const unsigned int b = addBlock(puz, 1, 1);
  Voxel& tray = puz.getShape(trayId);
  for (const unsigned int x : {0U, 2U, 3U}) {
    tray.setState(x, 1, 0, Voxel::kEmpty);
  }
  tray.setStartPiece(0, 0, 0, a + 1);
  tray.setStartPiece(1, 0, 0, b + 1);
  tray.setGoalPiece(tray.getIndex(1, 0, 0), a + 1);
  tray.setGoalPiece(tray.getIndex(0, 0, 0), b + 1);
  syncSlidingProblems(puz);
  return puz;
}

}  // namespace

TEST_CASE("sliding: optimized paths find the grid a board of blocks fits", "[sliding]") {
  const Puzzle p = cupAndBlocks();
  const SlideGrid g = slideGridFor(p, p.getProblem(0));
  CHECK(g.size == 3);
  // each block stops at 16 places on the empty board, 4 on the grid; p at
  // 36, and on the grid at the 8 where it sits in a cup's pocket
  CHECK(g.stops == 3 * 16 + 36);
  CHECK(g.gridStops == 3 * 4 + 8);

  SlidingOptions o;
  CHECK(searchGrid(p, p.getProblem(0), o) == 0);
  o.optimizedPaths = true;
  CHECK(searchGrid(p, p.getProblem(0), o) == 3);
  o.grid = 2;
  CHECK(searchGrid(p, p.getProblem(0), o) == 2);

  // a board of unit pieces fits no grid that leaves fewer stops
  const Puzzle corridor = corridorSwap();
  CHECK(slideGridFor(corridor, corridor.getProblem(0)).size == 0);
}

TEST_CASE("sliding session: optimized paths find the fewest moves of a board of blocks",
          "[sliding][session]") {
  SolveOptions full = findSolutions(1);
  full.sliding.nested = true;
  Puzzle every = cupAndBlocks();
  SolveSession e(every, every.getProblem(0), full);
  REQUIRE(e.start().has_value());
  e.wait();
  const SlideTotals all = slideTotals(every.getProblem(0), false);
  CHECK(every.getProblem(0).getSlideGrid() == 0);

  SolveOptions optimized = full;
  optimized.sliding.optimizedPaths = true;
  Puzzle p = cupAndBlocks();
  SolveSession s(p, p.getProblem(0), optimized);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  const Problem& pr = p.getProblem(0);
  const SlideTotals grid = slideTotals(pr, false);
  CHECK(grid == all);
  CHECK(pr.getSlideGrid() == 3);
  CHECK(pr.getSearched() < every.getProblem(0).getSearched());
  CHECK(s.note().empty());

  // every stop of the path is on the grid: the blocks on its lines
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation& path = *pr.getSavedSolution(0).getDisassembly();
  CHECK(path.getMoves() == 3);
  for (unsigned int k = 0; k <= path.getMoves(); k++) {
    for (unsigned int i = 0; i < 3; i++) {
      CHECK(path.getState(k).getX(i) % 3 == 0);
      CHECK(path.getState(k).getY(i) % 3 == 0);
    }
  }

  // the stops of a grid cell told apart (the A/B switch): the same result
  SolveOptions apart = optimized;
  apart.sliding.gridStopsApart = true;
  Puzzle q = cupAndBlocks();
  SolveSession a(q, q.getProblem(0), apart);
  REQUIRE(a.start().has_value());
  a.wait();
  CHECK(slideTotals(q.getProblem(0), false) == all);
  CHECK(pr.getSearched() <= q.getProblem(0).getSearched());

  // the grid goes with the results
  p.getProblem(0).removeAllSolutions();
  CHECK(p.getProblem(0).getSlideGrid() == 0);
}

// What the Solver asks before Continue (decision 0033): this build saves a
// search with nested slides on a grid from both ends and carries it on so; a
// search saved from the start alone, as the versions before saved it, goes
// on that way, and the paused problem says so.
TEST_CASE("sliding session: a paused search saved from the start alone is told",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_alone";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.nested = true;
  o.sliding.optimizedPaths = true;
  o.sliding.workDir = dir.string();
  for (const bool oneEnd : {false, true}) {
    INFO("saved from the start alone " << oneEnd);
    Puzzle p = cupAndBlocks();
    const Problem& pr = p.getProblem(0);
    CHECK_FALSE(pendingFromStartAlone(p, pr, o.sliding));
    {
      // out of disk with its first level: saved, the run paused
      SolveOptions tight = o;
      tight.sliding.memoryBytes = 1;
      tight.sliding.diskBudget = 1;
      tight.sliding.oneEnd = oneEnd;
      SolveSession s(p, p.getProblem(0), tight);
      REQUIRE(s.start().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
      REQUIRE(pr.getPending().size() == 1);
      CHECK_FALSE(std::filesystem::is_empty(dir));
    }
    CHECK(pendingFromStartAlone(p, pr, o.sliding) == oneEnd);
    // carried on as it was begun, to the same three moves
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    REQUIRE(pr.getNumberOfSavedSolutions() == 1);
    CHECK(pr.getSavedSolution(0).getDisassembly()->getMoves() == 3);
    CHECK(std::filesystem::is_empty(dir));
    CHECK_FALSE(pendingFromStartAlone(p, pr, o.sliding));
  }
  std::filesystem::remove_all(dir);
}

// Solve starts afresh: a saved search left in the folder would be carried
// on by the search of its start, from the start alone if it was saved so.
TEST_CASE("sliding session: Solve throws the saved search away", "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_afresh";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.nested = true;
  o.sliding.optimizedPaths = true;
  o.sliding.workDir = dir.string();
  // out of disk with its first level: saved, the run paused
  SolveOptions tight = o;
  tight.sliding.memoryBytes = 1;
  tight.sliding.diskBudget = 1;
  Puzzle p = cupAndBlocks();
  const Problem& pr = p.getProblem(0);
  auto folders = [&] {
    return std::distance(std::filesystem::directory_iterator(dir),
                         std::filesystem::directory_iterator());
  };
  {
    SolveOptions alone = tight;
    alone.sliding.oneEnd = true;
    SolveSession s(p, p.getProblem(0), alone);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    CHECK(folders() == 1);
    CHECK(pendingFromStartAlone(p, pr, o.sliding));
  }
  {
    // solved again, paused the same way: the search saved now is this
    // run's, from both ends
    SolveSession s(p, p.getProblem(0), tight);
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    REQUIRE(pr.getPending().size() == 1);
    CHECK(folders() == 1);
    CHECK_FALSE(pendingFromStartAlone(p, pr, o.sliding));
  }
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  CHECK(pr.getSavedSolution(0).getDisassembly()->getMoves() == 3);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// The same board as release v1.8.1 left it (tests/data/slide_paused_by_v1.8.1:
// built and solved through its engine with nested slides and optimized
// paths, paused by a disk budget of nothing, the document saved): its
// search is found by the name that version gave it, told as saved from the
// start alone, carried on by Continue and thrown away by Solve.
TEST_CASE("sliding session: a search paused in v1.8.1 is told, carried on or solved again",
          "[sliding][session][resume]") {
  const std::filesystem::path from = "tests/data/slide_paused_by_v1.8.1";
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_v181";
  SolveOptions o = findSolutions(2);
  o.sliding.nested = true;
  o.sliding.optimizedPaths = true;
  o.sliding.workDir = dir.string();
  for (const bool again : {false, true}) {
    INFO("solved again " << again);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::filesystem::copy(from / "slide-cafc026be534476e", dir / "slide-cafc026be534476e");
    Result<Puzzle> loaded = io::loadPuzzle(from / "cup_and_blocks.xmpuzzle");
    REQUIRE(loaded.has_value());
    Puzzle& p = *loaded;
    const Problem& pr = p.getProblem(0);
    REQUIRE(pr.getPending().size() == 1);
    CHECK(pr.getSlideGrid() == 3);
    CHECK(pendingFromStartAlone(p, pr, o.sliding));
    // not the search a solve without the grid, or without nested slides, looks for
    SolveOptions other = o;
    other.sliding.optimizedPaths = false;
    CHECK_FALSE(pendingFromStartAlone(p, pr, other.sliding));
    other = o;
    other.sliding.nested = false;
    CHECK_FALSE(pendingFromStartAlone(p, pr, other.sliding));

    SolveSession s(p, p.getProblem(0), o);
    REQUIRE((again ? s.start() : s.resume()).has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    REQUIRE(pr.getNumberOfSavedSolutions() == 1);
    CHECK(pr.getSavedSolution(0).getDisassembly()->getMoves() == 3);
    CHECK(std::filesystem::is_empty(dir));
    CHECK_FALSE(pendingFromStartAlone(p, pr, o.sliding));
    // Continue carried the saved search on, its level files (which that
    // version did not compress) written again first (decision 0041); Solve
    // carried nothing on
    const std::vector<LogLine> log = s.takeLog();
    const auto told = [&log](const char* words) {
      return std::any_of(log.begin(), log.end(), [words](const LogLine& l) {
        return l.text.find(words) != std::string::npos;
      });
    };
    CHECK(told("was written by an earlier version of the program") == !again);
    CHECK(told("Written again compressed: 2 files of the saved search") == !again);
    CHECK(told("Carried on the saved search in ") == !again);
    CHECK_FALSE(told("could not be carried on"));
  }
  std::filesystem::remove_all(dir);
}

// The "1 to 8 Heart" puzzle (decision 0044): its first board was paused by
// a version before uids, and its second is a copy of it in every cell,
// never solved. The search was named from the problem's cells alone, so
// the copy found it as its own, and the cache manager named the copy as
// the paused one. Now only the problem the file says is paused keeps the
// name without a uid; the copy has a uid, and searches of its own.
TEST_CASE("sliding session: a copy of a problem paused before uids does not take its search",
          "[sliding][session][resume]") {
  const std::filesystem::path from = "tests/data/slide_paused_by_v1.8.1";
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_uid_copy";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::filesystem::copy(from / "slide-cafc026be534476e", dir / "slide-cafc026be534476e");
  Result<std::string> xml = io::readMaybeGzipFile(from / "cup_and_blocks.xmpuzzle");
  REQUIRE(xml.has_value());
  // the paused problem, then a copy of its definition, unsolved
  const std::size_t open = xml->find("  <problem ");
  const std::size_t end = xml->find("  </problem>\n");
  REQUIRE(open != std::string::npos);
  REQUIRE(end != std::string::npos);
  std::string copy = xml->substr(open, end + 13 - open);
  copy.replace(copy.find("state=\"1\""), 9, "state=\"0\"");
  copy.erase(copy.find("   <pending>"), copy.find("</pending>\n") + 11 - copy.find("   <pending>"));
  xml->insert(end + 13, copy);
  Result<Puzzle> loaded = io::parsePuzzle(*xml);
  REQUIRE(loaded.has_value());
  Puzzle& p = *loaded;
  REQUIRE(p.getNumberOfProblems() == 2);
  const Problem& paused = p.getProblem(0);
  const Problem& twin = p.getProblem(1);
  CHECK(paused.getSolveState() == ProblemState::Solving);
  CHECK(twin.getSolveState() == ProblemState::Unsolved);
  CHECK(paused.getUid() == 0);
  CHECK(twin.getUid() != 0);

  // the saved search is the paused problem's alone, and so is its lock
  CHECK(savedFolders(p, paused, dir.string()).size() == 1);
  CHECK(savedFolders(p, twin, dir.string()).empty());
  CHECK(solveLockPath(p, paused, dir.string()) != solveLockPath(p, twin, dir.string()));

  // saved and read again: the same uids, so the same searches
  Result<Puzzle> again = io::parsePuzzle(io::writePuzzle(p));
  REQUIRE(again.has_value());
  CHECK(again->getProblem(0).getUid() == 0);
  CHECK(again->getProblem(1).getUid() == twin.getUid());
  CHECK(savedFolders(*again, again->getProblem(0), dir.string()).size() == 1);

  // the copy solved and paused has a search of its own beside the first's
  SolveOptions o = findSolutions(2);
  o.sliding.nested = true;
  o.sliding.optimizedPaths = true;
  o.sliding.workDir = dir.string();
  o.sliding.memoryBytes = 1;
  o.sliding.diskBudget = 1;
  SolveSession s(p, p.getProblem(1), o);
  REQUIRE(s.start().has_value());
  s.wait();
  const std::vector<std::string> mine = savedFolders(p, twin, dir.string());
  CHECK(mine.size() == 1);
  CHECK(mine != savedFolders(p, paused, dir.string()));
  CHECK(savedFolders(p, paused, dir.string()).size() == 1);
  std::filesystem::remove_all(dir);
}

TEST_CASE("sliding session: optimized paths say a full search may find what they miss",
          "[sliding][session]") {
  Puzzle every = corridorSwap();
  const SlideTotals all = straightSolve(every, findSolutions(1), false);
  CHECK(all.solutions == 1);
  REQUIRE(all.endings.size() == 1);
  CHECK(all.endings.begin()->ends_with(" 4"));

  // held to a grid of 2, b cannot wait in the pocket
  SolveOptions o = findSolutions(1);
  o.sliding.grid = 2;
  Puzzle p = corridorSwap();
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(p.getProblem(0).getNumSolutions() == 0);
  CHECK(p.getProblem(0).getSlideGrid() == 2);
  CHECK(s.note() ==
        "Every arrangement optimized paths reach was searched: with the pieces stopping only on "
        "a 2-cell grid, none reaches the goal. A full search, which tries every stop, may still "
        "find a solution.");

  // the legacy search keeps to the grid too
  o.sliding.legacy = true;
  Puzzle legacy = corridorSwap();
  SolveSession l(legacy, legacy.getProblem(0), o);
  REQUIRE(l.start().has_value());
  l.wait();
  CHECK(legacy.getProblem(0).getNumSolutions() == 0);
  o.sliding.grid = 0;
  Puzzle legacyAll = corridorSwap();
  CHECK(straightSolve(legacyAll, o, false).solutions == 1);
}

TEST_CASE("sliding session: each move's time is kept while the solve is open",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_level_times";
  std::filesystem::remove_all(dir);
  // the 4x4 tray of fifteen with the swap: searched long enough to pause
  // several moves deep
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  Puzzle p = swapped(4);
  SolveSession s(p, p.getProblem(0), o);
  REQUIRE(s.start().has_value());
  untilSearching(s);
  SolveProgress g = s.progress();
  while (g.slideLevelMs.size() < 8 && isActive(s.state())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    g = s.progress();
  }
  REQUIRE(g.slideLevelMs.size() >= 8);
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  // the problem keeps them, one a move, as deep as the search went
  const Problem& pr = p.getProblem(0);
  const std::vector<std::uint64_t> kept = pr.getSlideLevelMs();
  CHECK(kept.size() >= g.slideLevelMs.size());
  CHECK(std::equal(g.slideLevelMs.begin(), g.slideLevelMs.end(), kept.begin()));
  CHECK(s.progress().slideLevelRunningMs == 0);

  // carried on: the times go on from where they were
  REQUIRE(s.resume().has_value());
  untilSearching(s);
  g = s.progress();
  while (g.slideLevelMs.size() <= kept.size() && isActive(s.state())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    g = s.progress();
  }
  REQUIRE(g.slideLevelMs.size() > kept.size());
  CHECK(std::equal(kept.begin(), kept.end(), g.slideLevelMs.begin()));
  s.abort();
  // thrown away with the solve
  CHECK(p.getProblem(0).getSlideLevelMs().empty());
  std::filesystem::remove_all(dir);

  // a solve that is through keeps none
  SolveOptions nested = findSolutions(1);
  nested.sliding.nested = true;
  Puzzle done = cupAndBlocks();
  SolveSession d(done, done.getProblem(0), nested);
  REQUIRE(d.start().has_value());
  d.wait();
  REQUIRE(d.state() == SolveState::Finished);
  CHECK(done.getProblem(0).getSlideLevelMs().empty());
}

TEST_CASE("sliding session: a paused solve opened without times goes on from untimed moves",
          "[sliding][session][resume]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_untimed";
  std::filesystem::remove_all(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.workDir = dir.string();
  Puzzle p = swapped(4);
  {
    SolveSession s(p, p.getProblem(0), o);
    REQUIRE(s.start().has_value());
    untilSearching(s);
    while (s.progress().slideLevelMs.size() < 8 && isActive(s.state())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    s.pause();
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
  }
  const std::uint64_t depth = p.getProblem(0).getSlideLevelMs().size();
  REQUIRE(depth >= 8);
  // the problem keeps how deep the search went
  CHECK(p.getProblem(0).getSearchDepth() == depth);

  // the file as a version that kept no times wrote it
  std::string xml = io::writePuzzle(p);
  const std::size_t at = xml.find(" slideLevelMs=\"");
  REQUIRE(at != std::string::npos);
  xml.erase(at, xml.find('"', at + 15) + 1 - at);
  Result<Puzzle> opened = io::parsePuzzle(xml);
  REQUIRE(opened.has_value());
  // its moves searched are there, untimed, and written so
  const Problem& pr = opened->getProblem(0);
  CHECK(pr.getSlideLevelMs() == std::vector<std::uint64_t>(depth, kUntimedMs));
  CHECK(io::writePuzzle(*opened).find(" slideLevelMs=\"- - -") != std::string::npos);

  // carried on: the moves searched since are timed after the untimed ones
  SolveSession s(*opened, opened->getProblem(0), o);
  REQUIRE(s.resume().has_value());
  untilSearching(s);
  SolveProgress g = s.progress();
  while (g.slideLevelMs.size() < depth + 2 && isActive(s.state())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    g = s.progress();
  }
  REQUIRE(g.slideLevelMs.size() >= depth + 2);
  for (std::size_t k = 0; k < g.slideLevelMs.size(); k++) {
    CHECK((g.slideLevelMs[k] == kUntimedMs) == (k < depth));
  }
  s.abort();
  std::filesystem::remove_all(dir);
}

// One solve of a problem at a time works in its search folders (decision
// 0035): while another holds the problem's lock, Solve and Continue are
// refused before anything is dropped, and an abort leaves the saved
// searches to the solve that works in them.
TEST_CASE("sliding session: a problem another solve works on is refused",
          "[sliding][session][lock]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_slide_session_lock";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  SolveOptions o = findSolutions(2);
  o.sliding.nested = true;
  o.sliding.optimizedPaths = true;
  o.sliding.workDir = dir.string();
  // out of disk with its first level: saved, the run paused
  SolveOptions tight = o;
  tight.sliding.memoryBytes = 1;
  tight.sliding.diskBudget = 1;
  Puzzle p = cupAndBlocks();
  const Problem& pr = p.getProblem(0);
  const std::string lockFile = solveLockPath(p, pr, dir.string());
  REQUIRE_FALSE(lockFile.empty());
  // the same problem in another document (a copy of the file: the same
  // uid) is the same lock; one made alike is not (decision 0044); no
  // folder, no lock
  {
    const Puzzle copy = p;
    CHECK(solveLockPath(copy, copy.getProblem(0), dir.string()) == lockFile);
    const Puzzle alike = cupAndBlocks();
    CHECK(solveLockPath(alike, alike.getProblem(0), dir.string()) != lockFile);
    CHECK(solveLockPath(p, pr, "").empty());
  }
  auto savedSearches = [&] { return savedFolders(p, pr, dir.string()).size(); };

  SolveSession s(p, p.getProblem(0), tight);
  FileLock other;
  REQUIRE(other.tryAcquire(lockFile) == FileLock::Outcome::Held);
  {
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::IoError);
    CHECK(r.error().message == kSolvedElsewhere);
    CHECK(s.state() == SolveState::Idle);
  }
  other.release();

  // the lock is the run's while it goes, and gone when the run is over
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK_FALSE(std::filesystem::exists(lockFile));
  REQUIRE(pr.getPending().size() == 1);
  REQUIRE(savedSearches() == 1);

  // the other solve again: neither Continue nor Solve gets in, and what
  // the paused solve holds stays as it is
  REQUIRE(other.tryAcquire(lockFile) == FileLock::Outcome::Held);
  CHECK_FALSE(s.resume().has_value());
  CHECK_FALSE(s.start().has_value());
  CHECK(s.state() == SolveState::Paused);
  CHECK(pr.getPending().size() == 1);
  CHECK(savedSearches() == 1);
  // an abort throws this solve away, not the searches the other works in
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  CHECK(savedSearches() == 1);
  CHECK(other.held());
  other.release();

  // alone again, the problem solves
  REQUIRE(s.setOptions(o).has_value());
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  CHECK_FALSE(std::filesystem::exists(lockFile));
  std::filesystem::remove_all(dir);
}
