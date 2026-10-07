// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel_1.cpp (everything except the meshes).

#include "bt/grid/voxel_prisms.h"

#include "bt/base/int_cast.h"

#include <cmath>

namespace bt {

namespace {

#include "bt/grid/tabs_1/tablesizes.inc"

using Matrices = double[NUM_TRANSFORMATIONS_MIRROR][9];

/// Every rotation and mirror matrix of the prism grid (the table uses sqrt,
/// so it is built at first use).
const Matrices& rotationMatrices() {
  using std::sqrt;
  static const Matrices m = {
#include "bt/grid/tabs_1/rotmatrix.inc"
  };
  return m;
}

struct Cell {
  int x;
  int y;
  int z;
};

/// Rotates cell (x, y, z) with matrix `r`: to Cartesian coordinates (the
/// cell's centre), rotate, back to cell coordinates. The expressions are the
/// original's, term for term, so the floating-point results are identical.
Cell rotateCell(const double* r, int x, int y, int z) {
  const double xp = 0.5 + 0.5 * x;
  double yp = y * std::sqrt(0.75);
  const double zp = z;

  if ((x + y) & 1) {
    yp += std::sqrt(1.0 / 3);
  } else {
    yp += std::sqrt(1.0 / 12);
  }

  double xpn = r[0] * xp + r[1] * yp + r[2] * zp;
  double ypn = r[3] * xp + r[4] * yp + r[5] * zp;
  const double zpn = r[6] * xp + r[7] * yp + r[8] * zp;

  xpn = (xpn - 0.5) * 2;
  ypn /= std::sqrt(0.75);

  return Cell{static_cast<int>(xpn + (xpn < 0 ? -0.5 : 0.5)), static_cast<int>(std::floor(ypn)),
              static_cast<int>(zpn + (zpn < 0 ? -0.5 : 0.5))};
}

}  // namespace

void VoxelPrisms::minimizePiece() {
  const bool moveAgain = ((bx1_ + by1_) & 1) != 0;

  Voxel::minimizePiece();

  if (moveAgain) {
    resize(sx_ + 1, sy_, sz_, 0);
    translate(1, 0, 0, 0);
  }
}

bool VoxelPrisms::transform(unsigned int nr) {
  if (nr == 0) {
    return true;
  }
  BT_ASSERT(nr < NUM_TRANSFORMATIONS_MIRROR);
  const double* r = rotationMatrices()[nr];

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
          const Cell c = rotateCell(r, x, y, z);
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

  if ((minx + miny) & 1) {
    minx--;
  }

  unsigned int nsx = asUnsigned(maxx - minx + 1);
  unsigned int nsy = asUnsigned(maxy - miny + 1);
  unsigned int nsz = asUnsigned(maxz - minz + 1);

  // never make the space smaller than before; centre the shape in the old
  // size instead
  if (nsz < sz_) {
    minz -= asInt((sz_ - nsz) / 2);
    nsz = sz_;
  }

  if (nsx < sx_ || nsy < sy_) {
    // shift so that the cells keep their type: (dx+dy) must be even
    int dx = nsx < sx_ ? asInt((sx_ - nsx) / 2) : 0;
    int dy = nsy < sy_ ? asInt((sy_ - nsy) / 2) : 0;

    if ((dx + dy) & 1) {
      if (dx > 0) {
        dx--;
      } else {
        dy--;
      }
    }

    minx -= dx;
    miny -= dy;
    nsx = std::max(sx_, nsx);
    nsy = std::max(sy_, nsy);
  }

  const unsigned int voxelsn = nsx * nsy * nsz;
  std::vector<VoxelType> s(voxelsn, kEmpty);

  index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (!isEmpty(index)) {
          const Cell c = rotateCell(r, x, y, z);
          s[asUnsigned(c.x - minx) + nsx * (asUnsigned(c.y - miny) + nsy * asUnsigned(c.z - minz))] =
              space_[index];
        }
        index++;
      }
    }
  }

  // the new hotspot
  const Cell h = rotateCell(r, hx_, hy_, hz_);
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

bool VoxelPrisms::identicalInBB(const Voxel& op, bool includeColors) const {
  return ((bx1_ + by1_) & 1) == ((op.boundX1() + op.boundY1()) & 1) &&
         Voxel::identicalInBB(op, includeColors);
}

void VoxelPrisms::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  const Cell c = rotateCell(rotationMatrices()[trans], *x, *y, *z);
  *x = c.x;
  *y = c.y;
  *z = c.z;
}

void VoxelPrisms::getTransformMatrix(unsigned int trans, double m[9]) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  for (int i = 0; i < 9; i++) {
    m[i] = rotationMatrices()[trans][i];
  }
}

bool VoxelPrisms::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                              int* yn, int* zn) const {
  const int t = ((x + y) & 1) ? -1 : 1;  // -1 for a triangle pointing down, 1 for up

  // clang-format off
  switch (typ) {
    case 0:
      switch (idx) {
        case 0: *xn = x-1; *yn = y;   *zn = z;   break;
        case 1: *xn = x+1; *yn = y;   *zn = z;   break;
        case 2: *xn = x;   *yn = y;   *zn = z-1; break;
        case 3: *xn = x;   *yn = y;   *zn = z+1; break;
        case 4: *xn = x;   *yn = y-t; *zn = z;   break;
        default: return false;
      }
      break;
    case 1:
      switch (idx) {
        case  0: *xn = x-2; *yn = y  ; *zn = z;   break;
        case  1: *xn = x+2; *yn = y  ; *zn = z;   break;
        case  2: *xn = x-1; *yn = y+1; *zn = z;   break;
        case  3: *xn = x-1; *yn = y-1; *zn = z;   break;
        case  4: *xn = x+1; *yn = y+1; *zn = z;   break;
        case  5: *xn = x+1; *yn = y-1; *zn = z;   break;
        case  6: *xn = x-2; *yn = y-t; *zn = z;   break;
        case  7: *xn = x+2; *yn = y-t; *zn = z;   break;
        case  8: *xn = x;   *yn = y+t; *zn = z;   break;

        case  9: *xn = x-1; *yn = y;   *zn = z+1; break;
        case 10: *xn = x-1; *yn = y;   *zn = z-1; break;
        case 11: *xn = x+1; *yn = y;   *zn = z+1; break;
        case 12: *xn = x+1; *yn = y;   *zn = z-1; break;

        case 13: *xn = x;   *yn = y-t; *zn = z-1; break;
        case 14: *xn = x;   *yn = y-t; *zn = z+1; break;
        default: return false;
      }
      break;
    case 2:
      switch (idx) {
        case  0: *xn = x-2; *yn = y  ; *zn = z-1; break;
        case  1: *xn = x+2; *yn = y  ; *zn = z-1; break;
        case  2: *xn = x-1; *yn = y+1; *zn = z-1; break;
        case  3: *xn = x-1; *yn = y-1; *zn = z-1; break;
        case  4: *xn = x+1; *yn = y+1; *zn = z-1; break;
        case  5: *xn = x+1; *yn = y-1; *zn = z-1; break;
        case  6: *xn = x-2; *yn = y-t; *zn = z-1; break;
        case  7: *xn = x+2; *yn = y-t; *zn = z-1; break;
        case  8: *xn = x;   *yn = y+t; *zn = z-1; break;

        case  9: *xn = x-2; *yn = y  ; *zn = z+1; break;
        case 10: *xn = x+2; *yn = y  ; *zn = z+1; break;
        case 11: *xn = x-1; *yn = y+1; *zn = z+1; break;
        case 12: *xn = x-1; *yn = y-1; *zn = z+1; break;
        case 13: *xn = x+1; *yn = y+1; *zn = z+1; break;
        case 14: *xn = x+1; *yn = y-1; *zn = z+1; break;
        case 15: *xn = x-2; *yn = y-t; *zn = z+1; break;
        case 16: *xn = x+2; *yn = y-t; *zn = z+1; break;
        case 17: *xn = x;   *yn = y+t; *zn = z+1; break;
        default: return false;
      }
      break;
    default:
      // the original fell through to "true" without writing the outputs
      break;
  }
  // clang-format on
  return true;
}

void VoxelPrisms::scale(unsigned int amount, bool grid) {
  const unsigned int nsx = amount - 1 + sx_ * amount;
  const unsigned int nsy = sy_ * amount;
  const unsigned int nsz = sz_ * amount;
  std::vector<VoxelType> s2(nsx * nsy * nsz, kEmpty);

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        const int ix = asInt(x);
        const int iy = asInt(y);
        const int iz = asInt(z);
        const bool up = ((x + y) & 1) == 0;
        for (unsigned int ax = 0; ax < 2 * amount - 1; ax++) {
          for (unsigned int ay = 0; ay < amount; ay++) {
            for (unsigned int az = 0; az < amount; az++) {
              VoxelType value = 0;

              if (!grid) {
                value = get(x, y, z);
              } else {
                const bool keep =
                    ((az == 0 || az == amount - 1) &&
                     (ay == 0 || ay == ax || ay + 1 == ax || ay == 2 * amount - 2 - ax ||
                      ay == 2 * amount - 3 - ax)) ||
                    (ax == 0 && ay == 0) || (ax == amount - 1 && ay == amount - 1) ||
                    (ax == 2 * amount - 2 && ay == 0) || (az == 0 && isEmpty2(ix, iy, iz - 1)) ||
                    (az == amount - 1 && isEmpty2(ix, iy, iz + 1)) ||
                    (ay == 0 && ((up && isEmpty2(ix, iy - 1, iz)) ||
                                 (!up && isEmpty2(ix, iy + 1, iz)))) ||
                    ((ay == ax || ay + 1 == ax) && isEmpty2(ix - 1, iy, iz)) ||
                    ((ay == 2 * amount - 2 - ax || ay == 2 * amount - 3 - ax) &&
                     isEmpty2(ix + 1, iy, iz));
                value = keep ? get(x, y, z) : 0;
              }
              if (ay <= ax && ay < 2 * amount - 1 - ax) {
                const unsigned int ny = up ? y * amount + ay : y * amount + amount - 1 - ay;
                s2[(x * amount + ax) + nsx * (ny + nsy * (z * amount + az))] = value;
              }
            }
          }
        }
      }
    }
  }

  space_ = std::move(s2);

  sx_ = nsx;
  sy_ = nsy;
  sz_ = nsz;

  hx_ = hy_ = hz_ = 0;

  voxels_ = sx_ * sy_ * sz_;

  recalcBoundingBox();
}

void VoxelPrisms::resizeInclude(int& px, int& py, int& pz) {
  int nsx = asInt(getX());
  int nsy = asInt(getY());
  int nsz = asInt(getZ());
  int tx = 0;
  int ty = 0;
  int tz = 0;

  if (px < 0) {
    nsx -= px;
    tx -= px;
    if (px & 1) {
      nsx++;
      tx++;
    }
  }
  if (py < 0) {
    nsy -= py;
    ty -= py;
    if (py & 1) {
      nsy++;
      ty++;
    }
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

bool VoxelPrisms::validCoordinate(int /*x*/, int /*y*/, int /*z*/) const { return true; }

bool VoxelPrisms::onGrid(int x, int y, int /*z*/) const {
  // the shape does not fit when its corner has the wrong parity
  return ((x + y) & 1) == 0;
}

}  // namespace bt
