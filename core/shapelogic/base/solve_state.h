// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The states a solve goes through, and the only moves allowed between them.
///
/// One writer at a time: the run's worker while it runs, the owner (under the
/// session's control lock) before the run's thread starts and after it is
/// joined. Other threads (the engine, a UI) read it and ask for a stop
/// through a StopSource; they never write the state. "Stopping…" is shown by
/// combining an active state with a requested stop, not by a state of its
/// own. See
/// docs/decisions/0004-cancellation-and-solve-state.md.

#include <atomic>
#include <cstdint>
#include <string_view>

namespace sl {

enum class SolveState : std::uint8_t {
  Idle,         ///< Nothing searched yet, or the results were thrown away.
  Preparing,    ///< Building the search structures.
  Assembling,   ///< Searching for assemblies (take-apart work may run alongside).
  Draining,     ///< Assembly search done; finishing the queued take-apart work.
  Paused,       ///< Stopped where it can be continued.
  Finished,     ///< Every assembly found and handled.
  Error,        ///< Stopped by an error; see the run's error report.
};

[[nodiscard]] std::string_view toString(SolveState s) noexcept;

/// True while a worker is running (Preparing, Assembling, Draining).
[[nodiscard]] bool isActive(SolveState s) noexcept;

/// The transition table. Everything not listed here is a bug:
///   Idle      -> Preparing
///   Preparing -> Assembling, Paused, Error, Idle
///   Assembling-> Draining, Paused, Finished, Error, Idle
///   Draining  -> Finished, Paused, Error, Idle
///   Paused    -> Preparing, Assembling, Idle
///   Finished  -> Idle
///   Error     -> Idle
/// (X -> Idle from an active state is an abort; from Paused, Finished or Error
/// it is "remove all results".)
[[nodiscard]] bool canTransition(SolveState from, SolveState to) noexcept;

/// Holds a run's state. One writer at a time (the run's worker while it
/// runs, the owner under its control lock before and after), read by any
/// thread.
class SolveStateCell {
 public:
  [[nodiscard]] SolveState load() const noexcept { return state_.load(std::memory_order_acquire); }

  /// Move to `to`. Asserts (SL_ASSERT) that the move is in the table, and
  /// that no other thread moved the state meanwhile (a second writer).
  void advance(SolveState to);

 private:
  std::atomic<SolveState> state_{SolveState::Idle};
};

}  // namespace sl
