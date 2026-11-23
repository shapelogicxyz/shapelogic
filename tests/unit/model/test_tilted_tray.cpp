// SPDX-License-Identifier: GPL-3.0-or-later
// The tilted tray's geometry (decision 0045). The expected counts were
// worked out outside this code: those of the open trays by the program
// behind the research page "Tilted Square Geometry", those of the walled
// trays by the placements found here, each counted again with a polygon
// library (cell inside the open squares grown by the allowance). A
// rectangular tray (stage E) is checked against the square tray with the
// rows or columns beyond it walled, which must give the same board.

#include "shapelogic/model/tilted_tray.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace bt;
using tilted::Cell;
using tilted::Layout;
using tilted::Placed;
using tilted::Point;
using tilted::Tray;

namespace {

/// A tray `width` by `height` at slope down/over, without walls.
Tray rectOf(unsigned int width, unsigned int height, unsigned int over, unsigned int down,
            double allowance = 0.0) {
  Tray t;
  t.width = width;
  t.height = height;
  t.over = over;
  t.down = down;
  t.allowance = allowance;
  return t;
}

/// A square tray of side `size` at slope down/over, strict, without walls.
Tray trayOf(unsigned int size, unsigned int over, unsigned int down, double allowance = 0.0) {
  return rectOf(size, size, over, down, allowance);
}

/// The tray with the listed squares (index y * width + x) made walls.
Tray walled(Tray t, const std::vector<unsigned int>& squares) {
  t.walls.assign(static_cast<std::size_t>(t.width) * t.height, 0);
  for (const unsigned int k : squares) {
    t.walls[k] = 1;
  }
  return t;
}

/// The tray at the shift bestShift() gives.
Tray atBest(Tray t) {
  const Placed p = tilted::bestShift(t);
  t.shiftX = p.shiftX;
  t.shiftY = p.shiftY;
  return t;
}

/// The most whole cells over an n by n net of shifts: never more than the
/// best there is, and less only where the best is a single point.
unsigned int searched(Tray t, int n) {
  t.placement = Tray::Placement::Custom;
  unsigned int best = 0;
  for (int a = 0; a < n; a++) {
    for (int b = 0; b < n; b++) {
      t.shiftX = -0.75 + (1.5 * a / n);
      t.shiftY = -0.75 + (1.5 * b / n);
      best = std::max(best, tilted::layout(t).whole);
    }
  }
  return best;
}

/// The square tray that stands for the rectangle `t`: its side the
/// rectangle's longer one, every square beyond the rectangle a wall, the
/// rectangle's own walls kept. The same slope, allowance, placement and
/// shift: the rectangle lies at the square's corner (0, 0).
Tray asWalledSquare(const Tray& t) {
  const unsigned int side = std::max(t.width, t.height);
  Tray sq = t;
  sq.width = side;
  sq.height = side;
  sq.walls.assign(static_cast<std::size_t>(side) * side, 0);
  for (unsigned int y = 0; y < side; y++) {
    for (unsigned int x = 0; x < side; x++) {
      if (x >= t.width || y >= t.height || t.isWall(x, y)) {
        sq.walls[(static_cast<std::size_t>(y) * side) + x] = 1;
      }
    }
  }
  if (std::find(sq.walls.begin(), sq.walls.end(), 1) == sq.walls.end()) {
    sq.walls.clear();
  }
  return sq;
}

/// The whole cells of a tray's board as grid cells (i, j), in order: the
/// board's box begins at a grid cell that the tray's corner (0, 0) tells
/// (it lies at R^T(-shift) in the grid and at corners[0] in the box).
std::vector<std::pair<long, long>> wholeCells(const Tray& t) {
  const Layout lay = tilted::layout(t);
  const double h = std::hypot(static_cast<double>(t.over), static_cast<double>(t.down));
  const double c = t.over / h;
  const double s = t.down / h;
  const double gx = (-t.shiftX * c) + (-t.shiftY * s);
  const double gy = (-t.shiftY * c) - (-t.shiftX * s);
  const long i0 = std::lround(gx - lay.corners[0].x);
  const long j0 = std::lround(gy - lay.corners[0].y);
  std::vector<std::pair<long, long>> out;
  for (unsigned int y = 0; y < lay.sizeY; y++) {
    for (unsigned int x = 0; x < lay.sizeX; x++) {
      if (lay.at(x, y) == Cell::Whole) {
        out.emplace_back(i0 + x, j0 + y);
      }
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

const std::vector<unsigned int> kCorners = {0, 7, 56, 63};
const std::vector<unsigned int> kScattered = {0, 1, 8, 27, 28, 36, 62, 63, 23};

}  // namespace

TEST_CASE("tilted tray: settings in range", "[model][tilted]") {
  CHECK(Tray{}.valid());
  CHECK(trayOf(8, 2, 1, 0.05).valid());
  CHECK(trayOf(Tray::kMaxSize, Tray::kMaxSlope, 1, Tray::kMaxAllowance).valid());

  CHECK_FALSE(trayOf(0, 1, 0).valid());
  CHECK_FALSE(trayOf(Tray::kMaxSize + 1, 1, 0).valid());
  // each side has the bounds
  CHECK(rectOf(1, Tray::kMaxSize, 2, 1).valid());
  CHECK(rectOf(Tray::kMaxSize, 1, 2, 1).valid());
  CHECK_FALSE(rectOf(0, 8, 1, 0).valid());
  CHECK_FALSE(rectOf(8, 0, 1, 0).valid());
  CHECK_FALSE(rectOf(Tray::kMaxSize + 1, 8, 1, 0).valid());
  CHECK_FALSE(rectOf(8, Tray::kMaxSize + 1, 1, 0).valid());
  CHECK(trayOf(8, 2, 1).square());
  CHECK_FALSE(rectOf(8, 6, 2, 1).square());
  CHECK_FALSE(trayOf(8, 0, 0).valid());
  // down no more than over, and in lowest terms
  CHECK_FALSE(trayOf(8, 1, 2).valid());
  CHECK_FALSE(trayOf(8, 4, 2).valid());
  CHECK_FALSE(trayOf(8, 2, 0).valid());
  CHECK(trayOf(8, 1, 1).valid());
  CHECK_FALSE(trayOf(8, 2, 1, -0.01).valid());
  CHECK_FALSE(trayOf(8, 2, 1, 0.3).valid());
  CHECK_FALSE(trayOf(8, 2, 1, std::nan("")).valid());

  Tray t = trayOf(8, 2, 1);
  t.walls.assign(63, 0);
  CHECK_FALSE(t.valid());
  t.walls.assign(64, 0);
  CHECK(t.valid());
  // a rectangle's walls are width * height
  Tray r = rectOf(6, 8, 2, 1);
  r.walls.assign(36, 0);
  CHECK_FALSE(r.valid());
  r.walls.assign(64, 0);
  CHECK_FALSE(r.valid());
  r.walls.assign(48, 0);
  CHECK(r.valid());
  t.shiftX = std::numeric_limits<double>::infinity();
  CHECK_FALSE(t.valid());
  t.shiftX = Tray::kMaxShift;
  CHECK(t.valid());
  t.shiftY = -Tray::kMaxShift - 1.0;
  CHECK_FALSE(t.valid());
  // a far shift gives the board of its nearest form
  t.shiftY = 1000.0;
  t.placement = Tray::Placement::Custom;
  const Point nearest = tilted::canonicalShift(t, {t.shiftX, t.shiftY});
  CHECK(std::abs(nearest.x) < 2.0);
  CHECK(std::abs(nearest.y) < 2.0);
  Tray nearTray = t;
  nearTray.shiftX = nearest.x;
  nearTray.shiftY = nearest.y;
  CHECK(tilted::layout(t).cells == tilted::layout(nearTray).cells);

  CHECK(trayOf(8, 1, 1).degrees() == 45.0);
  CHECK(std::abs(trayOf(8, 2, 1).degrees() - 26.56505117707799) < 1e-12);
  CHECK(trayOf(8, 1, 0).degrees() == 0.0);
}

TEST_CASE("tilted tray: the best counts of the research page", "[model][tilted]") {
  struct Row {
    unsigned int size;
    unsigned int over;
    std::array<unsigned int, 5> best;  // allowance 0, 0.01, 0.02, 0.05, 0.1
  };
  const std::array<double, 5> allowances = {0.0, 0.01, 0.02, 0.05, 0.1};
  // trays 4 to 12, 1 down and 1 to 6 over
  const std::vector<Row> rows = {
      {4, 1, {8, 8, 8, 8, 8}},            {4, 2, {8, 8, 10, 10, 10}},
      {4, 3, {9, 9, 9, 9, 10}},           {4, 4, {9, 9, 9, 9, 10}},
      {4, 5, {9, 9, 9, 9, 10}},           {4, 6, {9, 9, 9, 9, 10}},
      {5, 1, {18, 18, 18, 18, 18}},       {5, 2, {17, 17, 17, 17, 17}},
      {5, 3, {15, 15, 15, 17, 17}},       {5, 4, {16, 16, 16, 17, 17}},
      {5, 5, {16, 16, 16, 17, 17}},       {5, 6, {16, 16, 16, 17, 17}},
      {6, 1, {25, 25, 25, 25, 25}},       {6, 2, {25, 25, 25, 25, 25}},
      {6, 3, {24, 26, 26, 26, 26}},       {6, 4, {24, 24, 24, 26, 26}},
      {6, 5, {25, 25, 25, 26, 26}},       {6, 6, {25, 25, 25, 26, 26}},
      {7, 1, {32, 32, 32, 41, 41}},       {7, 2, {34, 34, 34, 34, 40}},
      {7, 3, {37, 37, 37, 37, 37}},       {7, 4, {35, 35, 37, 37, 37}},
      {7, 5, {35, 35, 35, 37, 37}},       {7, 6, {36, 36, 36, 37, 37}},
      {8, 1, {50, 50, 50, 50, 50}},       {8, 2, {45, 45, 45, 52, 52}},
      {8, 3, {49, 49, 49, 49, 49}},       {8, 4, {48, 50, 50, 50, 50}},
      {8, 5, {48, 48, 48, 50, 50}},       {8, 6, {48, 48, 48, 50, 50}},
      {9, 1, {61, 61, 61, 61, 72}},       {9, 2, {65, 65, 65, 65, 65}},
      {9, 3, {64, 64, 64, 64, 68}},       {9, 4, {65, 65, 65, 65, 65}},
      {9, 5, {63, 63, 65, 65, 65}},       {9, 6, {63, 63, 63, 65, 65}},
      {10, 1, {85, 85, 85, 85, 85}},      {10, 2, {80, 80, 80, 80, 80}},
      {10, 3, {79, 79, 79, 79, 85}},      {10, 4, {81, 81, 81, 81, 85}},
      {10, 5, {80, 82, 82, 82, 85}},      {10, 6, {80, 80, 82, 82, 85}},
      {11, 1, {98, 98, 98, 98, 98}},      {11, 2, {97, 97, 97, 97, 106}},
      {11, 3, {97, 97, 97, 103, 103}},    {11, 4, {100, 100, 100, 100, 104}},
      {11, 5, {101, 101, 101, 101, 104}}, {11, 6, {99, 101, 101, 101, 104}},
      {12, 1, {113, 113, 128, 128, 128}}, {12, 2, {116, 116, 116, 125, 125}},
      {12, 3, {116, 124, 124, 124, 124}}, {12, 4, {121, 121, 121, 121, 125}},
      {12, 5, {121, 121, 121, 121, 125}}, {12, 6, {120, 122, 122, 122, 125}},
  };
  for (const Row& row : rows) {
    for (std::size_t k = 0; k < allowances.size(); k++) {
      INFO("tray " << row.size << ", 1 down " << row.over << " over, allowance " << allowances[k]);
      const Tray t = trayOf(row.size, row.over, 1, allowances[k]);
      const Placed p = tilted::bestShift(t);
      CHECK(p.whole == row.best[k]);
      // the board made at that shift has as many
      CHECK(tilted::layout(atBest(t)).whole == row.best[k]);
    }
  }

  // other slopes, the 8 by 8 tray
  struct Slope {
    unsigned int over;
    unsigned int down;
    std::array<unsigned int, 5> best;
  };
  const std::vector<Slope> slopes = {{3, 2, {45, 45, 45, 49, 49}},
                                     {4, 3, {48, 48, 48, 48, 50}},
                                     {5, 2, {49, 49, 49, 49, 51}},
                                     {12, 5, {47, 47, 47, 49, 49}}};
  for (const Slope& sl : slopes) {
    for (std::size_t k = 0; k < allowances.size(); k++) {
      INFO("slope " << sl.down << "/" << sl.over << ", allowance " << allowances[k]);
      CHECK(tilted::bestShift(trayOf(8, sl.over, sl.down, allowances[k])).whole == sl.best[k]);
    }
  }
}

TEST_CASE("tilted tray: a straight tray is the plain board", "[model][tilted]") {
  for (const unsigned int size : {1U, 4U, 8U}) {
    const Tray t = atBest(trayOf(size, 1, 0));
    const Layout lay = tilted::layout(t);
    CHECK(lay.sizeX == size);
    CHECK(lay.sizeY == size);
    CHECK(lay.whole == size * size);
    CHECK(lay.cut == 0);
    CHECK(std::all_of(lay.cells.begin(), lay.cells.end(),
                      [](Cell c) { return c == Cell::Whole; }));
    // the tray's corners are the box's
    CHECK(std::abs(lay.corners[0].x) < 1e-9);
    CHECK(std::abs(lay.corners[0].y) < 1e-9);
    CHECK(std::abs(lay.corners[2].x - size) < 1e-9);
    CHECK(std::abs(lay.corners[2].y - size) < 1e-9);
  }

  // walls are then the board's missing cells, in place
  const Tray w = atBest(walled(trayOf(8, 1, 0), kCorners));
  const Layout lay = tilted::layout(w);
  CHECK(lay.whole == 60);
  CHECK(lay.cut == 4);
  CHECK(lay.at(0, 0) == Cell::Cut);
  CHECK(lay.at(7, 0) == Cell::Cut);
  CHECK(lay.at(0, 7) == Cell::Cut);
  CHECK(lay.at(7, 7) == Cell::Cut);
  CHECK(lay.at(1, 0) == Cell::Whole);
  REQUIRE(lay.walls.size() == 4);
  // the first wall is square (0, 0)
  CHECK(std::abs(lay.walls[0][0].x) < 1e-9);
  CHECK(std::abs(lay.walls[0][2].x - 1.0) < 1e-9);
  CHECK(std::abs(lay.walls[0][2].y - 1.0) < 1e-9);
}

TEST_CASE("tilted tray: the board at its shift", "[model][tilted]") {
  const Tray t = atBest(trayOf(8, 2, 1));
  const Layout lay = tilted::layout(t);
  CHECK(lay.whole == 45);
  CHECK(lay.cells.size() == static_cast<std::size_t>(lay.sizeX) * lay.sizeY);
  CHECK(static_cast<unsigned int>(std::count(lay.cells.begin(), lay.cells.end(), Cell::Whole)) ==
        lay.whole);
  CHECK(static_cast<unsigned int>(std::count(lay.cells.begin(), lay.cells.end(), Cell::Cut)) ==
        lay.cut);
  CHECK(lay.walls.empty());

  // the tray is a square of side 8 in the box's frame, turned by the slope
  for (std::size_t k = 0; k < 4; k++) {
    const Point a = lay.corners[k];
    const Point b = lay.corners[(k + 1) % 4];
    CHECK(std::abs(std::hypot(b.x - a.x, b.y - a.y) - 8.0) < 1e-9);
  }
  const Point edge = {lay.corners[1].x - lay.corners[0].x, lay.corners[1].y - lay.corners[0].y};
  CHECK(std::abs((std::atan2(-edge.y, edge.x) * 180.0 / std::numbers::pi) - t.degrees()) <
        1e-9);

  // every whole cell lies inside it (strict), every row and column of the
  // box holds a cell that reaches into the tray
  const auto inside = [&](double x, double y) {
    for (std::size_t k = 0; k < 4; k++) {
      const Point a = lay.corners[k];
      const Point b = lay.corners[(k + 1) % 4];
      if (((b.x - a.x) * (y - a.y)) - ((b.y - a.y) * (x - a.x)) < -1e-8) {
        return false;
      }
    }
    return true;
  };
  for (unsigned int y = 0; y < lay.sizeY; y++) {
    bool any = false;
    for (unsigned int x = 0; x < lay.sizeX; x++) {
      any = any || lay.at(x, y) != Cell::Outside;
      if (lay.at(x, y) == Cell::Whole) {
        CHECK(inside(x, y));
        CHECK(inside(x + 1.0, y));
        CHECK(inside(x + 1.0, y + 1.0));
        CHECK(inside(x, y + 1.0));
      }
    }
    CHECK(any);
  }
  for (unsigned int x = 0; x < lay.sizeX; x++) {
    bool any = false;
    for (unsigned int y = 0; y < lay.sizeY; y++) {
      any = any || lay.at(x, y) != Cell::Outside;
    }
    CHECK(any);
  }
}

TEST_CASE("tilted tray: the same settings give the same board", "[model][tilted]") {
  for (const unsigned int over : {1U, 2U, 3U, 5U}) {
    const Tray t = walled(trayOf(8, over, 1, 0.05), kCorners);
    const Placed a = tilted::bestShift(t);
    const Placed b = tilted::bestShift(t);
    CHECK(a.shiftX == b.shiftX);
    CHECK(a.shiftY == b.shiftY);
    CHECK(a.whole == b.whole);
    // the shift in the settings is not looked at
    Tray moved = t;
    moved.shiftX = 0.3;
    moved.shiftY = -0.2;
    moved.placement = Tray::Placement::Custom;
    const Placed c = tilted::bestShift(moved);
    CHECK(c.shiftX == a.shiftX);
    CHECK(c.shiftY == a.shiftY);
    CHECK(tilted::layout(atBest(t)).cells == tilted::layout(atBest(moved)).cells);
  }
}

TEST_CASE("tilted tray: a shift moved by grid steps gives the same board", "[model][tilted]") {
  Tray t = walled(trayOf(7, 3, 1, 0.02), {3, 10, 48});
  t.placement = Tray::Placement::Custom;
  t.shiftX = 0.17;
  t.shiftY = -0.41;
  const Layout first = tilted::layout(t);
  const double h = std::sqrt(10.0);
  // one step along the grid's first axis and two back along its second
  Tray stepped = t;
  stepped.shiftX += (3.0 / h) + (2.0 * 1.0 / h);
  stepped.shiftY += (1.0 / h) - (2.0 * 3.0 / h);
  const Layout second = tilted::layout(stepped);
  CHECK(second.sizeX == first.sizeX);
  CHECK(second.sizeY == first.sizeY);
  CHECK(second.cells == first.cells);

  const Point ca = tilted::canonicalShift(t, {t.shiftX, t.shiftY});
  const Point cb = tilted::canonicalShift(stepped, {stepped.shiftX, stepped.shiftY});
  CHECK(std::abs(ca.x - cb.x) < 1e-9);
  CHECK(std::abs(ca.y - cb.y) < 1e-9);
  Tray canon = t;
  canon.shiftX = ca.x;
  canon.shiftY = ca.y;
  CHECK(tilted::layout(canon).cells == first.cells);
  // the best shift comes in that form
  const Placed p = tilted::bestShift(t);
  const Point again = tilted::canonicalShift(t, {p.shiftX, p.shiftY});
  CHECK(std::abs(again.x - p.shiftX) < 1e-9);
  CHECK(std::abs(again.y - p.shiftY) < 1e-9);
}

TEST_CASE("tilted tray: walls stay where they are at every tilt", "[model][tilted]") {
  struct Row {
    double allowance;
    unsigned int over;
    unsigned int corners;    // the four corner squares walled
    unsigned int scattered;  // nine squares walled, some side by side
  };
  const std::vector<Row> rows = {
      {0.00, 1, 44, 37}, {0.00, 2, 43, 32}, {0.00, 3, 45, 34}, {0.00, 4, 44, 33},
      {0.00, 5, 45, 34}, {0.00, 6, 45, 34}, {0.05, 1, 46, 38}, {0.05, 2, 48, 35},
      {0.05, 3, 46, 35}, {0.05, 4, 46, 36}, {0.05, 5, 47, 37}, {0.05, 6, 49, 37},
      {0.10, 1, 46, 39}, {0.10, 2, 48, 38}, {0.10, 3, 46, 37}, {0.10, 4, 47, 38},
      {0.10, 5, 47, 39}, {0.10, 6, 49, 38},
  };
  for (const Row& row : rows) {
    INFO("1 down " << row.over << " over, allowance " << row.allowance);
    const Tray corners = walled(trayOf(8, row.over, 1, row.allowance), kCorners);
    const Tray scattered = walled(trayOf(8, row.over, 1, row.allowance), kScattered);
    CHECK(tilted::bestShift(corners).whole == row.corners);
    CHECK(tilted::bestShift(scattered).whole == row.scattered);
    const Layout lay = tilted::layout(atBest(corners));
    CHECK(lay.whole == row.corners);
    CHECK(lay.walls.size() == 4);
    // no net of shifts does better
    CHECK(searched(corners, 12) <= row.corners);
    CHECK(searched(scattered, 12) <= row.scattered);
  }

  // a wall never adds a cell, wherever the grid sits
  Tray open = trayOf(8, 3, 1, 0.05);
  open.placement = Tray::Placement::Custom;
  for (int k = 0; k < 20; k++) {
    open.shiftX = -0.7 + (0.07 * k);
    open.shiftY = 0.6 - (0.05 * k);
    const Layout a = tilted::layout(open);
    const Layout b = tilted::layout(walled(open, kScattered));
    REQUIRE(a.cells.size() == b.cells.size());
    for (std::size_t c = 0; c < a.cells.size(); c++) {
      CHECK((b.cells[c] != Cell::Whole || a.cells[c] == Cell::Whole));
      CHECK((a.cells[c] == Cell::Outside) == (b.cells[c] == Cell::Outside));
    }
  }
}

TEST_CASE("tilted tray: the allowance at a wall", "[model][tilted]") {
  // A straight 3 by 1 strip would be simplest, but trays are square: a 3 by
  // 3 tray, its middle square a wall, the grid straight and pushed right by
  // d. The cells left of the wall then reach d into it.
  const auto wholeAt = [](double d, double allowance) {
    Tray t = walled(trayOf(3, 1, 0, allowance), {4});
    t.placement = Tray::Placement::Custom;
    t.shiftX = d;
    return tilted::layout(t);
  };
  // flush: the eight squares round the wall
  CHECK(wholeAt(0.0, 0.0).whole == 8);
  // pushed 0.03: strict, the left column's middle cell is cut by the wall,
  // and the right column is cut by the edge
  CHECK(wholeAt(0.03, 0.0).whole == 4);
  // with an allowance of 0.05 all eight are whole again
  CHECK(wholeAt(0.03, 0.05).whole == 8);
  // and at 0.06 they are not
  CHECK(wholeAt(0.06, 0.05).whole == 4);

  // Two walls side by side leave no slit between them: a cell pushed up
  // into the pair is cut although it would fit the gap an allowance on
  // each wall alone would leave.
  Tray pair = walled(trayOf(4, 1, 1, 0.1), {9, 10});
  pair.placement = Tray::Placement::Custom;
  // a diamond whose top corner points at the seam between the walls (the
  // seam is x = 2, the walls' lower edge y = 2): its corner at (2, 2.15)
  // is 0.15 into the walls, more than the allowance
  const double r = std::sqrt(0.5);
  pair.shiftX = 2.0;
  pair.shiftY = 2.15 - (2.0 * r);
  const Layout into = tilted::layout(pair);
  // the cell with corners (2, 2.15 - 2r) .. (2, 2.15): grid cell (0, 0)
  const Point lowest = into.corners[0];
  const auto cellOf = [&](double gx, double gy) {
    // grid cell (0, 0) sits at box cell (-corner offset): the tray corner
    // (0, 0) is at `lowest` in the box, and at grid coordinates
    // R^T(-shift)
    const double cx = (-pair.shiftX * r) + (-pair.shiftY * r);
    const double cy = (-pair.shiftY * r) - (-pair.shiftX * r);
    return into.at(static_cast<unsigned int>(std::lround(gx + lowest.x - cx)),
                   static_cast<unsigned int>(std::lround(gy + lowest.y - cy)));
  };
  CHECK(cellOf(0.0, 0.0) == Cell::Cut);
  // 0.05 into the walls it is whole
  pair.shiftY = 2.05 - (2.0 * r);
  const Layout touching = tilted::layout(pair);
  const Point low2 = touching.corners[0];
  const double cx = (-pair.shiftX * r) + (-pair.shiftY * r);
  const double cy = (-pair.shiftY * r) - (-pair.shiftX * r);
  CHECK(touching.at(static_cast<unsigned int>(std::lround(low2.x - cx)),
                    static_cast<unsigned int>(std::lround(low2.y - cy))) == Cell::Whole);
}

// ---- rectangular trays (stage E) ---------------------------------------------------

TEST_CASE("tilted tray: a straight rectangle is the plain board", "[model][tilted]") {
  const Tray t = atBest(rectOf(6, 4, 1, 0));
  const Layout lay = tilted::layout(t);
  CHECK(lay.sizeX == 6);
  CHECK(lay.sizeY == 4);
  CHECK(lay.whole == 24);
  CHECK(lay.cut == 0);
  CHECK(std::abs(lay.corners[0].x) < 1e-9);
  CHECK(std::abs(lay.corners[0].y) < 1e-9);
  CHECK(std::abs(lay.corners[1].x - 6.0) < 1e-9);
  CHECK(std::abs(lay.corners[1].y) < 1e-9);
  CHECK(std::abs(lay.corners[2].x - 6.0) < 1e-9);
  CHECK(std::abs(lay.corners[2].y - 4.0) < 1e-9);
  CHECK(std::abs(lay.corners[3].x) < 1e-9);
  CHECK(std::abs(lay.corners[3].y - 4.0) < 1e-9);

  // the walls are numbered row by row along the width: square (5, 1) is 11
  const Tray w = atBest(walled(rectOf(6, 4, 1, 0), {11, 18}));
  CHECK(w.isWall(5, 1));
  CHECK(w.isWall(0, 3));
  CHECK_FALSE(w.isWall(1, 1));
  CHECK_FALSE(w.isWall(3, 0));
  const Layout walls = tilted::layout(w);
  CHECK(walls.whole == 22);
  CHECK(walls.at(5, 1) == Cell::Cut);
  CHECK(walls.at(0, 3) == Cell::Cut);
  CHECK(walls.at(4, 1) == Cell::Whole);
  REQUIRE(walls.walls.size() == 2);
  CHECK(std::abs(walls.walls[0][0].x - 5.0) < 1e-9);
  CHECK(std::abs(walls.walls[0][0].y - 1.0) < 1e-9);
  CHECK(std::abs(walls.walls[1][2].x - 1.0) < 1e-9);
  CHECK(std::abs(walls.walls[1][2].y - 4.0) < 1e-9);
}

TEST_CASE("tilted tray: a rectangle's board at its shift", "[model][tilted]") {
  const Tray t = atBest(rectOf(9, 5, 2, 1, 0.0));
  const Layout lay = tilted::layout(t);
  CHECK(lay.whole == tilted::bestShift(t).whole);
  CHECK(static_cast<unsigned int>(std::count(lay.cells.begin(), lay.cells.end(), Cell::Whole)) ==
        lay.whole);
  // the tray is 9 by 5 in the box's frame, turned by the slope
  const auto sideLength = [&](std::size_t a, std::size_t b) {
    return std::hypot(lay.corners[b].x - lay.corners[a].x, lay.corners[b].y - lay.corners[a].y);
  };
  CHECK(std::abs(sideLength(0, 1) - 9.0) < 1e-9);
  CHECK(std::abs(sideLength(1, 2) - 5.0) < 1e-9);
  CHECK(std::abs(sideLength(2, 3) - 9.0) < 1e-9);
  CHECK(std::abs(sideLength(3, 0) - 5.0) < 1e-9);
  const Point edge = {lay.corners[1].x - lay.corners[0].x, lay.corners[1].y - lay.corners[0].y};
  CHECK(std::abs((std::atan2(-edge.y, edge.x) * 180.0 / std::numbers::pi) - t.degrees()) <
        1e-9);
  // every whole cell lies inside the rectangle, and every row and column of
  // the box holds a cell that reaches into it
  const auto inside = [&](double x, double y) {
    for (std::size_t k = 0; k < 4; k++) {
      const Point a = lay.corners[k];
      const Point b = lay.corners[(k + 1) % 4];
      if (((b.x - a.x) * (y - a.y)) - ((b.y - a.y) * (x - a.x)) < -1e-8) {
        return false;
      }
    }
    return true;
  };
  for (unsigned int y = 0; y < lay.sizeY; y++) {
    bool any = false;
    for (unsigned int x = 0; x < lay.sizeX; x++) {
      any = any || lay.at(x, y) != Cell::Outside;
      if (lay.at(x, y) == Cell::Whole) {
        CHECK(inside(x, y));
        CHECK(inside(x + 1.0, y));
        CHECK(inside(x + 1.0, y + 1.0));
        CHECK(inside(x, y + 1.0));
      }
    }
    CHECK(any);
  }
  for (unsigned int x = 0; x < lay.sizeX; x++) {
    bool any = false;
    for (unsigned int y = 0; y < lay.sizeY; y++) {
      any = any || lay.at(x, y) != Cell::Outside;
    }
    CHECK(any);
  }
  // the same settings give the same shift, in its canonical form
  const Placed a = tilted::bestShift(t);
  const Placed b = tilted::bestShift(t);
  CHECK(a.shiftX == b.shiftX);
  CHECK(a.shiftY == b.shiftY);
  const Point again = tilted::canonicalShift(t, {a.shiftX, a.shiftY});
  CHECK(std::abs(again.x - a.shiftX) < 1e-9);
  CHECK(std::abs(again.y - a.shiftY) < 1e-9);
}

TEST_CASE("tilted tray: a rectangle is the square with the rest walled",
          "[model][tilted][slow]") {
  // The oracle of stage E: a wall acts as the edge does, the allowance
  // included, so the 6 by 8 tray is the 8 by 8 tray with two columns
  // walled: the same whole cells at every shift, and the same best count.
  // The square goes through the walls' blocks, the rectangle through the
  // edge test alone.
  for (unsigned int width = 3; width <= 10; width++) {
    for (unsigned int height = 3; height <= 10; height++) {
      for (const unsigned int over : {1U, 2U, 3U, 4U}) {
        for (const double allowance : {0.0, 0.05}) {
          INFO(width << " by " << height << ", 1 down " << over << " over, allowance "
                     << allowance);
          const Tray rect = rectOf(width, height, over, 1, allowance);
          const Tray square = asWalledSquare(rect);
          REQUIRE(square.valid());
          const Placed bestRect = tilted::bestShift(rect);
          const Placed bestSquare = tilted::bestShift(square);
          CHECK(bestRect.whole == bestSquare.whole);

          // cell for cell, where each of the two would place the grid
          for (const Placed& at : {bestRect, bestSquare}) {
            Tray r = rect;
            r.shiftX = at.shiftX;
            r.shiftY = at.shiftY;
            const Tray q = asWalledSquare(r);
            const std::vector<std::pair<long, long>> cells = wholeCells(r);
            CHECK(cells.size() == bestRect.whole);
            CHECK(cells == wholeCells(q));
          }
          // and at shifts that are no one's best
          for (int k = 0; k < 6; k++) {
            Tray r = rect;
            r.placement = Tray::Placement::Custom;
            r.shiftX = -0.61 + (0.23 * k) + (0.011 * width);
            r.shiftY = 0.47 - (0.19 * k) - (0.013 * height);
            const std::vector<std::pair<long, long>> cells = wholeCells(r);
            CHECK(cells == wholeCells(asWalledSquare(r)));
            CHECK(cells.size() <= bestRect.whole);
          }
        }
      }
    }
  }
}

TEST_CASE("tilted tray: a rectangle with walls of its own", "[model][tilted]") {
  // the oracle again, with walls inside the rectangle and on its rim, some
  // side by side: a 6 by 8 tray, index y * 6 + x
  const std::vector<unsigned int> squares = {0, 5, 14, 15, 20, 33, 42, 47};
  for (const unsigned int over : {1U, 2U, 3U, 5U}) {
    for (const double allowance : {0.0, 0.05, 0.1}) {
      INFO("1 down " << over << " over, allowance " << allowance);
      const Tray rect = walled(rectOf(6, 8, over, 1, allowance), squares);
      const Tray square = asWalledSquare(rect);
      const Placed best = tilted::bestShift(rect);
      CHECK(best.whole == tilted::bestShift(square).whole);
      CHECK(tilted::layout(atBest(rect)).whole == best.whole);
      CHECK(tilted::layout(atBest(rect)).walls.size() == squares.size());
      CHECK(wholeCells(atBest(rect)) == wholeCells(asWalledSquare(atBest(rect))));
      // no net of shifts does better, and a wall never adds a cell
      CHECK(searched(rect, 8) <= best.whole);
      CHECK(best.whole <= tilted::bestShift(rectOf(6, 8, over, 1, allowance)).whole);
    }
  }
}

TEST_CASE("tilted tray: a rectangle turned a quarter holds as many cells", "[model][tilted]") {
  // A width by height tray turned a quarter is the height by width tray,
  // and the square grid a quarter turn on is the same grid: the best counts
  // are equal. Nothing of this goes through walls.
  for (const unsigned int over : {1U, 2U, 3U, 4U, 5U}) {
    for (const double allowance : {0.0, 0.02, 0.05}) {
      for (const auto& [width, height] :
           std::vector<std::pair<unsigned int, unsigned int>>{
               {3, 5}, {4, 7}, {6, 8}, {5, 12}, {9, 10}, {1, 6}, {2, 11}}) {
        INFO(width << " by " << height << ", 1 down " << over << " over, allowance "
                   << allowance);
        const unsigned int wide = tilted::bestShift(rectOf(width, height, over, 1, allowance)).whole;
        const unsigned int high = tilted::bestShift(rectOf(height, width, over, 1, allowance)).whole;
        CHECK(wide == high);
        // no net of shifts does better
        CHECK(searched(rectOf(width, height, over, 1, allowance), 10) <= wide);
      }
    }
  }
  // a tray one cell wide holds nothing once the grid is turned
  CHECK(tilted::bestShift(rectOf(1, 6, 2, 1)).whole == 0);
  CHECK(tilted::bestShift(rectOf(1, 6, 1, 0)).whole == 6);
}

TEST_CASE("tilted tray: a new size keeps the walls inside", "[model][tilted]") {
  // an 8 by 8 tray with walls at (0, 0), (7, 0), (2, 5), (7, 7)
  const Tray t = walled(trayOf(8, 2, 1, 0.05), {0, 7, 42, 63});
  // narrower: the walls of the columns that stay, numbered along the new width
  const Tray narrow = tilted::withSize(t, 6, 8);
  CHECK(narrow.valid());
  CHECK(narrow.width == 6);
  CHECK(narrow.height == 8);
  CHECK(narrow.walls.size() == 48);
  CHECK(narrow.isWall(0, 0));
  CHECK(narrow.isWall(2, 5));
  CHECK(std::count(narrow.walls.begin(), narrow.walls.end(), 1) == 2);
  // lower: the rows that stay
  const Tray low = tilted::withSize(t, 8, 5);
  CHECK(low.isWall(0, 0));
  CHECK(low.isWall(7, 0));
  CHECK(std::count(low.walls.begin(), low.walls.end(), 1) == 2);
  // wider and higher: every wall where it was
  const Tray grown = tilted::withSize(t, 10, 9);
  CHECK(grown.walls.size() == 90);
  CHECK(grown.isWall(0, 0));
  CHECK(grown.isWall(7, 0));
  CHECK(grown.isWall(2, 5));
  CHECK(grown.isWall(7, 7));
  CHECK(std::count(grown.walls.begin(), grown.walls.end(), 1) == 4);
  // and back: what was cut off does not return
  const Tray back = tilted::withSize(narrow, 8, 8);
  CHECK(std::count(back.walls.begin(), back.walls.end(), 1) == 2);
  CHECK_FALSE(back.isWall(7, 0));
  CHECK_FALSE(back.isWall(7, 7));
  // no wall left: no wall list
  CHECK(tilted::withSize(t, 1, 1).walls.size() == 1);
  CHECK(tilted::withSize(walled(trayOf(8, 2, 1), {63}), 4, 4).walls.empty());
  // the other settings stay
  CHECK(narrow.over == 2);
  CHECK(narrow.down == 1);
  CHECK(narrow.allowance == 0.05);
}
