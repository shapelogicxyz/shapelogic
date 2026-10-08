// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/gui/grideditor_0.cpp to grideditor_4.cpp (drawTileFrame,
// getTriangle, getPolygon, the circles of grideditor_2, calcParameters)
// and the shared edges of drawTileCursor / findCommonEdge. The corners are
// worked out on an integer lattice per grid (half a cell where the
// original took sx / 2), so that neighbouring cells share their corners
// exactly, and scaled to cell units at the end.
#include "bt/app/layer_geometry.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace bt::app {

namespace {

struct Lattice {
  int x = 0;
  int y = 0;
  auto operator<=>(const Lattice&) const = default;
};

// A cell's corners on the lattice, in the original's order.
using Corners = std::vector<Lattice>;

// grideditor_0: the square of cell (x, y).
Corners cubeCorners(int x, int y) { return {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}}; }

// grideditor_1: lattice x in half sides (s / 2), y in rows (s2). A
// triangle with its base at the top when x + y is odd.
Corners prismCorners(int x, int y) {
  if (((x + y) & 1) != 0) {
    return {{x, y + 1}, {x + 2, y + 1}, {x + 1, y}};
  }
  return {{x, y}, {x + 2, y}, {x + 1, y + 1}};
}

// grideditor_3's getTriangle (offs 0): lattice in half squares (sx / 2), a
// square of 5 by 5 cells being 2 by 2. The layer in the middle of a block
// (z % 5 == 2) has eight small triangles, the others four large ones.
Corners rhombicCorners(int x, int y, int z) {
  const int xc = x / 5;
  const int yc = y / 5;
  x -= 5 * xc;
  y -= 5 * yc;
  z %= 5;
  const int x0 = 2 * xc;
  const int y0 = 2 * yc;
  const Lattice m{x0 + 1, y0 + 1};
  if (z == 2) {
    if (x == 0 && y == 1) {
      return {m, {x0, y0 + 1}, {x0, y0}};
    }
    if (x == 0 && y == 3) {
      return {m, {x0, y0 + 1}, {x0, y0 + 2}};
    }
    if (x == 1 && y == 0) {
      return {m, {x0 + 1, y0}, {x0, y0}};
    }
    if (x == 1 && y == 4) {
      return {m, {x0 + 1, y0 + 2}, {x0, y0 + 2}};
    }
    if (x == 3 && y == 0) {
      return {m, {x0 + 1, y0}, {x0 + 2, y0}};
    }
    if (x == 3 && y == 4) {
      return {m, {x0 + 1, y0 + 2}, {x0 + 2, y0 + 2}};
    }
    if (x == 4 && y == 1) {
      return {m, {x0 + 2, y0 + 1}, {x0 + 2, y0}};
    }
    if (x == 4 && y == 3) {
      return {m, {x0 + 2, y0 + 1}, {x0 + 2, y0 + 2}};
    }
    BT_ASSERT_MSG(false, "a rhombic cell the editor has no triangle for");
  }
  if (x < 2) {
    return {{x0, y0}, m, {x0, y0 + 2}};
  }
  if (x > 2) {
    return {{x0 + 2, y0}, m, {x0 + 2, y0 + 2}};
  }
  if (y < 2) {
    return {{x0, y0}, m, {x0 + 2, y0}};
  }
  return {{x0, y0 + 2}, m, {x0 + 2, y0 + 2}};
}

// grideditor_4's getPolygon (offs 0): lattice in squares of 3 by 3 cells.
// The octahedra (z % 3 == 1) are squares, the tetrahedra the triangles of
// a square's corners.
Corners tetraOctaCorners(int x, int y, int z) {
  const int xc = x / 3;
  const int yc = y / 3;
  x -= 3 * xc;
  y -= 3 * yc;
  if (z % 3 == 1) {
    return {{xc, yc}, {xc + 1, yc}, {xc + 1, yc + 1}, {xc, yc + 1}};
  }
  if (x == 0 && y == 0) {
    return {{xc, yc}, {xc + 1, yc}, {xc, yc + 1}};
  }
  if (x == 2 && y == 0) {
    return {{xc + 1, yc}, {xc + 1, yc + 1}, {xc, yc}};
  }
  if (x == 0 && y == 2) {
    return {{xc, yc + 1}, {xc, yc}, {xc + 1, yc + 1}};
  }
  BT_ASSERT_MSG(x == 2 && y == 2, "a tetrahedra-octahedra cell the editor has no polygon for");
  return {{xc + 1, yc + 1}, {xc, yc + 1}, {xc + 1, yc}};
}

// Counter-clockwise, keeping the first corner.
void counterClockwise(Corners& c) {
  long area2 = 0;
  for (std::size_t i = 0; i < c.size(); i++) {
    const Lattice& a = c[i];
    const Lattice& b = c[(i + 1) % c.size()];
    area2 += long{a.x} * b.y - long{b.x} * a.y;
  }
  if (area2 < 0) {
    std::reverse(c.begin() + 1, c.end());
  }
}

}  // namespace

LayerGeometry layerGeometry(const Voxel& space, unsigned int layer) {
  LayerGeometry g;
  const GridKind grid = space.getGridType().getType();
  const unsigned int sx = space.getX();
  const unsigned int sy = space.getY();

  // the lattice's units in cell units, and the extent (calcParameters)
  double ux = 1;
  double uy = 1;
  switch (grid) {
    case GridKind::TriangularPrism:
      ux = 0.5;
      uy = std::sqrt(3.0) / 2;
      g.width = (sx + 1) / 2.0;
      g.height = sy * uy;
      break;
    case GridKind::Rhombic:
      ux = uy = 0.5;
      g.width = (sx + 4) / 5;
      g.height = (sy + 4) / 5;
      break;
    case GridKind::TetraOcta:
      g.width = (sx + 2) / 3;
      g.height = (sy + 2) / 3;
      break;
    default:
      g.width = sx;
      g.height = sy;
      break;
  }
  if (layer >= space.getZ()) {
    return g;
  }

  const int z = asInt(layer);
  std::vector<Corners> corners;
  for (unsigned int ux0 = 0; ux0 < sx; ux0++) {
    for (unsigned int uy0 = 0; uy0 < sy; uy0++) {
      const int x = asInt(ux0);
      const int y = asInt(uy0);
      if (!space.validCoordinate(x, y, z)) {
        continue;
      }
      LayerCell cell;
      cell.x = x;
      cell.y = y;
      Corners c;
      switch (grid) {
        case GridKind::Spheres:
          // centred on its square (the original's circle sat 0.09 up and
          // to the left of it, a rounding of the widget's pixels)
          cell.circle = true;
          cell.centre = {x + 0.5, y + 0.5};
          cell.radius = std::sqrt(0.5);
          break;
        case GridKind::TriangularPrism:
          c = prismCorners(x, y);
          break;
        case GridKind::Rhombic:
          c = rhombicCorners(x, y, z);
          break;
        case GridKind::TetraOcta:
          c = tetraOctaCorners(x, y, z);
          break;
        default:
          c = cubeCorners(x, y);
          break;
      }
      counterClockwise(c);
      for (const Lattice& p : c) {
        cell.polygon.push_back({p.x * ux, p.y * uy});
      }
      cell.neighbours.assign(c.size(), -1);
      corners.push_back(std::move(c));
      g.cells.push_back(std::move(cell));
    }
  }

  // the cells across each edge: the ones with the same two corners
  std::map<std::pair<Lattice, Lattice>, std::pair<int, std::size_t>> edges;
  for (std::size_t i = 0; i < corners.size(); i++) {
    const Corners& c = corners[i];
    for (std::size_t e = 0; e < c.size(); e++) {
      const Lattice a = c[e];
      const Lattice b = c[(e + 1) % c.size()];
      const auto key = a < b ? std::pair{a, b} : std::pair{b, a};
      const auto [it, inserted] = edges.try_emplace(key, static_cast<int>(i), e);
      if (!inserted) {
        const auto [other, otherEdge] = it->second;
        g.cells[i].neighbours[e] = other;
        g.cells[asUnsigned(other)].neighbours[otherEdge] = static_cast<int>(i);
      }
    }
  }
  return g;
}

}  // namespace bt::app
