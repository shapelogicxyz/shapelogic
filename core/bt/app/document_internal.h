// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The undo steps of the document (internal to document*.cpp).

#include "bt/app/document.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"

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
  Rgb colour;                      // Colour
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

/// Refuses text the file cannot hold (InvalidArgument naming `what`):
/// control characters other than tab, newline and carriage return, and
/// with `oneLine` line breaks (a name is an attribute).
Result<void> checkText(std::string_view text, bool oneLine, std::string_view what);

}  // namespace bt::app
