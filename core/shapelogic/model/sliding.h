// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sliding puzzles: the tray and its start/goal shapes, the starts and
/// goals stamped on them, the walls, and what a solve needs set up first.
///
/// A sliding puzzle (GridKind::Sliding) has pieces, which are plain shapes,
/// and start/goal shapes, whose names begin "sg:<n>:": a tray whose empty
/// cells are walls, whose variable cells are corridors a piece may cross
/// but not stop on. The starts and goals are the problem's whose result
/// the start/goal shape is (Problem::slideMarks, decision 0027): per cell
/// of the shape, a start mark, the start of a piece (shape index + 1), and
/// a goal mark, the goal of one. So several problems may share a board,
/// each with a layout of its own, and a board that is edited takes its
/// problems' starts and goals with its cells (BoardEdit). The cells'
/// colors are the user's restriction colors, as on every grid (the
/// original kept the starts in them and the goals on the shape; a file of
/// its is converted as it loads, convertLegacyStarts, and so is a file of
/// ours from when the marks were the shape's, adoptBoardMarks). The
/// functions below that take a tray and marks take one problem's marks
/// with its board. Trays and pieces may be
/// more than one layer deep; a start or goal
/// marks every cell its piece fills, in every layer. A piece the problem
/// has several copies of may have a start and a goal for each: its marks
/// are then several whole pieces (stampCopies), and any copy may end on
/// any of its goals; the copies without a start start anywhere, those
/// without a goal end anywhere. The original gave each start/goal shape
/// a problem of its own with that shape as its result
/// (syncSlidingProblems); the document lets the user make the problems and
/// choose a start/goal shape as each one's result, as on other grids
/// (syncSlidingParts). The assembler finds the start layout, each piece
/// held to its start by the marks (MatrixOptions::startLocks, with
/// freeStartParts), and the slide search (bt/solve/sliding_search.h) takes
/// it to the goal.
///
/// An older model, a tray and a goal map painted in piece colors
/// (ensureSetup, placeStart, placeGoal), is still read by the solver; the
/// tests build such puzzles.
///
/// Ported from src/lib/sliding.{h,cpp} (the tray and start/goal part; the
/// search is in bt/solve/sliding_search.h).

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sl {
class Assembly;
class Problem;
class Puzzle;
class SlideMarks;
class Voxel;
}  // namespace sl

namespace sl::sliding {

/// A cell of a tray, (x, y, z).
using TrayCell = std::array<int, 3>;

/// True when the puzzle is on the sliding grid.
[[nodiscard]] bool isSliding(const Puzzle& puzzle);

/// True when the shape is a start/goal shape (its name begins "sg:").
[[nodiscard]] bool isStartGoalShape(const Voxel* v);
/// The number n of a start/goal shape "sg:<n>:"; 0 for another shape.
[[nodiscard]] int startGoalNumber(const Voxel* v);
/// The name the user gave a start/goal shape (after "sg:<n>:"); the whole
/// name of another shape.
[[nodiscard]] std::string startGoalUserName(const Voxel* v);
/// Sets the user's part of a start/goal shape's name, keeping its number;
/// the whole name of another shape.
void setStartGoalUserName(Voxel* v, const std::string& user);
/// The name a start/goal shape shows while the user has given it none:
/// "Board n".
[[nodiscard]] std::string defaultStartGoalName(int n);
/// The name of a start/goal shape's problem while the user has given the
/// shape none: "Board n - Start / Goal".
[[nodiscard]] std::string defaultStartGoalProblemName(int n);
/// The name shown: the user's name for a start/goal shape, or
/// defaultStartGoalName while it has none; the name for another shape.
[[nodiscard]] std::string displayName(const Voxel* v);
/// The name of a start/goal shape's problem: the user's name for the
/// shape, or defaultStartGoalProblemName while it has none.
[[nodiscard]] std::string problemName(const Voxel* v);
/// The label of a shape: on a sliding puzzle "B1, B2, ..." for the
/// start/goal shapes (boards) and "S1, S2, ..." for the other shapes, each
/// by place among its own kind; "S" and its place on any other puzzle.
[[nodiscard]] std::string shapeLabel(const Puzzle& puzzle, unsigned int shape);
/// Numbers the start/goal shapes 1, 2, ... by place, so a deleted or moved
/// one leaves no gap; the user's names are kept.
void renumberStartGoalShapes(Puzzle& puzzle);
/// True for a shape whose name begins "__sggoal:" (hidden from the lists).
[[nodiscard]] bool isHiddenSlidingShape(const Voxel* v);
/// True for a name that would make a piece a start/goal shape or a hidden
/// one ("sg:" or "__sggoal:" at its start): a rename gives a piece no such name.
[[nodiscard]] bool isReservedName(std::string_view name);

/// Adds a start/goal shape of sx by sy by sz cells (one layer unless
/// asked), all filled, with the next free number. Returns its index.
unsigned int addStartGoalShape(Puzzle& puzzle, unsigned int sx, unsigned int sy,
                               unsigned int sz = 1);

/// One problem per start/goal shape, its result locked to that shape: the
/// start/goal shapes are numbered by place (renumberStartGoalShapes), the
/// problems of no start/goal shape go, a missing one is added, each is
/// named as its shape shows (displayName), no start/goal shape is a piece
/// of one, every piece stamped on a shape is a part of its problem (at
/// least one), and the start locks are refreshed. Marks drawn on a
/// start/goal shape itself become its problems' (adoptBoardMarks).
void syncSlidingProblems(Puzzle& puzzle);

/// The document's sync: the problems are the user's, made and given a
/// start/goal shape as their result as on other grids, so none is added,
/// dropped or renamed. The start/goal shapes are numbered by place, no
/// start/goal shape is a piece of any problem, and each problem whose
/// result is a start/goal shape has every piece stamped on it as a part
/// (a copy for each start and each goal) and its start locks refreshed.
void syncSlidingParts(Puzzle& puzzle);

/// True when the piece is stamped on the start or goal of the problem's
/// result (a start/goal shape): it must stay a part.
[[nodiscard]] bool shapeIsRequired(const Puzzle& puzzle, const Problem& problem,
                                   unsigned int shapeId);

/// Toggles the mark of shapeId on one cell of a start/goal shape, the
/// start or (goal) the goal. False when the cell is not floor or the shape
/// is beyond the 255 that can be marked.
bool toggleCellMark(const Voxel* tray, SlideMarks* marks, int x, int y, int z,
                    unsigned int shapeId, bool goal);

/// Marks the whole of a piece as its start (goal false) or its goal on a
/// start/goal shape, as the piece is drawn, from a click on cell (x, y) of
/// layer z (y rises upwards, as in the grid editor): the slice of the piece
/// on layer z has its top-left cell there, at the lowest height that puts
/// a layer of the piece on layer z and fits (stampCells). On a tray of one
/// layer that is the piece's top-left cell at (x, y). `copies` is how many
/// copies of the piece the problem has: with one, the stamp replaces the
/// piece's earlier start or goal; with more, it is one more copy's, beside
/// the others (refused once every copy has one). Empty on success, else
/// why it does not fit, and nothing changes.
std::string stampPiece(const Voxel* tray, SlideMarks* marks, const Voxel* piece,
                       unsigned int shapeId, int x, int y, int z, bool goal,
                       unsigned int copies = 1);

/// The tray cells stampPiece would mark: at the lowest height that fits, or
/// when none does, the lowest that puts a layer of the piece on layer z
/// (then stampFits says why). Empty when no height does that. Sorted lowest
/// layer first, top row first, left to right. Without a tray nothing is
/// checked; without marks the tray has none.
[[nodiscard]] std::vector<TrayCell> stampCells(const Voxel* tray, const SlideMarks* marks,
                                               const Voxel* piece, unsigned int shapeId, int x,
                                               int y, int z, bool goal, unsigned int copies = 1);

/// Why stampPiece would refuse, or empty when the piece fits; changes nothing.
[[nodiscard]] std::string stampFits(const Voxel* tray, const SlideMarks* marks,
                                    const Voxel* piece, unsigned int shapeId, int x, int y,
                                    int z, bool goal, unsigned int copies = 1);

/// True when one of shapeId's starts or goals is exactly where stampPiece
/// would put it.
[[nodiscard]] bool stampIsAt(const Voxel* tray, const SlideMarks* marks, const Voxel* piece,
                             unsigned int shapeId, int x, int y, int z, bool goal);

/// The starts (goal false) or goals of a piece on a start/goal shape, one
/// list of cells a copy (each sorted as stampCells sorts them, the copies
/// by their first cell): its marks split into whole pieces as drawn. Empty
/// when it has none; nullopt when the marks are not whole pieces. A split
/// is the only one there is: the first marked cell left is always the
/// first cell of a copy.
[[nodiscard]] std::optional<std::vector<std::vector<TrayCell>>> stampCopies(
    const Voxel& tray, const SlideMarks& marks, const Voxel& piece, unsigned int shapeId,
    bool goal);

/// Takes off the start or goal of shapeId's copy that covers (x, y, z):
/// that copy's cells (all of shapeId's marks when they are not whole
/// pieces). True when one covered it.
bool clearCopyAt(const Voxel* tray, SlideMarks* marks, const Voxel* piece, unsigned int shapeId,
                 int x, int y, int z, bool goal);

/// After piece shapeId was edited: removes its starts and goals from every
/// problem where they are no longer the piece's shape. Returns the
/// start/goal shapes of the problems that lost one, in order; an edit that
/// leaves the shape as it was, such as moving it in its grid, removes
/// nothing.
std::vector<unsigned int> dropChangedStamps(Puzzle& puzzle, unsigned int shapeId);

/// Removes shapeId's starts or goals; true when it had one.
bool clearStamp(SlideMarks* marks, unsigned int shapeId, bool goal);

/// Why copyStamps would refuse (a start on a variable cell cannot be a
/// goal; the problem's result is no start/goal shape), or empty when it
/// would not; changes nothing.
[[nodiscard]] std::string copyStampsRefusal(const Puzzle& puzzle, const Problem& problem,
                                            bool toGoal);

/// Copies every start of the problem onto its goals (toGoal), or every
/// goal onto its starts, replacing what that side had. Empty on success,
/// else why not (copyStampsRefusal), and nothing changes.
std::string copyStamps(const Puzzle& puzzle, Problem& problem, bool toGoal);

/// One piece of a problem: a copy of one of its parts.
struct LayoutPiece {
  unsigned int part = 0;     ///< the problem's part
  unsigned int shapeId = 0;  ///< the part's shape
};

/// The problem's pieces in the order a solve numbers them: its parts in
/// order, copy by copy (each part's maximum). A layout gives a place for
/// each: the lowest corner of the piece's cells on the board (the least x,
/// y and z of a cell it fills), which does not depend on where the piece is
/// drawn in its own grid or on its hotspot.
[[nodiscard]] std::vector<LayoutPiece> layoutPieces(const Problem& problem);

/// The board cells a piece as drawn fills with the lowest corner of its
/// cells at `at`; none for a piece without cells.
[[nodiscard]] std::vector<TrayCell> layoutCells(const Voxel& piece, const TrayCell& at);

/// The layout the problem's starts give: where each piece of layoutPieces
/// starts (the copies of a part in stampCopies' order). None, with *why
/// when it is given, for a problem whose starts place no whole layout: its
/// result is no start/goal shape, it has no pieces, a start is not a whole
/// piece, or a piece has starts for fewer (or more) than its copies.
[[nodiscard]] std::optional<std::vector<TrayCell>> startLayout(const Puzzle& puzzle,
                                                               const Problem& problem,
                                                               std::string* why = nullptr);

/// Why `at`, a place for each piece of layoutPieces, is no layout of the
/// problem's pieces on its board, or with `goal` none that can be its
/// goals; empty when it is one. Refused: a result that is no start/goal
/// shape, another number of places than pieces, a piece off the floor,
/// two pieces on one cell, a piece past the 255th shape (it can have no
/// marks), and for goals a piece on a variable cell.
[[nodiscard]] std::string layoutRefusal(const Puzzle& puzzle, const Problem& problem, bool goal,
                                        std::span<const TrayCell> at);

/// The problem's marks with its starts (goal false) or its goals replaced
/// by the layout `at`: every piece marked where the layout puts it. The
/// layout must be one layoutRefusal does not refuse.
[[nodiscard]] SlideMarks layoutMarks(const Puzzle& puzzle, const Problem& problem, bool goal,
                                     std::span<const TrayCell> at);

/// Makes the layout `at` the problem's starts (goal false) or its goals,
/// replacing what that side had. Empty on success, else why not
/// (layoutRefusal), and nothing changes.
std::string setLayout(const Puzzle& puzzle, Problem& problem, bool goal,
                      std::span<const TrayCell> at);

/// The piece whose start or goal covers (x, y, z), or ~0u.
[[nodiscard]] unsigned int stampAt(const Voxel* tray, const SlideMarks* marks, int x, int y, int z,
                                   bool goal);

/// Empty when the start/goal shape trayShape describes a puzzle that can be
/// solved, else what is wrong: starts or goals that are not whole pieces,
/// more of them than the problem has copies, or no piece with every copy
/// at a start and at a goal. A piece may have a goal without a start (it
/// starts anywhere) and starts or goals for only some of its copies. The
/// starts, goals and copies are `problem`'s (one whose result is
/// trayShape), or with none those of trayProblem.
[[nodiscard]] std::string startGoalError(const Puzzle& puzzle, unsigned int trayShape,
                                         const Problem* problem = nullptr);

/// The first problem whose result is the start/goal shape trayShape, or
/// null (several problems may share one).
[[nodiscard]] const Problem* trayProblem(const Puzzle& puzzle, unsigned int trayShape);

/// How many copies of a piece `problem` has (its maximum), or with none the
/// start/goal shape trayShape's first problem (trayProblem); at least 1,
/// the copy a stamp needs.
[[nodiscard]] unsigned int pieceCopies(const Puzzle& puzzle, unsigned int trayShape,
                                       unsigned int shapeId, const Problem* problem = nullptr);

/// The most copies of a piece stamped on the problem's start/goal shape, at
/// their starts or at their goals: the problem keeps at least this many.
[[nodiscard]] unsigned int stampedCopies(const Puzzle& puzzle, const Problem& problem,
                                         unsigned int shapeId);

/// Whether the problem's result is a start/goal shape, whose start marks
/// hold the pieces to their starts (MatrixOptions::startLocks). False for
/// the older model, a tray and a goal map painted in piece colors, which
/// a solve matches strictly (MatrixOptions::strictColors) as the original
/// did.
[[nodiscard]] bool usesStartMarks(const Puzzle& puzzle, const Problem& problem);

/// Per part of the problem, whether its pieces may also go on cells that
/// are no start (MatrixOptions::freeStarts): a part with starts for fewer
/// than all its copies, or none. A part with a start for every copy goes
/// only on its starts.
[[nodiscard]] std::vector<bool> freeStartParts(const Puzzle& puzzle, const Problem& problem);

/// False when an assembly (a start layout) leaves a start of a piece with
/// starts for only some of its copies unfilled: the assembler lets those
/// copies on unmarked cells, so it also finds layouts with a copy beside
/// its start instead of on it.
[[nodiscard]] bool startFillsStamps(const Puzzle& puzzle, const Problem& problem,
                                    const Assembly& assembly);

/// Renumbers the problems' marks before shape shapeId is removed (its own
/// go), or before shapes a and b swap places. A mark is a byte, so a
/// stamped piece swapped past the 255th place loses its start and goal:
/// noteShapeSwap returns the start/goal shapes of the problems that lost
/// one, in order, as they are numbered after the swap.
void noteShapeRemoved(Puzzle& puzzle, unsigned int shapeId);
std::vector<unsigned int> noteShapeSwap(Puzzle& puzzle, unsigned int a, unsigned int b);

/// The older model: makes sure a sliding puzzle has a filled tray (the
/// result), a goal map shape and a problem that refers to both; with no
/// shapes yet, a tray of width by height. Safe to call repeatedly.
void ensureSetup(Puzzle& puzzle, unsigned int width = 6, unsigned int height = 6);

/// The color (from 1) a piece is painted in; 0 when it is neutral.
[[nodiscard]] unsigned int pieceColor(const Puzzle& puzzle, unsigned int shapeId);

/// True when the piece has a colored start on the problem's tray.
[[nodiscard]] bool hasStart(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId);

/// True when the piece has a colored goal on the problem's goal map.
[[nodiscard]] bool hasGoal(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId);

/// The older model: puts a piece on the start tray as drawn, its hotspot at
/// (ax, ay, 0), with a color of its own on piece and tray. False when it
/// would leave the floor or cover another start.
bool placeStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Takes a piece's start off (the piece is neutral again).
void clearStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId);

/// The older model: puts the piece's goal at (ax, ay, 0) on the goal map;
/// the piece keeps its color (a free piece stays neutral).
bool placeGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Takes the piece's goal off the goal map.
void clearGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId);

/// Takes off the start that covers (x, y); true when there was one.
bool clearStartAt(Puzzle& puzzle, Problem& problem, int x, int y);

/// Places the piece's goal at (ax, ay), or takes it off when it is there.
bool toggleGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Sets the problem's maximum of holes: floor cells less the pieces'
/// cells (0 when the pieces need more).
void syncMaxHoles(const Puzzle& puzzle, Problem& problem);

/// Empty when every start and goal stamp set fits inside its piece's copies;
/// else a message naming each piece marked on more cells than they have.
[[nodiscard]] std::string stampOverflowMessage(const Puzzle& puzzle, const Problem& problem);

/// The number of floor (non-empty) cells of a tray.
[[nodiscard]] unsigned int floorCells(const Voxel& tray);

/// What a sliding solve sets up first (the solve thread did it as a run
/// started): the problem's maximum of holes (syncMaxHoles), and nothing
/// else: no shape, no color and no other problem changes, so a solve of
/// another problem may be running. True when it changed.
bool prepareForSolving(const Puzzle& puzzle, Problem& problem);

/// A file the original wrote (or one of ours from before the start marks
/// had a place of their own) keeps the starts as the start/goal shapes'
/// cell colors and paints each piece in its mark: the starts become start
/// marks, the colors go, and so does the palette the locks needed, unless
/// a problem has color rules of its own. Nothing happens to a puzzle
/// without start/goal shapes (the older model). The marks then go to the
/// problems (adoptBoardMarks).
void convertLegacyStarts(Puzzle& puzzle);

/// Marks a shape itself carries (Voxel::getStartPiece, getGoalPiece: what
/// a file from before the marks were the problems' holds, decision 0021)
/// become the marks of every problem whose result the start/goal shape is,
/// and the shapes carry none any more. Those of a start/goal shape that is
/// no problem's result are dropped.
void adoptBoardMarks(Puzzle& puzzle);

/// What an edit of a start/goal shape needs to take the starts and goals
/// of its problems along: before the edit each marked cell gets a tag,
/// carried by the cell itself through whatever the edit does (the shape's
/// own mark bytes, which every Voxel operation moves with its cells and
/// drops with a cell that is emptied); after it the tags say where each
/// cell went. A start or goal whose cells are all still there, on the
/// floor, and still the piece as drawn stays, where its cells now are
/// (a board resized, moved in its grid or trimmed keeps its layout); one
/// that lost a cell, or whose cells no longer make the piece, goes whole.
struct BoardEdit {
  /// One start or goal of a piece in a problem: a whole copy as drawn, or
  /// (whole false) all its marks when they are no whole copies.
  struct Stamp {
    unsigned int problem;            ///< the problem's index
    unsigned int shapeId;            ///< the piece
    bool goal;                       ///< a goal, else a start
    bool whole;                      ///< a whole copy of the piece
    std::vector<unsigned int> tags;  ///< its cells' tags (from 1)
  };
  unsigned int tray = 0;                         ///< the start/goal shape
  std::vector<Stamp> stamps;                     ///< every problem's starts and goals on it
  std::vector<unsigned int> cells;               ///< per tag, the cell's index before the edit
  std::vector<std::vector<unsigned int>> moved;  ///< per tag, the cells that carry it after
};

/// Starts following an edit of start/goal shape trayShape: tags the marked
/// cells of `v`, the shape that will be edited (the puzzle's, or a copy of
/// it that will replace it).
[[nodiscard]] BoardEdit beginBoardEdit(const Puzzle& puzzle, unsigned int trayShape, Voxel& v);

/// After the edit: reads where the tagged cells of `v` went and takes the
/// tags off it.
void readBoardTags(BoardEdit& edit, Voxel& v);

/// Once the edited shape is the puzzle's: gives each problem on it the
/// starts and goals that are still there, at their cells' new places.
/// Returns the pieces that lost a start or a goal, in order.
std::vector<unsigned int> applyBoardEdit(Puzzle& puzzle, const BoardEdit& edit);

/// Makes shape trayShape the problem's result. A problem that goes from
/// one start/goal shape to another keeps the starts and goals that fit the
/// new one at the same cells (whole, on its floor, a goal on no variable
/// cell); the others go. Returns the pieces that lost a start or a goal,
/// in order.
std::vector<unsigned int> changeBoard(Puzzle& puzzle, unsigned int problem,
                                      unsigned int trayShape);

/// The transformations each part of the problem may be placed in, for the
/// assembler (MatrixOptions::partTransforms): a piece keeps its orientation
/// as drawn on the board and through the solve, so its "up" stays up. A
/// piece with a start or a goal on the problem's tray, for any copy, takes
/// only that orientation; with anyOrientation, one with neither may also
/// take any quarter turn in its layers (about the axis the layers stack
/// along), and with mirrored too, the mirror images of those (a piece
/// turned over, on a tray one layer deep).
[[nodiscard]] std::vector<std::uint64_t> partTransforms(const Puzzle& puzzle,
                                                        const Problem& problem,
                                                        bool anyOrientation, bool mirrored);

}  // namespace sl::sliding
