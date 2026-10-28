// SPDX-License-Identifier: GPL-3.0-or-later
// The solve behind Problem.solve(): a thread of its own finds the
// solutions and hands them over through a short queue, so Python takes
// them one at a time while the search goes on.
// Ported from src/python/py_solution_iterator.{h,cpp}.
#pragma once

#include "py_solution.h"

#include "shapelogic/base/stop.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/stack_search.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bt {
class Assembler;
class Disassembler;
class Problem;
class Puzzle;
class SolveSession;
}  // namespace bt

namespace bt::python {

/// The arguments of Problem.solve().
struct IteratorOptions {
  /// Take each assembly apart; only those that come apart are yielded.
  bool disassemble = true;
  /// Remove placements that cannot be part of an assembly first.
  bool reduce = false;
  bool keepRotations = false;
  bool keepMirror = false;
  /// The threads of the solve; 0 for every hardware thread (the original's
  /// default, without SHAPELOGIC_THREADS).
  unsigned int threads = 0;
  /// A sliding puzzle's search (the GUI's sliding boxes).
  sliding::SlidingOptions sliding;
  /// A stacking puzzle's search; its workDir is where a Panex search keeps
  /// its levels (empty: nowhere, each half solved again).
  stacking::StackingOptions stacking;
};

/// A solve of one problem of a copy of the puzzle (the module's
/// `SolutionIterator`). Brick puzzles run as the original's iterator ran
/// them: the assembler on this object's thread, each assembly taken apart
/// in its callback. Sliding and stacking puzzles run through a
/// SolveSession, as every solve of theirs does (decisions 0014, 0015), and
/// their solutions come when the run ends.
class SolutionIterator {
 public:
  /// Starts the solve of problem `problemIdx` of a copy of `puzzle`.
  SolutionIterator(const Puzzle& puzzle, unsigned int problemIdx, IteratorOptions options);
  /// Stops the solve and waits for its thread.
  ~SolutionIterator();

  SolutionIterator(const SolutionIterator&) = delete;
  SolutionIterator& operator=(const SolutionIterator&) = delete;

  /// The next solution; pybind11::stop_iteration at the end or after
  /// stop(), std::runtime_error when the solve failed. Waits with the GIL
  /// released, and stops on Ctrl-C (the pending KeyboardInterrupt raised).
  PySolution next();

  /// Stops the solve and waits for its thread; solutions already queued
  /// are still handed out.
  void stop();
  /// The assembler's search steps (the run's, for a session), live.
  [[nodiscard]] std::uint64_t getIterations() const;
  [[nodiscard]] bool isFinished() const { return finished_.load(); }

 private:
  struct QueueItem {
    enum class Type : std::uint8_t { Solution, Finished, Error };
    Type type = Type::Solution;
    PySolution solution;
    std::string errorMessage;
  };

  std::unique_ptr<Puzzle> puzzle_;
  unsigned int problemIdx_;
  IteratorOptions options_;

  // made and used by the worker; read through the atomics while it runs,
  // destroyed after it has ended
  std::unique_ptr<Assembler> assembler_;
  std::unique_ptr<SolveSession> session_;
  std::atomic<const Assembler*> activeAssembler_{nullptr};
  std::atomic<const SolveSession*> activeSession_{nullptr};

  StopSource stopSource_;
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> finished_{false};
  std::atomic<std::uint64_t> iterations_{0};

  std::mutex queueMutex_;
  std::condition_variable canPop_;
  std::condition_variable canPush_;
  std::deque<QueueItem> queue_;
  static constexpr std::size_t kMaxQueueSize = 4;

  std::thread worker_;

  void workerRun();
  void runAssembler(Problem& problem);
  void runSession(Problem& problem);
  void pushItem(QueueItem&& item);
};

}  // namespace bt::python
