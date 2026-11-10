// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// When a long search saves itself as it runs, so that a crash loses little
/// (the crash recovery journal): often while a save is cheap, less often as
/// it gets dear.

#include <algorithm>
#include <chrono>

namespace bt {

/// The saves of one run. The first is due a floor after the run starts;
/// each next one when the time since the last is at least the floor and at
/// least `kCostFactor` times what the last save took, so that saving costs
/// the run no more than about 2% of its time. The front end's journal
/// (web/src/renderer/solver/autosave.ts) keeps to the same rule.
class SaveCadence {
 public:
  using Clock = std::chrono::steady_clock;

  /// Saving takes at most one part in this many of the run.
  static constexpr unsigned int kCostFactor = 50;
  /// Saves are never closer than this, unless a run asks for less.
  static constexpr std::chrono::seconds kFloor{60};

  explicit SaveCadence(Clock::duration floor = kFloor, Clock::time_point now = Clock::now())
      : floor_(floor), wait_(floor), last_(now) {}

  /// True when a save is due at `now`.
  [[nodiscard]] bool due(Clock::time_point now = Clock::now()) const {
    return now - last_ >= wait_;
  }

  /// A save that began at `began` has ended at `now`.
  void saved(Clock::time_point began, Clock::time_point now = Clock::now()) {
    wait_ = std::max(floor_, (now - began) * kCostFactor);
    last_ = now;
  }

  /// The wait before the next save, from the last one.
  [[nodiscard]] Clock::duration wait() const { return wait_; }

 private:
  Clock::duration floor_;
  Clock::duration wait_;
  Clock::time_point last_;
};

}  // namespace bt
