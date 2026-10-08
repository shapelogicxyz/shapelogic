// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from mainWindow_c::cb_AddDisasm and cb_AddAllDisasm
// (src/gui/mainwindow_solve.cpp): one disassembler for the problem, made
// with the Solver tab's take-apart settings, and each assembly taken apart
// in turn; a separation found replaces what the solution had.
#include "bt/app/analysis.h"

#include "bt/base/assert.h"
#include "bt/base/thread_pool.h"

#include <exception>

namespace bt::app {

Analysis::Analysis(const Puzzle& puzzle, const Problem& problem,
                   std::vector<std::pair<std::uint64_t, Assembly>> work,
                   const AnalyseOptions& options)
    : work_(std::move(work)),
      pool_(std::make_unique<ThreadPool>(options.threads == 0 ? hardwareThreads()
                                                              : options.threads)) {
  DisassemblerOptions d;
  d.rotations = options.rotations;
  d.solver = options.solver;
  d.pool = pool_.get();
  // made here, on the owner's thread: it warms the shapes' caches
  disassembler_ = std::make_unique<Disassembler>(puzzle, problem, d);
  thread_ = std::thread([this] { run(); });
}

Analysis::~Analysis() {
  stop();
  wait();
}

void Analysis::stop() { stop_.request(StopKind::Abort); }

void Analysis::wait() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Analysis::run() {
  try {
    for (const auto& [id, assembly] : work_) {
      if (stop_.stopRequested()) {
        break;
      }
      TakeApartResult r = disassembler_->disassemble(assembly, stop_.token());
      if (r.stopped) {
        break;
      }
      if (r.separation) {
        const std::lock_guard<std::mutex> lock(mutex_);
        results_.emplace_back(id, std::move(*r.separation));
        separated_.fetch_add(1, std::memory_order_relaxed);
      }
      done_.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    const std::lock_guard<std::mutex> lock(mutex_);
    failure_ = e.what();
  }
  stopped_.store(done_.load() < work_.size(), std::memory_order_relaxed);
  active_.store(false, std::memory_order_release);
}

AnalyseProgress Analysis::progress() const {
  AnalyseProgress p;
  p.active = active();
  p.total = static_cast<unsigned int>(work_.size());
  p.done = done_.load(std::memory_order_relaxed);
  p.separated = separated_.load(std::memory_order_relaxed);
  p.stopped = !p.active && stopped_.load(std::memory_order_relaxed);
  if (p.active) {
    p.current = disassembler_->progress();
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  p.failure = failure_;
  return p;
}

std::vector<std::pair<std::uint64_t, Separation>> Analysis::takeResults() {
  BT_ASSERT_MSG(!active(), "the results of an analysis still running");
  wait();
  const std::lock_guard<std::mutex> lock(mutex_);
  return std::move(results_);
}

}  // namespace bt::app
