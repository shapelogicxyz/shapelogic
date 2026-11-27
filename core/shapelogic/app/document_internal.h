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
#include <utility>
#include <vector>

namespace bt::app {

/// One part of the document as it was before a step changed it (or, once
/// undone, as the step left it): swapIn() exchanges it with the document's.
struct Document::Snapshot {
  SnapshotKind kind = SnapshotKind::Notes;
  unsigned int index = 0;
  std::unique_ptr<Voxel> shape;    // Shape
  std::optional<Problem> problem;  // Problem
  // Problem: the copy is without the saved solutions and the assemblies to
  // take apart (Problem::copyWithoutSolutions), which the command leaves
  // as they are: the step swaps the rest (touchProblemSettings)
  bool settingsOnly = false;
  Rgb color;                      // Color
  std::string comment;             // Notes
  bool commentPopup = false;
  std::unique_ptr<Puzzle> whole;  // Whole, with the ids
  Ids ids;

  /// About the bytes the part holds, for the steps' budget: the cells of a
  /// shape, the solutions of a problem (the rest is small beside them).
  [[nodiscard]] std::size_t bytes() const;
};

/// An undo step: the parts it changed, in the order it first changed them.
struct Document::Step {
  std::string label;
  std::vector<Snapshot> parts;
  std::uint32_t what = 0;  ///< ChangeFlag bits
  /// Names the step for the clean mark; never 0.
  std::uint64_t stamp = 0;
  /// The step of a run or a take-apart of problem `runOf` (openRunStep):
  /// while it is on top of the undo stack, that problem's next run of the
  /// same kind records no other.
  RunKind run = RunKind::None;
  Id runOf = 0;

  /// About the bytes its parts hold (Snapshot::bytes), counted as the step
  /// goes onto the undo stack (count(): a pass over every solution it
  /// holds, so once a push, not once a look). Not the same before and
  /// after the step is undone, since the parts then hold the other state:
  /// a redo counts again.
  std::uint64_t bytes = 0;
  void count();
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

template <class Job, class... Args>
void Document::startJob(Id problem, Args&&... args) {
  jobs_.emplace(std::pair{problem, Job::kKind},
                std::make_unique<Job>(std::forward<Args>(args)...));
}

// (startJob alone fills jobs_, so what is filed under a class's kind is
// of that class)
template <class Job>
Job* Document::job(Id problem) const {
  const auto it = jobs_.find({problem, Job::kKind});
  return it == jobs_.end() ? nullptr : &static_cast<Job&>(*it->second);
}

template <class Value, class Get, class Set>
Result<void> Document::editShapeField(Id shape, const char* label, Value value, Get get,
                                      Set set) {
  const Result<unsigned int> s = editableShape(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  Voxel& v = puzzle_->getShape(*s);
  auto to = value(std::as_const(v));
  if (!to) {
    return std::unexpected(to.error());
  }
  if (get(std::as_const(v)) == *to) {
    return {};
  }
  StepGuard g(*this, label);
  touchShape(*s);
  set(v, std::move(*to));
  lastChange_.shapes = {shape};
  g.commit(kChangeShapes);
  return {};
}

template <class Value, class Get, class Set>
Result<void> Document::editProblemField(Id problem, const char* label, Value value, Get get,
                                        Set set) {
  const Result<unsigned int> p = freeProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  Problem& pr = puzzle_->getProblem(*p);
  auto to = value(std::as_const(pr));
  if (!to) {
    return std::unexpected(to.error());
  }
  if (get(std::as_const(pr)) == *to) {
    return {};
  }
  StepGuard g(*this, label);
  touchProblemSettings(*p);
  set(pr, std::move(*to));
  lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return {};
}

/// Moves an entry of a list from place `from` to place `to` one place at a
/// time, as the original's buttons did: `exchange(a, b)` swaps two
/// neighbors (the entries and their ids, and whatever follows them).
template <class Exchange>
void moveInList(unsigned int from, unsigned int to, Exchange exchange) {
  unsigned int at = from;
  while (at != to) {
    const unsigned int next = at < to ? at + 1 : at - 1;
    exchange(at, next);
    at = next;
  }
}

/// Refuses text the file cannot hold (InvalidArgument naming `what`):
/// control characters other than tab, newline and carriage return, and
/// with `oneLine` line breaks (a name is an attribute).
Result<void> checkText(std::string_view text, bool oneLine, std::string_view what);

}  // namespace bt::app
