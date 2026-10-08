// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A paused solve as one file (*.btsolve), to carry it on later or on
/// another machine: the original's BTSOLVE2 format. "BTSOLVE2", then
/// sections, each a 32-bit name length, the name, a 64-bit size and the
/// bytes (little-endian): "puzzle", the puzzle gzip-compressed as it is
/// saved (a brick problem keeps its paused solve in it), and "problem",
/// the paused problem's number as text. Other sections (a stacking
/// solve's "searchz/..." files) are skipped on reading and not written.
/// Ported from the export and import of mainWindow_c::cb_ExportPaused and
/// cb_ImportPaused (src/gui/mainwindow_file.cpp).

#include "bt/base/error.h"
#include "bt/model/puzzle.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace bt::io {

/// What a .btsolve file holds.
struct PausedSolve {
  Puzzle puzzle;
  /// The paused problem's number in the puzzle.
  unsigned int problem = 0;
};

/// The bytes of a .btsolve file.
[[nodiscard]] std::string writePausedSolve(const Puzzle& puzzle, unsigned int problem);
/// Writes a .btsolve file, replacing it.
[[nodiscard]] Result<void> savePausedSolve(const Puzzle& puzzle, unsigned int problem,
                                           const std::filesystem::path& path);

/// Reads .btsolve bytes: ParseError when they are not one (the original's
/// "not a paused solver state exported by BurrTools", "could not be read
/// completely"), or the puzzle cannot be read, or the problem is not one
/// of it.
[[nodiscard]] Result<PausedSolve> parsePausedSolve(std::string_view bytes);
/// Reads a .btsolve file.
[[nodiscard]] Result<PausedSolve> loadPausedSolve(const std::filesystem::path& path);

}  // namespace bt::io
