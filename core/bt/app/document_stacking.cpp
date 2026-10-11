// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from the stacking parts of the original GUI: the rod sets of
// mainwindow_rods.cpp (cb_NewRod, cb_DeleteRod, cb_CopyRod, cb_NameRod,
// cb_RodExchange, cb_RodField with syncPanexFields), the disc list of
// BlockList.cpp (DiskSelector::applySize, onDelta, onSize) with
// cb_DiskList, cb_NewShape's stacking branch, and the stacking branches of
// the Problems tab in mainwindow.cpp (cb_ShapeToResult, cb_AddShapeToProblem
// with placeOneDisk, cb_AddAllShapesToProblem, cb_RemoveShapeFromProblem,
// cb_RemoveAllShapesFromProblem, cb_ProbShapeExchange). The GUI kept a rod
// history of its own (rodPast, rodFuture); here every one of them is a
// step of the document's undo (decision 0011). Which edits drop solutions
// follows each callback: only new rules (cb_RodField) and an emptied
// stacking (changeProblem) do.
#include "bt/app/document.h"
#include "bt/app/document_internal.h"
#include "bt/base/assert.h"
#include "bt/grid/voxel.h"
#include "bt/io/paused_solve.h"
#include "bt/model/problem.h"
#include "bt/model/stacking.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace bt::app {

namespace {

// The GUI's bounds: the rod count's input (STACK_ROD_BUTTONS), the
// heights' inputs, and the disc list's size (onSize's message).
constexpr unsigned int kMaxRods = 16;
constexpr unsigned int kMaxHeight = 64;
constexpr unsigned int kMaxDiscSize = 100;

// The stacking a command edits: the start's or the goal's.
const stacking::StackMap& mapOf(const Problem& pr, bool goal) {
  return goal ? pr.goalStacks() : pr.startStacks();
}

// The rods a stack map has once syncMaps has run on a board of `board`
// rods: it adds the missing ones, and of those past the count it keeps
// the ones up to the last that holds discs.
std::size_t rodsAfterSync(const stacking::StackMap& map, std::size_t board) {
  std::size_t size = map.rods.size();
  if (size < board) {
    return board;
  }
  while (size > board && map.rods[size - 1].empty()) {
    size--;
  }
  return size;
}

std::size_t rodsAfterSync(const Puzzle& puzzle, const Problem& pr, bool goal) {
  return rodsAfterSync(mapOf(pr, goal),
                       stacking::totalRods(puzzle.getRodSet(pr.getRodSetId())));
}

// cb_RodField: the fields as the GUI read them back, clamped, and the
// Panex dependencies as syncPanexFields left them (the pocket only with
// Panex columns, the outside channel with three rods or more too). "Can
// move over" was only read from older files: an edit writes it off.
stacking::RodSet rulesFromFields(const stacking::RodSet& fields, const std::string& name) {
  stacking::RodSet r;
  r.name = name;
  r.rodCount = std::clamp(fields.rodCount, 1U, kMaxRods);
  r.growHeight = fields.growHeight;
  r.definedHeight = std::clamp(fields.definedHeight, 1U, kMaxHeight);
  r.sizeMatters = fields.sizeMatters;
  r.distanceMatters = fields.distanceMatters;
  r.canMoveOver = false;
  r.panexColumns = fields.panexColumns;
  r.pocketColumn = r.panexColumns && fields.pocketColumn;
  r.pocketHeight = std::clamp(fields.pocketHeight, 1U, kMaxHeight);
  r.outsideChannel = r.panexColumns && r.rodCount >= 3 && fields.outsideChannel;
  return r;
}

// The GUI's placeOneDisk: one more copy when every copy is on the edited
// stacking, then the next free copy on top of the rod, the rules not
// enforced; the count back when the disc could not go.
std::string placeOneDisk(const Puzzle& puzzle, Problem& pr, unsigned int shape, unsigned int rod,
                         bool goal) {
  const unsigned int before = pr.getShapeMaximum(shape);
  bool added = true;
  const stacking::StackMap& map = mapOf(pr, goal);
  for (unsigned int i = 0; i < before && added; i++) {
    bool used = false;
    for (const std::vector<stacking::DiskRef>& r : map.rods) {
      for (const stacking::DiskRef& d : r) {
        used = used || (d.shapeId == shape && d.instance == i);
      }
    }
    added = used;
  }
  if (added) {
    pr.setShapeMaximum(puzzle, shape, before + 1);
    if (pr.getShapeMinimum(shape) == before) {
      pr.setShapeMinimum(puzzle, shape, before + 1);
    }
  }
  std::string err = stacking::placeDisk(puzzle, pr, goal, shape, rod, false);
  if (!err.empty() && added) {
    pr.setShapeMaximum(puzzle, shape, before);
    if (pr.getShapeMinimum(shape) > before) {
      pr.setShapeMinimum(puzzle, shape, before);
    }
  }
  return err;
}

}  // namespace

// ---- the checks ------------------------------------------------------------------------

Result<void> Document::checkStacking() const {
  if (!stacking::isStacking(*puzzle_)) {
    return fail(Errc::Unsupported, "only stacking puzzles have rod sets and discs");
  }
  return {};
}

Result<void> Document::refuseOnStacking(const char* why) const {
  if (stacking::isStacking(*puzzle_)) {
    return fail(Errc::InvalidArgument, why);
  }
  return {};
}

Result<unsigned int> Document::stackingProblem(Id problem) const {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return p;
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return std::unexpected(r.error());
  }
  if (!puzzle_->getProblem(*p).rodSetValid(*puzzle_)) {
    return fail(Errc::InvalidArgument, "Choose a rod set with Set Start/Goal Rods");
  }
  return p;
}

// ---- discs -----------------------------------------------------------------------------

Result<Id> Document::addDisc() {
  StepGuard g(*this, "New disc");
  touchAppendedShape();
  (void)stacking::addDisk(*puzzle_, 1);
  const Id id = newId();
  ids_.shapes.push_back(id);
  lastChange_.shapes = {id};
  g.commit(kChangeShapes);
  return id;
}

Result<void> Document::setDiscSize(Id shape, unsigned int size) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (size < 1 || size > kMaxDiscSize) {
    return fail(Errc::InvalidArgument,
                "An invalid size was entered, please pick a value between 1-100.");
  }
  // the list's size field and its + and - change the size only
  if (puzzle_->getShape(*s).getDiskSize() == size) {
    return {};
  }
  StepGuard g(*this, "Disc size");
  touchShape(*s);
  stacking::generateDisk(puzzle_->getShape(*s), size);
  // the solutions stay (cb_DiskList); a paused solve of a problem with
  // the disc starts its next run anew
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    if (puzzle_->getProblem(p).usesShape(*s)) {
      endSession(p);
    }
  }
  lastChange_.shapes = {shape};
  g.commit(kChangeShapes);
  return {};
}

// ---- rod sets --------------------------------------------------------------------------

Result<Id> Document::addRodSet() {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  StepGuard g(*this, "New rod set");
  touchWhole();
  (void)puzzle_->addRodSet();
  const Id id = newId();
  ids_.rodSets.push_back(id);
  g.commit(kChangeRodSets);
  return id;
}

Result<Id> Document::duplicateRodSet(Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  StepGuard g(*this, "Copy rod set");
  touchWhole();
  const stacking::RodSet copy = puzzle_->getRodSet(*r);
  (void)puzzle_->addRodSet(copy);
  const Id id = newId();
  ids_.rodSets.push_back(id);
  g.commit(kChangeRodSets);
  return id;
}

Result<void> Document::deleteRodSet(Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (puzzle_->rodSetCount() < 2) {
    return fail(Errc::InvalidArgument, "Keep at least one rod set");
  }
  StepGuard g(*this, "Delete rod set");
  touchWhole();
  // the problems on it stand on none afterwards (their stacks kept): a
  // paused solve of theirs could not carry on
  std::vector<Id> lost;
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    if (puzzle_->getProblem(p).getRodSetId() == *r) {
      endSession(p);
      lost.push_back(ids_.problems[p]);
    }
  }
  puzzle_->removeRodSet(*r);
  ids_.rodSets.erase(ids_.rodSets.begin() + *r);
  lastChange_.problems = std::move(lost);
  g.commit(kChangeRodSets | (lastChange_.problems.empty() ? 0U : kChangeProblems));
  return {};
}

Result<void> Document::renameRodSet(Id rodSet, std::string name) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (Result<void> r = checkText(name, true, "a name"); !r) {
    return r;
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (puzzle_->getRodSet(*r).name == name) {
    return {};
  }
  StepGuard g(*this, "Rename rod set");
  touchWhole();
  puzzle_->getRodSet(*r).name = std::move(name);
  g.commit(kChangeRodSets);
  return {};
}

Result<void> Document::moveRodSet(Id rodSet, unsigned int toIndex) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (toIndex >= puzzle_->rodSetCount()) {
    return fail(Errc::InvalidArgument, "there is no such place in the list of rod sets");
  }
  if (toIndex == *r) {
    return {};
  }
  StepGuard g(*this, "Move rod set");
  touchWhole();
  // one place at a time, as the arrows did; the problems keep their board
  unsigned int at = *r;
  while (at != toIndex) {
    const unsigned int next = at < toIndex ? at + 1 : at - 1;
    puzzle_->exchangeRodSets(at, next);
    std::swap(ids_.rodSets[at], ids_.rodSets[next]);
    at = next;
  }
  g.commit(kChangeRodSets);
  return {};
}

Result<void> Document::setRodSetRules(Id rodSet, const stacking::RodSet& rules) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  // an edit drops the solutions of every problem on the rod set, one of
  // which may be solving (cb_RodField put the fields back)
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  const stacking::RodSet next = rulesFromFields(rules, puzzle_->getRodSet(*r).name);
  if (next == puzzle_->getRodSet(*r)) {
    return {};
  }
  StepGuard g(*this, "Rod rules");
  touchWhole();
  puzzle_->getRodSet(*r) = next;
  // Stacks saved against the old rules are kept, illegal or not (the
  // validity bar says what is wrong); a solution found under the old
  // rules may not hold under these.
  std::vector<Id> changedProblems;
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    Problem& pr = puzzle_->getProblem(p);
    if (pr.rodSetValid(*puzzle_) && pr.getRodSetId() == *r) {
      stacking::syncMaps(*puzzle_, pr);
      problemChanged(p);
      changedProblems.push_back(ids_.problems[p]);
    }
  }
  lastChange_.problems = std::move(changedProblems);
  g.commit(kChangeRodSets |
           (lastChange_.problems.empty() ? 0U : kChangeProblems | kChangeSolutions));
  return {};
}

// ---- the stacks ------------------------------------------------------------------------

Result<void> Document::setProblemRodSet(Id problem, Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  const Result<unsigned int> r = rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  // the same board, its stacks fitted to it already: nothing to do
  const Problem& pr = puzzle_->getProblem(*p);
  const std::size_t rods = stacking::totalRods(puzzle_->getRodSet(*r));
  if (pr.getRodSetId() == *r &&
      rodsAfterSync(pr.startStacks(), rods) == pr.startStacks().rods.size() &&
      rodsAfterSync(pr.goalStacks(), rods) == pr.goalStacks().rods.size()) {
    return {};
  }
  StepGuard g(*this, "Set start/goal rods");
  touchProblem(*p);
  Problem& edited = puzzle_->getProblem(*p);
  edited.setRodSetId(*r);
  stacking::syncMaps(*puzzle_, edited);
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return {};
}

Result<void> Document::placeDisc(Id problem, Id shape, unsigned int rod, bool goal) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (rod >= rodsAfterSync(*puzzle_, puzzle_->getProblem(*p), goal)) {
    return fail(Errc::InvalidArgument, "That rod is not on this board");
  }
  StepGuard g(*this, goal ? "Place goal disc" : "Place start disc");
  touchProblem(*p);
  const std::string err = placeOneDisk(*puzzle_, puzzle_->getProblem(*p), *s, rod, goal);
  // a rod on the board and a free copy: nothing else stops a disc
  BT_ASSERT_MSG(err.empty(), "a disc placed without its rules did not go");
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<DiscsPlaced> Document::placeAllDiscs(Id problem, unsigned int rod, bool goal) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (rod >= rodsAfterSync(*puzzle_, puzzle_->getProblem(*p), goal)) {
    return fail(Errc::InvalidArgument, "That rod is not on this board");
  }
  // largest first, so a size rule can still stack them on one rod
  std::vector<unsigned int> shapes;
  for (unsigned int j = 0; j < puzzle_->getNumberOfShapes(); j++) {
    shapes.push_back(j);
  }
  std::stable_sort(shapes.begin(), shapes.end(), [this](unsigned int a, unsigned int b) {
    return puzzle_->getShape(a).getDiskSize() > puzzle_->getShape(b).getDiskSize();
  });
  DiscsPlaced out;
  if (shapes.empty()) {
    return out;
  }
  StepGuard g(*this, goal ? "Place all goal discs" : "Place all start discs");
  touchProblem(*p);
  for (const unsigned int shape : shapes) {
    out.error = placeOneDisk(*puzzle_, puzzle_->getProblem(*p), shape, rod, goal);
    if (!out.error.empty()) {
      break;
    }
    out.placed++;
  }
  if (out.placed == 0) {
    // nothing went: what was touched goes back with the guard
    return fail(Errc::InvalidArgument, out.error);
  }
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return out;
}

Result<void> Document::removeDisc(Id problem, bool goal, unsigned int rod, unsigned int index) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const stacking::StackMap& map = mapOf(puzzle_->getProblem(*p), goal);
  if (rod >= map.rods.size() || map.rods[rod].empty()) {
    return fail(Errc::InvalidArgument, "That rod is empty");
  }
  if (index >= map.rods[rod].size()) {
    return fail(Errc::InvalidArgument, "there is no disc at that place on the rod");
  }
  const stacking::DiskRef disk = map.rods[rod][index];
  StepGuard g(*this, "Remove disc");
  touchProblem(*p);
  Problem& pr = puzzle_->getProblem(*p);
  // off both stackings, the later copies renumbered, one copy fewer
  stacking::dropInstance(pr, disk.shapeId, disk.instance);
  if (pr.getShapeMinimum(disk.shapeId) > 0) {
    pr.setShapeMinimum(*puzzle_, disk.shapeId, pr.getShapeMinimum(disk.shapeId) - 1);
  }
  if (pr.getShapeMaximum(disk.shapeId) > 0) {
    pr.setShapeMaximum(*puzzle_, disk.shapeId, pr.getShapeMaximum(disk.shapeId) - 1);
  }
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::clearStacks(Id problem, bool goal) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  // Start and goal share the counts: only the stacking edited is emptied
  const stacking::StackMap& map = mapOf(puzzle_->getProblem(*p), goal);
  if (std::all_of(map.rods.begin(), map.rods.end(),
                  [](const std::vector<stacking::DiskRef>& r) { return r.empty(); })) {
    return {};
  }
  StepGuard g(*this, goal ? "Clear goal" : "Clear start");
  problemChanged(*p);
  Problem& pr = puzzle_->getProblem(*p);
  for (std::vector<stacking::DiskRef>& r : goal ? pr.editGoal().rods : pr.editStart().rods) {
    r.clear();
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<unsigned int> Document::moveDisc(Id problem, bool goal, unsigned int rod,
                                        unsigned int index, int delta) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (delta != -1 && delta != 1) {
    return fail(Errc::InvalidArgument, "a disc moves one place at a time");
  }
  const stacking::StackMap& map = mapOf(puzzle_->getProblem(*p), goal);
  if (rod >= map.rods.size() || index >= map.rods[rod].size()) {
    return fail(Errc::InvalidArgument, "there is no disc at that place on the rod");
  }
  const int next = static_cast<int>(index) + delta;
  if (next < 0 || next >= static_cast<int>(map.rods[rod].size())) {
    return fail(Errc::InvalidArgument, "the disc cannot move further");
  }
  StepGuard g(*this, "Move disc");
  touchProblem(*p);
  const bool moved = stacking::moveDisk(*puzzle_, puzzle_->getProblem(*p), goal, rod, index, delta);
  BT_ASSERT_MSG(moved, "a disc that could move did not");
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return static_cast<unsigned int>(next);
}

}  // namespace bt::app
