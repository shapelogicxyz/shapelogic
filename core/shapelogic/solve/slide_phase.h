// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The parts of a move of the slide search that visit no arrangement
/// (SlidePhaseProgress in sliding_search.h, SolveProgress::slidePhase).

#include <cstdint>

namespace sl::sliding {

/// What the level search does in a move when it is not expanding it: the
/// parts of a move that visit no arrangement, so that neither the count of
/// those visited nor the live preview moves while they run, deep in a big
/// search for many minutes (SlidePhaseProgress).
enum class SlidePhase : std::uint8_t {
  /// Expanding the move, or between moves: nothing more to tell.
  None,
  /// The move's arrangements all expanded, the search waits for the older
  /// levels to go into the visited sets (the merge that runs beside the
  /// expansion, on one thread; with nested slides).
  Visited,
  /// What the move reached merged into the next level, less the levels
  /// before and what is in the visited sets, and written.
  Merging,
  /// Sets in memory written to the disk to leave room for the next level.
  Writing,
  /// The search saved, so that a crash loses no more than this (autosave).
  Saving,
  /// A move of the level reaches the goal: the level is gone through again
  /// for the first arrangement that does, from which the path is read back
  /// (every run gives the same path).
  Goal,
};

}  // namespace sl::sliding
