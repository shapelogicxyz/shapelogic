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
#include "shapelogic/app/document.h"
#include "shapelogic/app/document_internal.h"
#include "shapelogic/app/stacking_edits.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/paused_solve.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/model/stacking_play.h"
#include "shapelogic/solve/panex_search.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace sl::app {

namespace {

using stacking::kMaxDiscSize;
using stacking::kMaxHeight;
using stacking::kMaxRods;

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

// cb_RodField: the fields as the GUI read them back (in its bounds,
// checked first), and the
// Panex dependencies as syncPanexFields left them (the pocket only with
// Panex columns, the outside channel with three rods or more too). "Can
// move over" was only read from older files: an edit writes it off.
stacking::RodSet rulesFromFields(const stacking::RodSet& fields, const std::string& name) {
  stacking::RodSet r;
  r.name = name;
  r.rodCount = fields.rodCount;
  r.growHeight = fields.growHeight;
  r.definedHeight = fields.definedHeight;
  r.sizeMatters = fields.sizeMatters;
  r.distanceMatters = fields.distanceMatters;
  r.canMoveOver = false;
  r.panexColumns = fields.panexColumns;
  r.pocketColumn = r.panexColumns && fields.pocketColumn;
  r.pocketHeight = fields.pocketHeight;
  r.outsideChannel = r.panexColumns && r.rodCount >= 3 && fields.outsideChannel;
  return r;
}

// The GUI's placeOneDisk: one more copy when every copy is on the edited
// stacking, then the next free copy on top of the rod, the rules not
// enforced; the count back when the disc could not go. Refused, with
// nothing changed, for a part that holds kMaxPartCount already.
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
  if (added && before >= kMaxPartCount) {
    return kPartFullText;
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

Result<void> StackingEdits::checkStacking() const {
  if (!stacking::isStacking(*d_.puzzle_)) {
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

bool Document::sharedSearchActive(unsigned int problem, const std::string& workDir) const {
  if (!stacking::isStacking(*puzzle_)) {
    return false;
  }
  const auto folderOf = [this](unsigned int p, const std::string& dir) {
    return readProblem(p, [this, &dir](const Problem& pr) {
      const std::string f = panex::searchFolder(*puzzle_, pr, dir);
      return f.empty() ? std::string() : std::filesystem::path(f).lexically_normal().string();
    });
  };
  const std::string folder = folderOf(problem, workDir);
  if (folder.empty()) {
    return false;
  }
  for (const auto& [id, session] : sessions_) {
    if (id == ids_.problems[problem] || !isActive(session->state())) {
      continue;
    }
    const Result<unsigned int> other = problemIndex(id);
    // a run's options stay as they were while it is active
    if (other && folderOf(*other, session->options().stacking.workDir) == folder) {
      return true;
    }
  }
  return false;
}

Result<void> Document::checkSharedSearchFree(unsigned int problem,
                                             const std::string& workDir) const {
  if (sharedSearchActive(problem, workDir)) {
    return fail(Errc::InvalidState,
                "an identical problem is being solved, and the two share one saved search: "
                "pause it first");
  }
  return {};
}

Result<unsigned int> StackingEdits::stackingProblem(Id problem) const {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> p = d_.problemIndex(problem);
  if (!p) {
    return p;
  }
  if (Result<void> r = d_.checkProblemFree(*p); !r) {
    return std::unexpected(r.error());
  }
  if (!d_.puzzle_->getProblem(*p).rodSetValid(*d_.puzzle_)) {
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

Result<void> StackingEdits::setDiscSize(Id shape, unsigned int size) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  const Result<unsigned int> s = d_.editableShape(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (size < 1 || size > kMaxDiscSize) {
    return fail(Errc::InvalidArgument,
                "An invalid size was entered, please pick a value between 1-100.");
  }
  // the list's size field and its + and - change the size only
  if (d_.puzzle_->getShape(*s).getDiskSize() == size) {
    return {};
  }
  Document::StepGuard g(d_, "Disc size");
  d_.touchShape(*s);
  stacking::generateDisk(d_.puzzle_->getShape(*s), size);
  // the solutions stay (cb_DiskList); a paused solve of a problem with
  // the disc starts its next run anew
  for (unsigned int p = 0; p < d_.puzzle_->getNumberOfProblems(); p++) {
    if (d_.puzzle_->getProblem(p).usesShape(*s)) {
      d_.endSession(p);
    }
  }
  d_.lastChange_.shapes = {shape};
  g.commit(kChangeShapes);
  return {};
}

// ---- rod sets --------------------------------------------------------------------------

Result<Id> StackingEdits::addRodSet() {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  Document::StepGuard g(d_, "New rod set");
  d_.touchWhole();
  (void)d_.puzzle_->addRodSet();
  const Id id = d_.newId();
  d_.ids_.rodSets.push_back(id);
  g.commit(kChangeRodSets);
  return id;
}

Result<Id> StackingEdits::duplicateRodSet(Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return std::unexpected(r.error());
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  Document::StepGuard g(d_, "Copy rod set");
  d_.touchWhole();
  const stacking::RodSet copy = d_.puzzle_->getRodSet(*r);
  (void)d_.puzzle_->addRodSet(copy);
  const Id id = d_.newId();
  d_.ids_.rodSets.push_back(id);
  g.commit(kChangeRodSets);
  return id;
}

Result<void> StackingEdits::deleteRodSet(Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (d_.puzzle_->rodSetCount() < 2) {
    return fail(Errc::InvalidArgument, "Keep at least one rod set");
  }
  Document::StepGuard g(d_, "Delete rod set");
  d_.touchWhole();
  // the problems on it stand on none afterwards (their stacks kept): a
  // paused solve of theirs could not carry on
  std::vector<Id> lost;
  for (unsigned int p = 0; p < d_.puzzle_->getNumberOfProblems(); p++) {
    if (d_.puzzle_->getProblem(p).getRodSetId() == *r) {
      d_.endSession(p);
      lost.push_back(d_.ids_.problems[p]);
    }
  }
  d_.puzzle_->removeRodSet(*r);
  d_.ids_.rodSets.erase(d_.ids_.rodSets.begin() + static_cast<std::ptrdiff_t>(*r));
  d_.lastChange_.problems = std::move(lost);
  g.commit(kChangeRodSets | (d_.lastChange_.problems.empty() ? 0U : kChangeProblems));
  return {};
}

Result<void> StackingEdits::renameRodSet(Id rodSet, std::string name) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return r;
  }
  if (Result<void> r = checkText(name, true, "a name"); !r) {
    return r;
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (d_.puzzle_->getRodSet(*r).name == name) {
    return {};
  }
  Document::StepGuard g(d_, "Rename rod set");
  d_.touchWhole();
  d_.puzzle_->getRodSet(*r).name = std::move(name);
  g.commit(kChangeRodSets);
  return {};
}

Result<void> StackingEdits::moveRodSet(Id rodSet, unsigned int toIndex) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  if (toIndex >= d_.puzzle_->rodSetCount()) {
    return fail(Errc::InvalidArgument, "there is no such place in the list of rod sets");
  }
  if (toIndex == *r) {
    return {};
  }
  Document::StepGuard g(d_, "Move rod set");
  d_.touchWhole();
  // one place at a time, as the arrows did; the problems keep their board
  moveInList(*r, toIndex, [this](unsigned int at, unsigned int next) {
    d_.puzzle_->exchangeRodSets(at, next);
    std::swap(d_.ids_.rodSets[at], d_.ids_.rodSets[next]);
  });
  g.commit(kChangeRodSets);
  return {};
}

Result<void> StackingEdits::setRodSetRules(Id rodSet, const stacking::RodSet& rules) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  // an edit drops the solutions of every problem on the rod set, one of
  // which may be solving (cb_RodField put the fields back)
  if (Result<void> r = d_.checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  // the GUI's inputs' bounds: a value past them is refused, as the engine
  // refuses it, unless it is the one the rod set has (a file's may be
  // larger: an edit of another rule keeps it)
  const stacking::RodSet& now = d_.puzzle_->getRodSet(*r);
  const auto outside = [](unsigned int v, unsigned int was, unsigned int most) {
    return v != was && (v < 1 || v > most);
  };
  if (outside(rules.rodCount, now.rodCount, kMaxRods)) {
    return fail(Errc::InvalidArgument, "a rod set has 1 to " + std::to_string(kMaxRods) + " rods");
  }
  if (outside(rules.definedHeight, now.definedHeight, kMaxHeight) ||
      outside(rules.pocketHeight, now.pocketHeight, kMaxHeight)) {
    return fail(Errc::InvalidArgument,
                "a rod's height and the pocket's are 1 to " + std::to_string(kMaxHeight));
  }
  const stacking::RodSet next = rulesFromFields(rules, d_.puzzle_->getRodSet(*r).name);
  if (next == d_.puzzle_->getRodSet(*r)) {
    return {};
  }
  Document::StepGuard g(d_, "Rod rules");
  d_.touchWhole();
  d_.puzzle_->getRodSet(*r) = next;
  // Stacks saved against the old rules are kept, illegal or not (the
  // validity bar says what is wrong); a solution found under the old
  // rules may not hold under these.
  std::vector<Id> changedProblems;
  for (unsigned int p = 0; p < d_.puzzle_->getNumberOfProblems(); p++) {
    Problem& pr = d_.puzzle_->getProblem(p);
    if (pr.rodSetValid(*d_.puzzle_) && pr.getRodSetId() == *r) {
      stacking::syncMaps(*d_.puzzle_, pr);
      d_.problemChanged(p);
      changedProblems.push_back(d_.ids_.problems[p]);
    }
  }
  d_.lastChange_.problems = std::move(changedProblems);
  g.commit(kChangeRodSets |
           (d_.lastChange_.problems.empty() ? 0U : kChangeProblems | kChangeSolutions));
  return {};
}

// ---- the stacks ------------------------------------------------------------------------

Result<void> StackingEdits::setProblemRodSet(Id problem, Id rodSet) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  const Result<unsigned int> p = d_.freeProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> r = d_.rodSetIndex(rodSet);
  if (!r) {
    return std::unexpected(r.error());
  }
  // the same board, its stacks fitted to it already: nothing to do
  const Problem& pr = d_.puzzle_->getProblem(*p);
  const std::size_t rods = stacking::totalRods(d_.puzzle_->getRodSet(*r));
  if (pr.getRodSetId() == *r &&
      rodsAfterSync(pr.startStacks(), rods) == pr.startStacks().rods.size() &&
      rodsAfterSync(pr.goalStacks(), rods) == pr.goalStacks().rods.size()) {
    return {};
  }
  Document::StepGuard g(d_, "Set start/goal rods");
  d_.touchProblemSettings(*p);
  Problem& edited = d_.puzzle_->getProblem(*p);
  edited.setRodSetId(*r);
  stacking::syncMaps(*d_.puzzle_, edited);
  d_.endSession(*p);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return {};
}

Result<void> StackingEdits::placeDisc(Id problem, Id shape, unsigned int rod, bool goal) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = d_.shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (rod >= rodsAfterSync(*d_.puzzle_, d_.puzzle_->getProblem(*p), goal)) {
    return fail(Errc::InvalidArgument, "That rod is not on this board");
  }
  Document::StepGuard g(d_, goal ? "Place goal disc" : "Place start disc");
  d_.touchProblem(*p);
  const std::string err = placeOneDisk(*d_.puzzle_, d_.puzzle_->getProblem(*p), *s, rod, goal);
  if (!err.empty()) {
    // a rod on the board: only a full part stops a disc placed without
    // its rules (what was touched goes back with the guard)
    SL_ASSERT_MSG(err == kPartFullText, "a disc placed without its rules did not go");
    return fail(Errc::InvalidArgument, err);
  }
  d_.endSession(*p);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<DiscsPlaced> StackingEdits::placeAllDiscs(Id problem, unsigned int rod, bool goal) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (rod >= rodsAfterSync(*d_.puzzle_, d_.puzzle_->getProblem(*p), goal)) {
    return fail(Errc::InvalidArgument, "That rod is not on this board");
  }
  // largest first, so a size rule can still stack them on one rod
  std::vector<unsigned int> shapes;
  shapes.reserve(d_.puzzle_->getNumberOfShapes());
  for (unsigned int j = 0; j < d_.puzzle_->getNumberOfShapes(); j++) {
    shapes.push_back(j);
  }
  std::stable_sort(shapes.begin(), shapes.end(), [this](unsigned int a, unsigned int b) {
    return d_.puzzle_->getShape(a).getDiskSize() > d_.puzzle_->getShape(b).getDiskSize();
  });
  DiscsPlaced out;
  if (shapes.empty()) {
    return out;
  }
  Document::StepGuard g(d_, goal ? "Place all goal discs" : "Place all start discs");
  d_.touchProblem(*p);
  for (const unsigned int shape : shapes) {
    out.error = placeOneDisk(*d_.puzzle_, d_.puzzle_->getProblem(*p), shape, rod, goal);
    if (!out.error.empty()) {
      break;
    }
    out.placed++;
  }
  if (out.placed == 0) {
    // nothing went: what was touched goes back with the guard
    return fail(Errc::InvalidArgument, out.error);
  }
  d_.endSession(*p);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return out;
}

Result<void> StackingEdits::removeDisc(Id problem, bool goal, unsigned int rod,
                                       unsigned int index) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const stacking::StackMap& map = mapOf(d_.puzzle_->getProblem(*p), goal);
  if (rod >= map.rods.size() || map.rods[rod].empty()) {
    return fail(Errc::InvalidArgument, "That rod is empty");
  }
  if (index >= map.rods[rod].size()) {
    return fail(Errc::InvalidArgument, "there is no disc at that place on the rod");
  }
  const stacking::DiskRef disk = map.rods[rod][index];
  Document::StepGuard g(d_, "Remove disc");
  d_.touchProblem(*p);
  Problem& pr = d_.puzzle_->getProblem(*p);
  // off both stackings, the later copies renumbered, one copy fewer
  stacking::dropInstance(pr, disk.shapeId, disk.instance);
  if (pr.getShapeMinimum(disk.shapeId) > 0) {
    pr.setShapeMinimum(*d_.puzzle_, disk.shapeId, pr.getShapeMinimum(disk.shapeId) - 1);
  }
  if (pr.getShapeMaximum(disk.shapeId) > 0) {
    pr.setShapeMaximum(*d_.puzzle_, disk.shapeId, pr.getShapeMaximum(disk.shapeId) - 1);
  }
  d_.endSession(*p);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> StackingEdits::clearStacks(Id problem, bool goal) {
  if (Result<void> r = checkStacking(); !r) {
    return r;
  }
  const Result<unsigned int> p = d_.freeProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // Start and goal share the counts: only the stacking edited is emptied
  const stacking::StackMap& map = mapOf(d_.puzzle_->getProblem(*p), goal);
  if (std::all_of(map.rods.begin(), map.rods.end(),
                  [](const std::vector<stacking::DiskRef>& r) { return r.empty(); })) {
    return {};
  }
  Document::StepGuard g(d_, goal ? "Clear goal" : "Clear start");
  d_.problemChanged(*p);
  Problem& pr = d_.puzzle_->getProblem(*p);
  for (std::vector<stacking::DiskRef>& r : goal ? pr.editGoal().rods : pr.editStart().rods) {
    r.clear();
  }
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<bool> StackingEdits::setStacks(Id problem, bool goal,
                                      const std::vector<std::vector<unsigned int>>& stacks) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // refused before the step opens, so a refusal leaves no undo step
  const Problem& pr = d_.puzzle_->getProblem(*p);
  if (std::string why = stacking::positionRefusal(*d_.puzzle_, pr, stacks); !why.empty()) {
    return fail(Errc::InvalidArgument, std::move(why));
  }
  // the position it has already: the problem keeps what it had found
  stacking::StackMap map = stacking::positionMap(*d_.puzzle_, pr, stacks);
  if (map == mapOf(pr, goal)) {
    return false;
  }
  Document::StepGuard g(d_, goal ? "Set goal position" : "Set start position");
  d_.problemChanged(*p);
  Problem& edited = d_.puzzle_->getProblem(*p);
  (goal ? edited.editGoal() : edited.editStart()) = std::move(map);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return true;
}

Result<unsigned int> StackingEdits::moveDisc(Id problem, bool goal, unsigned int rod,
                                             unsigned int index, int delta) {
  const Result<unsigned int> p = stackingProblem(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (delta != -1 && delta != 1) {
    return fail(Errc::InvalidArgument, "a disc moves one place at a time");
  }
  const stacking::StackMap& map = mapOf(d_.puzzle_->getProblem(*p), goal);
  if (rod >= map.rods.size() || index >= map.rods[rod].size()) {
    return fail(Errc::InvalidArgument, "there is no disc at that place on the rod");
  }
  const int next = static_cast<int>(index) + delta;
  if (next < 0 || next >= static_cast<int>(map.rods[rod].size())) {
    return fail(Errc::InvalidArgument, "the disc cannot move further");
  }
  Document::StepGuard g(d_, "Move disc");
  d_.touchProblemSettings(*p);
  const bool moved =
      stacking::moveDisk(*d_.puzzle_, d_.puzzle_->getProblem(*p), goal, rod, index, delta);
  SL_ASSERT_MSG(moved, "a disc that could move did not");
  d_.endSession(*p);
  d_.lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return static_cast<unsigned int>(next);
}

}  // namespace sl::app
