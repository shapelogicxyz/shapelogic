// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxel_0_c::getConnectionFace (src/lib/voxel_0.cpp), with its
// table src/lib/tabs_0/meshverts.inc copied unchanged.
#include "bt/mesh/grid_geometry_tables.h"

#include "bt/base/int_cast.h"

namespace bt::mesh::detail {

namespace {
#include "bt/mesh/tabs_0/meshverts.inc"

// corner v of a cell at (x, y, z): its place, moved in by the offset and
// along the edge by the bevel
void push(std::vector<float>& c, int x, int y, int z, int v, double bevel, double offset) {
  const auto* p = vertices[v];
  c.push_back(static_cast<float>(x + p[0][0] + offset * p[1][0] + bevel * p[2][0]));
  c.push_back(static_cast<float>(y + p[0][1] + offset * p[1][1] + bevel * p[2][1]));
  c.push_back(static_cast<float>(z + p[0][2] + offset * p[1][2] + bevel * p[2][2]));
}
}  // namespace

void connectionFaceBricks(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners) {
  if (n < 0) {
    n = -1 - n;
    if (n < 20) {
      for (int i = 0; i < bevelFaces[n][0]; i++) {
        push(corners, x, y, z, bevelFaces[n][i + 1], bevel, offset);
      }
    }
  } else if (n < 6) {
    for (int i = 0; i < 4; i++) {
      push(corners, x, y, z, normalFaces[n][i], bevel, offset);
    }
  }
}

}  // namespace bt::mesh::detail
