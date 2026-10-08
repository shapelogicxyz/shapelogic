// SPDX-License-Identifier: GPL-3.0-or-later
// The grid editor's layer drawing (layer_geometry.h) against the original:
// the corners of every cell of every layer of every grid, compared with
// the original's own drawing code at its pixel scale. The original's
// functions (getTriangle of src/gui/grideditor_3.cpp, getPolygon of
// grideditor_4.cpp and the corner sums of grideditor_0/1's
// drawTileFrame) are copied below as they are, with the FLTK calls left
// out, so the comparison needs no GUI.
#include "bt/app/layer_geometry.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::app;

namespace {

// ---- the original's code (grideditor_3.cpp, grideditor_4.cpp) ------------------

int intdiv_inf(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

void getTriangle(int x, int y, int z, int tx, int ty, int sx, int sy, int* x1, int* y1, int* x2,
                 int* y2, int* x3, int* y3, int offs) {
  int xc = (x + 1000) / 5 - 200;
  int yc = (y + 1000) / 5 - 200;

  x -= 5 * xc;
  y -= 5 * yc;
  z -= 5 * ((z + 1000) / 5 - 200);

  if (z == 2) {
    if (x == 0 && y == 1) {
      *x1 = tx + xc * sx + sx / 2 - 2 * offs; *y1 = ty - yc * sy - sy / 2 + offs;
      *x2 = tx + xc * sx + offs;             *y2 = ty - yc * sy - sy / 2 + offs;
      *x3 = tx + xc * sx + offs;             *y3 = ty - yc * sy - 2 * offs;
    } else if (x == 0 && y == 3) {
      *x1 = tx + xc * sx + sx / 2 - 2 * offs; *y1 = ty - yc * sy - sy / 2 - offs;
      *x2 = tx + xc * sx + offs;             *y2 = ty - yc * sy - sy / 2 - offs;
      *x3 = tx + xc * sx + offs;             *y3 = ty - yc * sy - sy + 2 * offs;
    } else if (x == 1 && y == 0) {
      *x1 = tx + xc * sx + sx / 2 - offs; *y1 = ty - yc * sy - sy / 2 + 2 * offs;
      *x2 = tx + xc * sx + sx / 2 - offs; *y2 = ty - yc * sy - offs;
      *x3 = tx + xc * sx + 2 * offs;      *y3 = ty - yc * sy - offs;
    } else if (x == 1 && y == 4) {
      *x1 = tx + xc * sx + sx / 2 - offs; *y1 = ty - yc * sy - sy / 2 - 2 * offs;
      *x2 = tx + xc * sx + sx / 2 - offs; *y2 = ty - yc * sy - sy + offs;
      *x3 = tx + xc * sx + 2 * offs;      *y3 = ty - yc * sy - sy + offs;
    } else if (x == 3 && y == 0) {
      *x1 = tx + xc * sx + sx / 2 + offs; *y1 = ty - yc * sy - sy / 2 + 2 * offs;
      *x2 = tx + xc * sx + sx / 2 + offs; *y2 = ty - yc * sy - offs;
      *x3 = tx + xc * sx + sx - 2 * offs; *y3 = ty - yc * sy - offs;
    } else if (x == 3 && y == 4) {
      *x1 = tx + xc * sx + sx / 2 + offs; *y1 = ty - yc * sy - sy / 2 - 2 * offs;
      *x2 = tx + xc * sx + sx / 2 + offs; *y2 = ty - yc * sy - sy + offs;
      *x3 = tx + xc * sx + sx - 2 * offs; *y3 = ty - yc * sy - sy + offs;
    } else if (x == 4 && y == 1) {
      *x1 = tx + xc * sx + sx / 2 + 2 * offs; *y1 = ty - yc * sy - sy / 2 + offs;
      *x2 = tx + xc * sx + sx - offs;         *y2 = ty - yc * sy - sy / 2 + offs;
      *x3 = tx + xc * sx + sx - offs;         *y3 = ty - yc * sy - 2 * offs;
    } else if (x == 4 && y == 3) {
      *x1 = tx + xc * sx + sx / 2 + 2 * offs; *y1 = ty - yc * sy - sy / 2 - offs;
      *x2 = tx + xc * sx + sx - offs;         *y2 = ty - yc * sy - sy / 2 - offs;
      *x3 = tx + xc * sx + sx - offs;         *y3 = ty - yc * sy - sy + 2 * offs;
    }
  } else {
    if (x < 2) {
      *x1 = tx + xc * sx + offs;          *y1 = ty - yc * sy - 2 * offs;
      *x2 = tx + xc * sx + sx / 2 - offs; *y2 = ty - yc * sy - sy / 2 + 0;
      *x3 = tx + xc * sx + offs;          *y3 = ty - yc * sy - sy + 2 * offs;
    } else if (x > 2) {
      *x1 = tx + xc * sx + sx - offs;     *y1 = ty - yc * sy - 2 * offs;
      *x2 = tx + xc * sx + sx / 2 + offs; *y2 = ty - yc * sy - sy / 2 + 0;
      *x3 = tx + xc * sx + sx - offs;     *y3 = ty - yc * sy - sy + 2 * offs;
    } else if (y < 2) {
      *x1 = tx + xc * sx + 2 * offs;      *y1 = ty - yc * sy - offs;
      *x2 = tx + xc * sx + sx / 2 + 0;    *y2 = ty - yc * sy - sy / 2 + offs;
      *x3 = tx + xc * sx + sx - 2 * offs; *y3 = ty - yc * sy - offs;
    } else {
      *x1 = tx + xc * sx + 2 * offs;      *y1 = ty - yc * sy - sy + offs;
      *x2 = tx + xc * sx + sx / 2 + 0;    *y2 = ty - yc * sy - sy / 2 - offs;
      *x3 = tx + xc * sx + sx - 2 * offs; *y3 = ty - yc * sy - sy + offs;
    }
  }
}

int getPolygon(int x, int y, int z, int tx, int ty, int sx, int sy, int* x1, int* y1, int* x2,
               int* y2, int* x3, int* y3, int* x4, int* y4, int offs) {
  int xc = intdiv_inf(x, 3);
  int yc = intdiv_inf(y, 3);
  int zc = intdiv_inf(z, 3);

  x -= 3 * xc;
  y -= 3 * yc;
  z -= 3 * zc;

  if (z == 1) {
    *x1 = tx + xc * sx + offs;      *y1 = ty - yc * sy - offs;
    *x2 = tx + xc * sx + sx - offs; *y2 = ty - yc * sy - offs;
    *x3 = tx + xc * sx + sx - offs; *y3 = ty - yc * sy - sy + offs;
    *x4 = tx + xc * sx + offs;      *y4 = ty - yc * sy - sy + offs;
    return 4;
  } else if (x == 0 && y == 0) {
    *x1 = tx + xc * sx + offs;          *y1 = ty - yc * sy - offs;
    *x2 = tx + xc * sx + sx - 2 * offs; *y2 = ty - yc * sy - offs;
    *x3 = tx + xc * sx + offs;          *y3 = ty - yc * sy - sy + 2 * offs;
  } else if (x == 2 && y == 0) {
    *x1 = tx + xc * sx + sx - offs; *y1 = ty - yc * sy - offs;
    *x2 = tx + xc * sx + sx - offs; *y2 = ty - yc * sy - sy + 2 * offs;
    *x3 = tx + xc * sx + 2 * offs;  *y3 = ty - yc * sy - offs;
  } else if (x == 0 && y == 2) {
    *x1 = tx + xc * sx + offs;          *y1 = ty - yc * sy - sy + offs;
    *x2 = tx + xc * sx + offs;          *y2 = ty - yc * sy - 2 * offs;
    *x3 = tx + xc * sx + sx - 2 * offs; *y3 = ty - yc * sy - sy + offs;
  } else if (x == 2 && y == 2) {
    *x1 = tx + xc * sx + sx - offs; *y1 = ty - yc * sy - sy + offs;
    *x2 = tx + xc * sx + 2 * offs;  *y2 = ty - yc * sy - sy + offs;
    *x3 = tx + xc * sx + sx - offs; *y3 = ty - yc * sy - 2 * offs;
  }
  return 3;
}

// ---- comparison ----------------------------------------------------------------

using Pixel = std::pair<long, long>;

// The original's corners as a set of points, y turned up (tx = ty = 0).
std::set<Pixel> pixels(const std::vector<int>& xs, const std::vector<int>& ys) {
  std::set<Pixel> s;
  for (std::size_t i = 0; i < xs.size(); i++) {
    s.insert({xs[i], -ys[i]});
  }
  return s;
}

// The port's corners at the original's scale: x in units of `px`, y of `py`.
std::set<Pixel> scaled(const LayerCell& c, double px, double py) {
  std::set<Pixel> s;
  for (const LayerPoint& p : c.polygon) {
    s.insert({std::lround(p.x * px), std::lround(p.y * py)});
  }
  return s;
}

double signedArea(const std::vector<LayerPoint>& p) {
  double a = 0;
  for (std::size_t i = 0; i < p.size(); i++) {
    const LayerPoint& u = p[i];
    const LayerPoint& v = p[(i + 1) % p.size()];
    a += u.x * v.y - v.x * u.y;
  }
  return a / 2;
}

// What holds for every layer of every grid: the cells are the valid
// coordinates of the layer, counter-clockwise, and each neighbour across
// an edge has the same edge the other way round.
void checkLayer(const Voxel& v, unsigned int z, const LayerGeometry& g) {
  std::size_t valid = 0;
  for (unsigned int x = 0; x < v.getX(); x++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      if (v.validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z))) {
        valid++;
      }
    }
  }
  REQUIRE(g.cells.size() == valid);
  for (std::size_t i = 0; i < g.cells.size(); i++) {
    const LayerCell& c = g.cells[i];
    CHECK(v.validCoordinate(c.x, c.y, static_cast<int>(z)));
    if (c.circle) {
      continue;
    }
    CHECK(signedArea(c.polygon) > 0);
    REQUIRE(c.neighbours.size() == c.polygon.size());
    for (std::size_t e = 0; e < c.polygon.size(); e++) {
      const int n = c.neighbours[e];
      if (n < 0) {
        continue;
      }
      const LayerCell& o = g.cells[static_cast<std::size_t>(n)];
      const LayerPoint a = c.polygon[e];
      const LayerPoint b = c.polygon[(e + 1) % c.polygon.size()];
      bool found = false;
      for (std::size_t f = 0; f < o.polygon.size(); f++) {
        if (o.polygon[f] == b && o.polygon[(f + 1) % o.polygon.size()] == a) {
          found = o.neighbours[f] == static_cast<int>(i);
        }
      }
      CHECK(found);
      // a neighbour in the drawing is one in the grid (by face or edge),
      // as drawTileCursor found them
      bool gridNeighbour = false;
      for (unsigned int typ = 0; typ < 2 && !gridNeighbour; typ++) {
        int nx = 0;
        int ny = 0;
        int nz = 0;
        for (unsigned int k = 0; v.getNeighbor(k, typ, c.x, c.y, static_cast<int>(z), &nx, &ny, &nz);
             k++) {
          gridNeighbour |= nx == o.x && ny == o.y && nz == static_cast<int>(z);
        }
      }
      CHECK(gridNeighbour);
    }
  }
}

}  // namespace

TEST_CASE("layer geometry: cubes are the squares of grideditor_0", "[app]") {
  const auto v = GridType(GridKind::Bricks).makeVoxel(4, 3, 2);
  const LayerGeometry g = layerGeometry(*v, 1);
  CHECK(g.width == 4);
  CHECK(g.height == 3);
  REQUIRE(g.cells.size() == 12);
  checkLayer(*v, 1, g);
  // x by x, then y: the original's drawing loops
  CHECK(g.cells[1].x == 0);
  CHECK(g.cells[1].y == 1);
  const LayerCell& c = g.cells[4];  // (1, 1)
  CHECK(c.polygon == std::vector<LayerPoint>{{1, 1}, {2, 1}, {2, 2}, {1, 2}});
  // right, top, left, bottom of an inner cell all have neighbours
  CHECK(std::count(c.neighbours.begin(), c.neighbours.end(), -1) == 0);
  CHECK(g.cells[0].neighbours == std::vector<int>{-1, 3, 1, -1});
  CHECK(layerGeometry(*v, 2).cells.empty());
}

TEST_CASE("layer geometry: prism triangles are grideditor_1's", "[app]") {
  const auto v = GridType(GridKind::TriangularPrism).makeVoxel(7, 4, 2);
  for (unsigned int z = 0; z < 2; z++) {
    const LayerGeometry g = layerGeometry(*v, z);
    CHECK(g.width == 4);
    CHECK(g.height == 4 * std::sqrt(3.0) / 2);
    checkLayer(*v, z, g);
    // drawTileFrame at s = 60: x is s * x / 2, rows s2 apart (counted
    // here in rows, as the original rounded s2 to whole pixels)
    const int s = 60;
    for (const LayerCell& c : g.cells) {
      std::vector<int> xs;
      std::vector<int> ys;
      const int x1 = s * c.x / 2;
      if (((c.x + c.y) & 1) != 0) {
        xs = {x1, x1 + s, x1 + s / 2};
        ys = {-(c.y + 1), -(c.y + 1), -c.y};
      } else {
        xs = {x1, x1 + s, x1 + s / 2};
        ys = {-c.y, -c.y, -(c.y + 1)};
      }
      CHECK(scaled(c, s, 2 / std::sqrt(3.0)) == pixels(xs, ys));
    }
  }
}

TEST_CASE("layer geometry: rhombic triangles are grideditor_3's getTriangle", "[app]") {
  const auto v = GridType(GridKind::Rhombic).makeVoxel(15, 10, 10);
  const int s = 80;
  for (unsigned int z = 0; z < 10; z++) {
    const LayerGeometry g = layerGeometry(*v, z);
    CHECK(g.width == 3);
    CHECK(g.height == 2);
    REQUIRE_FALSE(g.cells.empty());
    checkLayer(*v, z, g);
    for (const LayerCell& c : g.cells) {
      std::vector<int> xs(3);
      std::vector<int> ys(3);
      getTriangle(c.x, c.y, static_cast<int>(z), 0, 0, s, s, &xs[0], &ys[0], &xs[1], &ys[1], &xs[2],
                  &ys[2], 0);
      CHECK(scaled(c, s, s) == pixels(xs, ys));
    }
  }
}

TEST_CASE("layer geometry: tetrahedra and octahedra are grideditor_4's getPolygon", "[app]") {
  const auto v = GridType(GridKind::TetraOcta).makeVoxel(9, 6, 9);
  const int s = 80;
  for (unsigned int z = 0; z < 9; z++) {
    const LayerGeometry g = layerGeometry(*v, z);
    CHECK(g.width == 3);
    CHECK(g.height == 2);
    REQUIRE_FALSE(g.cells.empty());
    checkLayer(*v, z, g);
    for (const LayerCell& c : g.cells) {
      std::vector<int> xs(4);
      std::vector<int> ys(4);
      const int n = getPolygon(c.x, c.y, static_cast<int>(z), 0, 0, s, s, &xs[0], &ys[0], &xs[1],
                               &ys[1], &xs[2], &ys[2], &xs[3], &ys[3], 0);
      xs.resize(static_cast<std::size_t>(n));
      ys.resize(static_cast<std::size_t>(n));
      CHECK(scaled(c, s, s) == pixels(xs, ys));
    }
  }
  // an octahedra layer: squares side by side
  const LayerGeometry mid = layerGeometry(*v, 1);
  REQUIRE(mid.cells.size() == 6);
  CHECK(mid.cells[0].polygon.size() == 4);
  CHECK(std::count(mid.cells[0].neighbours.begin(), mid.cells[0].neighbours.end(), -1) == 2);
}

TEST_CASE("layer geometry: spheres are circles of the valid cells", "[app]") {
  const auto v = GridType(GridKind::Spheres).makeVoxel(4, 4, 3);
  for (unsigned int z = 0; z < 3; z++) {
    const LayerGeometry g = layerGeometry(*v, z);
    checkLayer(*v, z, g);
    REQUIRE(g.cells.size() == 8);
    for (const LayerCell& c : g.cells) {
      CHECK(c.circle);
      CHECK(c.polygon.empty());
      CHECK(c.centre == LayerPoint{c.x + 0.5, c.y + 0.5});
      // diagonal neighbours touch (grideditor_2's 10000 / 7072 of a cell)
      CHECK(std::abs(c.radius * 2 - 10000.0 / 7072) < 1e-3);
    }
  }
}
