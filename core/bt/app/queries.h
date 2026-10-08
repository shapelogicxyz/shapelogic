// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Read-only questions about a puzzle that the editor shows: whether a
/// problem's pieces can fill its result (the fit summary and the status
/// line), the status window's facts about each shape, and what kind of
/// problem a problem is.
/// Ported from mainWindow_c::StatProblemInfo and StatPieceInfo
/// (src/gui/mainwindow.cpp), the table of src/gui/statuswindow.cpp, and
/// isNotchable / isMillable (src/lib/millable.cpp).

#include "bt/grid/types.h"

#include <optional>
#include <string>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
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

/// What kind of problem: the solvers handle bricks; sliding and stacking
/// puzzles load and save but are not solved yet.
enum class ProblemKind : unsigned char { Brick, Sliding, Stacking };

/// The kind of the puzzle's problems (all problems of a puzzle are of the
/// kind of its grid).
[[nodiscard]] ProblemKind problemKind(const Puzzle& puzzle);

}  // namespace bt::app
