// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Reading and writing .xmpuzzle files: gzip-compressed XML (plain XML is
/// read too). The reader accepts what the original accepts and keeps what
/// it does not model (decisions 0006 and 0008); the writer produces the
/// original's element order and layout.
/// Ported from the load and save functions of src/lib/puzzle.cpp,
/// problem.cpp, solution.cpp, assembly.cpp, disassembly.cpp, voxel.cpp and
/// gridtype.cpp.

#include "bt/base/error.h"
#include "bt/model/puzzle.h"
#include "bt/model/xml_element.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace bt::io {

/// A puzzle from its <puzzle> element. Errors are ParseError, with the line
/// of the offending element.
[[nodiscard]] Result<Puzzle> readPuzzle(const XmlElement& root);

/// A puzzle from XML text.
[[nodiscard]] Result<Puzzle> parsePuzzle(std::string_view xml);

/// A puzzle from a file, gzip-compressed or plain.
[[nodiscard]] Result<Puzzle> loadPuzzle(const std::filesystem::path& path);

/// The puzzle as XML text, in the original's layout.
[[nodiscard]] std::string writePuzzle(const Puzzle& puzzle);

/// Saves the puzzle gzip-compressed, as the original does.
[[nodiscard]] Result<void> savePuzzle(const Puzzle& puzzle, const std::filesystem::path& path);

}  // namespace bt::io
