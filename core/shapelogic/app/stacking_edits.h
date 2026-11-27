// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A stacking puzzle's commands of a document (decision 0015), apart from
/// document.h (cleanup item 21.6): only their engine methods and their
/// tests include this header. What the generic commands refuse or do on a
/// stacking puzzle (refuseOnStacking, a new shape as a disc), its saved
/// searches and the solves stay in the Document.

#include "shapelogic/app/document.h"
#include "shapelogic/base/error.h"
#include "shapelogic/model/stacking_types.h"

#include <string>
#include <vector>

namespace sl::app {

/// What placing every disc did (StackingEdits::placeAllDiscs): how many
/// went on the rod before one would not, and why that one would not (empty
/// when all went).
struct DiscsPlaced {
  unsigned int placed = 0;
  std::string error;
};

/// A stacking puzzle's rod sets, disc sizes and its problems' stacks. A
/// stacking puzzle's boards are its rod sets (at least one); a problem
/// stands on one (Set Start/Goal Rods) and holds the discs on each rod at
/// its start and its goal. Its shapes are discs: shape.create makes one of
/// size 1 (cb_NewShape), and their size is set here (the disc list), not by
/// cells. Each command that changes something is one undo step of the
/// document, and each is refused (Unsupported) on other grids. The rod set
/// commands are refused while anything is solved (the GUI grayed them out
/// while a solve ran); the stacks' commands while their problem is. Made
/// where it is used, on a document that outlives it:
/// `StackingEdits(doc).placeDisc(...)`.
class StackingEdits {
 public:
  /// The commands of `doc`.
  explicit StackingEdits(Document& doc) : d_(doc) {}

  /// Adds a rod set with the default rules (cb_NewRod).
  Result<Id> addRodSet();
  /// Adds a copy of a rod set, name and all, at the end (cb_CopyRod).
  Result<Id> duplicateRodSet(Id rodSet);
  /// Removes a rod set; the problems on it have none afterwards, their
  /// stacks kept (cb_DeleteRod). The last one is kept ("Keep at least one
  /// rod set").
  Result<void> deleteRodSet(Id rodSet);
  /// Renames a rod set (cb_NameRod); one line of text the file can hold.
  Result<void> renameRodSet(Id rodSet, std::string name);
  /// Moves a rod set to place `toIndex`, one place at a time as the arrows
  /// did; the problems keep theirs (cb_RodExchange).
  Result<void> moveRodSet(Id rodSet, unsigned int toIndex);
  /// Sets a rod set's rules (cb_RodField; `rules.name` is not used): the
  /// rod count 1 to stacking::kMaxRods, the heights 1 to kMaxHeight (others
  /// refused, InvalidArgument), the pocket and the outside
  /// channel only with Panex columns (the channel with three rods or more
  /// too), the older "can move over" dropped. The problems on it have
  /// their stacks fitted to the new count and lose their solutions.
  Result<void> setRodSetRules(Id rodSet, const stacking::RodSet& rules);
  /// A disc's size, 1 to 100 (the disc list's size; generateDisk: a new
  /// square of (2 size + 1) cells, its color kept). Solutions stay, as in
  /// the original (cb_DiskList).
  Result<void> setDiscSize(Id shape, unsigned int size);
  /// The board a problem stands on (Set Start/Goal Rods: cb_ShapeToResult's
  /// stacking branch); its stacks get the board's rods.
  Result<void> setProblemRodSet(Id problem, Id rodSet);
  /// Puts a copy of a disc on top of a rod of the start (goal false) or
  /// the goal (cb_AddShapeToProblem: placeOneDisk): the next copy not on
  /// that stacking, one more copy when every copy is; the rules are not
  /// enforced (setupError tells what breaks them). `rod` is a place of the
  /// problem's stack map (the pocket column is the last).
  Result<void> placeDisc(Id problem, Id shape, unsigned int rod, bool goal);
  /// Puts a copy of every disc on a rod, largest first
  /// (cb_AddAllShapesToProblem); what was placed before a refusal stays.
  Result<DiscsPlaced> placeAllDiscs(Id problem, unsigned int rod, bool goal);
  /// Takes the disc at place `index` (from the bottom) of a rod off both
  /// stackings, one copy fewer (cb_RemoveShapeFromProblem).
  Result<void> removeDisc(Id problem, bool goal, unsigned int rod, unsigned int index);
  /// Empties the start's or the goal's rods; the counts stay, and the
  /// problem loses its solutions (cb_RemoveAllShapesFromProblem).
  Result<void> clearStacks(Id problem, bool goal);
  /// Swaps the disc at place `index` (from the bottom) with the one above
  /// (+1) or below (-1) it (cb_ProbShapeExchange); returns its new place.
  Result<unsigned int> moveDisc(Id problem, bool goal, unsigned int rod, unsigned int index,
                                int delta);
  /// Makes a position of `problem`'s discs its start (goal false) or its
  /// goal (stacking.setStacks), replacing what that side had: `stacks` has
  /// each rod's discs bottom to top, numbered as stacking::discsOf numbers
  /// them. Refused (InvalidArgument) with the reason when it is no position
  /// of all the discs under the board's rules (stacking::positionRefusal).
  /// The problem loses its solutions. False, with no undo step, when that
  /// side is the position already.
  Result<bool> setStacks(Id problem, bool goal,
                         const std::vector<std::vector<unsigned int>>& stacks);

 private:
  /// Unsupported on other grids.
  [[nodiscard]] Result<void> checkStacking() const;
  /// A stacking problem's index, free to edit, standing on a rod set
  /// ("Choose a rod set with Set Start/Goal Rods").
  [[nodiscard]] Result<unsigned int> stackingProblem(Id problem) const;

  Document& d_;
};

}  // namespace sl::app
