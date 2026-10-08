// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxel_4_c::getConnectionFace (src/lib/voxel_4.cpp), with its
// table src/lib/tabs_4/meshverts.inc copied unchanged.
#include "bt/mesh/grid_geometry_tables.h"

#include "bt/base/assert.h"

#include <cmath>

namespace bt::mesh::detail {

namespace {
using std::sqrt;
#include "bt/mesh/tabs_4/meshverts.inc"

void push(std::vector<float>& c, int x, int y, int z, int v, double bevel, double offset) {
  const auto* p = vertices[v];
  c.push_back(static_cast<float>(x + p[0][0] + offset * p[1][0] + bevel * p[2][0]));
  c.push_back(static_cast<float>(y + p[0][1] + offset * p[1][1] + bevel * p[2][1]));
  c.push_back(static_cast<float>(z + p[0][2] + offset * p[1][2] + bevel * p[2][2]));
}
}  // namespace

void connectionFaceTetraOcta(int x, int y, int z, int n, double bevel, double offset,
                             std::vector<float>& corners) {
  // which of the 10 cells of a 3x3x3 block (two kinds of block, by parity)
  static constexpr int kVoxels[] = {
      -1, -1, 2,  3,  -1, -1, -1, -1, -1, -1, -1, -1, 4,  -1, -1, -1, -1, 5,

      -1, -1, -1, -1, -1, -1, -1, 0,  -1, -1, 1,  -1, -1, -1, -1, -1, -1, -1,

      6,  -1, -1, -1, -1, 7,  -1, -1, -1, -1, -1, -1, -1, -1, 8,  9,  -1, -1,
  };

  const int xc = floorDiv(x, 3);
  const int yc = floorDiv(y, 3);
  const int zc = floorDiv(z, 3);

  int xs = x - 3 * xc;
  const int ys = y - 3 * yc;
  const int zs = z - 3 * zc;

  if (((xc + yc + zc) & 1) != 0) {
    xs += 3;
  }

  const int p = kVoxels[xs + 6 * (ys + 3 * zs)];
  BT_ASSERT(p != -1);

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
