// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationmoves_crowell.cpp.
#include "shapelogic/solve/rotation_moves_crowell.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/rotation_moves_classic.h"

namespace bt {

namespace {

bool rotateCellCrowell(const RotationRules::Cell& cell, const RotationRules::Pivot& pivot,
                       unsigned int axis, unsigned int sense, RotationRules::Cell& out) {
  int x = cell.x;
  int y = cell.y;
  int z = cell.z;
  if (!rotateDoubled(&x, &y, &z, pivot, axis, sense)) {
    return false;
  }
  out = RotationRules::Cell(x, y, z);
  return true;
}

}  // namespace

RotationMovesCrowell::RotationMovesCrowell(const Puzzle& puzzle, MovementCache& cache)
    : cache_(cache), sym_(puzzle.getGridType().getSymmetries()) {}

// The next group after `mask` without the largest piece; 0 when through.
unsigned int RotationMovesCrowell::nextSubsetMask(unsigned int mask, unsigned int n) const {
  const unsigned int allMask = (n >= 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);
  const unsigned int largestBit = (largestIdx_ < n) ? (1U << largestIdx_) : 0U;

  do {
    mask++;
    if (mask > allMask) {
      return 0;
    }
  } while ((mask & largestBit) != 0);

  return mask;
}

void RotationMovesCrowell::collectWorldCells(unsigned int pieceIdx,
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

unsigned int RotationMovesCrowell::countFilled(unsigned int pieceIdx) const {
  unsigned int n = 0;
  const unsigned int pieceId = (*pieces_)[pieceIdx];
  const unsigned int shapeId = cache_.getShapeOfPiece(pieceId);
  const auto trans = static_cast<unsigned char>(searchnode_->getTrans(pieceIdx));
  const Voxel& sh = cache_.getTransformedShape(shapeId, trans);

  for (unsigned int z = 0; z < sh.getZ(); z++) {
    for (unsigned int y = 0; y < sh.getY(); y++) {
      for (unsigned int x = 0; x < sh.getX(); x++) {
        if (sh.isFilled(x, y, z)) {
          n++;
        }
      }
    }
  }
  return n;
}

void RotationMovesCrowell::loadSubsetCells() {
  cachedStart_.clear();
  cachedOccupied_.clear();
  cachedMask_ = nextsubset_;
  cellsReady_ = true;

  if (pieces_ == nullptr || searchnode_ == nullptr) {
    return;
  }

  std::vector<RotationRules::Cell> cells;
  for (unsigned int i = 0; i < pieces_->size(); i++) {
    if (searchnode_->isPieceRemoved(i)) {
      continue;
    }

    collectWorldCells(i, cells);
    std::vector<RotationRules::Cell>& to =
        (nextsubset_ & (1U << i)) != 0 ? cachedStart_ : cachedOccupied_;
    to.insert(to.end(), cells.begin(), cells.end());
  }
}

void RotationMovesCrowell::rebuildPivotCells(unsigned int axis) {
  pivotCells_.clear();

  if (cachedStart_.empty()) {
    return;
  }

  int umin = 0;
  int umax = 0;
  int vmin = 0;
  int vmax = 0;
  int amin = 0;
  if (axis == 0) {
    umin = umax = cachedStart_[0].y;
    vmin = vmax = cachedStart_[0].z;
    amin = cachedStart_[0].x;
  } else if (axis == 1) {
    umin = umax = cachedStart_[0].x;
    vmin = vmax = cachedStart_[0].z;
    amin = cachedStart_[0].y;
  } else {
    umin = umax = cachedStart_[0].x;
    vmin = vmax = cachedStart_[0].y;
    amin = cachedStart_[0].z;
  }

  for (std::size_t c = 1; c < cachedStart_.size(); c++) {
    const RotationRules::Cell& p = cachedStart_[c];
    const int u = (axis == 0) ? p.y : p.x;
    const int v = (axis == 2) ? p.y : p.z;
    const int a = (axis == 0) ? p.x : ((axis == 1) ? p.y : p.z);
    umin = u < umin ? u : umin;
    umax = u > umax ? u : umax;
    vmin = v < vmin ? v : vmin;
    vmax = v > vmax ? v : vmax;
    amin = a < amin ? a : amin;
  }

  const int da = 2 * amin;

  // a pivot from (u, v) in the plane and the layer along the axis
  auto pivotAt = [axis, da](int u, int v) {
    if (axis == 0) {
      return RotationRules::Pivot(da, u, v);
    }
    if (axis == 1) {
      return RotationRules::Pivot(u, da, v);
    }
    return RotationRules::Pivot(u, v, da);
  };

  // the Fortran solver's SimpleRot with Rmult 1: cell centres, south-west
  // corners, and the far edges in u and v (not Classic's dense half grid)
  for (int u = umin; u <= umax; u++) {
    for (int v = vmin; v <= vmax; v++) {
      pivotCells_.push_back(pivotAt(2 * u, 2 * v));
      pivotCells_.push_back(pivotAt(2 * u - 1, 2 * v - 1));
    }
    pivotCells_.push_back(pivotAt(2 * u - 1, 2 * vmax + 1));
  }
  for (int v = vmin; v <= vmax; v++) {
    pivotCells_.push_back(pivotAt(2 * umax + 1, 2 * v - 1));
  }
  pivotCells_.push_back(pivotAt(2 * umax + 1, 2 * vmax + 1));
}

void RotationMovesCrowell::startAxis() {
  const auto n = static_cast<unsigned int>(pieces_->size());

  while (active_) {
    if (!cellsReady_ || cachedMask_ != nextsubset_) {
      loadSubsetCells();
    }

    if (cachedStart_.size() <= 1) {
      nextsubset_ = nextSubsetMask(nextsubset_, n);
      cellsReady_ = false;
      nextaxis_ = 0;
      nextpivot_ = 0;
      nextsense_ = 0;
      if (nextsubset_ == 0) {
        active_ = false;
      }
      continue;
    }

    if (rules_.axisBlocked(cachedOccupied_, cachedStart_, nextaxis_)) {
      nextaxis_++;
      nextpivot_ = 0;
      nextsense_ = 0;
      if (nextaxis_ >= 3) {
        nextaxis_ = 0;
        nextsubset_ = nextSubsetMask(nextsubset_, n);
        cellsReady_ = false;
        if (nextsubset_ == 0) {
          active_ = false;
        }
      }
      continue;
    }

    rebuildPivotCells(nextaxis_);
    if (pivotCells_.empty()) {
      nextaxis_++;
      nextpivot_ = 0;
      nextsense_ = 0;
      if (nextaxis_ >= 3) {
        nextaxis_ = 0;
        nextsubset_ = nextSubsetMask(nextsubset_, n);
        cellsReady_ = false;
        if (nextsubset_ == 0) {
          active_ = false;
        }
      }
      continue;
    }
    return;
  }
}

void RotationMovesCrowell::advanceCandidate() {
  nextsense_++;
  if (nextsense_ >= 2) {
    nextsense_ = 0;
    nextpivot_++;
    if (static_cast<unsigned int>(nextpivot_) >= pivotCells_.size()) {
      nextpivot_ = 0;
      nextaxis_++;
      if (nextaxis_ >= 3) {
        nextaxis_ = 0;
        nextsubset_ = nextSubsetMask(nextsubset_, static_cast<unsigned int>(pieces_->size()));
        cellsReady_ = false;
        if (nextsubset_ == 0) {
          active_ = false;
          return;
        }
      }
      startAxis();
    }
  }
}

bool RotationMovesCrowell::skipCurrentDirection() const {
  const unsigned int dir = nextaxis_ * 2 + nextsense_;
  if (dirCounts_[dir] >= kDctMax) {
    return true;
  }

  if (skipReverse_ && nextaxis_ == skipRevAxis_ && nextsense_ == skipRevSense_) {
    const unsigned int rp = searchnode_->getRotPiece();
    if (rp < pieces_->size() && (nextsubset_ & (1U << rp)) != 0) {
      return true;
    }
  }

  return false;
}

NodeRef RotationMovesCrowell::tryCurrentCandidate() {
  if (static_cast<unsigned int>(nextpivot_) >= pivotCells_.size()) {
    return nullptr;
  }

  if (!cellsReady_ || cachedMask_ != nextsubset_) {
    loadSubsetCells();
  }

  if (cachedStart_.size() <= 1) {
    return nullptr;
  }

  const RotationRules::Pivot pivot = pivotCells_[static_cast<std::size_t>(nextpivot_)];

  std::vector<RotationRules::Cell> combinedEnd;
  combinedEnd.reserve(cachedStart_.size());

  for (const RotationRules::Cell& c : cachedStart_) {
    RotationRules::Cell endCell;
    if (!rotateCellCrowell(c, pivot, nextaxis_, nextsense_, endCell)) {
      return nullptr;
    }
    combinedEnd.push_back(endCell);
  }

  if (!rules_.allowRotation(cachedOccupied_, cachedStart_, combinedEnd, pivot, nextaxis_,
                            nextsense_)) {
    return nullptr;
  }

  const auto n = static_cast<unsigned int>(pieces_->size());

  unsigned int primaryPiece = 0;
  while (primaryPiece < n && (nextsubset_ & (1U << primaryPiece)) == 0) {
    primaryPiece++;
  }
  BT_ASSERT(primaryPiece < n);

  const unsigned char rotId = rotationTransformId(nextaxis_, nextsense_);
  bool changed = false;

  const unsigned int dir = DisassemblerNode::kRotationDirBase + nextaxis_ * 2 + nextsense_;
  NodeRef node = NodeRef::make(n, NodeRef::share(searchnode_), static_cast<int>(dir), 1);
  node->setRotationInfo(primaryPiece, pivot.hx, pivot.hy, pivot.hz);

  for (unsigned int i = 0; i < n; i++) {
    if (searchnode_->isPieceRemoved(i)) {
      node->set(i, searchnode_->getX(i), searchnode_->getY(i), searchnode_->getZ(i),
                searchnode_->getTrans(i));
      continue;
    }

    if ((nextsubset_ & (1U << i)) != 0) {
      const auto oldTrans = static_cast<unsigned char>(searchnode_->getTrans(i));
      const unsigned char newTrans = sym_.transAdd(oldTrans, rotId);

      int px = searchnode_->getX(i);
      int py = searchnode_->getY(i);
      int pz = searchnode_->getZ(i);
      if (!rotateDoubled(&px, &py, &pz, pivot, nextaxis_, nextsense_)) {
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

void RotationMovesCrowell::initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pcs) {
  searchnode_ = nd;
  pieces_ = &pcs;
  nextpivot_ = 0;
  nextaxis_ = 0;
  nextsense_ = 0;
  cellsReady_ = false;
  cachedMask_ = 0;
  active_ = pcs.size() > 1 && pcs.size() <= kMaxTurnPieces;

  dirCounts_.fill(0);

  // the largest piece left (the last of the largest) never turns
  largestIdx_ = 0;
  unsigned int best = 0;
  bool have = false;
  for (unsigned int i = 0; i < pcs.size(); i++) {
    if (nd->isPieceRemoved(i)) {
      continue;
    }
    const unsigned int count = countFilled(i);
    if (!have || count >= best) {
      best = count;
      largestIdx_ = i;
      have = true;
    }
  }

  // no turning straight back
  skipReverse_ = false;
  if (nd->isRotationMove()) {
    const unsigned int code = nd->getDirection() - DisassemblerNode::kRotationDirBase;
    skipRevAxis_ = code / 2;
    skipRevSense_ = 1U - (code % 2);
    skipReverse_ = true;
  }

  nextsubset_ = 1;
  const auto n = static_cast<unsigned int>(pcs.size());
  if (active_ && (nextsubset_ & (1U << largestIdx_)) != 0) {
    nextsubset_ = nextSubsetMask(0, n);
  }

  if (nextsubset_ == 0) {
    active_ = false;
  }

  if (active_) {
    startAxis();
  }
}

NodeRef RotationMovesCrowell::find() {
  while (active_) {
    if (stopRequested()) {
      return nullptr;
    }

    if (skipCurrentDirection()) {
      advanceCandidate();
      continue;
    }

    NodeRef node = tryCurrentCandidate();

    if (node) {
      dirCounts_[nextaxis_ * 2 + nextsense_]++;
    }

    advanceCandidate();

    if (node) {
      return node;
    }
  }

  return nullptr;
}

}  // namespace bt
