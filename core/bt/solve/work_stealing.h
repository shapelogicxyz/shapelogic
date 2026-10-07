// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The run layer's second driver (decision 0009): a search that is searched
/// a slice of steps at a time and can be split, spread over threads that
/// take work from each other. BurrTools 2 assembles with it.
/// Ported from bt2Assemble (src/lib/bt2_assemble.cpp); the threads come from
/// the solve's pool, the stop is the run's token, and the parked branches
/// and progress peers are the search's, behind the interface below.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/solve/search_run.h"

#include <atomic>
#include <condition_variable>
#include <concepts>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace bt {

/// What runWorkStealing() needs of a search whose branches are of type B.
/// All of it may be called from several threads at once, for different
/// branches.
template <class S, class B>
concept WorkStealingSearch = requires(S& s, B& b, const B& cb, SearchRun& run,
                                      std::unique_ptr<B> owned) {
  /// Searches up to `budget` steps of a branch (assembleLimited).
  s.searchSlice(b, run, 1U);
  /// True when a branch is through (searchFinished).
  { s.branchFinished(cb) } -> std::same_as<bool>;
  /// A new branch with part of what `b` has left, counted in the progress
  /// from now on; null when there is nothing to give (splitSearch).
  { s.splitBranch(b) } -> std::same_as<std::unique_ptr<B>>;
  /// A branch split off earlier is through: what it did stays counted
  /// (removeProgressPeer).
  s.retireBranch(b);
  /// The branches a stopped run left, to be taken up first; and giving one
  /// back when this run stops (takeParkedSearches, parkSearch).
  { s.takeParked() } -> std::same_as<std::vector<std::unique_ptr<B>>>;
  s.park(std::move(owned));
};

/// Searches `root` and the branches parked by an earlier run on up to
/// `workers` threads: the caller's and those `pool` lends. The caller's
/// thread starts with the root; the root is first split so that every
/// thread has a branch. Each thread searches its branch a slice at a time;
/// between two slices a busy thread splits off part of its branch for a
/// thread that waits. The run is over when every thread waits with nothing
/// left, or when it is stopped: then every branch not finished is parked
/// for the next run. The first exception from any thread stops the others
/// (an abort) and is rethrown here once all are through.
template <class B, WorkStealingSearch<B> S>
void runWorkStealing(S& search, B& root, unsigned int workers, ThreadPool* pool, SearchRun& run) {
  struct Shared {
    std::mutex m;
    std::condition_variable cv;
    /// Split branches waiting for a thread.
    std::deque<std::unique_ptr<B>> ready;
    /// Threads taking part (the caller's from the start; a lent thread
    /// counts once it has started, so one that starts late finds the run
    /// over and goes) and the ones of them waiting for a branch.
    unsigned int registered = 1;
    unsigned int waiting = 0;
    bool done = false;
    /// Threads with nothing to search; read by the busy ones without the
    /// lock.
    std::atomic<unsigned int> hungry{0};
    std::exception_ptr error;
  } shared;

  // branches a pause left unfinished come first
  for (std::unique_ptr<B>& b : search.takeParked()) {
    shared.ready.push_back(std::move(b));
  }

  // Peel root branches so that every thread starts with one. When the first
  // steps of the search are forced nothing can be peeled yet; the threads
  // left without then get theirs below.
  while (shared.ready.size() + 1 < workers) {
    std::unique_ptr<B> n = search.splitBranch(root);
    if (!n) {
      break;
    }
    shared.ready.push_back(std::move(n));
  }

  constexpr unsigned int kSlice = 1000;

  // One thread: search what it has a slice at a time. Between slices, give a
  // waiting thread a branch; when its own search ends, wait for one. The
  // root is the caller's, which the thread that has it does not own.
  auto worker = [&](unsigned int slot) {
    std::unique_ptr<B> owned;
    B* cur = slot == 0 ? &root : nullptr;
    if (slot != 0) {
      const std::lock_guard<std::mutex> lock(shared.m);
      shared.registered++;
    }

    try {
      for (;;) {
        if (cur == nullptr) {
          std::unique_lock<std::mutex> lock(shared.m);
          shared.waiting++;
          shared.hungry.fetch_add(1, std::memory_order_relaxed);
          while (shared.ready.empty() && !shared.done && !run.stopRequested()) {
            if (shared.waiting == shared.registered) {
              shared.done = true;
              shared.cv.notify_all();
              break;
            }
            shared.cv.wait(lock);
          }
          shared.waiting--;
          shared.hungry.fetch_sub(1, std::memory_order_relaxed);
          if (shared.ready.empty() || run.stopRequested()) {
            // A stop comes from another thread and notifies nobody: the
            // threads still waiting must be woken to see it. (The original
            // returned here without, so a pause that came as the last busy
            // thread finished its branch left the others waiting for good.)
            shared.cv.notify_all();
            return;
          }
          owned = std::move(shared.ready.front());
          shared.ready.pop_front();
          cur = owned.get();
        }

        if (run.stopRequested()) {
          break;
        }

        search.searchSlice(*cur, run, kSlice);

        if (search.branchFinished(*cur)) {
          if (owned) {
            search.retireBranch(*owned);
            owned.reset();
          }
          cur = nullptr;
          continue;
        }

        if (run.stopRequested()) {
          break;
        }

        if (shared.hungry.load(std::memory_order_relaxed) > 0) {
          bool wanted = false;
          {
            const std::lock_guard<std::mutex> lock(shared.m);
            wanted = shared.ready.size() < shared.waiting;
          }
          if (wanted) {
            std::unique_ptr<B> n = search.splitBranch(*cur);
            if (n) {
              const std::lock_guard<std::mutex> lock(shared.m);
              shared.ready.push_back(std::move(n));
              shared.cv.notify_one();
            }
          }
        }
      }
    } catch (...) {
      run.stop(StopKind::Abort);
      const std::lock_guard<std::mutex> lock(shared.m);
      if (!shared.error) {
        shared.error = std::current_exception();
      }
    }

    // stopped: what is left of this branch waits for the next run
    if (owned) {
      search.park(std::move(owned));
    }
    const std::lock_guard<std::mutex> lock(shared.m);
    shared.cv.notify_all();
  };

  if (pool != nullptr && workers > 1) {
    (void)pool->run(workers, worker);
  } else {
    worker(0);
  }

  // a stop can leave branches nobody had taken yet
  while (!shared.ready.empty()) {
    search.park(std::move(shared.ready.front()));
    shared.ready.pop_front();
  }

  if (shared.error) {
    std::rethrow_exception(shared.error);
  }
}

}  // namespace bt
