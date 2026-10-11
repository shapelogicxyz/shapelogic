// SPDX-License-Identifier: GPL-3.0-or-later
// Stacking puzzles in the document: solved through it (the discs' hotspots
// set as the GUI's Solve set them), what has no meaning for them refused
// with the grid's words, a paused Panex solve exported with its saved
// search and imported into another work folder, and the original's "undo
// of a disc edit keeps the stacks" (test_stacking.cpp).
#include "app_test_helpers.h"
#include "../model/model_test_helpers.h"

#include "bt/app/document.h"
#include "bt/app/playback.h"
#include "bt/base/solve_state.h"
#include "bt/io/paused_solve.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/stacking.h"
#include "bt/solve/panex_search.h"
#include "bt/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

using namespace bt;
using namespace bt::app;
using test::openFixture;

namespace {

// A folder of this test's own, removed at the end of the case.
class Scratch {
 public:
  Scratch() {
    std::random_device rd;
    path = std::filesystem::temp_directory_path() /
           ("bt_test_document_stacking_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::filesystem::create_directories(path);
  }
  ~Scratch() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  std::filesystem::path path;
};

SolveOptions findSolutions(const std::filesystem::path& work) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = 2;
  o.stacking.workDir = work.string();
  return o;
}

}  // namespace

TEST_CASE("stacking document: a stacking problem solves and plays", "[app][stacking]") {
  const Scratch work;
  // a disc whose hotspot is elsewhere has it at its centre once solving
  // starts, as the GUI's Solve put it (cb_BtnCont's ensureHotspot)
  Puzzle hanoi = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  hanoi.getShape(0).setHotspot(0, 0, 0);
  Document d = Document::openBytes(io::writePuzzle(hanoi)).value();
  const Id p = d.problemIds().front();
  REQUIRE(d.puzzle().getShape(0).getHx() == 0);
  REQUIRE(d.solveStart(p, findSolutions(work.path)).has_value());
  CHECK(d.puzzle().getShape(0).getHx() == 3);
  CHECK(d.puzzle().getShape(0).getHy() == 3);
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Finished);
  const Problem& pr = d.puzzle().getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  CHECK(stacking::logicalMoves(*pr.getSavedSolution(0).getDisassembly()) == 7);
  const Result<PlaybackSampler> pb = d.playback(p, pr.getSavedSolution(0).getId());
  REQUIRE(pb.has_value());
  CHECK(pb->stepCount() == 7);

  // a stacking puzzle has no assembler and no take-apart: refused in the
  // grid's words
  CHECK(d.exploreAssembly(p, SolverType::Classic, {}, false).error().code == Errc::Unsupported);
  AnalyseOptions a;
  CHECK(d.analyseStart(p, a).error().message ==
        "Sorry, this space grid doesn't have a disassembler (yet)!");
}

TEST_CASE("stacking document: a paused Panex solve exported and imported carries on",
          "[app][stacking][parallel][slow]") {
  const Scratch work;
  const std::filesystem::path here = work.path / "here";
  const std::filesystem::path there = work.path / "there";
  Document d = openFixture("stacking/stack_panex_swap_5.xmpuzzle");
  const Id p = d.problemIds().front();
  const std::filesystem::path file = work.path / "swap.btsolve";

  // nothing paused: refused
  CHECK(d.exportPaused(p, file, {here.string(), true}).error().code == Errc::InvalidState);
  SolveOptions o = findSolutions(here);
  o.stacking.solver = stacking::StackingSolver::Panex;
  REQUIRE(d.solveStart(p, o).has_value());
  // while it runs: refused
  CHECK(d.exportPaused(p, file, {here.string(), true}).error().code == Errc::InvalidState);
  REQUIRE(d.solvePause(p).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Paused);
  const std::string saved = panex::savedSearch(d.puzzle(), d.puzzle().getProblem(0), here.string());
  REQUIRE_FALSE(saved.empty());

  for (const bool withLevels : {true, false}) {
    INFO((withLevels ? "with" : "without") << " the levels");
    REQUIRE(d.exportPaused(p, file, {here.string(), withLevels}).has_value());
    // the original's sections, the saved search's packed after the problem
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("searchz/state.bin") != std::string::npos);
    CHECK((bytes.find("searchz/f0.lvl") != std::string::npos) == withLevels);
    CHECK(bytes.find("BTZ1") != std::string::npos);

    // into another work folder, which the import empties first
    std::filesystem::remove_all(there);
    std::filesystem::create_directories(
        std::filesystem::path(
            panex::searchFolder(d.puzzle(), d.puzzle().getProblem(0), there.string())) /
        "stale");
    auto imported = Document::importPaused(file, {}, there.string());
    REQUIRE(imported.has_value());
    Document& e = imported->first;
    const Id q = imported->second;
    CHECK(e.dirty());
    const Problem& pr = e.puzzle().getProblem(0);
    CHECK(panex::savedSearch(e.puzzle(), pr, there.string()) == saved);
    if (withLevels) {
      CHECK(std::filesystem::exists(
          std::filesystem::path(panex::searchFolder(e.puzzle(), pr, there.string())) / "f0.lvl"));
    }
    SolveOptions t = findSolutions(there);
    t.stacking.solver = stacking::StackingSolver::Panex;
    REQUIRE(e.solveResume(q, t).has_value());
    REQUIRE(e.solveWait(q).has_value());
    REQUIRE(e.solveState(q) == SolveState::Finished);
    REQUIRE(e.puzzle().getProblem(0).getNumberOfSavedSolutions() == 1);
    CHECK(stacking::logicalMoves(*e.puzzle().getProblem(0).getSavedSolution(0).getDisassembly()) ==
          343);
    // a finished search keeps nothing
    CHECK(panex::savedSearch(e.puzzle(), e.puzzle().getProblem(0), there.string()).empty());
  }

  // a stacking file whose search has no place (no work folder) is refused,
  // as the original refused it
  CHECK(Document::importPaused(file, {}, "").error().code == Errc::ParseError);

  // the abort of the paused solve throws its saved search away
  REQUIRE(d.solveAbort(p).has_value());
  CHECK(panex::savedSearch(d.puzzle(), d.puzzle().getProblem(0), here.string()).empty());
}

// The original's "undo of a disc edit keeps the stacks": restoring the part
// counts went through zero, which took the discs off the rods. The
// document's undo puts the problem back as it was, stacks and all.
TEST_CASE("stacking document: undo of a disc edit keeps the stacks", "[app][stacking]") {
  Document d = openFixture("stacking/stack_panex_jr.xmpuzzle");
  const Id p = d.problemIds().front();
  const Problem& pr = d.puzzle().getProblem(0);
  const stacking::StackMap start = pr.startStacks();
  const stacking::StackMap goal = pr.goalStacks();
  REQUIRE_FALSE(start.rods.empty());

  // a structural edit: one more copy of the first disc
  const unsigned int firstShape = pr.getShapeIdOfPart(0);
  const Id first = d.shapeIds()[firstShape];
  REQUIRE(d.setPieceCount(p, first, 2, 2).has_value());
  REQUIRE(d.undo().has_value());

  const Problem& after = d.puzzle().getProblem(0);
  CHECK(after.getShapeMaximum(after.getShapeIdOfPart(0)) == 1);
  CHECK(after.startStacks().rods == start.rods);
  CHECK(after.goalStacks().rods == goal.rods);
  CHECK(stacking::setupError(d.puzzle(), after).empty());

  // and a count lowered takes the dropped copy off the rods, back on undo
  REQUIRE(d.setPieceCount(p, first, 0, 0).has_value());
  for (const auto& rod : d.puzzle().getProblem(0).startStacks().rods) {
    for (const stacking::DiskRef& r : rod) {
      CHECK(r.shapeId != firstShape);
    }
  }
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().getProblem(0).startStacks().rods == start.rods);
}
