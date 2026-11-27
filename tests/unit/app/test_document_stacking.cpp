// SPDX-License-Identifier: GPL-3.0-or-later
// Stacking puzzles in the document: solved through it (the discs' hotspots
// set as the GUI's Solve set them), what has no meaning for them refused
// with the grid's words, a paused Panex solve exported with its saved
// search and imported into another work folder, and the original's "undo
// of a disc edit keeps the stacks" (test_stacking.cpp).
#include "app_test_helpers.h"
#include "../model/model_test_helpers.h"
#include "../scratch_dir.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/playback.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/app/stacking_edits.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/paused_solve.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

using namespace sl;
using namespace sl::app;
using test::openFixture;
using sl::test::ScratchDir;

namespace {

SolveOptions findSolutions(const std::filesystem::path& work) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = 2;
  o.stacking.workDir = work.string();
  return o;
}

// The classic Panex swap of n discs a tower (test_stacking.cpp's): six
// discs take seconds on one thread.
Puzzle swapPuzzle(unsigned int n) {
  Puzzle puz{GridType(GridKind::Stacking)};
  stacking::RodSet& board = puz.getRodSet(0);
  board.rodCount = 3;
  board.growHeight = false;
  board.definedHeight = n;
  board.sizeMatters = false;
  board.panexColumns = true;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = n; size >= 1; size--) {
      const unsigned int id = stacking::addDisk(puz, size);
      pr.setShapeMaximum(puz, id, 1);
      pr.setShapeMinimum(puz, id, 1);
    }
  }
  pr.setRodSetId(0);
  stacking::syncMaps(puz, pr);
  for (unsigned int i = 0; i < n; i++) {
    REQUIRE(stacking::placeDisk(puz, pr, false, i, 0).empty());
    REQUIRE(stacking::placeDisk(puz, pr, false, n + i, 2).empty());
    REQUIRE(stacking::placeDisk(puz, pr, true, n + i, 0).empty());
    REQUIRE(stacking::placeDisk(puz, pr, true, i, 2).empty());
  }
  REQUIRE(stacking::setupError(puz, pr).empty());
  return puz;
}

// swapPuzzle(n) saved in `folder` and opened.
Document swapDocument(unsigned int n, const std::filesystem::path& folder) {
  const Puzzle puz = swapPuzzle(n);
  const std::filesystem::path file = folder / ("swap" + std::to_string(n) + ".xmpuzzle");
  REQUIRE(io::savePuzzle(puz, file).has_value());
  Result<Document> d = Document::open(file);
  REQUIRE(d.has_value());
  return std::move(*d);
}

}  // namespace

TEST_CASE("stacking document: a stacking problem solves and plays", "[app][stacking]") {
  const ScratchDir work("document_stacking");
  // a disc whose hotspot is elsewhere has it at its center once solving
  // starts, as the GUI's Solve put it (cb_BtnCont's ensureHotspot)
  Puzzle hanoi = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  hanoi.getShape(0).setHotspot(0, 0, 0);
  Document d = Document::openBytes(io::writePuzzle(hanoi)).value();
  const Id p = d.problemIds().front();
  REQUIRE(d.puzzle().getShape(0).getHx() == 0);
  REQUIRE(d.solveStart(p, findSolutions(work.path())).has_value());
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
  CHECK(d.exploreAssembly(p, SolverType::Classic, {}).error().code == Errc::Unsupported);
  AnalyseOptions a;
  CHECK(d.analyseStart(p, a).error().message ==
        "Sorry, this space grid doesn't have a disassembler (yet)!");
}

TEST_CASE("stacking document: a paused Panex solve exported and imported carries on",
          "[app][stacking][parallel][slow]") {
  const ScratchDir work("document_stacking");
  const std::filesystem::path here = work.path() / "here";
  const std::filesystem::path there = work.path() / "there";
  Document d = openFixture("stacking/stack_panex_swap_5.xmpuzzle");
  const Id p = d.problemIds().front();
  const std::filesystem::path file = work.path() / "swap.slsolve";

  // nothing paused: refused
  CHECK(d.exportPaused(p, file, {here.string(), true}).error().code == Errc::InvalidState);
  SolveOptions o = findSolutions(here);
  o.stacking.solver = stacking::StackingSolver::Panex;
  REQUIRE(d.solveStart(p, o).has_value());
  // while it runs: refused, and the rod sets, the discs and its stacks
  // wait; its saved search is not told
  CHECK(d.exportPaused(p, file, {here.string(), true}).error().code == Errc::InvalidState);
  CHECK(StackingEdits(d).addRodSet().error().code == Errc::InvalidState);
  CHECK(StackingEdits(d).setDiscSize(d.shapeIds()[0], 2).error().code == Errc::InvalidState);
  CHECK(StackingEdits(d).moveDisc(p, false, 0, 0, 1).error().code == Errc::InvalidState);
  CHECK_FALSE(d.savedSearch(p, here.string()).value().has_value());
  REQUIRE(d.solvePause(p).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Paused);
  const std::string saved = panex::savedSearch(d.puzzle(), d.puzzle().getProblem(0), here.string());
  REQUIRE_FALSE(saved.empty());
  const std::optional<SavedSearchInfo> info = d.savedSearch(p, here.string()).value();
  REQUIRE(info.has_value());
  CHECK(info->description == saved);
  CHECK(info->ms == panex::savedMs(d.puzzle(), d.puzzle().getProblem(0), here.string()));
  CHECK(info->levelBytes > 0);
  CHECK_FALSE(d.savedSearch(p, there.string()).value().has_value());

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

  // An import that fails part of the way (a file cut short) leaves the
  // saved search that was in the folder as it was, and nothing beside it
  // (decision 0041).
  {
    const std::filesystem::path whole = work.path() / "whole.slsolve";
    REQUIRE(d.exportPaused(p, whole, {here.string(), true}).has_value());
    REQUIRE(Document::importPaused(whole, {}, there.string()).has_value());
    const Problem& pr = d.puzzle().getProblem(0);
    const std::filesystem::path folder = panex::searchFolder(d.puzzle(), pr, there.string());
    auto listing = [&] {
      std::map<std::string, std::uintmax_t> files;
      for (const auto& e : std::filesystem::directory_iterator(folder)) {
        files[e.path().filename().string()] = e.file_size();
      }
      return files;
    };
    const std::map<std::string, std::uintmax_t> before = listing();
    REQUIRE(before.contains("state.bin"));
    REQUIRE(before.contains("f0.lvl"));
    const std::filesystem::path cut = work.path() / "cut.slsolve";
    std::filesystem::copy_file(whole, cut);
    std::filesystem::resize_file(cut, std::filesystem::file_size(cut) - 100);
    CHECK_FALSE(Document::importPaused(cut, {}, there.string()).has_value());
    CHECK(listing() == before);
    CHECK(panex::savedSearch(d.puzzle(), pr, there.string()) == saved);
    std::filesystem::path beside = folder;
    beside += ".import";
    CHECK_FALSE(std::filesystem::exists(beside));
  }

  // Abort of an imported search, which no session holds (the GUI's Abort
  // was offered for a saved search): the search and the results go
  {
    auto imported = Document::importPaused(file, {}, there.string());
    REQUIRE(imported.has_value());
    Document& e = imported->first;
    const Id q = imported->second;
    const std::optional<SavedSearchInfo> held = e.savedSearch(q, there.string()).value();
    REQUIRE(held.has_value());
    CHECK(held->description == saved);
    CHECK(held->levelBytes == 0);  // exported without them
    CHECK(e.solveAbort(q).error().code == Errc::InvalidState);
    const std::uint64_t rev = e.revision();
    REQUIRE(e.solveAbort(q, there.string()).has_value());
    CHECK(e.revision() > rev);
    CHECK_FALSE(e.savedSearch(q, there.string()).value().has_value());
    CHECK(e.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
    CHECK(e.solveAbort(q, there.string()).error().code == Errc::InvalidState);
  }

  // the abort of the paused solve throws its saved search away
  REQUIRE(d.solveAbort(p).has_value());
  CHECK(panex::savedSearch(d.puzzle(), d.puzzle().getProblem(0), here.string()).empty());
}

// The original's "undo of a disc edit keeps the stacks": restoring the part
// counts went through zero, which took the discs off the rods. The
// document's undo puts the problem back as it was, stacks and all. The
// structural edit is the GUI's Add: one more copy of the first disc
// (placeOneDisk), since the counts follow the stacks.
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
  REQUIRE(StackingEdits(d).placeDisc(p, first, 1, false).has_value());
  REQUIRE(d.puzzle().getProblem(0).getShapeMaximum(firstShape) == 2);
  REQUIRE(d.undo().has_value());

  const Problem& after = d.puzzle().getProblem(0);
  CHECK(after.getShapeMaximum(after.getShapeIdOfPart(0)) == 1);
  CHECK(after.startStacks().rods == start.rods);
  CHECK(after.goalStacks().rods == goal.rods);
  CHECK(stacking::setupError(d.puzzle(), after).empty());

  // and a copy taken off goes from both stackings, back on undo
  unsigned int rod = 0;
  unsigned int index = 0;
  bool found = false;
  for (unsigned int r = 0; r < start.rods.size() && !found; r++) {
    for (unsigned int i = 0; i < start.rods[r].size() && !found; i++) {
      if (start.rods[r][i].shapeId == firstShape) {
        rod = r;
        index = i;
        found = true;
      }
    }
  }
  REQUIRE(found);
  REQUIRE(StackingEdits(d).removeDisc(p, false, rod, index).has_value());
  for (const stacking::StackMap* map :
       {&d.puzzle().getProblem(0).startStacks(), &d.puzzle().getProblem(0).goalStacks()}) {
    for (const auto& r : map->rods) {
      for (const stacking::DiskRef& ref : r) {
        CHECK(ref.shapeId != firstShape);
      }
    }
  }
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().getProblem(0).startStacks().rods == start.rods);
  CHECK(d.puzzle().getProblem(0).goalStacks().rods == goal.rods);
}

// The Entities tab's rod sets (cb_NewRod, cb_CopyRod, cb_NameRod,
// cb_RodExchange, cb_DeleteRod), each undone and redone byte for byte.
TEST_CASE("stacking document: rod sets are added, copied, named, moved and deleted",
          "[app][stacking]") {
  Document d = openFixture("stacking/stack_two_boards.xmpuzzle");
  REQUIRE(d.rodSetIds().size() == 2);
  const Id hanoi = d.rodSetIds()[0];
  const Id panex = d.rodSetIds()[1];
  const Id p2 = d.problemIds()[1];
  CHECK(Document::rodSetLabel(0) == "R1");

  // new: the default rules, no name, at the end
  const Id added =
      test::roundTrip(d, [](Document& x) { return StackingEdits(x).addRodSet(); }).value();
  REQUIRE(d.rodSetIds().size() == 3);
  CHECK(d.rodSetIds()[2] == added);
  CHECK(d.puzzle().getRodSet(2) == stacking::RodSet{});

  // a copy: the rules and the name
  const Id copy = test::roundTrip(d, [panex](Document& x) {
                    return StackingEdits(x).duplicateRodSet(panex);
                  }).value();
  CHECK(d.puzzle().getRodSet(3) == d.puzzle().getRodSet(1));

  // a name; what the file cannot hold is refused; the same name changes nothing
  test::roundTrip(
      d, [copy](Document& x) { return StackingEdits(x).renameRodSet(copy, "Panex copy"); });
  CHECK(d.puzzle().getRodSet(3).name == "Panex copy");
  test::refused(d, Errc::InvalidArgument,
                [copy](Document& x) { return StackingEdits(x).renameRodSet(copy, "two\nlines"); });
  const std::size_t steps = d.history().undo.size();
  REQUIRE(StackingEdits(d).renameRodSet(copy, "Panex copy").has_value());
  CHECK(d.history().undo.size() == steps);

  // moved one place at a time: the problems keep their boards
  test::roundTrip(d, [panex](Document& x) { return StackingEdits(x).moveRodSet(panex, 0); });
  CHECK(d.rodSetIds()[0] == panex);
  CHECK(d.rodSetIds()[1] == hanoi);
  CHECK(d.puzzle().getProblem(0).getRodSetId() == 1);
  CHECK(d.puzzle().getProblem(1).getRodSetId() == 0);
  test::refused(d, Errc::InvalidArgument,
                [panex](Document& x) { return StackingEdits(x).moveRodSet(panex, 9); });

  // deleted: the problems on it stand on none, their stacks kept; the
  // others follow the renumbering
  const stacking::StackMap start = d.puzzle().getProblem(1).startStacks();
  test::roundTrip(d, [panex](Document& x) { return StackingEdits(x).deleteRodSet(panex); });
  CHECK_FALSE(d.puzzle().getProblem(1).rodSetValid(d.puzzle()));
  CHECK(d.puzzle().getProblem(1).startStacks() == start);
  CHECK(d.puzzle().getProblem(0).getRodSetId() == 0);
  REQUIRE(d.undo().has_value());
  REQUIRE(StackingEdits(d).deleteRodSet(panex).has_value());
  CHECK(d.lastChange().problems == std::vector<Id>{p2});
  CHECK((d.lastChange().what & kChangeRodSets) != 0);

  // at least one is kept
  REQUIRE(StackingEdits(d).deleteRodSet(copy).has_value());
  REQUIRE(StackingEdits(d).deleteRodSet(added).has_value());
  REQUIRE(d.rodSetIds() == std::vector<Id>{hanoi});
  test::refused(d, Errc::InvalidArgument,
                [hanoi](Document& x) { return StackingEdits(x).deleteRodSet(hanoi); });
  CHECK(StackingEdits(d).deleteRodSet(hanoi).error().message == "Keep at least one rod set");
  test::refused(d, Errc::NotFound,
                [panex](Document& x) { return StackingEdits(x).renameRodSet(panex, "x"); });
}

// A file may hold more rods or a taller rod than the inputs allow (the
// reader takes up to 1000): an edit of another rule keeps what the rod set
// has, where the clamp cut it to the bounds, and only a change to a value
// past them is refused (cleanup item 11.2; this path, checkpoint E).
TEST_CASE("stacking document: rules past the bounds, as a file gave them, stay through an edit",
          "[app][stacking]") {
  std::ifstream in("tests/data/puzzles/test_stacking_hanoi.xmpuzzle");
  REQUIRE(in.good());
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto put = [&text](const std::string& was, const std::string& now) {
    const std::size_t at = text.find(was);
    REQUIRE(at != std::string::npos);
    text.replace(at, was.size(), now);
  };
  put("rods=\"3\"", "rods=\"20\"");
  put("grow=\"1\" height=\"8\"", "grow=\"0\" height=\"80\"");
  Result<Document> opened = Document::openBytes(text);
  REQUIRE(opened.has_value());
  Document& d = *opened;
  const Id r = d.rodSetIds().front();
  REQUIRE(d.puzzle().getRodSet(0).rodCount == 20);
  REQUIRE(d.puzzle().getRodSet(0).definedHeight == 80);

  // another rule changes: the count and the height stay
  stacking::RodSet f = d.puzzle().getRodSet(0);
  f.sizeMatters = !f.sizeMatters;
  test::roundTrip(d, [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  CHECK(d.puzzle().getRodSet(0).rodCount == 20);
  CHECK(d.puzzle().getRodSet(0).definedHeight == 80);
  CHECK(d.puzzle().getRodSet(0).sizeMatters == f.sizeMatters);
  CHECK(d.puzzle().getProblem(0).startStacks().rods.size() == 20);

  // another value past the bounds is refused; one inside them is taken,
  // and then the old one is past them like any other
  f = d.puzzle().getRodSet(0);
  f.rodCount = 21;
  test::refused(d, Errc::InvalidArgument,
                [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  f.rodCount = 20;
  f.definedHeight = 81;
  test::refused(d, Errc::InvalidArgument,
                [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  f.definedHeight = 80;
  f.rodCount = stacking::kMaxRods;
  test::roundTrip(d, [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  CHECK(d.puzzle().getRodSet(0).rodCount == stacking::kMaxRods);
  CHECK(d.puzzle().getRodSet(0).definedHeight == 80);
  f.rodCount = 20;
  test::refused(d, Errc::InvalidArgument,
                [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
}

// cb_RodField: the fields as the GUI read them back, in its bounds (past
// them refused, as the engine refuses them: cleanup item 11.2).
TEST_CASE("stacking document: a rod set's rules are bounded and follow Panex columns",
          "[app][stacking]") {
  Document d = openFixture("test_stacking_hanoi.xmpuzzle");
  const Id r = d.rodSetIds().front();
  const stacking::RodSet before = d.puzzle().getRodSet(0);
  for (const auto& [rods, height, pocket] :
       {std::tuple{stacking::kMaxRods + 1, 1U, 1U}, std::tuple{0U, 1U, 1U},
        std::tuple{3U, stacking::kMaxHeight + 1, 1U}, std::tuple{3U, 0U, 1U},
        std::tuple{3U, 1U, stacking::kMaxHeight + 1}, std::tuple{3U, 1U, 0U}}) {
    stacking::RodSet out = before;
    out.rodCount = rods;
    out.definedHeight = height;
    out.pocketHeight = pocket;
    const Result<void> refused = StackingEdits(d).setRodSetRules(r, out);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == Errc::InvalidArgument);
    CHECK(d.puzzle().getRodSet(0) == before);
  }
  stacking::RodSet f = before;
  f.name = "not this";
  f.rodCount = stacking::kMaxRods;
  f.growHeight = false;
  f.definedHeight = 1;
  f.pocketColumn = true;
  f.pocketHeight = stacking::kMaxHeight;
  f.outsideChannel = true;
  f.canMoveOver = true;
  test::roundTrip(d, [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  const stacking::RodSet& got = d.puzzle().getRodSet(0);
  CHECK(got.name == "Hanoi");
  CHECK(got.rodCount == 16);
  CHECK_FALSE(got.growHeight);
  CHECK(got.definedHeight == 1);
  // the pocket and the channel need Panex columns
  CHECK_FALSE(got.pocketColumn);
  CHECK_FALSE(got.outsideChannel);
  CHECK(got.pocketHeight == 64);
  CHECK_FALSE(got.canMoveOver);
  // the problem's stacks have the board's rods
  CHECK(d.puzzle().getProblem(0).startStacks().rods.size() == 16);
  CHECK(d.puzzle().getProblem(0).goalStacks().rods.size() == 16);

  // Panex columns: the pocket; the channel with three rods or more
  f = got;
  f.panexColumns = true;
  f.pocketColumn = true;
  f.outsideChannel = true;
  f.rodCount = 2;
  test::roundTrip(d, [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  CHECK(d.puzzle().getRodSet(0).pocketColumn);
  CHECK_FALSE(d.puzzle().getRodSet(0).outsideChannel);
  f.rodCount = 3;
  test::roundTrip(d, [r, &f](Document& x) { return StackingEdits(x).setRodSetRules(r, f); });
  CHECK(d.puzzle().getRodSet(0).outsideChannel);
  // the same rules again change nothing
  const std::size_t steps = d.history().undo.size();
  REQUIRE(StackingEdits(d).setRodSetRules(r, d.puzzle().getRodSet(0)).has_value());
  CHECK(d.history().undo.size() == steps);
}

// The disc list: a new shape is a disc, its size is set there, and what
// the GUI hid (the cell editor, the tool tabs) is refused.
TEST_CASE("stacking document: discs are made and sized, not edited by cells",
          "[app][stacking]") {
  Document d(GridKind::Stacking);
  REQUIRE(d.rodSetIds().size() == 1);
  const Id s = test::roundTrip(d, [](Document& x) { return x.createShape(); }).value();
  const Voxel& v = d.puzzle().getShape(0);
  CHECK(v.getDiskSize() == 1);
  CHECK(v.getX() == 3);
  CHECK(v.getY() == 3);
  CHECK(v.getZ() == 1);
  test::refused(d, Errc::InvalidArgument, [](Document& x) {
    return x.createShape(std::array<unsigned int, 3>{3, 3, 1});
  });

  test::roundTrip(d, [s](Document& x) { return StackingEdits(x).setDiscSize(s, 4); });
  const Voxel& w = d.puzzle().getShape(0);
  CHECK(w.getDiskSize() == 4);
  CHECK(w.getX() == 9);
  CHECK(w.countState(Voxel::kFilled) == 80);
  CHECK(w.getHx() == 4);
  CHECK(w.getHy() == 4);
  for (const unsigned int bad : {0U, 101U}) {
    test::refused(d, Errc::InvalidArgument,
                  [s, bad](Document& x) { return StackingEdits(x).setDiscSize(s, bad); });
  }
  const std::size_t steps = d.history().undo.size();
  REQUIRE(StackingEdits(d).setDiscSize(s, 4).has_value());
  CHECK(d.history().undo.size() == steps);
  test::roundTrip(d, [s](Document& x) { return StackingEdits(x).setDiscSize(s, 100); });

  // the disc's color is the shape's own color
  test::roundTrip(d, [s](Document& x) { return x.setShapeColor(s, 0x336699U); });

  // the cell editor and the tool tabs were hidden
  test::refused(d, Errc::InvalidArgument, [s](Document& x) {
    return x.editCells(s, CellTool::Erase, 0, 0, {CellEdit{0, 0, 0}});
  });
  test::refused(d, Errc::InvalidArgument, [s](Document& x) {
    return x.editBox(s, CellTool::Erase, 0, 0, CellBox{0, 0, 0, 1, 1, 0});
  });
  test::refused(d, Errc::InvalidArgument,
                [s](Document& x) { return x.resizeShape(s, 5, 5, 1, false); });
  test::refused(d, Errc::InvalidArgument,
                [s](Document& x) { return x.shapeTask(s, ShapeTask::Rotate6); });

  // the stacking commands are a stacking puzzle's
  Document bricks(GridKind::Bricks);
  const Id b = bricks.createShape().value();
  test::refused(bricks, Errc::Unsupported,
                [](Document& x) { return StackingEdits(x).addRodSet(); });
  test::refused(bricks, Errc::Unsupported,
                [b](Document& x) { return StackingEdits(x).setDiscSize(b, 2); });
}

// The Problems tab on a stacking puzzle (its buttons' stacking branches):
// Hanoi built from a new document equals the fixture, then discs are
// added, taken off, moved and cleared, each undone and redone byte for
// byte.
TEST_CASE("stacking document: Hanoi built through the commands, its stacks edited",
          "[app][stacking]") {
  const ScratchDir work("document_stacking");
  Document d(GridKind::Stacking);
  const Id board = d.rodSetIds().front();
  std::vector<Id> discs;
  for (const unsigned int size : {3U, 2U, 1U}) {
    discs.push_back(d.createShape().value());
    REQUIRE(StackingEdits(d).setDiscSize(discs.back(), size).has_value());
  }
  const Id p = d.createProblem().value();
  REQUIRE(d.renameProblem(p, "Tower of Hanoi").has_value());
  REQUIRE(StackingEdits(d).renameRodSet(board, "Hanoi").has_value());

  // a disc needs a board first
  test::refused(d, Errc::InvalidArgument, [p, &discs](Document& x) {
    return StackingEdits(x).placeDisc(p, discs[0], 0, false);
  });
  CHECK(StackingEdits(d).placeDisc(p, discs[0], 0, false).error().message ==
        "Choose a rod set with Set Start/Goal Rods");
  test::roundTrip(d,
                  [p, board](Document& x) { return StackingEdits(x).setProblemRodSet(p, board); });
  CHECK(d.puzzle().getProblem(0).startStacks().rods.size() == 3);

  // all of them, largest first; their counts follow
  const DiscsPlaced start = test::roundTrip(d, [p](Document& x) {
                              return StackingEdits(x).placeAllDiscs(p, 0, false);
                            }).value();
  CHECK(start.placed == 3);
  CHECK(start.error.empty());
  test::refused(d, Errc::InvalidArgument, [p, &discs](Document& x) {
    return StackingEdits(x).placeDisc(p, discs[0], 3, true);
  });
  // on the goal one at a time (the copies on the start are not used there)
  for (const Id disc : discs) {
    test::roundTrip(
        d, [p, disc](Document& x) { return StackingEdits(x).placeDisc(p, disc, 2, true); });
  }
  const Problem& pr = d.puzzle().getProblem(0);
  CHECK(pr.getShapeMaximum(0) == 1);
  CHECK(stacking::setupError(d.puzzle(), pr).empty());
  CHECK(test::withoutUids(io::writePuzzle(d.puzzle())) ==
        test::withoutUids(io::writePuzzle(test::loadFixture("test_stacking_hanoi.xmpuzzle"))));

  // and it solves
  REQUIRE(d.solveStart(p, findSolutions(work.path())).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 1);
  CHECK(stacking::logicalMoves(*d.puzzle().getProblem(0).getSavedSolution(0).getDisassembly()) ==
        7);

  // another copy of the smallest: every copy is on the start, so one more,
  // the minimum with it
  test::roundTrip(
      d, [p, &discs](Document& x) { return StackingEdits(x).placeDisc(p, discs[2], 1, false); });
  CHECK(pr.getShapeMaximum(2) == 2);
  CHECK(pr.getShapeMinimum(2) == 2);
  CHECK_FALSE(stacking::setupError(d.puzzle(), pr).empty());

  // taken off (from the bottom of rod 2): from both stackings, one fewer
  test::roundTrip(d, [p](Document& x) { return StackingEdits(x).removeDisc(p, false, 1, 0); });
  CHECK(pr.getShapeMaximum(2) == 1);
  CHECK(stacking::setupError(d.puzzle(), pr).empty());
  test::refused(d, Errc::InvalidArgument,
                [p](Document& x) { return StackingEdits(x).removeDisc(p, false, 1, 0); });
  CHECK(StackingEdits(d).removeDisc(p, false, 1, 0).error().message == "That rod is empty");
  test::refused(d, Errc::InvalidArgument,
                [p](Document& x) { return StackingEdits(x).removeDisc(p, false, 0, 3); });

  // moved on its rod; the top cannot go higher
  const unsigned int at = test::roundTrip(d, [p](Document& x) {
                            return StackingEdits(x).moveDisc(p, false, 0, 0, 1);
                          }).value();
  CHECK(at == 1);
  CHECK(pr.startStacks().rods[0][1].shapeId == 0);
  test::refused(d, Errc::InvalidArgument,
                [p](Document& x) { return StackingEdits(x).moveDisc(p, false, 0, 2, 1); });
  test::refused(d, Errc::InvalidArgument,
                [p](Document& x) { return StackingEdits(x).moveDisc(p, false, 0, 0, 2); });

  // the goal emptied, the start and the counts kept
  test::roundTrip(d, [p](Document& x) { return StackingEdits(x).clearStacks(p, true); });
  CHECK(pr.goalStacks().rods[2].empty());
  CHECK(pr.startStacks().rods[0].size() == 3);
  CHECK(pr.getNumberOfPieces() == 3);
  const std::size_t steps = d.history().undo.size();
  REQUIRE(StackingEdits(d).clearStacks(p, true).has_value());
  CHECK(d.history().undo.size() == steps);
}

// A position saved as a problem's start or goal (the Play workspace's Save
// position as Start and as Goal): one undo step that takes the solutions
// with it, nothing at all when that side is the position already, and
// refused with the reason when it is no position of the discs.
TEST_CASE("stacking document: a position becomes the start or the goal", "[app][stacking]") {
  const ScratchDir work("document_stacking");
  Document d = openFixture("test_stacking_hanoi.xmpuzzle");
  const Id p = d.problemIds()[0];
  REQUIRE(d.solveStart(p, findSolutions(work.path())).has_value());
  REQUIRE(d.solveWait(p).has_value());
  const Problem& pr = d.puzzle().getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  using Stacks = std::vector<std::vector<unsigned int>>;

  // the start it has: nothing changes, nothing is lost
  const std::size_t steps = d.history().undo.size();
  const std::uint64_t rev = d.revision();
  CHECK_FALSE(StackingEdits(d).setStacks(p, false, Stacks{{0, 1, 2}, {}, {}}).value());
  CHECK(d.history().undo.size() == steps);
  CHECK(d.revision() == rev);
  CHECK(pr.getNumberOfSavedSolutions() == 1);
  CHECK_FALSE(StackingEdits(d).setStacks(p, true, Stacks{{}, {}, {0, 1, 2}}).value());

  // a position two moves on: the start, in one step, the solution gone
  CHECK(test::roundTrip(d, [p](Document& x) {
          return StackingEdits(x).setStacks(p, false, Stacks{{0}, {1}, {2}});
        }).value());
  CHECK(d.history().undo.size() == steps + 1);
  CHECK(d.history().undo.back() == "Set start position");
  CHECK(pr.getNumberOfSavedSolutions() == 0);
  REQUIRE(pr.startStacks().rods.size() == 3);
  CHECK(pr.startStacks().rods[1] == std::vector<stacking::DiskRef>{stacking::DiskRef{1, 0}});
  CHECK(pr.startStacks().rods[2] == std::vector<stacking::DiskRef>{stacking::DiskRef{2, 0}});
  CHECK(stacking::setupError(d.puzzle(), pr).empty());
  // undone: the old start and its solution are back
  REQUIRE(d.undo().has_value());
  CHECK(pr.getNumberOfSavedSolutions() == 1);
  CHECK(pr.startStacks().rods[0].size() == 3);

  // the goal: the same command with the other side
  CHECK(test::roundTrip(d, [p](Document& x) {
          return StackingEdits(x).setStacks(p, true, Stacks{{}, {0, 1, 2}, {}});
        }).value());
  CHECK(d.history().undo.back() == "Set goal position");
  CHECK(pr.goalStacks().rods[1].size() == 3);
  CHECK(pr.goalStacks().rods[2].empty());
  CHECK(pr.startStacks().rods[0].size() == 3);

  // no position of the discs: refused with the reason, no step left
  const auto why = [&d, p](const Stacks& stacks) {
    test::refused(d, Errc::InvalidArgument, [p, &stacks](Document& x) {
      return StackingEdits(x).setStacks(p, false, stacks);
    });
    return StackingEdits(d).setStacks(p, false, stacks).error().message;
  };
  CHECK(why(Stacks{{0, 1}, {}, {}}) == "S3 is not on the position.");
  CHECK(why(Stacks{{0, 1, 2}, {}}) == "The position does not match the number of rods.");
  CHECK(why(Stacks{{1, 0, 2}, {}, {}}).find("sits on the smaller") != std::string::npos);
  CHECK(why(Stacks{{0, 1, 2}, {7}, {}}) == "The position names a disc that is not in the problem.");
  test::refused(d, Errc::NotFound, [](Document& x) {
    return StackingEdits(x).setStacks(999, false, Stacks{{0, 1, 2}, {}, {}});
  });

  // only a stacking puzzle has stacks
  Document cubes(GridKind::Bricks);
  const Id q = cubes.createProblem().value();
  test::refused(cubes, Errc::Unsupported,
                [q](Document& x) { return StackingEdits(x).setStacks(q, false, Stacks{}); });
}

// Which edits drop a problem's solutions follows each callback: new rules
// (cb_RodField) and an emptied stacking (changeProblem) do; a disc's size,
// a disc moved and a board chosen do not.
TEST_CASE("stacking document: what drops the solutions follows the GUI", "[app][stacking]") {
  const ScratchDir work("document_stacking");
  Document d = openFixture("stacking/stack_two_boards.xmpuzzle");
  const Id hanoi = d.rodSetIds()[0];
  const Id p1 = d.problemIds()[0];
  const Id p2 = d.problemIds()[1];
  for (const Id p : {p1, p2}) {
    REQUIRE(d.solveStart(p, findSolutions(work.path())).has_value());
    REQUIRE(d.solveWait(p).has_value());
  }
  const auto saved = [&d](unsigned int problem) {
    return d.puzzle().getProblem(problem).getNumberOfSavedSolutions();
  };
  REQUIRE(saved(0) == 1);
  REQUIRE(saved(1) == 1);

  REQUIRE(StackingEdits(d).setDiscSize(d.shapeIds()[2], 2).has_value());
  CHECK(saved(0) == 1);
  REQUIRE(StackingEdits(d).moveDisc(p1, false, 0, 0, 1).has_value());
  CHECK(saved(0) == 1);
  REQUIRE(StackingEdits(d).setProblemRodSet(p2, hanoi).has_value());
  CHECK(saved(1) == 1);
  REQUIRE(d.undo().has_value());
  REQUIRE(d.undo().has_value());
  REQUIRE(d.undo().has_value());

  // new rules: the problems on the rod set lose theirs, the others keep them
  stacking::RodSet four = d.puzzle().getRodSet(0);
  four.rodCount = 4;
  REQUIRE(StackingEdits(d).setRodSetRules(hanoi, four).has_value());
  CHECK(saved(0) == 0);
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
  CHECK(saved(1) == 1);
  CHECK(d.lastChange().problems == std::vector<Id>{p1});
  REQUIRE(d.undo().has_value());
  CHECK(saved(0) == 1);

  REQUIRE(StackingEdits(d).clearStacks(p1, false).has_value());
  CHECK(saved(0) == 0);
}

// What the GUI's Problems tab did with its brick buttons on a stacking
// puzzle was the stacking commands: the brick commands are refused.
TEST_CASE("stacking document: the brick commands of the Problems tab are refused",
          "[app][stacking]") {
  Document d = openFixture("test_stacking_hanoi.xmpuzzle");
  const Id p = d.problemIds().front();
  const Id s = d.shapeIds().front();
  using test::refused;
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.setResult(p, s); });
  refused(d, Errc::InvalidArgument, [p](Document& x) { return x.clearResult(p); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.setPieceCount(p, s, 2, 2); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.addPiece(p, s); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.removePiece(p, s); });
  refused(d, Errc::InvalidArgument, [p](Document& x) { return x.setRangeAll(p, 0, 2); });
  refused(d, Errc::InvalidArgument, [p](Document& x) { return x.addOneOfEach(p); });
  refused(d, Errc::InvalidArgument, [p](Document& x) { return x.removeAllPieces(p); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.movePart(p, s, 1); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.setGroupCount(p, s, 1, 1); });
  refused(d, Errc::InvalidArgument, [p, s](Document& x) { return x.setGroup(p, s, 1); });
  refused(d, Errc::InvalidArgument, [p](Document& x) { return x.deleteGroup(p, 1); });
  CHECK(d.setPieceCount(p, s, 2, 2).error().message ==
        "a stacking problem's discs follow its stacks: place them on its rods");
}

// The Entities tab's view of a rod set alone (showRodSet): rods that grow
// stand 5 high, spaced for the largest disc, the pocket left of rod 1.
TEST_CASE("stacking document: a rod set is laid out on its own", "[app][stacking]") {
  Document d = openFixture("test_stacking_hanoi.xmpuzzle");
  const stacking::BoardLayout hanoi = rodSetLayout(d.puzzle(), 0);
  CHECK(hanoi.spacing == 7);
  CHECK(hanoi.rodCount == 3);
  CHECK(hanoi.rodHeight == 5);
  CHECK(hanoi.rodX == std::vector<int>{0, 7, 14});
  CHECK(hanoi.rodHeights == std::vector<unsigned int>{5, 5, 5});
  CHECK_FALSE(hanoi.panex);
  CHECK_FALSE(hanoi.pocket);
  CHECK(hanoi.maxSize == 3);
  CHECK(hanoi.disks.empty());

  const Id r = d.rodSetIds().front();
  stacking::RodSet f = d.puzzle().getRodSet(0);
  f.growHeight = false;
  f.definedHeight = 4;
  f.panexColumns = true;
  f.pocketColumn = true;
  f.pocketHeight = 2;
  REQUIRE(StackingEdits(d).setRodSetRules(r, f).has_value());
  const stacking::BoardLayout panex = rodSetLayout(d.puzzle(), 0);
  CHECK(panex.rodCount == 4);
  CHECK(panex.rodHeight == 4);
  CHECK(panex.panex);
  CHECK(panex.pocket);
  // the pocket is the last rod and stands left of rod 1
  CHECK(panex.rodX == std::vector<int>{7, 14, 21, 0});
  CHECK(panex.rodHeights == std::vector<unsigned int>{4, 4, 4, 2});
}

// syncMaps drops the empty rods past the board's count as well as adding
// the missing ones: a rod emptied after the count was lowered is not on
// the board, and a disc for it is refused, not placed and lost.
TEST_CASE("stacking document: a rod past the board's count is not on it", "[app][stacking]") {
  Document d = openFixture("test_stacking_hanoi.xmpuzzle");
  const Id p = d.problemIds().front();
  const Id r = d.rodSetIds().front();
  const Id smallest = d.shapeIds()[2];
  stacking::RodSet f = d.puzzle().getRodSet(0);
  f.rodCount = 5;
  REQUIRE(StackingEdits(d).setRodSetRules(r, f).has_value());
  // the smallest disc onto rod 4, then the board back to three rods: rod 4
  // keeps its disc (the stacks keep what holds discs)
  REQUIRE(StackingEdits(d).removeDisc(p, false, 0, 2).has_value());
  REQUIRE(StackingEdits(d).placeDisc(p, smallest, 3, false).has_value());
  f.rodCount = 3;
  REQUIRE(StackingEdits(d).setRodSetRules(r, f).has_value());
  REQUIRE(d.puzzle().getProblem(0).startStacks().rods.size() == 4);
  // emptied, rod 4 is past the count: no rod of the board
  REQUIRE(StackingEdits(d).removeDisc(p, false, 3, 0).has_value());
  REQUIRE(d.puzzle().getProblem(0).startStacks().rods.size() == 4);
  test::refused(d, Errc::InvalidArgument, [p, smallest](Document& x) {
    return StackingEdits(x).placeDisc(p, smallest, 3, false);
  });
  test::refused(d, Errc::InvalidArgument,
                [p](Document& x) { return StackingEdits(x).placeAllDiscs(p, 3, false); });
  // and a board chosen again fits the stacks to it (the GUI's syncMaps)
  test::roundTrip(d, [p, r](Document& x) { return StackingEdits(x).setProblemRodSet(p, r); });
  CHECK(d.puzzle().getProblem(0).startStacks().rods.size() == 3);
}

// A copy of a stacking problem has its rules and stacks. Before problems
// had uids (decision 0044) it had the first one's saved search too, as in
// the original: the end of the copy's run removed the first problem's
// paused search, and the cache manager named the copy's problem as the
// paused one. Each problem's search is now its own.
TEST_CASE("stacking document: identical problems have their own saved searches",
          "[app][stacking][parallel]") {
  const ScratchDir work("document_stacking");
  Document d = swapDocument(6, work.path());
  const Id p1 = d.problemIds().front();
  const Result<Id> copy = d.duplicateProblem(p1);
  REQUIRE(copy.has_value());
  const Id p2 = *copy;
  CHECK(d.puzzle().getProblem(1).getUid() != d.puzzle().getProblem(0).getUid());
  SolveOptions o = findSolutions(work.path() / "panex");
  o.threads = 1;
  const std::string& folder = o.stacking.workDir;

  REQUIRE(d.solveStart(p1, o).has_value());
  // while the first runs, the copy starts a search of its own
  REQUIRE(d.solveStart(p2, o).has_value());
  for (const Id p : {p1, p2}) {
    REQUIRE(d.solvePause(p).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.solveState(p) == SolveState::Paused);
    REQUIRE(d.savedSearch(p, folder).value().has_value());
  }
  const std::vector<std::string> f1 = d.searchFolders(p1, "", folder).value();
  const std::vector<std::string> f2 = d.searchFolders(p2, "", folder).value();
  REQUIRE(f1.size() == 1);
  REQUIRE(f2.size() == 1);
  CHECK(f1 != f2);

  // an abort of one throws away its own search only, and the other carries on
  REQUIRE(d.solveAbort(p1, folder).has_value());
  CHECK_FALSE(d.savedSearch(p1, folder).value().has_value());
  CHECK(d.savedSearch(p2, folder).value().has_value());
  REQUIRE(d.solveResume(p2, o).has_value());
  REQUIRE(d.solvePause(p2).has_value());
  REQUIRE(d.solveWait(p2).has_value());
  CHECK(d.savedSearch(p2, folder).value().has_value());
}

// Problems paused by a version before uids (decision 0044) are given none
// when the file opens (io's giveUids: the first of each definition), so
// that each finds the search it saved, which was named without one. Two
// such problems that differ only where the folder's name does not look
// (here their stored Solver settings) name one folder, as every copy did
// in the original: the one case left where two problems share a saved
// search, so one run at a time works on it (Document::sharedSearchActive).
TEST_CASE("stacking document: problems paused before uids share their saved search",
          "[app][stacking][parallel]") {
  const ScratchDir work("document_stacking");
  Puzzle puz = swapPuzzle(6);
  Problem& copy = puz.getProblem(puz.addProblem(puz.getProblem(0)));
  copy.setName("twin");
  copy.setSolverOptions({{"highMemory", "1"}});
  // as a version before uids saved them: paused, no uid
  const std::string noUids =
      std::regex_replace(io::writePuzzle(puz), std::regex(R"( uid="[0-9a-f]{16}")"), "");
  const std::string xml = std::regex_replace(noUids, std::regex(R"( state="0")"), R"( state="1")");
  Result<Document> opened = Document::openBytes(xml);
  REQUIRE(opened.has_value());
  Document& d = *opened;
  const Id p1 = d.problemIds()[0];
  const Id p2 = d.problemIds()[1];
  SolveOptions o = findSolutions(work.path() / "panex");
  o.threads = 1;
  const std::string& folder = o.stacking.workDir;
  const Problem& first = d.puzzle().getProblem(0);
  const Problem& twin = d.puzzle().getProblem(1);
  REQUIRE(first.getUid() == 0);
  REQUIRE(twin.getUid() == 0);
  REQUIRE(panex::searchFolder(d.puzzle(), first, folder) ==
          panex::searchFolder(d.puzzle(), twin, folder));

  REQUIRE(d.solveStart(p1, o).has_value());
  // while the first runs on the folder, the twin is refused and told of no
  // saved search: the run writes it
  const Result<void> second = d.solveStart(p2, o);
  REQUIRE_FALSE(second.has_value());
  CHECK(second.error().code == Errc::InvalidState);
  const Result<void> abort = d.solveAbort(p2, folder);
  REQUIRE_FALSE(abort.has_value());
  CHECK(abort.error().code == Errc::InvalidState);
  CHECK_FALSE(d.savedSearch(p2, folder).value().has_value());

  REQUIRE(d.solvePause(p1).has_value());
  REQUIRE(d.solveWait(p1).has_value());
  REQUIRE(d.solveState(p1) == SolveState::Paused);
  // paused, the search is the twin's too
  CHECK(d.savedSearch(p2, folder).value().has_value());
  CHECK(d.searchFolders(p2, "", folder).value() == d.searchFolders(p1, "", folder).value());
}
