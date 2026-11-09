// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023): the faces of a cairo prism cell, from
// cairo_lattice.h. No bevel faces: the uniform mesher draws this grid.
#include "shapelogic/grid/cairo_lattice.h"
#include "shapelogic/mesh/grid_geometry_tables.h"

#include <array>

namespace bt::mesh::detail {

namespace {

// a corner in mesh units: the plane moved by one lattice unit and scaled
void push(std::vector<float>& c, const cairo::Point& p, double z) {
  c.push_back(static_cast<float>((p.x.value() + 1) * cairo::kScale));
  c.push_back(static_cast<float>((p.y.value() + 1) * cairo::kScale));
  c.push_back(static_cast<float>(z));
}

}  // namespace

void connectionFaceCairo(int x, int y, int z, int n, std::vector<float>& corners) {
  if (n < 0 || n >= 7 || !cairo::isCell(x, y)) {
    return;
  }
  const std::array<cairo::Point, 5> p = cairo::pentagon(x, y);
  const double z0 = z;
  const double z1 = z + 1;
  if (n < 5) {
    // side n, between corners n and n + 1
    const auto a = static_cast<std::size_t>(n);
    const std::size_t b = (a + 1) % 5;
    push(corners, p[a], z0);
    push(corners, p[b], z0);
    push(corners, p[b], z1);
    push(corners, p[a], z1);
  } else if (n == 5) {
    // the bottom, clockwise seen from above
    for (int i = 4; i >= 0; i--) {
      push(corners, p[static_cast<std::size_t>(i)], z0);
    }
  } else {
    // the top
    for (const cairo::Point& c : p) {
      push(corners, c, z1);
    }
  }
}

}  // namespace bt::mesh::detail
