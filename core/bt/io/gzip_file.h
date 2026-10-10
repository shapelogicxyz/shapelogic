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

/// The most bytes a gzip stream may inflate to (256 MiB): a puzzle file is
/// a few MiB at most, and a small stream can inflate to gigabytes.
inline constexpr std::size_t kMaxInflatedBytes = std::size_t{256} << 20;

/// The contents of a file, decompressed when it is gzip data and as it is
/// otherwise. A missing or unreadable file is an IoError; an empty file is
/// an empty string; a file that inflates to more than `maxBytes` is a
/// ParseError.
[[nodiscard]] Result<std::string> readMaybeGzipFile(const std::filesystem::path& path,
                                                    std::size_t maxBytes = kMaxInflatedBytes);

/// Writes `data` gzip-compressed (level 9, as the original), replacing the
/// file only once the new one is whole: it is written to `<file>.tmp` beside
/// the file `path` names (saveTarget) and renamed over it, so a failed save
/// leaves the old file.
[[nodiscard]] Result<void> writeGzipFile(const std::filesystem::path& path, std::string_view data);

/// The file a save to `path` replaces: `path` followed through its symbolic
/// links, so a link is written through, not replaced by a file (the file it
/// names would keep the old puzzle). IoError when that file exists and
/// cannot be written: renaming over a read-only file would replace it.
[[nodiscard]] Result<std::filesystem::path> saveTarget(const std::filesystem::path& path);

/// Renames a file just written over `target` (a saveTarget), giving it the
/// permissions of the file it replaces; removes it when that fails.
[[nodiscard]] Result<void> replaceWith(const std::filesystem::path& written,
                                       const std::filesystem::path& target);

/// `data` gzip-compressed in memory, as writeGzipFile writes it.
[[nodiscard]] std::string gzipCompress(std::string_view data);

/// `data` decompressed when it is gzip data (it starts with the gzip
/// magic), as it is otherwise; ParseError for broken gzip data and for
/// data that inflates to more than `maxBytes`.
[[nodiscard]] Result<std::string> gunzipIfCompressed(std::string_view data,
                                                     std::size_t maxBytes = kMaxInflatedBytes);

}  // namespace bt::io
