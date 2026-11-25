// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What a document's background jobs (Analysis, PlacementFit) share: a
/// thread of their own, a stop, whether they still run, how many of their
/// items are done, and what ended them early (cleanup item 11.12). New
/// code.

#include "shapelogic/base/stop.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace bt::app {

/// One background job of a document. A job starts its thread at the end of
/// its constructor (start()) and ends it first thing in its destructor
/// (finish()), as the thread runs the job's own run().
class BackgroundJob {
 public:
  /// The kinds of job; the document keeps one of each kind per problem.
  /// Each job class names its own as `kKind`, which is what the document
  /// files it under and finds it by (Document::startJob, job), so a job is
  /// never read as another class.
  enum class Kind : std::uint8_t {
    Analysis,      ///< saved assemblies taken apart (analysis.h)
    PlacementFit,  ///< a tilted tray's placements tried (placement_fit.h)
  };

  virtual ~BackgroundJob();
  BackgroundJob(const BackgroundJob&) = delete;
  BackgroundJob& operator=(const BackgroundJob&) = delete;

  /// Asks it to stop as soon as it can; does not wait. Any thread.
  void stop();
  /// Waits until it has ended.
  void wait();
  /// Still running. Any thread.
  [[nodiscard]] bool active() const { return active_.load(std::memory_order_acquire); }

 protected:
  BackgroundJob() = default;

  /// Starts the thread on run(): the last thing a job's constructor does.
  void start();
  /// Stops and waits: the first thing a job's destructor does, before its
  /// members go.
  void finish();
  /// The job, on its thread. An exception ends it, its message the job's
  /// failure.
  virtual void run() = 0;
  /// What stop() does besides the stop request (a solve under way paused).
  virtual void onStop() {}

  /// The items done so far; written by the job's thread alone.
  [[nodiscard]] unsigned int done() const { return done_.load(std::memory_order_relaxed); }
  void countDone() { done_.fetch_add(1, std::memory_order_relaxed); }
  StopSource stop_;
  /// Guards failure_ and what a job keeps for progress() and its results.
  mutable std::mutex mutex_;
  /// What ended the job early, when it was no stop (an exception, or the
  /// job's own reason); under mutex_.
  std::optional<std::string> failure_;

 private:
  std::atomic<unsigned int> done_{0};
  std::atomic<bool> active_{true};
  std::thread thread_;
};

}  // namespace bt::app
