// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the two assemblers' searches hand to the run layer in the same way:
/// an assembly a thread of a split search found, a thread's search of its
/// tasks (the vector search with the plain worker beside it), and the
/// vector search of the whole matrix on the caller's thread. The original
/// has a copy of each in assembler_0.cpp and in assembler_1.cpp; here each
/// assembler says only what is its own (its task, its solver, its worker).
/// After src/lib/assembler_0.cpp and assembler_1.cpp (reportParallelSolution,
/// parallelMultiSearch, simdSearch).

#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/resume_state.h"
#include "shapelogic/solve/row_rotation_check.h"
#include "shapelogic/solve/search_run.h"
#include "shapelogic/solve/subtree_split.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sl {

/// An assembly found by a search split over threads, by its plain workers
/// and its vector ones alike: dropped when `rowsRejected()` says a rotation
/// of its rows is smaller, else built (`build()`), dropped when
/// `rejected(assembly)` says so, and handed over once: with tasks that never
/// overlap and are left exactly nothing is found twice; else
/// `signature(assembly)` tells it from the ones reported before. False when
/// the run is stopped and it was not handed over (the search then leaves
/// the frame as not begun, and the next run finds it again).
template <class Task, class RowsRejected, class Build, class Rejected, class Signature>
bool reportSplitAssembly(SearchRun& run, SplitSearchState<Task>& split,
                         RowsRejected&& rowsRejected, Build&& build, Rejected&& rejected,
                         Signature&& signature) {
  if (!run.hasCallback()) {
    return true;
  }

  if (rowsRejected()) {
    return true;
  }

  Assembly assembly = build();

  if (rejected(assembly)) {
    return true;
  }

  if (split.exact) {
    return run.deliverUnlessStopped(std::move(assembly));
  }

  const std::uint64_t sig = signature(assembly);
  return run.deliverOnce(std::move(assembly), sig, split.reported);
}

/// One thread's search of the tasks of a split search: the vector search
/// for the tasks it takes, whose branches (the rows of each) become tasks
/// here, and the plain worker for the others.
///
/// `Policy` is the assembler's part, made once a thread:
///   Task                        the task type
///   plainTakes(task)            the plain worker searches this task
///   plain(index, task, units)   that search (the policy makes its worker
///                               the first time); true when through
///   vector(task, cover, stop, iterations, progress, splitter)
///                               the vector search of the task, as its
///                               solver's solveSubtree takes them; true
///                               when through
///   report(nodes, scratch)      a cover found: reportParallelSolution
///   branchTask(branches, i), restTask(branches)
///                               a branch handed on, and the one the search
///                               goes on with, as tasks
template <class Policy>
class SplitTaskSearch final : public SubtreeSplitter {
 public:
  using Task = typename Policy::Task;

  /// For the thread of `slot`; the iterations a vector search counts go to
  /// `iterations`, how far it is to `progress`.
  SplitTaskSearch(Policy policy, SplitTasks<Task>& tasks, unsigned int slot,
                  std::atomic<std::uint64_t>& iterations, TaskProgress& progress,
                  const StopToken& stop)
      : policy_(std::move(policy)),
        tasks_(tasks),
        slot_(slot),
        iterations_(iterations),
        progress_(progress),
        stop_(stop) {}

  /// Searches `task` to its end (true) or to a stop (false).
  bool operator()(std::size_t index, const Task& task, std::uint64_t taskUnits) {
    if (policy_.plainTakes(task)) {
      return policy_.plain(index, task, taskUnits);
    }

    taskIndex_ = index;
    units_ = taskUnits;

    std::atomic<std::uint64_t> taskIter{0};
    auto drain = [this, &taskIter] {
      iterations_.fetch_add(taskIter.exchange(0, std::memory_order_relaxed),
                            std::memory_order_relaxed);
    };
    const bool through = policy_.vector(
        task,
        [this, &drain](const std::vector<unsigned int>& nodes) -> bool {
          drain();
          return policy_.report(nodes, rowScratch_);
        },
        stop_, taskIter,
        [this, &drain](float fraction) {
          drain();
          progress_.reportUnits(slot_, units_, fraction);
        },
        this);
    drain();
    return through;
  }

  [[nodiscard]] bool wanted() const override { return tasks_.wanted(); }

  void handOff(SubtreeBranches&& branches) override {
    TaskSplit<Task> split = toSplit(branches);
    split.rest = policy_.restTask(branches);
    split.restShare = branches.restShare;
    split.restFraction = branches.restFraction;
    units_ = tasks_.handOff(slot_, taskIndex_, std::move(split));
  }

  void leave(SubtreeBranches&& branches) override {
    tasks_.leave(slot_, taskIndex_, toSplit(branches));
  }

 private:
  [[nodiscard]] TaskSplit<Task> toSplit(const SubtreeBranches& branches) const {
    TaskSplit<Task> split;
    split.shares = branches.shares;
    split.branches.reserve(branches.branches.size());
    for (std::size_t i = 0; i < branches.branches.size(); i++) {
      split.branches.push_back(policy_.branchTask(branches, i));
    }
    return split;
  }

  Policy policy_;
  SplitTasks<Task>& tasks_;
  unsigned int slot_;
  std::atomic<std::uint64_t>& iterations_;
  TaskProgress& progress_;
  const StopToken& stop_;
  std::size_t taskIndex_ = 0;
  std::uint64_t units_ = 0;
  RowRotationCheck::Scratch rowScratch_;
};

/// The vector search of the whole matrix on the caller's thread, with
/// `solver` (its solve()): reported as one task, so that the progress has
/// something to say while it runs, and its iterations handed to
/// `iterations` as it goes. `found(nodes)` gets every cover but the first
/// `split.vectorSkip`, which a run before this one reported (a stopped
/// vector search starts again from the beginning). True when the search ran
/// to its end; false when the run was stopped, and `split` then says how
/// many covers the next run passes over.
template <class Solver, class Task, class Found>
bool vectorSearchWhole(const Solver& solver, SearchRun& run, SplitSearchState<Task>& split,
                       TaskProgress& progress, std::atomic<std::uint64_t>& iterations,
                       Found&& found) {
  std::atomic<std::uint64_t> simdIter{0};

  progress.begin({1.0}, {0});

  auto drain = [&iterations, &simdIter] {
    iterations.fetch_add(simdIter.exchange(0, std::memory_order_relaxed),
                         std::memory_order_relaxed);
  };

  split.vectorDone = 0;
  solver.solve(
      [&](const std::vector<unsigned int>& solutionNodes) -> bool {
        drain();
        // already reported before the search was stopped
        if (split.vectorDone++ < split.vectorSkip) {
          return !run.stopRequested();
        }
        found(solutionNodes);
        return !run.stopRequested();
      },
      run.token(), simdIter,
      [&progress, &drain](float fraction) {
        drain();
        progress.report(0, 0, fraction);
      });

  drain();

  if (run.stopRequested()) {
    split.vectorSkip = std::max(split.vectorSkip, split.vectorDone);
    split.interrupted = true;
    return false;
  }
  progress.finish(0, 0);
  split.vectorSkip = 0;
  split.interrupted = false;
  return true;
}

}  // namespace sl
