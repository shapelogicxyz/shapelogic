// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023), after voxel_prisms.cpp for the shape of the class.
#include "shapelogic/grid/voxel_cairo.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/cairo_lattice.h"
#include "shapelogic/grid/symmetries_derived.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bt {

namespace {

struct Cell {
  int x;
  int y;
  int z;
};

// The in-plane part of a transformation's matrix: a signed permutation
// of x and y (turns by right angles and mirrors), as integers.
struct Plane {
  int xx;
  int xy;
  int yx;
  int yy;
  bool flipsZ;
};

Plane planeOf(unsigned int trans) {
  const auto& m = detail::cairoSymmetries().matrix(trans);
  const auto r = [](double v) { return static_cast<int>(std::lround(v)); };
  return {r(m[0]), r(m[1]), r(m[3]), r(m[4]), m[8] < 0};
}

// The pentagon of cell (x, y) moved by a transformation's plane part, as
// the exact corner coordinates, in any order.
std::vector<cairo::Point> movedPentagon(const Plane& p, int x, int y) {
  std::vector<cairo::Point> out;
  for (const cairo::Point& c : cairo::pentagon(x, y)) {
    // (a + o ofs) * s for s in {-1, 0, 1}, summed over x and y
    const cairo::Coord nx{p.xx * c.x.a + p.xy * c.y.a, p.xx * c.x.o + p.xy * c.y.o};
    const cairo::Coord ny{p.yx * c.x.a + p.yy * c.y.a, p.yx * c.x.o + p.yy * c.y.o};
    out.push_back({nx, ny});
  }
  return out;
}

bool samePentagon(std::vector<cairo::Point> a, const std::array<cairo::Point, 5>& b) {
  std::ranges::sort(a);
  std::vector<cairo::Point> bb(b.begin(), b.end());
  std::ranges::sort(bb);
  return a == bb;
}

// The offset (dx, dy) that, added to a cell moved by the transformation's
// plane part, lands it on the cell of the moved pentagon: 0 for the turns
// about the 4-fold centre, the glide's shift for the mirrors and the
// turns over. Found once for each transformation from the four kinds.
const std::array<std::array<int, 2>, 16>& offsets() {
  static const std::array<std::array<int, 2>, 16> all = [] {
    std::array<std::array<int, 2>, 16> out{};
    const unsigned int n = detail::cairoSymmetries().getNumTransformationsMirror();
    BT_ASSERT(n == 16);
    for (unsigned int t = 0; t < n; t++) {
      const Plane p = planeOf(t);
      bool found = false;
      for (int r = 0; r <= 3 && !found; r++) {
        for (int dy = -r; dy <= r && !found; dy++) {
          for (int dx = -r; dx <= r && !found; dx++) {
            if (std::max(std::abs(dx), std::abs(dy)) != r) {
              continue;
            }
            bool all = true;
            for (int k = 0; k < 4 && all; k++) {
              const int bx = cairo::kBase[k][0];
              const int by = cairo::kBase[k][1];
              const int mx = p.xx * bx + p.xy * by + dx;
              const int my = p.yx * bx + p.yy * by + dy;
              if (!cairo::isCell(mx, my)) {
                all = false;
                break;
              }
              std::vector<cairo::Point> moved = movedPentagon(p, bx, by);
              for (cairo::Point& c : moved) {
                c.x.a += dx;
                c.y.a += dy;
              }
              all = samePentagon(std::move(moved), cairo::pentagon(mx, my));
            }
            if (all) {
              out[t] = {dx, dy};
              found = true;
            }
          }
        }
      }
      BT_ASSERT_MSG(found, "a cairo transformation lands between cells");
    }
    return out;
  }();
  return all;
}

Cell turned(int x, int y, int z, unsigned int trans) {
  const Plane p = planeOf(trans);
  const auto& d = offsets()[trans];
  return {p.xx * x + p.xy * y + d[0], p.yx * x + p.yy * y + d[1], p.flipsZ ? -z - 1 : z};
}

}  // namespace

void VoxelCairo::transformPoint(int* x, int* y, int* z, unsigned int trans) const {
  BT_ASSERT(trans < detail::cairoSymmetries().getNumTransformationsMirror());
  const Cell c = turned(*x, *y, *z, trans);
  *x = c.x;
  *y = c.y;
  *z = c.z;
}

void VoxelCairo::getTransformMatrix(unsigned int trans, double m[9]) const {
  BT_ASSERT(trans < detail::cairoSymmetries().getNumTransformationsMirror());
  const auto& r = detail::cairoSymmetries().matrix(trans);
  for (int i = 0; i < 9; i++) {
    m[i] = r[static_cast<std::size_t>(i)];
  }
}

bool VoxelCairo::transform(unsigned int nr) {
  if (nr == 0) {
    return true;
  }
  BT_ASSERT(nr < detail::cairoSymmetries().getNumTransformationsMirror());

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
          const Cell c = turned(x, y, z, nr);
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

  // move the shape into the box by a lattice vector, so that the cells
  // keep their kinds
  const cairo::Fix f = cairo::fixToLattice(-minx, -miny);
  minx -= f.a;
  miny -= f.b;

  const unsigned int nsx = asUnsigned(maxx - minx + 1);
  const unsigned int nsy = asUnsigned(maxy - miny + 1);
  const unsigned int nsz = asUnsigned(maxz - minz + 1);
  const unsigned int voxelsn = nsx * nsy * nsz;
  std::vector<VoxelType> s(voxelsn, kEmpty);

  index = 0;
  for (int z = 0; z < asInt(sz_); z++) {
    for (int y = 0; y < asInt(sy_); y++) {
      for (int x = 0; x < asInt(sx_); x++) {
        if (!isEmpty(index)) {
          const Cell c = turned(x, y, z, nr);
          s[asUnsigned(c.x - minx) + nsx * (asUnsigned(c.y - miny) + nsy * asUnsigned(c.z - minz))] =
              space_[index];
        }
        index++;
      }
    }
  }

  const Cell h = turned(hx_, hy_, hz_, nr);
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

void VoxelCairo::minimizePiece() {
  // the move to the corner must be a lattice vector: add what it lacks
  const cairo::Fix f = cairo::fixToLattice(-asInt(bx1_), -asInt(by1_));

  Voxel::minimizePiece();

  if (f.a != 0 || f.b != 0) {
    resize(sx_ + asUnsigned(f.a), sy_ + asUnsigned(f.b), sz_, 0);
    translate(f.a, f.b, 0, 0);
  }
}

bool VoxelCairo::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                             int* yn, int* zn) const {
  const int kind = cairo::kindOf(x, y);
  if (kind < 0) {
    return false;
  }
  const cairo::Around& a = cairo::around(kind);
  const auto corners = static_cast<unsigned int>(a.corners.size());
  switch (typ) {
    case 0:
      // the five sides, then the bottom and the top
      if (idx < 5) {
        *xn = x + a.sides[idx][0];
        *yn = y + a.sides[idx][1];
        *zn = z;
        return true;
      }
      if (idx < 7) {
        *xn = x;
        *yn = y;
        *zn = idx == 5 ? z - 1 : z + 1;
        return true;
      }
      return false;
    case 1:
      // in the plane the cells that share a corner (a whole edge of the
      // prism), then the cells across the sides in the layers below and
      // above (an edge of the bottom or the top)
      if (idx < corners) {
        *xn = x + a.corners[idx][0];
        *yn = y + a.corners[idx][1];
        *zn = z;
        return true;
      }
      if (idx < corners + 10) {
        const unsigned int i = idx - corners;
        *xn = x + a.sides[i % 5][0];
        *yn = y + a.sides[i % 5][1];
        *zn = i < 5 ? z - 1 : z + 1;
        return true;
      }
      return false;
    default:
      // the cells that share a corner in the plane, in the layers below
      // and above: a corner alone
      if (idx < 2 * corners) {
        *xn = x + a.corners[idx % corners][0];
        *yn = y + a.corners[idx % corners][1];
        *zn = idx < corners ? z - 1 : z + 1;
        return true;
      }
      return false;
  }
}

void VoxelCairo::resizeInclude(int& px, int& py, int& pz) {
  int nsx = asInt(getX());
  int nsy = asInt(getY());
  int nsz = asInt(getZ());
  int tx = px < 0 ? -px : 0;
  int ty = py < 0 ? -py : 0;
  int tz = 0;

  // the move must be a lattice vector
  const cairo::Fix f = cairo::fixToLattice(tx, ty);
  tx += f.a;
  ty += f.b;
  nsx += tx;
  nsy += ty;

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

bool VoxelCairo::validCoordinate(int x, int y, int /*z*/) const { return cairo::isCell(x, y); }

bool VoxelCairo::onGrid(int x, int y, int /*z*/) const { return cairo::keepsKinds(x, y); }

bool VoxelCairo::identicalInBB(const Voxel& op, bool includeColors) const {
  return cairo::keepsKinds(asInt(bx1_) - asInt(op.boundX1()), asInt(by1_) - asInt(op.boundY1())) &&
         Voxel::identicalInBB(op, includeColors);
}

}  // namespace bt
