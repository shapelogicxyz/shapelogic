// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The run layer the assemblers share (decision 0009): one run of a search,
/// its stop, the hand-over of what it finds, the progress of a search split
/// into tasks, and the loop that searches those tasks on several threads.
/// Ported from what assembler_0_c and assembler_1_c each had (their
/// parallelMultiSearch, reportParallelSolution, and the progress members of
/// assembler_c), written once.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/model/assembly.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace bt {

/// Receives each assembly a search finds; false asks the search to stop
/// (it can be continued later). Never called by two threads at once.
using AssemblyCallback = std::function<bool(Assembly)>;

/// One run of a search: a call of Assembler::assemble(), from its start
/// until the search is through or stopped.
///
/// The run polls one token: its own source's, which is a child of the
/// caller's. So a stop from the caller, made before or during the run, is
/// always seen; and the run can end itself without touching the caller's
/// source: a callback that answers false stops it as a pause, a worker that
/// throws stops it as an abort. Nothing is ever reset: the next run is a new
/// SearchRun with a new source.
class SearchRun {
 public:
  SearchRun(const StopToken& caller, const AssemblyCallback& callback)
      : source_(caller), token_(source_.token()), callback_(callback) {}

  SearchRun(const SearchRun&) = delete;
  SearchRun& operator=(const SearchRun&) = delete;

  /// The token the search polls.
  [[nodiscard]] const StopToken& token() const noexcept { return token_; }
  [[nodiscard]] bool stopRequested() const noexcept { return token_.stopRequested(); }
  /// Stops this run (and not the caller's source).
  void stop(StopKind kind) const noexcept { source_.request(kind); }

  /// False when nobody listens (the assembler debugger steps without one).
  [[nodiscard]] bool hasCallback() const noexcept { return static_cast<bool>(callback_); }

  /// Hands an assembly to the callback; an answer of false stops the run
  /// (a pause), an exception stops it (an abort) and is passed on.
  void deliver(Assembly assembly);

  /// Hands an assembly found by a search split over threads to the callback
  /// once: under the hand-over lock it is dropped when the run is stopped
  /// (the next run finds it again) or when `signature` is in `reported` (a
  /// task searched again after a stop finds again what it reported before);
  /// otherwise the signature is recorded and the assembly handed over.
  void deliverOnce(Assembly assembly, std::uint64_t signature,
                   std::unordered_set<std::uint64_t>& reported);

 private:
  /// Calls the callback (the lock held) and stops the run as it answers.
  void handOver(Assembly assembly);

  StopSource source_;
  StopToken token_;
  const AssemblyCallback& callback_;
  std::mutex mutex_;
};

/// FNV-1a: the signatures of reported assemblies are built with it, one
/// value at a time (the original's formula; saved positions carry them).
inline constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
[[nodiscard]] constexpr std::uint64_t fnvMix(std::uint64_t h, std::uint64_t v) noexcept {
  return (h ^ v) * kFnvPrime;
}

/// How far a search split into tasks is, readable from any thread.
///
/// The tasks are not of one size, so each has a weight (an estimate of its
/// part of the whole; all alike when nothing better is known), held as
/// whole units so that threads can add them up without a lock. The units of
/// finished tasks go into one sum; each worker slot holds the units it
/// reckons it has done of the task it is in.
class TaskProgress {
 public:
  /// Sets up a run over tasks with these weights (all 1 when the sizes
  /// differ or the weights add up to nothing); completed[i] nonzero marks
  /// the ones an earlier run finished. Call before the workers start.
  void begin(const std::vector<double>& weights, const std::vector<std::uint8_t>& completed);

  /// A worker's report: the fraction (0..1) done of the task it is in.
  void report(unsigned int slot, std::size_t task, float fraction) noexcept {
    running_[slot].store(static_cast<std::uint64_t>(static_cast<double>(units_[task]) * static_cast<double>(fraction)),
                         std::memory_order_relaxed);
  }

  /// A worker finished a task.
  void finish(unsigned int slot, std::size_t task) noexcept {
    done_.fetch_add(units_[task], std::memory_order_relaxed);
    running_[slot].store(0, std::memory_order_relaxed);
    completedTasks_.fetch_add(1, std::memory_order_relaxed);
  }

  /// True while the search reports by tasks (begin() was called and not
  /// reset() since).
  [[nodiscard]] bool active() const noexcept {
    return totalTasks_.load(std::memory_order_relaxed) > 0;
  }

  /// 0..1 over all tasks; 1 only when every task is finished.
  [[nodiscard]] float fraction() const noexcept;

  /// Back to "not reporting by tasks", so that a later search of another
  /// kind does not show the fraction of this one.
  void reset() noexcept {
    totalTasks_.store(0, std::memory_order_relaxed);
    completedTasks_.store(0, std::memory_order_relaxed);
  }

  /// Every task finished (a search with nothing left to split).
  void markAllDone() noexcept {
    totalTasks_.store(1, std::memory_order_relaxed);
    completedTasks_.store(1, std::memory_order_relaxed);
  }

 private:
  static constexpr std::uint64_t kUnits = std::uint64_t{1} << 40;

  std::vector<std::uint64_t> units_;
  std::atomic<std::size_t> totalTasks_{0};
  std::atomic<std::size_t> completedTasks_{0};
  std::atomic<std::uint64_t> total_{0};
  std::atomic<std::uint64_t> done_{0};
  std::array<std::atomic<std::uint64_t>, kMaxThreads> running_{};
};

/// Searches the tasks that are not completed, on up to `workers` threads:
/// the caller's and those `pool` lends (all on the caller when there is no
/// pool). Each thread calls `makeWorker(slot)` once and then the callable it
/// returns with the index of every task it takes. A task is marked completed
/// (and its progress finished) only when its search returned while the run
/// was not stopped: a task cut short by a stop is searched again by the next
/// run. The first exception from any thread stops the others (an abort) and
/// is rethrown here once all are through.
template <class MakeWorker>
void runTasks(std::vector<std::uint8_t>& completed, unsigned int workers, ThreadPool* pool,
              SearchRun& run, TaskProgress& progress, MakeWorker&& makeWorker) {
  std::vector<std::size_t> remaining;
  remaining.reserve(completed.size());
  for (std::size_t i = 0; i < completed.size(); i++) {
    if (!completed[i]) {
      remaining.push_back(i);
    }
  }

  std::atomic<std::size_t> next{0};
  auto job = [&](unsigned int slot) {
    try {
      auto worker = makeWorker(slot);
      while (!run.stopRequested()) {
        const std::size_t idx = next.fetch_add(1, std::memory_order_relaxed);
        if (idx >= remaining.size()) {
          break;
        }
        const std::size_t task = remaining[idx];
        worker(task);
        if (!run.stopRequested()) {
          completed[task] = 1;
          progress.finish(slot, task);
        }
      }
    } catch (...) {
      run.stop(StopKind::Abort);
      throw;
    }
  };

  if (pool != nullptr && workers > 1) {
    (void)pool->run(workers, job);
  } else {
    job(0);
  }
}

/// The thread count a search runs with: `threads` when set (at most
/// kMaxThreads), else as many as the pool has, else 1.
[[nodiscard]] inline unsigned int effectiveThreads(unsigned int threads,
                                                   const ThreadPool* pool) noexcept {
  if (threads > 0) {
    return threads < kMaxThreads ? threads : kMaxThreads;
  }
  return pool != nullptr ? pool->cores() : 1;
}

}  // namespace bt
