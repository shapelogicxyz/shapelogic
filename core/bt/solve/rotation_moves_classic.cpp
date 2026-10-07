// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationmoves_0.cpp.
#include "bt/solve/rotation_moves_classic.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/model/puzzle.h"
#include "bt/solve/movement_cache.h"

namespace bt {

namespace {

// Cube orientations of +-90 degrees about x, y and z (tabs_0/rotmatrix.inc).
constexpr unsigned char kRotXP90 = 1;
constexpr unsigned char kRotXM90 = 3;
constexpr unsigned char kRotYP90 = 12;
constexpr unsigned char kRotYM90 = 4;
constexpr unsigned char kRotZP90 = 16;
constexpr unsigned char kRotZM90 = 20;

// The next group of pieces after `mask`, 0 when through; never all of them
// (turning every piece leaves the arrangement as it is).
unsigned int nextSubsetMask(unsigned int mask, unsigned int n) {
  const unsigned int allMask = (n >= 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);

  do {
    mask++;
    if (mask > allMask) {
      return 0;
    }
  } while (mask == allMask);

  return mask;
}

}  // namespace

void rotateVector90(int* x, int* y, int* z, unsigned int axis, unsigned int sense) {
  const int ox = *x;
  const int oy = *y;
  const int oz = *z;

  if (axis == 0) {
    if (sense == 0) {  // +90 x
      *x = ox;
      *y = -oz;
      *z = oy;
    } else {  // -90 x
      *x = ox;
      *y = oz;
      *z = -oy;
    }
  } else if (axis == 1) {
    if (sense == 0) {  // +90 y
      *x = oz;
      *y = oy;
      *z = -ox;
    } else {  // -90 y
      *x = -oz;
      *y = oy;
      *z = ox;
    }
  } else {
    if (sense == 0) {  // +90 z
      *x = -oy;
      *y = ox;
      *z = oz;
    } else {  // -90 z
      *x = oy;
      *y = -ox;
      *z = oz;
    }
  }
}

bool rotateDoubled(int* x, int* y, int* z, const RotationRules::Pivot& pivot, unsigned int axis,
                   unsigned int sense) {
  int dx = (*x) * 2 - pivot.hx;
  int dy = (*y) * 2 - pivot.hy;
  int dz = (*z) * 2 - pivot.hz;

  rotateVector90(&dx, &dy, &dz, axis, sense);

  const int nx = pivot.hx + dx;
  const int ny = pivot.hy + dy;
  const int nz = pivot.hz + dz;
  if (((nx | ny | nz) & 1) != 0) {
    return false;
  }

  *x = nx / 2;
  *y = ny / 2;
  *z = nz / 2;
  return true;
}

unsigned char rotationTransformId(unsigned int axis, unsigned int sense) {
  static constexpr unsigned char kIds[3][2] = {
      {kRotXP90, kRotXM90}, {kRotYP90, kRotYM90}, {kRotZP90, kRotZM90}};
  BT_ASSERT(axis < 3 && sense < 2);
  return kIds[axis][sense];
}

RotationMovesClassic::RotationMovesClassic(const Puzzle& puzzle, MovementCache& cache,
                                           Options options)
    : cache_(cache), sym_(puzzle.getGridType().getSymmetries()), options_(options) {}

void RotationMovesClassic::collectWorldCells(unsigned int pieceIdx,
                                             std::vector<RotationRules::Cell>& out) const {
  out.clear();

  const unsigned int pieceId = (*pieces_)[pieceIdx];
  const unsigned int shapeId = cache_.getShapeOfPiece(pieceId);
  const auto trans = static_cast<unsigned char>(searchnode_->getTrans(pieceIdx));
  const Voxel& sh = cache_.getTransformedShape(shapeId, trans);

  const int px = searchnode_->getX(pieceIdx);
  const int py = searchnode_->getY(pieceIdx);
  const int pz = searchnode_->getZ(pieceIdx);
  const int hx = sh.getHx();
  const int hy = sh.getHy();
  const int hz = sh.getHz();

  for (unsigned int z = 0; z < sh.getZ(); z++) {
    for (unsigned int y = 0; y < sh.getY(); y++) {
      for (unsigned int x = 0; x < sh.getX(); x++) {
        if (sh.isFilled(x, y, z)) {
          out.emplace_back(px - hx + asInt(x), py - hy + asInt(y), pz - hz + asInt(z));
        }
      }
    }
  }
}

// The moving group's cells and everyone else's, from the position's piece
// cells; once per group, the same for every axis, pivot and sense.
void RotationMovesClassic::loadSubset(unsigned int subsetMask) {
  subsetStart_.clear();
  subsetOccupied_.clear();
  for (unsigned int i = 0; i < pieceCells_.size(); i++) {
    std::vector<RotationRules::Cell>& to =
        (subsetMask & (1U << i)) != 0 ? subsetStart_ : subsetOccupied_;
    to.insert(to.end(), pieceCells_[i].begin(), pieceCells_[i].end());
  }

  if (options_.fast && subsetStart_.size() > 1) {
    rules_.setBodies(subsetOccupied_, subsetStart_);
  }
}

void RotationMovesClassic::rebuildPivotCells(unsigned int axis) {
  pivotCells_.clear();

  const std::vector<RotationRules::Cell>& moving = subsetStart_;

  // nothing to turn, or a lone unit cube, which cannot free itself by
  // spinning in place
  if (moving.size() <= 1) {
    return;
  }

  // pinched or walled in on this axis whatever the pivot: no candidates
  if (options_.fast && rules_.preparedAxisBlocked(axis)) {
    return;
  }

  int umin = 0;
  int umax = 0;
  int vmin = 0;
  int vmax = 0;
  int amin = 0;
  if (axis == 0) {
    umin = umax = moving[0].y;
    vmin = vmax = moving[0].z;
    amin = moving[0].x;
  } else if (axis == 1) {
    umin = umax = moving[0].x;
    vmin = vmax = moving[0].z;
    amin = moving[0].y;
  } else {
    umin = umax = moving[0].x;
    vmin = vmax = moving[0].y;
    amin = moving[0].z;
  }

  for (std::size_t c = 1; c < moving.size(); c++) {
    const RotationRules::Cell& p = moving[c];
    const int u = (axis == 0) ? p.y : p.x;
    const int v = (axis == 2) ? p.y : p.z;
    const int a = (axis == 0) ? p.x : ((axis == 1) ? p.y : p.z);
    umin = u < umin ? u : umin;
    umax = u > umax ? u : umax;
    vmin = v < vmin ? v : vmin;
    vmax = v > vmax ? v : vmax;
    amin = a < amin ? a : amin;
  }

  // the half grid in the plane over the body's box: cell centres, faces,
  // edges and corners; along the axis, the moving body's lowest layer
  const int du0 = 2 * umin - 1;
  const int du1 = 2 * umax + 1;
  const int dv0 = 2 * vmin - 1;
  const int dv1 = 2 * vmax + 1;
  const int da = 2 * amin;

  for (int du = du0; du <= du1; du++) {
    for (int dv = dv0; dv <= dv1; dv++) {
      // a quarter turn lands on the grid only about a cell centre or a
      // corner: in-plane coordinates both even or both odd
      if (options_.fast && ((du ^ dv) & 1) != 0) {
        continue;
      }
      RotationRules::Pivot p;
      if (axis == 0) {
        p.hx = da;
        p.hy = du;
        p.hz = dv;
      } else if (axis == 1) {
        p.hx = du;
        p.hy = da;
        p.hz = dv;
      } else {
        p.hx = du;
        p.hy = dv;
        p.hz = da;
      }
      pivotCells_.push_back(p);
    }
  }
}

NodeRef RotationMovesClassic::tryCurrentCandidate() {
  if (static_cast<unsigned int>(nextpivot_) >= pivotCells_.size()) {
    return nullptr;
  }

  const unsigned int subsetMask = nextsubset_;
  const RotationRules::Pivot pivot = pivotCells_[static_cast<std::size_t>(nextpivot_)];

  if (options_.fast) {
    if (!rules_.allowPrepared(pivot, nextaxis_, nextsense_, true)) {
      return nullptr;
    }
  } else {
    const std::vector<RotationRules::Cell>& combinedStart = subsetStart_;

    std::vector<RotationRules::Cell> combinedEnd;
    combinedEnd.reserve(combinedStart.size());

    for (const RotationRules::Cell& c : combinedStart) {
      RotationRules::Cell endCell;
      if (!RotationRules::rotateCell(c, pivot, nextaxis_, nextsense_, endCell)) {
        return nullptr;
      }
      combinedEnd.push_back(endCell);
    }

    if (!rules_.allowRotation(subsetOccupied_, combinedStart, combinedEnd, pivot, nextaxis_,
                              nextsense_)) {
      return nullptr;
    }
  }

  // Turning a group one way about a pivot and everything else the other
  // way leave the pieces in the same places relative to each other. With
  // the anchor the position keeps the first piece's orientation and turns
  // whichever side does not hold it, so an arrangement is one position
  // however the whole puzzle is turned. The rules above still judged the
  // group as asked.
  unsigned int moveMask = subsetMask;
  unsigned int sense = nextsense_;
  if (options_.anchor && (subsetMask & 1U) != 0) {
    moveMask = presentMask_ & ~subsetMask;
    sense = 1 - nextsense_;
  }

  const auto n = static_cast<unsigned int>(pieces_->size());

  unsigned int primaryPiece = 0;
  while (primaryPiece < n && (moveMask & (1U << primaryPiece)) == 0) {
    primaryPiece++;
  }
  BT_ASSERT(primaryPiece < n);

  const unsigned char rotId = rotationTransformId(nextaxis_, sense);
  bool changed = false;

  const unsigned int dir = DisassemblerNode::kRotationDirBase + nextaxis_ * 2 + sense;
  NodeRef node =
      NodeRef::make(n, NodeRef::share(searchnode_), static_cast<int>(dir), 1);
  node->setRotationInfo(primaryPiece, pivot.hx, pivot.hy, pivot.hz);

  for (unsigned int i = 0; i < n; i++) {
    if (searchnode_->isPieceRemoved(i)) {
      node->set(i, searchnode_->getX(i), searchnode_->getY(i), searchnode_->getZ(i),
                searchnode_->getTrans(i));
      continue;
    }

    if ((moveMask & (1U << i)) != 0) {
      const auto oldTrans = static_cast<unsigned char>(searchnode_->getTrans(i));
      const unsigned char newTrans = sym_.transAdd(oldTrans, rotId);

      int px = searchnode_->getX(i);
      int py = searchnode_->getY(i);
      int pz = searchnode_->getZ(i);
      if (!rotateDoubled(&px, &py, &pz, pivot, nextaxis_, sense)) {
        return nullptr;
      }

      if (px != searchnode_->getX(i) || py != searchnode_->getY(i) ||
          pz != searchnode_->getZ(i) || newTrans != oldTrans) {
        changed = true;
      }

      node->set(i, px, py, pz, newTrans);
    } else {
      node->set(i, searchnode_->getX(i), searchnode_->getY(i), searchnode_->getZ(i),
                searchnode_->getTrans(i));
    }
  }

  if (!changed) {
    return nullptr;
  }

  return node;
}

void RotationMovesClassic::initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pcs) {
  searchnode_ = nd;
  pieces_ = &pcs;
  nextsubset_ = !pcs.empty() ? 1U : 0U;
  nextpivot_ = 0;
  nextaxis_ = 0;
  nextsense_ = 0;
  active_ = !pcs.empty() && pcs.size() <= kMaxTurnPieces;
  if (!active_) {
    return;
  }

  // each piece's cells at this position, once; one that left has none
  pieceCells_.resize(pcs.size());
  presentMask_ = 0;
  for (unsigned int i = 0; i < pcs.size(); i++) {
    if (nd->isPieceRemoved(i)) {
      pieceCells_[i].clear();
    } else {
      collectWorldCells(i, pieceCells_[i]);
      presentMask_ |= 1U << i;
    }
  }

  if (active_) {
    loadSubset(nextsubset_);
    rebuildPivotCells(nextaxis_);
  }
}

NodeRef RotationMovesClassic::find() {
  if (!active_) {
    return nullptr;
  }

  const auto n = static_cast<unsigned int>(pieces_->size());

  while (true) {
    if (stopRequested()) {
      return nullptr;
    }

    NodeRef node = tryCurrentCandidate();

    nextsense_++;
    if (nextsense_ >= 2 || pivotCells_.empty()) {
      nextsense_ = 0;
      nextpivot_++;
      if (static_cast<unsigned int>(nextpivot_) >= pivotCells_.size()) {
        nextpivot_ = 0;
        nextaxis_++;
        if (nextaxis_ >= 3) {
          nextaxis_ = 0;
          nextsubset_ = nextSubsetMask(nextsubset_, n);
          if (nextsubset_ == 0) {
            active_ = false;
            return node;
          }
          loadSubset(nextsubset_);
        }
        rebuildPivotCells(nextaxis_);
      }
    }

    if (node) {
      return node;
    }
  }
}

}  // namespace bt
