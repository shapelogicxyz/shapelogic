// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023), after voxel_prisms.cpp and voxel_bricks.cpp for the
// shape of the class.
#include "shapelogic/grid/voxel_hex.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/grid/symmetries_derived.h"

#include <array>
#include <cstddef>

namespace bt {

namespace {

// Cell (x, y, z) turned by transformation `trans` about the center of
// cell (0, 0, 0): its center moved by the matrix, then the cell there.
hex::HexCell turned(int x, int y, int z, unsigned int trans) {
  const auto& m = detail::hexagonalSymmetries().matrix(trans);
  const double px = hex::centerX(x, y) - hex::centerX(0, 0);
  const double py = hex::centerY(y) - hex::centerY(0);
  const double pz = hex::centerZ(z) - hex::centerZ(0);
  const double qx = m[0] * px + m[1] * py + m[2] * pz + hex::centerX(0, 0);
  const double qy = m[3] * px + m[4] * py + m[5] * pz + hex::centerY(0);
  const double qz = m[6] * px + m[7] * py + m[8] * pz + hex::centerZ(0);
  return hex::cellAt(qx, qy, qz);
}

}  // namespace

void VoxelHex::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  SL_ASSERT(trans < detail::hexagonalSymmetries().getNumTransformationsMirror());
  const hex::HexCell c = turned(*x, *y, *z, trans);
  *x = c.x;
  *y = c.y;
  *z = c.z;
}

void VoxelHex::getTransformMatrix(unsigned int trans, double m[9]) const {
  SL_ASSERT(trans < detail::hexagonalSymmetries().getNumTransformationsMirror());
  const auto& r = detail::hexagonalSymmetries().matrix(trans);
  for (int i = 0; i < 9; i++) {
    m[i] = r[static_cast<std::size_t>(i)];
  }
}

bool VoxelHex::transform(unsigned int nr) {
  if (nr == 0) {
    return true;
  }
  SL_ASSERT(nr < detail::hexagonalSymmetries().getNumTransformationsMirror());
  rebuildTurned([nr](int x, int y, int z) { return turned(x, y, z, nr); },
                [this](std::array<int, 3>& low, std::array<unsigned int, 3>& size) {
                  // never make the space smaller than before; center the
                  // shape in the old size instead
                  const std::array<unsigned int, 3> old{sx_, sy_, sz_};
                  for (std::size_t k = 0; k < 3; k++) {
                    if (size[k] < old[k]) {
                      low[k] -= asInt((old[k] - size[k]) / 2);
                      size[k] = old[k];
                    }
                  }
                });
  return true;
}

bool VoxelHex::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                           int* yn, int* zn) const {
  switch (typ) {
    case 0:
      // the six sides, then the bottom and the top
      if (idx < 6) {
        *xn = x + hex::kAround[idx][0];
        *yn = y + hex::kAround[idx][1];
        *zn = z;
        return true;
      }
      if (idx < 8) {
        *xn = x;
        *yn = y;
        *zn = idx == 6 ? z - 1 : z + 1;
        return true;
      }
      return false;
    case 1:
      // the six cells around, in the layer below and the layer above:
      // each shares an edge of the bottom or the top
      if (idx < 12) {
        *xn = x + hex::kAround[idx % 6][0];
        *yn = y + hex::kAround[idx % 6][1];
        *zn = idx < 6 ? z - 1 : z + 1;
        return true;
      }
      return false;
    default:
      // a corner of a hexagonal prism is shared by cells that also share
      // an edge with it: no corner-only neighbors
      return false;
  }
}

bool VoxelHex::validCoordinate(int /*x*/, int /*y*/, int /*z*/) const { return true; }

bool VoxelHex::onGrid(int /*x*/, int /*y*/, int /*z*/) const { return true; }

}  // namespace bt
