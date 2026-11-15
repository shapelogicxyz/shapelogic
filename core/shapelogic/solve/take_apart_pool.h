// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Take-aparts side by side: the assemblies a search finds are independent
/// of each other, so each goes to one of several threads, every thread with
/// a disassembler of its own, and the results are handed on in the order
/// the assemblies came in.
///
/// After src/lib/disassemblerpool.{h,cpp} (its workers, bounded queue and
/// ordered merge). The results are handed on by the workers themselves, one
/// worker at a time, not by a merger thread; nothing is salvaged for a later
/// run (the solve session keeps its own queue for that).
///
/// A puzzle may have millions of assemblies that each take microseconds to
/// take apart, so handing one over must cost less than that: a worker takes
/// several at once when they are small (as many as take about a tenth of a
/// millisecond, by the time the last ones took), looks for more for a moment
/// before it sleeps, and a sleeping worker is woken only when those at work
/// will not get to what is queued soon.

#include "shapelogic/base/stop.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/disassembler.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace bt {

class Problem;
class Puzzle;

/// How a take-apart pool works.
struct TakeApartPoolOptions {
  /// The threads that take apart, at least 1 (clamped to kMaxThreads).
  unsigned int workers = 1;
  /// The most of them at work while the search runs beside them: with a
  /// thread limit, the search's threads and these are the limit together.
  /// When the take-aparts fall behind (the search waits for them in
  /// submit()) and once the search is over, all work. 0: no such limit.
  unsigned int besideSearch = 0;
  /// The search's threads, counted as busy cores in the take-apart's
  /// ThreadPool (takeApart.pool) while the search runs: a take-apart
  /// spreads a level only over cores nothing else is using.
  unsigned int searchCores = 0;
  /// Each worker's disassembler. Its pool, when set, must outlive this.
  DisassemblerOptions takeApart;
};

/// The take-aparts of one problem's assemblies, on several threads.
///
/// submit() is called from one thread at a time (a search hands over its
/// assemblies so), finish() once, after the last submit().
class TakeApartPool {
 public:
  /// What to do with a result: called in the order of the submit() calls,
  /// from one thread at a time (a worker's, or finish()'s caller never).
  using OnResult = std::function<void(const Assembly&, TakeApartResult&)>;

  /// A pool for `problem` of `puzzle` (both must outlive it, and not change;
  /// see Disassembler). `stop` ends the take-aparts under way as it ends
  /// one disassemble(); their results still come, as stopped.
  TakeApartPool(const Puzzle& puzzle, const Problem& problem, const TakeApartPoolOptions& options,
                OnResult onResult, const StopToken& stop = {});
  /// Ends the workers; assemblies not taken apart yet are dropped.
  ~TakeApartPool();

  TakeApartPool(const TakeApartPool&) = delete;
  TakeApartPool& operator=(const TakeApartPool&) = delete;

  /// Hands an assembly over. Waits while too many are unfinished: the
  /// queue, and what is taken apart but not handed on yet, stay small, and
  /// the cores go to the take-aparts.
  void submit(Assembly assembly);

  /// The search is over: waits until every result is handed on. The first
  /// exception of a take-apart or of the result function is rethrown here.
  void finish();

  /// The threads that take apart.
  [[nodiscard]] unsigned int workers() const noexcept {
    return static_cast<unsigned int>(disassemblers_.size());
  }

  /// The take-aparts under way; from any thread.
  [[nodiscard]] std::vector<DisassemblyProgress> progress() const;

  /// Assemblies handed over, and results handed on; from any thread.
  [[nodiscard]] std::uint64_t submitted() const;
  [[nodiscard]] std::uint64_t delivered() const;

 private:
  struct Task {
    std::uint64_t seq = 0;
    Assembly assembly;
  };
  struct Done {
    std::uint64_t seq = 0;
    Assembly assembly;
    TakeApartResult result;
  };

  void workerLoop(Disassembler& disassembler);
  /// Hands on what is next in line; `lock` holds mutex_ and is released
  /// around the result function.
  void deliver(std::unique_lock<std::mutex>& lock);
  /// The workers that may be at work now. Under mutex_.
  [[nodiscard]] unsigned int limit() const;
  /// The assemblies a worker takes at once. Under mutex_.
  [[nodiscard]] std::size_t batchSize() const;
  /// A sleeping worker should be woken for what is queued. Under mutex_.
  [[nodiscard]] bool wakeForQueue() const;
  /// A submit() that had to wait goes on when no more than this many
  /// assemblies are unfinished.
  [[nodiscard]] std::size_t lowWater() const noexcept { return window_ - window_ / 4; }
  /// Adds to the busy cores of the take-apart's ThreadPool, when there is one.
  void addLoad(int cores) const;
  void fail(const std::exception_ptr& error);

  TakeApartPoolOptions options_;
  OnResult onResult_;
  StopToken stop_;
  std::vector<std::unique_ptr<Disassembler>> disassemblers_;
  std::vector<std::thread> threads_;

  mutable std::mutex mutex_;
  /// Work for a sleeping worker, more workers may work, or the pool ends.
  std::condition_variable workCv_;
  /// Results were handed on, or the pool failed.
  std::condition_variable doneCv_;
  std::deque<Task> queue_;
  /// queue_'s size, for a worker that looks for work without the mutex.
  std::atomic<std::size_t> queued_{0};
  /// Taken apart, waiting for the ones before them: the result of assembly
  /// number s at [s % window_] (no two unfinished ones share a place).
  std::vector<std::optional<Done>> done_;
  /// The most assemblies handed over and not handed on.
  std::size_t window_ = 256;
  std::uint64_t nextSeq_ = 0;
  std::uint64_t delivered_ = 0;
  /// Workers taking apart, looking for work, and asleep.
  unsigned int working_ = 0;
  unsigned int looking_ = 0;
  unsigned int sleeping_ = 0;
  /// How long a take-apart took lately, in nanoseconds (a moving average;
  /// long until the first ones are through: one at a time then).
  double jobNs_ = 1e9;
  /// A worker is handing results on.
  bool delivering_ = false;
  /// submit() waits for results to be handed on.
  bool submitWaiting_ = false;
  /// The take-aparts fell behind the search (submit() had to wait); until
  /// the queue is nearly empty again.
  bool behind_ = false;
  /// finish() was called: no search beside the take-aparts any more.
  bool searchOver_ = false;
  /// The search's cores are counted in the ThreadPool's load.
  bool searchLoad_ = false;
  bool quit_ = false;
  /// quit_ for a worker that looks for work without the mutex.
  std::atomic<bool> quitFlag_{false};
  std::exception_ptr error_;
};

}  // namespace bt
