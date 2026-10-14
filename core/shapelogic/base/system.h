// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The machine a search runs on: its physical memory, and where large files
/// may be kept between runs.
/// Ported from src/lib/sysmemory.{h,cpp} (executableDirectory is not
/// ported: nothing here looks for files beside the program).

#include <cstdint>
#include <string>

namespace bt {

/// The physical memory of the machine in bytes; 0 when it cannot be read.
[[nodiscard]] std::uint64_t physicalMemoryBytes();

/// Where a program may keep large files between runs:
/// ~/Library/Caches/ShapeLogic on macOS, %LOCALAPPDATA%\ShapeLogic on Windows,
/// $XDG_CACHE_HOME/shapelogic or ~/.cache/shapelogic elsewhere; empty when
/// none of those can be found. It reads the environment, so it is for the
/// programs that own a search (the CLI, the engine), which pass the folder
/// into the core's options; nothing in the core calls it.
[[nodiscard]] std::string userCacheDirectory();

}  // namespace bt
