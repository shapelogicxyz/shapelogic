// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxel_3_c::getConnectionFace (src/lib/voxel_3.cpp), with its
// table src/lib/tabs_3/meshverts.inc copied unchanged.
#include "shapelogic/mesh/grid_geometry_tables.h"

#include "shapelogic/base/assert.h"

#include <cmath>

namespace bt::mesh::detail {

namespace {
using std::sqrt;
#include "shapelogic/mesh/tabs_3/meshverts.inc"

void push(std::vector<float>& c, int x, int y, int z, int v, double bevel, double offset) {
  const auto* p = vertices[v];
  c.push_back(static_cast<float>(x + p[0][0] + offset * p[1][0] + bevel * p[2][0]));
  c.push_back(static_cast<float>(y + p[0][1] + offset * p[1][1] + bevel * p[2][1]));
  c.push_back(static_cast<float>(z + p[0][2] + offset * p[1][2] + bevel * p[2][2]));
}
}  // namespace

void connectionFaceRhombic(int x, int y, int z, int n, double bevel, double offset,
                           std::vector<float>& corners) {
  // which of the 24 tetrahedra of a 5x5x5 block a cell is
  static constexpr int kVoxels[5 * 5 * 5] = {
      -1, -1, -1, -1, -1, -1, -1, 0,  -1, -1, -1, 1,  -1, 2,  -1, -1, -1, 3,  -1, -1, -1,
      -1, -1, -1, -1,

      -1, -1, 4,  -1, -1, -1, -1, -1, -1, -1, 5,  -1, -1, -1, 6,  -1, -1, -1, -1, -1, -1,
      -1, 7,  -1, -1,

      -1, 8,  -1, 9,  -1, 10, -1, -1, -1, 11, -1, -1, -1, -1, -1, 12, -1, -1, -1, 13, -1,
      14, -1, 15, -1,

      -1, -1, 16, -1, -1, -1, -1, -1, -1, -1, 17, -1, -1, -1, 18, -1, -1, -1, -1, -1, -1,
      -1, 19, -1, -1,

      -1, -1, -1, -1, -1, -1, -1, 20, -1, -1, -1, 21, -1, 22, -1, -1, -1, 23, -1, -1, -1,
      -1, -1, -1, -1,
  };

  int xc = floorDiv(x, 5);
  int yc = floorDiv(y, 5);
  int zc = floorDiv(z, 5);

  const int xs = x - 5 * xc;
  const int ys = y - 5 * yc;
  const int zs = z - 5 * zc;

  const int p = kVoxels[xs + 5 * (ys + 5 * zs)];
  BT_ASSERT(p != -1);

  xc *= 2;
  yc *= 2;
  zc *= 2;

  if (n < 0) {
    n = -1 - n;
    if (n < 10) {
      for (int i = 0; i < bevelFaces[p][n][0]; i++) {
        push(corners, xc, yc, zc, bevelFaces[p][n][i + 1], bevel, offset);
      }
    }
  } else if (n < 4) {
    for (int i = 0; i < 3; i++) {
      push(corners, xc, yc, zc, normalFaces[p][n][i], bevel, offset);
    }
  }
}

}  // namespace bt::mesh::detail
