// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel_0.cpp (everything except the meshes).

#include "shapelogic/grid/voxel_bricks.h"

#include "shapelogic/base/int_cast.h"

#include <algorithm>
#include <cstdlib>

namespace bt {

namespace {

#include "shapelogic/grid/tabs_0/tablesizes.inc"

/// Every rotation and mirror matrix of the cube grid.
constexpr int kRotationMatrices[NUM_TRANSFORMATIONS_MIRROR][9] = {
#include "shapelogic/grid/tabs_0/rotmatrix.inc"
};

}  // namespace

// The cube grid can compute the new size, bounding box and hotspot directly,
// which makes this faster than the sphere and prism versions.
bool VoxelBricks::transform(unsigned int nr) {
  BT_ASSERT(nr < NUM_TRANSFORMATIONS_MIRROR);
  const int* m = kRotationMatrices[nr];

  // transform the corner opposite (0, 0, 0) to find the new size and the
  // shift that brings everything back into the first octant
  const int sx = asInt(sx_) - 1;
  const int sy = asInt(sy_) - 1;
  const int sz = asInt(sz_) - 1;
  int tx = m[0] * sx + m[1] * sy + m[2] * sz;
  int ty = m[3] * sx + m[4] * sy + m[5] * sz;
  int tz = m[6] * sx + m[7] * sy + m[8] * sz;

  const int shx = tx < 0 ? -tx : 0;
  const int shy = ty < 0 ? -ty : 0;
  const int shz = tz < 0 ? -tz : 0;

  const int nsx = std::abs(tx) + 1;
  const int nsy = std::abs(ty) + 1;
  const int nsz = std::abs(tz) + 1;
  const auto newSize = asUnsigned(nsx * nsy * nsz);

  std::vector<VoxelType> s(newSize, kEmpty);
  std::vector<unsigned char> g;
  if (!goalPiece_.empty()) {
    g.assign(newSize, 0);
  }

  unsigned int index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        const bool hasCell = space_[index] != 0;
        const bool hasGoal = !g.empty() && index < goalPiece_.size() && goalPiece_[index] != 0;
        if (hasCell || hasGoal) {
          tx = m[0] * x + m[1] * y + m[2] * z + shx;
          ty = m[3] * x + m[4] * y + m[5] * z + shy;
          tz = m[6] * x + m[7] * y + m[8] * z + shz;
          BT_ASSERT(tx >= 0 && ty >= 0 && tz >= 0);
          const auto ni = asUnsigned(tx + nsx * (ty + nsy * tz));
          if (hasCell) {
            s[ni] = space_[index];
          }
          if (hasGoal) {
            g[ni] = goalPiece_[index];
          }
        }
        index++;
      }
    }
  }

  space_ = std::move(s);
  if (!g.empty()) {
    goalPiece_ = std::move(g);
  }

  sx_ = asUnsigned(nsx);
  sy_ = asUnsigned(nsy);
  sz_ = asUnsigned(nsz);

  // the hotspot
  const int thx = m[0] * hx_ + m[1] * hy_ + m[2] * hz_ + shx;
  const int thy = m[3] * hx_ + m[4] * hy_ + m[5] * hz_ + shy;
  const int thz = m[6] * hx_ + m[7] * hy_ + m[8] * hz_ + shz;
  hx_ = thx;
  hy_ = thy;
  hz_ = thz;

  // the bounding box
  const int x1 = asInt(bx1_);
  const int y1 = asInt(by1_);
  const int z1 = asInt(bz1_);
  const int x2 = asInt(bx2_);
  const int y2 = asInt(by2_);
  const int z2 = asInt(bz2_);
  const int tbx1 = m[0] * x1 + m[1] * y1 + m[2] * z1 + shx;
  const int tby1 = m[3] * x1 + m[4] * y1 + m[5] * z1 + shy;
  const int tbz1 = m[6] * x1 + m[7] * y1 + m[8] * z1 + shz;
  const int tbx2 = m[0] * x2 + m[1] * y2 + m[2] * z2 + shx;
  const int tby2 = m[3] * x2 + m[4] * y2 + m[5] * z2 + shy;
  const int tbz2 = m[6] * x2 + m[7] * y2 + m[8] * z2 + shz;

  bx1_ = asUnsigned(std::min(tbx1, tbx2));
  bx2_ = asUnsigned(std::max(tbx1, tbx2));
  by1_ = asUnsigned(std::min(tby1, tby2));
  by2_ = asUnsigned(std::max(tby1, tby2));
  bz1_ = asUnsigned(std::min(tbz1, tbz2));
  bz2_ = asUnsigned(std::max(tbz1, tbz2));

  invalidateSymmetry();
  // The original kept the transformation cache here, which then described
  // the old orientation; nothing may read it after a transform.
  invalidateTransformCache();

  return true;
}

void VoxelBricks::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  const int* m = kRotationMatrices[trans];

  const int sx = *x;
  const int sy = *y;
  const int sz = *z;

  *x = m[0] * sx + m[1] * sy + m[2] * sz;
  *y = m[3] * sx + m[4] * sy + m[5] * sz;
  *z = m[6] * sx + m[7] * sy + m[8] * sz;
}

void VoxelBricks::getTransformMatrix(unsigned int trans, double m[9]) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  for (int i = 0; i < 9; i++) {
    m[i] = kRotationMatrices[trans][i];
  }
}

bool VoxelBricks::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                              int* yn, int* zn) const {
  // clang-format off
  switch (typ) {
    case 0:
      switch (idx) {
        case 0: *xn = x-1; *yn = y;   *zn = z;   break;
        case 1: *xn = x;   *yn = y-1; *zn = z;   break;
        case 2: *xn = x  ; *yn = y;   *zn = z-1; break;
        case 3: *xn = x+1; *yn = y;   *zn = z;   break;
        case 4: *xn = x  ; *yn = y+1; *zn = z;   break;
        case 5: *xn = x  ; *yn = y;   *zn = z+1; break;
        default: return false;
      }
      break;
    case 1:
      switch (idx) {
        case  0: *xn = x-1; *yn = y-1; *zn = z;   break;
        case  1: *xn = x-1; *yn = y+1; *zn = z;   break;
        case  2: *xn = x+1; *yn = y+1; *zn = z;   break;
        case  3: *xn = x+1; *yn = y-1; *zn = z;   break;
        case  4: *xn = x-1; *yn = y;   *zn = z-1; break;
        case  5: *xn = x-1; *yn = y;   *zn = z+1; break;
        case  6: *xn = x+1; *yn = y;   *zn = z+1; break;
        case  7: *xn = x+1; *yn = y;   *zn = z-1; break;
        case  8: *xn = x;   *yn = y-1; *zn = z-1; break;
        case  9: *xn = x;   *yn = y-1; *zn = z+1; break;
        case 10: *xn = x;   *yn = y+1; *zn = z+1; break;
        case 11: *xn = x;   *yn = y+1; *zn = z-1; break;
        default: return false;
      }
      break;
    case 2:
      switch (idx) {
        case 0: *xn = x-1; *yn = y-1; *zn = z-1; break;
        case 1: *xn = x-1; *yn = y-1; *zn = z+1; break;
        case 2: *xn = x-1; *yn = y+1; *zn = z-1; break;
        case 3: *xn = x-1; *yn = y+1; *zn = z+1; break;
        case 4: *xn = x+1; *yn = y-1; *zn = z-1; break;
        case 5: *xn = x+1; *yn = y-1; *zn = z+1; break;
        case 6: *xn = x+1; *yn = y+1; *zn = z-1; break;
        case 7: *xn = x+1; *yn = y+1; *zn = z+1; break;
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

void VoxelBricks::scale(unsigned int amount, bool grid) {
  const unsigned int nsx = sx_ * amount;
  const unsigned int nsy = sy_ * amount;
  const unsigned int nsz = sz_ * amount;
  std::vector<VoxelType> s2(nsx * nsy * nsz);
  std::vector<unsigned char> g2;
  if (!goalPiece_.empty()) {
    g2.assign(nsx * nsy * nsz, 0);
  }

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        const int ix = asInt(x);
        const int iy = asInt(y);
        const int iz = asInt(z);
        for (unsigned int ax = 0; ax < amount; ax++) {
          for (unsigned int ay = 0; ay < amount; ay++) {
            for (unsigned int az = 0; az < amount; az++) {
              const unsigned int ni =
                  (x * amount + ax) + nsx * ((y * amount + ay) + nsy * (z * amount + az));
              const bool edge0x = ax == 0;
              const bool edge1x = ax == amount - 1;
              const bool edge0y = ay == 0;
              const bool edge1y = ay == amount - 1;
              const bool edge0z = az == 0;
              const bool edge1z = az == amount - 1;
              const bool keep =
                  !grid ||
                  (!isEmpty(x, y, z) &&
                   ((edge0x && isEmpty2(ix - 1, iy, iz)) || (edge1x && isEmpty2(ix + 1, iy, iz)) ||
                    (edge0y && isEmpty2(ix, iy - 1, iz)) || (edge1y && isEmpty2(ix, iy + 1, iz)) ||
                    (edge0z && isEmpty2(ix, iy, iz - 1)) || (edge1z && isEmpty2(ix, iy, iz + 1)) ||
                    ((edge0x || edge1x) && (edge0y || edge1y || edge0z || edge1z)) ||
                    ((edge0y || edge1y) && (edge0x || edge1x || edge0z || edge1z)) ||
                    ((edge0z || edge1z) && (edge0x || edge1x || edge0y || edge1y))));
              if (keep) {
                s2[ni] = get(x, y, z);
                if (!g2.empty()) {
                  g2[ni] = static_cast<unsigned char>(getGoalPiece(x, y, z));
                }
              } else {
                s2[ni] = 0;
              }
            }
          }
        }
      }
    }
  }

  space_ = std::move(s2);
  if (!g2.empty()) {
    goalPiece_ = std::move(g2);
  }

  sx_ = nsx;
  sy_ = nsy;
  sz_ = nsz;
  voxels_ = sx_ * sy_ * sz_;

  hx_ = hy_ = hz_ = 0;

  recalcBoundingBox();
}

bool VoxelBricks::scaleDown(unsigned char by, bool action) {
  if (by < 2) {
    return true;
  }
  if (sx_ < by || sy_ < by || sz_ < by) {
    return false;
  }

  const int b = by;
  for (int shx = 0; shx < b; shx++) {
    for (int shy = 0; shy < b; shy++) {
      for (int shz = 0; shz < b; shz++) {
        bool problem = false;

        for (int x = 0; x < asInt(sx_) / b + 1; x++) {
          for (int y = 0; y < asInt(sy_) / b + 1; y++) {
            for (int z = 0; z < asInt(sz_) / b + 1; z++) {
              for (int cx = 0; cx < b; cx++) {
                for (int cy = 0; cy < b; cy++) {
                  for (int cz = 0; cz < b; cz++) {
                    problem |= get2(x * b - shx, y * b - shy, z * b - shz) !=
                               get2(x * b - shx + cx, y * b - shy + cy, z * b - shz + cz);
                  }
                }
              }
            }
          }
        }

        if (!problem) {
          if (action) {
            // the +1 of the check is not needed here: those cells are empty
            const unsigned int nsx = sx_ / by;
            const unsigned int nsy = sy_ / by;
            const unsigned int nsz = sz_ / by;

            std::vector<VoxelType> s2(nsx * nsy * nsz);
            std::vector<unsigned char> g2;
            if (!goalPiece_.empty()) {
              g2.assign(nsx * nsy * nsz, 0);
            }

            for (unsigned int x = 0; x < nsx; x++) {
              for (unsigned int y = 0; y < nsy; y++) {
                for (unsigned int z = 0; z < nsz; z++) {
                  s2[x + nsx * (y + nsy * z)] =
                      get2(asInt(x) * b, asInt(y) * b, asInt(z) * b);
                  if (!g2.empty()) {
                    g2[x + nsx * (y + nsy * z)] =
                        static_cast<unsigned char>(getGoalPiece(x * by, y * by, z * by));
                  }
                }
              }
            }

            space_ = std::move(s2);
            if (!g2.empty()) {
              goalPiece_ = std::move(g2);
            }

            sx_ = nsx;
            sy_ = nsy;
            sz_ = nsz;
            voxels_ = sx_ * sy_ * sz_;

            recalcBoundingBox();
          }
          return true;
        }
      }
    }
  }
  return false;
}

void VoxelBricks::resizeInclude(int& px, int& py, int& pz) {
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

bool VoxelBricks::validCoordinate(int /*x*/, int /*y*/, int /*z*/) const { return true; }

bool VoxelBricks::onGrid(int /*x*/, int /*y*/, int /*z*/) const { return true; }

}  // namespace bt
