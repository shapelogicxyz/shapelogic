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

#include "shapelogic/base/memory_share.h"
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
class SearchSample;
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

/// The memory a search may fill with arrangements, in bytes: its share of
/// `share` when that is set; else `bytes` when it is set; otherwise
/// kSearchMemoryBytes, or with `high`, half the machine's physical memory.
/// High never allows less than normal, and is the same as normal when the
/// memory size cannot be read.
[[nodiscard]] std::uint64_t memoryBudget(std::uint64_t bytes, bool high,
                                         const MemoryShare* share = nullptr);

/// Arrangements a search may hold when each takes stateBytes: as many as
/// fit in memoryBudget(bytes, high, share), and at least one.
[[nodiscard]] std::uint64_t memoryStates(std::uint64_t stateBytes, bool high,
                                         std::uint64_t bytes = 0,
                                         const MemoryShare* share = nullptr);

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

/// What a level search does once it has been asked to stop, for the
/// solve's progress (read on other threads while the search writes it):
/// until `saving`, it finishes what it has under way (the level being
/// merged cannot be cut short); then it writes its sets still in memory to
/// its folder, `doneBytes` of `totalBytes`, so that it can be carried on.
struct SlideStopProgress {
  std::atomic<bool> saving{false};
  std::atomic<std::uint64_t> doneBytes{0};
  std::atomic<std::uint64_t> totalBytes{0};
};

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
  /// When set (and maxMemoryStates is 0), the memory budget is this search's share
  /// of what the searches running at once share, read again as the search
  /// goes, so it shrinks when another search starts and grows when one
  /// ends; memoryBytes and highMemory play no part. The caller holds a
  /// lease on it for as long as the search runs.
  const MemoryShare* memoryShare = nullptr;
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
  /// Polled as the search runs; a stop request ends it (Stopped). The
  /// level search saves itself as it stops (see resume), unless the stop
  /// is an abort, which throws the search away.
  StopToken stop;
  /// When set, the level search tells what it does once stopped.
  SlideStopProgress* stopProgress = nullptr;
  /// When set, the arrangements the search visits are added to it as it
  /// goes, so that it can count a whole solve's searches: a search carried
  /// on adds only those it visits now (its saved count is in `visited`),
  /// and a level cut short by a stop takes its own back off.
  std::atomic<std::uint64_t>* progress = nullptr;
  /// When set, the level search keeps it up to date with the moves it has
  /// searched to (both ends together).
  std::atomic<std::uint64_t>* depthProgress = nullptr;
  /// When set, counts the levels searched out while the path is traced back
  /// across thinned levels.
  std::atomic<std::uint64_t>* traced = nullptr;
  /// When set, the search puts a picture of an arrangement it is looking
  /// at here when one is asked for (a live preview).
  SearchSample* sample = nullptr;
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
  /// Save the search as it runs, so that a crash loses little: a minute
  /// in, then as often as saving stays cheap (SaveCadence). Off, it is
  /// saved only when it stops. autosaveFloorSeconds is the least time
  /// between two saves (the tests ask for less).
  bool autosave = true;
  unsigned int autosaveFloorSeconds = 60;
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
  /// Optimized paths: with a size above 1, a piece may stop only where its
  /// place, x and y modulo the size, is one of its grid offsets: an offset
  /// at which a piece of the same cells starts or fills a goal, or lies
  /// wholly in the outline of another piece at one of that piece's offsets
  /// (a block's hole). A piece with no such offset stops anywhere. Pieces
  /// still pass through the cells between. The search is a restriction: a
  /// path it finds is real but may have more than the fewest moves, and
  /// NoPath proves nothing. 0 or 1 searches every stop.
  ///
  /// With nested slides the stops a piece has inside one cell of the grid
  /// are searched as one where the piece can be at any of them (loose
  /// stops, decision 0031): the same moves found, in fewer arrangements.
  int grid = 0;
  /// A/B switch: with a grid, every stop of a grid cell told apart instead
  /// of searched as one.
  bool gridStopsApart = false;
  /// A/B switch: with a grid, an arrangement numbers every position of a
  /// piece, as it does without one, instead of only the positions the grid
  /// lets it stop at. The same search in more bits: a board that fits the
  /// key only with the stops numbered goes to the search over text keys.
  bool plainKeys = false;
  /// Told each level the level search finishes; empty for none.
  SlideLevelTrace onLevel;
  /// The level search: told how many moves deep it is (both ends
  /// together) as it begins (a search carried on: the depth it goes on
  /// from, whose moves the caller may not have times for) and each time it is a move deeper, on the search's thread, so
  /// that the caller can time each move (the Performance Analysis). Empty
  /// for none.
  std::function<void(std::uint64_t)> onDepth;

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
  /// Out, for the level search: the stops inside a grid cell were searched
  /// as one (see grid). Then `visited` counts arrangements that may each
  /// stand for several.
  bool looseStops = false;
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
  bool gridStopsApart = false;
  bool plainKeys = false;
  /// Hold the stops to the grid slideGridFor finds for the problem, when
  /// it finds one (the Solver's Automatically use optimized paths): a
  /// smaller search, whose path may have more than the fewest moves.
  bool optimizedPaths = false;
  /// A/B switch: SlideSearch::grid of this size, whether or not the board
  /// fits it; above 1 it takes the place of optimizedPaths' grid.
  int grid = 0;
};

/// The grid optimized paths would hold a sliding problem's stops to, and
/// how far it narrows them.
struct SlideGrid {
  /// Cells a side (SlideSearch::grid); 0 when the board fits no grid that
  /// leaves fewer stops.
  int size = 0;
  /// The places the problem's pieces can stop on the empty board, summed
  /// over the pieces: all of them, and those the grid leaves.
  std::uint64_t stops = 0;
  std::uint64_t gridStops = 0;
};

/// The grid for optimized paths: the largest size above 1 that divides the
/// board's floor (its bounding box) in x and y, for which at least one
/// piece measures a multiple of it in x and y (a block) and every such
/// piece starts and fills its goals on the grid's lines, and which leaves
/// fewer stops. Each piece's offsets are found as SlideSearch::grid finds
/// them, from the starts and goals stamped. None for a problem whose result
/// is no start/goal shape, or that has no starts.
[[nodiscard]] SlideGrid slideGridFor(const Puzzle& puzzle, const Problem& problem);

/// The grid a solve's searches use: options.grid when above 1, else with
/// optimizedPaths the size slideGridFor finds (0 when none), else 0.
[[nodiscard]] int searchGrid(const Puzzle& puzzle, const Problem& problem,
                             const SlidingOptions& options);

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

/// The folders in workDir holding saved searches of this problem's starts
/// (a folder each): what discardSaved deletes. Empty without a work folder.
[[nodiscard]] std::vector<std::string> savedFolders(const Puzzle& puzzle, const Problem& problem,
                                                    const std::string& workDir);

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
