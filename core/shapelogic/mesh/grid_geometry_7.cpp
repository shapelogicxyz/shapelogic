// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023): the faces of a hexagonal prism cell, from
// hex_lattice.h. No bevel faces: the uniform mesher draws this grid.
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/mesh/grid_geometry_tables.h"

namespace bt::mesh::detail {

namespace {

void push(std::vector<float>& c, double x, double y, double z) {
  c.push_back(static_cast<float>(x));
  c.push_back(static_cast<float>(y));
  c.push_back(static_cast<float>(z));
}

}  // namespace

void connectionFaceHex(int x, int y, int z, int n, std::vector<float>& corners) {
  if (n < 0 || n >= 8) {
    return;
  }
  const double cx = hex::centreX(x, y);
  const double cy = hex::centreY(y);
  const double z0 = z;
  const double z1 = z + 1;
  if (n < 6) {
    // side n, between corners n and n + 1, counter clockwise seen from
    // outside
    const int a = n;
    const int b = (n + 1) % 6;
    push(corners, cx + hex::kCornerX[a], cy + hex::kCornerY[a], z0);
    push(corners, cx + hex::kCornerX[b], cy + hex::kCornerY[b], z0);
    push(corners, cx + hex::kCornerX[b], cy + hex::kCornerY[b], z1);
    push(corners, cx + hex::kCornerX[a], cy + hex::kCornerY[a], z1);
  } else if (n == 6) {
    // the bottom, clockwise seen from above
    for (int i = 5; i >= 0; i--) {
      push(corners, cx + hex::kCornerX[i], cy + hex::kCornerY[i], z0);
    }
  } else {
    // the top
    for (int i = 0; i < 6; i++) {
      push(corners, cx + hex::kCornerX[i], cy + hex::kCornerY[i], z1);
    }
  }
}

}  // namespace bt::mesh::detail
