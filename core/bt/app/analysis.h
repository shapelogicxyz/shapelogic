// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Taking a problem's saved assemblies apart afterwards ("Add disassembly"
/// for one solution, or for all of them or those without their moves):
/// on a thread of its own, with progress and a stop.
/// Ported from mainWindow_c::cb_AddDisasm and cb_AddAllDisasm
/// (src/gui/mainwindow_solve.cpp), on the Disassembler of stage 4. The
/// original took them apart on the GUI thread behind a modal window; a
/// solution whose assembly does not come apart keeps what it had, as
/// there.

#include "bt/base/stop.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solver_type.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
class ThreadPool;
}  // namespace bt

namespace bt::app {

/// Which saved solutions an analysis takes apart.
enum class AnalyseWhich : std::uint8_t {
  All,      ///< every one (cb_AddAllDisasm(true))
  Missing,  ///< those without their moves stored (cb_AddAllDisasm(false))
  One,      ///< one solution (cb_AddDisasm)
};

/// How an analysis takes apart (the Solver tab's settings in the original).
struct AnalyseOptions {
  AnalyseWhich which = AnalyseWhich::Missing;
  /// The solution for One (Solution::getId()).
  std::uint64_t solution = 0;
  /// Crowell has a take-apart of its own; the others Classic's.
  SolverType solver = SolverType::Classic;
  /// Try 90 degree turns too.
  bool rotations = false;
  /// The threads a level is spread over; 0 every hardware thread.
  unsigned int threads = 0;
};

/// Where an analysis stands.
struct AnalyseProgress {
  bool active = false;
  unsigned int total = 0;
  unsigned int done = 0;
  /// Taken apart so far (the others do not come apart).
  unsigned int separated = 0;
  /// Stopped before the end.
  bool stopped = false;
  /// The take-apart under way.
  DisassemblyProgress current;
  /// An internal error ended it (what it said); what was done before stays.
  std::optional<std::string> failure;
};

/// One analysis: takes the given assemblies apart, one after the other, on
/// a thread of its own. The puzzle and the problem are read while it runs
/// (the shapes and the problem's parts): nobody may change them.
class Analysis {
 public:
  /// Starts at once with the assembly of each solution (its id first).
  Analysis(const Puzzle& puzzle, const Problem& problem,
           std::vector<std::pair<std::uint64_t, Assembly>> work, const AnalyseOptions& options);
  /// Stops and waits.
  ~Analysis();

  Analysis(const Analysis&) = delete;
  Analysis& operator=(const Analysis&) = delete;

  /// Asks it to stop as soon as it can; does not wait. Any thread.
  void stop();
  /// Waits until it has ended.
  void wait();
  /// Still taking apart. Any thread.
  [[nodiscard]] bool active() const { return active_.load(std::memory_order_acquire); }
  /// Any thread.
  [[nodiscard]] AnalyseProgress progress() const;
  /// The separations found, by solution id, in the order of the work;
  /// once it has ended.
  [[nodiscard]] std::vector<std::pair<std::uint64_t, Separation>> takeResults();

 private:
  void run();

  std::vector<std::pair<std::uint64_t, Assembly>> work_;
  std::unique_ptr<ThreadPool> pool_;
  std::unique_ptr<Disassembler> disassembler_;
  StopSource stop_;

  std::atomic<unsigned int> done_{0};
  std::atomic<unsigned int> separated_{0};
  std::atomic<bool> active_{true};
  std::atomic<bool> stopped_{false};

  mutable std::mutex mutex_;
  std::vector<std::pair<std::uint64_t, Separation>> results_;
  std::optional<std::string> failure_;

  std::thread thread_;
};

}  // namespace bt::app
