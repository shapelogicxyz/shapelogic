// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler.cpp (the task progress of assembler_c) and
// the hand-over in assembler_0_c / assembler_1_c::reportParallelSolution.
#include "bt/solve/search_run.h"

namespace bt {

void SearchRun::handOver(Assembly assembly) {
  // An exception from the callback stops the run before the lock is given
  // up, so that no other thread hands over anything after it (the original
  // stopped only when the exception reached its worker loop).
  try {
    if (!callback_(std::move(assembly))) {
      stop(StopKind::Pause);
    }
  } catch (...) {
    stop(StopKind::Abort);
    throw;
  }
}

void SearchRun::deliver(Assembly assembly) {
  if (!callback_) {
    return;
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  handOver(std::move(assembly));
}

void SearchRun::deliverOnce(Assembly assembly, std::uint64_t signature,
                            std::unordered_set<std::uint64_t>& reported) {
  if (!callback_) {
    return;
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  if (stopRequested()) {
    return;
  }
  if (!reported.insert(signature).second) {
    return;
  }
  handOver(std::move(assembly));
}

void SearchRun::deliverFound(Assembly assembly) {
  if (!callback_) {
    return;
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  if (token_.kind() == StopKind::Abort) {
    return;
  }
  handOver(std::move(assembly));
}

void TaskProgress::begin(const std::vector<double>& weights,
                         const std::vector<std::uint8_t>& completed) {
  const std::size_t n = completed.size();

  std::vector<double> w = weights;
  if (w.size() != n) {
    w.assign(n, 1.0);
  }
  double sum = 0;
  for (const double x : w) {
    sum += x;
  }
  if (!(sum > 0)) {
    w.assign(n, 1.0);
    sum = static_cast<double>(n);
  }

  units_.resize(n);
  std::uint64_t total = 0;
  std::uint64_t done = 0;
  std::size_t doneTasks = 0;
  for (std::size_t i = 0; i < n; i++) {
    std::uint64_t u = static_cast<std::uint64_t>(w[i] / sum * static_cast<double>(kUnits));
    if (u < 1) {
      u = 1;
    }
    units_[i] = u;
    total += u;
    if (completed[i]) {
      done += u;
      doneTasks++;
    }
  }

  for (std::atomic<std::uint64_t>& r : running_) {
    r.store(0, std::memory_order_relaxed);
  }
  done_.store(done, std::memory_order_relaxed);
  total_.store(total, std::memory_order_relaxed);
  completedTasks_.store(doneTasks, std::memory_order_relaxed);
  totalTasks_.store(n, std::memory_order_relaxed);
}

float TaskProgress::fraction() const noexcept {
  const std::size_t tasks = totalTasks_.load(std::memory_order_relaxed);
  if (tasks == 0) {
    return 0;
  }
  if (completedTasks_.load(std::memory_order_relaxed) >= tasks) {
    return 1;
  }

  const std::uint64_t total = total_.load(std::memory_order_relaxed);
  if (total == 0) {
    return static_cast<float>(completedTasks_.load(std::memory_order_relaxed)) /
           static_cast<float>(tasks);
  }

  std::uint64_t v = done_.load(std::memory_order_relaxed);
  for (const std::atomic<std::uint64_t>& r : running_) {
    v += r.load(std::memory_order_relaxed);
  }

  // a task being finished is for a moment counted twice
  if (v >= total) {
    return 0.9999F;
  }
  return static_cast<float>(static_cast<double>(v) / static_cast<double>(total));
}

}  // namespace bt
