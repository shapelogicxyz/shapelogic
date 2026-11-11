// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Other programs' puzzle files: File ▸ Import ▸ PuzzleSolver3D and
/// Puzzlecad (OpenSCAD), and File ▸ Export ▸ Puzzlecad (OpenSCAD). Each
/// import makes a cube-grid puzzle with one problem, as the original's
/// loaders did; the export writes puzzlecad's `burr_plate` of a problem's
/// pieces.
///
/// Ported from src/lib/ps3dloader.{h,cpp} (ps3d_file.cpp) and
/// src/lib/scadloader.{h,cpp} (scad_file.cpp).

#include "shapelogic/base/error.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bt::io {

/// The most cells an import's shapes may hold together. The original had
/// none: a PuzzleSolver3D size line of a few bytes made a shape of up to
/// 500x500x500 cells, so a short crafted file asked for gigabytes. This
/// takes one shape of the largest size the format allows.
inline constexpr std::uint64_t kMaxImportCells = std::uint64_t{1} << 27;

/// A puzzle from a PuzzleSolver3D file's text (loadPuzzlerSolver3D): each
/// `PIECE XxYxZ` or `RESULT XxYxZ` line starts a shape, the Y lines after
/// it hold its Z layers side by side, X cells and a separator each, a
/// space an empty cell. The pieces are the one problem's ("Problem"), one
/// of each; pieces identical under rotation are counted on the first, the
/// others named "Duplicate" and left out. ParseError for a size line that
/// does not read as three numbers from 1 to 500, a comma among the cells,
/// or shapes past `maxCells` together. An empty text is an empty puzzle.
[[nodiscard]] Result<Puzzle> readPuzzleSolver3D(std::string_view text,
                                                std::uint64_t maxCells = kMaxImportCells);

/// A puzzle from a puzzlecad OpenSCAD file's text (loadOpenScadPuzzle):
/// the pieces of every `burr_plate` and `burr_piece` call not disabled by
/// OpenSCAD's `*` ("Piece 1", ...; one of each, in the problem "Puzzle"),
/// and the first `packing_box` as the result; without one, a cube of
/// variable cells as long as the longest piece. A piece is a string (one
/// Z layer) or a list of them; `|` separates Y rows, each character is an
/// X cell, `.` empty, `{...}` annotations skipped. Pieces larger than 256
/// in some direction, or without a filled cell, are left out. ParseError
/// when no piece was read, or the shapes pass `maxCells` together.
[[nodiscard]] Result<Puzzle> readPuzzlecad(std::string_view text,
                                           std::uint64_t maxCells = kMaxImportCells);

/// The most pieces a Puzzlecad export writes copies of (the caller's check).
inline constexpr std::uint64_t kMaxPuzzlecadCopies = 4096;

/// What a Puzzlecad export writes beyond the original's file.
struct PuzzlecadOptions {
  /// Each of the problem's parts as many times as the problem has it (its
  /// maximum), not each distinct shape once.
  bool copies = false;
  /// A sliding puzzle's trays, its start/goal shapes (writePuzzlecad);
  /// nothing on other grids.
  bool tray = false;
  /// The height in cells of the wall around a tray, which grows the tray
  /// by a cell on each side; 0 for none.
  unsigned int lip = 0;
  /// Without a problem, the shapes to write (indices, in this order)
  /// rather than every shape; a start/goal shape is left out.
  std::vector<unsigned int> shapes;
};

/// The puzzlecad OpenSCAD file of a problem's pieces (saveOpenScadPuzzle;
/// the original's MagellanTIC.scad layout): the distinct shapes of
/// `problem`'s parts but its result (each copy with options.copies), or,
/// when that leaves none, every shape of the puzzle but that result (every
/// shape when the problem is null, or options.shapes when it lists some),
/// each as its bounding box's layers in a
/// `burr_plate`. `sourceName`'s last path component is named in the header
/// comment ("unknown" when empty). Nothing (the original's false) when no
/// shape has a filled or variable cell. Cube and sliding grids only: the
/// caller refuses other grids.
///
/// A sliding puzzle's start/goal shapes are never pieces. With
/// options.tray (sliding grid only) its trays come first: the problem's
/// start/goal shape, else every one. A tray is a floor layer under its
/// bounding box and wall layers on it, the cells the tray leaves empty, as
/// many as options.lip (at least one, none when there is no wall cell); a
/// lip grows the floor by a cell on each side and walls the ring around it.
///
/// A cube-grid puzzle's file is the original's, its "burr_scal" typo
/// fixed and without its disabled `packing_box` placeholder. A sliding puzzle's has beveled units and no inset, each tray a
/// `packing_box` of its own (all but the first disabled with `*`), then
/// the pieces' `burr_plate` (disabled while a box is written), both scaled
/// to [16, 16, 5.6] mm.
[[nodiscard]] std::optional<std::string> writePuzzlecad(const Puzzle& puzzle,
                                                        const Problem* problem,
                                                        std::string_view sourceName,
                                                        const PuzzlecadOptions& options = {});

}  // namespace bt::io
