// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The slide search: the fewest moves that take the pieces of a sliding
/// puzzle from a start layout to the goal, and the routes the moves take.
///
/// One move is one piece going anywhere it can reach while the others stay
/// put, round corners included; with nested slides a piece may carry what
/// is nested in its outline. A piece stays in its layer; with vertical
/// moves, on a tray more than one layer deep, it may also go up and down
/// (lifted out of a box and put back elsewhere). The search (the Sliding
/// Puzzle Solver) has no limit on the arrangements it visits: it goes
/// breadth first, level by level, every level a sorted set that lives in
/// memory while the memory budget allows and in a file otherwise, spreads
/// each level over threads, searches from the goal too once a level is wide
/// (when the goal is one arrangement), and is saved when it is stopped so
/// that a search of the same start carries it on. It finds a path with the
/// fewest moves, the same every run, or proves there is none.
///
/// Ported from src/lib/sliding.{h,cpp} (the search, routes and outcomes;
/// the tray model is bt/model/sliding.h). The original's Fast and Deep
/// solvers, which stopped at 250,000 and 1,000,000 arrangements, and its
/// queue search are not kept. The original's environment switches are
/// options here: SHAPELOGIC_SLIDE_LEGACY (legacy),
/// SHAPELOGIC_NO_SLIDE_SYMMETRY (likePiecesApart), SHAPELOGIC_SLIDE_TRACE
/// (onLevel), and
/// SHAPELOGIC_SLIDE_DIR and SHAPELOGIC_SLIDE_DISK_GB (workDir, diskBudget,
/// which the CLI and the engine set).

#include "shapelogic/base/stop.h"
#include "shapelogic/model/disassembly.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

/// A shift of a piece or a group of pieces in cells, (dx, dy, dz).
using SlideShift = std::array<int, 3>;

/// Memory a search may fill with arrangements, about 2 GB. A search that
/// reaches it with nowhere to write its levels stops instead of exhausting
/// the machine's memory; with a work folder its levels go to files and the
/// disk is the limit.
inline constexpr std::uint64_t kSearchMemoryBytes = 2000000000ULL;

/// The memory a search may fill with arrangements, in bytes: `bytes` when
/// it is set; otherwise kSearchMemoryBytes, or with `high`, half the
/// machine's physical memory. High never allows less than normal, and is
/// the same as normal when the memory size cannot be read.
[[nodiscard]] std::uint64_t memoryBudget(std::uint64_t bytes, bool high);

/// Arrangements a search may hold when each takes stateBytes: as many as
/// fit in memoryBudget(bytes, high), and at least one.
[[nodiscard]] std::uint64_t memoryStates(std::uint64_t stateBytes, bool high,
                                         std::uint64_t bytes = 0);

/// How a slide search ended.
enum class SlideOutcome : std::uint8_t {
  Found,    ///< a path to the goal
  NoPath,   ///< every reachable arrangement was searched: there is no path
  Memory,   ///< stopped at its memory limit; a path may still exist
  Stopped,  ///< the stop token asked it to
  Disk,     ///< the search filled the disk it may use, or could not write; see error
};

/// One level of the search, done (SHAPELOGIC_SLIDE_TRACE in the
/// original): the line it printed.
using SlideLevelTrace = std::function<void(const std::string&)>;

/// The least free disk a slide search leaves for saving itself, besides
/// what the save writes: 100 MB.
inline constexpr std::uint64_t kSlideSaveReserve = 100000000ULL;

/// The settings of one slide search, and how it went.
struct SlideSearch {
  /// Arrangements' worth of memory the search may fill (memoryStates each)
  /// before its sets go to files, or with no work folder before it gives
  /// up; 0 for the memory budget.
  std::uint64_t maxMemoryStates = 0;
  /// With maxMemoryStates 0, budget half the physical memory, not 2 GB.
  bool highMemory = false;
  /// With maxMemoryStates 0, the memory the arrangements may fill, in
  /// bytes; 0 for the budget highMemory says (memoryBudget). Set, highMemory
  /// plays no part.
  std::uint64_t memoryBytes = 0;
  /// Let a piece carry what is nested in its outline.
  bool nested = false;
  /// Let a piece also carry what is partly nested in its outline: a piece
  /// with at least one cell in the outline, the rest sticking out of it.
  /// A further move: the moves that leave such pieces behind stay. Implies
  /// nested.
  bool partialNested = false;
  /// On a tray more than one layer deep, let a piece also move up and
  /// down; off, every piece stays in its layer.
  bool verticalMoves = false;
  /// Polled as the search runs; a stop request ends it (Stopped).
  StopToken stop;
  /// When set, kept up to date with the arrangements visited so far.
  std::atomic<std::uint64_t>* progress = nullptr;
  /// When set, the level search keeps it up to date with the moves it has
  /// searched to (both ends together).
  std::atomic<std::uint64_t>* depthProgress = nullptr;
  /// When set, counts the levels searched out while the path is traced back
  /// across thinned levels.
  std::atomic<std::uint64_t>* traced = nullptr;
  /// The threads the level search spreads its levels over; null for a pool
  /// of the search's own of `threads` threads.
  ThreadPool* pool = nullptr;
  /// Threads for the level search: at most this many of the pool's; 0 for
  /// all of the pool's, or with no pool, every hardware thread.
  unsigned int threads = 0;
  /// Where the level search keeps its levels once they outgrow the memory
  /// budget, in a folder of its own that goes when the search ends unless
  /// the search is saved there (see resume). Empty: everything stays in
  /// memory, and the budget is the limit.
  std::string workDir;
  /// Disk those levels may take, in bytes; 0 for half the free space, at
  /// most 200 GB. Either way it is held to what the disk has room for
  /// (diskBudgetFor in base/system.h). Without nested slides the levels
  /// kept for tracing the path back are thinned as the disk fills (every
  /// other one goes), so only the newest levels count against it.
  std::uint64_t diskBudget = 0;
  /// Free disk the search leaves so that it can always save itself: it
  /// stops (Disk, saved) before the free space falls below this plus what
  /// a save writes (its sets in memory) and what the next level may take.
  /// The tests raise it to make a disk look full.
  std::uint64_t diskReserve = kSlideSaveReserve;
  /// Bytes the search's sets may take in memory before they go to files;
  /// 0 for the memory budget. A small search never touches the disk.
  std::uint64_t memoryLevelBytes = 0;
  /// Carry on this start's saved search in workDir, if there is one (a
  /// search stopped by its token is saved there, and so is one that filled
  /// its disk budget or the disk). Off, a saved search is thrown away.
  bool resume = true;
  /// Save the search this often as it runs, so a crash loses no more. 0
  /// saves only when the search stops.
  unsigned int autosaveMinutes = 20;
  /// Search from the goal as well once a level has this many arrangements,
  /// when the goal is one arrangement (not with nested slides): the ends
  /// meet in the middle, and the path is as short. 0 for the default, a
  /// million; smaller puzzles find the same path as a search from the
  /// start alone.
  std::uint64_t meetFrom = 0;
  /// Keep every this-many-th level for tracing the path back from the
  /// start (1: every level; the search thins them further as the disk
  /// fills). Above 1 the gaps are searched when the path is traced: the
  /// tests set it to try that on small puzzles. Not with nested slides.
  unsigned int keepEvery = 1;

  /// A/B switches: the original search over text keys (the fallback for
  /// trays the position tables cannot pack into 128 bits; it finds a path
  /// as short, on one thread, with no levels on disk); like pieces told
  /// apart instead of searched as one.
  bool legacy = false;
  bool likePiecesApart = false;
  /// Coarse moves: a piece may stop only where it is a multiple of this
  /// many cells from its start in x and in y (it still passes through the
  /// cells between). 1 searches every stop. Above 1 the search is a
  /// restriction: a path it finds is real, but NoPath proves nothing.
  int step = 1;
  /// Told each level the level search finishes; empty for none.
  SlideLevelTrace onLevel;

  /// Out: how the search ended, and how many arrangements it visited.
  SlideOutcome outcome = SlideOutcome::NoPath;
  std::uint64_t visited = 0;
  /// Out: the memory limit the search used, in arrangements.
  std::uint64_t memoryStates = 0;
  /// Out, for the level search: the moves searched to (both ends together),
  /// and the most disk its levels took.
  std::uint64_t depth = 0;
  std::uint64_t diskBytes = 0;
  /// Out, for the level search: how many levels apart the levels kept for
  /// tracing the path back are (1: every level).
  unsigned int levelInterval = 1;
  /// Out, for the level search: it carried on a saved search; it was saved
  /// when it stopped (a search with `resume` carries it on).
  bool resumed = false;
  bool saved = false;
  /// Out, for the level search with nested slides: arrangements reached by a
  /// move that cannot be made backwards that had been reached two or more
  /// moves earlier, so had to be looked for on disk.
  std::uint64_t oneWayRepeats = 0;
  /// Out: what went wrong, for Disk.
  std::string error;
  /// Out, for Disk: the budget was held to the disk's free space, so a
  /// larger one would not have helped.
  bool diskHeldToFree = false;
};

/// The one sliding solver's name (the GUI's Solver Type for a sliding
/// puzzle, bt's --solver).
inline constexpr std::string_view kSlidingSolverName = "Sliding Puzzle Solver";

/// What a sliding solve asks of each search (the solve thread's sliding
/// flags: PAR_NESTED_SLIDES, PAR_PARTIAL_NESTED, PAR_HIGH_MEMORY, with
/// PAR_FULL_SEARCH always on; and vertical moves, which the original, whose
/// trays were one layer deep, did not have), and where the search keeps
/// its levels.
struct SlidingOptions {
  bool nested = false;
  /// Implies nested.
  bool partialNested = false;
  /// SlideSearch::verticalMoves.
  bool verticalMoves = false;
  /// A piece with neither a start nor a goal on the board may be placed in
  /// any quarter turn in its layers, not only as drawn; with
  /// mirroredOrientations, also their mirror images (partTransforms in
  /// bt/model/sliding.h). Every piece keeps its orientation as it slides.
  bool anyOrientation = false;
  /// Plays a part only with anyOrientation.
  bool mirroredOrientations = false;
  bool highMemory = false;
  /// SlideSearch::memoryBytes: set by the program (the engine from its
  /// settings; the CLI leaves it to highMemory).
  std::uint64_t memoryBytes = 0;
  /// SlideSearch::workDir and diskBudget: set by the program (the CLI from
  /// SHAPELOGIC_SLIDE_DIR or the user's cache folder, the engine from its
  /// settings).
  std::string workDir;
  std::uint64_t diskBudget = 0;
  /// The A/B switches (SlideSearch).
  bool legacy = false;
  bool likePiecesApart = false;
  /// SlideSearch::step.
  int step = 1;
};

/// A search set up as the options say (nested, partialNested,
/// verticalMoves, highMemory, memoryBytes, workDir, diskBudget, the
/// switches); the rest is the caller's.
[[nodiscard]] SlideSearch slideSearchFor(const SlidingOptions& options);

/// A path with the fewest moves from the start assembly to the goal of
/// problem `problem` (whose result is the tray): a separation of one node
/// whose states are the pieces' placements (where the assembly puts them,
/// not shifts from it), start first. None when no path is found within the
/// search's limits; search.outcome says why.
[[nodiscard]] std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                                      const Assembly& start, SlideSearch& search);

/// findSlidePath with the default search, and nested slides or not.
[[nodiscard]] std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                                      const Assembly& start, bool nested = false);

/// Deletes the saved searches of this problem's starts in workDir, if there
/// are any (a solve that starts afresh).
void discardSaved(const Puzzle& puzzle, const Problem& problem, const std::string& workDir);

/// Deletes the saved searches in workDir that nothing has touched for
/// `days` days (searches of starts never searched again); returns how
/// many went.
unsigned int sweepSaved(const std::string& workDir, unsigned int days = 30);

/// The corner points, as shifts from the start, of the route the moving
/// pieces take on step `step` of path, start and end included, with the
/// fewest straight runs. *movers gets the pieces that move: one, or an
/// outer piece and what is nested in it. Empty when the step is not such a
/// slide.
[[nodiscard]] std::vector<SlideShift> slideRoute(const Puzzle& puzzle, const Problem& problem,
                                                 const Separation& path, unsigned int step,
                                                 std::vector<unsigned int>* movers);

/// Makes anim follow each move's route, so a move that turns a corner does
/// not cut across it.
void applySlideRoutes(const Puzzle& puzzle, const Problem& problem, const Separation& path,
                      DisasmToMoves& anim);

/// What the finished picture is: each piece's placement in the last state.
/// Paths with the same key end with the pieces in the same places.
[[nodiscard]] std::string finalPlacementKey(const Separation& path);

}  // namespace bt::sliding
