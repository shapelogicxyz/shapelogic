// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A stacking puzzle played by hand (decision 0042): how a problem's game
/// starts, which transfers a position allows, a list of transfers made one
/// after another, and a position checked before it becomes the problem's
/// start or goal. Nothing here keeps a game: each call is asked of one
/// position, a `Stacking` in the searches' numbers (stacking::discsOf).
/// The rules are the Stacking Solver's own (stacking::moveAllowed), and a
/// position is the goal when it is the goal's stacking disc for disc, as
/// both searches end. Defined at the end of model/stacking.cpp, beside the
/// rules they use.

#include "shapelogic/base/error.h"
#include "shapelogic/model/stacking.h"

#include <string>
#include <vector>

namespace sl {
class Problem;
class Puzzle;
}  // namespace sl

namespace sl::stacking {

/// How high each disc of a stacking rests, rod by rod as the stacking has
/// them: on a plain rod its place from the bottom, in a Panex column where
/// its depth lets it fall to (what layoutBoard and the frames draw).
using Heights = std::vector<std::vector<int>>;

/// How a problem's game starts.
struct PlayStart {
  /// Why the problem cannot be played (no rod set, no discs, a start that
  /// breaks the board's rules or leaves a disc off); empty when it can.
  std::string error;
  /// The problem's discs, numbered as the searches number them.
  std::vector<Disc> discs;
  /// The start's stacking and where its discs rest; empty with an error.
  Stacking stacks;
  Heights heights;
  /// The problem has a goal to play to: it is ready to solve (setupError).
  bool hasGoal = false;
  /// The goal's stacking and where its discs rest; empty without a goal.
  Stacking goal;
  Heights goalHeights;
  /// Why the goal the problem has is not played to (setupError's
  /// sentence); empty with a goal, and when the goal's rods are empty.
  std::string goalError;
  /// The start is the goal already.
  bool solved = false;
};

/// How `problem`'s game starts. A problem is played once its start is a
/// position of all its discs under the board's rules; the goal is played
/// to only when the problem is ready to solve.
[[nodiscard]] PlayStart playStart(const Puzzle& puzzle, const Problem& problem);

/// Why `stacks` is no position of the problem's discs on its board: not
/// the board's rods, a disc missing, twice or unknown, or a rod that
/// breaks the board's rules (its height, the size rule, a Panex column's
/// depths). Empty when it is one.
[[nodiscard]] std::string positionRefusal(const Puzzle& puzzle, const Problem& problem,
                                          const Stacking& stacks);

/// Where the discs of a position rest (a position positionRefusal takes).
[[nodiscard]] Heights restingHeights(const Puzzle& puzzle, const Problem& problem,
                                     const Stacking& stacks);

/// Why the top disc of rod `from` may not go onto rod `to`, as a sentence;
/// empty when stacking::moveAllowed allows it. The search's rule decides,
/// this only words it.
[[nodiscard]] std::string moveRefusal(const RodSet& board, const std::vector<Disc>& discs,
                                      const Stacking& stacking, unsigned int from,
                                      unsigned int to);

/// One transfer a position allows.
struct PlayTransfer {
  unsigned int from = 0;
  unsigned int to = 0;
  /// Where the disc comes to rest on `to`.
  int z = 0;
  /// The position it gives is the goal (never without a goal).
  bool solved = false;
};

/// Every transfer `stacks` allows, rod by rod as the Stacking Solver tries
/// them. InvalidArgument with positionRefusal's sentence when `stacks` is
/// no position.
[[nodiscard]] Result<std::vector<PlayTransfer>> playMoves(const Puzzle& puzzle,
                                                          const Problem& problem,
                                                          const Stacking& stacks);

/// A transfer asked for: the top disc of `from` onto `to`.
struct PlayMove {
  unsigned int from = 0;
  unsigned int to = 0;
};

/// One transfer made.
struct PlayStep {
  /// The disc that moved.
  unsigned int disc = 0;
  /// Where it came to rest.
  int z = 0;
  /// The position after it is the goal.
  bool solved = false;
};

/// A list of transfers made from a position.
struct PlayReplay {
  /// The position the transfers start from is the goal.
  bool solved = false;
  /// The transfers made: all of them, or those before the first that
  /// cannot be made.
  std::vector<PlayStep> steps;
  /// Why transfer number steps.size() cannot be made; empty when all were.
  std::string error;
};

/// Makes `moves` one after another from `stacks`, stopping at the first
/// the rules refuse. InvalidArgument with positionRefusal's sentence when
/// `stacks` is no position.
[[nodiscard]] Result<PlayReplay> playReplay(const Puzzle& puzzle, const Problem& problem,
                                            const Stacking& stacks,
                                            const std::vector<PlayMove>& moves);

/// A position as a stack map (the discs by shape and copy), to be a
/// problem's start or goal. For a position positionRefusal takes.
[[nodiscard]] StackMap positionMap(const Puzzle& puzzle, const Problem& problem,
                                   const Stacking& stacks);

}  // namespace sl::stacking
