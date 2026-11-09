// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The run layer the assemblers share (decision 0009): one run of a search,
/// its stop, the hand-over of what it finds, the progress of a search split
/// into tasks, and the loop that searches those tasks on several threads.
/// Ported from what assembler_0_c and assembler_1_c each had (their
/// parallelMultiSearch, reportParallelSolution, and the progress members of
/// assembler_c), written once.

#include "shapelogic/base/stop.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/resume_state.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <utility>
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
  /// the ones an earlier run finished, and `doneWeight` is the weight of
  /// finished tasks no longer in the list. Call before the workers start.
  void begin(const std::vector<double>& weights, const std::vector<std::uint8_t>& completed,
             double doneWeight = 0);

  /// The units of each task begin() was given, and of the whole.
  [[nodiscard]] const std::vector<std::uint64_t>& units() const noexcept { return units_; }
  static constexpr std::uint64_t kUnits = std::uint64_t{1} << 40;

  /// A worker's report on a task of `units` units (one whose units are kept
  /// elsewhere: SplitTasks): the fraction (0..1) done of it.
  void reportUnits(unsigned int slot, std::uint64_t units, float fraction) noexcept {
    const double done = static_cast<double>(units) * static_cast<double>(fraction);
    running_[slot].store(static_cast<std::uint64_t>(done), std::memory_order_relaxed);
  }

  /// A worker finished such a task.
  void finishUnits(unsigned int slot, std::uint64_t units) noexcept {
    done_.fetch_add(units, std::memory_order_relaxed);
    running_[slot].store(0, std::memory_order_relaxed);
    completedTasks_.fetch_add(1, std::memory_order_relaxed);
  }

  /// A worker's task was split: `added` tasks more (and `finished` of the
  /// old ones stood for by them), `done` units of it are through for good,
  /// and the worker now stands at `running` units of what it goes on with.
  void split(unsigned int slot, std::size_t added, std::size_t finished, std::uint64_t done,
             std::uint64_t running) noexcept {
    totalTasks_.fetch_add(added, std::memory_order_relaxed);
    completedTasks_.fetch_add(finished, std::memory_order_relaxed);
    done_.fetch_add(done, std::memory_order_relaxed);
    running_[slot].store(running, std::memory_order_relaxed);
  }

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
  std::vector<std::uint64_t> units_;
  std::atomic<std::size_t> totalTasks_{0};
  std::atomic<std::size_t> completedTasks_{0};
  std::atomic<std::uint64_t> total_{0};
  std::atomic<std::uint64_t> done_{0};
  std::array<std::atomic<std::uint64_t>, kMaxThreads> running_{};
};

/// What a search hands on of the task it is in (SplitTasks::handOff, leave).
/// The shares are parts of that task as it stood, every choice of a level
/// taken as alike (the measure the progress of a task uses); what they leave
/// of the whole is the part the search has finished.
template <class Task>
struct TaskSplit {
  /// Branches the search has not begun, as tasks.
  std::vector<Task> branches;
  std::vector<double> shares;
  /// A hand-off: what the worker goes on with, its share, and how far it is
  /// in that (0..1).
  Task rest;
  double restShare = 0;
  float restFraction = 0;
};

/// The tasks of a split search while a run searches them. The list grows:
/// a worker whose task still has untried branches hands them on as new
/// tasks when another worker has nothing to do (handOff), and when the run
/// is stopped it replaces its task by what is left of it (leave), so that
/// the next run searches only that. Workers with nothing to do wait here
/// for a hand-off; the run is through when nothing is queued and nobody
/// searches.
///
/// The state is the assembler's SplitSearchState, changed only under this
/// object's lock while a run is on; a worker gets a copy of its task.
template <class Task>
class SplitTasks {
 public:
  SplitTasks(SplitSearchState<Task>& state, SearchRun& run, TaskProgress& progress)
      : state_(state), run_(run), progress_(progress), units_(progress.units()) {
    BT_ASSERT(units_.size() == state_.tasks.size());
    for (std::size_t i = 0; i < state_.completed.size(); i++) {
      if (!state_.completed[i]) {
        queue_.push_back(i);
      }
    }
    queued_.store(queue_.size(), std::memory_order_relaxed);
  }

  SplitTasks(const SplitTasks&) = delete;
  SplitTasks& operator=(const SplitTasks&) = delete;

  /// The next task for a worker: a copy of it, its index and its units.
  /// Waits while none is queued and another worker may still hand some on.
  /// False when the search is through or the run stopped.
  bool take(std::size_t& index, Task& task, std::uint64_t& units) {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      if (run_.stopRequested()) {
        return false;
      }
      if (head_ < queue_.size()) {
        index = queue_[head_++];
        queued_.store(queue_.size() - head_, std::memory_order_relaxed);
        active_++;
        task = state_.tasks[index];
        units = units_[index];
        return true;
      }
      if (active_ == 0) {
        return false;
      }
      // a stop has no way to wake a waiting thread: look again shortly
      hungry_.fetch_add(1, std::memory_order_relaxed);
      changed_.wait_for(lock, std::chrono::milliseconds(2));
      hungry_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  /// The worker in `slot` searched task `index` to its end.
  void finish(unsigned int slot, std::size_t index) {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.completed[index] = 1;
    progress_.finishUnits(slot, units_[index]);
    released();
  }

  /// The worker left task `index` unfinished (a stop).
  void release() {
    const std::lock_guard<std::mutex> lock(mutex_);
    released();
  }

  /// True when a worker waits and nothing is queued: a search polls this
  /// (it costs two loads) and hands branches on when it has some.
  [[nodiscard]] bool wanted() const noexcept {
    return hungry_.load(std::memory_order_relaxed) > 0 &&
           queued_.load(std::memory_order_relaxed) == 0;
  }

  /// The worker in `slot` hands branches of task `index` on and goes on
  /// with `split.rest`, which the task now is. Returns the units of that.
  std::uint64_t handOff(unsigned int slot, std::size_t index, TaskSplit<Task>&& split) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t whole = units_[index];
    std::uint64_t left = whole;
    const std::size_t added = append(split, whole, left, true);
    const std::uint64_t rest = share(whole, split.restShare, left);
    left -= rest;
    state_.tasks[index] = std::move(split.rest);
    units_[index] = rest;
    progress_.split(slot, added, 0, left,
                    static_cast<std::uint64_t>(static_cast<double>(rest) *
                                               static_cast<double>(split.restFraction)));
    changed_.notify_all();
    return rest;
  }

  /// The run is stopped: what is left of task `index` is `split.branches`
  /// (nothing of them begun), which the next run searches in its place.
  void leave(unsigned int slot, std::size_t index, TaskSplit<Task>&& split) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t whole = units_[index];
    std::uint64_t left = whole;
    const std::size_t added = append(split, whole, left, false);
    state_.completed[index] = 1;
    progress_.split(slot, added, 1, left, 0);
  }

  /// Wakes the waiting workers (a worker that ends with an exception).
  void wake() { changed_.notify_all(); }

  /// Writes the tasks' units back as the state's weights; call when the
  /// workers are through.
  void storeWeights() {
    state_.weights.resize(units_.size());
    for (std::size_t i = 0; i < units_.size(); i++) {
      state_.weights[i] =
          static_cast<double>(units_[i]) / static_cast<double>(TaskProgress::kUnits);
    }
  }

 private:
  /// `part` of `whole` units, at least one and at most what is `left`.
  static std::uint64_t share(std::uint64_t whole, double part, std::uint64_t left) {
    auto u = static_cast<std::uint64_t>(static_cast<double>(whole) * part);
    if (u < 1) {
      u = 1;
    }
    return u < left ? u : left;
  }

  /// Adds the branches as tasks, queued or not; takes their units off `left`.
  std::size_t append(TaskSplit<Task>& split, std::uint64_t whole, std::uint64_t& left,
                     bool queue) {
    for (std::size_t i = 0; i < split.branches.size(); i++) {
      const std::uint64_t u = share(whole, split.shares[i], left);
      left -= u;
      if (queue) {
        queue_.push_back(state_.tasks.size());
      }
      state_.tasks.push_back(std::move(split.branches[i]));
      state_.completed.push_back(0);
      units_.push_back(u);
    }
    queued_.store(queue_.size() - head_, std::memory_order_relaxed);
    return split.branches.size();
  }

  void released() {
    active_--;
    if (active_ == 0) {
      changed_.notify_all();
    }
  }

  SplitSearchState<Task>& state_;
  SearchRun& run_;
  TaskProgress& progress_;

  std::mutex mutex_;
  std::condition_variable changed_;
  /// The units of each task (TaskProgress's, kept here once tasks split).
  std::vector<std::uint64_t> units_;
  /// The tasks not begun, from head_ on.
  std::vector<std::size_t> queue_;
  std::size_t head_ = 0;
  /// Workers that have a task.
  unsigned int active_ = 0;
  std::atomic<unsigned int> hungry_{0};
  std::atomic<std::size_t> queued_{0};
};

/// Searches the tasks of `tasks` on up to `workers` threads: the caller's
/// and those `pool` lends (all on the caller when there is no pool). Each
/// thread calls `makeWorker(slot)` once and then the callable it returns
/// with every task it takes: (index, task, units), answering true when it
/// searched the task to its end (it is then marked finished) and false when
/// a stop cut it short (it has then told the list what is left of the task,
/// or the task is searched again by the next run). The first exception from
/// any thread stops the others (an abort) and is rethrown here once all are
/// through.
template <class Task, class MakeWorker>
void runSplitTasks(SplitTasks<Task>& tasks, unsigned int workers, ThreadPool* pool,
                   SearchRun& run, MakeWorker&& makeWorker) {
  auto job = [&](unsigned int slot) {
    try {
      auto worker = makeWorker(slot);
      std::size_t index = 0;
      Task task;
      std::uint64_t units = 0;
      while (tasks.take(index, task, units)) {
        if (worker(index, task, units)) {
          tasks.finish(slot, index);
        } else {
          tasks.release();
        }
      }
    } catch (...) {
      run.stop(StopKind::Abort);
      tasks.wake();
      throw;
    }
  };

  if (pool != nullptr && workers > 1) {
    (void)pool->run(workers, job);
  } else {
    job(0);
  }
  tasks.storeWeights();
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
