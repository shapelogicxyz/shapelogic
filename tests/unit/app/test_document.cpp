// SPDX-License-Identifier: GPL-3.0-or-later
// The document: every command with its undo / redo round trip (byte for
// byte through writePuzzle, the ids too), what each does (the original
// GUI's callbacks), ids that stay through reordering, groups, the undo
// depth, the dirty flag and the revision. New (the original had no tests
// for its GUI); the solving guards are in test_document_solving.cpp.
#include "../grid/grid_test_helpers.h"
#include "app_test_helpers.h"

#include "bt/app/document.h"
#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

using namespace bt;
using namespace bt::app;
using test::openFixture;
using test::refused;
using test::roundTrip;

namespace {

Id firstShape(const Document& d) { return d.shapeIds().front(); }
Id firstProblem(const Document& d) { return d.problemIds().front(); }

const Problem& problem0(const Document& d) { return d.puzzle().getProblem(0); }

}  // namespace

TEST_CASE("document: new, open, save, and the ids of what it holds", "[app][document]") {
  Document fresh(GridKind::Bricks);
  CHECK(fresh.puzzle().getNumberOfShapes() == 0);
  CHECK_FALSE(fresh.dirty());
  CHECK(fresh.revision() == 0);

  Document d = openFixture("PelikanBurr.xmpuzzle");
  CHECK(d.shapeIds().size() == d.puzzle().getNumberOfShapes());
  CHECK(d.problemIds().size() == d.puzzle().getNumberOfProblems());
  CHECK_FALSE(d.dirty());
  // ids are unique
  std::vector<Id> all = d.shapeIds();
  all.insert(all.end(), d.colourIds().begin(), d.colourIds().end());
  all.insert(all.end(), d.problemIds().begin(), d.problemIds().end());
  std::sort(all.begin(), all.end());
  CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
  CHECK(std::find(all.begin(), all.end(), Id{0}) == all.end());

  CHECK(d.shapeIndex(d.shapeIds()[2]).value() == 2);
  CHECK(d.shapeIndex(12345).error().code == Errc::NotFound);
  CHECK(Document::shapeLabel(0) == "S1");
  CHECK(Document::problemLabel(1) == "P2");
  CHECK(Document::colourLabel(2) == "C3");

  // solutions: ids from the file, unique, found back
  const Problem& pr = problem0(d);
  REQUIRE(pr.getNumberOfSavedSolutions() > 0);
  const Id sol = pr.getSavedSolution(0).getId();
  CHECK(sol != 0);
  CHECK(d.solutionIndex(firstProblem(d), sol).value() == 0);

  // a save, and the bytes read back give the same file
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "bt_document_test.xmpuzzle";
  REQUIRE(d.renameShape(firstShape(d), "renamed").has_value());
  CHECK(d.dirty());
  REQUIRE(d.save(tmp).has_value());
  CHECK_FALSE(d.dirty());
  Result<Document> back = Document::open(tmp);
  REQUIRE(back.has_value());
  CHECK(io::writePuzzle(back->puzzle()) == io::writePuzzle(d.puzzle()));
  Result<std::string> bytes = d.saveBytes();
  REQUIRE(bytes.has_value());
  Result<Document> again = Document::openBytes(*bytes);
  REQUIRE(again.has_value());
  CHECK(io::writePuzzle(again->puzzle()) == *bytes);
  // a rescue copy reopened: dirty until saved, and no undo step
  CHECK_FALSE(again->dirty());
  again->markUnsaved();
  CHECK(again->dirty());
  CHECK_FALSE(again->history().undo.size() > 0);
  REQUIRE(again->save(tmp).has_value());
  CHECK_FALSE(again->dirty());
  std::filesystem::remove(tmp);
}

TEST_CASE("document: notes", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  roundTrip(d, [](Document& x) { return x.setNotes("a note"); });
  CHECK(d.puzzle().getComment() == "a note");
  CHECK((d.lastChange().what & kChangeNotes) != 0);
  // the same text again changes nothing
  const std::uint64_t rev = d.revision();
  REQUIRE(d.setNotes("a note").has_value());
  CHECK(d.revision() == rev);
}

TEST_CASE("document: shapes are made, copied, deleted, moved, named, weighed and coloured",
          "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const unsigned int n = d.puzzle().getNumberOfShapes();

  SECTION("create, of the grid's default size or a given one") {
    const Id a = roundTrip(d, [](Document& x) { return x.createShape(); }).value();
    CHECK(d.puzzle().getNumberOfShapes() == n + 1);
    CHECK(d.shapeIndex(a).value() == n);
    CHECK(d.puzzle().getShape(n).getX() == 4);
    const Id b = roundTrip(d, [](Document& x) {
                   return x.createShape(std::array<unsigned int, 3>{2, 3, 5});
                 }).value();
    CHECK(d.puzzle().getShape(n + 1).getZ() == 5);
    CHECK(a != b);
    refused(d, Errc::InvalidArgument,
            [](Document& x) { return x.createShape(std::array<unsigned int, 3>{0, 1, 1}); });
  }
  SECTION("duplicate: a copy at the end, name and all") {
    const Id s = d.shapeIds()[3];
    const Id c = roundTrip(d, [s](Document& x) { return x.duplicateShape(s); }).value();
    CHECK(d.puzzle().getShape(d.shapeIndex(c).value()) == d.puzzle().getShape(3));
    refused(d, Errc::NotFound, [](Document& x) { return x.duplicateShape(999999); });
  }
  SECTION("delete: the problems drop it") {
    const Id s = d.shapeIds()[2];
    const Id next = d.shapeIds()[3];
    roundTrip(d, [s](Document& x) { return x.deleteShape(s); });
    CHECK(d.puzzle().getNumberOfShapes() == n - 1);
    CHECK(d.shapeIndex(s).error().code == Errc::NotFound);
    CHECK(d.shapeIndex(next).value() == 2);
  }
  SECTION("move: the ids follow, the labels renumber, the problems follow") {
    const std::vector<Id> ids = d.shapeIds();
    const unsigned int result = problem0(d).getResultId();
    roundTrip(d, [&ids](Document& x) { return x.moveShape(ids[0], 3); });
    CHECK(d.shapeIds()[3] == ids[0]);
    CHECK(d.shapeIds()[0] == ids[1]);
    CHECK(d.shapeIndex(ids[0]).value() == 3);
    // the result followed its shape
    CHECK(d.shapeIds()[problem0(d).getResultId()] == ids[result]);
    refused(d, Errc::InvalidArgument, [&ids](Document& x) { return x.moveShape(ids[1], 99); });
  }
  SECTION("rename, weight, colour") {
    const Id s = firstShape(d);
    roundTrip(d, [s](Document& x) { return x.renameShape(s, "cage"); });
    CHECK(d.puzzle().getShape(0).getName() == "cage");
    roundTrip(d, [s](Document& x) { return x.setShapeWeight(s, 5); });
    CHECK(d.puzzle().getShape(0).getWeight() == 5);
    roundTrip(d, [s](Document& x) { return x.setShapeColour(s, 0x336699U); });
    CHECK(d.puzzle().getShape(0).getShapeColor() == 0x336699);
    roundTrip(d, [s](Document& x) { return x.setShapeColour(s, std::nullopt); });
    CHECK(d.puzzle().getShape(0).getShapeColor() == -1);
    refused(d, Errc::InvalidArgument, [s](Document& x) { return x.setShapeColour(s, 0x1000000U); });
  }
}

TEST_CASE("document: cells are edited with the tools, mirror and span", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape(std::array<unsigned int, 3>{4, 4, 4}).value();
  const Id c = d.addColour(Rgb{200, 10, 10}).value();
  const auto& shape = [&d]() -> const Voxel& { return d.puzzle().getShape(0); };

  CHECK(roundTrip(d, [s](Document& x) {
          return x.editCells(s, CellTool::Fixed, 0, 0, {{1, 1, 1}, {2, 1, 1}});
        }).value() == 2);
  CHECK(shape().isFilled(1, 1, 1));
  CHECK(roundTrip(d, [s](Document& x) {
          return x.editCells(s, CellTool::Variable, 0, 0, {{2, 1, 1}});
        }).value() == 1);
  CHECK(shape().isVariable(2, 1, 1));
  // paint: colour only, on cells that are not empty
  CHECK(roundTrip(d, [s, c](Document& x) {
          return x.editCells(s, CellTool::Paint, c, 0, {{1, 1, 1}, {0, 0, 0}});
        }).value() == 1);
  CHECK(shape().getColor(1, 1, 1) == 1);
  CHECK(shape().isEmpty(0, 0, 0));
  // erase: the cell and its colour go
  CHECK(roundTrip(d, [s](Document& x) {
          return x.editCells(s, CellTool::Erase, 0, 0, {{1, 1, 1}});
        }).value() == 1);
  CHECK(shape().isEmpty(1, 1, 1));
  CHECK(shape().getColor(1, 1, 1) == 0);
  // nothing changed: no step
  const std::size_t steps = d.history().undo.size();
  CHECK(d.editCells(s, CellTool::Erase, 0, 0, {{0, 0, 0}, {9, 9, 9}}).value() == 0);
  CHECK(d.history().undo.size() == steps);

  // mirror in x: the cell and its mirror
  CHECK(roundTrip(d, [s](Document& x) {
          return x.editCells(s, CellTool::Fixed, 0, kMirrorX, {{0, 3, 0}});
        }).value() == 2);
  CHECK(shape().isFilled(0, 3, 0));
  CHECK(shape().isFilled(3, 3, 0));
  // span in z and y: the rows through the cell
  CHECK(roundTrip(d, [s](Document& x) {
          return x.editCells(s, CellTool::Fixed, 0, kSpanZ | kSpanY, {{2, 2, 2}});
        }).value() == 7);
  for (unsigned int i = 0; i < 4; i++) {
    CHECK(shape().isFilled(2, 2, i));
    CHECK(shape().isFilled(2, i, 2));
  }
  // a box (the selection fill), clamped to the space
  CHECK(roundTrip(d, [s](Document& x) {
          return x.editBox(s, CellTool::Variable, 0, 0, CellBox{3, 3, 3, 9, 9, 9});
        }).value() == 1);
  CHECK(shape().isVariable(3, 3, 3));
  refused(d, Errc::NotFound,
          [s](Document& x) { return x.editCells(s, CellTool::Paint, 4242, 0, {{1, 1, 1}}); });
}

TEST_CASE("document: an edited shape takes the solutions of its problems with it",
          "[app][document]") {
  // the original's changeShape: every problem using the shape loses them
  Document d = openFixture("PelikanBurr.xmpuzzle");
  REQUIRE(problem0(d).getNumberOfSavedSolutions() > 0);
  const unsigned int piece = problem0(d).getShapeIdOfPart(0);
  const Voxel& v = d.puzzle().getShape(piece);
  // a cell that is filled, made variable
  CellEdit cell;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    if (v.isFilled(i)) {
      unsigned int x = 0;
      unsigned int y = 0;
      unsigned int z = 0;
      REQUIRE(v.indexToXYZ(i, &x, &y, &z));
      cell = {static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
      break;
    }
  }
  const Id s = d.shapeIds()[piece];
  roundTrip(d, [s, cell](Document& x) { return x.editCells(s, CellTool::Variable, 0, 0, {cell}); });
  CHECK(problem0(d).getNumberOfSavedSolutions() == 0);
  CHECK(problem0(d).getSolveState() == ProblemState::Unsolved);
  CHECK((d.lastChange().what & kChangeSolutions) != 0);
  // and undo gave them back (checked byte for byte by the round trip)
  REQUIRE(d.undo().has_value());
  CHECK(problem0(d).getNumberOfSavedSolutions() > 0);

  // a shape no problem uses: the solutions stay
  Document e = openFixture("PelikanBurr.xmpuzzle");
  const Id extra = e.createShape().value();
  REQUIRE(e.editCells(extra, CellTool::Fixed, 0, 0, {{0, 0, 0}}).has_value());
  CHECK(e.puzzle().getProblem(0).getNumberOfSavedSolutions() > 0);
}

TEST_CASE("document: resize, one shape or all", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id a = d.createShape(std::array<unsigned int, 3>{2, 2, 2}).value();
  const Id b = d.createShape(std::array<unsigned int, 3>{5, 1, 1}).value();
  roundTrip(d, [a](Document& x) { return x.resizeShape(a, 3, 2, 1, false); });
  CHECK(d.puzzle().getShape(0).getX() == 3);
  CHECK(d.puzzle().getShape(0).getZ() == 1);
  CHECK(d.puzzle().getShape(1).getX() == 5);
  // to all: the others grow by as much (never shrink); this one is set
  roundTrip(d, [a](Document& x) { return x.resizeShape(a, 4, 2, 1, true); });
  CHECK(d.puzzle().getShape(0).getX() == 4);
  CHECK(d.puzzle().getShape(1).getX() == 6);
  CHECK(d.puzzle().getShape(1).getY() == 1);
  (void)b;
  refused(d, Errc::InvalidArgument, [a](Document& x) { return x.resizeShape(a, 0, 1, 1, false); });

  // the rhombic grid sizes in blocks of 5
  Document r(GridKind::Rhombic);
  const Id s = r.createShape().value();
  REQUIRE(r.resizeShape(s, 2, 1, 1, false).has_value());
  CHECK(r.puzzle().getShape(0).getX() == 10);
}

TEST_CASE("document: every tool-tab task of every grid round-trips", "[app][document]") {
  for (const GridKind grid : test::kGeometryGrids) {
    INFO(toString(grid));
    // a small shape of three cells in the middle of a grid with room
    Document d(grid);
    // room for the nudges either side (the cut grids move a block of 5 or 6)
    const unsigned int n = sizeUnit(grid) == 1 ? 8 : 6 * sizeUnit(grid);
    const Id s = d.createShape(std::array<unsigned int, 3>{n, n, n}).value();
    const Voxel& v = d.puzzle().getShape(0);
    // the three valid cells nearest the middle
    std::vector<CellEdit> cells;
    const int m = asInt(n) / 2;
    for (const auto& [x, y, z] : test::validCoordinates(v, asInt(n))) {
      cells.push_back({x, y, z});
    }
    std::sort(cells.begin(), cells.end(), [m](const CellEdit& a, const CellEdit& b) {
      const auto d2 = [m](const CellEdit& c) {
        return (c.x - m) * (c.x - m) + (c.y - m) * (c.y - m) + (c.z - m) * (c.z - m);
      };
      return d2(a) < d2(b);
    });
    cells.resize(3);
    REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, cells).value() == 3);
    const std::string base = io::writePuzzle(d.puzzle());

    for (const ShapeTaskInfo& t : taskSet(grid)) {
      INFO(t.label);
      const Result<TaskOutcome> o = d.shapeTask(s, t.task, false);
      REQUIRE(o.has_value());
      if (!o->changed) {
        // e.g. fill holes without holes
        CHECK(io::writePuzzle(d.puzzle()) == base);
        continue;
      }
      const std::string after = io::writePuzzle(d.puzzle());
      CHECK(after != base);
      REQUIRE(d.undo().has_value());
      CHECK(io::writePuzzle(d.puzzle()) == base);
      REQUIRE(d.redo().has_value());
      CHECK(io::writePuzzle(d.puzzle()) == after);
      REQUIRE(d.undo().has_value());
    }
  }
}

TEST_CASE("document: a nudge out of the grid is refused, a task the grid lacks too",
          "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape(std::array<unsigned int, 3>{2, 2, 2}).value();
  REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, {{1, 0, 0}}).has_value());
  roundTrip(d, [s](Document& x) { return x.shapeTask(s, ShapeTask::Nudge1, false); });
  CHECK(d.puzzle().getShape(0).isFilled(0, 0, 0));
  refused(d, Errc::InvalidArgument,
          [s](Document& x) { return x.shapeTask(s, ShapeTask::Nudge1, false); });
  refused(d, Errc::Unsupported,
          [s](Document& x) { return x.shapeTask(s, ShapeTask::Nudge27, false); });

  // to all: the grid tasks only
  const Id t = d.createShape(std::array<unsigned int, 3>{3, 3, 3}).value();
  REQUIRE(d.editCells(t, CellTool::Fixed, 0, 0, {{2, 2, 2}}).has_value());
  roundTrip(d, [s](Document& x) { return x.shapeTask(s, ShapeTask::Prune, true); });
  CHECK(d.puzzle().getShape(1).getX() == 1);
  // a flip "to all" flips only the shape asked for
  REQUIRE(d.editCells(t, CellTool::Fixed, 0, 0, {{0, 0, 0}}).has_value());
  const std::unique_ptr<Voxel> before = d.puzzle().getShape(1).clone();
  REQUIRE(d.shapeTask(s, ShapeTask::Flip12, true).has_value());
  CHECK(d.puzzle().getShape(1) == *before);
}

TEST_CASE("document: minimize scales all shapes down together", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape(std::array<unsigned int, 3>{1, 1, 1}).value();
  REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, {{0, 0, 0}}).has_value());
  REQUIRE(d.shapeTask(s, ShapeTask::Triple, false).has_value());
  CHECK(d.puzzle().getShape(0).countState(Voxel::kFilled) == 27);
  const auto o = roundTrip(d, [s](Document& x) { return x.shapeTask(s, ShapeTask::Minimize, false); });
  CHECK(o->changed);
  CHECK(d.puzzle().getShape(0).countState(Voxel::kFilled) == 1);
}

TEST_CASE("document: colours", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id a = roundTrip(d, [](Document& x) { return x.addColour(Rgb{1, 2, 3}); }).value();
  // every problem allows the new colour on itself
  CHECK(problem0(d).placementAllowed(d.puzzle(), 1, 1));
  const Id b = d.addColour(Rgb{4, 5, 6}).value();
  roundTrip(d, [b](Document& x) { return x.editColour(b, Rgb{7, 8, 9}); });
  CHECK(d.puzzle().getColor(1) == Rgb{7, 8, 9});

  // a shape coloured with the first colour
  const Id s = firstShape(d);
  REQUIRE(d.editCells(s, CellTool::Paint, a, kSpanX | kSpanY | kSpanZ, {{0, 0, 0}}).has_value());
  const Id other = d.createShape().value();
  REQUIRE(d.editCells(other, CellTool::Fixed, b, 0, {{0, 0, 0}}).has_value());
  roundTrip(d, [a](Document& x) { return x.deleteColour(a); });
  CHECK(d.puzzle().colorNumber() == 1);
  CHECK(d.colourIds() == std::vector<Id>{b});
  // the cells of the deleted colour are neutral, the higher colour moved down
  const Voxel& o = d.puzzle().getShape(d.shapeIndex(other).value());
  CHECK(o.getColor(0, 0, 0) == 1);

  Document full(GridKind::Bricks);
  for (unsigned int i = 0; i < Puzzle::kMaxColors; i++) {
    REQUIRE(full.addColour(Rgb{}).has_value());
  }
  refused(full, Errc::InvalidArgument, [](Document& x) { return x.addColour(Rgb{}); });
}

TEST_CASE("document: problems are made, copied, deleted, moved and named", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  REQUIRE(d.addColour(Rgb{1, 1, 1}).has_value());
  const Id p0 = firstProblem(d);
  const Id p1 = roundTrip(d, [](Document& x) { return x.createProblem(); }).value();
  CHECK(d.puzzle().getNumberOfProblems() == 2);
  // a new problem allows each colour on itself
  CHECK(d.puzzle().getProblem(1).placementAllowed(d.puzzle(), 1, 1));
  const Id p2 = roundTrip(d, [p0](Document& x) { return x.duplicateProblem(p0); }).value();
  const Problem& copy = d.puzzle().getProblem(2);
  CHECK(copy.getParts() == problem0(d).getParts());
  CHECK(copy.getNumberOfSavedSolutions() == 0);
  CHECK(copy.getName().empty());
  roundTrip(d, [p2](Document& x) { return x.moveProblem(p2, 0); });
  CHECK(d.problemIds() == std::vector<Id>{p2, p0, p1});
  roundTrip(d, [p1](Document& x) { return x.renameProblem(p1, "second"); });
  CHECK(d.puzzle().getProblem(2).getName() == "second");
  roundTrip(d, [p0](Document& x) { return x.deleteProblem(p0); });
  CHECK(d.problemIds() == std::vector<Id>{p2, p1});
}

TEST_CASE("document: the pieces of a problem", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id p = firstProblem(d);
  const Problem& pr = problem0(d);
  const unsigned int resultIdx = pr.getResultId();
  const unsigned int pieceIdx = pr.getShapeIdOfPart(0);
  const Id piece = d.shapeIds()[pieceIdx];
  const Id spare = d.createShape().value();
  const unsigned int spareIdx = d.shapeIndex(spare).value();

  SECTION("one more, one fewer, a count range") {
    roundTrip(d, [p, spare](Document& x) { return x.addPiece(p, spare); });
    CHECK(problem0(d).getShapeMinimum(spareIdx) == 1);
    CHECK(problem0(d).getShapeMaximum(spareIdx) == 1);
    roundTrip(d, [p, spare](Document& x) { return x.setPieceCount(p, spare, 0, 3); });
    CHECK(problem0(d).getShapeMaximum(spareIdx) == 3);
    roundTrip(d, [p, spare](Document& x) { return x.removePiece(p, spare); });
    CHECK(problem0(d).getShapeMaximum(spareIdx) == 2);
    refused(d, Errc::InvalidArgument, [p, spare](Document& x) { return x.setPieceCount(p, spare, 3, 1); });
    // the result cannot be a piece
    const Id result = d.shapeIds()[resultIdx];
    refused(d, Errc::InvalidArgument, [p, result](Document& x) { return x.setPieceCount(p, result, 1, 1); });
  }
  SECTION("the result: a piece made the result stops being one") {
    roundTrip(d, [p, piece](Document& x) { return x.setResult(p, piece); });
    CHECK(problem0(d).getResultId() == pieceIdx);
    CHECK(problem0(d).getShapeMaximum(pieceIdx) == 0);
  }
  SECTION("bulk: one range for all, one of each, none") {
    roundTrip(d, [p](Document& x) { return x.setRangeAll(p, 2, 1); });
    for (unsigned int i = 0; i < problem0(d).getNumberOfParts(); i++) {
      CHECK(problem0(d).getPartMinimum(i) == 2);
      CHECK(problem0(d).getPartMaximum(i) == 2);
    }
    CHECK(problem0(d).getNumberOfSavedSolutions() == 0);
    roundTrip(d, [p](Document& x) { return x.addOneOfEach(p); });
    CHECK(problem0(d).getShapeMaximum(spareIdx) == 1);
    CHECK(problem0(d).getShapeMaximum(resultIdx) == 0);
    roundTrip(d, [p](Document& x) { return x.removeAllPieces(p); });
    CHECK(problem0(d).getNumberOfParts() == 0);
  }
  SECTION("the order of the parts") {
    const unsigned int second = problem0(d).getShapeIdOfPart(1);
    roundTrip(d, [p, piece](Document& x) { return x.movePart(p, piece, 1); });
    CHECK(problem0(d).getShapeIdOfPart(0) == second);
    CHECK(problem0(d).getShapeIdOfPart(1) == pieceIdx);
    refused(d, Errc::InvalidArgument, [p, piece](Document& x) { return x.movePart(p, piece, 2); });
  }
  SECTION("groups") {
    roundTrip(d, [p, piece](Document& x) { return x.setGroup(p, piece, 2); });
    const unsigned int part = problem0(d).getPartIdForShape(pieceIdx);
    REQUIRE(problem0(d).getNumberOfPartGroups(part) == 1);
    CHECK(problem0(d).getPartGroupId(part, 0) == 2);
    CHECK(problem0(d).getPartGroupCount(part, 0) == problem0(d).getPartMaximum(part));
    roundTrip(d, [p, piece](Document& x) { return x.setGroupCount(p, piece, 3, 1); });
    CHECK(problem0(d).getNumberOfPartGroups(part) == 2);
    roundTrip(d, [p](Document& x) { return x.deleteGroup(p, 2); });
    REQUIRE(problem0(d).getNumberOfPartGroups(part) == 1);
    CHECK(problem0(d).getPartGroupId(part, 0) == 3);
    roundTrip(d, [p, piece](Document& x) { return x.setGroup(p, piece, 0); });
    CHECK(problem0(d).getNumberOfPartGroups(part) == 0);
    refused(d, Errc::InvalidArgument, [p, spare](Document& x) { return x.setGroup(p, spare, 1); });
  }
}

TEST_CASE("document: colour rules", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id p = firstProblem(d);
  const Id a = d.addColour(Rgb{1, 0, 0}).value();
  const Id b = d.addColour(Rgb{0, 1, 0}).value();
  roundTrip(d, [p, a, b](Document& x) { return x.setColourRule(p, a, b, true); });
  CHECK(problem0(d).placementAllowed(d.puzzle(), 1, 2));
  roundTrip(d, [p, a](Document& x) { return x.setColourRule(p, a, a, false); });
  CHECK_FALSE(problem0(d).placementAllowed(d.puzzle(), 1, 1));
  roundTrip(d, [p](Document& x) { return x.setColourRules(p, ColourPreset::AllowAll); });
  CHECK(problem0(d).placementAllowed(d.puzzle(), 2, 1));
  roundTrip(d, [p](Document& x) { return x.setColourRules(p, ColourPreset::EachOnItself); });
  CHECK(problem0(d).placementAllowed(d.puzzle(), 2, 2));
  CHECK_FALSE(problem0(d).placementAllowed(d.puzzle(), 2, 1));
  roundTrip(d, [p](Document& x) { return x.setColourRules(p, ColourPreset::Clear); });
  CHECK(problem0(d).getColorConstraints().empty());
}

TEST_CASE("document: the stored solver settings", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id p = firstProblem(d);
  const unsigned int saved = problem0(d).getNumberOfSavedSolutions();
  const std::map<std::string, std::string> o = {{"disassemble", "1"}, {"solverType", "bt2"}};
  roundTrip(d, [p, &o](Document& x) { return x.setSolverOptions(p, o); });
  CHECK(problem0(d).getSolverOptions() == o);
  // only stored: the solutions stay
  CHECK(problem0(d).getNumberOfSavedSolutions() == saved);
  REQUIRE(d.setSolverOptions(p, {{"justCount", "1"}}).has_value());
  CHECK(d.lastChange().problems == std::vector<Id>{p});
  REQUIRE(d.undo().has_value());
  // the same settings again change nothing
  const std::uint64_t rev = d.revision();
  REQUIRE(d.setSolverOptions(p, o).has_value());
  CHECK(d.revision() == rev);
  // they are saved with the file
  Document again = Document::openBytes(d.saveBytes().value()).value();
  CHECK(again.puzzle().getProblem(0).getSolverOptions() == o);
  CHECK(d.setSolverOptions(0, o).error().code == Errc::NotFound);
}

TEST_CASE("document: what undo and redo changed is not the last command's", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape().value();
  REQUIRE(d.renameShape(s, "x").has_value());
  CHECK(d.lastChange().shapes == std::vector<Id>{s});
  REQUIRE(d.undo().has_value());
  CHECK(d.lastChange().what == kChangeShapes);
  CHECK(d.lastChange().shapes.empty());
  REQUIRE(d.redo().has_value());
  CHECK(d.lastChange().shapes.empty());
}

TEST_CASE("document: solutions are removed and turned into shapes", "[app][document]") {
  Document d = openFixture("SolidSixPieceBurrs.xmpuzzle");
  const Id p = firstProblem(d);
  const auto ids = [&d] {
    std::vector<Id> v;
    for (unsigned int i = 0; i < problem0(d).getNumberOfSavedSolutions(); i++) {
      v.push_back(problem0(d).getSavedSolution(i).getId());
    }
    return v;
  };
  const std::vector<Id> all = ids();
  REQUIRE(all.size() > 10);

  roundTrip(d, [p, &all](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::One, all[3]); });
  std::vector<Id> want = all;
  want.erase(want.begin() + 3);
  CHECK(ids() == want);
  roundTrip(d, [p, &all](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::Before, all[2]); });
  CHECK(ids().front() == all[2]);
  roundTrip(d, [p, &all](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::After, all[5]); });
  CHECK(ids().back() == all[5]);
  roundTrip(d, [p](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::All); });
  CHECK(problem0(d).getNumberOfSavedSolutions() == 0);
  refused(d, Errc::NotFound, [p](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::One, 77777); });

  // the disassemblies (Pelikan Burr keeps them in full)
  Document f = openFixture("PelikanBurr.xmpuzzle");
  const Id fp = firstProblem(f);
  REQUIRE(problem0(f).getSavedSolution(0).getDisassembly() != nullptr);
  roundTrip(f, [fp](Document& x) { return x.deleteDisassemblies(fp, std::nullopt); });
  CHECK(problem0(f).getSavedSolution(0).getDisassembly() == nullptr);
  CHECK(problem0(f).getSavedSolution(0).getSeparationInfo() != nullptr);

  Document e = openFixture("SolidSixPieceBurrs.xmpuzzle");
  const unsigned int shapes = e.puzzle().getNumberOfShapes();
  const unsigned int problems = e.puzzle().getNumberOfProblems();
  AssemblyImport opt;
  opt.solution = e.puzzle().getProblem(0).getSavedSolution(0).getId();
  opt.addTo = AssemblyImport::AddTo::NewProblem;
  const std::vector<Id> added = roundTrip(e, [&opt](Document& x) {
                                  return x.assembliesToShapes(x.problemIds().front(), opt);
                                }).value();
  REQUIRE(added.size() == 1);
  CHECK(e.puzzle().getNumberOfShapes() == shapes + 1);
  CHECK(e.puzzle().getNumberOfProblems() == problems + 1);
  // the shape has the cells of the assembly's pieces
  CHECK(e.puzzle().getShape(shapes).countState(Voxel::kFilled) ==
        e.puzzle().getProblem(0).resultShape(e.puzzle()).countState(Voxel::kFilled));
  CHECK(e.puzzle().getProblem(problems).getShapeMaximum(shapes) == 1);

  // all of them, identical ones once
  AssemblyImport every;
  every.dropIdentical = true;
  const std::vector<Id> unique = e.assembliesToShapes(e.problemIds().front(), every).value();
  CHECK_FALSE(unique.empty());
  CHECK(unique.size() <= e.puzzle().getProblem(0).getNumberOfSavedSolutions());
}

TEST_CASE("document: groups of edits are one step; depth, dirty and history", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape().value();
  const std::string before = io::writePuzzle(d.puzzle());
  const std::size_t steps = d.history().undo.size();

  // a drag over cells: one step, and every batch a change of its own (a
  // new revision naming the shape), though the shape is saved only once
  REQUIRE(d.beginGroup("Draw").has_value());
  for (int i = 0; i < 4; i++) {
    const std::uint64_t rev = d.revision();
    REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, {{i, 0, 0}}).has_value());
    CHECK(d.revision() == rev + 1);
    CHECK(d.lastChange().shapes == std::vector<Id>{s});
  }
  // a batch that changes nothing is no change
  const std::uint64_t still = d.revision();
  REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, {{0, 0, 0}}).has_value());
  CHECK(d.revision() == still);
  REQUIRE(d.shapeTask(s, ShapeTask::Nudge2, false).has_value());
  CHECK(d.revision() == still + 1);
  CHECK(d.beginGroup("again").error().code == Errc::InvalidState);
  CHECK(d.undo().error().code == Errc::InvalidState);
  REQUIRE(d.endGroup().has_value());
  CHECK(d.history().undo.size() == steps + 1);
  CHECK(d.history().undo.back() == "Draw");
  const std::string after = io::writePuzzle(d.puzzle());
  REQUIRE(d.undo().has_value());
  CHECK(io::writePuzzle(d.puzzle()) == before);
  CHECK(d.history().redo.front() == "Draw");
  REQUIRE(d.redo().has_value());
  CHECK(io::writePuzzle(d.puzzle()) == after);
  CHECK(d.endGroup().error().code == Errc::InvalidState);

  // an empty group leaves no step
  REQUIRE(d.beginGroup("nothing").has_value());
  REQUIRE(d.endGroup().has_value());
  CHECK(d.history().undo.size() == steps + 1);

  // a new edit after an undo drops the redo
  REQUIRE(d.undo().has_value());
  REQUIRE(d.renameShape(s, "x").has_value());
  CHECK_FALSE(d.canRedo());

  // the depth: the oldest steps go
  d.setSettings(DocumentSettings{3});
  for (int i = 0; i < 5; i++) {
    REQUIRE(d.renameShape(s, "n" + std::to_string(i)).has_value());
  }
  CHECK(d.history().undo.size() == 3);
  CHECK(d.undo().has_value());
  CHECK(d.undo().has_value());
  CHECK(d.undo().has_value());
  CHECK(d.undo().error().code == Errc::InvalidState);

  // clean after a save; undoing back to there is clean again
  Document e(GridKind::Bricks);
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "bt_dirty.xmpuzzle";
  REQUIRE(e.createShape().has_value());
  REQUIRE(e.save(tmp).has_value());
  CHECK_FALSE(e.dirty());
  REQUIRE(e.createShape().has_value());
  CHECK(e.dirty());
  REQUIRE(e.undo().has_value());
  CHECK_FALSE(e.dirty());
  REQUIRE(e.undo().has_value());
  CHECK(e.dirty());
  std::filesystem::remove(tmp);
}

TEST_CASE("document: queries and meshes through the document", "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const FitStats f = d.fit(firstProblem(d)).value();
  CHECK(f.verdict == FitVerdict::Fits);
  CHECK(f.statusLine.starts_with("Problem P1 result can contain"));
  CHECK(d.shapeTable().size() == d.puzzle().getNumberOfShapes());
  const mesh::DrawMesh m = d.shapeMesh(firstShape(d), mesh::MeshStyle::Drawing).value();
  CHECK(m.triangleCount() > 0);
  CHECK(d.shapeMesh(4242, mesh::MeshStyle::Flat).error().code == Errc::NotFound);
}

TEST_CASE("document: names and notes keep to text the file can hold", "[app][document]") {
  Document d(GridKind::Bricks);
  const Id s = d.createShape().value();
  const Id p = d.createProblem().value();
  // control characters are not well-formed XML 1.0; a name is an attribute,
  // where a line break would come back as a space
  test::refused(d, Errc::InvalidArgument, [s](Document& x) { return x.renameShape(s, "line\nbreak"); });
  test::refused(d, Errc::InvalidArgument, [s](Document& x) { return x.renameShape(s, "a\rb"); });
  test::refused(d, Errc::InvalidArgument,
                [s](Document& x) { return x.renameShape(s, std::string("a\0b", 3)); });
  test::refused(d, Errc::InvalidArgument, [s](Document& x) { return x.renameShape(s, "\x01ctrl"); });
  test::refused(d, Errc::InvalidArgument, [p](Document& x) { return x.renameProblem(p, "x\ny"); });
  test::refused(d, Errc::InvalidArgument,
                [](Document& x) { return x.setNotes(std::string("a\0b", 3)); });
  test::refused(d, Errc::InvalidArgument, [](Document& x) { return x.setNotes("\x1f"); });
  // tabs in names, and line breaks in the notes, are fine
  CHECK(d.renameShape(s, "tab\there").has_value());
  CHECK(d.renameProblem(p, "tab\there").has_value());
  CHECK(d.setNotes("two\nlines\r\n\twith a tab").has_value());
  CHECK(d.puzzle().getComment() == "two\nlines\r\n\twith a tab");
}

TEST_CASE("document: a shape's mesh in an orientation is the mesh of the turned shape",
          "[app][document]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id s = firstShape(d);
  const Voxel& v = d.puzzle().getShape(0);
  for (const unsigned int t : {0U, 5U, 23U, 47U}) {
    INFO("transformation " << t);
    const std::unique_ptr<Voxel> turned = v.clone();
    REQUIRE(turned->transform(t));
    for (const mesh::MeshStyle style : {mesh::MeshStyle::Drawing, mesh::MeshStyle::Flat}) {
      const mesh::DrawMesh expect = mesh::shapeDrawMesh(*turned, style);
      const mesh::DrawMesh got = d.shapeMesh(s, style, t).value();
      CHECK(got.positions == expect.positions);
      CHECK(got.normals == expect.normals);
      CHECK(got.indices == expect.indices);
      CHECK(got.edgeLines == expect.edgeLines);
    }
  }
  // 0 is the shape as it is; beyond the grid's table is refused
  CHECK(d.shapeMesh(s, mesh::MeshStyle::Drawing, 0).value().positions ==
        d.shapeMesh(s, mesh::MeshStyle::Drawing).value().positions);
  CHECK(d.shapeMesh(s, mesh::MeshStyle::Drawing, 48).error().code == Errc::InvalidArgument);
  CHECK(d.shapeMesh(4242, mesh::MeshStyle::Drawing, 1).error().code == Errc::NotFound);
}
