// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The slide search: the fewest moves that take the pieces of a sliding
/// puzzle from a start layout to the goal, and the routes the moves take.
///
/// One move is one piece going anywhere it can reach while the others stay
/// put, round corners included; with nested slides a piece may carry what
/// is nested in its outline. A limited search (the Fast and Deep solvers)
/// goes breadth first through a queue and stops at a number of
/// arrangements; the full search goes level by level, keeps only the
/// newest levels in memory and the older ones in files, and spreads each
/// level over threads. Both find a path with the fewest moves, the same
/// every run.
///
/// Ported from src/lib/sliding.{h,cpp} (the search, routes and outcomes;
/// the tray model is bt/model/sliding.h). The original's environment
/// switches are options here: BURRTOOLS_SLIDE_LEGACY (legacy),
/// BURRTOOLS_NO_SLIDE_SYMMETRY (likePiecesApart), BURRTOOLS_SLIDE_QUEUE
/// (fullThroughQueue), BURRTOOLS_SLIDE_TRACE (onLevel), and
/// BURRTOOLS_SLIDE_DIR and BURRTOOLS_SLIDE_DISK_GB (workDir, diskBudget,
/// which the CLI and the engine set).

#include "bt/base/stop.h"
#include "bt/model/disassembly.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bt {
class Assembly;
class DisasmToMoves;
class Problem;
class Puzzle;
class ThreadPool;
}  // namespace bt

namespace bt::sliding {

/// Arrangements the Fast and the Deep solvers visit at most.
inline constexpr std::uint64_t kSearchStates = 250000;
inline constexpr std::uint64_t kDeepSearchStates = 1000000;
/// maxStates of a full search: no limit on the arrangements visited.
inline constexpr std::uint64_t kFullSearch = 0;
/// Memory any search may fill with arrangements, about 2 GB. A search that
/// reaches it stops instead of exhausting the machine's memory. A full
/// search keeps only its newest levels in memory, so it goes much further.
inline constexpr std::uint64_t kSearchMemoryBytes = 2000000000ULL;

/// Arrangements a search may hold when each takes stateBytes: as many as
/// fit in kSearchMemoryBytes, or with `high`, in half the machine's
/// physical memory. High never allows fewer than normal, and is the same
/// as normal when the memory size cannot be read.
[[nodiscard]] std::uint64_t memoryStates(std::uint64_t stateBytes, bool high);

/// How a slide search ended.
enum class SlideOutcome : std::uint8_t {
  Found,    ///< a path to the goal
  NoPath,   ///< every reachable arrangement was searched: there is no path
  Limit,    ///< stopped at maxStates; a path may still exist
  Memory,   ///< stopped at its memory limit; a path may still exist
  Stopped,  ///< the stop token asked it to
  Disk,     ///< a full search could not keep its levels on disk; see error
};

/// One level of a full search, done (BURRTOOLS_SLIDE_TRACE in the
/// original): the line it printed.
using SlideLevelTrace = std::function<void(const std::string&)>;

/// The settings of one slide search, and how it went.
struct SlideSearch {
  /// Arrangements to visit before giving up; kFullSearch for no limit (the
  /// full search).
  std::uint64_t maxStates = kSearchStates;
  /// Arrangements to hold in memory before giving up, whatever maxStates
  /// says; 0 for as many as fit the memory budget (memoryStates). A full
  /// search holds only its newest levels in memory, so it counts just
  /// those.
  std::uint64_t maxMemoryStates = 0;
  /// With maxMemoryStates 0, budget half the physical memory, not 2 GB.
  bool highMemory = false;
  /// Let a piece carry what is nested in its outline.
  bool nested = false;
  /// Let a piece also carry what is partly nested in its outline: a piece
  /// with at least one cell in the outline, the rest sticking out of it.
  /// Implies nested.
  bool partialNested = false;
  /// Polled as the search runs; a stop request ends it (Stopped).
  StopToken stop;
  /// When set, kept up to date with the arrangements visited so far.
  std::atomic<std::uint64_t>* progress = nullptr;
  /// When set, a full search keeps it up to date with the moves it has
  /// searched to.
  std::atomic<std::uint64_t>* depthProgress = nullptr;
  /// The threads a full search spreads its levels over; null for a pool of
  /// the search's own of `threads` threads.
  ThreadPool* pool = nullptr;
  /// Threads for a full search: at most this many of the pool's; 0 for all
  /// of the pool's, or with no pool, every hardware thread.
  unsigned int threads = 0;
  /// Where a full search keeps its older levels, in a folder of its own
  /// that goes when the search ends. Empty: they stay in memory.
  std::string workDir;
  /// Disk those levels may take, in bytes; 0 for all the free space but a
  /// margin, and at least a little of it.
  std::uint64_t diskBudget = 0;
  /// Bytes of older levels a full search keeps in memory before it starts
  /// writing them to disk; 0 for the default (256 MB). A small search
  /// never touches the disk.
  std::uint64_t memoryLevelBytes = 0;

  /// A/B switches: the original search over text keys (the fallback for
  /// trays the position tables cannot pack into 128 bits); like pieces told
  /// apart instead of searched as one; a full search through the queue
  /// instead of level by level.
  bool legacy = false;
  bool likePiecesApart = false;
  bool fullThroughQueue = false;
  /// Told each level a full search finishes; empty for none.
  SlideLevelTrace onLevel;

  /// Out: how the search ended, and how many arrangements it visited.
  SlideOutcome outcome = SlideOutcome::NoPath;
  std::uint64_t visited = 0;
  /// Out: the memory limit the search used, in arrangements.
  std::uint64_t memoryStates = 0;
  /// Out, for a full search: the moves searched to, and the most disk its
  /// levels took.
  std::uint64_t depth = 0;
  std::uint64_t diskBytes = 0;
  /// Out, for a full search with nested slides: arrangements reached by a
  /// move that cannot be made backwards that had been reached two or more
  /// moves earlier, so had to be looked for on disk.
  std::uint64_t oneWayRepeats = 0;
  /// Out: what went wrong, for Disk.
  std::string error;
};

/// The three sliding solvers: how far a search may go (the GUI's Solver
/// Type for a sliding puzzle, burrTxt's --solver).
enum class SlidingSolver : std::uint8_t {
  Fast,  ///< up to kSearchStates arrangements ("Sliding Fast Solver (250k depth)")
  Deep,  ///< up to kDeepSearchStates ("Sliding Deep Solver (1mil depth)")
  Full,  ///< no limit: levels on disk ("Sliding Full Solver (full depth)")
};

/// What a sliding solve asks of each search (the solve thread's sliding
/// flags: PAR_DEEP_SEARCH, PAR_FULL_SEARCH, PAR_NESTED_SLIDES,
/// PAR_PARTIAL_NESTED, PAR_HIGH_MEMORY), and where a full search keeps its
/// levels.
struct SlidingOptions {
  /// The GUI's default is the full search.
  SlidingSolver solver = SlidingSolver::Full;
  bool nested = false;
  /// Implies nested.
  bool partialNested = false;
  bool highMemory = false;
  /// SlideSearch::workDir and diskBudget: set by the program (the CLI from
  /// BURRTOOLS_SLIDE_DIR or the user's cache folder, the engine from its
  /// settings).
  std::string workDir;
  std::uint64_t diskBudget = 0;
  /// The A/B switches (SlideSearch).
  bool legacy = false;
  bool likePiecesApart = false;
  bool fullThroughQueue = false;
};

/// The arrangements a solver visits at most (kFullSearch for Full).
[[nodiscard]] std::uint64_t maxStatesOf(SlidingSolver solver);

/// A search set up as the options say (maxStates, nested, partialNested,
/// highMemory, workDir, diskBudget, the switches); the rest is the
/// caller's.
[[nodiscard]] SlideSearch slideSearchFor(const SlidingOptions& options);

/// A path with the fewest moves from the start assembly to the goal of
/// problem `problem` (whose result is the tray): a separation of one node
/// whose states are the pieces' placements (where the assembly puts them,
/// not shifts from it), start first. None when no path is found within the
/// search's limits; search.outcome says why.
[[nodiscard]] std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                                      const Assembly& start, SlideSearch& search);

/// findSlidePath with a limit on the arrangements and nested slides.
[[nodiscard]] std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                                      const Assembly& start,
                                                      std::uint64_t maxStates = kSearchStates,
                                                      bool nested = false);

/// The corner points, as shifts from the start, of the route the moving
/// pieces take on step `step` of path, start and end included, with the
/// fewest straight runs. *movers gets the pieces that move: one, or an
/// outer piece and what is nested in it. Empty when the step is not such a
/// slide.
[[nodiscard]] std::vector<std::pair<int, int>> slideRoute(const Puzzle& puzzle,
                                                          const Problem& problem,
                                                          const Separation& path,
                                                          unsigned int step,
                                                          std::vector<unsigned int>* movers);

/// Makes anim follow each move's route, so a move that turns a corner does
/// not cut across it.
void applySlideRoutes(const Puzzle& puzzle, const Problem& problem, const Separation& path,
                      DisasmToMoves& anim);

/// What the finished picture is: each piece's placement in the last state.
/// Paths with the same key end with the pieces in the same places.
[[nodiscard]] std::string finalPlacementKey(const Separation& path);

}  // namespace bt::sliding
