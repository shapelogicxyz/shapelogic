// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Read-only questions about a puzzle that the editor shows: whether a
/// problem's pieces can fill its result (the fit summary and the status
/// line), the status window's facts about each shape, and what kind of
/// problem a problem is, and where a stacking puzzle's rod set stands on
/// its own.
/// Ported from mainWindow_c::StatProblemInfo and StatPieceInfo
/// (src/gui/mainwindow.cpp), the table of src/gui/statuswindow.cpp,
/// isNotchable / isMillable (src/lib/millable.cpp), the layout of
/// voxelFrame_c::showRodSet (src/gui/voxelframe.cpp), and the pieces of a
/// solution's STL export (src/gui/stlexportsolution.cpp).

#include "shapelogic/grid/types.h"
#include "shapelogic/model/stacking.h"

#include <optional>
#include <string>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
class Solution;
class Voxel;
}  // namespace bt

namespace bt::app {

/// Whether the pieces can fill the result (the web UI's fit summary).
enum class FitVerdict : unsigned char {
  NoResult,  ///< the problem has no result shape
  Fits,      ///< the piece cells can match the result's cell range
  TooLarge,  ///< even the fewest pieces have more cells than the result can take
  TooSmall,  ///< even the most pieces cannot fill the result's fixed cells
};

/// The fit of a problem's pieces and result.
struct FitStats {
  FitVerdict verdict = FitVerdict::NoResult;
  /// The result's cells: fixed (all must be filled) to fixed + variable.
  unsigned int resultMin = 0;
  unsigned int resultMax = 0;
  /// The number of pieces, at the minimum counts and at the maxima.
  unsigned int piecesMin = 0;
  unsigned int piecesMax = 0;
  /// The pieces' fixed cells at the minimum counts and at the maxima
  /// (variable piece cells do not count, as in the original).
  unsigned int cellsMin = 0;
  unsigned int cellsMax = 0;
  /// The result cells left empty: from the most pieces to the fewest
  /// (0 when the pieces fill more than the result can take).
  unsigned int holesMin = 0;
  unsigned int holesMax = 0;
  /// The problem's limit on holes, when set.
  std::optional<unsigned int> maxHoles;
  /// The original's status line ("Problem P1 result can contain ..."),
  /// empty without a result.
  std::string statusLine;
};

/// The fit of problem `problemIndex` of the puzzle.
[[nodiscard]] FitStats problemFit(const Puzzle& puzzle, unsigned int problemIndex);

/// A row of the status window: what is known about one shape.
struct ShapeInfo {
  unsigned int fixed = 0;
  unsigned int variable = 0;
  /// An earlier shape it is identical to: with mirrors allowed, as a shape
  /// (any orientation), and completely (with its colours, not mirrored).
  std::optional<unsigned int> identicalMirror;
  std::optional<unsigned int> identicalShape;
  std::optional<unsigned int> identicalComplete;
  /// Connected through faces, through edges, through corners.
  bool connectedFace = false;
  bool connectedEdge = false;
  bool connectedCorner = false;
  /// Holes seen in a layer (2D), and closed-in empty cells (3D).
  bool holes2D = false;
  bool holes3D = false;
  /// Cube grid only: can be made by cuts alone, and has no inside corners.
  std::optional<bool> notchable;
  std::optional<bool> millable;
  /// The symmetry group number, when the grid's tables know the shape's.
  std::optional<Symmetry> symmetry;
};

/// The status window's table: one row per shape, in shape order.
[[nodiscard]] std::vector<ShapeInfo> shapeTable(const Puzzle& puzzle);

/// The status line for a shape (StatPieceInfo): "Shape S1 has 6 voxels
/// (6 fixed, 0 variable)".
[[nodiscard]] std::string shapeStatusLine(const Puzzle& puzzle, unsigned int shapeIndex);

/// A brick piece can be made by cuts alone (isNotchable).
[[nodiscard]] bool isNotchable(const Voxel& v);
/// A brick piece has no inside corners (isMillable).
[[nodiscard]] bool isMillable(const Voxel& v);

/// What kind of problem: brick, sliding or stacking puzzles each have
/// solvers of their own.
enum class ProblemKind : unsigned char { Brick, Sliding, Stacking };

/// The kind of the puzzle's problems (all problems of a puzzle are of the
/// kind of its grid).
[[nodiscard]] ProblemKind problemKind(const Puzzle& puzzle);

/// A stacking puzzle's rod set drawn on its own, without a problem's discs
/// (the Entities tab's view of the rod set chosen, showRodSet): rods that
/// grow drawn 5 high, the spacing of the puzzle's largest disc, the pocket
/// left of rod 1 with every x at 0 or more. `disks` is empty.
[[nodiscard]] stacking::BoardLayout rodSetLayout(const Puzzle& puzzle, unsigned int rodSet);

/// A shape placed in a solution, for the solution's STL export.
struct StlPiece {
  unsigned int shape = 0;  ///< the shape's index
  unsigned int count = 0;  ///< how many copies the assembly places
  std::string fileName;    ///< its file's name, safe and distinct
};

/// The shapes placed in the solution's assembly (not the problem's
/// counts), in shape order, each with its file's name (placedShapeCounts
/// and cb_Export): the shape's name, or S<n> without one, with every byte
/// but ASCII letters, digits, '-' and '_' made '_', then "-S<n>" when an
/// earlier piece's file has that name, then ".stl".
[[nodiscard]] std::vector<StlPiece> stlPieces(const Puzzle& puzzle, const Problem& problem,
                                              const Solution& solution);

}  // namespace bt::app
