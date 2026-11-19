// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A sliding puzzle played by hand: what a position of a problem's pieces
/// allows, asked one question at a time. Nothing here keeps a game: the
/// caller holds the position and the moves made, and asks where a piece
/// can go, whether a list of moves can be made and what routes they take,
/// and whether the goal is reached.
///
/// The rules are the slide search's (sliding_search.h), read from the same
/// code: one move is one piece going anywhere it can reach while the
/// others stay put, round corners included; with nested slides a piece may
/// carry what is nested in its outline; a piece stays in its layer unless
/// vertical moves are allowed; the goal is reached as the search judges it
/// (any copy of a piece on any of its goals, a piece without a goal
/// anywhere, no piece resting on a variable cell). Optimized paths play no
/// part: they narrow a search, not the moves there are.
///
/// A position is a layout (bt/model/sliding.h): for each piece of
/// layoutPieces, the lowest corner of its cells on the board. A piece is
/// named by its place in that list.

#include "shapelogic/base/error.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_search.h"

#include <span>
#include <string>
#include <vector>

namespace bt::sliding {

/// The rules of a game that the problem does not fix: the Solver's
/// options of the same names (SlidingOptions).
struct PlayRules {
  /// A piece may carry what is nested in its outline.
  bool nested = false;
  /// It may also carry what is partly nested in it. Implies nested.
  bool partialNested = false;
  /// On a board more than one layer deep, a piece may also go up and down.
  bool verticalMoves = false;
};

/// A piece of a game.
struct PlayPiece {
  unsigned int shapeId = 0;     ///< its shape
  TrayCell at{};                ///< where it starts: the lowest corner of its cells
  std::vector<TrayCell> cells;  ///< the cells it fills, from that corner
  /// That corner in the shape's own grid: what places the shape's picture,
  /// whose box is in the grid's cells, on the piece.
  TrayCell origin{};
};

/// How a game of a problem starts.
struct PlayStart {
  /// Why the problem cannot be played (startLayout's reason); else empty.
  std::string error;
  /// The pieces, in layoutPieces' order; none with an error.
  std::vector<PlayPiece> pieces;
  /// The problem has goals: without any, no position is solved.
  bool hasGoal = false;
  /// The start is the goal already.
  bool solved = false;
};

/// The start of a game of the problem: its pieces where its starts put
/// them.
[[nodiscard]] PlayStart playStart(const Puzzle& puzzle, const Problem& problem);

/// A place one move can take a piece to.
struct PlayStop {
  SlideShift by{};      ///< the shift from where it is
  bool solved = false;  ///< the position it gives is the goal
};

/// Pieces that can move as one, and where one move can take them.
struct PlayGroup {
  /// The piece asked about first, then what it carries.
  std::vector<unsigned int> movers;
  /// Every shift they can reach while the others stay put, nearest first.
  std::vector<PlayStop> stops;
};

/// Where one move can take piece `piece` of the position `places`: as the
/// search has it, the piece alone, then with nested slides the piece with
/// what is nested in it (when anything is), then with partial nesting the
/// wider group (when it is wider). InvalidArgument when the places are no
/// position of the problem (layoutRefusal) or there is no such piece.
[[nodiscard]] Result<std::vector<PlayGroup>> playReach(const Puzzle& puzzle,
                                                       const Problem& problem,
                                                       std::span<const TrayCell> places,
                                                       unsigned int piece, const PlayRules& rules);

/// One move of a game: the pieces that move as one, and how far.
struct PlayMove {
  std::vector<unsigned int> movers;
  SlideShift by{};
};

/// A move made.
struct PlayStep {
  /// The corner points of its route, as shifts from where the pieces were,
  /// start and end included, with the fewest straight runs (slideRoute).
  std::vector<SlideShift> route;
  /// The position after it is the goal.
  bool solved = false;
};

/// A list of moves made from a position.
struct PlayReplay {
  /// The position the moves start from is the goal.
  bool solved = false;
  /// The moves that could be made, in order: all of them without an error.
  std::vector<PlayStep> steps;
  /// Why move steps.size() cannot be made; empty when all were.
  std::string error;
};

/// Makes `moves` one after another from the position `places`: each must
/// be one move as playReach has them (its pieces a group that moves as
/// one, its shift one they can reach). Stops at the first that is not.
/// InvalidArgument when the places are no position of the problem.
[[nodiscard]] Result<PlayReplay> playReplay(const Puzzle& puzzle, const Problem& problem,
                                            std::span<const TrayCell> places,
                                            std::span<const PlayMove> moves,
                                            const PlayRules& rules);

}  // namespace bt::sliding
