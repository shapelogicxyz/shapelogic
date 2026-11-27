// SPDX-License-Identifier: GPL-3.0-or-later
// A sliding board's start and goal stamps (model/sliding.h): a whole piece
// placed as a start or a goal, copies one by one, a tray of layers, a piece
// that changes shape (cleanup item 21.2 moved these cases here from
// solve/test_sliding.cpp).
#include "../solve/sliding_test_helpers.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"
#include "shapelogic/model/sliding.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace sl;
using namespace sl::sliding;
using namespace sl::test;

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
