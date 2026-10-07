// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The 90 degree turns a take-apart may make on the cube grid: a generator
/// hands them out one by one, as new positions. Classic tries every group
/// of pieces about every pivot (RotationMovesClassic); Andrew Crowell's
/// heuristics try fewer (RotationMovesCrowell). The movement analysis talks
/// to this interface only.
/// Ported from src/lib/rotationmoves.h.

#include "bt/base/stop.h"
#include "bt/solve/disassembler_node.h"

#include <vector>

namespace bt {

/// A generator of 90 degree turns from one position.
class RotationMoves {
 public:
  virtual ~RotationMoves() = default;

  /// Starts with position `nd` of the pieces `pcs` (both must stay until
  /// the next initFind).
  virtual void initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pcs) = 0;

  /// The next turn, as a new position; null when there are no more.
  virtual NodeRef find() = 0;

  /// The take-apart's stop: one find() can try every group about every
  /// pivot, so it looks at it and returns null as if there were no more
  /// turns (the caller looks at the token itself).
  void setStop(StopToken stop) { stop_ = std::move(stop); }

 protected:
  RotationMoves() = default;
  RotationMoves(const RotationMoves&) = delete;
  RotationMoves& operator=(const RotationMoves&) = delete;

  [[nodiscard]] bool stopRequested() const { return stop_.stopRequested(); }

 private:
  StopToken stop_;
};

}  // namespace bt
