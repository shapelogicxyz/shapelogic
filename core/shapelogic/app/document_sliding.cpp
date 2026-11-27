// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from the sliding parts of the original GUI: cb_NewStartGoal
// (mainwindow_rods.cpp), the start/goal editor's click (cb_pieceEdit,
// RS_SLIDING_CLICK), changeShape's dropChangedStamps, and the
// syncSlidingProblems that follow them. The original made sliding shapes
// one layer deep on load (flattenPieces); here they keep their layers. The
// original gave each start/goal shape a problem of its own; here the user
// makes the problems and chooses their boards (syncSlidingParts), and the
// starts and goals are each problem's own (decision 0027): the commands
// here change one problem's, and an edited board takes them along
// (beginBoardEdit, finishBoardEdit).
#include "shapelogic/app/document.h"
#include "shapelogic/app/document_internal.h"
#include "shapelogic/app/sliding_edits.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/slide_marks.h"
#include "shapelogic/model/sliding.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace bt::app {

namespace {

constexpr std::uint32_t kChangeAll =
    kChangeShapes | kChangeColors | kChangeProblems | kChangeSolutions;

// "B1, B3": shapes by their labels.
std::string shapeList(const Puzzle& puzzle, const std::vector<unsigned int>& shapes) {
  std::string names;
  for (const unsigned int s : shapes) {
    names += (names.empty() ? "" : ", ") + sliding::shapeLabel(puzzle, s);
  }
  return names;
}

}  // namespace

bool Document::boardsAbovePieces() {
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
  return lost;
}

void Document::syncSliding() {
  touchWhole();
  endSessions();
  const bool lost = boardsAbovePieces();
  // the problems are the user's: none comes or goes (syncSlidingParts)
  sliding::syncSlidingParts(*puzzle_);
  SL_ASSERT(ids_.problems.size() == puzzle_->getNumberOfProblems());
  // parts changed: everything is news
  lastChange_.shapes.clear();
  lastChange_.problems.clear();
  lastChange_.what |= kChangeAll;
  if (lost) {
    lastChange_.notice =
        "Only the first 255 shapes can have a start or goal: the boards moved above the pieces, "
        "and the pieces moved past the 255th place lost their starts and goals.";
  }
}

void Document::slidingShapeChanged(unsigned int shape) {
  if (!sliding::isSliding(*puzzle_)) {
    return;
  }
  const Voxel& v = puzzle_->getShape(shape);
  if (sliding::isStartGoalShape(&v) || sliding::isHiddenSlidingShape(&v) || shape >= 255) {
    return;
  }
  // only when a problem has a mark of it
  const unsigned int mark = shape + 1;
  bool marked = false;
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems() && !marked; p++) {
    const SlideMarks& marks = puzzle_->getProblem(p).slideMarks();
    for (std::size_t i = 0; i < marks.size() && !marked; i++) {
      marked = marks.start(i) == mark || marks.goal(i) == mark;
    }
  }
  if (!marked) {
    return;
  }
  // A piece whose shape changed no longer fits its starts and goals: they
  // come off every problem (an edit that leaves the shape as it was, such
  // as moving it in its grid, removes nothing).
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
  lastChange_.notice = "Only the first 255 shapes can have a start or goal: " +
                       shapeLabelOf(shapeIndex(shape).value()) +
                       " moved past the 255th place and its start and goal were removed from " +
                       shapeList(*puzzle_, indices) + ".";
}

bool Document::slidingRequired(unsigned int problem, unsigned int shape) const {
  return sliding::isSliding(*puzzle_) &&
         sliding::shapeIsRequired(*puzzle_, puzzle_->getProblem(problem), shape);
}

Result<const Problem*> SlidingEdits::trayProblemOf(Id problem, unsigned int tray) const {
  if (problem == 0) {
    return sliding::trayProblem(*d_.puzzle_, tray);
  }
  const Result<unsigned int> p = d_.problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Problem& pr = d_.puzzle_->getProblem(*p);
  if (!pr.resultValid() || pr.getResultId() != tray) {
    return fail(Errc::InvalidArgument, "the problem's result is not this board");
  }
  return &pr;
}

Result<unsigned int> SlidingEdits::stampProblem(Id problem, unsigned int tray) const {
  const Result<const Problem*> on = trayProblemOf(problem, tray);
  if (!on) {
    return std::unexpected(on.error());
  }
  for (unsigned int p = 0; p < d_.puzzle_->getNumberOfProblems(); p++) {
    if (&d_.puzzle_->getProblem(p) == *on) {
      return p;
    }
  }
  // the starts and goals are a problem's: a board no problem uses has none
  return fail(Errc::InvalidArgument,
              "No problem uses this board: choose it as a problem's result first.");
}

std::vector<sliding::BoardEdit> Document::beginBoardEdit(unsigned int shape, Voxel& v) const {
  std::vector<sliding::BoardEdit> edit;
  if (sliding::isSliding(*puzzle_) && sliding::isStartGoalShape(&puzzle_->getShape(shape))) {
    edit.push_back(sliding::beginBoardEdit(*puzzle_, shape, v));
  }
  return edit;
}

void Document::finishBoardEdit(const std::vector<sliding::BoardEdit>& edit) {
  if (edit.empty()) {
    return;
  }
  touchWhole();
  const std::vector<unsigned int> lost = sliding::applyBoardEdit(*puzzle_, edit.front());
  // the walls and floor of a start/goal shape: its problems follow (the
  // grid editor's RS_CHANGESQUARE)
  syncSliding();
  if (!lost.empty()) {
    lastChange_.notice = "The board changed under " + shapeList(*puzzle_, lost) +
                         ": the starts and goals that lost a cell were removed.";
  }
}

Result<Id> SlidingEdits::addStartGoal(std::array<unsigned int, 3> size) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have start/goal shapes");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  if (std::ranges::any_of(size, [](unsigned int n) { return n == 0 || n > kMaxStartGoalSide; })) {
    return fail(Errc::InvalidArgument, "a start/goal shape is 1 to " +
                                           std::to_string(kMaxStartGoalSide) +
                                           " cells wide, high and deep");
  }
  Document::StepGuard g(d_, "New start/goal");
  d_.touchWhole();
  (void)sliding::addStartGoalShape(*d_.puzzle_, size[0], size[1], size[2]);
  const Id id = d_.newId();
  d_.ids_.shapes.push_back(id);
  d_.syncSliding();
  g.commit(kChangeAll);
  return id;
}

Result<bool> SlidingEdits::placeStamp(Id tray, Id piece, int x, int y, int z, bool goal,
                                      Id problem) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = d_.shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  const Result<unsigned int> s = d_.shapeIndex(piece);
  if (!s) {
    return std::unexpected(s.error());
  }
  const Voxel& trayShape = d_.puzzle_->getShape(*t);
  const Voxel& pieceShape = d_.puzzle_->getShape(*s);
  if (!sliding::isStartGoalShape(&trayShape)) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  if (sliding::isStartGoalShape(&pieceShape) || sliding::isHiddenSlidingShape(&pieceShape)) {
    return fail(Errc::InvalidArgument, "a start/goal shape is no piece");
  }
  const Result<unsigned int> p = stampProblem(problem, *t);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Problem* on = &d_.puzzle_->getProblem(*p);
  const SlideMarks& had = on->slideMarks();
  // A whole piece at a time: it goes with its top-left cell there, moving
  // it if it was elsewhere (a piece of one copy) or beside its other copies,
  // or the copy that is exactly there is taken off.
  const unsigned int copies = sliding::pieceCopies(*d_.puzzle_, *t, *s, on);
  const bool there = sliding::stampIsAt(&trayShape, &had, &pieceShape, *s, x, y, z, goal);
  if (!there) {
    const std::string why =
        sliding::stampFits(&trayShape, &had, &pieceShape, *s, x, y, z, goal, copies);
    if (!why.empty()) {
      return fail(Errc::InvalidArgument, why);
    }
  }
  Document::StepGuard g(d_, goal ? "Place goal" : "Place start");
  d_.touchWhole();
  const Voxel& v = d_.puzzle_->getShape(*t);
  SlideMarks* marks = &d_.puzzle_->getProblem(*p).editSlideMarks();
  if (there) {
    const std::vector<sliding::TrayCell> cells =
        sliding::stampCells(&v, marks, &d_.puzzle_->getShape(*s), *s, x, y, z, goal, copies);
    SL_ASSERT_MSG(!cells.empty(), "a stamp that was there has no cells");
    const sliding::TrayCell& c = cells.front();
    (void)sliding::clearCopyAt(&v, marks, &d_.puzzle_->getShape(*s), *s, c[0], c[1], c[2], goal);
  } else {
    const std::string why =
        sliding::stampPiece(&v, marks, &d_.puzzle_->getShape(*s), *s, x, y, z, goal, copies);
    SL_ASSERT_MSG(why.empty(), "a stamp that fit did not");
  }
  d_.syncSliding();
  // the problem loses what it had found; the others on this board have
  // starts and goals of their own
  d_.problemChanged(*p);
  g.commit(kChangeAll);
  return !there;
}

Result<bool> SlidingEdits::clearStampAt(Id tray, int x, int y, int z, bool goal, Id problem) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = d_.shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  if (!sliding::isStartGoalShape(&d_.puzzle_->getShape(*t))) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  const Result<unsigned int> p = stampProblem(problem, *t);
  if (!p) {
    return std::unexpected(p.error());
  }
  const unsigned int under = sliding::stampAt(
      &d_.puzzle_->getShape(*t), &d_.puzzle_->getProblem(*p).slideMarks(), x, y, z, goal);
  if (under == static_cast<unsigned int>(-1)) {
    return false;
  }
  Document::StepGuard g(d_, goal ? "Remove goal" : "Remove start");
  d_.touchWhole();
  // the copy under the cell, not the piece's other copies
  (void)sliding::clearCopyAt(&d_.puzzle_->getShape(*t),
                             &d_.puzzle_->getProblem(*p).editSlideMarks(),
                             &d_.puzzle_->getShape(under), under, x, y, z, goal);
  d_.syncSliding();
  d_.problemChanged(*p);
  g.commit(kChangeAll);
  return true;
}

Result<void> SlidingEdits::cloneStamps(Id tray, bool toGoal, Id problem) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = d_.shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  if (!sliding::isStartGoalShape(&d_.puzzle_->getShape(*t))) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  const Result<unsigned int> p = stampProblem(problem, *t);
  if (!p) {
    return std::unexpected(p.error());
  }
  // refused before the step opens, so a refusal leaves no undo step
  if (const std::string why =
          sliding::copyStampsRefusal(*d_.puzzle_, d_.puzzle_->getProblem(*p), toGoal);
      !why.empty()) {
    return fail(Errc::InvalidArgument, why);
  }
  Document::StepGuard g(d_, toGoal ? "Clone start to goal" : "Clone goal to start");
  d_.touchWhole();
  const std::string why = sliding::copyStamps(*d_.puzzle_, d_.puzzle_->getProblem(*p), toGoal);
  SL_ASSERT_MSG(why.empty(), "a clone that was checked was refused");
  d_.syncSliding();
  d_.problemChanged(*p);
  g.commit(kChangeAll);
  return {};
}

Result<bool> SlidingEdits::clearStamps(Id tray, bool goal, Id problem) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> t = d_.shapeIndex(tray);
  if (!t) {
    return std::unexpected(t.error());
  }
  if (!sliding::isStartGoalShape(&d_.puzzle_->getShape(*t))) {
    return fail(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  const Result<unsigned int> p = stampProblem(problem, *t);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (!d_.puzzle_->getProblem(*p).slideMarks().any(goal)) {
    return false;
  }
  Document::StepGuard g(d_, goal ? "Clear goal" : "Clear start");
  d_.touchWhole();
  SlideMarks& marks = d_.puzzle_->getProblem(*p).editSlideMarks();
  for (std::size_t i = marks.size(goal); i-- > 0;) {
    marks.set(i, goal, 0);
  }
  d_.syncSliding();
  d_.problemChanged(*p);
  g.commit(kChangeAll);
  return true;
}

Result<bool> SlidingEdits::setSlideLayout(Id problem, bool goal,
                                          const std::vector<sliding::TrayCell>& at) {
  if (!sliding::isSliding(*d_.puzzle_)) {
    return fail(Errc::Unsupported, "only sliding puzzles have starts and goals");
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> p = d_.problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // refused before the step opens, so a refusal leaves no undo step
  if (const std::string why =
          sliding::layoutRefusal(*d_.puzzle_, d_.puzzle_->getProblem(*p), goal, at);
      !why.empty()) {
    return fail(Errc::InvalidArgument, why);
  }
  // the position it has already: the problem keeps what it had found
  SlideMarks marks = sliding::layoutMarks(*d_.puzzle_, d_.puzzle_->getProblem(*p), goal, at);
  if (marks == d_.puzzle_->getProblem(*p).slideMarks()) {
    return false;
  }
  Document::StepGuard g(d_, goal ? "Set goal position" : "Set start position");
  d_.touchWhole();
  d_.puzzle_->getProblem(*p).editSlideMarks() = std::move(marks);
  d_.syncSliding();
  d_.problemChanged(*p);
  g.commit(kChangeAll);
  return true;
}

}  // namespace bt::app
