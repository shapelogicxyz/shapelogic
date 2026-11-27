// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023): the faces of a hexagonal prism cell, from
// hex_lattice.h. No bevel faces: the uniform mesher draws this grid.
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/mesh/grid_geometry_tables.h"

#include <array>

namespace sl::mesh::detail {

void connectionFaceHex(int x, int y, int z, int n, std::vector<float>& corners) {
  const double cx = hex::centerX(x, y);
  const double cy = hex::centerY(y);
  std::array<std::array<double, 2>, 6> polygon{};
  for (std::size_t i = 0; i < polygon.size(); i++) {
    polygon[i] = {cx + hex::kCornerX[i], cy + hex::kCornerY[i]};
  }
  prismFace(polygon, z, n, corners);
}

}  // namespace sl::mesh::detail
