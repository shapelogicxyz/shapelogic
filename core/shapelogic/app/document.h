// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The document: a puzzle being edited, with its undo history, stable ids
/// for its shapes, colors, problems and solutions, a revision number and
/// a dirty flag; every editing action of the front end as a command (one
/// undo step each, or grouped); the solves of its problems; and the
/// queries, meshes, playback and explorers the front end shows.
///
/// Ported from the original GUI: the shape history (src/gui/shapehistory.cpp,
/// generalised to every edit), the callbacks of src/gui/mainwindow.cpp
/// (shapes, colors, problems, pieces, groups, color rules, solutions,
/// changeShape / changeProblem), mainwindow_file.cpp (assemblies to
/// shapes, the imports, the Puzzlecad export, grid conversion) and
/// mainwindow_solve.cpp (deleting solutions), and for sliding
/// puzzles mainwindow.cpp's and mainwindow_rods.cpp's start/goal shapes,
/// stamps and the problems that follow them (decision 0014), and for
/// stacking puzzles mainwindow_rods.cpp's rod sets, BlockList.cpp's disc
/// sizes and mainwindow.cpp's stacking branches of the Problems tab
/// (decision 0015). The rules for edits while solving are decision 0011.

#include "shapelogic/app/analysis.h"
#include "shapelogic/app/placement_fit.h"
#include "shapelogic/app/explorers.h"
#include "shapelogic/app/playback.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/error.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/io/imports.h"
#include "shapelogic/mesh/draw_mesh.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/stacking_types.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sl::app {

/// The largest space a command makes, in cells: a guard against sizes typed
/// by mistake, far above any real puzzle (grid.geometry draws no larger).
inline constexpr std::uint64_t kMaxShapeCells = std::uint64_t{1} << 24;

/// A stable runtime id: names a shape, color or problem while the
/// document is open, whatever its place in the list (labels like "S1"
/// follow the place). Never 0, never given twice in a document. A
/// solution's id is Solution::getId(), unique within its problem.
using Id = std::uint64_t;

/// What a change touched (document.changed).
enum ChangeFlag : std::uint32_t {
  kChangeShapes = 1,
  kChangeColors = 2,
  kChangeProblems = 4,
  kChangeSolutions = 8,
  kChangeNotes = 16,
  kChangeRodSets = 32,  ///< a stacking puzzle's rod sets
  kChangeTray = 64,     ///< a Tilted Square puzzle's tray
};

/// What the undo steps of a document may hold together, in bytes, unless
/// its settings say otherwise (DocumentSettings::undoBytes): the depth
/// counts steps, and a few steps that each hold every solution of a problem
/// that keeps them all would be gigabytes (cleanup item 17.10).
inline constexpr std::uint64_t kUndoByteBudget = std::uint64_t{512} << 20;

/// The settings of a document that are not UI (configuration.cpp).
struct DocumentSettings {
  /// Undo steps kept (the web UI offers 25 to 500).
  unsigned int undoDepth = 25;
  /// The memory the undo steps may hold together, about (kUndoByteBudget):
  /// past it the oldest go, as past the depth, down to the newest, which
  /// always stays. A step holds what it changed, and a step that changed a
  /// problem's solutions holds them all.
  std::uint64_t undoBytes = kUndoByteBudget;
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

/// The color rules a problem can be set to at once.
enum class ColorPreset : std::uint8_t {
  EachOnItself,  ///< every color may go on itself, nothing else
  AllowAll,      ///< every color on every color
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

/// What an export of a paused stacking solve takes along
/// (Document::exportPaused): its saved search in `stackingWorkDir`, with
/// the levels or not.
struct PausedExport {
  std::string stackingWorkDir;
  bool withLevels = true;
};

/// A stacking problem's saved search (Document::savedSearch): what the
/// original GUI said of it when Solve would throw it away ("a saved search
/// 200 moves deep, 3.4M stackings"), the solving time it holds, and the
/// bytes its levels take (an export asked about them beyond 1 GB).
struct SavedSearchInfo {
  std::string description;
  std::uint64_t ms = 0;
  std::uint64_t levelBytes = 0;
};

/// The other programs' files File ▸ Import reads (shapelogic/io/imports.h).
enum class ImportFormat : std::uint8_t {
  PuzzleSolver3D,  ///< a PuzzleSolver3D file (.puz)
  Puzzlecad,       ///< a puzzlecad OpenSCAD file (.scad)
};

/// A puzzle being edited. The owner calls it from one thread. Solves run
/// on their own threads; while a run is active (decision 0011) the
/// document refuses (InvalidState) edits of shapes, of the shape, color
/// and problem lists, of problems being solved, undo, redo and saving;
/// other problems, color values and the notes can still be edited, and a
/// problem can be added at the end (new or a copy). A problem being solved
/// is read through readProblem().
class Document {
 public:
  /// A new empty puzzle on a grid (document.new); the grid never changes.
  explicit Document(GridKind grid, DocumentSettings settings = {});
  /// A new Tilted Square puzzle (decision 0045): a cube-grid puzzle with
  /// `tray`, its board (shape 0) and one problem whose result is the board.
  /// The tray's shift is the best one, or with a custom placement the one
  /// given. InvalidArgument when the tray is not valid().
  static Result<Document> newTilted(tilted::Tray tray, DocumentSettings settings = {});
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

  /// Opens a paused solve exported as a .slsolve file (cb_ImportPaused): a
  /// new, untitled document (dirty: it has no file yet) and the paused
  /// problem, which solveResume() carries on. A stacking solve's saved
  /// search goes into its folder in `stackingWorkDir` (where the solves'
  /// SolveOptions::stacking.workDir look), replacing what was there; a
  /// stacking file whose search has no place there is refused.
  static Result<std::pair<Document, Id>> importPaused(const std::filesystem::path& path,
                                                      DocumentSettings settings = {},
                                                      const std::string& stackingWorkDir = {});

  /// A puzzle from another program's file (cb_Load_Ps3d, cb_Load_Scad;
  /// shapelogic/io/imports.h): a new, untitled document with unsaved changes, so
  /// Save asks where to write the ShapeLogic file and never writes over
  /// the source. ParseError as the format's reader says (the original's
  /// "Could not load puzzle, sorry!"), IoError when the file cannot be
  /// read; the file is read as it is, at most io::kMaxInflatedBytes.
  static Result<Document> importFile(ImportFormat format, const std::filesystem::path& path,
                                     DocumentSettings settings = {});
  /// The same from the file's bytes.
  static Result<Document> importBytes(ImportFormat format, std::string_view bytes,
                                      DocumentSettings settings = {});

  /// The puzzle on another grid (cb_Convert; shapelogic/model/grid_convert.h): a
  /// new document with unsaved changes, the shapes converted, the problems'
  /// definitions copied without their solutions. The original replaced its
  /// puzzle so, its undo history gone and its file name kept (the caller
  /// keeps the path). Unsupported for a grid this puzzle does not convert
  /// to; refused while solving (the original replaced the puzzle under its
  /// solver thread).
  [[nodiscard]] Result<Document> convertGrid(GridKind target) const;

  /// The Puzzlecad OpenSCAD file of a problem's pieces (cb_Export_Scad;
  /// io::writePuzzlecad): `problem`'s distinct shapes but its result (when
  /// it has none, every other shape), or every shape when no problem is
  /// given, with the copies and trays `options` asks for; `sourceName`,
  /// the puzzle's file (or the export's when it has none), is named in its
  /// header. InvalidArgument with the original's words for a puzzle not on
  /// the cube or sliding grid, without shapes, or without a cell to write,
  /// for a tray off the sliding grid, and for copies of more than
  /// io::kMaxPuzzlecadCopies pieces. Allowed while the problem is being
  /// solved (read through readProblem: a solve keeps its problem's parts).
  [[nodiscard]] Result<std::string> exportPuzzlecad(std::optional<Id> problem,
                                                    std::string_view sourceName,
                                                    const io::PuzzlecadOptions& options = {}) const;

  /// Saves (gzip, as the original); the document is clean afterwards.
  /// Refused while a solve runs (save at a save point instead).
  Result<void> save(const std::filesystem::path& path);
  /// The file's bytes (plain XML), without marking the document clean.
  Result<std::string> saveBytes() const;
  /// Marks the document as holding changes no file has (a rescue copy
  /// reopened after the engine died): dirty until it is saved.
  void markUnsaved() { solveDirty_ = true; }
  /// What opening the file changed and said (the original's load
  /// message), to tell the user. Empty mostly.
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
  [[nodiscard]] const std::vector<Id>& colorIds() const { return ids_.colors; }
  [[nodiscard]] const std::vector<Id>& problemIds() const { return ids_.problems; }
  /// A stacking puzzle's rod sets (empty on other grids).
  [[nodiscard]] const std::vector<Id>& rodSetIds() const { return ids_.rodSets; }
  /// The place of an id in its list (colors: 0 is color number 1);
  /// NotFound for an unknown id.
  [[nodiscard]] Result<unsigned int> shapeIndex(Id id) const;
  [[nodiscard]] Result<unsigned int> colorIndex(Id id) const;
  [[nodiscard]] Result<unsigned int> problemIndex(Id id) const;
  [[nodiscard]] Result<unsigned int> rodSetIndex(Id id) const;
  /// The place of a solution in its problem's list.
  [[nodiscard]] Result<unsigned int> solutionIndex(Id problem, Id solution) const;
  /// The labels the front end shows: "S1", "C1", "P1" by place.
  [[nodiscard]] static std::string shapeLabel(unsigned int index);
  /// The label of a shape of this puzzle: shapeLabel, but on a sliding
  /// puzzle "B1, B2, ..." for the boards (start/goal shapes) and "S1,
  /// S2, ..." for the pieces, each by place among its kind
  /// (sliding::shapeLabel).
  [[nodiscard]] std::string shapeLabelOf(unsigned int index) const;
  [[nodiscard]] static std::string colorLabel(unsigned int index);
  [[nodiscard]] static std::string problemLabel(unsigned int index);
  /// "R1": a rod set's label (the original's rod-set list).
  [[nodiscard]] static std::string rodSetLabel(unsigned int index);

  // ---- history ----------------------------------------------------------------

  Result<void> undo();
  Result<void> redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] HistoryInfo history() const;
  /// About the bytes the undo steps hold together: what
  /// DocumentSettings::undoBytes bounds.
  [[nodiscard]] std::uint64_t undoBytesHeld() const;
  /// Groups the following commands into one undo step (edit.begin), for
  /// a drag over many cells; groups do not nest.
  Result<void> beginGroup(std::string label);
  /// Ends the group (edit.end); an empty group leaves no step.
  Result<void> endGroup();

  // ---- notes ------------------------------------------------------------------

  /// The puzzle's comment (document.setNotes).
  Result<void> setNotes(const std::string& text);

  // ---- shapes -----------------------------------------------------------------

  /// Adds an empty shape (shape.create): of the given size in cells, or of
  /// the grid's default size. On a stacking puzzle a disc of size 1
  /// (cb_NewShape), and no size may be given.
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
  /// The shape's own color, 0xRRGGBB, or none for the default one.
  Result<void> setShapeColor(Id shape, std::optional<std::uint32_t> rgb);
  /// A picture for a shape (shape.setImage, decision 0038): new image bytes
  /// (PNG or JPEG) or the id of one the puzzle has, how it was fitted, and
  /// the part of it shown, in pixels.
  struct ImageChoice {
    std::optional<std::string> bytes;
    std::optional<unsigned int> id;
    ImageFit fit = ImageFit::Crop;
    std::array<int, 4> src{0, 0, 1, 1};
    /// The picture of the tray's border (decision 0046), not the shape's
    /// own: over the whole tray, for a board only.
    bool border = false;
  };
  /// Lays a picture over the shape's bounding box, upright, on a sliding
  /// puzzle (InvalidArgument elsewhere, for an empty shape, bytes that are
  /// not a picture, or a part outside it); with `choice.border`, over the
  /// tray of a board, shown on its border (InvalidArgument for a shape
  /// that is no board). Returns the picture's id.
  Result<unsigned int> setShapeImage(Id shape, const ImageChoice& choice);
  /// One picture of a stack for setShapeImages: its file (the bytes, or
  /// the id of one the puzzle has), its fit and the part shown, which may
  /// reach past the picture's edges but must overlap it.
  struct ImageLayer {
    std::optional<std::string> bytes;
    std::optional<unsigned int> id;
    ImageFit fit = ImageFit::Crop;
    std::array<int, 4> src{0, 0, 1, 1};
  };
  /// Lays a stack of pictures over the shape's bounding box (with
  /// `border`, over the board's tray), the bottom one first, in place of
  /// the ones it has (decision 0047): one undo step, none when nothing
  /// differs; an empty stack takes them off. InvalidArgument as for
  /// setShapeImage, and for more than kMaxShapeImages.
  Result<void> setShapeImages(Id shape, const std::vector<ImageLayer>& layers,
                              bool border = false);
  /// Takes the shape's picture off, or with `border` its border's
  /// (nothing when it has none).
  Result<void> clearShapeImage(Id shape, bool border = false);
  /// What setTrayBorder changes: each field that is given.
  struct BorderChange {
    std::optional<bool> enabled;
    std::optional<BorderCorners> corners;
    std::optional<BorderMaterial> material;
    /// 0 for thin, else the rim's width in voxels (up to kMaxBorderThickness).
    std::optional<unsigned int> thickness;
    /// The border's own color, 0xRRGGBB; an empty inner value takes it off.
    std::optional<std::optional<std::uint32_t>> color;
  };
  /// Changes the border of a sliding puzzle's board (decision 0046): one
  /// undo step, none when nothing differs. InvalidArgument for a shape
  /// that is no sliding puzzle's board, a color over 0xFFFFFF or a
  /// thickness over kMaxBorderThickness.
  Result<void> setTrayBorder(Id shape, const BorderChange& change);
  /// Applies a cell tool to cells (shape.setCells); `color` is a color
  /// id, or 0 for none. Returns the number of cells changed (0 changes
  /// leave no undo step).
  Result<unsigned int> editCells(Id shape, CellTool tool, Id color, std::uint8_t symmetry,
                                 const std::vector<CellEdit>& cells);
  /// The same for every cell of a box (shape.selectionFill / selectionClear).
  Result<unsigned int> editBox(Id shape, CellTool tool, Id color, std::uint8_t symmetry,
                               const CellBox& box);
  /// Sets the size in the editor's units (sizeUnit(); shape.resize): with
  /// `toAll` the other shapes grow by as much as this one (never shrink).
  Result<void> resizeShape(Id shape, unsigned int x, unsigned int y, unsigned int z, bool toAll);
  /// A button of the tool tabs (shape.transform, prune, center, minimize,
  /// scale, fillHoles, surface); with `toAll` and a task that allows it,
  /// on every shape. Returns what it did to this shape.
  Result<TaskOutcome> shapeTask(Id shape, ShapeTask task, bool toAll = false);
  /// The drawing mesh of a shape as `shapeTask` would leave it, on a copy:
  /// nothing changes (shape.taskPreview). The same refusals as `shapeTask`
  /// (a nudge out of the grid is InvalidArgument), but allowed while solving.
  [[nodiscard]] Result<mesh::DrawMesh> shapeTaskPreview(Id shape, ShapeTask task) const;

  // ---- colors ----------------------------------------------------------------

  /// Adds a constraint color; every problem allows it on itself.
  Result<Id> addColor(Rgb rgb);
  Result<void> editColor(Id color, Rgb rgb);
  /// Removes a color: its cells become neutral, the colors above move
  /// down, the problems with shapes using it lose their solutions.
  Result<void> deleteColor(Id color);

  // ---- problems ---------------------------------------------------------------

  /// Adds a problem; it allows each color on itself. Allowed while
  /// solving: the sessions go on.
  Result<Id> createProblem();
  /// Adds a copy of a problem's definition (no name, no solutions), of a
  /// problem being solved too. Allowed while solving: the sessions go on.
  Result<Id> duplicateProblem(Id problem);
  Result<void> deleteProblem(Id problem);
  Result<void> moveProblem(Id problem, unsigned int toIndex);
  Result<void> renameProblem(Id problem, const std::string& name);
  /// Makes a shape the result (it stops being a piece).
  Result<void> setResult(Id problem, Id shape);
  /// Leaves the problem without a result; its solutions go (not in the original).
  Result<void> clearResult(Id problem);
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
  /// Allows or forbids piece color on result color (color ids).
  Result<void> setColorRule(Id problem, Id pieceColor, Id resultColor, bool allowed);
  Result<void> setColorRules(Id problem, ColorPreset preset);
  /// In a sliding puzzle the result is the start/goal shape, a piece with
  /// starts or goals there keeps at least a copy for each
  /// (sliding::stampedCopies), bulk changes leave such pieces alone, and
  /// there are no groups: those commands are refused (InvalidArgument).
  /// Problems are made, copied and deleted as on other grids (the original
  /// grayed that out: its problems followed the start/goal shapes). Colors
  /// and their rules are the user's, as on every grid.
  ///
  /// Stores the Solver tab's settings with a problem (cb_SolverOptions:
  /// the original's names and values, saved in the file; the front end
  /// decides what they mean). Replaces them all; not while the problem is
  /// being solved.
  Result<void> setSolverOptions(Id problem, const std::map<std::string, std::string>& options);

  // ---- solutions --------------------------------------------------------------

  /// Removes saved solutions (solution.delete); `solution` names the one
  /// that One, Before and After are relative to.
  Result<void> deleteSolutions(Id problem, SolutionsToDelete which, Id solution = 0);
  /// Drops the disassembly of one solution, or of all.
  Result<void> deleteDisassemblies(Id problem, std::optional<Id> solution);
  /// Turns assemblies into new shapes (solution.toShapes); returns their ids.
  Result<std::vector<Id>> assembliesToShapes(Id problem, const AssemblyImport& options);

  // ---- sliding, tilted and stacking puzzles -------------------------------------
  //
  // Each kind's own commands are a class of their own, beside its .cpp
  // (cleanup item 21.6): SlidingEdits (sliding_edits.h: the start/goal
  // shapes and the starts and goals, decision 0014), TrayEdits
  // (tray_edits.h: a tilted tray and the trying of its placements, decision
  // 0045) and StackingEdits (stacking_edits.h: the rod sets, the disc sizes
  // and the stacks, decision 0015). They are the Document's friends; a
  // command added to one compiles its callers alone. What the commands here
  // do on each kind stays here:
  // - a sliding puzzle's problems on a board keep its stamped pieces as
  //   parts (syncSlidingParts) whatever changes them: deleting, moving,
  //   duplicating and editing shapes, as well as SlidingEdits;
  // - on a stacking puzzle, what the original GUI hid is refused
  //   (InvalidArgument): the cell editor and the tool tabs (editCells,
  //   editBox, resizeShape, shapeTask, a shape of a given size), and the
  //   Problems tab's brick commands, whose buttons did the stacking ones
  //   there (setResult, the piece counts and ranges, movePart, the
  //   groups); shape.create makes a disc of size 1 (cb_NewShape).

  // ---- tilted trays (decision 0045; their commands are TrayEdits') -------------
  //
  // A Tilted Square puzzle is a cube-grid puzzle with a tray
  // (Puzzle::tray()). Shape 0 is its board: the tray's whole cells, as
  // variable cells (the pieces must fit the board, not fill it), made from
  // the tray and never edited. Every shape has the board's size, one layer,
  // and a piece's cells lie on whole cells. So on such a puzzle: the board
  // is not deleted, copied, moved, edited or used as a piece; no shape is
  // resized; a problem's result is the board; and a shape task that would
  // leave a piece off the whole cells is refused (InvalidArgument each).

  /// True when the puzzle has a tilted tray.
  [[nodiscard]] bool tilted() const { return puzzle_->tray().has_value(); }
  /// True when every cell of shape `index` lies on a whole cell of the
  /// board (always so off a tilted puzzle, and for the board).
  [[nodiscard]] bool fitsTray(unsigned int index) const;
  /// The board the tray gives (tilted() only), worked out once a tray.
  [[nodiscard]] const tilted::Layout& trayLayout() const;
  /// Every board the tray can give that is worth trying
  /// (tilted::placements(), tilted() only), worked out once a tray: the best
  /// placement's first, `inUse` the board the tray lies at.
  [[nodiscard]] const tilted::Placements& trayPlacements() const;

  // ---- queries ----------------------------------------------------------------

  [[nodiscard]] Result<FitStats> fit(Id problem) const;
  [[nodiscard]] std::vector<ShapeInfo> shapeTable() const;
  /// One row of the table (shape.info), the other shapes' rows not worked out.
  [[nodiscard]] Result<ShapeInfo> shapeInfo(Id shape) const;
  /// The drawing mesh of a shape (shape.mesh), in its own orientation or
  /// turned by one of the grid's transformations (a Placement's number;
  /// InvalidArgument beyond the grid's table).
  [[nodiscard]] Result<mesh::DrawMesh> shapeMesh(
      Id shape, mesh::MeshStyle style, std::optional<unsigned int> transformation = {}) const;

  // ---- solving ----------------------------------------------------------------

  /// Solves a problem from the start (solve.start). A stacking problem is
  /// refused (InvalidState, here and in solveResume) while another
  /// problem's run is active on the same saved-search folder
  /// (sharedSearchActive). Solves of different problems run side by side,
  /// on every grid. A run that starts (this one, solvePrepare's,
  /// solveResume's) records an undo step, "Solve P1", that holds the
  /// problem as it was: undo takes the run's results out, redo brings
  /// them back (decision 0011, amended; openRunStep says when a run
  /// records none). A launch that is refused records nothing.
  Result<void> solveStart(Id problem, const SolveOptions& options);
  /// Prepares the search and pauses (solve.prepare).
  Result<void> solvePrepare(Id problem, const SolveOptions& options);
  /// Carries a solve on (solve.continue), with these options. Refused
  /// (InvalidArgument) when they name another search than the one a
  /// sliding solve paused in memory has saved
  /// (SolveSession::resumeRefusal): it would search from the first move.
  Result<void> solveResume(Id problem, const SolveOptions& options);
  /// The options of the last run of a problem's solve paused in memory:
  /// what solve.continue without options carries it on with. None when
  /// the problem has no solve paused in memory (never run here, running,
  /// finished, or paused only in the file).
  [[nodiscard]] std::optional<SolveOptions> pausedOptions(Id problem) const;
  /// Asks the run to pause; does not wait.
  Result<void> solvePause(Id problem);
  /// Asks the run to pause at a save point (autosave).
  Result<void> solvePauseAtSavePoint(Id problem);
  /// Stops a solve under way (active or paused in memory) and throws it
  /// away: the results, the paused state and the saved position; waits
  /// for the run to end. Refused (InvalidState) when the problem has no
  /// such solve: a finished solve's solutions go through
  /// deleteSolutions(All), which can be undone. A paused solve with no run
  /// in memory is a solve under way too (the GUI's Abort was offered for
  /// it): a problem a file left solving, and a stacking problem whose
  /// search is saved in `stackingWorkDir`; its results, its saved position
  /// and its saved search go (cb_BtnAbort); refused (InvalidState) while
  /// another problem's run is active on the same folder
  /// (sharedSearchActive).
  Result<void> solveAbort(Id problem, const std::string& stackingWorkDir = {});
  /// Throws away the results of a problem whose solve is over (finished,
  /// stopped at a limit, failed, or left by a change: the GUI's Reset):
  /// its solutions, counts and state, as one undo step; nothing when it
  /// has none. Refused (InvalidState) while a solve is under way, paused
  /// included (solveAbort's), or the solutions are being taken apart.
  Result<void> solveReset(Id problem, const std::string& stackingWorkDir = {});
  /// Waits for the run of a problem to end.
  Result<void> solveWait(Id problem);
  /// The state of a problem's solve (Idle when it has none).
  [[nodiscard]] SolveState solveState(Id problem) const;
  [[nodiscard]] Result<SolveProgress> solveProgress(Id problem) const;
  [[nodiscard]] Result<SolveStats> solveStats(Id problem) const;
  [[nodiscard]] std::optional<SolveFailure> solveFailure(Id problem) const;
  /// Asks a problem's run for a picture of where its search is, and takes
  /// the newest that has come (SolveSession::requestSample, takeSample):
  /// a live preview. Nothing when the problem has no solve.
  void requestSolveSample(Id problem);
  [[nodiscard]] std::optional<SearchSnapshot> takeSolveSample(Id problem);
  /// The lines the problem's solve has for the application's log since the
  /// last call (SolveSession::takeLog); none without a solve.
  [[nodiscard]] std::vector<LogLine> takeSolveLog(Id problem);
  /// What a sliding or stacking solve's last run said (SolveSession::note:
  /// once through, or a stacking search that paused and saved itself);
  /// empty otherwise.
  [[nodiscard]] std::string solveNote(Id problem) const;
  /// Why a sliding or stacking solve's last run paused before its search
  /// began: the search's folder could not be written
  /// (SolveSession::folderFailure); empty otherwise.
  [[nodiscard]] std::optional<FolderFailure> solveFolderFailure(Id problem) const;
  /// Whether any run, analysis or fit of the tray's placements is active
  /// (the guards of decision 0011 hold for all three).
  [[nodiscard]] bool solving() const;

  /// Writes a paused solve of a problem as a .slsolve file
  /// (cb_ExportPaused): refused unless the problem holds a solve that can
  /// be carried on (Solving, no run active; a stacking problem: a saved
  /// search in the work folder) and while anything runs.
  Result<void> exportPaused(Id problem, const std::filesystem::path& path,
                            const PausedExport& options = {}) const;

  // the saved searches of sliding and stacking problems

  /// The saved search of a stacking problem in `workDir`, which Continue
  /// carries on and Solve would throw away; none while the problem's run
  /// is active (the GUI asked only when nothing ran) or another problem's
  /// run is on the same folder (sharedSearchActive), and none on other
  /// grids.
  [[nodiscard]] Result<std::optional<SavedSearchInfo>> savedSearch(
      Id problem, const std::string& workDir) const;
  /// The folders that hold this problem's saved searches (solve.searchFolders):
  /// on the sliding grid one in slideDir for each start whose search was
  /// saved, on the stacking grid its search's folder in panexDir when it is
  /// there; none on the other grids. What the app's cache manager names
  /// after the puzzle.
  [[nodiscard]] Result<std::vector<std::string>> searchFolders(Id problem,
                                                               const std::string& slideDir,
                                                               const std::string& panexDir) const;

  /// Whether Continue would carry a sliding problem's paused search on from
  /// the start alone where Solve would search from both ends
  /// (sliding::pendingFromStartAlone, sliding.resumeAdvice): a search saved
  /// by a version before the two ends. `options` are the solve's. False
  /// while the problem's run is active, and on the other grids.
  [[nodiscard]] Result<bool> slideFromStartAlone(Id problem,
                                                 const sliding::SlidingOptions& options) const;

  /// Whether Continue would search a sliding problem all over again, as
  /// Solve would (sliding::pausedWithNothingSaved): paused with its starts
  /// waiting, none searched to its end, and no search of them saved in
  /// slideDir (the cache's saved search deleted, say). False while the
  /// problem's run is active, after a run that found its start's saved
  /// search unreadable (Continue tries it again), and on the other grids.
  [[nodiscard]] Result<bool> continueStartsOver(Id problem, const std::string& slideDir) const;

  // ---- analysing ----------------------------------------------------------------

  /// Takes saved assemblies apart afterwards (solution.analyse) on a thread
  /// of its own. Refused while the problem is being solved or analysed,
  /// for grids without a take-apart (sliding and stacking puzzles), and
  /// when there is nothing to take apart. While it lasts the document is
  /// frozen as while solving (decision 0011); its results go in at poll()
  /// once it has ended. Returns the number of assemblies to take apart.
  /// With any, it records an undo step as a run does, "Take apart P1"
  /// (undo takes out the disassemblies it found), and the step is a
  /// revision of its own that names no change.
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
  /// have ended go into their problems, and the runs and the fits that
  /// ended since the last call are noted. Either gives a new revision, with
  /// lastChange() saying what (the undo step of a run or a take-apart was
  /// recorded as it started). Returns
  /// whether there was any. The owner calls it now and then (the engine
  /// every 50 ms while solving() and after each request).
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

  // ---- the STL export ----------------------------------------------------------

  /// A shape's STL mesh for the values of its grid's parameters
  /// (mesh::stlMesh; the export dialog's preview and file). A mesh that
  /// fails one of the meshers' assertions is refused with the words the
  /// original's dialog used (InvalidArgument), as the dialog caught them.
  [[nodiscard]] Result<mesh::Polyhedron> stlMesh(Id shape, std::span<const double> values) const;
  /// The pieces of a solution's STL export (stlPieces), read while its
  /// solve does not move the solutions.
  [[nodiscard]] Result<std::vector<StlPiece>> stlPieces(Id problem, Id solution) const;

  // ---- playback and explorers -------------------------------------------------

  /// The playback of a saved solution (playback.open): its disassembly
  /// when it has one, else the assembled pose alone (stepCount() 0).
  [[nodiscard]] Result<PlaybackSampler> playback(Id problem, Id solution) const;
  /// The assembly explorer of a problem, copied but not built
  /// (AssemblyExplorer::copy, under the lock a solve of the problem writes
  /// with): the caller builds it, outside any lock (AssemblyExplorer::build,
  /// or an AssemblyExplorerBuild on a thread of its own).
  [[nodiscard]] Result<AssemblyExplorer> exploreAssembly(Id problem, SolverType solver,
                                                         const MatrixOptions& matrix) const;
  [[nodiscard]] Result<MovementExplorer> exploreMovements(Id problem, Id solution,
                                                          bool rotations) const;

 private:
  // each kind's commands (sliding_edits.h, tray_edits.h, stacking_edits.h)
  friend class SlidingEdits;
  friend class TrayEdits;
  friend class StackingEdits;

  struct Ids {
    std::vector<Id> shapes;
    std::vector<Id> colors;
    std::vector<Id> problems;
    std::vector<Id> rodSets;
  };
  /// What a snapshot saved: a shape, a problem, a color's value, the
  /// notes, or everything.
  enum class SnapshotKind : std::uint8_t {
    Shape,
    Problem,
    Color,
    Notes,
    Whole,
    AppendedShape,
    AppendedProblem
  };
  enum class LaunchMode : std::uint8_t { Start, Prepare, Resume };
  /// What a step stands for beside a command: a run of a solve, or a
  /// take-apart of solutions (openRunStep).
  enum class RunKind : std::uint8_t { None, Solve, TakeApart };
  struct Snapshot;
  struct Step;
  class StepGuard;
  struct RunStepGuard;

  explicit Document(std::unique_ptr<Puzzle> puzzle, DocumentSettings settings);
  void assignIds();
  Id newId() { return nextId_++; }

  // the guards of decision 0011
  [[nodiscard]] Result<void> checkNotSolving() const;
  [[nodiscard]] Result<void> checkProblemFree(unsigned int problem) const;
  /// A shape's place, for a command that edits shapes: checkNotSolving()'s
  /// refusal first, then NotFound for an unknown id.
  [[nodiscard]] Result<unsigned int> editableShape(Id shape) const;
  /// A problem's place, for a command that edits it: NotFound for an
  /// unknown id, then checkProblemFree()'s refusal.
  [[nodiscard]] Result<unsigned int> freeProblem(Id problem) const;
  /// A command that sets one field of a shape and nothing else (its
  /// weight, its own color), the one way such a command goes (cleanup item
  /// 19.2): editableShape(); `value(shape)` gives what the field is set
  /// to, or the command's refusal; nothing happens when `get(shape)` is
  /// that already; else one step named `label` that saves the shape, with
  /// `set(shape, value)` made and the shape named in the report.
  template <class Value, class Get, class Set>
  Result<void> editShapeField(Id shape, const char* label, Value value, Get get, Set set);
  /// The same for a field of a problem that is no part of what a solve
  /// reads or writes (its name, its stored solver options): freeProblem();
  /// the step keeps the problem without its solutions
  /// (touchProblemSettings), and the report names the problem.
  template <class Value, class Get, class Set>
  Result<void> editProblemField(Id problem, const char* label, Value value, Get get, Set set);
  void endSessions();
  void endSession(unsigned int problem);
  SolveSession& sessionFor(unsigned int problem, const SolveOptions& options);
  void prepareShapesForSolving();

  Result<void> solveLaunch(Id problem, const SolveOptions& options, LaunchMode mode);

  // recording a step
  [[nodiscard]] bool covered(SnapshotKind kind, unsigned int index) const;
  void touchShape(unsigned int index);
  void touchProblem(unsigned int index);
  /// touchProblem for a command that changes neither the problem's saved
  /// solutions nor its assemblies to take apart: the step keeps the problem
  /// without them, so it costs nothing where they are many (its name, its
  /// solver options, its groups, its rods and stacks). A later touchProblem
  /// of the same step (in a group of edits) adds them.
  void touchProblemSettings(unsigned int index);
  void touchColor(unsigned int index);
  void touchNotes();
  void touchWhole();
  /// Before a shape is added at the end: undo takes it out again.
  void touchAppendedShape();
  /// Before a problem is added at the end: undo takes it out again. Saves
  /// nothing, so it may be called while a run writes another problem.
  void touchAppendedProblem();
  void swapIn(Snapshot& s);
  /// Opens the undo step of a run or a take-apart of problem `problem`
  /// (decision 0011, amended 2026-11-24), "Solve P1" or "Take apart P1":
  /// it saves the problem as it is now, unless the step on top of the undo
  /// stack is that problem's of the same kind already (the state to go
  /// back to is the same). prepareShapesForSolving() adds the shapes whose
  /// hotspot it moves. Closed by closeRunStep.
  void openRunStep(unsigned int problem, RunKind kind);
  /// Ends the step openRunStep opened. Started: it goes on the undo stack
  /// when it saved anything, and nothing can be redone any more. Not
  /// started (the launch was refused): it is dropped.
  void closeRunStep(bool started);
  void pushStep(Step step);
  /// Drops the oldest undo steps past the depth and past the byte budget
  /// (never the newest), moving baseStamp_ up.
  void dropOldestSteps();
  /// The stamp of the state the document is in: the step on top of the
  /// undo stack, or baseStamp_ with none.
  [[nodiscard]] std::uint64_t currentStamp() const;
  /// A new revision whose change is `what` beside what lastChange_ holds
  /// already: a command's ChangeSet starts empty (StepGuard, or assigned
  /// by the command) and its helpers add to it before this does.
  void changed(std::uint32_t what);

  // the original's changeShape / changeProblem
  void shapeChanged(unsigned int shape);
  void problemChanged(unsigned int problem);

  // sliding puzzles
  /// syncSlidingParts: saves the whole puzzle for undo and ends the
  /// sessions first (pieces' locks and parts change).
  void syncSliding();
  /// Moves the boards above the pieces, the marks and ids following; true
  /// when a piece lost its marks past the 255th place (a mark is a byte).
  bool boardsAbovePieces();
  /// The sliding part of changeShape, after shape `shape` was edited.
  void slidingShapeChanged(unsigned int shape);
  /// Before an edit of shape `shape`, made on `v` (the shape itself or the
  /// copy that will replace it): when it is a sliding puzzle's start/goal
  /// shape, what takes its problems' starts and goals along
  /// (sliding::beginBoardEdit); empty for any other shape. Read the tags
  /// (sliding::readBoardTags) once `v` is edited, before it is compared.
  [[nodiscard]] std::vector<sliding::BoardEdit> beginBoardEdit(unsigned int shape, Voxel& v) const;
  /// After the edited shape is the puzzle's: the problems on it get the
  /// starts and goals that are still whole, where their cells now are, and
  /// follow the board (syncSliding), with a notice of the pieces that lost
  /// one. Nothing for an empty `edit`.
  void finishBoardEdit(const std::vector<sliding::BoardEdit>& edit);
  /// After moveShape took piece `shape` past the 255th place and its marks
  /// off the start/goal shapes `trays` (sliding::noteShapeSwap): the
  /// problems follow, with a notice.
  void slidingMarksLost(Id shape, const std::vector<Id>& trays);
  /// A sliding piece with a start or goal on the problem's start/goal
  /// shape: it stays in the problem.
  [[nodiscard]] bool slidingRequired(unsigned int problem, unsigned int shape) const;

  // stacking puzzles
  /// What the original GUI hid on a stacking puzzle (InvalidArgument with
  /// `why`); nothing on other grids.
  [[nodiscard]] Result<void> refuseOnStacking(const char* why) const;

  // ---- tilted trays (document_tilted.cpp) ------------------------------------------
  /// InvalidArgument with `why` when the puzzle is tilted and `shape` is
  /// its board.
  [[nodiscard]] Result<void> refuseTiltedBoard(unsigned int shape, const char* why) const;
  /// A tilted puzzle's piece after shape task `task` made `after` of it:
  /// back on the board's grid on whole cells (a nudge where it took the
  /// piece, the other tools as near as can be to where it lay). An error
  /// when it cannot be.
  [[nodiscard]] Result<std::unique_ptr<Voxel>> onTrayAfterTask(unsigned int shape,
                                                               const Voxel& after,
                                                               ShapeTask task) const;
  /// After a file is read: makes the board and the shapes agree with the
  /// tray; a puzzle whose shapes cannot is opened without its tray.
  void adoptTray();
  /// cb_NewShape's stacking branch: a disc of size 1.
  Result<Id> addDisc();
  /// True while a run of another problem is active on the folder of
  /// `problem`'s saved search in `workDir` (that run writes the folder, and
  /// its end removes it). Since decision 0044 a problem's uid names its
  /// folder, so copies have their own. Two problems share one only when
  /// both were paused by a version before uids, have the same rules and
  /// stacks, and differ elsewhere (their stored Solver settings, which of
  /// two rod sets with the same rules): io/puzzle_file.cpp's giveUids
  /// leaves the first of each definition paused that way without a uid, so
  /// that it finds the search it saved. Every copy shared one so in the
  /// original, which never ran two solves at once.
  [[nodiscard]] bool sharedSearchActive(unsigned int problem, const std::string& workDir) const;
  /// sharedSearchActive's refusal (InvalidState), or nothing.
  [[nodiscard]] Result<void> checkSharedSearchFree(unsigned int problem,
                                                   const std::string& workDir) const;

  std::unique_ptr<Puzzle> puzzle_;
  Ids ids_;
  std::string openNotice_;
  /// trayLayout()'s answer and the tray it is for.
  mutable std::unique_ptr<std::pair<tilted::Tray, tilted::Layout>> trayLayout_;
  /// trayPlacements()'s answer and the tray it is for.
  mutable std::unique_ptr<std::pair<tilted::Tray, tilted::Placements>> trayPlacements_;
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
  /// The state with no step left to undo: 0, the file as opened, until the
  /// depth drops the oldest step, then the stamp of the last step dropped
  /// (cleanup item 4.4: an empty stack counted as the file as opened, so
  /// more edits than the depth, all undone, looked saved).
  std::uint64_t baseStamp_ = 0;
  /// The state when the document was clean (currentStamp then).
  std::uint64_t cleanStamp_ = 0;
  bool solveDirty_ = false;

  std::uint64_t revision_ = 0;
  ChangeSet lastChange_;

  /// One solve per problem, by problem id; destroyed before the puzzle.
  std::map<Id, std::unique_ptr<SolveSession>> sessions_;
  /// The problems whose run started since poll() last looked.
  std::set<Id> runsStarted_;
  /// The background jobs, one of each kind per problem: an analysis until
  /// poll() puts its results in, a fit of the tray's placements (on a
  /// puzzle of its own) until poll() sees it ended. solving(),
  /// checkProblemFree(), poll() and the destructor walk them all, so a new
  /// kind is a new BackgroundJob::Kind (cleanup item 11.12).
  std::map<std::pair<Id, BackgroundJob::Kind>, std::unique_ptr<BackgroundJob>> jobs_;
  /// Starts a job of class `Job` for a problem, filed under the class's
  /// kind (Job::kKind): the one place a job goes into jobs_.
  template <class Job, class... Args>
  void startJob(Id problem, Args&&... args);
  /// The problem's job of class `Job` (found by Job::kKind); null for none.
  template <class Job>
  [[nodiscard]] Job* job(Id problem) const;
  /// How each problem's last analysis ended, kept by poll() as it puts the
  /// results in: an owner that looks after poll() still learns its counts.
  std::map<Id, AnalyseProgress> analysisEnds_;
  /// How each problem's last fit ended, kept by poll(): dropped when what
  /// it counted is no longer what the document holds.
  std::map<Id, PlacementFitProgress> fitEnds_;
};

}  // namespace sl::app
