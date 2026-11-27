// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationmoves_crowell.cpp.
#include "shapelogic/solve/rotation_moves_crowell.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_cache.h"

namespace bt {

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

// The moving group's cells and everyone else's, from the position's piece
// cells, and the rules prepared for them; once per group, the same for
// every axis, pivot and sense.
void RotationMovesCrowell::loadSubsetCells() {
  cachedStart_.clear();
  cachedOccupied_.clear();
  cachedMask_ = nextsubset_;
  cellsReady_ = true;

  for (unsigned int i = 0; i < pieceCells_.size(); i++) {
    std::vector<RotationRules::Cell>& to =
        (nextsubset_ & (1U << i)) != 0 ? cachedStart_ : cachedOccupied_;
    to.insert(to.end(), pieceCells_[i].begin(), pieceCells_[i].end());
  }

  if (cachedStart_.size() > 1) {
    rules_.setBodies(cachedOccupied_, cachedStart_);
  }
}

void RotationMovesCrowell::rebuildPivotCells(unsigned int axis) {
  pivotCells_.clear();

  if (cachedStart_.empty()) {
    return;
  }

  const TurnBox box = turnBox(cachedStart_, axis);
  const int umin = box.umin;
  const int umax = box.umax;
  const int vmin = box.vmin;
  const int vmax = box.vmax;
  const int da = 2 * box.amin;

  // the Fortran solver's SimpleRot with Rmult 1: cell centers, south-west
  // corners, and the far edges in u and v (not Classic's dense half grid)
  for (int u = umin; u <= umax; u++) {
    for (int v = vmin; v <= vmax; v++) {
      pivotCells_.push_back(pivotAt(axis, 2 * u, 2 * v, da));
      pivotCells_.push_back(pivotAt(axis, 2 * u - 1, 2 * v - 1, da));
    }
    pivotCells_.push_back(pivotAt(axis, 2 * u - 1, 2 * vmax + 1, da));
  }
  for (int v = vmin; v <= vmax; v++) {
    pivotCells_.push_back(pivotAt(axis, 2 * umax + 1, 2 * v - 1, da));
  }
  pivotCells_.push_back(pivotAt(axis, 2 * umax + 1, 2 * vmax + 1, da));
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

    if (rules_.preparedAxisBlocked(nextaxis_)) {
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

  // startAxis() found the axis free of the sandwich and the walls across
  if (!rules_.allowPrepared(pivot, nextaxis_, nextsense_, true)) {
    return nullptr;
  }

  return turnedNode(searchnode_, sym_, nextsubset_, pivot, nextaxis_, nextsense_);
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

  if (!active_) {
    return;
  }

  // each piece's cells at this position, once; one that left has none
  pieceCells_.resize(pcs.size());
  for (unsigned int i = 0; i < pcs.size(); i++) {
    if (nd->isPieceRemoved(i)) {
      pieceCells_[i].clear();
    } else {
      collectWorldCells(cache_, *nd, pcs, i, pieceCells_[i]);
    }
  }

  // the largest piece left (the last of the largest) never turns
  largestIdx_ = 0;
  std::size_t best = 0;
  bool have = false;
  for (unsigned int i = 0; i < pcs.size(); i++) {
    if (nd->isPieceRemoved(i)) {
      continue;
    }
    const std::size_t count = pieceCells_[i].size();
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
