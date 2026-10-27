// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The Panex Solver: the fewest rod transfers for stacking puzzles, built
/// for large Panex towers. It searches level by level from both ends (from
/// one end only when the goal is the start's mirror image), holds only the
/// newest levels in memory, keeps every few levels on disk when they do
/// not fit, traces the path back between them once the ends meet, and can
/// stop and carry on later from what it saved (a folder per puzzle in a
/// work folder). The Stacking Solver runs on it too, whenever the puzzle
/// fits it (anyRules).
///
/// Ported from src/lib/panex.{h,cpp}. The original's environment switches
/// are options here: SHAPELOGIC_PANEX_DIR (workDir, which the CLI and the
/// engine set), SHAPELOGIC_PANEX_DISK_GB (diskBudget), SHAPELOGIC_PANEX_LOG
/// (onLevel), SHAPELOGIC_NO_TOWER_RULE (towerRule), SHAPELOGIC_THREADS
/// (threads, pool). Its threads are a ThreadPool's (decision 0004), its
/// stop a StopToken. The saved search is byte for byte the original's, so
/// a search stopped by one program carries on in the other.

#include "shapelogic/base/stop.h"
#include "shapelogic/model/disassembly.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace bt {
class Problem;
class Puzzle;
class ThreadPool;
}  // namespace bt

namespace bt::panex {

/// Empty when the Panex Solver can take this problem, otherwise why it
/// cannot (the original's words). With anyRules, rod sets without Panex
/// columns are taken too: the same search then serves the Stacking Solver.
[[nodiscard]] std::string unsupported(const Puzzle& puzzle, const Problem& problem,
                                      bool anyRules = false);

/// A saved search of this problem in workDir that a search with `resume`
/// carries on, described for the user ("a saved search 200 moves deep,
/// 3.4M stackings"); empty when there is none.
[[nodiscard]] std::string savedSearch(const Puzzle& puzzle, const Problem& problem,
                                      const std::string& workDir);

/// Milliseconds of solving the saved search of this problem holds; 0 when
/// there is none.
[[nodiscard]] std::uint64_t savedMs(const Puzzle& puzzle, const Problem& problem,
                                    const std::string& workDir);

/// The folder in workDir where this problem's saved search lives, whether
/// there is one or not; empty when it has no place (workDir empty, or the
/// problem is not one the search takes). For export and import.
[[nodiscard]] std::string searchFolder(const Puzzle& puzzle, const Problem& problem,
                                       const std::string& workDir);

/// Deletes the saved search of this problem in workDir, if there is one.
void discardSaved(const Puzzle& puzzle, const Problem& problem, const std::string& workDir);

/// How a Panex search ended.
enum class Outcome : std::uint8_t {
  Found,    ///< a shortest path to the goal
  NoPath,   ///< every reachable stacking was searched: there is no path
  Memory,   ///< the frontiers outgrew the memory budget
  Stopped,  ///< the stop token asked it to
  Error,    ///< see PanexSearch::error
};

/// One level of a search, done (SHAPELOGIC_PANEX_LOG in the original): the
/// line it printed, without its newline.
using PanexLevelLog = std::function<void(const std::string&)>;

/// The settings of one Panex search, and how it went.
struct PanexSearch {
  /// Budget half the physical memory, not 2 GB.
  bool highMemory = false;
  /// The memory the search may fill, in bytes; 0 for the budget highMemory
  /// says (sliding::memoryBudget). Set, highMemory plays no part.
  std::uint64_t memoryBytes = 0;
  /// Polled as the search runs; a stop request ends it (Stopped), saved
  /// when it has a folder.
  StopToken stop;
  /// When set, kept up to date with the stackings found so far.
  std::atomic<std::uint64_t>* progress = nullptr;
  /// When set, kept up to date with how many moves both ends have searched
  /// together.
  std::atomic<std::uint64_t>* depth = nullptr;
  /// When set, kept up to date with how many moves of the path are traced
  /// back, once the ends meet.
  std::atomic<std::uint64_t>* traced = nullptr;
  /// Carry on this problem's saved search, if there is one; otherwise start
  /// afresh.
  bool resume = false;
  /// Where searches are saved: a folder per puzzle in here. Empty: nothing
  /// is saved (the search still solves, by halves once its levels outgrow
  /// memory, but cannot be carried on).
  std::string workDir;
  /// Disk the saved levels may fill, in bytes; 0 for half the free space,
  /// at most 200 GB. Either way it is held to what the disk has room for
  /// (diskBudgetFor in base/system.h). The fuller it gets, the fewer levels
  /// are kept, and the longer tracing the path back takes.
  std::uint64_t diskBudget = 0;
  /// Save the search this often, the first time this long into the run, so
  /// a crash loses no more. 0 saves only when the search stops.
  unsigned int autosaveMinutes = 20;
  /// For the tests: stop once both ends together are this many moves deep.
  std::uint64_t stopAtDepth = 0;
  /// The threads the levels are spread over, and the newest saved level
  /// written beside them (the original wrote it on a thread of its own):
  /// null for a pool of the search's own of `threads` + 1 threads.
  ThreadPool* pool = nullptr;
  /// Threads a level is spread over: at most this many of the pool's; 0
  /// for all of the pool's but one, or with no pool, every hardware thread.
  unsigned int threads = 0;
  /// Take rod sets without Panex columns too (unsupported with anyRules).
  bool anyRules = false;
  /// Stackings a search may keep for tracing its path back before it solves
  /// each half instead; 0 for a quarter of the memory budget. The tests set
  /// it low to try the halving on small puzzles.
  std::uint64_t keepLimit = 0;
  /// The classic tower (three plain rods, the size rule, one tower moved)
  /// is answered by its recursion without a search; false searches it too
  /// (an A/B switch).
  bool towerRule = true;
  /// Told each level the search finishes; empty for none.
  PanexLevelLog onLevel;

  /// Out: how the search ended, and what it took.
  Outcome outcome = Outcome::NoPath;
  std::string error;
  std::uint64_t found = 0;
  std::uint64_t peakMemory = 0;
  /// The search was saved when it stopped: a search with `resume` carries
  /// it on.
  bool saved = false;
  /// Disk the saved levels take, and how many levels apart they are.
  std::uint64_t diskBytes = 0;
  unsigned int levelInterval = 1;
  /// Moves both ends had searched when the search ended.
  std::uint64_t depthReached = 0;
};

/// A shortest sequence of rod transfers from the start stacking of
/// `problem` to its goal, as stacking::pathSeparation's frames, as
/// findStackPath would give but for far larger Panex puzzles. A search
/// that stops, by its token or for want of memory, is saved for resume
/// when it has a folder. None when there is no path or the search stopped;
/// search.outcome says which.
[[nodiscard]] std::optional<Separation> solve(const Puzzle& puzzle, const Problem& problem,
                                              PanexSearch& search);

}  // namespace bt::panex
