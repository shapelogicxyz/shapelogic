// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel_2.cpp (the meshes, voxel_2_mesh.cpp, come in stage 6).

#include "bt/grid/voxel_spheres.h"

#include "bt/base/int_cast.h"

#include <cmath>

namespace bt {

namespace {

#include "bt/grid/tabs_2/tablesizes.inc"

/// Every rotation and mirror matrix of the sphere grid.
constexpr double kRotationMatrices[NUM_TRANSFORMATIONS_MIRROR][9] = {
#include "bt/grid/tabs_2/rotmatrix.inc"
};

struct Point {
  double x;
  double y;
  double z;
};

/// Rotates the centre of sphere (x, y, z), in sphere coordinates. The
/// expressions are the original's, term for term.
Point rotateSphere(const double* r, int x, int y, int z) {
  const double xp = x * std::sqrt(0.5);
  const double yp = y * std::sqrt(0.5);
  const double zp = z * std::sqrt(0.5);

  double xpn = r[0] * xp + r[1] * yp + r[2] * zp;
  double ypn = r[3] * xp + r[4] * yp + r[5] * zp;
  double zpn = r[6] * xp + r[7] * yp + r[8] * zp;

  xpn /= std::sqrt(0.5);
  ypn /= std::sqrt(0.5);
  zpn /= std::sqrt(0.5);

  return Point{xpn, ypn, zpn};
}

int roundHalfAway(double v) { return static_cast<int>(v + (v < 0 ? -0.5 : 0.5)); }

}  // namespace

bool VoxelSpheres::transform(unsigned int nr) {
  if (nr == 0) {
    return true;
  }
  BT_ASSERT(nr < NUM_TRANSFORMATIONS_MIRROR);
  const double* r = kRotationMatrices[nr];

  int minx = 100000;
  int miny = 100000;
  int minz = 100000;
  int maxx = -100000;
  int maxy = -100000;
  int maxz = -100000;

  bool first = true;
  double shx = 0;
  double shy = 0;
  double shz = 0;

  // first pass: check every sphere lands on the lattice, find the new box
  unsigned int index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (((x + y + z) & 1) == 0 && !isEmpty(index)) {
          Point p = rotateSphere(r, x, y, z);

          if (first) {
            shx = p.x;
            shy = p.y;
            shz = p.z;
            p = Point{0, 0, 0};
            first = false;
          } else {
            p.x -= shx;
            p.y -= shy;
            p.z -= shz;
          }

          const int xn = roundHalfAway(p.x);
          const int yn = roundHalfAway(p.y);
          const int zn = roundHalfAway(p.z);

          // the new position must be (close to) a whole coordinate...
          if (std::fabs(p.x - xn) > 0.01 || std::fabs(p.y - yn) > 0.01 ||
              std::fabs(p.z - zn) > 0.01) {
            return false;
          }
          // ...and a valid sphere position
          if (((xn + yn + zn) & 1) != 0) {
            return false;
          }

          maxx = std::max(maxx, xn);
          maxy = std::max(maxy, yn);
          maxz = std::max(maxz, zn);
          minx = std::min(minx, xn);
          miny = std::min(miny, yn);
          minz = std::min(minz, zn);
        }
        index++;
      }
    }
  }

  // no spheres, nothing to do
  if (first) {
    return true;
  }

  if ((minx + miny + minz) & 1) {
    minx--;
  }

  unsigned int nsx = asUnsigned(maxx - minx + 1);
  unsigned int nsy = asUnsigned(maxy - miny + 1);
  unsigned int nsz = asUnsigned(maxz - minz + 1);

  // never make the space smaller than before; centre the shape in the old
  // size instead
  if (nsx < sx_ || nsy < sy_ || nsz < sz_) {
    // shift so that spheres stay on valid positions: (dx+dy+dz) must be even
    int dx = nsx < sx_ ? asInt((sx_ - nsx) / 2) : 0;
    int dy = nsy < sy_ ? asInt((sy_ - nsy) / 2) : 0;
    int dz = nsz < sz_ ? asInt((sz_ - nsz) / 2) : 0;

    if ((dx + dy + dz) & 1) {
      if (dx > 0) {
        dx--;
      } else if (dy > 0) {
        dy--;
      } else {
        dz--;
      }
    }

    minx -= dx;
    miny -= dy;
    minz -= dz;
    nsx = std::max(sx_, nsx);
    nsy = std::max(sy_, nsy);
    nsz = std::max(sz_, nsz);
  }

  const unsigned int voxelsn = nsx * nsy * nsz;
  std::vector<VoxelType> s(voxelsn, kEmpty);

  index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (((x + y + z) & 1) == 0 && !isEmpty(index)) {
          Point p = rotateSphere(r, x, y, z);
          p.x -= shx;
          p.y -= shy;
          p.z -= shz;

          const int xn = roundHalfAway(p.x);
          const int yn = roundHalfAway(p.y);
          const int zn = roundHalfAway(p.z);

          s[asUnsigned(xn - minx) + nsx * (asUnsigned(yn - miny) + nsy * asUnsigned(zn - minz))] =
              space_[index];
        }
        index++;
      }
    }
  }

  // the new hotspot
  BT_ASSERT(((hx_ + hy_ + hz_) & 1) == 0);

  Point h = rotateSphere(r, hx_, hy_, hz_);
  h.x -= shx;
  h.y -= shy;
  h.z -= shz;

  hx_ = roundHalfAway(h.x) - minx;
  hy_ = roundHalfAway(h.y) - miny;
  hz_ = roundHalfAway(h.z) - minz;

  BT_ASSERT(((hx_ + hy_ + hz_) & 1) == 0);

  sx_ = nsx;
  sy_ = nsy;
  sz_ = nsz;
  space_ = std::move(s);
  voxels_ = voxelsn;

  recalcBoundingBox();
  invalidateSymmetry();

  return true;
}

void VoxelSpheres::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  BT_ASSERT(((*x + *y + *z) & 1) == 0);

  const Point p = rotateSphere(kRotationMatrices[trans], *x, *y, *z);

  // The result may be off the lattice. The original's comment: this is only
  // used to transform assemblies, where two transformed points are
  // subtracted and the rounding errors are assumed to cancel out.
  *x = static_cast<int>(std::floor(p.x + 0.5));
  *y = static_cast<int>(std::floor(p.y + 0.5));
  *z = static_cast<int>(std::floor(p.z + 0.5));
}

void VoxelSpheres::getTransformMatrix(unsigned int trans, double m[9]) const {
  BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
  for (int i = 0; i < 9; i++) {
    m[i] = kRotationMatrices[trans][i];
  }
}

bool VoxelSpheres::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                               int* yn, int* zn) const {
  // spheres only have one kind of neighbour
  if (typ != 0) {
    return false;
  }
  // clang-format off
  switch (idx) {
    case  0: *xn = x-1; *yn = y-1; *zn = z;   break;
    case  1: *xn = x-1; *yn = y+1; *zn = z;   break;
    case  2: *xn = x+1; *yn = y-1; *zn = z;   break;
    case  3: *xn = x+1; *yn = y+1; *zn = z;   break;
    case  4: *xn = x-1; *yn = y;   *zn = z-1; break;
    case  5: *xn = x-1; *yn = y;   *zn = z+1; break;
    case  6: *xn = x+1; *yn = y;   *zn = z-1; break;
    case  7: *xn = x+1; *yn = y;   *zn = z+1; break;
    case  8: *xn = x;   *yn = y-1; *zn = z-1; break;
    case  9: *xn = x;   *yn = y-1; *zn = z+1; break;
    case 10: *xn = x;   *yn = y+1; *zn = z-1; break;
    case 11: *xn = x;   *yn = y+1; *zn = z+1; break;
    default: return false;
  }
  // clang-format on
  return true;
}

void VoxelSpheres::minimizePiece() {
  const bool moveAgain = ((bx1_ + by1_ + bz1_) & 1) != 0;

  Voxel::minimizePiece();

  if (moveAgain) {
    resize(sx_ + 1, sy_, sz_, 0);
    translate(1, 0, 0, 0);
  }
}

void VoxelSpheres::initHotspot() {
  unsigned long best = getX() * getX() + getY() * getY() + getZ() * getZ();

  // the sphere nearest to the origin
  for (unsigned int z = 0; z < getZ(); z++) {
    for (unsigned int y = 0; y < getY(); y++) {
      for (unsigned int x = 0; x < getX(); x++) {
        if (!isEmpty(x, y, z)) {
          const unsigned long diff = x * x + y * y + z * z;
          if (diff < best) {
            best = diff;
            setHotspot(asInt(x), asInt(y), asInt(z));
          }
        }
      }
    }
  }
}

void VoxelSpheres::resizeInclude(int& px, int& py, int& pz) {
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
    if (pz & 1) {
      nsz++;
      tz++;
    }
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

bool VoxelSpheres::validCoordinate(int x, int y, int z) const { return ((x + y + z) & 1) == 0; }

bool VoxelSpheres::onGrid(int x, int y, int z) const {
  // the shape does not fit when its corner has the wrong parity
  return ((x + y + z) & 1) == 0;
}

}  // namespace bt
