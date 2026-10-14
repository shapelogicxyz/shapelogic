// SPDX-License-Identifier: GPL-3.0-or-later
// The shape tools (the tool tabs' tasks per grid, the cell tools) and the
// queries (fit, status line, the status window's table, the problem kind).
// New: the original had no tests for its GUI; the fit line and the status
// table are also compared with the original's in test_app_dump.cpp.
#include "../grid/grid_test_helpers.h"
#include "../model/model_test_helpers.h"

#include "shapelogic/app/queries.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>

using namespace bt;
using namespace bt::app;

namespace {

std::unique_ptr<Voxel> cube(unsigned int size) {
  return test::makeVoxel(GridType(GridKind::Bricks), size, size, size);
}

}  // namespace

TEST_CASE("shape tools: the task sets of the grids", "[app][tools]") {
  const auto has = [](GridKind g, ShapeTask t) { return taskOffered(g, t); };
  // the cube grid: everything but the extra nudges of prisms and spheres
  CHECK(has(GridKind::Bricks, ShapeTask::Minimize));
  CHECK(has(GridKind::Bricks, ShapeTask::GridScale));
  CHECK_FALSE(has(GridKind::Bricks, ShapeTask::Nudge27));
  // prisms: 180 degree turns about x and y, 60 about z; no minimize
  CHECK(has(GridKind::TriangularPrism, ShapeTask::Nudge27));
  CHECK_FALSE(has(GridKind::TriangularPrism, ShapeTask::Rotate7));
  CHECK_FALSE(has(GridKind::TriangularPrism, ShapeTask::Minimize));
  // spheres: twelve nudges, no scaling
  CHECK(has(GridKind::Spheres, ShapeTask::Nudge32));
  CHECK_FALSE(has(GridKind::Spheres, ShapeTask::Double));
  CHECK_FALSE(has(GridKind::Spheres, ShapeTask::GridScale));
  CHECK_FALSE(has(GridKind::TetraOcta, ShapeTask::GridScale));
  CHECK(has(GridKind::Rhombic, ShapeTask::GridScale));
  for (const GridKind g : test::kGeometryGrids) {
    std::set<ShapeTask> seen;
    for (const ShapeTaskInfo& t : taskSet(g)) {
      CHECK(seen.insert(t.task).second);
      CHECK_FALSE(t.label.empty());
    }
  }
}

TEST_CASE("shape tools: nudges, turns, centre and origin keep the shape", "[app][tools]") {
  std::unique_ptr<Voxel> v = cube(5);
  v->setState(0, 0, 0, Voxel::kFilled);
  v->setState(1, 0, 0, Voxel::kFilled);
  v->setState(1, 1, 0, Voxel::kFilled);

  // four quarter turns about z give the shape back (the space resizes)
  std::unique_ptr<Voxel> t = v->clone();
  for (int i = 0; i < 4; i++) {
    REQUIRE(applyTask(*t, ShapeTask::Rotate10).has_value());
  }
  CHECK(t->identicalInBB(*v));

  // centre: an odd gap gets one more cell first (the original's rule)
  REQUIRE(applyTask(*v, ShapeTask::Centre).has_value());
  CHECK(v->countState(Voxel::kFilled) == 3);
  CHECK(v->getX() == 6);
  CHECK(v->boundX1() == 2);
  REQUIRE(applyTask(*v, ShapeTask::ToOrigin).has_value());
  CHECK(v->boundX1() == 0);
  CHECK(v->boundY1() == 0);
  CHECK(v->countState(Voxel::kFilled) == 3);

  // a nudge out of the grid is refused and changes nothing
  const std::unique_ptr<Voxel> before = v->clone();
  const Result<TaskOutcome> r = applyTask(*v, ShapeTask::Nudge1);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::InvalidArgument);
  CHECK(*v == *before);
  const Result<TaskOutcome> up = applyTask(*v, ShapeTask::Nudge0);
  REQUIRE(up.has_value());
  CHECK(up->changed);
  CHECK(up->cellsChanged == 4);
  CHECK(v->isFilled(1, 0, 0));
}

TEST_CASE("shape tools: prune, scale, minimize, fill holes and the surface", "[app][tools]") {
  std::unique_ptr<Voxel> v = cube(6);
  for (unsigned int x = 1; x < 4; x++) {
    for (unsigned int y = 1; y < 4; y++) {
      for (unsigned int z = 1; z < 4; z++) {
        v->setState(x, y, z, Voxel::kFilled);
      }
    }
  }
  v->setState(2, 2, 2, Voxel::kEmpty);

  // the hole inside: filled
  const Result<TaskOutcome> holes = applyTask(*v, ShapeTask::FillHoles);
  REQUIRE(holes.has_value());
  CHECK(holes->cellsChanged == 1);
  CHECK(v->isFilled(2, 2, 2));

  // the surface made variable: all but the centre
  const Result<TaskOutcome> outer = applyTask(*v, ShapeTask::OutsideVariable);
  REQUIRE(outer.has_value());
  CHECK(outer->cellsChanged == 26);
  CHECK(v->isFilled(2, 2, 2));

  REQUIRE(applyTask(*v, ShapeTask::Prune).has_value());
  CHECK(v->getX() == 3);
  REQUIRE(applyTask(*v, ShapeTask::Double).has_value());
  CHECK(v->getX() == 6);
  CHECK(v->countState(Voxel::kFilled) == 8);
  minimizeShapes({v.get()});
  CHECK(v->getX() == 3);
  CHECK(v->countState(Voxel::kFilled) == 1);

  // minimize goes by the shapes together: one that cannot shrink holds all
  std::unique_ptr<Voxel> a = cube(2);
  std::unique_ptr<Voxel> b = cube(2);
  for (unsigned int i = 0; i < 8; i++) {
    a->setState(i, Voxel::kFilled);
  }
  b->setState(0, Voxel::kFilled);
  minimizeShapes({a.get(), b.get()});
  CHECK(a->getX() == 2);
}

TEST_CASE("shape tools: the cell tools", "[app][tools]") {
  std::unique_ptr<Voxel> v = cube(4);
  // mirror in x and y: four cells
  CHECK(applyCellTool(*v, CellTool::Fixed, 0, kMirrorX | kMirrorY, 0, 1, 0) == 4);
  CHECK(v->isFilled(3, 1, 0));
  CHECK(v->isFilled(0, 2, 0));
  CHECK(v->isFilled(3, 2, 0));
  // again: nothing changes
  CHECK(applyCellTool(*v, CellTool::Fixed, 0, kMirrorX | kMirrorY, 0, 1, 0) == 0);
  // a span along x through a filled cell: three more
  CHECK(applyCellTool(*v, CellTool::Variable, 2, kSpanX, 0, 0, 3) == 4);
  CHECK(v->isVariable(2, 0, 3));
  CHECK(v->getColor(2, 0, 3) == 2);
  // outside the space: nothing
  CHECK(applyCellTool(*v, CellTool::Fixed, 0, 0, 7, 0, 0) == 0);
  CHECK(applyCellTool(*v, CellTool::Fixed, 0, 0, -1, 0, 0) == 0);
  // a cell the grid does not have (spheres: odd coordinate sums)
  std::unique_ptr<Voxel> s = test::makeVoxel(GridType(GridKind::Spheres), 3, 3, 3);
  CHECK(applyCellTool(*s, CellTool::Fixed, 0, 0, 1, 0, 0) == 0);
  CHECK(applyCellTool(*s, CellTool::Fixed, 0, 0, 1, 1, 0) == 1);

  // editCells resets the hotspot when it changed anything
  std::unique_ptr<Voxel> h = test::makeVoxel(GridType(GridKind::Spheres), 3, 3, 3);
  h->setHotspot(0, 0, 0);
  CHECK(editCells(*h, CellTool::Fixed, 0, 0, {{1, 1, 0}}) == 1);
  CHECK(h->hasInitialHotspot());
  CHECK(h->getHx() == 1);
}

TEST_CASE("queries: the fit of a problem", "[app][queries]") {
  Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const FitStats f = problemFit(p, 0);
  CHECK(f.verdict == FitVerdict::Fits);
  CHECK(f.cellsMin == f.cellsMax);
  CHECK(f.piecesMax == p.getProblem(0).getNumberOfPieces());
  CHECK(f.resultMin <= f.resultMax);
  CHECK(f.statusLine ==
        "Problem P1 result can contain " + std::to_string(f.resultMin) + " - " +
            std::to_string(f.resultMax) + " voxels, pieces (n = " + std::to_string(f.piecesMax) +
            ") contain " + std::to_string(f.cellsMax) + " voxels");

  // too many pieces
  Problem& pr = p.getProblem(0);
  const unsigned int s = pr.getShapeIdOfPart(0);
  pr.setShapeMaximum(p, s, pr.getShapeMaximum(s) + 2);
  pr.setShapeMinimum(p, s, pr.getShapeMinimum(s) + 2);
  const FitStats big = problemFit(p, 0);
  CHECK(big.verdict == FitVerdict::TooLarge);
  CHECK(big.holesMax == 0);

  // too few: the minimum at 0 and the maximum too small
  Puzzle q = test::loadFixture("PelikanBurr.xmpuzzle");
  Problem& qr = q.getProblem(0);
  qr.setShapeMaximum(q, qr.getShapeIdOfPart(0), 0);
  const FitStats small = problemFit(q, 0);
  CHECK(small.verdict == FitVerdict::TooSmall);
  CHECK(small.holesMin > 0);

  // no result
  Puzzle e{GridType(GridKind::Bricks)};
  (void)e.addProblem();
  CHECK(problemFit(e, 0).verdict == FitVerdict::NoResult);
  CHECK(problemFit(e, 0).statusLine.empty());
}

TEST_CASE("queries: the status line, the table and the problem kind", "[app][queries]") {
  Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const Voxel& s0 = p.getShape(0);
  CHECK(shapeStatusLine(p, 0) ==
        "Shape S1 has " + std::to_string(s0.countState(Voxel::kFilled) + s0.countState(Voxel::kVariable)) +
            " voxels (" + std::to_string(s0.countState(Voxel::kFilled)) + " fixed, " +
            std::to_string(s0.countState(Voxel::kVariable)) + " variable)");
  const std::vector<ShapeInfo> t = shapeTable(p);
  REQUIRE(t.size() == p.getNumberOfShapes());
  CHECK(t[0].notchable.has_value());
  // the first shape is identical to no earlier one
  CHECK_FALSE(t[0].identicalShape.has_value());

  CHECK(problemKind(p) == ProblemKind::Brick);
  CHECK(problemKind(test::loadFixture("test_sliding_solver.xmpuzzle")) == ProblemKind::Sliding);
  CHECK(problemKind(test::loadFixture("test_stacking_hanoi.xmpuzzle")) == ProblemKind::Stacking);

  // notchable and millable: an L of three cubes is both; a cube with a
  // notch cut in from one side is notchable, one with a hole in the middle
  // of a face is not millable
  std::unique_ptr<Voxel> l = cube(2);
  l->setState(0, 0, 0, Voxel::kFilled);
  l->setState(1, 0, 0, Voxel::kFilled);
  l->setState(0, 1, 0, Voxel::kFilled);
  CHECK(isNotchable(*l));
  CHECK(isMillable(*l));
  std::unique_ptr<Voxel> pit = cube(3);
  for (unsigned int i = 0; i < 27; i++) {
    pit->setState(i, Voxel::kFilled);
  }
  pit->setState(1, 1, 2, Voxel::kEmpty);
  CHECK_FALSE(isMillable(*pit));
}
