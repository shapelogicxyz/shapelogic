// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Where a solve stands, from start to end: one picture for a front end, the
/// command line tools and anything else that shows progress, and the counts
/// and times of a debug statistics panel. Both are made by SolveSession
/// (progress(), stats()) and are plain values.
///
/// Ported from src/lib/solveprogress.h (solveProgress_c, activity()) and
/// solveStats_c of src/lib/solvethread.h.

#include "bt/base/solve_state.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solver_type.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bt {

/// What a solve is doing, finer than SolveState (solveProgress_c::Stage).
enum class SolveStage : std::uint8_t {
  Idle,         ///< nothing has run yet, or the results were thrown away
  Prepare,      ///< finding the placements of each piece
  Reduce,       ///< removing placements that cannot be part of an assembly
  Assemble,     ///< searching for assemblies (taking them apart alongside)
  Disassemble,  ///< all assemblies found, the rest are being taken apart
  Stopping,     ///< asked to pause or abort, winding down
  Paused,
  Done,
  Error,
  /// A search that reports in its own way (STAGE_OTHER): the slide search
  /// of a sliding puzzle; see SolveProgress::searched.
  Other,
};

/// A picture of a solve (solveProgress_c).
struct SolveProgress {
  SolveStage stage = SolveStage::Idle;
  /// Milliseconds since this run started.
  std::uint64_t elapsedMs = 0;

  /// Preparing and reducing: the piece at work (from 1) of how many.
  unsigned int piece = 0;
  unsigned int pieces = 0;

  /// The assembly search: how far (0..1), search steps, threads.
  float assemblyFraction = 0;
  std::uint64_t iterations = 0;
  unsigned int assemblerThreads = 1;

  std::uint64_t assemblies = 0;
  std::uint64_t solutions = 0;

  /// The slide search under way: the arrangements it has visited, and how
  /// many moves deep a full search is (0 before its first level). What the
  /// original GUI showed ("Sliding search, 12 moves deep. Scanned 3,456
  /// arrangements.").
  std::uint64_t searched = 0;
  std::uint64_t searchDepth = 0;

  /// Taking apart: is it wanted, how many done, how many found and not
  /// done, by how many workers.
  bool disassembly = false;
  unsigned int disasmCompleted = 0;
  unsigned int disasmPending = 0;
  unsigned int disasmWorkers = 0;
  /// The take-aparts under way, the longest running first.
  std::vector<DisassemblyProgress> running;

  /// The whole solve, 0..1; it does not go back during a run. An estimate
  /// while the assembly search runs. When all that is left is take-aparts
  /// of unknown length, overallKnown is false and the value only says how
  /// many of them are through.
  float overall = 0;
  bool overallKnown = true;
  /// The level at hand of the longest running take-apart, 0..1.
  float levelFraction = 0;

  /// Seconds still to go; negative when that cannot be told.
  double secondsLeft = -1;

  /// One line saying what is going on, for a status line (the original's
  /// text, word for word).
  [[nodiscard]] std::string activity() const;
};

/// The counts and times of a solve, for a debug statistics panel
/// (solveStats_c). Times are milliseconds of the current or last run.
struct SolveStats {
  SolveState state = SolveState::Idle;
  SolverType solver = SolverType::Classic;
  bool disassemblyEnabled = false;
  bool rotationsEnabled = false;
  unsigned int hardwareThreads = 0;
  unsigned int assemblerThreads = 0;
  unsigned int disasmWorkers = 0;
  std::uint64_t assembliesFound = 0;
  std::uint64_t solutionsFound = 0;
  /// Assemblies taken apart that do not come apart.
  std::uint64_t inseparable = 0;
  /// Search steps of the assembler.
  std::uint64_t iterations = 0;
  float assemblyProgress = 0;
  /// Assemblies waiting to be taken apart, and the most there were.
  unsigned int pending = 0;
  unsigned int peakPending = 0;
  /// Take-aparts finished.
  unsigned int disasmCompleted = 0;
  std::uint64_t elapsedMs = 0;
  std::uint64_t prepareMs = 0;
  std::uint64_t reduceMs = 0;
  std::uint64_t assemblyMs = 0;
  /// Time spent taking apart, summed over the workers.
  std::uint64_t disasmWorkMs = 0;
  std::uint64_t linearSearchMs = 0;
  std::uint64_t rotationSearchMs = 0;
  /// Taking apart what was left after the assembly search was through.
  std::uint64_t drainMs = 0;
  float avgDisasmSeconds = 0;
  /// What each take-apart under way is doing.
  std::vector<DisassemblyProgress> running;
};

}  // namespace bt
