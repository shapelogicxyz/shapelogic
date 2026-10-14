// SPDX-License-Identifier: GPL-3.0-or-later
// Sliding puzzles through the document: start/goal shapes and stamps as
// the original GUI made them (its New Start/Goal, the start/goal editor's
// clicks), the problems that follow them, the shape commands that keep the
// marks in step, the buttons it greyed out, the checks of its Solve, the
// flattening of its load, and a sliding solve played back. Every command
// is checked to undo and redo byte for byte.
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

// The marks of a start/goal shape as text: per cell "start/goal".
std::string marks(const Document& d, Id tray) {
  const Voxel& v = d.puzzle().getShape(index(d, tray));
  std::string out;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    out += std::to_string(v.getColor(i)) + "/" + std::to_string(v.getGoalPiece(i)) + " ";
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
                    return x.addStartGoal({4, 5});
                  }).value();
  REQUIRE(d.problemIds().size() == 1);
  CHECK(d.puzzle().getProblem(0).getName() == "START/GOAL 1");
  CHECK(d.puzzle().getShape(index(d, tray)).getName() == "sg:1:");

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
    CHECK(roundTrip(d, [=](Document& z) { return z.placeStamp(tray, s, x, y, false); }).value());
  }
  // a goal without which the shape is not valid yet
  CHECK_FALSE(sliding::startGoalError(d.puzzle(), index(d, tray)).empty());
  CHECK(roundTrip(d, [=](Document& z) { return z.placeStamp(tray, block, 1, 1, true); }).value());
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
    CHECK(gotTray.getColor(i) == wantTray.getColor(i));
    CHECK(gotTray.getGoalPiece(i) == wantTray.getGoalPiece(i));
  }
  // and the pieces are locked to their starts
  for (unsigned int s = 1; s < d.puzzle().getNumberOfShapes(); s++) {
    CHECK(sliding::pieceColor(d.puzzle(), s) == s + 1);
  }

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
  const Id tray = d.addStartGoal({5, 3}).value();
  const Id L = piece(d, 2, 2, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
  const Id U = piece(d, 1, 1, {{0, 0, 0}});

  // placed, then placed again at the same place: taken off
  CHECK(roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 1, 1, false); }).value());
  CHECK_FALSE(
      roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 1, 1, false); }).value());
  CHECK(d.placeStamp(tray, L, 1, 1, false).value());
  // elsewhere: moved
  CHECK(roundTrip(d, [=](Document& x) { return x.placeStamp(tray, L, 0, 2, false); }).value());
  CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, tray)), 0, 2, false) == index(d, L));

  // what does not fit is refused with the reason, nothing changed
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(tray, L, 4, 0, false); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(tray, U, 0, 1, false); });
  // a start/goal shape is no piece, and a piece is no start/goal shape
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(tray, tray, 0, 0, true); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.placeStamp(U, L, 0, 0, true); });

  // the right click: what is under the cell comes off; nothing there:
  // false (the L at (0,2) covers (0,2), (0,1) and (1,1))
  CHECK(roundTrip(d, [=](Document& x) { return x.clearStampAt(tray, 1, 1, false); }).value());
  CHECK_FALSE(d.clearStampAt(tray, 1, 1, false).value());
  CHECK_FALSE(d.clearStampAt(tray, 1, 2, false).value());

  // not on other grids
  Document bricks(GridKind::Bricks);
  refused(bricks, Errc::Unsupported, [](Document& x) { return x.addStartGoal({3, 3}); });
}

TEST_CASE("sliding document: the shape commands keep the marks in step", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 3}).value();
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id b = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, false).value());
  REQUIRE(d.placeStamp(tray, b, 2, 2, false).value());
  REQUIRE(d.placeStamp(tray, b, 0, 2, true).value());
  const unsigned int ia = index(d, a);
  const unsigned int ib = index(d, b);

  SECTION("delete: the marks of the shapes above move down, the shape's own go") {
    roundTrip(d, [=](Document& x) { return x.deleteShape(a); });
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 0, 0, false) == static_cast<unsigned int>(-1));
    // b is a shape lower now, and so is its mark
    CHECK(index(d, b) == ib - 1);
    CHECK(sliding::stampAt(&t, 2, 2, false) == index(d, b));
    CHECK(sliding::stampAt(&t, 0, 2, true) == index(d, b));
    // and the problem keeps b alone
    REQUIRE(d.puzzle().getProblem(0).getNumberOfParts() == 1);
    CHECK(d.puzzle().getProblem(0).getShapeIdOfPart(0) == index(d, b));
  }
  SECTION("move: the marks follow") {
    roundTrip(d, [=](Document& x) { return x.moveShape(b, 0); });
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 2, 2, false) == index(d, b));
    CHECK(sliding::stampAt(&t, 0, 0, false) == index(d, a));
    CHECK(sliding::stampAt(&t, 0, 2, true) == index(d, b));
  }
  SECTION("duplicate a start/goal shape: the next number, and a problem") {
    const Id copy = roundTrip(d, [=](Document& x) { return x.duplicateShape(tray); }).value();
    CHECK(d.puzzle().getShape(index(d, copy)).getName() == "sg:2:");
    REQUIRE(d.problemIds().size() == 2);
    CHECK(d.puzzle().getProblem(1).getName() == "START/GOAL 2");
    CHECK(d.puzzle().getProblem(1).getResultId() == index(d, copy));
    // a copy of a piece is a plain copy
    const Id pc = roundTrip(d, [=](Document& x) { return x.duplicateShape(a); }).value();
    CHECK(d.puzzle().getShape(index(d, pc)).getName().empty());
    CHECK(d.problemIds().size() == 2);
  }
  SECTION("rename a start/goal shape: the user's part") {
    roundTrip(d, [=](Document& x) { return x.renameShape(tray, "Klotski"); });
    CHECK(d.puzzle().getShape(index(d, tray)).getName() == "sg:1:Klotski");
    CHECK(sliding::displayName(&d.puzzle().getShape(index(d, tray))) == "(START/GOAL 1) Klotski");
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
          "S" + std::to_string(ib + 1) + " changed shape: its start and goal were removed from S" +
              std::to_string(index(d, tray) + 1) + ".");
    const Voxel& t = d.puzzle().getShape(index(d, tray));
    CHECK(sliding::stampAt(&t, 2, 2, false) == static_cast<unsigned int>(-1));
    CHECK(sliding::stampAt(&t, 0, 2, true) == static_cast<unsigned int>(-1));
    CHECK(sliding::stampAt(&t, 0, 0, false) == ia);
    // b keeps its part (the original's sync only adds parts), not its lock
    CHECK(d.puzzle().getProblem(0).getShapeMaximum(ib) == 1);
    CHECK(sliding::pieceColor(d.puzzle(), ib) == 0);
    // one step: undone, everything is back
    const test::DocState now = test::state(d);
    REQUIRE(d.undo().has_value());
    CHECK(test::state(d) == was);
    REQUIRE(d.redo().has_value());
    CHECK(test::state(d) == now);
  }
  SECTION("walls on a start/goal shape: the problem follows") {
    // a's start cell made a wall: its mark goes, and with it the lock (the
    // part stays, as the original's sync only adds parts)
    roundTrip(d, [=](Document& x) {
      return x.editCells(tray, CellTool::Erase, 0, 0, {{0, 0, 0}});
    });
    CHECK(sliding::stampAt(&d.puzzle().getShape(index(d, tray)), 0, 0, false) ==
          static_cast<unsigned int>(-1));
    CHECK(sliding::pieceColor(d.puzzle(), ia) == 0);
    CHECK(sliding::pieceColor(d.puzzle(), ib) == ib + 1);
    CHECK(d.puzzle().getProblem(0).getNumberOfParts() == 2);
  }
}

TEST_CASE("sliding document: what the original greyed out is refused", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 3}).value();
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  const Id free = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, false).value());
  const Id p = d.problemIds()[0];

  // the result is the start/goal shape; a stamped piece keeps its count
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setResult(p, free); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.addPiece(p, a); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.removePiece(p, a); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.setPieceCount(p, a, 0, 3); });
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

  // the problem buttons: a sliding puzzle's problems follow its start/goal
  // shapes (a new one would go at the next change, a deleted one come
  // back without the user's counts, a copy double a shape's)
  refused(d, Errc::InvalidArgument, [](Document& x) { return x.createProblem(); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.duplicateProblem(p); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.deleteProblem(p); });
  // the colour group was hidden: the cell colours are the start marks and
  // the pieces' locks, which deleting a colour would renumber
  REQUIRE_FALSE(d.colourIds().empty());
  refused(d, Errc::InvalidArgument,
          [](Document& x) { return x.deleteColour(x.colourIds()[0]); });
  const Puzzle before = d.puzzle();
  CHECK(roundTrip(d, [](Document& x) { return x.addColour(Rgb{1, 2, 3}); }).has_value());
  CHECK(sliding::startGoalError(d.puzzle(), index(d, tray)) ==
        sliding::startGoalError(before, index(d, tray)));
}

TEST_CASE("sliding document: shapes stay one layer deep, pieces keep plain names",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 3}).value();
  const Id a = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});

  // the tool tab's buttons ToolTab_0::lockZ greyed out are not offered, nor done
  const std::vector<ShapeTaskInfo> set = taskSet(GridKind::Sliding);
  const auto offered = [&set](ShapeTask t) {
    return std::any_of(set.begin(), set.end(), [t](const ShapeTaskInfo& i) { return i.task == t; });
  };
  for (const ShapeTask t : {ShapeTask::Nudge4, ShapeTask::Nudge5, ShapeTask::Rotate6,
                            ShapeTask::Rotate7, ShapeTask::Rotate8, ShapeTask::Rotate9,
                            ShapeTask::Double, ShapeTask::Triple, ShapeTask::GridScale}) {
    INFO(static_cast<int>(t));
    CHECK_FALSE(offered(t));
    refused(d, Errc::Unsupported, [=](Document& x) { return x.shapeTask(a, t, false); });
  }
  // what keeps a shape flat stays (the bricks' set less those)
  for (const ShapeTask t : {ShapeTask::Nudge0, ShapeTask::Nudge3, ShapeTask::Rotate10,
                            ShapeTask::Rotate11, ShapeTask::Minimize, ShapeTask::Prune}) {
    CHECK(offered(t));
  }
  CHECK(set.size() + 9 == taskSet(GridKind::Bricks).size());
  CHECK(taskSet(GridKind::Bricks).size() == taskSet(GridKind::Stacking).size());
  CHECK(roundTrip(d, [=](Document& x) { return x.shapeTask(a, ShapeTask::Rotate10, false); })
            .has_value());
  CHECK(d.puzzle().getShape(index(d, a)).getZ() == 1);

  // the Z size is locked; X and Y are not, alone or for all shapes
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.resizeShape(a, 3, 3, 2, false); });
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.resizeShape(tray, 5, 3, 2, true); });
  CHECK(roundTrip(d, [=](Document& x) { return x.resizeShape(tray, 5, 4, 1, true); }).has_value());
  for (unsigned int s = 0; s < d.puzzle().getNumberOfShapes(); s++) {
    CHECK(d.puzzle().getShape(s).getZ() == 1);
  }

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

TEST_CASE("sliding document: a piece moved past the 63rd place loses its marks, not the puzzle",
          "[app][sliding]") {
  // a mark is a cell's colour, six bits: the original's noteShapeSwap
  // overran them (an assertion) when a stamped piece crossed into the
  // 64th place; here the marks come off, with a notice, and the puzzle
  // stays whole
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({4, 3}).value();
  std::vector<Id> pieces;
  for (int k = 0; k < 64; k++) {
    pieces.push_back(piece(d, 1, 1, {{0, 0, 0}}));
  }
  // the 63rd shape (mark 63) is the last that can be stamped
  const Id last = pieces[61];
  REQUIRE(index(d, last) == 62);
  REQUIRE(d.placeStamp(tray, last, 0, 0, false).value());
  REQUIRE(d.placeStamp(tray, last, 2, 0, true).value());
  refused(d, Errc::InvalidArgument,
          [=](Document& x) { return x.placeStamp(tray, pieces[62], 1, 0, false); });
  const unsigned int it = index(d, tray);
  const auto cleared = [&] {
    const Voxel& t = d.puzzle().getShape(it);
    return sliding::stampAt(&t, 0, 0, false) == static_cast<unsigned int>(-1) &&
           sliding::stampAt(&t, 2, 0, true) == static_cast<unsigned int>(-1) &&
           sliding::pieceColor(d.puzzle(), index(d, last)) == 0;
  };

  // moved up past the place: its start and goal go, the problem follows
  std::string notice;
  roundTrip(d, [&](Document& x) {
    const Result<void> r = x.moveShape(last, 63);
    notice = x.lastChange().notice;
    return r;
  });
  CHECK(index(d, last) == 63);
  CHECK(cleared());
  CHECK(notice ==
        "Only the first 63 shapes can have a start or goal: S64 moved past the 63rd place and "
        "its start and goal were removed from S1.");
  CHECK_FALSE(sliding::shapeIsRequired(d.puzzle(), d.puzzle().getProblem(0), 63));

  // back in the 63rd place and stamped again, another shape moved down
  // across it takes it past the place too
  REQUIRE(d.moveShape(last, 62).has_value());
  REQUIRE(d.placeStamp(tray, last, 0, 0, false).value());
  roundTrip(d, [&](Document& x) {
    const Result<void> r = x.moveShape(pieces[63], 1);
    notice = x.lastChange().notice;
    return r;
  });
  CHECK(index(d, last) == 63);
  CHECK(cleared());
  CHECK(notice.starts_with("Only the first 63 shapes can have a start or goal: S64 moved"));
  // a move that crosses no stamped piece says nothing
  REQUIRE(d.moveShape(pieces[0], 5).has_value());
  CHECK(d.lastChange().notice.empty());
}

TEST_CASE("sliding document: Solve checks the start/goal shape and sets up the locks",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal({3, 1}).value();
  const Id a = piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(d.placeStamp(tray, a, 0, 0, false).value());
  const Id p = d.problemIds()[0];
  SolveOptions o;
  o.disassemble = true;

  // a start without a goal: the original's message
  refused(d, Errc::InvalidArgument, [=](Document& x) { return x.solveStart(p, o); });
  REQUIRE(d.placeStamp(tray, a, 2, 0, true).value());

  // locks lost (an older file): set up again by the solve, with the change told
  Puzzle q = d.puzzle();
  q.getShape(index(d, a)).setColor(0, 0, 0, 0);
  Result<Document> e = Document::openBytes(io::writePuzzle(q));
  REQUIRE(e.has_value());
  const Id ep = e->problemIds()[0];
  const std::uint64_t rev = e->revision();
  REQUIRE(e->solveStart(ep, o).has_value());
  CHECK(e->revision() > rev);
  CHECK((e->lastChange().what & kChangeShapes) != 0);
  REQUIRE(e->solveWait(ep).has_value());
  (void)e->poll();
  CHECK(sliding::pieceColor(e->puzzle(), 1) == 2);
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

TEST_CASE("sliding document: opening a file makes deeper shapes flat", "[app][sliding]") {
  Puzzle p = test::openFixture("test_sliding_solver.xmpuzzle").puzzle();
  p.getShape(1).resize(2, 2, 3, 0);
  p.getShape(1).setState(0, 0, 2, Voxel::kFilled);
  Result<Document> d = Document::openBytes(io::writePuzzle(p));
  REQUIRE(d.has_value());
  CHECK(d->puzzle().getShape(1).getZ() == 1);
  CHECK(d->openNotice() ==
        "Sliding shapes are one layer deep. S2 had more than one layer: only the first layer was "
        "kept. Save the puzzle to keep this change.");
  CHECK(d->dirty());
  // a flat file says nothing
  const Document flat = openFixture("test_sliding_solver.xmpuzzle");
  CHECK(flat.openNotice().empty());
  CHECK_FALSE(flat.dirty());
}
