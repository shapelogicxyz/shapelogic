// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles through the document: start/goal shapes and stamps as
// the original GUI made them (its New Start/Goal, the start/goal editor's
// clicks), the problems the user makes on them, the shape commands that keep
// the marks in step, the buttons it greyed out, the checks of its Solve, shapes
// of more than one layer, and a sliding solve played back. Every command is
// checked to undo and redo byte for byte.
#include "app_test_helpers.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::app;
using bt::test::openFixture;
using bt::test::refused;
using bt::test::roundTrip;

namespace {

// A new shape of w by h cells (one layer) with the cells given filled.
Id piece(Document& d, unsigned int w, unsigned int h, const std::vector<CellEdit>& cells) {
  const Result<Id> s = d.createShape(std::array<unsigned int, 3>{w, h, 1});
  REQUIRE(s.has_value());
  REQUIRE(d.editCells(*s, CellTool::Fixed, 0, 0, cells).has_value());
  return *s;
}

unsigned int index(const Document& d, Id shape) { return d.shapeIndex(shape).value(); }

// A new board with a new problem on it, as the user makes them: + New
// Board, then a new problem with the board chosen as its result.
Id board(Document& d, std::array<unsigned int, 3> size) {
  const Id tray = d.addStartGoal(size).value();
  const Id p = d.createProblem().value();
  REQUIRE(d.setResult(p, tray).has_value());
  return tray;
}

// The marks of a start/goal shape as text: per cell "start/goal".
std::string marks(const Document& d, Id tray) {
  const Voxel& v = d.puzzle().getShape(index(d, tray));
  std::string out;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    out += std::to_string(v.getStartPiece(i)) + "/" + std::to_string(v.getGoalPiece(i)) + " ";
  }
  return out;
}

}  // namespace

TEST_CASE("sliding document: the 81-move test puzzle built through the commands solves",
          "[app][sliding]") {
  // tests/data/puzzles/test_sliding_solver.xmpuzzle rebuilt as the GUI
  // would: a 4x5 start/goal shape, ten pieces stamped in the order the
  // file's problem lists them, the 2x2 block's goal at the bottom centre
  Document d(GridKind::Sliding);
  const Id tray = roundTrip(d, [](Document& x) {
                    return x.addStartGoal({4, 5, 1});
                  }).value();
  // no problem comes with the board: the user makes one and chooses it
  CHECK(d.problemIds().empty());
  CHECK(d.puzzle().getShape(index(d, tray)).getName() == "sg:1:");
  const Id problem = roundTrip(d, [](Document& x) { return x.createProblem(); }).value();
  roundTrip(d, [=](Document& x) { return x.setResult(problem, tray); });
  CHECK(d.puzzle().getProblem(0).getName().empty());

  const Id block = piece(d, 2, 2, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}});
  std::vector<Id> upright;
  for (int k = 0; k < 4; k++) {
    upright.push_back(piece(d, 1, 2, {{0, 0, 0}, {0, 1, 0}}));
  }
  const Id flat = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  std::vector<Id> unit;
  for (int k = 0; k < 4; k++) {
    unit.push_back(piece(d, 1, 1, {{0, 0, 0}}));
  }
  // the file's order: S2, S3, S4, S7, S8, S9, S10, S11, S5, S6
  const std::vector<std::pair<Id, std::array<int, 2>>> starts = {
      {block, {1, 4}},     {upright[0], {0, 4}}, {upright[1], {0, 2}}, {flat, {1, 2}},
      {unit[0], {1, 1}},   {unit[1], {0, 0}},    {unit[2], {3, 0}},    {unit[3], {2, 1}},
      {upright[2], {3, 4}}, {upright[3], {3, 2}},
  };
  for (const auto& [shape, at] : starts) {
    const Id s = shape;
    const int x = at[0];
    const int y = at[1];
    CHECK(roundTrip(d, [=](Document& z) { return z.placeStamp(tray, s, x, y, 0, false); }).value());
  }
  // a goal without which the shape is not valid yet
  CHECK_FALSE(sliding::startGoalError(d.puzzle(), index(d, tray)).empty());
  CHECK(roundTrip(d, [=](Document& z) {
          return z.placeStamp(tray, block, 1, 1, 0, true);
        }).value());
  CHECK(sliding::startGoalError(d.puzzle(), index(d, tray)).empty());

  // the problem and the marks are the file's
  const Puzzle fixture = test::openFixture("test_sliding_solver.xmpuzzle").puzzle();
  const Problem& want = fixture.getProblem(0);
  const Problem& got = d.puzzle().getProblem(0);
  REQUIRE(got.getNumberOfParts() == want.getNumberOfParts());
  for (unsigned int p = 0; p < got.getNumberOfParts(); p++) {
    CHECK(got.getShapeIdOfPart(p) == want.getShapeIdOfPart(p));
    CHECK(got.getPartMinimum(p) == 1);
    CHECK(got.getPartMaximum(p) == 1);
  }
  const Voxel& wantTray = fixture.getShape(0);
  const Voxel& gotTray = d.puzzle().getShape(index(d, tray));
  for (unsigned int i = 0; i < wantTray.getXYZ(); i++) {
    CHECK(gotTray.getStartPiece(i) == wantTray.getStartPiece(i));
    CHECK(gotTray.getGoalPiece(i) == wantTray.getGoalPiece(i));
  }
  // and the pieces carry no colours: the marks hold them to their starts
  for (unsigned int s = 1; s < d.puzzle().getNumberOfShapes(); s++) {
    CHECK(sliding::pieceColor(d.puzzle(), s) == 0);
  }
  CHECK(d.colourIds().empty());

  // solved through the document: one solution of 81 moves, which plays
  SolveOptions o;
  o.disassemble = true;
  REQUIRE(d.solveStart(d.problemIds()[0], o).has_value());
  REQUIRE(d.solveWait(d.problemIds()[0]).has_value());
  (void)d.poll();
  const Problem& solved = d.puzzle().getProblem(0);
  REQUIRE(solved.getNumberOfSavedSolutions() == 1);
  CHECK(solved.getSavedSolution(0).getDisassembly()->getMoves() == 81);
  const Result<PlaybackSampler> pb =
      d.playback(d.problemIds()[0], solved.getSavedSolution(0).getId());
  REQUIRE(pb.has_value());
  CHECK(pb->stepCount() == 81);
}

TEST_CASE("sliding document: stamps are placed, moved, taken off and checked", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({5, 3, 1}).value();
  const Id L = piece(d, 2, 2, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
  const Id U = piece(d, 1, 1, {{0, 0, 0}});

  // placed, then placed again at the same place: taken off
  CHECK(roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 1, 1, 0, false); }).value());
  CHECK_FALSE(
      roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 1, 1, 0, false); }).value());
  CHECK(d.placeStamp(tray, L, 1, 1, 0, false).value());
  // elsewhere: moved
  CHECK(roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 0, 2, 0, false); }).value());
  CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, tray)), 0, 2, 0, false) == index(d, L));

  // what does not fit is refused with the reason, nothing changed
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(tray, L, 4, 0, 0, false); });
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(tray, U, 0, 1, 0, false); });
  // a start/goal shape is no piece, and a piece is no start/goal shape
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(tray, tray, 0, 0, 0, true); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(U, L, 0, 0, 0, true); });

  // the right click: what is under the cell comes off; nothing there:
  // false (the L at (0,2) covers (0,2), (0,1) and (1,1))
  CHECK(roundTrip(d, [=](Document& x) { return x.clearStampAt(tray, 1, 1, 0, false); }).value());
  CHECK_FALSE(d.clearStampAt(tray, 1, 1, 0, false).value());
  CHECK_FALSE(d.clearStampAt(tray, 1, 2, 0, false).value());

  // not on other grids
  Document bricks(GridKind::Bricks);
  refused(bricks, Errc::Unsupported, [](Document& x) { return x.addStartGoal({3, 3, 1}); });
}

TEST_CASE("sliding document: the starts are cloned to the goals, and back", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 2, 1}).value();
  const Id a = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  const Id b = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 1, 0, false).value());
  REQUIRE(d.placeStamp(tray, b, 3, 0, 0, true).value());
  const unsigned int ma = index(d, a) + 1;
  const unsigned int mb = index(d, b) + 1;

  // start to goal: the goals become the starts, b's goal replaced
  roundTrip(d, [=](Document& x) { return x.cloneStamps(tray, true); });
  const Voxel& v = d.puzzle().getShape(index(d, tray));
  CHECK(v.getGoalPiece(0, 1, 0) == ma);
  CHECK(v.getGoalPiece(1, 1, 0) == ma);
  CHECK(v.getGoalPiece(3, 0, 0) == 0);
  CHECK(sliding::startGoalError(d.puzzle(), index(d, tray)).empty());

  // goal to start, after the goal moved
  REQUIRE(d.placeStamp(tray, b, 3, 0, 0, true).value());
  roundTrip(d, [=](Document& x) { return x.cloneStamps(tray, false); });
  CHECK(d.puzzle().getShape(index(d, tray)).getStartPiece(3, 0, 0) == mb);

  // a start on a corridor cannot be a goal: refused, nothing changed
  REQUIRE(d.editCells(tray, CellTool::Variable, 0, 0, {{0, 0, 0}}).has_value());
  REQUIRE(d.placeStamp(tray, b, 0, 0, 0, false).value());
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.cloneStamps(tray, true); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.cloneStamps(a, true); });
  roundTrip(d, [=](Document& x) { return x.cloneStamps(tray, false); });
}

TEST_CASE("sliding document: the shape commands keep the marks in step", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = board(d, {4, 3, 1});
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id b = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, 0, false).value());
  REQUIRE(d.placeStamp(tray, b, 2, 2, 0, false).value());
  REQUIRE(d.placeStamp(tray, b, 0, 2, 0, true).value());
  const unsigned int ia = index(d, a);
  const unsigned int ib = index(d, b);

  SECTION("delete: the marks of the shapes above move down, the shape's own go") {
    roundTrip(d, [=](Document& x) { return x.deleteShape(a); });
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 0, 0, 0, false) == static_cast<unsigned int>(-1));
    // b is a shape lower now, and so is its mark
    CHECK(index(d, b) == ib - 1);
    CHECK(sliding::stampAt(&t, 2, 2, 0, false) == index(d, b));
    CHECK(sliding::stampAt(&t, 0, 2, 0, true) == index(d, b));
    // and the problem keeps b alone
    REQUIRE(d.puzzle().getProblem(0).getNumberOfParts() == 1);
    CHECK(d.puzzle().getProblem(0).getShapeIdOfPart(0) == index(d, b));
  }
  SECTION("move: the marks follow") {
    roundTrip(d, [=](Document& x) { return x.moveShape(b, ia); });
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 2, 2, 0, false) == index(d, b));
    CHECK(sliding::stampAt(&t, 0, 0, 0, false) == index(d, a));
    CHECK(sliding::stampAt(&t, 0, 2, 0, true) == index(d, b));
  }
  SECTION("duplicate a start/goal shape: the next number, no problem") {
    const Id copy = roundTrip(d, [=](Document& x) { return x.duplicateShape(tray); }).value();
    CHECK(d.puzzle().getShape(index(d, copy)).getName() == "sg:2:");
    CHECK(d.problemIds().size() == 1);
    CHECK(marks(d, copy) == marks(d, tray));
    // a copy of a piece is a plain copy
    const Id pc = roundTrip(d, [=](Document& x) { return x.duplicateShape(a); }).value();
    CHECK(d.puzzle().getShape(index(d, pc)).getName().empty());
    CHECK(d.problemIds().size() == 1);
  }
  SECTION("rename a start/goal shape: the user's part") {
    roundTrip(d, [=](Document& x) { return x.renameShape(tray, "Klotski"); });
    CHECK(d.puzzle().getShape(index(d, tray)).getName() == "sg:1:Klotski");
    CHECK(sliding::displayName(&d.puzzle().getShape(index(d, tray))) == "Klotski");
    // the problem on it keeps its own name
    CHECK(d.puzzle().getProblem(0).getName().empty());
  }
  SECTION("edit a piece: moved in its grid it keeps its marks, changed it loses them") {
    const std::string before = marks(d, tray);
    // b moved one row up in its own 2x1 grid: grow it first, then move
    REQUIRE(d.resizeShape(b, 2, 2, 1, false).has_value());
    CHECK(marks(d, tray) == before);
    CHECK(d.lastChange().notice.empty());
    const test::DocState was = test::state(d);
    REQUIRE(d.editCells(b, CellTool::Fixed, 0, 0, {{0, 1, 0}}).has_value());
    CHECK(d.lastChange().notice ==
          "S2 changed shape: its start and goal were removed from B1.");
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 2, 2, 0, false) == static_cast<unsigned int>(-1));
    CHECK(sliding::stampAt(&t, 0, 2, 0, true) == static_cast<unsigned int>(-1));
    CHECK(sliding::stampAt(&t, 0, 0, 0, false) == ia);
    // b keeps its part (the original's sync only adds parts)
    CHECK(d.puzzle().getProblem(0).getShapeMaximum(ib) == 1);
    // one step: undone, everything is back
    const test::DocState now = test::state(d);
    REQUIRE(d.undo().has_value());
    CHECK(test::state(d) == was);
    REQUIRE(d.redo().has_value());
    CHECK(test::state(d) == now);
  }
  SECTION("walls on a start/goal shape: the problem follows") {
    // a's start cell made a wall: its mark goes (the part stays, as the
    // original's sync only adds parts)
    roundTrip(d, [=](Document& x) {
      return x.editCells(tray, CellTool::Erase, 0, 0, {{0, 0, 0}});
    });
    CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, tray)), 0, 0, 0, false) ==
          static_cast<unsigned int>(-1));
    CHECK(d.puzzle().getProblem(0).getNumberOfParts() == 2);
  }
}

TEST_CASE("sliding document: what the original greyed out is refused", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = board(d, {4, 3, 1});
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id free = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, 0, false).value());
  const Id p = d.problemIds()[0];

  // the result is a board, never a piece; a stamped piece keeps a copy
  // for each of its stamps, and may have more
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setResult(p, free); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.removePiece(p, a); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setPieceCount(p, a, 0, 3); });
  roundTrip(d, [=](Document& x) { return x.addPiece(p, a); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, a)) == 2);
  roundTrip(d, [=](Document& x) { return x.removePiece(p, a); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, a)) == 1);
  // no groups
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setGroup(p, a, 1); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setGroupCount(p, a, 1, 1); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.deleteGroup(p, 1); });

  // the bulk buttons leave a stamped piece alone
  roundTrip(d, [=](Document& x) { return x.addOneOfEach(p); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, a)) == 1);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, free)) == 1);
  roundTrip(d, [=](Document& x) { return x.setRangeAll(p, 0, 3); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, a)) == 1);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, free)) == 3);
  roundTrip(d, [=](Document& x) { return x.removeAllPieces(p); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, a)) == 1);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, free)) == 0);
  // an unstamped piece's count can be set
  roundTrip(d, [=](Document& x) { return x.setPieceCount(p, free, 1, 2); });

  // the colours are the user's, as on every grid (the original kept the
  // start marks in them and hid the colour group): added, painted on a
  // tray cell and deleted, the marks stay as they were
  CHECK(d.colourIds().empty());
  const std::string before = marks(d, tray);
  const Id red = roundTrip(d, [](Document& x) { return x.addColour(Rgb{255, 0, 0}); }).value();
  roundTrip(d, [=](Document& x) { return x.editCells(tray, CellTool::Paint, red, 0, {{0, 0, 0}}); });
  CHECK(d.puzzle().getShape(index(d, tray)).getColor(0, 0, 0) == 1);
  CHECK(marks(d, tray) == before);
  roundTrip(d, [=](Document& x) { return x.deleteColour(red); });
  CHECK(d.puzzle().getShape(index(d, tray)).getColor(0, 0, 0) == 0);
  CHECK(marks(d, tray) == before);
  CHECK(sliding::startGoalError(d.puzzle(), index(d, tray)).empty() ==
        sliding::startGoalError(d.puzzle(), index(d, tray)).empty());
}

TEST_CASE("sliding document: the user makes the problems and chooses their boards",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id b1 = d.addStartGoal({4, 2, 1}).value();
  const Id b2 = d.addStartGoal({4, 2, 1}).value();
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id c = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.problemIds().empty());

  // a new problem has no result, and the colour rules every grid starts
  // with (none without colours); a piece is no result, a board is
  const Id p = roundTrip(d, [](Document& x) { return x.createProblem(); }).value();
  CHECK_FALSE(d.puzzle().getProblem(0).resultValid());
  CHECK(d.puzzle().getProblem(0).getColorConstraints().empty());
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setResult(p, a); });
  // stamped before any problem is on the board: one copy each
  REQUIRE(d.placeStamp(b1, a, 0, 0, 0, false).value());
  REQUIRE(d.placeStamp(b1, a, 3, 1, 0, true).value());
  // chosen: the pieces stamped on it come in, and the board is valid for it
  roundTrip(d, [=](Document& x) { return x.setResult(p, b1); });
  CHECK(d.puzzle().getProblem(0).getResultId() == index(d, b1));
  CHECK(d.puzzle().getProblem(0).getShapeMinimum(index(d, a)) == 1);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, b2)) == 0);
  CHECK(sliding::startGoalError(d.puzzle(), index(d, b1), &d.puzzle().getProblem(0)).empty());
  // its name is its own: renaming it leaves the board, and the board's name the problem
  roundTrip(d, [=](Document& x) { return x.renameProblem(p, "Easy"); });
  CHECK(d.puzzle().getShape(index(d, b1)).getName() == "sg:1:");
  roundTrip(d, [=](Document& x) { return x.renameShape(b1, "Klotski"); });
  CHECK(d.puzzle().getProblem(0).getName() == "Easy");
  // another board: the other's stamps stay as parts, the new one's come in
  REQUIRE(d.placeStamp(b2, c, 1, 0, 0, false).value());
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, c)) == 0);
  roundTrip(d, [=](Document& x) { return x.setResult(p, b2); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, c)) == 1);
  roundTrip(d, [=](Document& x) { return x.setResult(p, b1); });

  // two problems may share a board; the copies a stamp may have are the
  // problem's named
  const Id q = roundTrip(d, [=](Document& x) { return x.duplicateProblem(p); }).value();
  CHECK(d.puzzle().getProblem(1).getResultId() == index(d, b1));
  roundTrip(d, [=](Document& x) { return x.setPieceCount(q, a, 2, 2); });
  // in p, a has one copy: the stamp moves; in q a second copy is placed
  CHECK(roundTrip(d, [=](Document& x) {
          return x.placeStamp(b1, a, 1, 0, 0, false, q);
        }).value());
  CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, b1)), 0, 0, 0, false) == index(d, a));
  CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, b1)), 1, 0, 0, false) == index(d, a));
  // p, sharing the board, keeps a copy for each stamp
  CHECK(d.puzzle().getProblem(0).getShapeMinimum(index(d, a)) == 2);
  // the validity is the problem's: q has two copies of a, one at a goal
  CHECK(sliding::startGoalError(d.puzzle(), index(d, b1), &d.puzzle().getProblem(1)) ==
        sliding::startGoalError(d.puzzle(), index(d, b1), &d.puzzle().getProblem(0)));
  // a problem not on the board is refused
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(b2, c, 2, 0, 0, false, q); });

  // cleared: no result, the parts stay
  roundTrip(d, [=](Document& x) { return x.clearResult(q); });
  CHECK_FALSE(d.puzzle().getProblem(1).resultValid());
  CHECK(d.puzzle().getProblem(1).getShapeMaximum(index(d, a)) == 2);
  // a board deleted: the problems on it stay, without a result
  roundTrip(d, [=](Document& x) { return x.deleteShape(b1); });
  REQUIRE(d.problemIds().size() == 2);
  CHECK_FALSE(d.puzzle().getProblem(0).resultValid());
  // and problems are deleted as on other grids
  roundTrip(d, [=](Document& x) { return x.deleteProblem(q); });
  CHECK(d.problemIds().size() == 1);
}

TEST_CASE("sliding document: shapes may have layers, pieces keep plain names",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 3, 1}).value();
  const Id a = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});

  // the original locked Z (ToolTab_0::lockZ); here sliding shapes may have
  // layers, so the tool tab is the bricks'
  const std::vector<ShapeTaskInfo> set = taskSet(GridKind::Sliding);
  CHECK(set.size() == taskSet(GridKind::Bricks).size());
  CHECK(taskSet(GridKind::Bricks).size() == taskSet(GridKind::Stacking).size());

  // Z grows as X and Y do, alone or for all shapes
  CHECK(roundTrip(d, [=](Document& x) { return x.resizeShape(a, 2, 1, 2, false); }).has_value());
  CHECK(d.puzzle().getShape(index(d, a)).getZ() == 2);
  CHECK(roundTrip(d, [=](Document& x) { return x.resizeShape(tray, 5, 4, 3, true); }).has_value());
  CHECK(d.puzzle().getShape(index(d, tray)).getZ() == 3);
  CHECK(d.puzzle().getShape(index(d, a)).getZ() == 4);

  // a piece named "sg:…" would turn into a start/goal shape at the next sync
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.renameShape(a, "sg:2:x"); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.renameShape(a, "__sggoal:1"); });
  CHECK(roundTrip(d, [=](Document& x) { return x.renameShape(a, "a sg: piece"); }).has_value());
  // a start/goal shape's name is the user's part: anything goes there
  CHECK(roundTrip(d, [=](Document& x) { return x.renameShape(tray, "sg:odd"); }).has_value());
  CHECK(sliding::startGoalNumber(&d.puzzle().getShape(index(d, tray))) == 1);
  CHECK(sliding::startGoalUserName(&d.puzzle().getShape(index(d, tray))) == "sg:odd");

  // other grids are as they were
  Document b(GridKind::Bricks);
  const Id s = b.createShape(std::array<unsigned int, 3>{2, 2, 2}).value();
  CHECK(b.resizeShape(s, 2, 2, 3, false).has_value());
  CHECK(b.renameShape(s, "sg:1:").has_value());
}

TEST_CASE("sliding document: a piece moved past the 255th place loses its marks, not the puzzle",
          "[app][sliding]") {
  // a mark is a byte (the original's was a cell's colour, six bits, and its
  // noteShapeSwap overran them when a stamped piece crossed into the 64th
  // place); here the marks come off at the 256th, with a notice, and the
  // puzzle stays whole
  Document d(GridKind::Sliding);
  const Id tray = board(d, {4, 3, 1});
  std::vector<Id> pieces;
  for (int k = 0; k < 256; k++) {
    pieces.push_back(piece(d, 1, 1, {{0, 0, 0}}));
  }
  // the 255th shape (mark 255) is the last that can be stamped
  const Id last = pieces[253];
  REQUIRE(index(d, last) == 254);
  REQUIRE(d.placeStamp(tray, last, 0, 0, 0, false).value());
  REQUIRE(d.placeStamp(tray, last, 2, 0, 0, true).value());
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(tray, pieces[254], 1, 0, 0, false); });
  const unsigned int it = index(d, tray);
  const auto cleared = [&] {
    const Voxel& t = d.puzzle().getShape(it);
    return sliding::stampAt(&t, 0, 0, 0, false) == static_cast<unsigned int>(-1) &&
           sliding::stampAt(&t, 2, 0, 0, true) == static_cast<unsigned int>(-1);
  };

  // moved up past the place: its start and goal go, the problem follows
  std::string notice;
  roundTrip(d, [&](Document& x) {
    const Result<void> r = x.moveShape(last, 255);
    notice = x.lastChange().notice;
    return r;
  });
  CHECK(index(d, last) == 255);
  CHECK(cleared());
  CHECK(notice ==
        "Only the first 255 shapes can have a start or goal: S255 moved past the 255th place and "
        "its start and goal were removed from B1.");
  CHECK_FALSE(sliding::shapeIsRequired(d.puzzle(), d.puzzle().getProblem(0), 255));

  // back in the 255th place and stamped again, another shape moved down
  // across it takes it past the place too
  REQUIRE(d.moveShape(last, 254).has_value());
  REQUIRE(d.placeStamp(tray, last, 0, 0, 0, false).value());
  roundTrip(d, [&](Document& x) {
    const Result<void> r = x.moveShape(pieces[255], 1);
    notice = x.lastChange().notice;
    return r;
  });
  CHECK(index(d, last) == 255);
  CHECK(cleared());
  CHECK(notice.starts_with("Only the first 255 shapes can have a start or goal: S255 moved"));
  // a move that crosses no stamped piece says nothing
  REQUIRE(d.moveShape(pieces[0], 5).has_value());
  CHECK(d.lastChange().notice.empty());
}

TEST_CASE("sliding document: Solve checks the start/goal shape and sets up the locks",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = board(d, {3, 1, 1});
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, 0, false).value());
  const Id p = d.problemIds()[0];
  SolveOptions o;
  o.disassemble = true;

  // a start without a goal: the original's message
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.solveStart(p, o); });
  REQUIRE(d.placeStamp(tray, a, 2, 0, 0, true).value());

  // the holes not set (an older file): set up by the solve, with the change
  // told; the pieces need no colours
  Puzzle q = d.puzzle();
  q.getProblem(0).setMaxHoles(9);
  Result<Document> e = Document::openBytes(io::writePuzzle(q));
  REQUIRE(e.has_value());
  const Id ep = e->problemIds()[0];
  const std::uint64_t rev = e->revision();
  REQUIRE(e->solveStart(ep, o).has_value());
  CHECK(e->revision() > rev);
  CHECK((e->lastChange().what & kChangeProblems) != 0);
  REQUIRE(e->solveWait(ep).has_value());
  (void)e->poll();
  CHECK(sliding::pieceColor(e->puzzle(), 1) == 0);
  REQUIRE(e->puzzle().getProblem(0).getNumberOfSavedSolutions() == 1);
  CHECK(e->puzzle().getProblem(0).getSavedSolution(0).getDisassembly()->getMoves() == 1);

  // a sliding puzzle has no take-apart to analyse (the original's message)
  const Result<unsigned int> an = d.analyseStart(p, AnalyseOptions{});
  REQUIRE_FALSE(an.has_value());
  CHECK(an.error().code == Errc::Unsupported);

  // its placements can be explored, as the solve makes them
  const Result<AssemblyExplorer> ex =
      d.exploreAssembly(p, SolverType::Classic, MatrixOptions{}, true);
  REQUIRE(ex.has_value());
  REQUIRE(ex->placementsSupported());
  CHECK(ex->placements(0).size() == 1);
}

TEST_CASE("sliding document: opening a file keeps deeper shapes", "[app][sliding]") {
  // the original made them flat on load; here they keep their layers
  Puzzle p = test::openFixture("test_sliding_solver.xmpuzzle").puzzle();
  p.getShape(1).resize(2, 2, 3, 0);
  p.getShape(1).setState(0, 0, 2, Voxel::kFilled);
  Result<Document> d = Document::openBytes(io::writePuzzle(p));
  REQUIRE(d.has_value());
  CHECK(d->puzzle().getShape(1).getZ() == 3);
  CHECK(d->openNotice().empty());
  CHECK_FALSE(d->dirty());
}

TEST_CASE("sliding document: the boards stay on top, numbered by place",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id b1 = d.addStartGoal({4, 3, 1}).value();
  const Id p1 = piece(d, 1, 1, {{0, 0, 0}});
  const Id b2 = d.addStartGoal({4, 3, 1}).value();
  const Id p2 = piece(d, 1, 1, {{0, 0, 0}});
  const Id b3 = roundTrip(d, [=](Document& x) { return x.duplicateShape(b1); }).value();
  // a new board goes below the boards, above the pieces
  CHECK(index(d, b1) == 0);
  CHECK(index(d, b2) == 1);
  CHECK(index(d, b3) == 2);
  CHECK(index(d, p1) == 3);
  CHECK(index(d, p2) == 4);
  // the labels: B and S by place among their kind
  CHECK(d.shapeLabelOf(index(d, b3)) == "B3");
  CHECK(d.shapeLabelOf(index(d, p1)) == "S1");
  CHECK(d.shapeLabelOf(index(d, p2)) == "S2");
  CHECK(Document(GridKind::Bricks).shapeLabelOf(0) == "S1");

  // a board deleted: the ones after it are numbered one lower; no problem comes or goes
  roundTrip(d, [=](Document& x) { return x.deleteShape(b1); });
  CHECK(sliding::startGoalNumber(&d.puzzle().getShape(index(d, b2))) == 1);
  CHECK(sliding::startGoalNumber(&d.puzzle().getShape(index(d, b3))) == 2);
  CHECK(d.puzzle().getNumberOfProblems() == 0);

  // a board moves among the boards only, a piece among the pieces
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.moveShape(b2, 2); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.moveShape(p1, 1); });
  roundTrip(d, [=](Document& x) { return x.moveShape(b3, 0); });
  CHECK(sliding::startGoalNumber(&d.puzzle().getShape(index(d, b3))) == 1);
  CHECK(sliding::startGoalNumber(&d.puzzle().getShape(index(d, b2))) == 2);

  // the name shown without one keeps none
  roundTrip(d, [=](Document& x) { return x.renameShape(b2, "Level two"); });
  CHECK(d.puzzle().getShape(index(d, b2)).getName() == "sg:2:Level two");
  roundTrip(d, [=](Document& x) { return x.renameShape(b2, "Board 2"); });
  CHECK(d.puzzle().getShape(index(d, b2)).getName() == "sg:2:");

  // a board is no piece of a problem, on a board or not yet
  const Id pb2 = d.createProblem().value();
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.addPiece(pb2, b3); });
  roundTrip(d, [=](Document& x) { return x.addOneOfEach(pb2); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, b3)) == 0);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, p1)) == 1);
  roundTrip(d, [=](Document& x) { return x.setResult(pb2, b2); });
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, b3)) == 0);
  CHECK(d.puzzle().getProblem(0).getShapeMaximum(index(d, b2)) == 0);
}

TEST_CASE("sliding document: copies of a piece have a start and a goal each", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = board(d, {4, 2, 1});
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id p = d.problemIds()[0];
  REQUIRE(d.placeStamp(tray, a, 0, 0, 0, false).value());
  // one copy moves
  REQUIRE(d.placeStamp(tray, a, 1, 0, 0, false).value());
  CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, tray)), 0, 0, 0, false) ==
        static_cast<unsigned int>(-1));
  // with three, each click is one more copy's, until all three are placed
  roundTrip(d, [=](Document& x) { return x.setPieceCount(p, a, 3, 3); });
  REQUIRE(d.placeStamp(tray, a, 0, 0, 0, false).value());
  REQUIRE(d.placeStamp(tray, a, 2, 0, 0, false).value());
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(tray, a, 3, 0, 0, false); });
  // fewer copies than are placed is refused, more are fine
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setPieceCount(p, a, 2, 2); });
  roundTrip(d, [=](Document& x) { return x.setPieceCount(p, a, 4, 4); });
  // a click on a copy takes it off, the right click the copy under it
  CHECK_FALSE(d.placeStamp(tray, a, 1, 0, 0, false).value());
  CHECK(d.clearStampAt(tray, 2, 0, 0, false).value());
  const Voxel& t = d.puzzle().getShape(index(d, tray));
  CHECK(sliding::stampAt(&t, 0, 0, 0, false) == index(d, a));
  CHECK(sliding::stampAt(&t, 1, 0, 0, false) == static_cast<unsigned int>(-1));
  CHECK(sliding::stampAt(&t, 2, 0, 0, false) == static_cast<unsigned int>(-1));
  roundTrip(d, [=](Document& x) { return x.setPieceCount(p, a, 1, 1); });
}
