// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from mainWindow_c::cb_AddDisasm and cb_AddAllDisasm
// (src/gui/mainwindow_solve.cpp): one disassembler for the problem, made
// with the Solver tab's take-apart settings, and each assembly taken apart
// in turn; a separation found replaces what the solution had.
#include "shapelogic/app/analysis.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/thread_pool.h"

#include <stdexcept>
#include <utility>

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
  d.maxDepth = options.maxDepth;
  d.pool = pool_.get();
  // made here, on the owner's thread: it warms the shapes' caches
  disassembler_ = std::make_unique<Disassembler>(puzzle, problem, d);
  start();
}

Analysis::~Analysis() { finish(); }

void Analysis::run() {
  for (const auto& [id, assembly] : work_) {
    if (stop_.stopRequested()) {
      break;
    }
    TakeApartResult r = disassembler_->disassemble(assembly, stop_.token());
    if (r.stopped) {
      break;
    }
    if (r.tooDeep) {
      // the job's failure (BackgroundJob): what was taken apart before it
      // is kept, this solution and those after it stay as they were
      throw std::runtime_error(Disassembler::tooDeepMessage());
    }
    if (r.separation) {
      const std::lock_guard<std::mutex> lock(mutex_);
      results_.emplace_back(id, std::move(*r.separation));
      separated_.fetch_add(1, std::memory_order_relaxed);
    }
    countDone();
  }
}

AnalyseProgress Analysis::progress() const {
  AnalyseProgress p;
  p.active = active();
  p.total = static_cast<unsigned int>(work_.size());
  p.done = done();
  p.separated = separated_.load(std::memory_order_relaxed);
  // ended before the last: stopped, or by an error
  p.stopped = !p.active && p.done < p.total;
  if (p.active) {
    p.current = disassembler_->progress();
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  p.failure = failure_;
  return p;
}

std::vector<std::pair<std::uint64_t, Separation>> Analysis::takeResults() {
  SL_ASSERT_MSG(!active(), "the results of an analysis still running");
  wait();
  const std::lock_guard<std::mutex> lock(mutex_);
  return std::move(results_);
}

}  // namespace bt::app
