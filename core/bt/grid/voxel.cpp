// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel.cpp (everything except the meshes and XML).

#include "bt/grid/voxel.h"

#include "bt/base/int_cast.h"
#include "bt/grid/symmetries.h"

#include <algorithm>
#include <cstring>

namespace bt {

namespace {
/// Cache entry not calculated yet.
constexpr int kCacheUninit = -30000;
/// Cache entry for a transformation this shape cannot take.
constexpr int kCacheNotDef = -30001;
}  // namespace

Voxel::Voxel(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init)
    : gt_(gt),
      sx_(x),
      sy_(y),
      sz_(z),
      voxels_(x * y * z),
      space_(voxels_, init),
      bbHsCache_(9 * gt.getSymmetries().getNumTransformationsMirror(), kCacheUninit) {
  if (init == 0) {
    bx2_ = by2_ = bz2_ = 0;
    bx1_ = x - 1;
    by1_ = y - 1;
    bz1_ = z - 1;
  } else {
    bx1_ = by1_ = bz1_ = 0;
    bx2_ = x - 1;
    by2_ = y - 1;
    bz2_ = z - 1;
  }
}

// The symmetry and transformation caches are copied too: they describe the
// content and hotspot, which the copy shares.
Voxel::Voxel(const Voxel&) = default;

Voxel::~Voxel() = default;

unsigned int Voxel::getBiggestDimension() const {
  if (sx_ > sy_) {
    return sz_ > sx_ ? sz_ : sx_;
  }
  return sz_ > sy_ ? sz_ : sy_;
}

void Voxel::skipRecalcBoundingBox(bool skipit) {
  if (skipit) {
    doRecalc_ = false;
  } else {
    doRecalc_ = true;
    recalcBoundingBox();
  }
}

void Voxel::recalcBoundingBox() {
  if (!doRecalc_) {
    return;
  }

  bx1_ = by1_ = bz1_ = sx_ + sy_ + sz_;
  bx2_ = by2_ = bz2_ = 0;

  bool empty = true;
  unsigned int index = 0;

  for (unsigned int z = 0; z < sz_; z++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int x = 0; x < sx_; x++) {
        if ((space_[index] & 3) != kEmpty) {
          bx1_ = std::min(bx1_, x);
          bx2_ = std::max(bx2_, x);
          by1_ = std::min(by1_, y);
          by2_ = std::max(by2_, y);
          bz1_ = std::min(bz1_, z);
          bz2_ = std::max(bz2_, z);
          empty = false;
        } else {
          space_[index] = 0;  // clear away colours that might be left
          if (index < goalPiece_.size()) {
            goalPiece_[index] = 0;
          }
        }
        index++;
      }
    }
  }

  if (empty) {
    bx1_ = by1_ = bz1_ = bx2_ = by2_ = bz2_ = 0;
  }

  invalidateTransformCache();
}

void Voxel::invalidateTransformCache() {
  std::fill(bbHsCache_.begin(), bbHsCache_.end(), kCacheUninit);
}

void Voxel::set(unsigned int p, VoxelType val) {
  BT_ASSERT(p < voxels_);
  const VoxelType old = space_[p];
  space_[p] = val;
  if ((old & 3) == kEmpty || (val & 3) == kEmpty) {
    if (p < goalPiece_.size()) {
      goalPiece_[p] = 0;
    }
  }
  recalcBoundingBox();
  invalidateSymmetry();
}

void Voxel::setAll(VoxelType val) {
  std::fill(space_.begin(), space_.end(), val);
  recalcBoundingBox();
  invalidateSymmetry();
}

bool Voxel::operator==(const Voxel& op) const {
  return sx_ == op.sx_ && sy_ == op.sy_ && sz_ == op.sz_ && space_ == op.space_ &&
         goalPiece_ == op.goalPiece_;
}

bool Voxel::identicalInBB(const Voxel& op, bool includeColors) const {
  if (bx2_ - bx1_ != op.bx2_ - op.bx1_) {
    return false;
  }
  if (by2_ - by1_ != op.by2_ - op.by1_) {
    return false;
  }
  if (bz2_ - bz1_ != op.bz2_ - op.bz1_) {
    return false;
  }

  for (unsigned int x = bx1_; x <= bx2_; x++) {
    for (unsigned int y = by1_; y <= by2_; y++) {
      for (unsigned int z = bz1_; z <= bz2_; z++) {
        const unsigned int ox = x - bx1_ + op.bx1_;
        const unsigned int oy = y - by1_ + op.by1_;
        const unsigned int oz = z - bz1_ + op.bz1_;
        if (includeColors) {
          if (get(x, y, z) != op.get(ox, oy, oz)) {
            return false;
          }
        } else {
          if (getState(x, y, z) != op.getState(ox, oy, oz)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool Voxel::identicalWithRots(const Voxel& op, bool includeMirror, bool includeColors) const {
  const Symmetries& sym = gt_.getSymmetries();
  const unsigned int maxTrans =
      includeMirror ? sym.getNumTransformationsMirror() : sym.getNumTransformations();

  for (unsigned int t = 0; t < maxTrans; t++) {
    auto v = op.clone();
    if (v->transform(t) && identicalInBB(*v, includeColors)) {
      return true;
    }
  }
  return false;
}

unsigned char Voxel::getMirrorTransform(const Voxel& op) const {
  const Symmetries& sym = gt_.getSymmetries();

  for (unsigned int t = sym.getNumTransformations(); t < sym.getNumTransformationsMirror(); t++) {
    auto v = clone();
    if (v->transform(t) && v->identicalInBB(op, true)) {
      return static_cast<unsigned char>(t);
    }
  }
  return 0;
}

// The original filled the hotspot and bounding-box halves of an entry
// separately, and for a transformation the shape cannot take its getHotspot
// reported success with the untransformed hotspot unless getBoundingBox had
// been asked first. Here both halves are filled together and an impossible
// transformation is "not defined" for both (see docs/handoff/stage-1.md).
void Voxel::fillTransformCache(unsigned char trans) const {
  int* e = &bbHsCache_[9 * std::size_t{trans}];
  if (e[0] != kCacheUninit && e[3] != kCacheUninit) {
    return;
  }

  auto tmp = clone();
  if (!tmp->transform(trans)) {
    e[0] = kCacheNotDef;
    e[3] = kCacheNotDef;
    return;
  }
  if (e[0] == kCacheUninit) {
    e[0] = tmp->getHx();
    e[1] = tmp->getHy();
    e[2] = tmp->getHz();
  }
  if (e[3] == kCacheUninit) {
    e[3] = asInt(tmp->boundX1());
    e[4] = asInt(tmp->boundX2());
    e[5] = asInt(tmp->boundY1());
    e[6] = asInt(tmp->boundY2());
    e[7] = asInt(tmp->boundZ1());
    e[8] = asInt(tmp->boundZ2());
  }
}

bool Voxel::getHotspot(unsigned char trans, int* x, int* y, int* z) const {
  BT_ASSERT(trans < gt_.getSymmetries().getNumTransformationsMirror());
  BT_ASSERT(x && y && z);

  fillTransformCache(trans);
  const int* e = &bbHsCache_[9 * std::size_t{trans}];
  if (e[0] == kCacheNotDef) {
    return false;
  }
  *x = e[0];
  *y = e[1];
  *z = e[2];
  return true;
}

bool Voxel::getBoundingBox(unsigned char trans, int* x1, int* y1, int* z1, int* x2, int* y2,
                           int* z2) const {
  BT_ASSERT(trans < gt_.getSymmetries().getNumTransformationsMirror());

  fillTransformCache(trans);
  const int* e = &bbHsCache_[9 * std::size_t{trans}];
  if (e[3] == kCacheNotDef) {
    return false;
  }
  if (x1) {
    *x1 = e[3];
  }
  if (x2) {
    *x2 = e[4];
  }
  if (y1) {
    *y1 = e[5];
  }
  if (y2) {
    *y2 = e[6];
  }
  if (z1) {
    *z1 = e[7];
  }
  if (z2) {
    *z2 = e[8];
  }
  return true;
}

void Voxel::warmCaches() const {
  (void)selfSymmetries();
  const unsigned int n = gt_.getSymmetries().getNumTransformationsMirror();
  for (unsigned int t = 0; t < n; t++) {
    fillTransformCache(static_cast<unsigned char>(t));
  }
}

void Voxel::resize(unsigned int nsx, unsigned int nsy, unsigned int nsz, VoxelType filler) {
  if (nsx == sx_ && nsy == sy_ && nsz == sz_) {
    return;
  }

  std::vector<VoxelType> s2(nsx * nsy * nsz, filler);
  std::vector<unsigned char> g2;
  if (!goalPiece_.empty()) {
    g2.assign(nsx * nsy * nsz, 0);
  }

  const unsigned int mx = std::min(sx_, nsx);
  const unsigned int my = std::min(sy_, nsy);
  const unsigned int mz = std::min(sz_, nsz);

  for (unsigned int x = 0; x < mx; x++) {
    for (unsigned int y = 0; y < my; y++) {
      for (unsigned int z = 0; z < mz; z++) {
        s2[x + nsx * (y + nsy * z)] = get(x, y, z);
        if (!g2.empty()) {
          g2[x + nsx * (y + nsy * z)] = static_cast<unsigned char>(getGoalPiece(x, y, z));
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

void Voxel::scale(unsigned int /*amount*/, bool /*grid*/) {
  // grids that cannot scale do nothing
}

bool Voxel::scaleDown(unsigned char /*by*/, bool /*action*/) { return false; }

unsigned int Voxel::count(VoxelType val) const {
  return static_cast<unsigned int>(std::count(space_.begin(), space_.end(), val));
}

unsigned int Voxel::countState(int state) const {
  unsigned int n = 0;
  for (unsigned int i = 0; i < voxels_; i++) {
    if ((get(i) & 3) == state) {
      n++;
    }
  }
  return n;
}

void Voxel::translate(int dx, int dy, int dz, VoxelType filler) {
  std::vector<VoxelType> s2(sx_ * sy_ * sz_, filler);
  std::vector<unsigned char> g2;
  if (!goalPiece_.empty()) {
    g2.assign(sx_ * sy_ * sz_, 0);
  }

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        const int tx = asInt(x) + dx;
        const int ty = asInt(y) + dy;
        const int tz = asInt(z) + dz;
        if (tx >= 0 && tx < asInt(sx_) && ty >= 0 && ty < asInt(sy_) && tz >= 0 &&
            tz < asInt(sz_)) {
          const unsigned int ni = asUnsigned(tx) + sx_ * (asUnsigned(ty) + sy_ * asUnsigned(tz));
          s2[ni] = get(x, y, z);
          if (!g2.empty()) {
            g2[ni] = static_cast<unsigned char>(getGoalPiece(x, y, z));
          }
        }
      }
    }
  }

  space_ = std::move(s2);
  if (!g2.empty()) {
    goalPiece_ = std::move(g2);
  }

  // cells may have been shifted out, so the box must be recalculated, not
  // just moved
  recalcBoundingBox();

  hx_ += dx;
  hy_ += dy;
  hz_ += dz;
}

// Union find (https://en.wikipedia.org/wiki/Disjoint-set_data_structure):
// one node per cell plus one for the outside; all neighbouring selected
// cells are merged. connected() then checks that all selected cells share a
// tree; fillHoles() merges the non-filled cells and fills those not in the
// outside's tree.
void Voxel::unionFind(std::vector<int>& tree, char type, bool inverse, VoxelType value,
                      bool outsideZ) const {
  std::fill(tree.begin(), tree.end(), -1);

  const bool mergeOutside = (inverse && value != 0) || (!inverse && value == 0);
  auto selected = [&](VoxelType v) { return inverse ? v != value : v == value; };
  auto root = [&tree](int n) {
    while (tree[static_cast<std::size_t>(n)] >= 0) {
      n = tree[static_cast<std::size_t>(n)];
    }
    return n;
  };

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        if (!validCoordinate(asInt(x), asInt(y), asInt(z)) || !selected(get(x, y, z))) {
          continue;
        }
        const int root1 = root(asInt(getIndex(x, y, z)));

        for (int curTyp = 0; curTyp <= type; curTyp++) {
          int x2 = 0;
          int y2 = 0;
          int z2 = 0;
          unsigned int idx = 0;
          while (getNeighbor(idx, asUnsigned(curTyp), asInt(x), asInt(y), asInt(z), &x2, &y2,
                             &z2)) {
            if (x2 >= 0 && x2 < asInt(sx_) && y2 >= 0 && y2 < asInt(sy_) && z2 >= 0 &&
                z2 < asInt(sz_)) {
              if (x2 < asInt(x) || y2 < asInt(y) || z2 < asInt(z)) {
                if (selected(get(asUnsigned(x2), asUnsigned(y2), asUnsigned(z2)))) {
                  const int root2 =
                      root(asInt(getIndex(asUnsigned(x2), asUnsigned(y2), asUnsigned(z2))));
                  if (root1 != root2) {
                    tree[static_cast<std::size_t>(root2)] = root1;
                  }
                }
              }
            } else if (mergeOutside) {
              if (outsideZ || (z2 >= 0 && z2 < asInt(sz_))) {
                const int root2 = root(asInt(voxels_));
                if (root1 != root2) {
                  tree[static_cast<std::size_t>(root2)] = root1;
                }
              }
            }
            idx++;
          }
        }
      }
    }
  }
}

bool Voxel::connected(char type, bool inverse, VoxelType value, bool outsideZ) const {
  // one node per cell plus one for the outside
  std::vector<int> tree(voxels_ + 1);
  unionFind(tree, type, inverse, value, outsideZ);

  auto root = [&tree](int n) {
    while (tree[static_cast<std::size_t>(n)] >= 0) {
      n = tree[static_cast<std::size_t>(n)];
    }
    return n;
  };

  const bool mergeOutside = (inverse && value != 0) || (!inverse && value == 0);
  int r = -1;

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        if (validCoordinate(asInt(x), asInt(y), asInt(z)) &&
            (inverse ? get(x, y, z) != value : get(x, y, z) == value)) {
          if (r == -1) {
            r = root(asInt(getIndex(x, y, z)));
          } else if (root(asInt(getIndex(x, y, z))) != r) {
            return false;
          }
        }
      }
    }
  }

  if (r != -1 && mergeOutside && root(asInt(voxels_)) != r) {
    return false;
  }
  return true;
}

void Voxel::fillHoles(char type) {
  std::vector<int> tree(voxels_ + 1);
  unionFind(tree, type, true, kFilled, true);

  auto root = [&tree](int n) {
    while (tree[static_cast<std::size_t>(n)] >= 0) {
      n = tree[static_cast<std::size_t>(n)];
    }
    return n;
  };

  const int outside = root(asInt(voxels_));

  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        if (validCoordinate(asInt(x), asInt(y), asInt(z)) && get(x, y, z) != kFilled) {
          if (root(asInt(getIndex(x, y, z))) != outside) {
            set(x, y, z, kFilled);
          }
        }
      }
    }
  }
}

void Voxel::setGoalPiece(unsigned int i, unsigned int pieceId) {
  if (i >= voxels_) {
    return;
  }
  if (goalPiece_.size() != voxels_) {
    goalPiece_.assign(voxels_, 0);
  }
  goalPiece_[i] = static_cast<unsigned char>(pieceId);
  if (pieceId == 0 && !hasGoalPieces()) {
    goalPiece_.clear();
  }
}

bool Voxel::hasGoalPieces() const {
  return std::any_of(goalPiece_.begin(), goalPiece_.end(), [](unsigned char c) { return c != 0; });
}

bool Voxel::neighbour(unsigned int p, VoxelType val) const {
  const unsigned int x = p % sx_;
  const unsigned int y = ((p - x) / sx_) % sy_;
  const unsigned int z = (((p - x) / sx_) - y) / sy_;

  BT_ASSERT(x + sx_ * (y + sy_ * z) == p);

  if (x > 0 && space_[p - 1] == val) {
    return true;
  }
  if (x < sx_ - 1 && space_[p + 1] == val) {
    return true;
  }
  if (y > 0 && space_[p - sx_] == val) {
    return true;
  }
  if (y < sy_ - 1 && space_[p + sx_] == val) {
    return true;
  }
  if (z > 0 && space_[p - sx_ * sy_] == val) {
    return true;
  }
  if (z < sz_ - 1 && space_[p + sx_ * sy_] == val) {
    return true;
  }
  return false;
}

Symmetry Voxel::selfSymmetries() const {
  if (isSymmetryInvalid(symmetries_)) {
    symmetries_ = gt_.getSymmetries().calculateSymmetry(*this);
  }
  return symmetries_;
}

unsigned char Voxel::normalizeTransformation(unsigned char trans) const {
  return gt_.getSymmetries().minimizeTransformation(selfSymmetries(), trans);
}

void Voxel::minimizePiece() {
  unsigned int x1 = getXYZ();
  unsigned int y1 = getXYZ();
  unsigned int z1 = getXYZ();
  unsigned int x2 = 0;
  unsigned int y2 = 0;
  unsigned int z2 = 0;

  for (unsigned int x = 0; x < getX(); x++) {
    for (unsigned int y = 0; y < getY(); y++) {
      for (unsigned int z = 0; z < getZ(); z++) {
        if (getState(x, y, z) != kEmpty) {
          x1 = std::min(x1, x);
          x2 = std::max(x2, x);
          y1 = std::min(y1, y);
          y2 = std::max(y2, y);
          z1 = std::min(z1, z);
          z2 = std::max(z2, z);
        }
      }
    }
  }

  // an empty space stays as it is
  if (x1 > x2) {
    return;
  }

  if (x1 != 0 || y1 != 0 || z1 != 0 || x2 != getX() - 1 || y2 != getY() - 1 || z2 != getZ() - 1) {
    translate(-asInt(x1), -asInt(y1), -asInt(z1), 0);
    resize(x2 - x1 + 1, y2 - y1 + 1, z2 - z1 + 1, 0);
  }
}

void Voxel::actionOnSpace(Action action, bool inside) {
  if (!getX() || !getY() || !getZ()) {
    return;
  }

  for (unsigned int x = 0; x < getX(); x++) {
    for (unsigned int y = 0; y < getY(); y++) {
      for (unsigned int z = 0; z < getZ(); z++) {
        if (getState(x, y, z) == kEmpty) {
          continue;
        }
        bool neighborEmpty = false;
        unsigned int idx = 0;
        int nx = 0;
        int ny = 0;
        int nz = 0;
        while (!neighborEmpty &&
               getNeighbor(idx, 0, asInt(x), asInt(y), asInt(z), &nx, &ny, &nz)) {
          neighborEmpty |= (getState2(nx, ny, nz) == kEmpty);
          idx++;
        }

        if (inside != neighborEmpty) {
          switch (action) {
            case Action::Fixed: setState(x, y, z, kFilled); break;
            case Action::Variable: setState(x, y, z, kVariable); break;
            case Action::Decolor: setColor(x, y, z, 0); break;
          }
        }
      }
    }
  }
}

void Voxel::setHotspot(int x, int y, int z) {
  hx_ = x;
  hy_ = y;
  hz_ = z;
  for (std::size_t i = 0; i < bbHsCache_.size(); i += 9) {
    bbHsCache_[i] = kCacheUninit;
  }
}

void Voxel::initHotspot() { setHotspot(0, 0, 0); }

bool Voxel::indexToXYZ(unsigned int index, unsigned int* x, unsigned int* y,
                       unsigned int* z) const {
  *x = index % sx_;
  index -= *x;
  index /= sx_;

  *y = index % sy_;
  index -= *y;
  index /= sy_;

  *z = index;
  return *z < sz_;
}

bool Voxel::unionintersect(const Voxel& va, int xa, int ya, int za, const Voxel& vb, int xb,
                           int yb, int zb) {
  // nothing to do when the two bounding boxes do not overlap
  if (asInt(va.bx1_) + xa > asInt(vb.bx2_) + xb || asInt(va.bx2_) + xa < asInt(vb.bx1_) + xb) {
    return false;
  }
  if (asInt(va.by1_) + ya > asInt(vb.by2_) + yb || asInt(va.by2_) + ya < asInt(vb.by1_) + yb) {
    return false;
  }
  if (asInt(va.bz1_) + za > asInt(vb.bz2_) + zb || asInt(va.bz2_) + za < asInt(vb.bz1_) + zb) {
    return false;
  }

  // make room for both spaces
  bool doResize = false;
  int nx = asInt(sx_);
  int ny = asInt(sy_);
  int nz = asInt(sz_);

  auto grow = [&doResize](int& n, int needed) {
    if (needed > n) {
      n = needed;
      doResize = true;
    }
  };
  grow(nx, xa + asInt(va.sx_));
  grow(ny, ya + asInt(va.sy_));
  grow(nz, za + asInt(va.sz_));
  grow(nx, xb + asInt(vb.sx_));
  grow(ny, yb + asInt(vb.sy_));
  grow(nz, zb + asInt(vb.sz_));

  if (doResize) {
    resize(asUnsigned(nx), asUnsigned(ny), asUnsigned(nz), kEmpty);
  }

  bool result = false;
  for (unsigned int x = 0; x < sx_; x++) {
    for (unsigned int y = 0; y < sy_; y++) {
      for (unsigned int z = 0; z < sz_; z++) {
        if (va.getState2(asInt(x) - xa, asInt(y) - ya, asInt(z) - za) == kFilled &&
            vb.getState2(asInt(x) - xb, asInt(y) - yb, asInt(z) - zb) == kFilled) {
          set(x, y, z, kFilled);
          result = true;
        }
      }
    }
  }
  return result;
}

}  // namespace bt
