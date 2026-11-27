// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from calculateSize and recalcSpaceCoordinates of
// src/lib/voxel_0.cpp to voxel_4.cpp, and the dispatch of getConnectionFace.
#include "shapelogic/mesh/grid_geometry.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/cairo_lattice.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/grid/types.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/grid_geometry_tables.h"

#include <cmath>

namespace sl::mesh {

void connectionFace(const Voxel& v, int x, int y, int z, int n, double bevel, double offset,
                    std::vector<float>& corners) {
  switch (v.getGridType().getType()) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      detail::connectionFaceBricks(x, y, z, n, bevel, offset, corners);
      return;
    case GridKind::TriangularPrism:
      detail::connectionFacePrisms(x, y, z, n, bevel, offset, corners);
      return;
    case GridKind::Spheres:
      // a ball has no bevel faces; its faces are those of the rhombic
      // dodecahedron around it
      if (n >= 0) {
        detail::connectionFaceSpheres(x, y, z, n, corners);
      }
      return;
    case GridKind::Rhombic:
      detail::connectionFaceRhombic(x, y, z, n, bevel, offset, corners);
      return;
    case GridKind::TetraOcta:
      detail::connectionFaceTetraOcta(x, y, z, n, bevel, offset, corners);
      return;
    case GridKind::Hexagonal:
      detail::connectionFaceHex(x, y, z, n, corners);
      return;
    case GridKind::Cairo:
      detail::connectionFaceCairo(x, y, z, n, corners);
      return;
  }
  SL_ASSERT_MSG(false, "unknown grid");
}

void calculateSize(const Voxel& v, float* x, float* y, float* z) {
  const double sx = v.getX();
  const double sy = v.getY();
  const double sz = v.getZ();
  switch (v.getGridType().getType()) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      *x = static_cast<float>(v.getX());
      *y = static_cast<float>(v.getY());
      *z = static_cast<float>(v.getZ());
      return;
    case GridKind::TriangularPrism:
      *x = static_cast<float>(1 + (sx - 1) * 0.5);
      *y = static_cast<float>(sy * (std::sqrt(3) / 2));
      *z = static_cast<float>(sz);
      return;
    case GridKind::Spheres:
      *x = static_cast<float>(1 + (sx - 1) * std::sqrt(0.5));
      *y = static_cast<float>(1 + (sy - 1) * std::sqrt(0.5));
      *z = static_cast<float>(1 + (sz - 1) * std::sqrt(0.5));
      return;
    case GridKind::Rhombic:
      *x = static_cast<float>(2 * wholeUnits(v.getX(), 5));
      *y = static_cast<float>(2 * wholeUnits(v.getY(), 5));
      *z = static_cast<float>(2 * wholeUnits(v.getZ(), 5));
      return;
    case GridKind::TetraOcta:
      *x = static_cast<float>(wholeUnits(v.getX(), 3));
      *y = static_cast<float>(wholeUnits(v.getY(), 3));
      *z = static_cast<float>(wholeUnits(v.getZ(), 3));
      return;
    case GridKind::Hexagonal:
      // each row reaches half a cell further right; the rows' lowest and
      // highest corners
      *x = static_cast<float>(sx + 0.5 * (sy - 1));
      *y = static_cast<float>(2 * hex::kRadius + (sy - 1) * hex::kRow);
      *z = static_cast<float>(sz);
      return;
    case GridKind::Cairo:
      // a cell's pentagon lies within one lattice unit of its coordinate
      *x = static_cast<float>((sx + 1) * cairo::kScale);
      *y = static_cast<float>((sy + 1) * cairo::kScale);
      *z = static_cast<float>(sz);
      return;
  }
  SL_ASSERT_MSG(false, "unknown grid");
}

void recalcSpaceCoordinates(const Voxel& v, float* x, float* y, float* z) {
  switch (v.getGridType().getType()) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      return;
    case GridKind::TriangularPrism:
      *x = static_cast<float>(static_cast<double>(*x) * 0.5);
      *y = static_cast<float>(static_cast<double>(*y) * (std::sqrt(3) / 2));
      return;
    case GridKind::Spheres:
      *x = static_cast<float>(static_cast<double>(*x) * std::sqrt(0.5));
      *y = static_cast<float>(static_cast<double>(*y) * std::sqrt(0.5));
      *z = static_cast<float>(static_cast<double>(*z) * std::sqrt(0.5));
      return;
    case GridKind::Rhombic:
      *x = static_cast<float>(static_cast<double>(*x) * 0.4);
      *y = static_cast<float>(static_cast<double>(*y) * 0.4);
      *z = static_cast<float>(static_cast<double>(*z) * 0.4);
      return;
    case GridKind::TetraOcta:
      *x /= 3;
      *y /= 3;
      *z /= 3;
      return;
    case GridKind::Hexagonal:
      // each row half a cell further right
      *x = static_cast<float>(static_cast<double>(*x) + 0.5 * static_cast<double>(*y));
      *y = static_cast<float>(static_cast<double>(*y) * hex::kRow);
      return;
    case GridKind::Cairo:
      *x = static_cast<float>(static_cast<double>(*x) * cairo::kScale);
      *y = static_cast<float>(static_cast<double>(*y) * cairo::kScale);
      return;
  }
  SL_ASSERT_MSG(false, "unknown grid");
}

namespace {

constexpr Matrix3 diagonal(double x, double y, double z) {
  return {{{x, 0, 0}, {0, y, 0}, {0, 0, z}}};
}

}  // namespace

SpaceRule spaceRule(GridKind kind) {
  const double half = std::sqrt(0.5);
  switch (kind) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      return {diagonal(1, 1, 1), 1, diagonal(1, 1, 1), {0, 0, 0}};
    case GridKind::TriangularPrism:
      // x: 1 + (x - 1) / 2
      return {diagonal(0.5, std::sqrt(3) / 2, 1), 1, diagonal(0.5, std::sqrt(3) / 2, 1),
              {0.5, 0, 0}};
    case GridKind::Spheres:
      // 1 + (s - 1) sqrt(1/2) on each axis
      return {diagonal(half, half, half), 1, diagonal(half, half, half),
              {1 - half, 1 - half, 1 - half}};
    case GridKind::Rhombic:
      return {diagonal(0.4, 0.4, 0.4), 5, diagonal(2, 2, 2), {0, 0, 0}};
    case GridKind::TetraOcta:
      return {diagonal(1.0 / 3, 1.0 / 3, 1.0 / 3), 3, diagonal(1, 1, 1), {0, 0, 0}};
    case GridKind::Hexagonal:
      // x + (y - 1) / 2, and 2 kRadius + (y - 1) kRow
      return {{{{1, 0.5, 0}, {0, hex::kRow, 0}, {0, 0, 1}}},
              1,
              {{{1, 0.5, 0}, {0, hex::kRow, 0}, {0, 0, 1}}},
              {-0.5, 2 * hex::kRadius - hex::kRow, 0}};
    case GridKind::Cairo:
      // (s + 1) kScale in x and y
      return {diagonal(cairo::kScale, cairo::kScale, 1), 1, diagonal(cairo::kScale, cairo::kScale, 1),
              {cairo::kScale, cairo::kScale, 0}};
  }
  SL_ASSERT_MSG(false, "unknown grid");
  return {};
}

}  // namespace sl::mesh
