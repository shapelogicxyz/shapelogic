// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Andrew Crowell's 90 degree generator (the SolveSlidingCube Fortran
/// solver's SimpleRot and PieceBuilder): fewer groups, fewer pivots, at
/// most three turns per direction. Not complete; kept apart so that Classic
/// stays complete. What it does and what is left of the Fortran solver:
/// CROWELL.md beside this file.
/// Ported from src/lib/rotationmoves_crowell.{h,cpp}.

#include "shapelogic/solve/rotation_moves.h"
#include "shapelogic/solve/rotation_rules.h"

#include <array>
#include <vector>

namespace bt {

class MovementCache;
class Puzzle;
class Symmetries;

/// The Fortran-style generator. The groups turned are those without the
/// largest piece; pivots are cell centres, south-west corners and the far
/// edges of the moving body's box; a turn back the way the last one came is
/// skipped.
class RotationMovesCrowell final : public RotationMoves {
 public:
  /// Turns accepted per direction (the Fortran solver's DctMax).
  static constexpr unsigned int kDctMax = 3;

  RotationMovesCrowell(const Puzzle& puzzle, MovementCache& cache);

  void initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pcs) override;
  NodeRef find() override;

 private:
  void collectWorldCells(unsigned int pieceIdx, std::vector<RotationRules::Cell>& out) const;
  [[nodiscard]] unsigned int countFilled(unsigned int pieceIdx) const;
  void loadSubsetCells();
  void rebuildPivotCells(unsigned int axis);
  void startAxis();
  void advanceCandidate();
  [[nodiscard]] bool skipCurrentDirection() const;
  NodeRef tryCurrentCandidate();
  [[nodiscard]] unsigned int nextSubsetMask(unsigned int mask, unsigned int n) const;

  MovementCache& cache_;
  const Symmetries& sym_;
  RotationRules rules_;

  DisassemblerNode* searchnode_ = nullptr;
  const std::vector<unsigned int>* pieces_ = nullptr;
  unsigned int nextsubset_ = 1;
  int nextpivot_ = 0;
  unsigned int nextaxis_ = 0;
  unsigned int nextsense_ = 0;
  bool active_ = false;
  unsigned int largestIdx_ = 0;
  std::array<unsigned int, 6> dirCounts_ = {};
  unsigned int skipRevAxis_ = 0;
  unsigned int skipRevSense_ = 0;
  bool skipReverse_ = false;

  std::vector<RotationRules::Pivot> pivotCells_;
  std::vector<RotationRules::Cell> cachedStart_;
  std::vector<RotationRules::Cell> cachedOccupied_;
  unsigned int cachedMask_ = 0;
  bool cellsReady_ = false;
};

}  // namespace bt
