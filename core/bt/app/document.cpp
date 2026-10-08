// SPDX-License-Identifier: GPL-3.0-or-later
// The document's core: ids, the undo history (src/gui/shapehistory.cpp,
// generalised: each step keeps the parts of the document it changed, as
// they were, and undo and redo swap them back and forth), the guards of
// decision 0011, files, solving and queries. The commands are in
// document_commands.cpp.
#include "bt/app/document.h"

#include "bt/app/document_internal.h"
#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/io/gzip_file.h"
#include "bt/io/paused_solve.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solver_type.h"

#include <algorithm>
#include <utility>

namespace bt::app {

// ---- construction and files ---------------------------------------------------------

Document::Document(GridKind grid, DocumentSettings settings)
    : Document(std::make_unique<Puzzle>(GridType(grid)), settings) {}

Document::Document(std::unique_ptr<Puzzle> puzzle, DocumentSettings settings)
    : puzzle_(std::move(puzzle)), settings_(settings) {
  assignIds();
}

Document::Document(Document&&) noexcept = default;

Document::~Document() {
  // the analyses and solves first: they hold the puzzle
  analyses_.clear();
  sessions_.clear();
}

Result<Document> Document::open(const std::filesystem::path& path, DocumentSettings settings) {
  Result<Puzzle> p = io::loadPuzzle(path);
  if (!p) {
    return std::unexpected(p.error());
  }
  return Document(std::make_unique<Puzzle>(std::move(*p)), settings);
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
  return Document(std::make_unique<Puzzle>(std::move(*p)), settings);
}

Result<std::pair<Document, Id>> Document::importPaused(const std::filesystem::path& path,
                                                     DocumentSettings settings) {
  Result<io::PausedSolve> ps = io::loadPausedSolve(path);
  if (!ps) {
    return std::unexpected(ps.error());
  }
  Document d(std::make_unique<Puzzle>(std::move(ps->puzzle)), settings);
  // a new, untitled puzzle: it is not saved anywhere yet
  d.solveDirty_ = true;
  const Id problem = d.ids_.problems[ps->problem];
  return std::pair<Document, Id>(std::move(d), problem);
}

Result<void> Document::exportPaused(Id problem, const std::filesystem::path& path) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (puzzle_->getProblem(*p).getSolveState() != ProblemState::Solving) {
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
    ids_.colours.push_back(newId());
  }
  for (unsigned int i = 0; i < puzzle_->getNumberOfProblems(); i++) {
    ids_.problems.push_back(newId());
  }
}

Result<void> Document::save(const std::filesystem::path& path) {
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (Result<void> r = io::savePuzzle(*puzzle_, path); !r) {
    return r;
  }
  cleanStamp_ = undo_.empty() ? 0 : undo_.back().stamp;
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

bool Document::dirty() const {
  const std::uint64_t current = undo_.empty() ? 0 : undo_.back().stamp;
  return solveDirty_ || current != cleanStamp_;
}

void Document::setSettings(const DocumentSettings& settings) {
  settings_ = settings;
  while (undo_.size() > settings_.undoDepth) {
    undo_.erase(undo_.begin());
  }
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

Result<unsigned int> Document::colourIndex(Id id) const {
  return indexIn(ids_.colours, id, "colour");
}

Result<unsigned int> Document::problemIndex(Id id) const {
  return indexIn(ids_.problems, id, "problem");
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

std::string Document::colourLabel(unsigned int index) { return "C" + std::to_string(index + 1); }

std::string Document::problemLabel(unsigned int index) { return "P" + std::to_string(index + 1); }

// ---- the history --------------------------------------------------------------------

Document::StepGuard::StepGuard(Document& doc, std::string label)
    : doc_(doc), grouped_(doc.grouping_) {
  doc_.lastChange_ = ChangeSet{};
  if (!grouped_) {
    BT_ASSERT(doc_.open_ == nullptr);
    doc_.open_ = std::make_unique<Step>();
    doc_.open_->label = std::move(label);
  }
  first_ = doc_.open_->parts.size();
}

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
  const bool any = s.parts.size() > first_;
  s.what |= what;
  if (!grouped_) {
    if (any) {
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
  if (covered(SnapshotKind::Problem, index)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Problem;
  s.index = index;
  s.problem = puzzle_->getProblem(index);
  open_->parts.push_back(std::move(s));
}

void Document::touchColour(unsigned int index) {
  if (covered(SnapshotKind::Colour, index)) {
    return;
  }
  Snapshot s;
  s.kind = SnapshotKind::Colour;
  s.index = index;
  s.colour = puzzle_->getColor(index);
  open_->parts.push_back(std::move(s));
}

void Document::touchNotes() {
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

void Document::touchWhole() {
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
      std::swap(puzzle_->getProblem(s.index), *s.problem);
      return;
    case SnapshotKind::Colour: {
      const Rgb now = puzzle_->getColor(s.index);
      puzzle_->changeColor(s.index, s.colour.r, s.colour.g, s.colour.b);
      s.colour = now;
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
  }
}

void Document::pushStep(Step step) {
  step.stamp = ++stamps_;
  undo_.push_back(std::move(step));
  redo_.clear();
  while (undo_.size() > settings_.undoDepth) {
    undo_.erase(undo_.begin());
  }
}

void Document::changed(std::uint32_t what) {
  revision_++;
  lastChange_.what = what;
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
  if (Result<void> r = checkSessionsReplaceable(); !r) {
    return r;
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
  if (Result<void> r = checkSessionsReplaceable(); !r) {
    return r;
  }
  endSessions();
  Step step = std::move(redo_.back());
  redo_.pop_back();
  for (Snapshot& s : step.parts) {
    swapIn(s);
  }
  const std::uint32_t what = step.what;
  undo_.push_back(std::move(step));
  lastChange_ = ChangeSet{};
  changed(what);
  return {};
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
  }
  open_.reset();
  return {};
}

// ---- the guards of decision 0011 ------------------------------------------------------

bool Document::solving() const {
  return !analyses_.empty() ||
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
  if (analyses_.contains(ids_.problems[problem])) {
    return fail(Errc::InvalidState, "the problem's solutions are being taken apart: stop it first");
  }
  return {};
}

// Whether ending the solve of a problem loses nothing it could not take up
// again from the problem: a paused BurrTools 2 search can be carried on
// in memory only (decision 0009), unless it is through.
Result<void> Document::checkSessionReplaceable(unsigned int problem) const {
  const auto it = sessions_.find(ids_.problems[problem]);
  if (it == sessions_.end() || it->second->state() != SolveState::Paused) {
    return {};
  }
  const Problem& pr = puzzle_->getProblem(problem);
  if (pr.getSolveState() != ProblemState::Solving ||
      pr.getAssemblerVersion() != AssemblerBt2::kVersion) {
    return {};
  }
  const std::string through = std::to_string(pr.getNumberOfPieces() + 1) + " ";
  if (pr.getAssemblerState().starts_with(through)) {
    return {};
  }
  return fail(Errc::InvalidState,
              "a paused BurrTools 2 solve would be lost: abort it or let it finish first");
}

Result<void> Document::checkSessionsReplaceable() const {
  for (unsigned int p = 0; p < puzzle_->getNumberOfProblems(); p++) {
    if (Result<void> r = checkSessionReplaceable(p); !r) {
      return r;
    }
  }
  return {};
}

void Document::endSessions() {
  BT_ASSERT(!solving());
  sessions_.clear();
}

void Document::endSession(unsigned int problem) {
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
// can read them while it runs.
void Document::prepareShapesForSolving() {
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    Voxel& v = puzzle_->getShape(i);
    if (!v.hasInitialHotspot()) {
      v.initHotspot();
    }
    v.warmCaches();
  }
}

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
  solveDirty_ = true;
  runsStarted_.insert(problem);
  lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, {problem}};
  changed(kChangeSolutions | kChangeProblems);
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

Result<void> Document::solveAbort(Id problem) {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  if (analyses_.contains(problem)) {
    return fail(Errc::InvalidState, "the problem's solutions are being taken apart: stop it first");
  }
  // the hard stop of a solve under way (the original's Abort button was
  // greyed out once the solve thread had ended): a finished solve's
  // solutions are removed with deleteSolutions, which can be undone
  const auto it = sessions_.find(problem);
  if (it == sessions_.end() ||
      (!isActive(it->second->state()) && it->second->state() != SolveState::Paused)) {
    return fail(Errc::InvalidState,
                "the problem has no solve under way: remove its solutions instead");
  }
  it->second->abort();
  solveDirty_ = true;
  lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, {problem}};
  changed(kChangeSolutions | kChangeProblems);
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

std::optional<SolveFailure> Document::solveFailure(Id problem) const {
  const auto it = sessions_.find(problem);
  return it == sessions_.end() ? std::nullopt : it->second->failure();
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
  if (problemKind(*puzzle_) != ProblemKind::Brick) {
    return fail(Errc::Unsupported, "sliding and stacking puzzles are not solved yet");
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
  // the shapes as a run leaves them, so that solves may start meanwhile
  if (!solving()) {
    prepareShapesForSolving();
  }
  analyses_.emplace(problem, std::make_unique<Analysis>(*puzzle_, pr, std::move(work), options));
  return n;
}

Result<void> Document::analyseStop(Id problem) {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  const auto it = analyses_.find(problem);
  if (it == analyses_.end()) {
    return fail(Errc::InvalidState, "the problem is not being analysed");
  }
  it->second->stop();
  return {};
}

Result<AnalyseProgress> Document::analyseProgress(Id problem) const {
  if (Result<unsigned int> p = problemIndex(problem); !p) {
    return std::unexpected(p.error());
  }
  const auto it = analyses_.find(problem);
  return it == analyses_.end() ? AnalyseProgress{} : it->second->progress();
}

bool Document::analysing(Id problem) const { return analyses_.contains(problem); }

bool Document::poll() {
  std::vector<Id> touched;
  // the analyses that ended: their separations go in (the problem cannot
  // have changed: it was frozen)
  for (auto it = analyses_.begin(); it != analyses_.end();) {
    if (it->second->active()) {
      ++it;
      continue;
    }
    const Result<unsigned int> p = problemIndex(it->first);
    BT_ASSERT_MSG(p.has_value(), "a problem was removed while being analysed");
    Problem& pr = puzzle_->getProblem(*p);
    for (auto& [id, separation] : it->second->takeResults()) {
      for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
        if (pr.getSavedSolution(i).getId() == id) {
          pr.getSavedSolution(i).setDisassembly(std::move(separation));
          break;
        }
      }
    }
    touched.push_back(it->first);
    it = analyses_.erase(it);
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
    it = runsStarted_.erase(it);
  }
  if (touched.empty()) {
    return false;
  }
  solveDirty_ = true;
  lastChange_ = ChangeSet{kChangeSolutions | kChangeProblems, {}, std::move(touched)};
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
                                                   const MatrixOptions& matrix,
                                                   bool reduce) const {
  const Result<unsigned int> p = problemIndex(problem);
  if (!p) {
    return std::unexpected(p.error());
  }
  return readProblem(*p, [this, &p, solver, &matrix, reduce](const Problem&) {
    return AssemblyExplorer::make(*puzzle_, *p, solver, matrix, reduce);
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
