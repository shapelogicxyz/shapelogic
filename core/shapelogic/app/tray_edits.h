// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A Tilted Square puzzle's tray commands of a document (decision 0045),
/// and the trying of every placement of its tray, apart from document.h
/// (cleanup item 21.6): only their engine methods and their tests include
/// this header. What every command does on a tilted puzzle (the board kept
/// as it is, a piece kept on whole cells) and the tray's queries
/// (Document::tilted, fitsTray, trayLayout, trayPlacements) stay in the
/// Document, which the generic commands and the queries read.

#include "shapelogic/app/document.h"
#include "shapelogic/app/placement_fit.h"
#include "shapelogic/base/error.h"
#include "shapelogic/model/tilted_tray.h"
#include "shapelogic/solve/solve_session.h"

#include <array>
#include <vector>

namespace bt::app {

/// The tray of a Tilted Square puzzle and the counting of its boards. Each
/// is refused (Unsupported) without a tray. Made where it is used, on a
/// document that outlives it: `TrayEdits(doc).setTray(...)`.
class TrayEdits {
 public:
  /// The commands of `doc`.
  explicit TrayEdits(Document& doc) : d_(doc) {}

  /// Sets the tray's settings (tray.set): the board is made again, every
  /// piece is given the new size and moved by whole cells to the place
  /// nearest its old one where all its cells are whole (a piece with no
  /// such place stays, centered on its old place, and fitsTray() tells).
  /// With the best placement the shift is worked out; a custom one is put
  /// in its canonical form. The solutions go. One undo step; refused
  /// while anything is solved, when the tray is not valid(), and when a
  /// piece is larger than the new board (InvalidArgument, naming it).
  Result<void> setTray(tilted::Tray tray);
  /// Makes the tray's squares `squares` ({x, y}, each inside the tray)
  /// walls, or open again (tray.setWalls); the rest as setTray(). Returns
  /// how many squares changed; none, no step.
  Result<unsigned int> setTrayWalls(const std::vector<std::array<unsigned int, 2>>& squares,
                                    bool wall);

  /// Counts the assemblies of a problem's pieces on every board of
  /// Document::trayPlacements() (tray.fitPlacements), on a thread of its
  /// own and on a copy of the shapes and the problem: the document is not
  /// changed. `options` are the solve's (the pieces turned over or not,
  /// the threads). Refused (InvalidState) while the problem is being
  /// solved, analysed or tried already, and in a group of edits;
  /// InvalidArgument when the problem has no pieces. While it lasts the
  /// document is frozen as while solving (decision 0011), so what it counts
  /// stays what the document holds. Returns the number of boards.
  Result<unsigned int> fitPlacementsStart(Id problem, const SolveOptions& options);
  /// Asks a fit to stop; what it counted is kept. Does not wait.
  Result<void> fitPlacementsStop(Id problem);
  /// Where it stands: running; or, once Document::poll() has seen it end,
  /// how the last one ended (inactive, kept until the next one starts or
  /// the tray, a piece or the problem changes, or its solve is reset);
  /// inactive with no boards when there is none.
  [[nodiscard]] Result<PlacementFitProgress> fitPlacementsProgress(Id problem) const;
  /// A fit of the problem exists: running, or ended and not yet seen by
  /// Document::poll().
  [[nodiscard]] bool fittingPlacements(Id problem) const;

 private:
  /// The common end of setTray() and setTrayWalls().
  Result<void> applyTray(tilted::Tray next, const char* label);

  Document& d_;
};

}  // namespace bt::app
