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

/// The puzzlecad OpenSCAD file of a problem's pieces (saveOpenScadPuzzle;
/// the original's MagellanTIC.scad layout): the distinct shapes of
/// `problem`'s parts but its result, or, when that leaves none, every
/// shape of the puzzle but that result (every shape when the problem is
/// null), each as its bounding box's layers in a `burr_plate`.
/// `sourceName`'s last path component is named in the header comment ("unknown" when empty). Nothing (the original's false)
/// when no shape has a filled or variable cell. Cube grid only: the
/// caller refuses other grids.
[[nodiscard]] std::optional<std::string> writePuzzlecad(const Puzzle& puzzle,
                                                        const Problem* problem,
                                                        std::string_view sourceName);

}  // namespace bt::io
