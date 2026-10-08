// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxel_1_c::getConnectionFace (src/lib/voxel_1.cpp), with its
// table src/lib/tabs_1/meshverts.inc copied unchanged.
#include "bt/mesh/grid_geometry_tables.h"

#include <cmath>

namespace bt::mesh::detail {

namespace {
using std::sqrt;
#include "bt/mesh/tabs_1/meshverts.inc"

void push(std::vector<float>& c, int x, int y, int z, int v, double bevel, double offset) {
  const double height = sqrt(3) / 2;
  const auto* p = vertices[v];
  c.push_back(static_cast<float>(0.5 * x + p[0][0] + offset * p[1][0] + bevel * p[2][0]));
  c.push_back(static_cast<float>(height * y + p[0][1] + offset * p[1][1] + bevel * p[2][1]));
  c.push_back(static_cast<float>(z + p[0][2] + offset * p[1][2] + bevel * p[2][2]));
}
}  // namespace

void connectionFacePrisms(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners) {
  // the triangles point up or down by the parity of x + y
  const int p = ((x + y) & 1) == 0 ? 0 : 1;
  if (n < 0) {
    n = -1 - n;
    if (n < 15) {
      for (int i = 0; i < bevelFaces[p][n][0]; i++) {
        push(corners, x, y, z, bevelFaces[p][n][i + 1], bevel, offset);
      }
    }
  } else if (n < 5) {
    for (int i = 0; i < normalFaces[p][n][0]; i++) {
      push(corners, x, y, z, normalFaces[p][n][i + 1], bevel, offset);
    }
  }
}

}  // namespace bt::mesh::detail
