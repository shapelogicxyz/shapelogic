// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The Stacking Solver's own search: the fewest rod transfers from a
/// stacking problem's start to its goal, breadth first over every stacking
/// reachable, held in memory up to a budget. The solve runs the Panex
/// search (bt/solve/panex_search.h) instead whenever that one can hold the
/// puzzle; this one takes the rest (more rods or discs than the Panex
/// search packs into its keys).
///
/// Ported from src/lib/stacking.{h,cpp} (findStackPath, stackBfs,
/// stackSearch_c, stackOutcome_e).

#include "shapelogic/base/stop.h"
#include "shapelogic/model/disassembly.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace bt {
class Problem;
class Puzzle;
}  // namespace bt

namespace bt::stacking {

/// How a stacking search ended.
enum class StackOutcome : std::uint8_t {
  Found,    ///< a path to the goal
  NoPath,   ///< every reachable stacking was searched: there is no path
  Memory,   ///< stopped at its memory limit; a path may still exist
  Stopped,  ///< the stop token asked it to
};

/// The settings of one stacking search, and how it went.
struct StackSearch {
  /// Stackings to hold before giving up; 0 for as many as fit the memory
  /// budget (sliding::memoryStates).
  std::uint64_t maxStates = 0;
  /// With maxStates 0, budget half the physical memory, not 2 GB.
  bool highMemory = false;
  /// With maxStates 0, the memory the stackings may fill, in bytes; 0 for
  /// the budget highMemory says. Set, highMemory plays no part.
  std::uint64_t memoryBytes = 0;
  /// Polled as the search runs; a stop request ends it (Stopped).
  StopToken stop;
  /// When set, kept up to date with the stackings visited so far.
  std::atomic<std::uint64_t>* progress = nullptr;

  /// Out: how the search ended, the stackings visited, and the limit used.
  StackOutcome outcome = StackOutcome::NoPath;
  std::uint64_t visited = 0;
  std::uint64_t memoryStates = 0;
};

/// The shortest sequence of rod transfers from the start stacking of
/// `problem` to its goal, as stacking::pathSeparation's frames (three a
/// transfer). None when the problem is not ready (setupError), there is no
/// path, or the search stopped; search.outcome says which.
[[nodiscard]] std::optional<Separation> findStackPath(const Puzzle& puzzle, const Problem& problem,
                                                      StackSearch& search);

/// findStackPath with a limit on the stackings held (0: the memory budget).
[[nodiscard]] std::optional<Separation> findStackPath(const Puzzle& puzzle, const Problem& problem,
                                                      std::uint64_t maxStates = 0);

/// The two stacking solvers (the GUI's Solver Type for a stacking puzzle,
/// burrTxt's --solver).
enum class StackingSolver : std::uint8_t {
  /// "Stacking Solver": the fewest transfers; it runs on the Panex search
  /// when the puzzle fits that one, else on findStackPath.
  Shortest,
  /// "Panex Solver": the Panex search alone (Panex columns only).
  Panex,
};

/// What a stacking solve asks of its search (the solve thread's stacking
/// flags: PAR_PANEX_SOLVER, PAR_HIGH_MEMORY, PAR_PANEX_NO_AUTOSAVE), and
/// where a Panex search is saved.
struct StackingOptions {
  StackingSolver solver = StackingSolver::Shortest;
  bool highMemory = false;
  /// StackSearch::memoryBytes and panex::PanexSearch::memoryBytes: set by
  /// the program (the engine from its settings; the CLI leaves it to
  /// highMemory).
  std::uint64_t memoryBytes = 0;
  /// The Panex search saves itself every 20 minutes; false saves it only
  /// when it stops (the GUI's Autosave box off).
  bool autosave = true;
  /// panex::PanexSearch::workDir and diskBudget: set by the program (the
  /// CLI from --panex-dir, SHAPELOGIC_PANEX_DIR or the user's cache folder,
  /// the engine from its settings). Empty: nothing is saved.
  std::string workDir;
  std::uint64_t diskBudget = 0;
  /// The A/B switch panex::PanexSearch::towerRule.
  bool towerRule = true;
};

}  // namespace bt::stacking
