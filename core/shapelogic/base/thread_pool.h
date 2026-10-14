// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The threads of a solve: one pool, made by whoever owns the solve (the
/// CLI, later the solve session) with the solve's thread budget, and lent
/// out to the searches that can spread over several threads.
/// Ported from src/lib/helperpool.{h,cpp}; the original's process-wide
/// thread limit and SHAPELOGIC_THREADS are gone: the budget is the pool's
/// size, passed in explicitly (decision 0009).

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bt {

/// The most threads anything in the core runs at once. Thread counts come
/// from users and files; they are clamped to this before threads are made.
inline constexpr unsigned int kMaxThreads = 256;

/// The number of hardware threads, at least 1 and at most kMaxThreads.
[[nodiscard]] unsigned int hardwareThreads() noexcept;

/// How a budget of threads is shared while assembly search and take-apart
/// run side by side (solveThreadSplit in the original).
struct ThreadSplit {
  unsigned int assembly = 1;
  unsigned int disassembly = 1;
};

/// For a budget of n threads: a quarter (at least one) take apart, the rest
/// search; one each when n is below 2.
[[nodiscard]] ThreadSplit splitThreads(unsigned int n) noexcept;

/// Threads that are lent out for a piece of work and given back.
///
/// The pool is made for a number of cores, the callers included. run()
/// executes a job on the calling thread and on as many lent threads as
/// cores are free; work that keeps cores busy on threads of its own says so
/// with addLoad(), and the pool lends correspondingly fewer. Several callers
/// may use the pool at once, also from inside a job: a caller never waits
/// for a thread that was not lent to it, so nesting cannot deadlock.
class ThreadPool {
 public:
  /// A pool for `cores` threads working at once (clamped to 1..kMaxThreads);
  /// it starts cores - 1 threads of its own.
  explicit ThreadPool(unsigned int cores);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /// The number of threads that may work at once, callers included.
  [[nodiscard]] unsigned int cores() const noexcept { return total_; }

  /// `n` more cores (or, negative, fewer) are in use outside the pool.
  void addLoad(int n) noexcept { load_.fetch_add(n, std::memory_order_relaxed); }

  /// Runs job(0) on the calling thread and job(1), job(2), ... on lent
  /// threads, at most `maxThreads` in all, and returns when every one has
  /// returned. Returns the number of threads that ran the job, at least 1.
  /// An exception from any of them is rethrown here once all are through
  /// (the caller's own first).
  unsigned int run(unsigned int maxThreads, const std::function<void(unsigned int)>& job);

 private:
  void threadMain();

  unsigned int total_;
  std::vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  bool quit_ = false;
  unsigned int lent_ = 0;  ///< threads lent at the moment (guarded by m_)
  std::atomic<int> load_{0};
};

}  // namespace bt
