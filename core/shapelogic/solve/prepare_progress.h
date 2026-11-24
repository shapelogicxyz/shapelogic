// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// How far an assembler's preparation is (createMatrix() and reduce()), for
/// progress displays. The original told only the piece it was at ("optimize
/// piece 4 of 9"); on a large board one piece of the reduce can take many
/// minutes, so these counts move with every placement checked.

#include <atomic>
#include <chrono>
#include <cstdint>

namespace bt {

/// A copy of PrepareCounters, read at one time.
struct PrepareProgress {
  /// The piece (part, for assembler 1) at work, from 0.
  unsigned int piece = 0;
  /// reduce(): 0 while it sweeps the columns (a voxel every placement of a
  /// column fills is no other column's), then the placement check's pass,
  /// from 1. The check goes round again as long as its last pass removed
  /// something, so how many passes there will be is not known.
  unsigned int pass = 0;
  /// reduce(): what this pass has checked (columns in pass 0, placements
  /// after), of how many. `total` can shrink as placements go.
  std::uint64_t done = 0;
  std::uint64_t total = 0;
  /// createMatrix(): the placements found so far; reduce(): the placements
  /// left.
  std::uint64_t placements = 0;
  /// reduce(): the placements it has removed so far.
  std::uint64_t removed = 0;
  /// reduce(): how long the pass under way has run, in milliseconds.
  std::uint64_t passMs = 0;
  /// reduce(): the seconds the pass under way has still to go, from its
  /// pace so far; negative until that pace says anything (the first few
  /// seconds of a pass). Rough: some columns and placements cost more than
  /// others, and the passes after this one are not counted.
  double passSecondsLeft = -1;
};

/// Written by the assembler's thread as it prepares, read by any thread
/// (Assembler::prepareProgress()).
struct PrepareCounters {
  /// How long a pass runs before its pace is taken for a time left.
  static constexpr std::uint64_t kPaceMs = 3000;

  std::atomic<unsigned int> piece{0};
  std::atomic<unsigned int> pass{0};
  std::atomic<std::uint64_t> done{0};
  std::atomic<std::uint64_t> total{0};
  std::atomic<std::uint64_t> placements{0};
  std::atomic<std::uint64_t> removed{0};
  /// steady_clock milliseconds when the pass under way began.
  std::atomic<std::int64_t> passSince{0};

  static std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  /// A new pass of reduce() begins, `total` to check.
  void beginPass(unsigned int p, std::uint64_t t) {
    done.store(0, std::memory_order_relaxed);
    total.store(t, std::memory_order_relaxed);
    pass.store(p, std::memory_order_relaxed);
    passSince.store(nowMs(), std::memory_order_relaxed);
  }

  [[nodiscard]] PrepareProgress read() const {
    PrepareProgress p;
    p.piece = piece.load(std::memory_order_relaxed);
    p.pass = pass.load(std::memory_order_relaxed);
    p.done = done.load(std::memory_order_relaxed);
    p.total = total.load(std::memory_order_relaxed);
    p.placements = placements.load(std::memory_order_relaxed);
    p.removed = removed.load(std::memory_order_relaxed);
    if (const std::int64_t since = passSince.load(std::memory_order_relaxed); since != 0) {
      const std::int64_t ms = nowMs() - since;
      p.passMs = ms > 0 ? static_cast<std::uint64_t>(ms) : 0;
    }
    if (p.done > 0 && p.done < p.total && p.passMs >= kPaceMs) {
      p.passSecondsLeft = static_cast<double>(p.passMs) / 1000.0 *
                          static_cast<double>(p.total - p.done) / static_cast<double>(p.done);
    }
    return p;
  }
};

}  // namespace bt
