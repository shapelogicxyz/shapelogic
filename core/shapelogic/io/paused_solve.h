// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A paused solve as one file (*.btsolve), to carry it on later or on
/// another machine: the original's BTSOLVE2 format. "BTSOLVE2", then
/// sections, each a 32-bit name length, the name, a 64-bit size and the
/// bytes (little-endian): "puzzle", the puzzle gzip-compressed as it is
/// saved (a brick or sliding problem keeps its paused solve in it),
/// "problem", the paused problem's number as text, and for a stacking
/// solve "searchz/<name>", the files of its saved search (a Panex search's
/// folder: state.bin and, if wanted, the levels), each packed by blockpack
/// (levels as key gaps). Other sections are skipped.
/// Ported from the export and import of mainWindow_c::cb_ExportPaused and
/// cb_ImportPaused (src/gui/mainwindow_file.cpp): writeSection,
/// writePackedSection, readSectionHeader, isPlainFileName and the loops.

#include "shapelogic/base/error.h"
#include "shapelogic/model/puzzle.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace bt::io {

/// What a .btsolve file holds.
struct PausedSolve {
  Puzzle puzzle;
  /// The paused problem's number in the puzzle.
  unsigned int problem = 0;
};

/// A stacking solve's saved search to put into a .btsolve file.
struct SavedSearchFiles {
  /// The folder of the problem's saved search (panex::searchFolder).
  std::filesystem::path folder;
  /// With its levels; without, the file is far smaller, and the search
  /// finds its path back by solving the rest by halves.
  bool withLevels = true;
};

/// The bytes of a .btsolve file (without a saved search's files).
[[nodiscard]] std::string writePausedSolve(const Puzzle& puzzle, unsigned int problem);
/// Writes a .btsolve file, replacing it; with `search`, a stacking solve's
/// saved search goes into it too (state.bin, then the levels in name
/// order), packed on up to `threads` threads (0: every hardware thread).
[[nodiscard]] Result<void> savePausedSolve(const Puzzle& puzzle, unsigned int problem,
                                           const std::filesystem::path& path,
                                           const SavedSearchFiles* search = nullptr,
                                           unsigned int threads = 0);

/// Where an import puts a stacking problem's saved search: the folder the
/// search will look for it in, worked out from the puzzle read and the
/// problem's number; empty when it has no place (the import then fails, as
/// the original's did).
using SavedSearchFolder =
    std::function<std::filesystem::path(const Puzzle& puzzle, unsigned int problem)>;

/// The bytes the levels of the saved search in `folder` take (the original
/// asked whether to put them into an export when they were over 1 GB).
[[nodiscard]] std::uint64_t savedLevelBytes(const std::filesystem::path& folder);

/// Reads .btsolve bytes: ParseError when they are not one (the original's
/// "not a paused solver state exported by ShapeLogic", "could not be read
/// completely"), or the puzzle cannot be read, or the problem is not one
/// of it.
[[nodiscard]] Result<PausedSolve> parsePausedSolve(std::string_view bytes);
/// Reads a .btsolve file. With `searchFolder`, a stacking puzzle's saved
/// search (its "searchz/" sections after the "puzzle" and "problem" ones)
/// is unpacked into the folder it names, emptied first; on threads as
/// savePausedSolve. Without, those sections are skipped.
[[nodiscard]] Result<PausedSolve> loadPausedSolve(const std::filesystem::path& path,
                                                  const SavedSearchFolder& searchFolder = {},
                                                  unsigned int threads = 0);

}  // namespace bt::io
