// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from the sliding parts of the original GUI: cb_NewStartGoal
// (mainwindow_rods.cpp), the start/goal editor's click (cb_pieceEdit,
// RS_SLIDING_CLICK), changeShape's dropChangedStamps, and the
// syncSlidingProblems that follow them. The original made sliding shapes
// one layer deep on load (flattenPieces); here they keep their layers.
#include "shapelogic/app/document.h"
#include "shapelogic/app/document_internal.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/sliding.h"

#include <algorithm>
#include <string>
#include <vector>

namespace bt::app {

namespace {

constexpr std::uint32_t kChangeAll =
    kChangeShapes | kChangeColours | kChangeProblems | kChangeSolutions;

// "B1, B3": shapes by their labels.
std::string shapeList(const Puzzle& puzzle, const std::vector<unsigned int>& shapes) {
  std::string names;
  for (const unsigned int s : shapes) {
    names += (names.empty() ? "" : ", ") + sliding::shapeLabel(puzzle, s);
  }
  return names;
}

}  // namespace

void Document::syncSliding() {
  touchWhole();
  endSessions();
  // the boards (start/goal shapes) stay above the pieces: one below a
  // piece moves up, one place at a time as moveShape moves, the marks and
  // problems following each swap
  bool lost = false;
  unsigned int top = 0;
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    if (!sliding::isStartGoalShape(&puzzle_->getShape(i))) {
      continue;
    }
    for (unsigned int at = i; at > top; at--) {
      lost = !sliding::noteShapeSwap(*puzzle_, at - 1, at).empty() || lost;
      puzzle_->exchangeShapes(at - 1, at);
      std::swap(ids_.shapes[at - 1], ids_.shapes[at]);
    }
    top++;
  }
  // The problems of no start/goal shape go; the others keep their places
  // and their ids, and the new ones come at the end (syncSlidingProblems).
  std::vector<Id> kept;
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    const Problem& pr = puzzle_->getProblem(p);
    if (pr.resultValid() && sliding::isStartGoalShape(&puzzle_->getShape(pr.getResultId()))) {
      kept.push_back(ids_.problems[p]);
    }
  }
  const unsigned int colours = puzzle_->colorNumber();
  sliding::syncSlidingProblems(*puzzle_);
  ids_.problems = std::move(kept);
  while (ids_.problems.size() < puzzle_->getNumberOfProblems()) {
    ids_.problems.push_back(newId());
  }
  // the start locks may need colours the puzzle did not have
  for (unsigned int c = colours; c < puzzle_->colorNumber(); c++) {
    ids_.colours.push_back(newId());
  }
  // pieces were painted, problems came and went: everything is news
  lastChange_.shapes.clear();
  lastChange_.problems.clear();
  lastChange_.what |= kChangeAll;
  if (lost) {
    lastChange_.notice =
        "Only the first 63 shapes can have a start or goal: the boards moved above the pieces, "
        "and the pieces moved past the 63rd place lost their starts and goals.";
  }
}

void Document::slidingShapeChanged(unsigned int shape) {
  if (!sliding::isSliding(*puzzle_)) {
    return;
  }
  const Voxel& v = puzzle_->getShape(shape);
  if (sliding::isStartGoalShape(&v) || sliding::isHiddenSlidingShape(&v) || shape >= 63) {
    return;
  }
  // only when a start/goal shape has a mark of it
  const unsigned int mark = shape + 1;
  bool marked = false;
  for (unsigned int t = 0; t < puzzle_->getNumberOfShapes() && !marked; t++) {
    const Voxel& tray = puzzle_->getShape(t);
    if (!sliding::isStartGoalShape(&tray)) {
      continue;
    }
    for (unsigned int i = 0; i < tray.getXYZ() && !marked; i++) {
      marked = tray.getColor(i) == mark || tray.getGoalPiece(i) == mark;
    }
  }
  if (!marked) {
    return;
  }
  // A piece whose shape changed no longer fits its starts and goals: they
  // come off every start/goal shape (an edit that leaves the shape as it
  // was, such as moving it in its grid, removes nothing).
  touchWhole();
  const std::vector<unsigned int> trays = sliding::dropChangedStamps(*puzzle_, shape);
  if (trays.empty()) {
    return;
  }
  syncSliding();
  lastChange_.notice = shapeLabelOf(shape) +
                       " changed shape: its start and goal were removed from " +
                       shapeList(*puzzle_, trays) + ".";
}

void Document::slidingMarksLost(Id shape, const std::vector<Id>& trays) {
  std::vector<unsigned int> indices;
  for (const Id t : trays) {
    const unsigned int i = shapeIndex(t).value();
    if (std::find(indices.begin(), indices.end(), i) == indices.end()) {
      indices.push_back(i);
    }
  }
  std::sort(indices.begin(), indices.end());
  syncSliding();
  lastChange_.notice = "Only the first 63 shapes can have a start or goal: " +
                       shapeLabelOf(shapeIndex(shape).value()) +
                       " moved past the 63rd place and its start and goal were removed from " +
                       shapeList(*puzzle_, indices) + ".";
}

bool Document::slidingRequired(unsigned int problem, unsigned int shape) const {
  return sliding::isSliding(*puzzle_) &&
         sliding::shapeIsRequired(*puzzle_, puzzle_->getProblem(problem), shape);
}

Result<Id> Document::addStartGoal(std::array<unsigned int, 3> size) {
  if (!sliding::isSliding(*puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have start/goal shapes");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  if (size[0] == 0 || size[1] == 0 || size[2] == 0 || size[0] > 1024 || size[1] > 1024 ||
      size[2] > 1024) {
    return fail(Errc::InvalidArgument,
                "a start/goal shape is 1 to 1024 cells wide, high and deep");
  }
  StepGuard g(*this, "New start/goal");
  touchWhole();
  (void)sliding::addStartGoalShape(*puzzle_, size[0], size[1], size[2]);
  const Id id = newId();
  ids_.shapes.push_back(id);
  syncSliding();
  g.commit(kChangeAll);
  return id;
}

Result<bool> Document::placeStamp(Id tray, Id piece, int x, int y, int z, bool goal) {
  if (!sliding::isSliding(*puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  const Result<unsigned int> s = shapeIndex(piece);
  if (!s) {
    return std::unexpected(s.error());
  }
  const Voxel& trayShape = puzzle_->getShape(*t);
  const Voxel& pieceShape = puzzle_->getShape(*s);
  if (!sliding::isStartGoalShape(&trayShape)) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  if (sliding::isStartGoalShape(&pieceShape) || sliding::isHiddenSlidingShape(&pieceShape)) {
    return fail(Errc::InvalidArgument, "a start/goal shape is no piece");
  }
  // A whole piece at a time: it goes with its top-left cell there, moving
  // it if it was elsewhere, or is taken off when it is exactly there.
  const bool there = sliding::stampIsAt(&trayShape, &pieceShape, *s, x, y, z, goal);
  if (!there) {
    const std::string why = sliding::stampFits(&trayShape, &pieceShape, *s, x, y, z, goal);
    if (!why.empty()) {
      return fail(Errc::InvalidArgument, why);
    }
  }
  StepGuard g(*this, goal ? "Place goal" : "Place start");
  touchWhole();
  Voxel& v = puzzle_->getShape(*t);
  if (there) {
    (void)sliding::clearStamp(&v, *s, goal);
  } else {
    const std::string why = sliding::stampPiece(&v, &puzzle_->getShape(*s), *s, x, y, z, goal);
    BT_ASSERT_MSG(why.empty(), "a stamp that fit did not");
  }
  syncSliding();
  // the problems of this start/goal shape lose what they had found
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    const Problem& pr = puzzle_->getProblem(p);
    if (pr.resultValid() && pr.getResultId() == *t) {
      problemChanged(p);
    }
  }
  g.commit(kChangeAll);
  return !there;
}

Result<bool> Document::clearStampAt(Id tray, int x, int y, int z, bool goal) {
  if (!sliding::isSliding(*puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  if (!sliding::isStartGoalShape(&puzzle_->getShape(*t))) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  const unsigned int under = sliding::stampAt(&puzzle_->getShape(*t), x, y, z, goal);
  if (under == static_cast<unsigned int>(-1)) {
    return false;
  }
  StepGuard g(*this, goal ? "Remove goal" : "Remove start");
  touchWhole();
  (void)sliding::clearStamp(&puzzle_->getShape(*t), under, goal);
  syncSliding();
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    const Problem& pr = puzzle_->getProblem(p);
    if (pr.resultValid() && pr.getResultId() == *t) {
      problemChanged(p);
    }
  }
  g.commit(kChangeAll);
  return true;
}

}  // namespace bt::app
