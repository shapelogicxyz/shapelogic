// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The complete 90 degree generator of BurrTools Classic (and BurrTools 2):
/// every group of pieces, as one body, about every pivot near it.
/// Ported from src/lib/rotationmoves_0.{h,cpp}.

#include "bt/solve/rotation_moves.h"
#include "bt/solve/rotation_rules.h"

#include <vector>

namespace bt {

class MovementCache;
class Problem;
class Puzzle;
class Symmetries;

/// Turns of single pieces and of groups (pieces moving as one), about one
/// pivot, axis and sense. Pivots are cell centres, empty cells in the
/// moving body's box, and faces, edges and corners in the plane of the turn
/// (the Fortran solver's SimpleRot), in doubled cell units. A single unit
/// cube is never turned (that frees nothing).
class RotationMovesClassic final : public RotationMoves {
 public:
  /// Options of the A/B switches (BURRTOOLS_NO_ROT_FAST and
  /// BURRTOOLS_NO_ROT_ANCHOR of the original).
  struct Options {
    /// Ask the rules through setBodies/allowPrepared and skip the pivots
    /// that cannot work; off asks allowRotation for every one (the same
    /// turns in the same order).
    bool fast = true;
    /// Keep the first piece's orientation: turn whichever side does not
    /// hold it, so an arrangement is one position however the whole puzzle
    /// lies (off: up to 24 positions for one arrangement).
    bool anchor = true;
  };

  /// A generator for `problem`'s pieces, with the shapes from `cache`; all
  /// must outlive it.
  RotationMovesClassic(const Puzzle& puzzle, MovementCache& cache, Options options);

  void initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pcs) override;
  NodeRef find() override;

 private:
  void collectWorldCells(unsigned int pieceIdx, std::vector<RotationRules::Cell>& out) const;
  void loadSubset(unsigned int subsetMask);
  void rebuildPivotCells(unsigned int axis);
  NodeRef tryCurrentCandidate();

  MovementCache& cache_;
  const Symmetries& sym_;
  RotationRules rules_;
  Options options_;

  // where find() stands
  DisassemblerNode* searchnode_ = nullptr;
  const std::vector<unsigned int>* pieces_ = nullptr;
  unsigned int nextsubset_ = 1;
  int nextpivot_ = 0;
  unsigned int nextaxis_ = 0;
  unsigned int nextsense_ = 0;
  bool active_ = false;

  std::vector<RotationRules::Pivot> pivotCells_;
  /// The moving group's cells and everyone else's, for the current position
  /// and group (the same for every pivot, axis and sense).
  std::vector<RotationRules::Cell> subsetStart_;
  std::vector<RotationRules::Cell> subsetOccupied_;
  /// Each piece's cells at the current position; empty for one that left.
  std::vector<std::vector<RotationRules::Cell>> pieceCells_;
  /// The pieces still in the puzzle, one bit each.
  unsigned int presentMask_ = 0;
};

/// The turn of a vector by 90 degrees around `axis` (0 x, 1 y, 2 z), sense 0
/// +90 degrees, 1 -90 degrees.
void rotateVector90(int* x, int* y, int* z, unsigned int axis, unsigned int sense);

/// A cell's position turned about `pivot` (doubled units): false when it
/// does not land on the grid.
bool rotateDoubled(int* x, int* y, int* z, const RotationRules::Pivot& pivot, unsigned int axis,
                   unsigned int sense);

/// The cube orientation of a +-90 degree turn about an axis (tabs_0's
/// rotation matrices).
unsigned char rotationTransformId(unsigned int axis, unsigned int sense);

}  // namespace bt
