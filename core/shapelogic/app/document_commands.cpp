// SPDX-License-Identifier: GPL-3.0-or-later
// The document's commands, ported from the callbacks of the original GUI:
// src/gui/mainwindow.cpp (cb_NewShape, cb_DeleteShape, cb_CopyShape,
// cb_NameShape, cb_WeightChange, cb_ShapeColor, cb_ShapeExchange,
// cb_AddColor, cb_RemoveColor, cb_ChangeColor, cb_NewProblem,
// cb_DeleteProblem, cb_CopyProblem, cb_RenameProblem, cb_ProblemExchange,
// cb_ProbShapeExchange, cb_ShapeToResult, cb_AddShapeToProblem,
// cb_AddAllShapesToProblem, cb_RemoveShapeFromProblem,
// cb_SetShapeMinimumToZero, cb_RemoveAllShapesFromProblem, cb_SetAllRange,
// cb_ShapeGroup, cb_AllowColor, cb_DisallowColor, changeShape,
// changeProblem), src/gui/tooltabs.cpp (resizeSpace, cb_transform),
// src/gui/grideditor.cpp (the cell tools), src/gui/groupseditor.cpp,
// src/gui/mainwindow_solve.cpp (cb_DeleteSolutions, cb_DeleteDisasm,
// cb_DeleteAllDisasm) and src/gui/mainwindow_file.cpp
// (cb_AssembliesToShapes) and cb_SolverOptions (mainwindow_solve.cpp). Each command checks what it is given first and
// changes the document only then, so a refused command changes nothing.
#include "shapelogic/app/document.h"

#include "shapelogic/app/document_internal.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/types.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/grid/voxel_table.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace bt::app {

namespace {

// The largest space a command makes (cells): a guard against sizes typed
// by mistake, far above any real puzzle.
constexpr std::uint64_t kMaxCells = std::uint64_t{1} << 24;

Result<void> checkSize(unsigned int x, unsigned int y, unsigned int z) {
  if (x == 0 || y == 0 || z == 0) {
    return fail(Errc::InvalidArgument, "a shape needs at least one cell in each direction");
  }
  if (std::uint64_t{x} * y * z > kMaxCells) {
    return fail(Errc::InvalidArgument, "the shape would be too large");
  }
  return {};
}

std::uint32_t packRgb(Rgb c) {
  return std::uint32_t{c.r} << 16 | std::uint32_t{c.g} << 8 | std::uint32_t{c.b};
}

}  // namespace

// Text that goes into the file: control characters other than tab,
// newline and carriage return are not well-formed XML 1.0 (and a name is
// an attribute, where a line break would come back as a space).
Result<void> checkText(std::string_view text, bool oneLine, std::string_view what) {
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      return fail(Errc::InvalidArgument, std::string(what) + " cannot hold control characters");
    }
    if (oneLine && (c == '\n' || c == '\r')) {
      return fail(Errc::InvalidArgument, std::string(what) + " cannot hold a line break");
    }
  }
  return {};
}

// ---- notes ---------------------------------------------------------------------------

Result<void> Document::setNotes(std::string text) {
  if (Result<void> r = checkText(text, false, "the notes"); !r) {
    return r;
  }
  if (text == puzzle_->getComment()) {
    return {};
  }
  StepGuard g(*this, "Edit notes");
  touchNotes();
  puzzle_->setComment(text);
  g.commit(kChangeNotes);
  return {};
}

// ---- shapes ------------------------------------------------------------------------

Result<Id> Document::createShape(std::optional<std::array<unsigned int, 3>> size) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  // a stacking puzzle's shapes are discs (cb_NewShape's stacking branch)
  if (stacking::isStacking(*puzzle_)) {
    if (size) {
      return fail(Errc::InvalidArgument,
                  "a stacking puzzle's shapes are discs: a new one has size 1, set its size "
                  "afterwards");
    }
    return addDisc();
  }
  const GridKind grid = puzzle_->getGridType().getType();
  std::array<unsigned int, 3> s{};
  if (size) {
    s = *size;
  } else {
    // sliding puzzles are mostly flat: a new shape is one layer, and may
    // be given more
    const unsigned int d = defaultShapeSize(grid);
    s = {d, d, grid == GridKind::Sliding ? 1U : d};
  }
  if (Result<void> r = checkSize(s[0], s[1], s[2]); !r) {
    return std::unexpected(r.error());
  }
  StepGuard g(*this, "New shape");
  touchAppendedShape();
  (void)puzzle_->addShape(s[0], s[1], s[2]);
  const Id id = newId();
  ids_.shapes.push_back(id);
  lastChange_.shapes = {id};
  g.commit(kChangeShapes);
  return id;
}

Result<Id> Document::duplicateShape(Id shape) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  // a copy of a sliding start/goal shape is a start/goal shape of its own
  // with the next number, and gets its problem (cb_CopyShape)
  const bool startGoal =
      sliding::isSliding(*puzzle_) && sliding::isStartGoalShape(&puzzle_->getShape(*s));
  StepGuard g(*this, "Duplicate shape");
  if (startGoal) {
    touchWhole();
  } else {
    touchAppendedShape();
  }
  const unsigned int copy = puzzle_->addShape(puzzle_->getShape(*s).clone());
  const Id id = newId();
  ids_.shapes.push_back(id);
  lastChange_.shapes = {id};
  if (startGoal) {
    int next = 1;
    for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
      if (i != copy) {
        next = std::max(next, sliding::startGoalNumber(&puzzle_->getShape(i)) + 1);
      }
    }
    Voxel& v = puzzle_->getShape(copy);
    v.setName("sg:" + std::to_string(next) + ":" + sliding::startGoalUserName(&v));
    syncSliding();
  }
  g.commit(kChangeShapes | lastChange_.what);
  return id;
}

Result<void> Document::deleteShape(Id shape) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  StepGuard g(*this, "Delete shape");
  touchWhole();
  endSessions();
  const bool slidingPuzzle = sliding::isSliding(*puzzle_);
  if (slidingPuzzle) {
    // the marks of the start/goal shapes follow the shapes' numbers
    sliding::noteShapeRemoved(*puzzle_, *s);
  }
  puzzle_->removeShape(*s);
  ids_.shapes.erase(ids_.shapes.begin() + static_cast<std::ptrdiff_t>(*s));
  if (slidingPuzzle) {
    syncSliding();
  }
  g.commit(kChangeShapes | kChangeProblems | lastChange_.what);
  return {};
}

Result<void> Document::moveShape(Id shape, unsigned int toIndex) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (toIndex >= puzzle_->getNumberOfShapes()) {
    return fail(Errc::InvalidArgument, "there is no such place in the list of shapes");
  }
  if (toIndex == *s) {
    return {};
  }
  // a sliding puzzle's boards stay above its pieces
  if (sliding::isSliding(*puzzle_)) {
    unsigned int boards = 0;
    for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
      boards += sliding::isStartGoalShape(&puzzle_->getShape(i)) ? 1 : 0;
    }
    const bool board = sliding::isStartGoalShape(&puzzle_->getShape(*s));
    if (board ? toIndex >= boards : toIndex < boards) {
      return fail(Errc::InvalidArgument, "the boards stay above the pieces");
    }
  }
  StepGuard g(*this, "Move shape");
  touchWhole();
  endSessions();
  // one place at a time, as the buttons did: the problems follow each
  // swap; on a sliding puzzle so do the marks, and a stamped piece that
  // crosses into the 64th place loses them (noteShapeSwap), which is told
  const bool slidingPuzzle = sliding::isSliding(*puzzle_);
  Id unmarked = 0;
  std::vector<Id> lostTrays;
  unsigned int at = *s;
  while (at != toIndex) {
    const unsigned int next = at < toIndex ? at + 1 : at - 1;
    if (slidingPuzzle) {
      const std::vector<unsigned int> lost = sliding::noteShapeSwap(*puzzle_, at, next);
      if (!lost.empty()) {
        // the shape going up is the one that crossed
        unmarked = ids_.shapes[std::min(at, next)];
        for (const unsigned int t : lost) {
          lostTrays.push_back(ids_.shapes[t]);
        }
      }
    }
    puzzle_->exchangeShapes(at, next);
    std::swap(ids_.shapes[at], ids_.shapes[next]);
    at = next;
  }
  if (!lostTrays.empty()) {
    slidingMarksLost(unmarked, lostTrays);
  } else if (slidingPuzzle) {
    // the boards are numbered by place, their problems named so
    syncSliding();
  }
  g.commit(kChangeShapes | kChangeProblems | lastChange_.what);
  return {};
}

Result<void> Document::renameShape(Id shape, std::string name) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (Result<void> r = checkText(name, true, "a name"); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  // a sliding start/goal shape keeps its "sg:<n>:": the name given is
  // the user's part (cb_NameShape)
  Voxel& named = puzzle_->getShape(*s);
  const bool startGoal = sliding::isSliding(*puzzle_) && sliding::isStartGoalShape(&named);
  // a piece named "sg:..." would become a start/goal shape at the next
  // sync (the original's cb_NameShape let it)
  if (sliding::isSliding(*puzzle_) && !startGoal && sliding::isReservedName(name)) {
    return fail(Errc::InvalidArgument,
                "a piece's name cannot begin with \"sg:\" or \"__sggoal:\" on a sliding puzzle");
  }
  // a board given the name it shows without one keeps none, so that the
  // name follows its number
  if (startGoal && name == sliding::defaultStartGoalName(sliding::startGoalNumber(&named))) {
    name.clear();
  }
  if ((startGoal ? sliding::startGoalUserName(&named) : named.getName()) == name) {
    return {};
  }
  StepGuard g(*this, "Rename shape");
  touchShape(*s);
  std::uint32_t what = kChangeShapes;
  if (startGoal) {
    sliding::setStartGoalUserName(&puzzle_->getShape(*s), name);
    // its problem is named as the board shows (syncSlidingProblems)
    for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
      Problem& pr = puzzle_->getProblem(p);
      if (pr.resultValid() && pr.getResultId() == *s) {
        touchProblem(p);
        pr.setName(sliding::displayName(&puzzle_->getShape(*s)));
        lastChange_.problems = {ids_.problems[p]};
        what |= kChangeProblems;
      }
    }
  } else {
    puzzle_->getShape(*s).setName(name);
  }
  lastChange_.shapes = {shape};
  g.commit(what);
  return {};
}

Result<void> Document::setShapeWeight(Id shape, int weight) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (puzzle_->getShape(*s).getWeight() == weight) {
    return {};
  }
  StepGuard g(*this, "Change weight");
  touchShape(*s);
  puzzle_->getShape(*s).setWeight(weight);
  lastChange_.shapes = {shape};
  g.commit(kChangeShapes);
  return {};
}

Result<void> Document::setShapeColour(Id shape, std::optional<std::uint32_t> rgb) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (rgb && *rgb > 0xFFFFFF) {
    return fail(Errc::InvalidArgument, "a colour is 0xRRGGBB");
  }
  const int value = rgb ? static_cast<int>(*rgb) : -1;
  if (puzzle_->getShape(*s).getShapeColor() == value) {
    return {};
  }
  StepGuard g(*this, "Shape colour");
  touchShape(*s);
  puzzle_->getShape(*s).setShapeColor(value);
  lastChange_.shapes = {shape};
  g.commit(kChangeShapes);
  return {};
}

Result<unsigned int> Document::editCells(Id shape, CellTool tool, Id colour,
                                         std::uint8_t symmetry,
                                         const std::vector<CellEdit>& cells) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = refuseOnStacking("a stacking puzzle's shapes are discs: they are not edited by cells"); !r) {
    return std::unexpected(r.error());
  }
  unsigned int colourNumber = 0;
  if (colour != 0) {
    const Result<unsigned int> c = colourIndex(colour);
    if (!c) {
      return std::unexpected(c.error());
    }
    colourNumber = *c + 1;
  }
  // on a copy first: an edit that changes nothing leaves no step
  std::unique_ptr<Voxel> v = puzzle_->getShape(*s).clone();
  const unsigned int n = app::editCells(*v, tool, colourNumber, symmetry, cells);
  if (n == 0) {
    return 0U;
  }
  const bool slidingPuzzle = sliding::isSliding(*puzzle_);
  StepGuard g(*this, "Edit cells");
  touchShape(*s);
  (void)puzzle_->replaceShape(*s, std::move(v));
  lastChange_.shapes = {shape};
  shapeChanged(*s);
  // the walls and floor of a start/goal shape: its problem follows (the
  // grid editor's RS_CHANGESQUARE)
  if (slidingPuzzle && sliding::isStartGoalShape(&puzzle_->getShape(*s))) {
    syncSliding();
  }
  g.commit(kChangeShapes | lastChange_.what);
  return n;
}

Result<unsigned int> Document::editBox(Id shape, CellTool tool, Id colour, std::uint8_t symmetry,
                                       const CellBox& box) {
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = refuseOnStacking("a stacking puzzle's shapes are discs: they are not edited by cells"); !r) {
    return std::unexpected(r.error());
  }
  // the box clamped to the space (setLayer)
  const Voxel& v = puzzle_->getShape(*s);
  const int x1 = std::max(std::min(box.x1, box.x2), 0);
  const int y1 = std::max(std::min(box.y1, box.y2), 0);
  const int z1 = std::max(std::min(box.z1, box.z2), 0);
  const int x2 = std::min(std::max(box.x1, box.x2), asInt(v.getX()) - 1);
  const int y2 = std::min(std::max(box.y1, box.y2), asInt(v.getY()) - 1);
  const int z2 = std::min(std::max(box.z1, box.z2), asInt(v.getZ()) - 1);
  std::vector<CellEdit> cells;
  for (int z = z1; z <= z2; z++) {
    for (int x = x1; x <= x2; x++) {
      for (int y = y1; y <= y2; y++) {
        cells.push_back(CellEdit{x, y, z});
      }
    }
  }
  return editCells(shape, tool, colour, symmetry, cells);
}

Result<void> Document::resizeShape(Id shape, unsigned int x, unsigned int y, unsigned int z,
                                   bool toAll) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking puzzle's shapes are discs: their size follows the disc's");
      !r) {
    return r;
  }
  const unsigned int factor = sizeUnit(puzzle_->getGridType().getType());
  if (Result<void> r = checkSize(factor * x, factor * y, factor * z); !r) {
    return r;
  }
  const Voxel& sel = puzzle_->getShape(*s);

  // the new size of every shape (resizeSpace)
  std::vector<std::array<unsigned int, 3>> sizes(puzzle_->getNumberOfShapes());
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    const Voxel& v = puzzle_->getShape(i);
    sizes[i] = {v.getX(), v.getY(), v.getZ()};
  }
  if (toAll) {
    // the others grow by as much as this one does (never shrink), to a
    // multiple of the unit
    const int dx = std::max(asInt(factor * x) - asInt(sel.getX()), 0);
    const int dy = std::max(asInt(factor * y) - asInt(sel.getY()), 0);
    const int dz = std::max(asInt(factor * z) - asInt(sel.getZ()), 0);
    for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
      const Voxel& v = puzzle_->getShape(i);
      const auto round = [factor](int n) {
        const int f = asInt(factor);
        return asUnsigned(f * ((std::max(n, 1) + f - 1) / f));
      };
      sizes[i] = {round(asInt(v.getX()) + dx), round(asInt(v.getY()) + dy),
                  round(asInt(v.getZ()) + dz)};
      if (Result<void> r = checkSize(sizes[i][0], sizes[i][1], sizes[i][2]); !r) {
        return r;
      }
    }
  }
  // this one always, also to shrink it with "apply to all" on
  sizes[*s] = {factor * x, factor * y, factor * z};
  StepGuard g(*this, "Resize");
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    Voxel& v = puzzle_->getShape(i);
    if (sizes[i][0] == v.getX() && sizes[i][1] == v.getY() && sizes[i][2] == v.getZ()) {
      continue;
    }
    touchShape(i);
    v.resize(sizes[i][0], sizes[i][1], sizes[i][2], 0);
    lastChange_.shapes.push_back(ids_.shapes[i]);
    shapeChanged(i);
  }
  g.commit(kChangeShapes | lastChange_.what);
  return {};
}

Result<TaskOutcome> Document::shapeTask(Id shape, ShapeTask task, bool toAll) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking puzzle's shapes are discs: the tool tabs do not change them");
      !r) {
    return std::unexpected(r.error());
  }
  const GridKind grid = puzzle_->getGridType().getType();
  const std::vector<ShapeTaskInfo> set = taskSet(grid);
  const auto info = std::find_if(set.begin(), set.end(),
                                 [task](const ShapeTaskInfo& t) { return t.task == task; });
  if (info == set.end()) {
    return fail(Errc::Unsupported, "this grid has no such shape operation");
  }
  // the shapes it applies to
  std::vector<unsigned int> which;
  if (toAll && info->toAll) {
    for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
      which.push_back(i);
    }
  } else {
    which.push_back(*s);
  }

  // on copies: a nudge that would push a shape out of the grid refuses
  // the whole command
  std::vector<std::unique_ptr<Voxel>> copies;
  for (const unsigned int i : which) {
    copies.push_back(puzzle_->getShape(i).clone());
  }
  TaskOutcome mine;
  std::vector<bool> changedShape(which.size(), false);
  if (task == ShapeTask::Minimize) {
    std::vector<Voxel*> all;
    for (std::unique_ptr<Voxel>& c : copies) {
      all.push_back(c.get());
    }
    minimizeShapes(all);
    for (std::size_t k = 0; k < which.size(); k++) {
      const Voxel& before = puzzle_->getShape(which[k]);
      const Voxel& after = *copies[k];
      changedShape[k] = !(after == before) || after.getHx() != before.getHx() ||
                        after.getHy() != before.getHy() || after.getHz() != before.getHz();
      if (which[k] == *s) {
        mine.changed = changedShape[k];
      }
    }
  } else {
    for (std::size_t k = 0; k < which.size(); k++) {
      const Result<TaskOutcome> o = applyTask(*copies[k], task);
      if (!o) {
        return std::unexpected(o.error());
      }
      changedShape[k] = o->changed;
      if (which[k] == *s) {
        mine = *o;
      }
    }
  }

  StepGuard g(*this, std::string(info->label));
  for (std::size_t k = 0; k < which.size(); k++) {
    if (!changedShape[k]) {
      continue;
    }
    touchShape(which[k]);
    (void)puzzle_->replaceShape(which[k], std::move(copies[k]));
    lastChange_.shapes.push_back(ids_.shapes[which[k]]);
    shapeChanged(which[k]);
  }
  g.commit(kChangeShapes | lastChange_.what);
  return mine;
}

Result<mesh::DrawMesh> Document::shapeTaskPreview(Id shape, ShapeTask task) const {
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking puzzle's shapes are discs: the tool tabs do not change them");
      !r) {
    return std::unexpected(r.error());
  }
  const std::vector<ShapeTaskInfo> set = taskSet(puzzle_->getGridType().getType());
  if (std::none_of(set.begin(), set.end(),
                   [task](const ShapeTaskInfo& t) { return t.task == task; })) {
    return fail(Errc::Unsupported, "this grid has no such shape operation");
  }
  const std::unique_ptr<Voxel> copy = puzzle_->getShape(*s).clone();
  if (task == ShapeTask::Minimize) {
    minimizeShapes({copy.get()});
  } else if (const Result<TaskOutcome> o = applyTask(*copy, task); !o) {
    return std::unexpected(o.error());
  }
  return mesh::shapeDrawMesh(*copy, mesh::MeshStyle::Drawing);
}

// ---- colours -----------------------------------------------------------------------

Result<Id> Document::addColour(Rgb rgb) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  if (puzzle_->colorNumber() >= Puzzle::kMaxColors) {
    return fail(Errc::InvalidArgument, "a puzzle has at most 63 colours");
  }
  StepGuard g(*this, "New colour");
  touchWhole();
  endSessions();
  (void)puzzle_->addColor(rgb.r, rgb.g, rgb.b);
  // pieces of the new colour may go on cells of it
  const unsigned int col = puzzle_->colorNumber();
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    puzzle_->getProblem(p).allowPlacement(*puzzle_, col, col);
  }
  const Id id = newId();
  ids_.colours.push_back(id);
  g.commit(kChangeColours | kChangeProblems);
  return id;
}

Result<void> Document::editColour(Id colour, Rgb rgb) {
  const Result<unsigned int> c = colourIndex(colour);
  if (!c) {
    return std::unexpected(c.error());
  }
  if (packRgb(puzzle_->getColor(*c)) == packRgb(rgb)) {
    return {};
  }
  // only how it looks: allowed while solving
  StepGuard g(*this, "Change colour");
  touchColour(*c);
  puzzle_->changeColor(*c, rgb.r, rgb.g, rgb.b);
  g.commit(kChangeColours);
  return {};
}

Result<void> Document::deleteColour(Id colour) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> c = colourIndex(colour);
  if (!c) {
    return std::unexpected(c.error());
  }
  // the original hid the colour group: a sliding puzzle's cell colours are
  // the start marks (shape index + 1), which removeColor would renumber
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument,
                "a sliding puzzle's cell colours are its start marks: a colour is not deleted");
  }
  const unsigned int col = *c + 1;
  StepGuard g(*this, "Delete colour");
  touchWhole();
  endSessions();
  // the shapes using it change (changeColor)
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    const Voxel& v = puzzle_->getShape(i);
    for (unsigned int j = 0; j < v.getXYZ(); j++) {
      if (v.getColor(j) == col) {
        shapeChanged(i);
        break;
      }
    }
  }
  puzzle_->removeColor(col);
  ids_.colours.erase(ids_.colours.begin() + static_cast<std::ptrdiff_t>(*c));
  g.commit(kChangeColours | kChangeShapes | kChangeProblems | lastChange_.what);
  return {};
}

// ---- problems ----------------------------------------------------------------------

Result<Id> Document::createProblem() {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  // the original greyed New, Copy and Delete problem out: a sliding
  // puzzle's problems follow its start/goal shapes (syncSlidingProblems
  // would drop a problem of no start/goal shape at the next change)
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument,
                "a sliding puzzle's problems follow its start/goal shapes: add a start/goal "
                "shape instead");
  }
  StepGuard g(*this, "New problem");
  touchWhole();
  endSessions();
  const unsigned int p = puzzle_->addProblem();
  for (unsigned int c = 0; c < puzzle_->colorNumber(); c++) {
    puzzle_->getProblem(p).allowPlacement(*puzzle_, c + 1, c + 1);
  }
  const Id id = newId();
  ids_.problems.push_back(id);
  lastChange_.problems = {id};
  g.commit(kChangeProblems);
  return id;
}

Result<Id> Document::duplicateProblem(Id problem) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument,
                "a sliding puzzle's problems follow its start/goal shapes: duplicate the "
                "shape instead");
  }
  StepGuard g(*this, "Duplicate problem");
  touchWhole();
  endSessions();
  (void)puzzle_->addProblem(puzzle_->getProblem(*p));
  const Id id = newId();
  ids_.problems.push_back(id);
  lastChange_.problems = {id};
  g.commit(kChangeProblems);
  return id;
}

Result<void> Document::deleteProblem(Id problem) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument,
                "a sliding puzzle's problems follow its start/goal shapes: delete the shape "
                "instead");
  }
  StepGuard g(*this, "Delete problem");
  touchWhole();
  endSessions();
  puzzle_->removeProblem(*p);
  ids_.problems.erase(ids_.problems.begin() + static_cast<std::ptrdiff_t>(*p));
  g.commit(kChangeProblems);
  return {};
}

Result<void> Document::moveProblem(Id problem, unsigned int toIndex) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (toIndex >= puzzle_->getNumberOfProblems()) {
    return fail(Errc::InvalidArgument, "there is no such place in the list of problems");
  }
  if (toIndex == *p) {
    return {};
  }
  StepGuard g(*this, "Move problem");
  touchWhole();
  endSessions();
  unsigned int at = *p;
  while (at != toIndex) {
    const unsigned int next = at < toIndex ? at + 1 : at - 1;
    puzzle_->exchangeProblems(at, next);
    std::swap(ids_.problems[at], ids_.problems[next]);
    at = next;
  }
  g.commit(kChangeProblems);
  return {};
}

Result<void> Document::renameProblem(Id problem, std::string name) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = checkText(name, true, "a name"); !r) {
    return r;
  }
  // a sliding board's problem is named as its board: the board is renamed
  if (const Problem& pr = puzzle_->getProblem(*p);
      sliding::isSliding(*puzzle_) && pr.resultValid() &&
      sliding::isStartGoalShape(&puzzle_->getShape(pr.getResultId()))) {
    return renameShape(ids_.shapes[pr.getResultId()], std::move(name));
  }
  if (puzzle_->getProblem(*p).getName() == name) {
    return {};
  }
  StepGuard g(*this, "Rename problem");
  touchProblem(*p);
  puzzle_->getProblem(*p).setName(name);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return {};
}

Result<void> Document::setResult(Id problem, Id shape) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument,
                "a sliding problem's result is its start/goal shape: it is not chosen");
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking problem has a rod set, not a result: set its start/goal rods");
      !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (pr.resultValid() && pr.getResultId() == *s && pr.getShapeMaximum(*s) == 0) {
    return {};
  }
  StepGuard g(*this, "Set result");
  touchProblem(*p);
  // a piece no more
  pr.setShapeMaximum(*puzzle_, *s, 0);
  pr.setResultId(*puzzle_, *s);
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setPieceCount(Id problem, Id shape, unsigned int min, unsigned int max) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = refuseOnStacking("a stacking problem's discs follow its stacks: place them on its rods"); !r) {
    return r;
  }
  if (min > max) {
    return fail(Errc::InvalidArgument, "the minimum is above the maximum");
  }
  if (max > 0xFFFF) {
    return fail(Errc::InvalidArgument, "too many pieces");
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (max > 0 && pr.resultValid() && pr.getResultId() == *s) {
    return fail(Errc::InvalidArgument, "the result cannot be a piece");
  }
  if (max > 0 && sliding::isSliding(*puzzle_) &&
      sliding::isStartGoalShape(&puzzle_->getShape(*s))) {
    return fail(Errc::InvalidArgument, "a board cannot be a piece");
  }
  if (pr.getShapeMinimum(*s) == min && pr.getShapeMaximum(*s) == max) {
    return {};
  }
  // a copy for each of its starts and goals on the problem's board
  if (const unsigned int placed =
          sliding::isSliding(*puzzle_) ? sliding::stampedCopies(*puzzle_, pr, *s) : 0;
      min < placed) {
    return fail(Errc::InvalidArgument,
                shapeLabelOf(*s) + " has " + std::to_string(placed) +
                    (placed == 1 ? " copy" : " copies") +
                    " placed at its start or goal on this problem's board: take one off first");
  }
  StepGuard g(*this, "Piece count");
  touchProblem(*p);
  pr.setShapeMaximum(*puzzle_, *s, max);
  if (max > 0) {
    pr.setShapeMinimum(*puzzle_, *s, min);
  }
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::addPiece(Id problem, Id shape) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  const Problem& pr = puzzle_->getProblem(*p);
  return setPieceCount(problem, shape, pr.getShapeMinimum(*s) + 1, pr.getShapeMaximum(*s) + 1);
}

Result<void> Document::removePiece(Id problem, Id shape) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  const Problem& pr = puzzle_->getProblem(*p);
  const unsigned int min = pr.getShapeMinimum(*s);
  const unsigned int max = pr.getShapeMaximum(*s);
  return setPieceCount(problem, shape, min > 0 ? min - 1 : 0, max > 0 ? max - 1 : 0);
}

Result<void> Document::setRangeAll(Id problem, unsigned int min, unsigned int max) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = refuseOnStacking("a stacking problem's discs follow its stacks: place them on its rods"); !r) {
    return r;
  }
  // the bulk range window: a maximum below the minimum is raised to it
  max = std::max(max, min);
  if (max > 0xFFFF) {
    return fail(Errc::InvalidArgument, "too many pieces");
  }
  StepGuard g(*this, "Set range for all pieces");
  problemChanged(*p);
  Problem& pr = puzzle_->getProblem(*p);
  // the parts as they are now: a maximum of 0 removes a part; a sliding
  // piece with a start or goal keeps its count
  std::vector<unsigned int> shapes;
  for (unsigned int i = 0; i < pr.getNumberOfParts(); i++) {
    if (!slidingRequired(*p, pr.getShapeIdOfPart(i))) {
      shapes.push_back(pr.getShapeIdOfPart(i));
    }
  }
  for (const unsigned int s : shapes) {
    pr.setShapeMaximum(*puzzle_, s, max);
    if (max > 0) {
      pr.setShapeMinimum(*puzzle_, s, min);
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::addOneOfEach(Id problem) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = refuseOnStacking("a stacking problem's discs follow its stacks: place them on its rods"); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  StepGuard g(*this, "Add one of each");
  touchProblem(*p);
  for (unsigned int j = 0; j < puzzle_->getNumberOfShapes(); j++) {
    // not the result, nor a sliding piece with a start or goal, nor a board
    if ((pr.resultValid() && j == pr.getResultId()) || slidingRequired(*p, j) ||
        (sliding::isSliding(*puzzle_) && sliding::isStartGoalShape(&puzzle_->getShape(j)))) {
      continue;
    }
    pr.setShapeMaximum(*puzzle_, j, pr.getShapeMaximum(j) + 1);
    pr.setShapeMinimum(*puzzle_, j, pr.getShapeMinimum(j) + 1);
  }
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::removeAllPieces(Id problem) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking problem's discs follow its stacks: clear its start or goal rods");
      !r) {
    return r;
  }
  if (puzzle_->getProblem(*p).getNumberOfParts() == 0) {
    return {};
  }
  StepGuard g(*this, "Remove all pieces");
  // a sliding piece with a start or goal stays
  std::vector<bool> keep(puzzle_->getNumberOfShapes());
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    keep[i] = slidingRequired(*p, i);
  }
  problemChanged(*p);
  Problem& pr = puzzle_->getProblem(*p);
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    if (!keep[i]) {
      pr.setShapeMaximum(*puzzle_, i, 0);
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::movePart(Id problem, Id shape, int delta) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (Result<void> r = refuseOnStacking(
          "a stacking problem's discs are moved on their rods, not in its list of pieces");
      !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  unsigned int current = 0;
  while (current < pr.getNumberOfParts() && pr.getShapeIdOfPart(current) != *s) {
    current++;
  }
  if (current >= pr.getNumberOfParts()) {
    return fail(Errc::InvalidArgument, "the shape is no piece of the problem");
  }
  const int other = asInt(current) + delta;
  if (delta != -1 && delta != 1) {
    return fail(Errc::InvalidArgument, "a part moves one place at a time");
  }
  if (other < 0 || other >= asInt(pr.getNumberOfParts())) {
    return fail(Errc::InvalidArgument, "the part cannot move further");
  }
  StepGuard g(*this, "Move part");
  touchProblem(*p);
  pr.exchangeParts(current, asUnsigned(other));
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setGroupCount(Id problem, Id shape, unsigned int group,
                                     unsigned int count) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument, "sliding puzzles have no piece groups");
  }
  if (Result<void> r = refuseOnStacking("stacking puzzles have no piece groups"); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (pr.getShapeMaximum(*s) == 0) {
    return fail(Errc::InvalidArgument, "the shape is no piece of the problem");
  }
  if (group == 0 || group > 0xFFFF || count > 0xFFFF) {
    return fail(Errc::InvalidArgument, "groups are numbered from 1");
  }
  StepGuard g(*this, "Group");
  touchProblem(*p);
  pr.setPartGroup(pr.getPartIdForShape(*s), static_cast<unsigned short>(group),
                  static_cast<unsigned short>(count));
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setGroup(Id problem, Id shape, unsigned int group) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument, "sliding puzzles have no piece groups");
  }
  if (Result<void> r = refuseOnStacking("stacking puzzles have no piece groups"); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (pr.getShapeMaximum(*s) == 0) {
    return fail(Errc::InvalidArgument, "the shape is no piece of the problem");
  }
  if (group > 0xFFFF) {
    return fail(Errc::InvalidArgument, "no such group");
  }
  const unsigned int part = pr.getPartIdForShape(*s);
  StepGuard g(*this, "Group");
  touchProblem(*p);
  // out of every group it is in, then all its pieces into this one
  while (pr.getNumberOfPartGroups(part) > 0) {
    pr.setPartGroup(part, pr.getPartGroupId(part, 0), 0);
  }
  if (group != 0) {
    pr.setPartGroup(part, static_cast<unsigned short>(group),
                    static_cast<unsigned short>(pr.getPartMaximum(part)));
  }
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::deleteGroup(Id problem, unsigned int group) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (sliding::isSliding(*puzzle_)) {
    return fail(Errc::InvalidArgument, "sliding puzzles have no piece groups");
  }
  if (Result<void> r = refuseOnStacking("stacking puzzles have no piece groups"); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  StepGuard g(*this, "Delete group");
  for (unsigned int part = 0; part < pr.getNumberOfParts(); part++) {
    for (unsigned int k = 0; k < pr.getNumberOfPartGroups(part); k++) {
      if (pr.getPartGroupId(part, k) == group) {
        touchProblem(*p);
        pr.setPartGroup(part, static_cast<unsigned short>(group), 0);
        endSession(*p);
        break;
      }
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setColourRule(Id problem, Id pieceColour, Id resultColour, bool allowed) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Result<unsigned int> pc = colourIndex(pieceColour);
  if (!pc) {
    return std::unexpected(pc.error());
  }
  const Result<unsigned int> rc = colourIndex(resultColour);
  if (!rc) {
    return std::unexpected(rc.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (pr.placementAllowed(*puzzle_, *pc + 1, *rc + 1) == allowed) {
    return {};
  }
  StepGuard g(*this, allowed ? "Allow colour" : "Disallow colour");
  problemChanged(*p);
  if (allowed) {
    pr.allowPlacement(*puzzle_, *pc + 1, *rc + 1);
  } else {
    pr.disallowPlacement(*puzzle_, *pc + 1, *rc + 1);
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setColourRules(Id problem, ColourPreset preset) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  const unsigned int n = puzzle_->colorNumber();
  StepGuard g(*this, "Colour rules");
  problemChanged(*p);
  for (unsigned int a = 1; a <= n; a++) {
    for (unsigned int b = 1; b <= n; b++) {
      const bool allow = preset == ColourPreset::AllowAll ||
                         (preset == ColourPreset::EachOnItself && a == b);
      if (allow) {
        pr.allowPlacement(*puzzle_, a, b);
      } else {
        pr.disallowPlacement(*puzzle_, a, b);
      }
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeProblems | kChangeSolutions);
  return {};
}

Result<void> Document::setSolverOptions(Id problem, std::map<std::string, std::string> options) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  if (pr.getSolverOptions() == options) {
    return {};
  }
  // only stored: the solve and the solutions stay
  StepGuard g(*this, "Solver options");
  touchProblem(*p);
  pr.setSolverOptions(options);
  lastChange_.problems = {problem};
  g.commit(kChangeProblems);
  return {};
}

// ---- solutions ---------------------------------------------------------------------

Result<void> Document::deleteSolutions(Id problem, SolutionsToDelete which, Id solution) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  Problem& pr = puzzle_->getProblem(*p);
  unsigned int sol = 0;
  if (which == SolutionsToDelete::One || which == SolutionsToDelete::Before ||
      which == SolutionsToDelete::After) {
    const Result<unsigned int> s = solutionIndex(problem, solution);
    if (!s) {
      return std::unexpected(s.error());
    }
    sol = *s;
  }
  if (pr.getNumberOfSavedSolutions() == 0) {
    return {};
  }
  StepGuard g(*this, "Remove assemblies");
  touchProblem(*p);
  switch (which) {
    case SolutionsToDelete::All:
      while (pr.getNumberOfSavedSolutions() > 0) {
        pr.removeSolution(0);
      }
      break;
    case SolutionsToDelete::Before:
      for (unsigned int i = 0; i < sol; i++) {
        pr.removeSolution(0);
      }
      break;
    case SolutionsToDelete::One:
      pr.removeSolution(sol);
      break;
    case SolutionsToDelete::After:
      while (pr.getNumberOfSavedSolutions() > sol + 1) {
        pr.removeSolution(sol + 1);
      }
      break;
    case SolutionsToDelete::WithoutDisassembly: {
      unsigned int i = 0;
      while (i < pr.getNumberOfSavedSolutions()) {
        if (pr.getSavedSolution(i).getDisassemblyInfo() != nullptr) {
          i++;
        } else {
          pr.removeSolution(i);
        }
      }
      break;
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeSolutions);
  return {};
}

Result<void> Document::deleteDisassemblies(Id problem, std::optional<Id> solution) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  std::optional<unsigned int> one;
  if (solution) {
    const Result<unsigned int> s = solutionIndex(problem, *solution);
    if (!s) {
      return std::unexpected(s.error());
    }
    one = *s;
  }
  Problem& pr = puzzle_->getProblem(*p);
  StepGuard g(*this, "Remove disassemblies");
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    if ((!one || *one == i) && pr.getSavedSolution(i).getDisassembly() != nullptr) {
      touchProblem(*p);
      pr.getSavedSolution(i).removeDisassembly();
    }
  }
  lastChange_.problems = {problem};
  g.commit(kChangeSolutions);
  return {};
}

Result<std::vector<Id>> Document::assembliesToShapes(Id problem, const AssemblyImport& opt) {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  std::optional<unsigned int> one;
  if (opt.solution) {
    const Result<unsigned int> s = solutionIndex(problem, *opt.solution);
    if (!s) {
      return std::unexpected(s.error());
    }
    one = *s;
  }
  std::optional<unsigned int> target;
  if (opt.addTo == AssemblyImport::AddTo::Problem) {
    if (!opt.target) {
      return fail(Errc::InvalidArgument, "no problem to add the pieces to");
    }
    const Result<unsigned int> t = problemIndex(*opt.target);
    if (!t) {
      return std::unexpected(t.error());
    }
    target = *t;
  }
  if (opt.addTo != AssemblyImport::AddTo::Nothing && (opt.min > opt.max || opt.max > 0xFFFF)) {
    return fail(Errc::InvalidArgument, "the minimum is above the maximum");
  }

  // the shapes, filtered (as the original's dialog)
  const Problem& pr = puzzle_->getProblem(*p);
  const Symmetries& sym = puzzle_->getGridType().getSymmetries();
  std::vector<std::unique_ptr<Voxel>> shapes;
  VoxelTable table([&shapes](unsigned int i) -> const Voxel& { return *shapes[i]; });
  for (unsigned int s = 0; s < pr.getNumberOfSavedSolutions(); s++) {
    if (one && *one != s) {
      continue;
    }
    std::unique_ptr<Voxel> shape = pr.getSavedSolution(s).getAssembly().createSpace(*puzzle_, pr);
    if (opt.dropDisconnected && !shape->connected(0, true, Voxel::kEmpty)) {
      continue;
    }
    const Symmetry ss = shape->selfSymmetries();
    if (opt.dropMirror && sym.symmetryContainsMirror(ss)) {
      continue;
    }
    if (opt.dropSymmetric && !unSymmetric(ss)) {
      continue;
    }
    if (opt.dropNonMillable && !isMillable(*shape)) {
      continue;
    }
    if (opt.dropNonNotchable && !isNotchable(*shape)) {
      continue;
    }
    const unsigned int cells = shape->countState(Voxel::kFilled);
    if (cells < opt.minCells || cells > opt.maxCells) {
      continue;
    }
    if (opt.dropIdentical && table.getSpace(*shape)) {
      continue;
    }
    shapes.push_back(std::move(shape));
    if (opt.dropIdentical) {
      table.addSpace(static_cast<unsigned int>(shapes.size() - 1));
    }
  }

  StepGuard g(*this, "Convert assemblies to pieces");
  touchWhole();
  endSessions();
  std::optional<unsigned int> dest = target;
  if (opt.addTo == AssemblyImport::AddTo::NewProblem) {
    dest = puzzle_->addProblem();
    const Id id = newId();
    ids_.problems.push_back(id);
    lastChange_.problems = {id};
  }
  std::vector<Id> added;
  for (std::unique_ptr<Voxel>& shape : shapes) {
    const unsigned int i = puzzle_->addShape(std::move(shape));
    const Id id = newId();
    ids_.shapes.push_back(id);
    added.push_back(id);
    if (dest) {
      Problem& d = puzzle_->getProblem(*dest);
      d.setShapeMaximum(*puzzle_, i, opt.max);
      d.setShapeMinimum(*puzzle_, i, opt.min);
    }
  }
  lastChange_.shapes = added;
  g.commit(kChangeShapes | kChangeProblems);
  return added;
}

}  // namespace bt::app
