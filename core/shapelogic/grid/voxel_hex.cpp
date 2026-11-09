// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023), after voxel_prisms.cpp and voxel_bricks.cpp for the
// shape of the class.
#include "shapelogic/grid/voxel_hex.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/grid/symmetries_derived.h"

#include <algorithm>
#include <vector>

namespace bt {

namespace {

// Cell (x, y, z) turned by transformation `trans` about the centre of
// cell (0, 0, 0): its centre moved by the matrix, then the cell there.
hex::HexCell turned(int x, int y, int z, unsigned int trans) {
  const auto& m = detail::hexagonalSymmetries().matrix(trans);
  const double px = hex::centreX(x, y) - hex::centreX(0, 0);
  const double py = hex::centreY(y) - hex::centreY(0);
  const double pz = hex::centreZ(z) - hex::centreZ(0);
  const double qx = m[0] * px + m[1] * py + m[2] * pz + hex::centreX(0, 0);
  const double qy = m[3] * px + m[4] * py + m[5] * pz + hex::centreY(0);
  const double qz = m[6] * px + m[7] * py + m[8] * pz + hex::centreZ(0);
  return hex::cellAt(qx, qy, qz);
}

}  // namespace

void VoxelHex::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  BT_ASSERT(trans < detail::hexagonalSymmetries().getNumTransformationsMirror());
  const hex::HexCell c = turned(*x, *y, *z, trans);
  *x = c.x;
  *y = c.y;
  *z = c.z;
}

void VoxelHex::getTransformMatrix(unsigned int trans, double m[9]) const {
  BT_ASSERT(trans < detail::hexagonalSymmetries().getNumTransformationsMirror());
  const auto& r = detail::hexagonalSymmetries().matrix(trans);
  for (int i = 0; i < 9; i++) {
    m[i] = r[static_cast<std::size_t>(i)];
  }
}

bool VoxelHex::transform(unsigned int nr) {
  if (nr == 0) {
    return true;
  }
  BT_ASSERT(nr < detail::hexagonalSymmetries().getNumTransformationsMirror());

  int minx = 100000;
  int miny = 100000;
  int minz = 100000;
  int maxx = -100000;
  int maxy = -100000;
  int maxz = -100000;

  unsigned int index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (!isEmpty(index)) {
          const hex::HexCell c = turned(x, y, z, nr);
          maxx = std::max(maxx, c.x);
          maxy = std::max(maxy, c.y);
          maxz = std::max(maxz, c.z);
          minx = std::min(minx, c.x);
          miny = std::min(miny, c.y);
          minz = std::min(minz, c.z);
        }
        index++;
      }
    }
  }

  // empty space, nothing to do
  if (minx == 100000) {
    return true;
  }

  unsigned int nsx = asUnsigned(maxx - minx + 1);
  unsigned int nsy = asUnsigned(maxy - miny + 1);
  unsigned int nsz = asUnsigned(maxz - minz + 1);

  // never make the space smaller than before; centre the shape in the old
  // size instead
  if (nsx < sx_) {
    minx -= asInt((sx_ - nsx) / 2);
    nsx = sx_;
  }
  if (nsy < sy_) {
    miny -= asInt((sy_ - nsy) / 2);
    nsy = sy_;
  }
  if (nsz < sz_) {
    minz -= asInt((sz_ - nsz) / 2);
    nsz = sz_;
  }

  const unsigned int voxelsn = nsx * nsy * nsz;
  std::vector<VoxelType> s(voxelsn, kEmpty);

  index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (!isEmpty(index)) {
          const hex::HexCell c = turned(x, y, z, nr);
          s[asUnsigned(c.x - minx) + nsx * (asUnsigned(c.y - miny) + nsy * asUnsigned(c.z - minz))] =
              space_[index];
        }
        index++;
      }
    }
  }

  // the new hotspot
  const hex::HexCell h = turned(hx_, hy_, hz_, nr);
  hx_ = h.x - minx;
  hy_ = h.y - miny;
  hz_ = h.z - minz;

  sx_ = nsx;
  sy_ = nsy;
  sz_ = nsz;
  space_ = std::move(s);
  voxels_ = voxelsn;

  recalcBoundingBox();
  invalidateSymmetry();

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
      // an edge with it: no corner-only neighbours
      return false;
  }
}

void VoxelHex::resizeInclude(int& px, int& py, int& pz) {
  // as the cube grid: every move keeps the cells' look
  int nsx = asInt(getX());
  int nsy = asInt(getY());
  int nsz = asInt(getZ());
  int tx = 0;
  int ty = 0;
  int tz = 0;

  if (px < 0) {
    nsx -= px;
    tx -= px;
  }
  if (py < 0) {
    nsy -= py;
    ty -= py;
  }
  if (pz < 0) {
    nsz -= pz;
    tz -= pz;
  }
  if (px >= asInt(getX())) {
    nsx += px - asInt(getX()) + 1;
  }
  if (py >= asInt(getY())) {
    nsy += py - asInt(getY()) + 1;
  }
  if (pz >= asInt(getZ())) {
    nsz += pz - asInt(getZ()) + 1;
  }

  resize(asUnsigned(nsx), asUnsigned(nsy), asUnsigned(nsz), 0);
  translate(tx, ty, tz, 0);

  px += tx;
  py += ty;
  pz += tz;
}

bool VoxelHex::validCoordinate(int /*x*/, int /*y*/, int /*z*/) const { return true; }

bool VoxelHex::onGrid(int /*x*/, int /*y*/, int /*z*/) const { return true; }

}  // namespace bt
