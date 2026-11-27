// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The sliding puzzle's commands of a document (decisions 0014, 0027),
/// apart from document.h (cleanup item 21.6): only their engine methods
/// and their tests include this header, so a command added here compiles
/// what calls it, not every file that holds a Document. What the generic
/// commands do for a sliding puzzle (a board's problems following an
/// edited shape, the boards above the pieces) stays in the Document.

#include "shapelogic/app/document.h"
#include "shapelogic/base/error.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/sliding.h"

#include <array>
#include <vector>

namespace sl::app {

/// The most cells a new start/goal shape of a sliding puzzle is wide, high
/// or deep (SlidingEdits::addStartGoal).
inline constexpr unsigned int kMaxStartGoalSide = 1024;

/// A sliding puzzle's start/goal shapes (boards) and the starts and goals
/// of its problems. A sliding puzzle's problems are the user's, each with
/// a board chosen as its result, as on other grids; the problems on a
/// board keep its stamped pieces as parts (syncSlidingParts), whatever
/// changes them: these commands, and deleting, moving, duplicating and
/// editing shapes. Each command that changes something is one undo step of
/// the document, and each is refused (Unsupported) on other grids. Made
/// where it is used, on a document that outlives it:
/// `SlidingEdits(doc).placeStamp(...)`.
class SlidingEdits {
 public:
  /// The commands of `doc`.
  explicit SlidingEdits(Document& doc) : d_(doc) {}

  /// Adds a start/goal shape of `size` cells (sliding.addStartGoal, the
  /// GUI's New Start/Goal): all floor, numbered after the others. No
  /// problem is made for it (the original made one).
  Result<Id> addStartGoal(std::array<unsigned int, 3> size);
  /// The start/goal editor's click (sliding.place): the whole of a piece as
  /// its start (goal false) or its goal, from a click on (x, y) of layer z,
  /// y rising upwards (sliding::stampPiece: the piece's slice on layer z
  /// under the mouse, as low as it fits): a piece of one copy moves there,
  /// one of several copies is one more copy's (refused once every copy the
  /// problem has is placed); a copy that is exactly there already is taken
  /// off. Refused (InvalidArgument) with the reason when it does not fit.
  /// The start or goal is `problem`'s (decision 0027), whose result must
  /// be the tray (0: the tray's first problem); the other problems on the
  /// tray keep theirs. Refused when no problem uses the tray. Returns
  /// whether the piece is there now.
  Result<bool> placeStamp(Id tray, Id piece, int x, int y, int z, bool goal, Id problem = 0);
  /// The problem `problem`, read for the sliding.* calls that name one,
  /// when its result is the start/goal shape `tray`; refused when it is
  /// not. 0 is the tray's first problem, null when no problem uses it.
  [[nodiscard]] Result<const Problem*> trayProblemOf(Id problem, unsigned int tray) const;
  /// Takes off `problem`'s start or goal of the copy that covers (x, y, z)
  /// (the right click); false when there is none.
  Result<bool> clearStampAt(Id tray, int x, int y, int z, bool goal, Id problem = 0);
  /// Copies every start of `problem` onto its goals (toGoal), or every
  /// goal onto its starts (sliding.clone), replacing what that side had.
  /// Refused (InvalidArgument) with the reason when a start is on a
  /// corridor.
  Result<void> cloneStamps(Id tray, bool toGoal, Id problem = 0);
  /// Takes every start (goal false) or every goal of `problem` off the
  /// tray (sliding.clear); false when that side had none.
  Result<bool> clearStamps(Id tray, bool goal, Id problem = 0);
  /// Makes a position of `problem`'s pieces its starts (goal false) or its
  /// goals (sliding.setLayout), replacing what that side had: `at` places
  /// each piece of sliding::layoutPieces by the lowest corner of its cells.
  /// Refused (InvalidArgument) with the reason when it is no position of
  /// the pieces on the problem's board, or a goal would be on a corridor
  /// (sliding::layoutRefusal). False, with no undo step, when that side is
  /// the position already.
  Result<bool> setSlideLayout(Id problem, bool goal, const std::vector<sliding::TrayCell>& at);

 private:
  /// The problem whose starts and goals a command changes: the one named
  /// (trayProblemOf), refused when no problem uses the tray.
  [[nodiscard]] Result<unsigned int> stampProblem(Id problem, unsigned int tray) const;

  Document& d_;
};

}  // namespace sl::app
