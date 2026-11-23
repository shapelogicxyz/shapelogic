// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The undo steps of the document (internal to document*.cpp).

#include "shapelogic/app/document.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bt::app {

/// One part of the document as it was before a step changed it (or, once
/// undone, as the step left it): swapIn() exchanges it with the document's.
struct Document::Snapshot {
  SnapshotKind kind = SnapshotKind::Notes;
  unsigned int index = 0;
  std::unique_ptr<Voxel> shape;    // Shape
  std::optional<Problem> problem;  // Problem
  Rgb color;                      // Color
  std::string comment;             // Notes
  bool commentPopup = false;
  std::unique_ptr<Puzzle> whole;  // Whole, with the ids
  Ids ids;
};

/// An undo step: the parts it changed, in the order it first changed them.
struct Document::Step {
  std::string label;
  std::vector<Snapshot> parts;
  std::uint32_t what = 0;  ///< ChangeFlag bits
  /// Names the step for the clean mark; never 0.
  std::uint64_t stamp = 0;
};

/// A command's part of the history: opens a step (or joins the open
/// group), and on commit() pushes it; a command that does not commit (an
/// exception) has what it changed put back.
class Document::StepGuard {
 public:
  StepGuard(Document& doc, std::string label);
  ~StepGuard();
  StepGuard(const StepGuard&) = delete;
  StepGuard& operator=(const StepGuard&) = delete;
  /// Ends the command: a step when anything was saved, a new revision
  /// when anything was touched (saved now or, in a group, before).
  void commit(std::uint32_t what);

 private:
  Document& doc_;
  bool grouped_;
  /** The document's touches when the command began. */
  std::uint64_t touches_;
  std::size_t first_ = 0;
  bool committed_ = false;
};

// ---- tilted trays (document_tilted.cpp) --------------------------------------------

/// Makes `board` the board of `lay`: the layout's size, one layer, its
/// whole cells variable and the rest empty.
void fillTrayBoard(Voxel& board, const tilted::Layout& lay);
/// True when `v` has the board's size and every cell of it lies on a whole
/// cell.
[[nodiscard]] bool onTray(const Voxel& v, const tilted::Layout& lay);
/// Puts back the cells of `v` that are not whole cells of the board as they
/// are in `before` (an edit changes nothing off the board); returns how
/// many cells of `v` then differ from `before`. Both have the board's size.
unsigned int keepOnTray(Voxel& v, const Voxel& before, const tilted::Layout& lay);
/// The middle of the cells of a shape's first layer, in its own grid;
/// nothing for an empty one.
[[nodiscard]] std::optional<tilted::Point> cellsMiddle(const Voxel& v);
/// A piece on the board's grid, and whether all its cells are whole cells.
struct TrayFit {
  std::unique_ptr<Voxel> shape;
  bool fits = true;
};
/// `piece` given the board's size and moved by whole cells so that the
/// middle of its cells is as near `want` (in the board's grid; its middle
/// when nothing) as it can be with every cell on a whole cell; with no
/// such place, as near as it can be. Its name, color and weight are kept.
/// Nothing when it does not lie in one layer or is larger than the board.
[[nodiscard]] std::optional<TrayFit> fitToTray(const Voxel& piece,
                                               std::optional<tilted::Point> want,
                                               const tilted::Layout& lay);

/// Refuses text the file cannot hold (InvalidArgument naming `what`):
/// control characters other than tab, newline and carriage return, and
/// with `oneLine` line breaks (a name is an attribute).
Result<void> checkText(std::string_view text, bool oneLine, std::string_view what);

}  // namespace bt::app
