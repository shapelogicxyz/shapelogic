// SPDX-License-Identifier: GPL-3.0-or-later
// The document's core: ids, the undo history (src/gui/shapehistory.cpp,
// generalised: each step keeps the parts of the document it changed, as
// they were, and undo and redo swap them back and forth), the guards of
// decision 0011, files, solving and queries. The commands are in
// document_commands.cpp.
#include "shapelogic/app/document.h"

#include "shapelogic/app/document_internal.h"
#include "shapelogic/app/solve_setup.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/imports.h"
#include "shapelogic/io/paused_solve.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/mesh/stl.h"
#include "shapelogic/model/grid_convert.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solver_type.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace bt::app {

// ---- construction and files ---------------------------------------------------------

Document::Document(GridKind grid, DocumentSettings settings)
    : Document(std::make_unique<Puzzle>(GridType(grid)), settings) {}

Document::Document(std::unique_ptr<Puzzle> puzzle, DocumentSettings settings)
    : puzzle_(std::move(puzzle)), settings_(settings) {
  // a tilted puzzle's board and shapes follow its tray
  if (puzzle_->tray()) {
    adoptTray();
  }
  assignIds();
  // a file from the original may list a board below a piece
  if (sliding::isSliding(*puzzle_)) {
    boardsAbovePieces();
  }
}

Document::Document(Document&&) noexcept = default;

Document::~Document() {
  // the background jobs and solves first: they hold the puzzle
  jobs_.clear();
  sessions_.clear();
}

Result<Document> Document::open(const std::filesystem::path& path, DocumentSettings settings) {
  Result<Puzzle> p = io::loadPuzzle(path);
  if (!p) {
    return std::unexpected(p.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(*p)), settings);
  return d;
}

Result<Document> Document::openBytes(std::string_view bytes, DocumentSettings settings) {
  // a file's bytes: gzip, or plain XML
  Result<std::string> xml = io::gunzipIfCompressed(bytes);
  if (!xml) {
    return std::unexpected(xml.error());
  }
  Result<Puzzle> p = io::parsePuzzle(*xml);
  if (!p) {
    return std::unexpected(p.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(*p)), settings);
  return d;
}

Result<std::pair<Document, Id>> Document::importPaused(const std::filesystem::path& path,
                                                     DocumentSettings settings,
                                                     const std::string& stackingWorkDir) {
  // where Continue looks for a stacking problem's saved search
  const io::SavedSearchFolder folder = [&stackingWorkDir](const Puzzle& puzzle,
                                                          unsigned int problem) {
    return std::filesystem::path(
        panex::searchFolder(puzzle, puzzle.getProblem(problem), stackingWorkDir));
  };
  Result<io::PausedSolve> ps = io::loadPausedSolve(path, folder);
  if (!ps) {
    return std::unexpected(ps.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(ps->puzzle)), settings);
  // a new, untitled puzzle: it is not saved anywhere yet
  d.solveDirty_ = true;
  const Id problem = d.ids_.problems[ps->problem];
  return std::pair<Document, Id>(std::move(d), problem);
}

Result<Document> Document::importBytes(ImportFormat format, std::string_view bytes,
                                       DocumentSettings settings) {
  Result<Puzzle> p = format == ImportFormat::PuzzleSolver3D ? io::readPuzzleSolver3D(bytes)
                                                            : io::readPuzzlecad(bytes);
  if (!p) {
    return std::unexpected(p.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(*p)), settings);
  // no file name: the source is another format, which Save must not write
  // over (cb_Load_Ps3d's setFileName("") and changed = true)
  d.solveDirty_ = true;
  return d;
}

Result<Document> Document::importFile(ImportFormat format, const std::filesystem::path& path,
                                      DocumentSettings settings) {
  const Result<std::string> bytes = io::readPlainFile(path);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  return importBytes(format, *bytes, settings);
}

Result<Document> Document::convertGrid(GridKind target) const {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  Result<Puzzle> p = bt::convertGrid(*puzzle_, target);
  if (!p) {
    return std::unexpected(p.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(*p)), settings_);
  d.solveDirty_ = true;
  return d;
}

Result<std::string> Document::exportPuzzlecad(std::optional<Id> problem,
                                              std::string_view sourceName,
                                              const io::PuzzlecadOptions& options) const {
  if (const std::string why = io::puzzlecadRefusal(*puzzle_, nullptr, options); !why.empty()) {
    return fail(Errc::InvalidArgument, why);
  }
  std::optional<std::string> text;
  if (problem) {
    const Result<unsigned int> p = problemIndex(*problem);
    if (!p) {
      return std::unexpected(p.error());
    }
    // the copies' cap, now that the problem is known
    if (const std::string why = io::puzzlecadRefusal(*puzzle_, &puzzle_->getProblem(*p), options);
        !why.empty()) {
      return fail(Errc::InvalidArgument, why);
    }
    // a problem being solved keeps its parts; its solve writes the rest
    text = readProblem(*p, [this, sourceName, &options](const Problem& pr) {
      return io::writePuzzlecad(*puzzle_, &pr, sourceName, options);
    });
  } else {
    text = io::writePuzzlecad(*puzzle_, nullptr, sourceName, options);
  }
  if (!text) {
    return fail(Errc::InvalidArgument, "Could not export puzzlecad file.");
  }
  return std::move(*text);
}

Result<void> Document::exportPaused(Id problem, const std::filesystem::path& path,
                                    const PausedExport& options) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  const Problem& pr = puzzle_->getProblem(*p);
  // a stacking solve is paused when its search is saved (problemPaused)
  if (stacking::isStacking(*puzzle_)) {
    if (panex::savedSearch(*puzzle_, pr, options.stackingWorkDir).empty()) {
      return fail(Errc::InvalidState, "pause a solve first: the problem has no paused solve");
    }
    const io::SavedSearchFiles search{
        panex::searchFolder(*puzzle_, pr, options.stackingWorkDir), options.withLevels};
    return io::savePausedSolve(*puzzle_, *p, path, &search);
  }
  if (pr.getSolveState() != ProblemState::Solving) {
    return fail(Errc::InvalidState, "pause a solve first: the problem has no paused solve");
  }
  return io::savePausedSolve(*puzzle_, *p, path);
}

void Document::assignIds() {
  ids_ = Ids{};
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    ids_.shapes.push_back(newId());
  }
  for (unsigned int i = 0; i < puzzle_->colorNumber(); i++) {
    ids_.colors.push_back(newId());
  }
  for (unsigned int i = 0; i < puzzle_->getNumberOfProblems(); i++) {
    ids_.problems.push_back(newId());
  }
  for (unsigned int i = 0; i < puzzle_->rodSetCount(); i++) {
    ids_.rodSets.push_back(newId());
  }
}

Result<void> Document::save(const std::filesystem::path& path) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (Result<void> r = io::savePuzzle(*puzzle_, path); !r) {
    return r;
  }
  cleanStamp_ = currentStamp();
  solveDirty_ = false;
  revision_++;
  lastChange_ = ChangeSet{};
  return {};
}

Result<std::string> Document::saveBytes() const {
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  return io::writePuzzle(*puzzle_);
}

bool Document::dirty() const { return solveDirty_ || currentStamp() != cleanStamp_; }

std::uint64_t Document::currentStamp() const {
  return undo_.empty() ? baseStamp_ : undo_.back().stamp;
}

std::size_t Document::Snapshot::bytes() const {
  std::size_t n = 0;
  if (shape) {
    n += shape->getXYZ() * sizeof(VoxelType);
  }
  if (problem) {
    n += problem->solutionBytes();
  }
  if (whole) {
    for (unsigned int i = 0; i < whole->getNumberOfShapes(); i++) {
      n += whole->getShape(i).getXYZ() * sizeof(VoxelType);
    }
    for (unsigned int i = 0; i < whole->getNumberOfProblems(); i++) {
      n += whole->getProblem(i).solutionBytes();
    }
  }
  return n;
}

void Document::Step::count() {
  bytes = 0;
  for (const Snapshot& s : parts) {
    bytes += s.bytes();
  }
}

void Document::dropOldestSteps() {
  while (undo_.size() > settings_.undoDepth ||
         (undo_.size() > 1 && undoBytesHeld() > settings_.undoBytes)) {
    // the state below the steps left is the one after the step that goes
    baseStamp_ = undo_.front().stamp;
    undo_.erase(undo_.begin());
  }
}

void Document::setSettings(const DocumentSettings& settings) {
  settings_ = settings;
  dropOldestSteps();
}

// ---- ids ----------------------------------------------------------------------------

namespace {

Result<unsigned int> indexIn(const std::vector<Id>& ids, Id id, const char* what) {
  const auto it = std::find(ids.begin(), ids.end(), id);
  if (id == 0 || it == ids.end()) {
    return fail(Errc::NotFound, std::string("there is no such ") + what);
  }
  return static_cast<unsigned int>(it - ids.begin());
}

}  // namespace

Result<unsigned int> Document::shapeIndex(Id id) const { return indexIn(ids_.shapes, id, "shape"); }

Result<unsigned int> Document::colorIndex(Id id) const {
  return indexIn(ids_.colors, id, "color");
}

Result<unsigned int> Document::problemIndex(Id id) const {
  return indexIn(ids_.problems, id, "problem");
}

Result<unsigned int> Document::rodSetIndex(Id id) const {
  return indexIn(ids_.rodSets, id, "rod set");
}

Result<unsigned int> Document::solutionIndex(Id problem, Id solution) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return p;
  }
  return readProblem(*p, [solution](const Problem& pr) -> Result<unsigned int> {
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      if (pr.getSavedSolution(i).getId() == solution) {
        return i;
      }
    }
    return fail(Errc::NotFound, "there is no such solution");
  });
}

std::string Document::shapeLabel(unsigned int index) { return "S" + std::to_string(index + 1); }

std::string Document::shapeLabelOf(unsigned int index) const {
  // a tilted puzzle: the board, then the pieces from S1
  if (tilted()) {
    return index == 0 ? "B" : "S" + std::to_string(index);
  }
  return sliding::shapeLabel(*puzzle_, index);
}

std::string Document::colorLabel(unsigned int index) { return "C" + std::to_string(index + 1); }

std::string Document::problemLabel(unsigned int index) { return "P" + std::to_string(index + 1); }

std::string Document::rodSetLabel(unsigned int index) { return "R" + std::to_string(index + 1); }

// ---- the history --------------------------------------------------------------------

Document::StepGuard::StepGuard(Document& doc, std::string label)
    : doc_(doc), grouped_(doc.grouping_), touches_(doc.touches_) {
  doc_.lastChange_ = ChangeSet{};
  if (!grouped_) {
    BT_ASSERT(doc_.open_ == nullptr);
    doc_.open_ = std::make_unique<Step>();
    doc_.open_->label = std::move(label);
  }
  first_ = doc_.open_->parts.size();
}

// undoing moves what the step holds: only a failed allocation could throw:
// NOLINTNEXTLINE(bugprone-exception-escape)
Document::StepGuard::~StepGuard() {
  if (committed_) {
    return;
  }
  // a command that failed half way (a broken invariant) leaves nothing
  // behind: what it changed goes back
  Step& s = *doc_.open_;
  while (s.parts.size() > first_) {
    doc_.swapIn(s.parts.back());
    s.parts.pop_back();
  }
  if (!grouped_) {
    doc_.open_.reset();
  }
}

void Document::StepGuard::commit(std::uint32_t what) {
  committed_ = true;
  Step& s = *doc_.open_;
  const bool saved = s.parts.size() > first_;
  // a command touches only what it changes; in a group a part touched
  // before is saved once, so the touches say whether this command changed
  // anything (a second batch of a drag on the same shape saves nothing
  // new, and is still a change with a revision of its own)
  const bool any = saved || doc_.touches_ > touches_;
  // with what the command's helpers reported (shapeChanged, syncSliding):
  // undo and redo tell it again
  s.what |= what | doc_.lastChange_.what;
  if (!grouped_) {
    if (saved) {
      doc_.pushStep(std::move(s));
    }
    doc_.open_.reset();
  }
  if (any) {
    doc_.changed(what);
  }
}

bool Document::covered(SnapshotKind kind, unsigned int index) const {
  BT_ASSERT_MSG(open_ != nullptr, "an edit outside a command");
  for (const Snapshot& s : open_->parts) {
    // after a whole copy nothing more is needed: undo puts it back last
    if (s.kind == SnapshotKind::Whole) {
      return true;
    }
    if (s.kind == kind && s.index == index) {
      return true;
    }
  }
  return false;
}

void Document::touchShape(unsigned int index) {
  touches_++;
  if (covered(SnapshotKind::Shape, index)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Shape;
  s.index = index;
  s.shape = puzzle_->getShape(index).clone();
  open_->parts.push_back(std::move(s));
}

void Document::touchProblem(unsigned int index) {
  touches_++;
  BT_ASSERT_MSG(open_ != nullptr, "an edit outside a command");
  Snapshot* settings = nullptr;
  for (Snapshot& s : open_->parts) {
    // after a whole copy nothing more is needed: undo puts it back last
    if (s.kind == SnapshotKind::Whole) {
      return;
    }
    if (s.kind == SnapshotKind::Problem && s.index == index) {
      if (!s.settingsOnly) {
        return;
      }
      settings = &s;
    }
  }
  if (settings != nullptr) {
    // The step has the problem without its solutions (touchProblemSettings,
    // an earlier command of the group), and this command may change them:
    // they are added to that copy. They are still what they were when it
    // was made, since nothing changes them without coming through here or
    // through touchWhole first.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a problem's snapshot holds its problem
    settings->problem->copySolutionsFrom(puzzle_->getProblem(index));
    settings->settingsOnly = false;
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Problem;
  s.index = index;
  s.problem = puzzle_->getProblem(index);
  open_->parts.push_back(std::move(s));
}

void Document::touchProblemSettings(unsigned int index) {
  touches_++;
  // (a copy with or without the solutions, or a whole copy)
  if (covered(SnapshotKind::Problem, index)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Problem;
  s.index = index;
  s.settingsOnly = true;
  s.problem = puzzle_->getProblem(index).copyWithoutSolutions();
  open_->parts.push_back(std::move(s));
}

void Document::touchColor(unsigned int index) {
  touches_++;
  if (covered(SnapshotKind::Color, index)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Color;
  s.index = index;
  s.color = puzzle_->getColor(index);
  open_->parts.push_back(std::move(s));
}

void Document::touchNotes() {
  touches_++;
  if (covered(SnapshotKind::Notes, 0)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Notes;
  s.comment = puzzle_->getComment();
  s.commentPopup = puzzle_->getCommentPopup();
  open_->parts.push_back(std::move(s));
}

void Document::touchAppendedShape() {
  touches_++;
  const unsigned int index = puzzle_->getNumberOfShapes();
  if (covered(SnapshotKind::AppendedShape, index)) {
    return;
  }
  // no shape saved: the document has it, after the command
  Snapshot s;
  s.kind = SnapshotKind::AppendedShape;
  s.index = index;
  open_->parts.push_back(std::move(s));
}

void Document::touchAppendedProblem() {
  touches_++;
  const unsigned int index = puzzle_->getNumberOfProblems();
  if (covered(SnapshotKind::AppendedProblem, index)) {
    return;
  }
  // no problem saved: the document has it, after the command
  Snapshot s;
  s.kind = SnapshotKind::AppendedProblem;
  s.index = index;
  open_->parts.push_back(std::move(s));
}

void Document::touchWhole() {
  touches_++;
  if (covered(SnapshotKind::Whole, 0)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Whole;
  s.whole = std::make_unique<Puzzle>(*puzzle_);
  s.ids = ids_;
  open_->parts.push_back(std::move(s));
}

// Puts the saved part in and keeps what was there, so that the same call
// undoes and redoes.
void Document::swapIn(Snapshot& s) {
  switch (s.kind) {
    case SnapshotKind::Shape:
      s.shape = puzzle_->replaceShape(s.index, std::move(s.shape));
      return;
    case SnapshotKind::Problem:
      if (s.settingsOnly) {
        // the solutions stay where they are, their ids and version too
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a problem's snapshot holds its problem
        puzzle_->getProblem(s.index).swapButSolutions(*s.problem);
        return;
      }
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a problem's snapshot holds its problem
      std::swap(puzzle_->getProblem(s.index), *s.problem);
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): as above
      puzzle_->getProblem(s.index).countOnFrom(*s.problem);
      return;
    case SnapshotKind::Color: {
      const Rgb now = puzzle_->getColor(s.index);
      puzzle_->changeColor(s.index, s.color.r, s.color.g, s.color.b);
      s.color = now;
      return;
    }
    case SnapshotKind::Notes: {
      std::string comment = puzzle_->getComment();
      const bool popup = puzzle_->getCommentPopup();
      puzzle_->setComment(s.comment);
      puzzle_->setCommentPopup(s.commentPopup);
      s.comment = std::move(comment);
      s.commentPopup = popup;
      return;
    }
    case SnapshotKind::Whole:
      std::swap(*puzzle_, *s.whole);
      std::swap(ids_, s.ids);
      // each problem that was there before counts on from itself as it was
      for (unsigned int i = 0; i < ids_.problems.size(); i++) {
        const auto was = std::find(s.ids.problems.begin(), s.ids.problems.end(), ids_.problems[i]);
        if (was != s.ids.problems.end()) {
          puzzle_->getProblem(i).countOnFrom(
              s.whole->getProblem(static_cast<unsigned int>(was - s.ids.problems.begin())));
        }
      }
      return;
    case SnapshotKind::AppendedShape:
      if (s.shape == nullptr) {
        // the shape is in the document: out with it (it is last, and no
        // problem uses it)
        BT_ASSERT(puzzle_->getNumberOfShapes() == s.index + 1);
        s.shape = puzzle_->getShape(s.index).clone();
        s.ids.shapes = {ids_.shapes.back()};
        puzzle_->removeShape(s.index);
        ids_.shapes.pop_back();
      } else {
        BT_ASSERT(puzzle_->getNumberOfShapes() == s.index);
        (void)puzzle_->addShape(std::move(s.shape));
        ids_.shapes.push_back(s.ids.shapes.front());
      }
      return;
    case SnapshotKind::AppendedProblem:
      if (!s.problem) {
        // the problem is in the document: out with it (it is last; undo
        // and redo end the sessions first)
        BT_ASSERT(puzzle_->getNumberOfProblems() == s.index + 1);
        s.problem = std::move(puzzle_->getProblem(s.index));
        s.ids.problems = {ids_.problems.back()};
        puzzle_->removeProblem(s.index);
        ids_.problems.pop_back();
      } else {
        BT_ASSERT(puzzle_->getNumberOfProblems() == s.index);
        (void)puzzle_->appendProblem(std::move(*s.problem));
        s.problem.reset();
        ids_.problems.push_back(s.ids.problems.front());
      }
      return;
  }
}

void Document::pushStep(Step step) {
  step.stamp = ++stamps_;
  step.count();
  undo_.push_back(std::move(step));
  redo_.clear();
  dropOldestSteps();
}

void Document::changed(std::uint32_t what) {
  revision_++;
  // added to what the command's helpers reported, never in their place
  // (cleanup item 15.3: assigned, an edit that removed a problem's
  // solutions through shapeChanged told of the shape alone)
  lastChange_.what |= what;
}

Result<void> Document::undo() {
  if (grouping_) {
    return fail(Errc::InvalidState, "a group of edits is open");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (undo_.empty()) {
    return fail(Errc::InvalidState, "there is nothing to undo");
  }
  endSessions();
  Step step = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = step.parts.rbegin(); it != step.parts.rend(); ++it) {
    swapIn(*it);
  }
  const std::uint32_t what = step.what;
  redo_.push_back(std::move(step));
  lastChange_ = ChangeSet{};
  changed(what);
  return {};
}

Result<void> Document::redo() {
  if (grouping_) {
    return fail(Errc::InvalidState, "a group of edits is open");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (redo_.empty()) {
    return fail(Errc::InvalidState, "there is nothing to redo");
  }
  endSessions();
  Step step = std::move(redo_.back());
  redo_.pop_back();
  for (Snapshot& s : step.parts) {
    swapIn(s);
  }
  const std::uint32_t what = step.what;
  // the step holds the state before it again, which may be the larger one
  step.count();
  undo_.push_back(std::move(step));
  dropOldestSteps();
  lastChange_ = ChangeSet{};
  changed(what);
  return {};
}

std::uint64_t Document::undoBytesHeld() const {
  std::uint64_t held = 0;
  for (const Step& s : undo_) {
    held += s.bytes;
  }
  return held;
}

bool Document::canUndo() const { return !undo_.empty(); }

bool Document::canRedo() const { return !redo_.empty(); }

HistoryInfo Document::history() const {
  HistoryInfo h;
  for (const Step& s : undo_) {
    h.undo.push_back(s.label);
  }
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
    h.redo.push_back(it->label);
  }
  return h;
}

Result<void> Document::beginGroup(std::string label) {
  if (grouping_) {
    return fail(Errc::InvalidState, "groups of edits do not nest");
  }
  open_ = std::make_unique<Step>();
  open_->label = std::move(label);
  grouping_ = true;
  return {};
}

Result<void> Document::endGroup() {
  if (!grouping_) {
    return fail(Errc::InvalidState, "no group of edits is open");
  }
  grouping_ = false;
  if (!open_->parts.empty()) {
    pushStep(std::move(*open_));
    // the step is on the undo stack only now: a revision of its own, so
    // that a client told of the group's edits reads canUndo and the step's
    // label again (the edits changed what they changed, this changes none)
    lastChange_ = ChangeSet{};
    changed(0);
  }
  open_.reset();
  return {};
}

// ---- the guards of decision 0011 ------------------------------------------------------

bool Document::solving() const {
  return !jobs_.empty() ||
         std::any_of(sessions_.begin(), sessions_.end(),
                     [](const auto& s) { return isActive(s.second->state()); });
}

Result<void> Document::checkNotSolving() const {
  if (solving()) {
    return fail(Errc::InvalidState, "a problem is being solved: pause it first");
  }
  return {};
}

Result<void> Document::checkProblemFree(unsigned int problem) const {
  const auto it = sessions_.find(ids_.problems[problem]);
  if (it != sessions_.end() && isActive(it->second->state())) {
    return fail(Errc::InvalidState, "the problem is being solved: pause it first");
  }
  const Id id = ids_.problems[problem];
  for (auto j = jobs_.lower_bound({id, BackgroundJob::Kind{}}); j != jobs_.end() && j->first.first == id;
       ++j) {
    switch (j->first.second) {
      case BackgroundJob::Kind::Analysis:
        return fail(Errc::InvalidState,
                    "the problem's solutions are being taken apart: stop it first");
      case BackgroundJob::Kind::PlacementFit:
        return fail(Errc::InvalidState,
                    "the tray's placements are being tried with the problem: stop it first");
    }
  }
  return {};
}

void Document::endSessions() {
  BT_ASSERT(!solving());
  sessions_.clear();
  fitEnds_.clear();
}

void Document::endSession(unsigned int problem) {
  // what the last fit of the tray's placements counted was this problem's
  // as it was
  fitEnds_.erase(ids_.problems[problem]);
  const auto it = sessions_.find(ids_.problems[problem]);
  if (it != sessions_.end()) {
    BT_ASSERT(!isActive(it->second->state()));
    sessions_.erase(it);
  }
}

// ---- the original's changeShape and changeProblem -------------------------------------

void Document::shapeChanged(unsigned int shape) {
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    Problem& pr = puzzle_->getProblem(p);
    if (!pr.usesShape(shape)) {
      continue;
    }
    if (pr.getSolveState() != ProblemState::Unsolved || pr.getNumberOfSavedSolutions() > 0 ||
        !pr.getPending().empty()) {
      touchProblem(p);
      pr.removeAllSolutions();
      lastChange_.what |= kChangeSolutions;
    }
    endSession(p);
  }
  // a sliding piece whose shape changed loses its starts and goals
  slidingShapeChanged(shape);
}

void Document::problemChanged(unsigned int problem) {
  Problem& pr = puzzle_->getProblem(problem);
  touchProblem(problem);
  pr.removeAllSolutions();
  endSession(problem);
}

// ---- solving ------------------------------------------------------------------------

// A solve only reads the shapes once they are as a run leaves them: the
// hotspots where a run puts them and the caches warm. Then the document
// can read them while it runs. A hotspot is saved with its shape, so a
// shape whose hotspot moves here goes into the open step (the run's or the
// take-apart's, openRunStep) as it was: undoing the step gives the file's
// bytes back.
void Document::prepareShapesForSolving() {
  struct Hotspot {
    int x, y, z;
    bool operator==(const Hotspot&) const = default;
  };
  const auto hotspotOf = [](const Voxel& v) { return Hotspot{v.getHx(), v.getHy(), v.getHz()}; };
  std::vector<Hotspot> was;
  was.reserve(puzzle_->getNumberOfShapes());
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    was.push_back(hotspotOf(puzzle_->getShape(i)));
  }
  app::prepareShapesForSolving(*puzzle_);
  if (open_ == nullptr) {
    return;
  }
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    const Voxel& v = puzzle_->getShape(i);
    if (hotspotOf(v) == was[i] || covered(SnapshotKind::Shape, i)) {
      continue;
    }
    Snapshot s;
    s.kind = SnapshotKind::Shape;
    s.index = i;
    s.shape = v.clone();
    s.shape->setHotspot(was[i].x, was[i].y, was[i].z);
    open_->parts.push_back(std::move(s));
    touches_++;
  }
}

// ---- the step of a run (decision 0011, amended 2026-11-24) ---------------------------

void Document::openRunStep(unsigned int problem, RunKind kind) {
  BT_ASSERT(open_ == nullptr && !grouping_);
  const Id id = ids_.problems[problem];
  open_ = std::make_unique<Step>();
  open_->label = std::string(kind == RunKind::Solve ? "Solve " : "Take apart ") + problemLabel(problem);
  open_->run = kind;
  open_->runOf = id;
  // The step on top is this problem's, of the same kind: the document is
  // as that run (or one after it) left it, and the state to go back to is
  // the one that step holds already. Nothing more is saved, so a solve
  // carried on run after run (the autosave) copies its solutions once.
  if (!undo_.empty() && undo_.back().run == kind && undo_.back().runOf == id) {
    return;
  }
  touchProblem(problem);
}

void Document::closeRunStep(bool started) {
  BT_ASSERT(open_ != nullptr);
  std::unique_ptr<Step> step = std::move(open_);
  if (!started) {
    // refused: nothing was started, and nothing is recorded
    return;
  }
  if (step->parts.empty()) {
    // the step on top stands for this run too; as after any new change,
    // what was undone before it can no longer be redone (a redone edit of
    // a shape would leave this run's solutions beside it)
    redo_.clear();
    return;
  }
  step->what = kChangeSolutions | kChangeProblems;
  for (const Snapshot& s : step->parts) {
    if (s.kind == SnapshotKind::Shape) {
      step->what |= kChangeShapes;
    }
  }
  pushStep(std::move(*step));
}

// Opens the step of a run in its constructor and closes it in its
// destructor, started or not, however the launch ends.
struct Document::RunStepGuard {
  RunStepGuard(Document& document, unsigned int problem, RunKind kind) : doc(document) {
    doc.openRunStep(problem, kind);
  }
  // pushing a step moves it and frees the steps past the depth: only a
  // failed allocation could throw
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~RunStepGuard() { doc.closeRunStep(started); }
  RunStepGuard(const RunStepGuard&) = delete;
  RunStepGuard& operator=(const RunStepGuard&) = delete;
  Document& doc;
  bool started = false;
};

SolveSession& Document::sessionFor(unsigned int problem, const SolveOptions& options) {
  const Id id = ids_.problems[problem];
  auto it = sessions_.find(id);
  if (it == sessions_.end()) {
    it = sessions_
             .emplace(id, std::make_unique<SolveSession>(*puzzle_, puzzle_->getProblem(problem),
                                                         options))
             .first;
  }
  return *it->second;
}

Result<void> Document::solveStart(Id problem, const SolveOptions& options) {
  return solveLaunch(problem, options, LaunchMode::Start);
}

Result<void> Document::solvePrepare(Id problem, const SolveOptions& options) {
  return solveLaunch(problem, options, LaunchMode::Prepare);
}

Result<void> Document::solveResume(Id problem, const SolveOptions& options) {
  return solveLaunch(problem, options, LaunchMode::Resume);
}

std::optional<SolveOptions> Document::pausedOptions(Id problem) const {
  const auto it = sessions_.find(problem);
  if (it == sessions_.end() || it->second->state() != SolveState::Paused) {
    return std::nullopt;
  }
  return it->second->options();
}

Result<void> Document::solveLaunch(Id problem, const SolveOptions& options, LaunchMode mode) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  if (grouping_) {
    return fail(Errc::InvalidState, "a group of edits is open");
  }
  // a sliding solve paused here is carried on as the search it is, or not
  // at all: with other search options it would begin again unasked
  if (mode == LaunchMode::Resume) {
    if (const auto it = sessions_.find(problem); it != sessions_.end()) {
      const std::string why = it->second->resumeRefusal(options);
      if (!why.empty()) {
        return fail(Errc::InvalidArgument, why);
      }
    }
  }
  // A stacking solve whose saved-search folder another problem's run is on
  // (two problems paused before uids, sharedSearchActive): not while that
  // run is active (Solve would throw the search away under it, and the end
  // of either removes it)
  if (mode != LaunchMode::Prepare) {
    if (Result<void> r = checkSharedSearchFree(*p, options.stacking.workDir); !r) {
      return r;
    }
  }
  // A sliding puzzle: what the original's Solve checked (the start/goal
  // shape valid, no stamp larger than its piece; solveRefusal: what the
  // session would refuse too, asked before anything changes). What its
  // solve thread set up as it began, the problem's maximum of holes, is the
  // session's to set once nothing can refuse the run
  // (sliding::prepareForSolving): it changes the problem being solved and
  // nothing else, so a solve of another problem may be running meanwhile
  // (cleanup item 15.4: the document set it up here and refused while any
  // run was active, from the time when it repainted the pieces).
  if (sliding::isSliding(*puzzle_)) {
    if (Result<void> r = solveRefusal(*puzzle_, puzzle_->getProblem(*p)); !r) {
      return r;
    }
  }
  // The run's undo step (decision 0011, amended: D1 of the cleanup plan):
  // the problem as it is now, before the session drops or adds anything,
  // on the undo stack once the run has started. Undo then takes a run's
  // results out before the edits made before it, and redo brings them
  // back.
  RunStepGuard step(*this, *p, RunKind::Solve);
  // the shapes are read by runs from now on; with a run of another
  // problem active they are prepared already
  if (!solving()) {
    prepareShapesForSolving();
  }
  SolveSession& s = sessionFor(*p, options);
  if (Result<void> r = s.setOptions(options); !r) {
    return r;
  }
  Result<void> r = mode == LaunchMode::Start     ? s.start()
                   : mode == LaunchMode::Prepare ? s.prepare()
                                                 : s.resume();
  if (!r) {
    return r;
  }
  step.started = true;
  solveDirty_ = true;
  runsStarted_.insert(problem);
  lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, {problem}, {}};
  changed(lastChange_.what);
  return {};
}

namespace {

template <class F>
Result<void> withSession(const std::map<Id, std::unique_ptr<SolveSession>>& sessions, Id id,
                         F&& f) {
  const auto it = sessions.find(id);
  if (it == sessions.end()) {
    return fail(Errc::InvalidState, "the problem is not being solved");
  }
  std::forward<F>(f)(*it->second);
  return {};
}

}  // namespace

Result<void> Document::solvePause(Id problem) {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  return withSession(sessions_, problem, [](SolveSession& s) { s.pause(); });
}

Result<void> Document::solvePauseAtSavePoint(Id problem) {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  return withSession(sessions_, problem, [](SolveSession& s) { s.pauseAtSavePoint(); });
}

Result<void> Document::solveAbort(Id problem, const std::string& stackingWorkDir) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (job<Analysis>(problem) != nullptr) {
    return fail(Errc::InvalidState, "the problem's solutions are being taken apart: stop it first");
  }
  // the hard stop of a solve under way (the original's Abort button was
  // grayed out once the solve thread had ended): a finished solve's
  // solutions are removed with deleteSolutions, which can be undone
  const auto it = sessions_.find(problem);
  // a saved-search folder that another problem's active run is on
  // (sharedSearchActive) is that run's to write: not thrown away under it
  if (it == sessions_.end() || !isActive(it->second->state())) {
    if (Result<void> r = checkSharedSearchFree(*p, stackingWorkDir); !r) {
      return r;
    }
  }
  if (it == sessions_.end() ||
      (!isActive(it->second->state()) && it->second->state() != SolveState::Paused)) {
    // a stacking search saved in its folder is a paused solve, on disk,
    // and so is a problem a file left solving (its assembler position):
    // the GUI's Abort threw either away with the problem's results
    // (cb_BtnAbort, abortable())
    Problem& pr = puzzle_->getProblem(*p);
    const bool savedSearch =
        stacking::isStacking(*puzzle_) && !panex::savedSearch(*puzzle_, pr, stackingWorkDir).empty();
    if (!savedSearch && pr.getSolveState() != ProblemState::Solving) {
      return fail(Errc::InvalidState,
                  "the problem has no solve under way: remove its solutions instead");
    }
    endSession(*p);
    pr.removeAllSolutions();
    if (savedSearch) {
      panex::discardSaved(*puzzle_, pr, stackingWorkDir);
    }
    solveDirty_ = true;
    lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, {problem}, {}};
    changed(kChangeSolutions | kChangeProblems);
    return {};
  }
  it->second->abort();
  solveDirty_ = true;
  lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, {problem}, {}};
  changed(kChangeSolutions | kChangeProblems);
  return {};
}

Result<void> Document::solveReset(Id problem, const std::string& stackingWorkDir) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return r;
  }
  // a solve under way (paused in memory, left solving by a file, or a
  // stacking search saved) is solveAbort's, which cannot be undone; so a
  // Reset that a run's pause overtook throws nothing away
  const auto it = sessions_.find(problem);
  Problem& pr = puzzle_->getProblem(*p);
  if ((it != sessions_.end() && it->second->state() == SolveState::Paused) ||
      pr.getSolveState() == ProblemState::Solving ||
      (stacking::isStacking(*puzzle_) &&
       !panex::savedSearch(*puzzle_, pr, stackingWorkDir).empty())) {
    return fail(Errc::InvalidState, "the problem has a solve under way: abort it instead");
  }
  if (pr.getSolveState() == ProblemState::Unsolved && pr.getNumberOfSavedSolutions() == 0 &&
      pr.getPending().empty()) {
    return {};
  }
  // the GUI's Reset (cb_BtnAbort when resettable()), a step here
  StepGuard g(*this, "Reset solve");
  touchProblem(*p);
  pr.removeAllSolutions();
  endSession(*p);
  lastChange_.problems = {problem};
  g.commit(kChangeSolutions | kChangeProblems);
  return {};
}

Result<void> Document::solveWait(Id problem) {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  const auto it = sessions_.find(problem);
  if (it != sessions_.end()) {
    it->second->wait();
  }
  return {};
}

SolveState Document::solveState(Id problem) const {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? SolveState::Idle : it->second->state();
}

Result<SolveProgress> Document::solveProgress(Id problem) const {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  const auto it = sessions_.find(problem);
  if (it == sessions_.end()) {
    return SolveProgress{};
  }
  return it->second->progress();
}

Result<SolveStats> Document::solveStats(Id problem) const {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  const auto it = sessions_.find(problem);
  if (it == sessions_.end()) {
    return SolveStats{};
  }
  return it->second->stats();
}

void Document::requestSolveSample(Id problem) {
  const auto it = sessions_.find(problem);
  if (it != sessions_.end()) {
    it->second->requestSample();
  }
}

std::optional<SearchSnapshot> Document::takeSolveSample(Id problem) {
  const auto it = sessions_.find(problem);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  return it->second->takeSample();
}

std::vector<LogLine> Document::takeSolveLog(Id problem) {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? std::vector<LogLine>{} : it->second->takeLog();
}

std::optional<SolveFailure> Document::solveFailure(Id problem) const {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? std::nullopt : it->second->failure();
}

std::string Document::solveNote(Id problem) const {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? std::string() : it->second->note();
}

std::optional<FolderFailure> Document::solveFolderFailure(Id problem) const {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? std::nullopt : it->second->folderFailure();
}

Result<std::optional<SavedSearchInfo>> Document::savedSearch(Id problem,
                                                             const std::string& workDir) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // a run writes its search's files (problemPaused: nothing while one runs),
  // this problem's or another's on the same folder (sharedSearchActive)
  if (!stacking::isStacking(*puzzle_) || isActive(solveState(problem)) ||
      sharedSearchActive(*p, workDir)) {
    return std::nullopt;
  }
  const Problem& pr = puzzle_->getProblem(*p);
  std::string description = panex::savedSearch(*puzzle_, pr, workDir);
  if (description.empty()) {
    return std::nullopt;
  }
  SavedSearchInfo info;
  info.description = std::move(description);
  info.ms = panex::savedMs(*puzzle_, pr, workDir);
  info.levelBytes = io::savedLevelBytes(panex::searchFolder(*puzzle_, pr, workDir));
  return info;
}

Result<bool> Document::slideFromStartAlone(Id problem,
                                           const sliding::SlidingOptions& options) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // a run writes the starts it keeps for the next one, and its search's files
  if (!sliding::isSliding(*puzzle_) || isActive(solveState(problem))) {
    return false;
  }
  return sliding::pendingFromStartAlone(*puzzle_, puzzle_->getProblem(*p), options);
}

Result<bool> Document::continueStartsOver(Id problem, const std::string& slideDir) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // a run writes the starts it keeps and its search's files
  if (!sliding::isSliding(*puzzle_) || isActive(solveState(problem))) {
    return false;
  }
  const auto it = sessions_.find(problem);
  if (it != sessions_.end() && it->second->slideSavedUnread()) {
    return false;
  }
  return sliding::pausedWithNothingSaved(*puzzle_, puzzle_->getProblem(*p), slideDir);
}

Result<std::vector<std::string>> Document::searchFolders(Id problem, const std::string& slideDir,
                                                         const std::string& panexDir) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  const Problem& pr = puzzle_->getProblem(*p);
  if (sliding::isSliding(*puzzle_)) {
    return sliding::savedFolders(*puzzle_, pr, slideDir);
  }
  std::vector<std::string> out;
  if (stacking::isStacking(*puzzle_)) {
    const std::string dir = panex::searchFolder(*puzzle_, pr, panexDir);
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
      out.push_back(dir);
    }
  }
  return out;
}

// ---- analysing and what background work did -----------------------------------------

Result<unsigned int> Document::analyseStart(Id problem, const AnalyseOptions& options) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkProblemFree(*p); !r) {
    return std::unexpected(r.error());
  }
  if (grouping_) {
    return fail(Errc::InvalidState, "a group of edits is open");
  }
  // a tilted tray's pieces lie flat in it: the player offers no take-apart either
  if (tilted()) {
    return fail(Errc::Unsupported, Disassembler::kNoTakeApartOnTray);
  }
  if (!Disassembler::canDisassemble(*puzzle_)) {
    return fail(Errc::Unsupported, "Sorry, this space grid doesn't have a disassembler (yet)!");
  }
  const Problem& pr = puzzle_->getProblem(*p);
  std::vector<std::pair<std::uint64_t, Assembly>> work;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    const Solution& sol = pr.getSavedSolution(i);
    const bool wanted = options.which == AnalyseWhich::All ||
                        (options.which == AnalyseWhich::Missing && sol.getDisassembly() == nullptr) ||
                        (options.which == AnalyseWhich::One && sol.getId() == options.solution);
    if (wanted) {
      work.emplace_back(sol.getId(), sol.getAssembly());
    }
  }
  if (options.which == AnalyseWhich::One && work.empty()) {
    return fail(Errc::NotFound, "there is no such solution");
  }
  const auto n = static_cast<unsigned int>(work.size());
  if (n == 0) {
    return 0U;
  }
  // The take-apart's undo step, as a run's (solveLaunch): the problem as it
  // is now, so that undo takes out the disassemblies poll() puts in (and
  // gives back the ones they replaced). Recorded now, not when the results
  // come: poll() runs outside any command.
  RunStepGuard step(*this, *p, RunKind::TakeApart);
  // the shapes as a run leaves them, so that solves may start meanwhile
  if (!solving()) {
    prepareShapesForSolving();
  }
  analysisEnds_.erase(problem);
  startJob<Analysis>(problem, *puzzle_, pr, std::move(work), options);
  step.started = true;
  // the step is news (canUndo and its label, nothing to redo): a revision
  // of its own, as a group's end has; the problem itself changes when
  // poll() puts the results in
  lastChange_ = ChangeSet{};
  changed(0);
  return n;
}

Result<void> Document::analyseStop(Id problem) {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  Analysis* a = job<Analysis>(problem);
  if (a == nullptr) {
    return fail(Errc::InvalidState, "the problem is not being analysed");
  }
  a->stop();
  return {};
}

Result<AnalyseProgress> Document::analyseProgress(Id problem) const {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  if (const Analysis* a = job<Analysis>(problem)) {
    return a->progress();
  }
  const auto e = analysisEnds_.find(problem);
  return e == analysisEnds_.end() ? AnalyseProgress{} : e->second;
}

bool Document::analysing(Id problem) const {
  return jobs_.contains({problem, Analysis::kKind});
}

bool Document::poll() {
  std::vector<Id> touched;
  // the jobs that ended: a fit changed nothing and is noted only, so that
  // whoever mirrors solving() learns it is over; an analysis's separations
  // go in (the problem cannot have changed: it was frozen)
  bool results = false;
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    if (it->second->active()) {
      ++it;
      continue;
    }
    const Id id = it->first.first;
    switch (it->first.second) {
      case BackgroundJob::Kind::PlacementFit:
        fitEnds_[id] = static_cast<const PlacementFit&>(*it->second).progress();
        break;
      case BackgroundJob::Kind::Analysis: {
        auto& a = static_cast<Analysis&>(*it->second);
        const Result<unsigned int> p = problemIndex(id);
        BT_ASSERT_MSG(p.has_value(), "a problem was removed while being analysed");
        Problem& pr = puzzle_->getProblem(*p);
        analysisEnds_[id] = a.progress();
        // where each solution is, looked up once (a search of the list for
        // each result was its square for "take apart all"); the first of
        // an id, as the search found it
        std::unordered_map<std::uint64_t, unsigned int> at;
        at.reserve(pr.getNumberOfSavedSolutions());
        for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
          at.emplace(pr.getSavedSolution(i).getId(), i);
        }
        for (auto& [sol, separation] : a.takeResults()) {
          if (const auto found = at.find(sol); found != at.end()) {
            pr.getSavedSolution(found->second).setDisassembly(std::move(separation));
          }
        }
        results = true;
        break;
      }
    }
    if (std::find(touched.begin(), touched.end(), id) == touched.end()) {
      touched.push_back(id);
    }
    it = jobs_.erase(it);
  }
  // the runs that ended
  for (auto it = runsStarted_.begin(); it != runsStarted_.end();) {
    const auto s = sessions_.find(*it);
    if (s != sessions_.end() && isActive(s->second->state())) {
      ++it;
      continue;
    }
    if (std::find(touched.begin(), touched.end(), *it) == touched.end()) {
      touched.push_back(*it);
    }
    results = true;
    it = runsStarted_.erase(it);
  }
  if (touched.empty()) {
    return false;
  }
  solveDirty_ = solveDirty_ || results;
  lastChange_ = ChangeSet{(results ? kChangeSolutions : 0U) | kChangeProblems, {},
                          std::move(touched), {}};
  revision_++;
  return true;
}

// ---- queries ------------------------------------------------------------------------

Result<FitStats> Document::fit(Id problem) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // the definition only, which no solve writes
  return problemFit(*puzzle_, *p);
}

std::vector<ShapeInfo> Document::shapeTable() const { return app::shapeTable(*puzzle_); }

Result<ShapeInfo> Document::shapeInfo(Id shape) const {
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  return app::shapeInfo(*puzzle_, *s);
}

Result<mesh::DrawMesh> Document::shapeMesh(Id shape, mesh::MeshStyle style,
                                           std::optional<unsigned int> transformation) const {
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  if (!transformation) {
    return mesh::shapeDrawMesh(puzzle_->getShape(*s), style);
  }
  return orientedMesh(puzzle_->getShape(*s), *transformation, style);
}

namespace {

// The place of a solution, found under the lock the caller reads with (a
// running solve moves solutions about).
std::optional<unsigned int> findSolution(const Problem& pr, Id solution) {
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    if (pr.getSavedSolution(i).getId() == solution) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

Result<mesh::Polyhedron> Document::stlMesh(Id shape, std::span<const double> values) const {
  const Result<unsigned int> s = shapeIndex(shape);
  if (!s) {
    return std::unexpected(s.error());
  }
  try {
    return mesh::stlMesh(puzzle_->getShape(*s), values);
  } catch (const InternalError&) {
    // the dialog's catch (...): parameters that make a degenerate mesh
    // (the sphere grid at 6 recursions, say); nothing outside the mesh
    // being built is touched
    return fail(Errc::InvalidArgument,
                "The generated mesh is faulty in some way, try to tweak the parameter");
  }
}

Result<std::vector<StlPiece>> Document::stlPieces(Id problem, Id solution) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  return readProblem(*p, [this, solution](const Problem& pr) -> Result<std::vector<StlPiece>> {
    const std::optional<unsigned int> s = findSolution(pr, solution);
    if (!s) {
      return fail(Errc::NotFound, "there is no such solution");
    }
    return app::stlPieces(*puzzle_, pr, pr.getSavedSolution(*s));
  });
}

Result<PlaybackSampler> Document::playback(Id problem, Id solution) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  return readProblem(*p, [this, &p, solution](const Problem& pr) -> Result<PlaybackSampler> {
    const std::optional<unsigned int> s = findSolution(pr, solution);
    if (!s) {
      return fail(Errc::NotFound, "there is no such solution");
    }
    return PlaybackSampler::make(*puzzle_, *p, *s);
  });
}

Result<AssemblyExplorer> Document::exploreAssembly(Id problem, SolverType solver,
                                                   const MatrixOptions& matrix) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  // Only the copy under the lock a solve of the problem writes with: the
  // matrix is built from the copy, by the caller, outside it (cleanup item
  // 18.1: built in here, a big problem's matrix stalled its running solve
  // and whoever called for as long as it took).
  return readProblem(*p, [this, &p, solver, &matrix](const Problem&) {
    return AssemblyExplorer::copy(*puzzle_, *p, solver, matrix);
  });
}

Result<MovementExplorer> Document::exploreMovements(Id problem, Id solution,
                                                    bool rotations) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  return readProblem(*p, [this, &p, solution, rotations](const Problem& pr) -> Result<MovementExplorer> {
    const std::optional<unsigned int> s = findSolution(pr, solution);
    if (!s) {
      return fail(Errc::NotFound, "there is no such solution");
    }
    return MovementExplorer::make(*puzzle_, *p, *s, rotations);
  });
}

}  // namespace bt::app
