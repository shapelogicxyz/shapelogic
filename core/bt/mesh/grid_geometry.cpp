// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from calculateSize and recalcSpaceCoordinates of
// src/lib/voxel_0.cpp to voxel_4.cpp, and the dispatch of getConnectionFace.
#include "bt/mesh/grid_geometry.h"

#include "bt/base/assert.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/mesh/grid_geometry_tables.h"

#include <cmath>

namespace bt::mesh {

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
  }
  BT_ASSERT_MSG(false, "unknown grid");
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
      *x = static_cast<float>(2 * ((v.getX() + 4) / 5));
      *y = static_cast<float>(2 * ((v.getY() + 4) / 5));
      *z = static_cast<float>(2 * ((v.getZ() + 4) / 5));
      return;
    case GridKind::TetraOcta:
      *x = static_cast<float>((v.getX() + 2) / 3);
      *y = static_cast<float>((v.getY() + 2) / 3);
      *z = static_cast<float>((v.getZ() + 2) / 3);
      return;
  }
  BT_ASSERT_MSG(false, "unknown grid");
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
  }
  BT_ASSERT_MSG(false, "unknown grid");
}

}  // namespace bt::mesh
