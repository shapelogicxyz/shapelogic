// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The document: a puzzle being edited, with its undo history, stable ids
/// for its shapes, colours, problems and solutions, a revision number and
/// a dirty flag; every editing action of the front end as a command (one
/// undo step each, or grouped); the solves of its problems; and the
/// queries, meshes, playback and explorers the front end shows.
///
/// Ported from the original GUI: the shape history (src/gui/shapehistory.cpp,
/// generalised to every edit), the callbacks of src/gui/mainwindow.cpp
/// (shapes, colours, problems, pieces, groups, colour rules, solutions,
/// changeShape / changeProblem), mainwindow_file.cpp (assemblies to
/// shapes) and mainwindow_solve.cpp (deleting solutions), and for sliding
/// puzzles mainwindow.cpp's and mainwindow_rods.cpp's start/goal shapes,
/// stamps and the problems that follow them (decision 0014). The rules for
/// edits while solving are decision 0011.

#include "bt/app/analysis.h"
#include "bt/app/explorers.h"
#include "bt/app/playback.h"
#include "bt/app/queries.h"
#include "bt/app/shape_tools.h"
#include "bt/base/error.h"
#include "bt/base/solve_state.h"
#include "bt/grid/grid_type.h"
#include "bt/mesh/draw_mesh.h"
#include "bt/model/puzzle.h"
#include "bt/solve/solve_progress.h"
#include "bt/solve/solve_session.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bt::app {

/// A stable runtime id: names a shape, colour or problem while the
/// document is open, whatever its place in the list (labels like "S1"
/// follow the place). Never 0, never given twice in a document. A
/// solution's id is Solution::getId(), unique within its problem.
using Id = std::uint64_t;

/// What a change touched (document.changed).
enum ChangeFlag : std::uint32_t {
  kChangeShapes = 1,
  kChangeColours = 2,
  kChangeProblems = 4,
  kChangeSolutions = 8,
  kChangeNotes = 16,
};

/// The settings of a document that are not UI (configuration.cpp).
struct DocumentSettings {
  /// Undo steps kept (the web UI offers 25 to 500).
  unsigned int undoDepth = 25;
};

/// The labels of the undo and redo steps, for edit.history.
struct HistoryInfo {
  std::vector<std::string> undo;  ///< oldest first; the last is undone next
  std::vector<std::string> redo;  ///< the first is redone next
};

/// A box of cells, corners included, in cell coordinates.
struct CellBox {
  int x1 = 0;
  int y1 = 0;
  int z1 = 0;
  int x2 = 0;
  int y2 = 0;
  int z2 = 0;
};

/// The colour rules a problem can be set to at once.
enum class ColourPreset : std::uint8_t {
  EachOnItself,  ///< every colour may go on itself, nothing else
  AllowAll,      ///< every colour on every colour
  Clear,         ///< no rules
};

/// Which solutions solution.delete removes (relative to `solution`).
enum class SolutionsToDelete : std::uint8_t {
  One,
  Before,
  After,
  WithoutDisassembly,
  All,
};

/// The options of "Convert assemblies to pieces" (assmImportWindow_c).
struct AssemblyImport {
  /// The solution to turn into a shape; none: all of them.
  std::optional<Id> solution;
  bool dropDisconnected = false;
  bool dropMirror = false;     ///< shapes whose symmetry contains a mirror
  bool dropSymmetric = false;  ///< shapes with any symmetry
  bool dropNonMillable = false;
  bool dropNonNotchable = false;
  bool dropIdentical = false;  ///< a shape identical to one added before
  unsigned int minCells = 0;
  unsigned int maxCells = std::numeric_limits<unsigned int>::max();
  /// Where the shapes go as pieces: nowhere, into a new problem, or into
  /// problem `target`, with `min` to `max` of each.
  enum class AddTo : std::uint8_t { Nothing, NewProblem, Problem } addTo = AddTo::Nothing;
  std::optional<Id> target;
  unsigned int min = 1;
  unsigned int max = 1;
};

/// What the last successful command changed.
struct ChangeSet {
  std::uint32_t what = 0;  ///< ChangeFlag bits
  std::vector<Id> shapes;
  std::vector<Id> problems;
  /// What the original GUI said of the change on its status line (a
  /// sliding piece whose new shape lost its start and goal); mostly empty.
  std::string notice;
};

/// A puzzle being edited. The owner calls it from one thread. Solves run
/// on their own threads; while a run is active (decision 0011) the
/// document refuses (InvalidState) edits of shapes, of the shape, colour
/// and problem lists, of problems being solved, undo, redo and saving;
/// other problems, colour values and the notes can still be edited. A
/// problem being solved is read through readProblem().
class Document {
 public:
  /// A new empty puzzle on a grid (document.new); the grid never changes.
  explicit Document(GridKind grid, DocumentSettings settings = {});
  /// Loads a .xmpuzzle file (document.open).
  static Result<Document> open(const std::filesystem::path& path, DocumentSettings settings = {});
  /// The same from the file's bytes (document.openBytes).
  static Result<Document> openBytes(std::string_view bytes, DocumentSettings settings = {});

  Document(Document&&) noexcept;
  /// Not assignable: the solves of the document assigned over would
  /// outlive its puzzle.
  Document& operator=(Document&&) = delete;
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  /// Pauses the solves and waits for them.
  ~Document();

  /// Opens a paused solve exported as a .btsolve file (cb_ImportPaused): a
  /// new, untitled document (dirty: it has no file yet) and the paused
  /// problem, which solveResume() carries on.
  static Result<std::pair<Document, Id>> importPaused(const std::filesystem::path& path,
                                                      DocumentSettings settings = {});

  /// Saves (gzip, as the original); the document is clean afterwards.
  /// Refused while a solve runs (save at a save point instead).
  Result<void> save(const std::filesystem::path& path);
  /// The file's bytes (plain XML), without marking the document clean.
  Result<std::string> saveBytes() const;
  /// Marks the document as holding changes no file has (a rescue copy
  /// reopened after the engine died): dirty until it is saved.
  void markUnsaved() { solveDirty_ = true; }
  /// What opening the file changed and said (the original's load
  /// message): a sliding shape of more than one layer made flat, which
  /// leaves the document dirty. Empty mostly.
  [[nodiscard]] const std::string& openNotice() const { return openNotice_; }

  // ---- reading ----------------------------------------------------------------

  /// The puzzle. A problem being solved is written by its solve: read it
  /// through readProblem() while a run is active.
  [[nodiscard]] const Puzzle& puzzle() const { return *puzzle_; }
  /// Incremented by every change.
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  /// Changed since it was opened or saved (undoing back to there is clean;
  /// solving makes it dirty until saved).
  [[nodiscard]] bool dirty() const;
  /// What the last successful command changed.
  [[nodiscard]] const ChangeSet& lastChange() const { return lastChange_; }

  [[nodiscard]] const DocumentSettings& settings() const { return settings_; }
  /// Changes the settings; a smaller undo depth drops the oldest steps.
  void setSettings(const DocumentSettings& settings);

  // ---- ids --------------------------------------------------------------------

  /// The ids in list order.
  [[nodiscard]] const std::vector<Id>& shapeIds() const { return ids_.shapes; }
  [[nodiscard]] const std::vector<Id>& colourIds() const { return ids_.colours; }
  [[nodiscard]] const std::vector<Id>& problemIds() const { return ids_.problems; }
  /// The place of an id in its list (colours: 0 is colour number 1);
  /// NotFound for an unknown id.
  [[nodiscard]] Result<unsigned int> shapeIndex(Id id) const;
  [[nodiscard]] Result<unsigned int> colourIndex(Id id) const;
  [[nodiscard]] Result<unsigned int> problemIndex(Id id) const;
  /// The place of a solution in its problem's list.
  [[nodiscard]] Result<unsigned int> solutionIndex(Id problem, Id solution) const;
  /// The labels the front end shows: "S1", "C1", "P1" by place.
  [[nodiscard]] static std::string shapeLabel(unsigned int index);
  [[nodiscard]] static std::string colourLabel(unsigned int index);
  [[nodiscard]] static std::string problemLabel(unsigned int index);

  // ---- history ----------------------------------------------------------------

  Result<void> undo();
  Result<void> redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] HistoryInfo history() const;
  /// Groups the following commands into one undo step (edit.begin), for
  /// a drag over many cells; groups do not nest.
  Result<void> beginGroup(std::string label);
  /// Ends the group (edit.end); an empty group leaves no step.
  Result<void> endGroup();

  // ---- notes ------------------------------------------------------------------

  /// The puzzle's comment (document.setNotes).
  Result<void> setNotes(std::string text);

  // ---- shapes -----------------------------------------------------------------

  /// Adds an empty shape (shape.create): of the given size in cells, or of
  /// the grid's default size.
  Result<Id> createShape(std::optional<std::array<unsigned int, 3>> size = std::nullopt);
  /// Adds a copy of a shape at the end (shape.duplicate).
  Result<Id> duplicateShape(Id shape);
  /// Removes a shape; the problems drop it (shape.delete).
  Result<void> deleteShape(Id shape);
  /// Moves a shape to place `toIndex`; labels renumber (shape.move).
  Result<void> moveShape(Id shape, unsigned int toIndex);
  Result<void> renameShape(Id shape, std::string name);
  /// The weight (heavier shapes stay in place when taken apart).
  Result<void> setShapeWeight(Id shape, int weight);
  /// The shape's own colour, 0xRRGGBB, or none for the default one.
  Result<void> setShapeColour(Id shape, std::optional<std::uint32_t> rgb);
  /// Applies a cell tool to cells (shape.setCells); `colour` is a colour
  /// id, or 0 for none. Returns the number of cells changed (0 changes
  /// leave no undo step).
  Result<unsigned int> editCells(Id shape, CellTool tool, Id colour, std::uint8_t symmetry,
                                 const std::vector<CellEdit>& cells);
  /// The same for every cell of a box (shape.selectionFill / selectionClear).
  Result<unsigned int> editBox(Id shape, CellTool tool, Id colour, std::uint8_t symmetry,
                               const CellBox& box);
  /// Sets the size in the editor's units (sizeUnit(); shape.resize): with
  /// `toAll` the other shapes grow by as much as this one (never shrink).
  Result<void> resizeShape(Id shape, unsigned int x, unsigned int y, unsigned int z, bool toAll);
  /// A button of the tool tabs (shape.transform, prune, centre, minimize,
  /// scale, fillHoles, surface); with `toAll` and a task that allows it,
  /// on every shape. Returns what it did to this shape.
  Result<TaskOutcome> shapeTask(Id shape, ShapeTask task, bool toAll = false);

  // ---- colours ----------------------------------------------------------------

  /// Adds a constraint colour; every problem allows it on itself.
  Result<Id> addColour(Rgb rgb);
  Result<void> editColour(Id colour, Rgb rgb);
  /// Removes a colour: its cells become neutral, the colours above move
  /// down, the problems with shapes using it lose their solutions.
  Result<void> deleteColour(Id colour);

  // ---- problems ---------------------------------------------------------------

  /// Adds a problem; it allows each colour on itself.
  Result<Id> createProblem();
  /// Adds a copy of a problem's definition (no name, no solutions).
  Result<Id> duplicateProblem(Id problem);
  Result<void> deleteProblem(Id problem);
  Result<void> moveProblem(Id problem, unsigned int toIndex);
  Result<void> renameProblem(Id problem, std::string name);
  /// Makes a shape the result (it stops being a piece).
  Result<void> setResult(Id problem, Id shape);
  /// The count range of a shape as pieces; max 0 removes it.
  Result<void> setPieceCount(Id problem, Id shape, unsigned int min, unsigned int max);
  /// One more (one fewer) of a shape: minimum and maximum both.
  Result<void> addPiece(Id problem, Id shape);
  Result<void> removePiece(Id problem, Id shape);
  /// One range for every piece (bulk range).
  Result<void> setRangeAll(Id problem, unsigned int min, unsigned int max);
  /// One more of every shape but the result.
  Result<void> addOneOfEach(Id problem);
  Result<void> removeAllPieces(Id problem);
  /// Moves a shape's part one place earlier (-1) or later (+1).
  Result<void> movePart(Id problem, Id shape, int delta);
  /// Puts `count` pieces of a shape in group `group` (0 removes; the
  /// groups editor).
  Result<void> setGroupCount(Id problem, Id shape, unsigned int group, unsigned int count);
  /// Puts all pieces of a shape in one group, or none (0).
  Result<void> setGroup(Id problem, Id shape, unsigned int group);
  /// Removes a group from every shape.
  Result<void> deleteGroup(Id problem, unsigned int group);
  /// Allows or forbids piece colour on result colour (colour ids).
  Result<void> setColourRule(Id problem, Id pieceColour, Id resultColour, bool allowed);
  Result<void> setColourRules(Id problem, ColourPreset preset);
  /// In a sliding puzzle the result is the start/goal shape, a piece with
  /// a start or goal there keeps its count (shapeIsRequired: the buttons
  /// the original greyed out), bulk changes leave such pieces alone, and
  /// there are no groups: those commands are refused (InvalidArgument), as
  /// are new, copied and deleted problems (they follow the start/goal
  /// shapes) and a deleted colour (the cell colours are the start marks).
  ///
  /// Stores the Solver tab's settings with a problem (cb_SolverOptions:
  /// the original's names and values, saved in the file; the front end
  /// decides what they mean). Replaces them all; not while the problem is
  /// being solved.
  Result<void> setSolverOptions(Id problem, std::map<std::string, std::string> options);

  // ---- solutions --------------------------------------------------------------

  /// Removes saved solutions (solution.delete); `solution` names the one
  /// that One, Before and After are relative to.
  Result<void> deleteSolutions(Id problem, SolutionsToDelete which, Id solution = 0);
  /// Drops the disassembly of one solution, or of all.
  Result<void> deleteDisassemblies(Id problem, std::optional<Id> solution);
  /// Turns assemblies into new shapes (solution.toShapes); returns their ids.
  Result<std::vector<Id>> assembliesToShapes(Id problem, const AssemblyImport& options);

  // ---- sliding puzzles (decision 0014) -----------------------------------------
  //
  // A sliding puzzle keeps one problem per start/goal shape in step with
  // its stamps (syncSlidingProblems), whatever changes them: these
  // commands, and deleting, moving, duplicating, renaming and editing
  // shapes, as the original GUI did. Refused (Unsupported) on other grids.

  /// Adds a start/goal shape of `size` cells (sliding.addStartGoal, the
  /// GUI's New Start/Goal): one layer, all floor, numbered after the
  /// others, with its problem.
  Result<Id> addStartGoal(std::array<unsigned int, 2> size);
  /// The start/goal editor's click (sliding.place): the whole of a piece as
  /// its start (goal false) or its goal, its top-left cell at (x, y), y
  /// rising upwards; a piece that is exactly there already is taken off.
  /// Refused (InvalidArgument) with the reason when it does not fit.
  /// Returns whether the piece is there now.
  Result<bool> placeStamp(Id tray, Id piece, int x, int y, bool goal);
  /// Takes off the start or goal that covers (x, y) (the right click);
  /// false when there is none.
  Result<bool> clearStampAt(Id tray, int x, int y, bool goal);

  // ---- queries ----------------------------------------------------------------

  [[nodiscard]] Result<FitStats> fit(Id problem) const;
  [[nodiscard]] std::vector<ShapeInfo> shapeTable() const;
  /// The drawing mesh of a shape (shape.mesh), in its own orientation or
  /// turned by one of the grid's transformations (a Placement's number;
  /// InvalidArgument beyond the grid's table).
  [[nodiscard]] Result<mesh::DrawMesh> shapeMesh(
      Id shape, mesh::MeshStyle style, std::optional<unsigned int> transformation = {}) const;

  // ---- solving ----------------------------------------------------------------

  /// Solves a problem from the start (solve.start).
  Result<void> solveStart(Id problem, const SolveOptions& options);
  /// Prepares the search and pauses (solve.prepare).
  Result<void> solvePrepare(Id problem, const SolveOptions& options);
  /// Carries a solve on (solve.continue), with these options.
  Result<void> solveResume(Id problem, const SolveOptions& options);
  /// Asks the run to pause; does not wait.
  Result<void> solvePause(Id problem);
  /// Asks the run to pause at a save point (autosave).
  Result<void> solvePauseAtSavePoint(Id problem);
  /// Stops a solve under way (active or paused in memory) and throws it
  /// away: the results, the paused state and the saved position; waits
  /// for the run to end. Refused (InvalidState) when the problem has no
  /// such solve: a finished solve's solutions go through
  /// deleteSolutions(All), which can be undone, and a paused solve from a
  /// file is carried on (solveResume) or started over (solveStart).
  Result<void> solveAbort(Id problem);
  /// Waits for the run of a problem to end.
  Result<void> solveWait(Id problem);
  /// The state of a problem's solve (Idle when it has none).
  [[nodiscard]] SolveState solveState(Id problem) const;
  [[nodiscard]] Result<SolveProgress> solveProgress(Id problem) const;
  [[nodiscard]] Result<SolveStats> solveStats(Id problem) const;
  [[nodiscard]] std::optional<SolveFailure> solveFailure(Id problem) const;
  /// What a sliding solve's last run said once through (SolveSession::note);
  /// empty otherwise.
  [[nodiscard]] std::string solveNote(Id problem) const;
  /// Whether any run or analysis is active (the guards of decision 0011
  /// hold for both).
  [[nodiscard]] bool solving() const;

  /// Writes a paused solve of a problem as a .btsolve file
  /// (cb_ExportPaused): refused unless the problem holds a solve that can
  /// be carried on (Solving, no run active) and while anything runs.
  Result<void> exportPaused(Id problem, const std::filesystem::path& path) const;

  // ---- analysing ----------------------------------------------------------------

  /// Takes saved assemblies apart afterwards (solution.analyse) on a thread
  /// of its own. Refused while the problem is being solved or analysed,
  /// for grids without a take-apart, sliding and stacking puzzles, and
  /// when there is nothing to take apart. While it lasts the document is
  /// frozen as while solving (decision 0011); its results go in at poll()
  /// once it has ended. Returns the number of assemblies to take apart.
  Result<unsigned int> analyseStart(Id problem, const AnalyseOptions& options);
  /// Asks an analysis to stop; what it finished is kept. Does not wait.
  Result<void> analyseStop(Id problem);
  /// Where it stands: running; or, once poll() put its results in, how
  /// the last one ended (inactive, its counts kept until the next one
  /// starts); inactive with zeros when there was none.
  [[nodiscard]] Result<AnalyseProgress> analyseProgress(Id problem) const;
  /// An analysis of the problem exists: running, or ended and not yet put
  /// in by poll().
  [[nodiscard]] bool analysing(Id problem) const;

  /// Takes in what background work did: the results of the analyses that
  /// have ended go into their problems, and the runs that ended since the
  /// last call are noted. Either gives a new revision, with lastChange()
  /// saying what (solving changes no undo step). Returns whether there was
  /// any. The owner calls it now and then (the engine every 50 ms while
  /// solving() and after each request).
  bool poll();

  /// Calls f(problem) while the problem's solve does not write it.
  template <class F>
  decltype(auto) readProblem(unsigned int index, F&& f) const {
    const Id id = ids_.problems[index];
    const auto it = sessions_.find(id);
    if (it != sessions_.end()) {
      return it->second->readProblem(std::forward<F>(f));
    }
    return std::forward<F>(f)(std::as_const(puzzle_->getProblem(index)));
  }

  // ---- playback and explorers -------------------------------------------------

  /// The playback of a saved solution (playback.open): its disassembly
  /// when it has one, else the assembled pose alone (stepCount() 0).
  [[nodiscard]] Result<PlaybackSampler> playback(Id problem, Id solution) const;
  [[nodiscard]] Result<AssemblyExplorer> exploreAssembly(Id problem, SolverType solver,
                                                         const MatrixOptions& matrix,
                                                         bool reduce) const;
  [[nodiscard]] Result<MovementExplorer> exploreMovements(Id problem, Id solution,
                                                          bool rotations) const;

 private:
  struct Ids {
    std::vector<Id> shapes;
    std::vector<Id> colours;
    std::vector<Id> problems;
  };
  /// What a snapshot saved: a shape, a problem, a colour's value, the
  /// notes, or everything.
  enum class SnapshotKind : std::uint8_t { Shape, Problem, Colour, Notes, Whole, AppendedShape };
  enum class LaunchMode : std::uint8_t { Start, Prepare, Resume };
  struct Snapshot;
  struct Step;
  class StepGuard;

  explicit Document(std::unique_ptr<Puzzle> puzzle, DocumentSettings settings);
  void assignIds();
  Id newId() { return nextId_++; }

  // the guards of decision 0011
  [[nodiscard]] Result<void> checkNotSolving() const;
  [[nodiscard]] Result<void> checkProblemFree(unsigned int problem) const;
  [[nodiscard]] Result<void> checkSessionsReplaceable() const;
  [[nodiscard]] Result<void> checkSessionReplaceable(unsigned int problem) const;
  void endSessions();
  void endSession(unsigned int problem);
  SolveSession& sessionFor(unsigned int problem, const SolveOptions& options);
  void prepareShapesForSolving();

  Result<void> solveLaunch(Id problem, const SolveOptions& options, LaunchMode mode);

  // recording a step
  [[nodiscard]] bool covered(SnapshotKind kind, unsigned int index) const;
  void touchShape(unsigned int index);
  void touchProblem(unsigned int index);
  void touchColour(unsigned int index);
  void touchNotes();
  void touchWhole();
  /// Before a shape is added at the end: undo takes it out again.
  void touchAppendedShape();
  void swapIn(Snapshot& s);
  void pushStep(Step step);
  void changed(std::uint32_t what);

  // the original's changeShape / changeProblem
  void shapeChanged(unsigned int shape);
  void problemChanged(unsigned int problem);

  // sliding puzzles
  /// syncSlidingProblems with the ids kept in step: saves the whole
  /// puzzle for undo and ends the sessions first (problems come and go).
  void syncSliding();
  /// The sliding part of changeShape, after shape `shape` was edited.
  void slidingShapeChanged(unsigned int shape);
  /// After moveShape took piece `shape` past the 63rd place and its marks
  /// off the start/goal shapes `trays` (sliding::noteShapeSwap): the
  /// problems follow, with a notice.
  void slidingMarksLost(Id shape, const std::vector<Id>& trays);
  /// A sliding piece with a start or goal on the problem's start/goal
  /// shape: its count follows them.
  [[nodiscard]] bool slidingRequired(unsigned int problem, unsigned int shape) const;
  /// Flattens the shapes of a sliding puzzle just opened (the original's
  /// load), with the notice.
  void flattenOnOpen();

  Result<unsigned int> requireShape(Id id) const { return shapeIndex(id); }
  Result<unsigned int> requireProblem(Id id) const { return problemIndex(id); }

  std::unique_ptr<Puzzle> puzzle_;
  Ids ids_;
  std::string openNotice_;
  Id nextId_ = 1;
  DocumentSettings settings_;

  std::vector<Step> undo_;
  std::vector<Step> redo_;
  /// The step being recorded: a group, or the command under way.
  std::unique_ptr<Step> open_;
  bool grouping_ = false;
  /// Every touch of a part by a command (saved or already covered).
  std::uint64_t touches_ = 0;
  std::uint64_t stamps_ = 0;
  /// The step on top of the undo stack when the document was clean.
  std::uint64_t cleanStamp_ = 0;
  bool solveDirty_ = false;

  std::uint64_t revision_ = 0;
  ChangeSet lastChange_;

  /// One solve per problem, by problem id; destroyed before the puzzle.
  std::map<Id, std::unique_ptr<SolveSession>> sessions_;
  /// The problems whose run started since poll() last looked.
  std::set<Id> runsStarted_;
  /// One analysis per problem, until poll() puts its results in.
  std::map<Id, std::unique_ptr<Analysis>> analyses_;
  /// How each problem's last analysis ended, kept by poll() as it puts the
  /// results in: an owner that looks after poll() still learns its counts.
  std::map<Id, AnalyseProgress> analysisEnds_;
};

}  // namespace bt::app
