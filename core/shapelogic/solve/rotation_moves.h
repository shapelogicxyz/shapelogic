// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The 90 degree turns a take-apart may make on the cube grid: a generator
/// hands them out one by one, as new positions. Classic tries every group
/// of pieces about every pivot (RotationMovesClassic); Andrew Crowell's
/// heuristics try fewer (RotationMovesCrowell). The movement analysis talks
/// to this interface only.
/// Ported from src/lib/rotationmoves.h.

#include "shapelogic/base/stop.h"
#include "shapelogic/solve/disassembler_node.h"
#include "shapelogic/solve/rotation_rules.h"

#include <vector>

namespace bt {

class MovementCache;
class Symmetries;

/// The most pieces a position may have for turns to be looked for: the
/// generators hold the pieces that turn in a 32-bit mask and go through
/// every group of them. A position of more pieces gets no turns (slides
/// and removals still); the parts it separates into get them again once
/// they are small enough. (The original shifted past the mask: undefined
/// behavior, and 2^32 groups to try at 32 pieces; stage 4 audit.)
inline constexpr unsigned int kMaxTurnPieces = 31;

/// A generator of 90 degree turns from one position.
class RotationMoves {
 public:
  virtual ~RotationMoves() = default;

  /// Starts with position `nd` of the pieces `pcs` (both must stay until
  /// the next initFind). More than kMaxTurnPieces pieces give no turns.
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

  // What every generator needs of a position: the pieces' cells, the
  // moving body's box, and the position after a turn.

  /// The cells piece `pieceIdx` of `pieces` fills at position `nd`, with
  /// the shapes from `cache`.
  static void collectWorldCells(MovementCache& cache, const DisassemblerNode& nd,
                                const std::vector<unsigned int>& pieces, unsigned int pieceIdx,
                                std::vector<RotationRules::Cell>& out);

  /// A moving body's box in the plane of a turn about an axis (u and v:
  /// y and z about x, x and z about y, x and y about z), and its lowest
  /// layer along the axis.
  struct TurnBox {
    int umin = 0, umax = 0, vmin = 0, vmax = 0, amin = 0;
  };
  /// The box of `moving`, which holds a cell at least.
  [[nodiscard]] static TurnBox turnBox(const std::vector<RotationRules::Cell>& moving,
                                       unsigned int axis);

  /// The pivot at (u, v) in the plane of a turn about `axis` and `a` along
  /// it, all in doubled units.
  [[nodiscard]] static RotationRules::Pivot pivotAt(unsigned int axis, int u, int v, int a) {
    if (axis == 0) {
      return {a, u, v};
    }
    if (axis == 1) {
      return {u, a, v};
    }
    return {u, v, a};
  }

  /// The position reached from `from` when the pieces of `moveMask` (one
  /// bit each) turn about `pivot` around `axis` in `sense`; null when a
  /// piece would not land on the grid or no piece comes to lie otherwise.
  [[nodiscard]] static NodeRef turnedNode(DisassemblerNode* from, const Symmetries& sym,
                                          unsigned int moveMask,
                                          const RotationRules::Pivot& pivot, unsigned int axis,
                                          unsigned int sense);

 private:
  StopToken stop_;
};

}  // namespace bt
