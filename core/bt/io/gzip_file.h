// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Whole-file reading and writing through zlib: .xmpuzzle files are
/// gzip-compressed, and plain files are read as they are.
/// Replaces src/tools/gzstream.{h,cpp} (the original read through a stream
/// buffer; the format and compression level are the same).

#include "bt/base/error.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace bt::io {

/// The contents of a file, decompressed when it is gzip data and as it is
/// otherwise. A missing or unreadable file is an IoError; an empty file is
/// an empty string.
[[nodiscard]] Result<std::string> readMaybeGzipFile(const std::filesystem::path& path);

/// Writes `data` gzip-compressed (level 9, as the original), replacing the
/// file.
[[nodiscard]] Result<void> writeGzipFile(const std::filesystem::path& path, std::string_view data);

}  // namespace bt::io
