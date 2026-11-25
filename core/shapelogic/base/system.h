// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The machine a search runs on: its physical memory, its free disk and
/// how much of it a search may take, and where large files may be kept
/// between runs.
/// Ported from src/lib/sysmemory.{h,cpp} (executableDirectory is not
/// ported: nothing here looks for files beside the program).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bt {

/// The physical memory of the machine in bytes; 0 when it cannot be read.
[[nodiscard]] std::uint64_t physicalMemoryBytes();

/// The disk space files written in `dir` (which exists) may take, in
/// bytes; none when it cannot be read. On macOS it counts the space the
/// system frees when asked (purgeable files, local snapshots), as Finder's
/// "available" does; elsewhere it is what the file system reports free.
[[nodiscard]] std::optional<std::uint64_t> availableDiskBytes(const std::filesystem::path& dir);

/// Why files cannot be written in `dir`, which is made first when it is not
/// there; empty when they can. It writes a small file there and removes it
/// again: a folder on a drive that is gone, or one without write access,
/// is found out before a search counts on it.
[[nodiscard]] std::string folderWriteError(const std::filesystem::path& dir);

/// The most disk a search takes when it is not told how much: 200 GB.
inline constexpr std::uint64_t kAutoDiskBudgetMax = 200000000000ULL;

/// The disk a search's files may take in a folder, and how it was chosen.
struct DiskBudget {
  /// Bytes; never 0.
  std::uint64_t bytes = 1;
  /// The free space seen when it was chosen, counting the search's own
  /// files already there; none when it could not be read.
  std::optional<std::uint64_t> free;
  /// No budget was asked for: half the free space, at most
  /// kAutoDiskBudgetMax.
  bool automatic = false;
  /// More was asked for than the disk had room for: the budget is what the
  /// free space allows (all of it but a reserve for everything else).
  bool heldToFree = false;
};

/// The disk a search may take in `dir` (which exists) when it asks for
/// `wanted` bytes (0: automatic) and its files there already take `own`.
/// A budget asked for is held to the free space but a reserve of 2 GB or a
/// twentieth of the free space, and never below the automatic share, so
/// the search stops before the disk is full.
[[nodiscard]] DiskBudget diskBudgetFor(const std::filesystem::path& dir, std::uint64_t wanted,
                                       std::uint64_t own = 0);

/// The most GB a disk budget may be given as (a million GB, a petabyte).
inline constexpr double kMaxDiskBudgetGB = 1e6;

/// A disk budget given in GB (decimal, as the settings and the options
/// read) as bytes; 0 stays 0 (automatic). None for what is no budget: NaN,
/// below 0, infinite or above kMaxDiskBudgetGB.
[[nodiscard]] std::optional<std::uint64_t> gbToBytes(double gb);

/// A disk budget given as text (an environment variable's value, read as
/// burrTxt read it: atof) as bytes; 0 (automatic) for none, an empty one,
/// or one that is no budget (gbToBytes; the CLI refuses those first).
[[nodiscard]] std::uint64_t diskBudgetFromText(const char* text);

/// How a budget was chosen, for a message: "half the 90.0 GB free there",
/// "as much as the 90.0 GB free there allows", or empty for a budget asked
/// for and given.
[[nodiscard]] std::string diskBudgetReason(const DiskBudget& budget);

/// Reads `len` bytes at `offset` of the open file descriptor `fd` without
/// moving its position, so several threads may read one file at once
/// (pread; ReadFile with an offset on Windows, whose header stays here).
/// False when they are not all there.
[[nodiscard]] bool readFileAt(int fd, std::uint64_t offset, void* buf, std::size_t len);

/// Lets the process open at least `files` files at once where the system
/// allows (the soft limit raised toward the hard one): a slide search
/// reads many sets at a time. For the programs that own a search; nothing
/// in the core calls it. Returns the limit now in force, 0 when unknown.
std::uint64_t raiseOpenFileLimit(std::uint64_t files);

/// Where a program may keep large files between runs:
/// ~/Library/Caches/ShapeLogic on macOS, %LOCALAPPDATA%\ShapeLogic on Windows,
/// $XDG_CACHE_HOME/shapelogic or ~/.cache/shapelogic elsewhere; empty when
/// none of those can be found. It reads the environment, so it is for the
/// programs that own a search (the CLI, the engine), which pass the folder
/// into the core's options; nothing in the core calls it.
[[nodiscard]] std::string userCacheDirectory();

/// Where searches of one kind keep their levels when no option names a
/// folder, the one rule of every program (cleanup item 11.6):
/// `fromEnvironment`, the value of the program's variable
/// (SHAPELOGIC_SLIDE_DIR, SHAPELOGIC_PANEX_DIR), unless it is null or
/// empty; else `name` ("sliding", "panex") in `cache` (userCacheDirectory);
/// empty, in memory, when that is empty too. The caller reads both: the
/// core reads no environment.
[[nodiscard]] std::string defaultSearchFolder(const char* fromEnvironment, const std::string& cache,
                                              std::string_view name);

}  // namespace bt
