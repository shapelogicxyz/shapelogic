// SPDX-License-Identifier: GPL-3.0-or-later
// The stacking flow of SolveSession (cleanup item 21.3): ported from
// src/lib/solvethread.cpp (runStacking) and the original GUI's Solve and
// Continue with their stacking branches (src/gui/mainwindow_solve.cpp).
#include "shapelogic/solve/solve_session.h"

#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/solve_session_internal.h"

#include <algorithm>
#include <optional>
#include <string>

namespace bt {

using namespace session_detail;

// Under controlMutex_, nothing active. A stacking puzzle's Solve and
// Continue (cb_BtnStart and cb_BtnCont with their stacking branches): the
// checks, then a run of runStacking.
Result<void> SolveSession::launchStacking(Mode mode) {
  if (mode == Mode::Prepare) {
    return fail(Errc::Unsupported, "a stacking search has nothing to prepare");
  }
  if (!problem_.rodSetValid(puzzle_)) {
    return fail(Errc::InvalidArgument, "Choose a rod set with Set Start/Goal Rods");
  }
  if (!options_.disassemble && !options_.justCount) {
    return fail(Errc::InvalidArgument, "Turn on Find Solutions");
  }
  if (const std::string err = stacking::setupError(puzzle_, problem_); !err.empty()) {
    return fail(Errc::InvalidArgument, err);
  }
  if (options_.stacking.solver == stacking::StackingSolver::Panex) {
    if (const std::string why = panex::unsupported(puzzle_, problem_); !why.empty()) {
      return fail(Errc::Unsupported, why);
    }
  }

  // Solve starts afresh, so a saved search would be lost (the GUI asked
  // first); Continue carries it on, as both stacking solvers do.
  if (mode == Mode::Start) {
    panex::discardSaved(puzzle_, problem_, options_.stacking.workDir);
  }
  // The hotspots where a stacking solve has them: a disc's at its center
  // (cb_BtnCont's ensureHotspot); no change when the owner set them so
  // before the first run (the document does).
  for (unsigned int i = 0; i < puzzle_.getNumberOfShapes(); i++) {
    (void)stacking::placeSolvingHotspot(puzzle_.getShape(i));
  }

  const SolveState st = state_.load();
  if (st == SolveState::Finished || st == SolveState::Error) {
    state_.advance(SolveState::Idle);
  }
  dropAssembler();

  Plan plan;
  plan.options = options_;
  resetRun(options_, plan);
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    runOnPanex_ = panex::runsOnPanexSearch(
        puzzle_, problem_, options_.stacking.solver == stacking::StackingSolver::Panex);
  }
  slideProgress_.store(0, std::memory_order_relaxed);

  state_.advance(SolveState::Preparing);
  runner_ = std::thread([this, plan = std::move(plan)] { run(plan); });
  return {};
}

// The solve thread's runStacking: one search from the start stacking to the
// goal, the Panex search whenever it can hold the puzzle (it can pause and
// carry on), else findStackPath; a found path is the one solution, its
// assembly the start stacking. The notes word for word.
void SolveSession::runStacking(const Plan& plan) {
  const SolveOptions& o = plan.options;
  const bool panexSolver = o.stacking.solver == stacking::StackingSolver::Panex;
  const bool countOnly = o.justCount;
  const bool panexSearch = panex::runsOnPanexSearch(puzzle_, problem_, panexSolver);
  // The Panex search saves itself in its folder: one it cannot write
  // pauses the run before it begins (the results as they were), and
  // Continue tries again.
  if (panexSearch && !folderWritable(o.stacking.workDir)) {
    leave(SolveState::Paused);
    return;
  }
  // the search's memory is shared with the other solves' while this runs
  MemoryShare::Lease lease;
  if (o.memoryShare) {
    lease = o.memoryShare->join();
  }
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    problem_.removeAllSolutions();
    problem_.markSolving();
  }
  state_.advance(SolveState::Assembling);
  beginPhase(Phase::Assemble);

  const unsigned int budget = o.threads > 0 ? std::min(o.threads, kMaxThreads) : hardwareThreads();
  std::optional<Separation> path;
  std::string note;
  bool stopped = false;
  // The Panex Solver, and the Stacking Solver on any puzzle the same
  // search can hold: that search can pause, resume and autosave.
  if (panexSearch) {
    // the levels on the solve's threads, a saved level written beside them
    // (the original's writer thread)
    ThreadPool pool(std::min(budget + 1, kMaxThreads));
    panex::PanexSearch search;
    search.anyRules = !panexSolver;
    search.highMemory = o.stacking.highMemory;
    search.memoryBytes = o.stacking.memoryBytes;
    search.memoryShare = o.memoryShare.get();
    search.stop = runToken();
    search.progress = &slideProgress_;
    search.depth = &searchDepth_;
    search.traced = &searchTraced_;
    search.sample = &sample_;
    search.resume = true;
    search.autosave = o.stacking.autosave;
    search.workDir = o.stacking.workDir;
    search.diskBudget = o.stacking.diskBudget;
    search.towerRule = o.stacking.towerRule;
    search.pool = &pool;
    search.threads = budget;
    // time used counts every run of a saved search, before this one too
    const std::uint64_t before = panex::savedMs(puzzle_, problem_, o.stacking.workDir);
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.addTimeMs(before);
    }
    path = panex::solve(puzzle_, problem_, search);
    stopped = search.outcome == panex::Outcome::Stopped;
    // A search that ended in an error with a saved search still in its
    // folder is not through: one that could not be read back (it is kept,
    // decision 0041), or the last one saved before a write failed. The run
    // pauses, as when it is stopped, so the problem is not kept as solved
    // without a solution, and Continue tries the saved search again.
    if (search.outcome == panex::Outcome::Error &&
        (search.savedUnread ||
         !panex::savedSearch(puzzle_, problem_, o.stacking.workDir).empty())) {
      stopped = true;
    }
    if (path) {
      // no note
    } else if (search.outcome == panex::Outcome::Error) {
      note = search.error;
      if (search.savedUnread) {
        note += " Continue tries to carry the saved search on again; Solve throws it away and "
                "searches from the beginning.";
      } else if (stopped) {
        note += " The search is still saved as it last was: Continue carries it on; Solve "
                "throws it away and searches from the beginning.";
      }
    } else if (stopped && search.saved) {
      note = "Paused " + withCommas(search.depthReached) +
             " moves deep. The search is saved: press Continue to carry on, even after quitting "
             "ShapeLogic.";
    } else if (stopped) {
      note = "Stopped before the search was complete.";
    } else if (search.outcome == panex::Outcome::Memory) {
      note = "The search outgrew its memory after " + withCommas(search.found) +
             " stackings.";
      if (search.saved && sharedNow(o)) {
        note += " It is saved. " + sharedMemoryWords(o);
      } else if (!o.settingsPlace.empty()) {
        note += search.saved ? " It is saved: give it more Search memory in " + o.settingsPlace +
                                   " and press Continue to carry on."
                             : " " + o.settingsPlace + " can give it more Search memory.";
      } else {
        note += search.saved ? std::string(" It is saved:") +
                                   (search.highMemory ? "" : " turn on Enable High Memory and") +
                                   " press Continue to carry on."
                             : (search.highMemory ? "" : " Try Enable High Memory.");
      }
    } else {
      note = "Every reachable stacking was searched: this puzzle has no solution.";
    }
  } else {
    stacking::StackSearch search;
    search.highMemory = o.stacking.highMemory;
    search.memoryBytes = o.stacking.memoryBytes;
    search.memoryShare = o.memoryShare.get();
    search.stop = runToken();
    search.progress = &slideProgress_;
    search.sample = &sample_;
    path = stacking::findStackPath(puzzle_, problem_, search);
    stopped = search.outcome == stacking::StackOutcome::Stopped;
    if (path) {
      // no note
    } else if (stopped) {
      note = "Stopped before the search was complete.";
    } else if (search.outcome == stacking::StackOutcome::Memory) {
      note = "The search reached its memory limit of " + withCommas(search.memoryStates) +
             " stackings. There may be a solution the solver did not find." +
             (sharedNow(o)               ? " " + sharedMemoryWords(o)
              : !o.settingsPlace.empty() ? " " + o.settingsPlace + " can give it more Search memory."
              : search.highMemory        ? ""
                                         : " Try Enable High Memory.");
    } else {
      note = "Every reachable stacking was searched: this puzzle has no solution.";
    }
  }
  endPhase(assemblyMs_);
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    problem_.addTimeMs(runMs());
    runTimeAdded_ = true;
    problem_.incNumAssemblies();
    if (path) {
      problem_.incNumSolutions();
      if (!countOnly) {
        problem_.addSolution(stacking::startAssembly(puzzle_, problem_), std::move(*path), 0, 0);
      }
    }
  }
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    note_ = std::move(note);
  }
  endStackingRun(stopped);
}

// The end of a stacking run. The solve thread finished it whatever had
// happened: the problem is solved, with one start and its path or none.
// A search that stopped short (a pause) is not through, so the session is
// Paused and resume() carries on what the search saved; an abort throws it
// all away.
void SolveSession::endStackingRun(bool stopped) {
  if (runStopKind() == StopKind::Abort) {
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
    }
    leave(SolveState::Idle);
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    problem_.finishedSolving();
    // what the GUI recorded when a solve stopped (recordSearchStats)
    problem_.setSearchStats(
        problem_.getSearched() + slideProgress_.load(std::memory_order_relaxed),
        std::max(problem_.getSearchDepth(), searchDepth_.load(std::memory_order_relaxed)),
        problem_.getTriedApart());
  }
  leave(stopped ? SolveState::Paused : SolveState::Finished);
}

}  // namespace bt
