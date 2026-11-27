// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023): the faces of a cairo prism cell, from
// cairo_lattice.h. No bevel faces: the uniform mesher draws this grid.
#include "shapelogic/grid/cairo_lattice.h"
#include "shapelogic/mesh/grid_geometry_tables.h"

#include <array>

namespace sl::mesh::detail {

void connectionFaceCairo(int x, int y, int z, int n, std::vector<float>& corners) {
  if (!cairo::isCell(x, y)) {
    return;
  }
  const std::array<cairo::Point, 5> p = cairo::pentagon(x, y);
  // the corners in mesh units: the plane moved by one lattice unit and scaled
  std::array<std::array<double, 2>, 5> polygon{};
  for (std::size_t i = 0; i < polygon.size(); i++) {
    polygon[i] = {(p[i].x.value() + 1) * cairo::kScale, (p[i].y.value() + 1) * cairo::kScale};
  }
  prismFace(polygon, z, n, corners);
}

}  // namespace sl::mesh::detail
