// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationmoves_0.cpp.
#include "shapelogic/solve/rotation_moves_classic.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_cache.h"

namespace sl {

namespace {

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

RotationMovesClassic::RotationMovesClassic(const Puzzle& puzzle, MovementCache& cache,
                                           Options options)
    : cache_(cache), sym_(puzzle.getGridType().getSymmetries()), options_(options) {}

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

  const TurnBox box = turnBox(moving, axis);

  // the half grid in the plane over the body's box: cell centers, faces,
  // edges and corners; along the axis, the moving body's lowest layer
  const int du0 = 2 * box.umin - 1;
  const int du1 = 2 * box.umax + 1;
  const int dv0 = 2 * box.vmin - 1;
  const int dv1 = 2 * box.vmax + 1;
  const int da = 2 * box.amin;

  for (int du = du0; du <= du1; du++) {
    for (int dv = dv0; dv <= dv1; dv++) {
      // a quarter turn lands on the grid only about a cell center or a
      // corner: in-plane coordinates both even or both odd
      if (options_.fast && ((du ^ dv) & 1) != 0) {
        continue;
      }
      pivotCells_.push_back(pivotAt(axis, du, dv, da));
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

  return turnedNode(searchnode_, sym_, moveMask, pivot, nextaxis_, sense);
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
      collectWorldCells(cache_, *nd, pcs, i, pieceCells_[i]);
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

}  // namespace sl
