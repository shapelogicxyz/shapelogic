// SPDX-License-Identifier: GPL-3.0-or-later
// The sliding flow of SolveSession (cleanup item 21.3): ported from
// src/lib/solvethread.cpp (the assembly callback's sliding branch,
// slidingSummary) and the original GUI's Continue refusal; the slide
// search's own state is SolveSession::SlideRun.
#include "shapelogic/solve/solve_session.h"

#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/solve_session_internal.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sl {

using namespace session_detail;

std::string SolveSession::resumeRefusal(const SolveOptions& options) const {
  if (!sliding_ || stacking_ || state_.load() != SolveState::Paused) {
    return "";
  }
  // every option a saved search is named by, from the one list of them
  // (sliding::SlideNaming: what the search mixes into the name)
  const sliding::SlidingOptions& was = runOptions_.sliding;
  const std::vector<std::string_view> names = sliding::namesDiffer(
      sliding::slideNaming(was, slide_.grid),
      sliding::slideNaming(options.sliding,
                           sliding::searchGrid(puzzle_, problem_, options.sliding)));
  // nothing saved: a search with other options leaves nothing behind
  if (names.empty() || sliding::savedFolders(puzzle_, problem_, was.workDir).empty()) {
    return "";
  }
  std::string differ;
  for (const std::string_view name : names) {
    differ.append(differ.empty() ? "" : ", ").append(name);
  }
  return "the paused search was made with other search options (" + differ +
         "): set them as they were to carry it on; to search with these instead, Abort, then "
         "Solve";
}

void SolveSession::prepareSlideRun(const SolveOptions& o) {
  const std::lock_guard<std::mutex> lock(resultsMutex_);
  (void)sliding::prepareForSolving(puzzle_, problem_);
  // optimized paths: the grid is worked out once a run, from the board
  // as it is now; the problem keeps it with what the run finds
  slide_.grid = sliding::searchGrid(puzzle_, problem_, o.sliding);
  if (slide_.grid > 1) {
    problem_.setSlideGrid(static_cast<unsigned int>(slide_.grid));
  }
  slide_.levelTiming = false;
}

// Every run's, of a puzzle of any kind.
void SolveSession::resetSlideRun(Mode mode) {
  // a sliding run's searches count on from the solve's runs before it
  // (nothing, once Solve has dropped them): the searches add what they
  // visit, so the count while it runs and once it stops is one
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    slideProgress_.store(sliding_ ? problem_.getSearched() : 0, std::memory_order_relaxed);
  }
  // The starts not searched completely are the solve's, not the run's: a
  // solve carried on after a pause (the crash recovery copy's save point
  // pauses and carries on by itself) still has them, and its last run
  // must not say that every arrangement was searched. Only a start that
  // waits to be searched again (a search out of disk) is taken off.
  if (mode != Mode::Resume) {
    slide_.startsCut = 0;
    slide_.memoryCut = false;
    slide_.memoryStates = 0;
  } else if (slide_.diskHalted && slide_.startsCut > 0) {
    slide_.startsCut--;
  }
  slide_.diskError.clear();
  slide_.diskHeldToFree = false;
  slide_.diskSaved = false;
  slide_.savedUnread.store(false, std::memory_order_release);
  slide_.resume = mode == Mode::Resume;
  slide_.diskHalted = false;
  slide_.savePointCut = false;
}

// On the run's thread, as its assembly search and take-aparts are over.
void SolveSession::noteSlideRun(const SolveOptions& o) {
  // whether "no solution" is proven, or the search gave up; not of a run
  // whose search a save point stopped, which is not through
  if (sliding_ && o.disassemble && !runStopRequested() && !slide_.savePointCut) {
    std::string note;
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      note = slidingSummary(o);
    }
    const std::lock_guard<std::mutex> lock(failureMutex_);
    note_ = std::move(note);
  }
}

// Of progress(): the slide search taking up its saved search, the part of
// a move that visits no arrangement, and what a stopping search saves.
void SolveSession::slidingProgress(SolveProgress& p, SolveState st, bool stopping) const {
  if (const std::int64_t since = slide_.pickingUpSince.load(std::memory_order_relaxed);
      isActive(st) && since != 0) {
    p.slidePickingUp = true;
    p.slidePickingUpMs =
        static_cast<std::uint64_t>(std::max<std::int64_t>(sliding::slideClockMs() - since, 0));
  }
  if (const sliding::SlidePhase part = slide_.phase.phase.load(std::memory_order_acquire);
      isActive(st) && part != sliding::SlidePhase::None) {
    const bool visited = part == sliding::SlidePhase::Visited;
    p.slidePhase = part;
    p.slidePhaseTotal = (visited ? slide_.phase.visitedTotal : slide_.phase.total)
                            .load(std::memory_order_relaxed);
    p.slidePhaseDone = std::min(
        (visited ? slide_.phase.visitedDone : slide_.phase.done).load(std::memory_order_relaxed),
        p.slidePhaseTotal);
    if (const std::int64_t since = slide_.phase.since.load(std::memory_order_relaxed); since != 0) {
      p.slidePhaseMs =
          static_cast<std::uint64_t>(std::max<std::int64_t>(sliding::slideClockMs() - since, 0));
    }
  }
  if (stopping) {
    p.slideStopping = slide_.searching.load(std::memory_order_relaxed) > 0;
    if (p.slideStopping && slide_.stop.saving.load(std::memory_order_acquire)) {
      p.stopSaving = true;
      p.stopTotalBytes = slide_.stop.totalBytes.load(std::memory_order_relaxed);
      p.stopBytes =
          std::min(slide_.stop.doneBytes.load(std::memory_order_relaxed), p.stopTotalBytes);
    }
  }
}

// The sliding branch of the assembly callback: Find Solutions keeps a start
// only when it slides to the goal, and of the starts that end in the same
// picture the one with the fewest moves.
bool SolveSession::onSlidingAssembly(Assembly assembly) {
  const SolveOptions& o = runOptions_;
  if (!o.justCount) {
    if (o.disassemble) {
      sliding::SlideSearch search = sliding::slideSearchFor(o.sliding);
      search.grid = slide_.grid;
      search.onDepth = [this](std::uint64_t depth) { onSlideDepth(depth); };
      // A save point does not cut the search short (stopSoft): the level
      // search stops at the end of the level it is on, saved, and its
      // start waits as after a pause; any other search runs to its end.
      search.stop = runToken();
      search.savePoint = assemblyToken();
      search.progress = &slideProgress_;
      search.depthProgress = &searchDepth_;
      search.pickingUpSince = &slide_.pickingUpSince;
      search.phase = &slide_.phase;
      search.sample = &sample_;
      search.pool = slide_.pool.get();
      search.memoryShare = o.memoryShare.get();
      search.stopProgress = &slide_.stop;
      search.resume = slide_.resume;
      search.onLog = [this](LogLevel level, const std::string& text) { addLog(level, text); };
      slide_.stop.saving.store(false, std::memory_order_relaxed);
      slide_.phase.end();
      slide_.searching.fetch_add(1, std::memory_order_relaxed);
      // The search reads the problem's definition and the shapes, which
      // nothing changes while the run is active; the lock is for what the
      // run writes, and is not held over a search.
      std::optional<Separation> path;
      try {
        path = sliding::findSlidePath(puzzle_, problem_, assembly, search);
      } catch (...) {
        slide_.searching.fetch_sub(1, std::memory_order_relaxed);
        throw;
      }
      slide_.searching.fetch_sub(1, std::memory_order_relaxed);
      // the progress counts the next start's depth afresh; the problem keeps
      // the deepest a search went (the run's end read the counter after it
      // was cleared, so a sliding problem kept none)
      const std::uint64_t reached = searchDepth_.exchange(0, std::memory_order_relaxed);
      if (path) {
        addLog(LogLevel::Info, "A start layout slides to the goal in " +
                                   std::to_string(path->getMoves()) + " moves (" +
                                   withCommas(search.visited) + " arrangements visited).");
      } else if (search.outcome == sliding::SlideOutcome::NoPath) {
        addLog(LogLevel::Info, "A start layout does not slide to the goal: all " +
                                   withCommas(search.visited) +
                                   " arrangements it reaches were searched.");
      }

      const std::lock_guard<std::mutex> lock(resultsMutex_);
      slide_.levelTiming = false;
      problem_.setSearchStats(problem_.getSearched(),
                              std::max(problem_.getSearchDepth(), reached),
                              problem_.getTriedApart());
      // paused in the middle of this start's search: it is searched again
      // on the next run instead of counted as one with no solution
      if (search.outcome == sliding::SlideOutcome::Stopped) {
        // stopped by a save point (no pause or abort was asked for): the
        // assembly search stops with it, and no other start is searched
        slide_.savePointCut = slide_.savePointCut || !runStopRequested();
        problem_.addPending(PendingAssembly{std::move(assembly), false, 0, 0});
        return true;
      }
      if (search.outcome == sliding::SlideOutcome::Memory ||
          search.outcome == sliding::SlideOutcome::Disk) {
        slide_.startsCut++;
        if (search.outcome == sliding::SlideOutcome::Disk) {
          slide_.diskError = search.error;
          slide_.diskHeldToFree = search.diskHeldToFree;
          slide_.diskSaved = search.saved;
          slide_.savedUnread.store(search.savedUnread, std::memory_order_release);
        }
        // Out of disk: the run pauses with this start pending, so Continue
        // carries its search on once there is more room (the starts after
        // it would only fill the disk with searches of their own). So does
        // a search that could not be saved: Continue searches its start
        // again from the beginning. And so does a start whose saved search
        // could not be read back: it is kept, and Continue tries it again
        // (the drive is back, or a version that reads it runs), where Solve
        // throws it away. Counted and passed over, the solve
        // would go on to its end and be kept as solved, with a start that
        // was never searched to its end.
        if (search.outcome == sliding::SlideOutcome::Disk) {
          slide_.diskHalted = true;
          problem_.addPending(PendingAssembly{std::move(assembly), false, 0, 0});
          return false;
        }
        if (search.outcome == sliding::SlideOutcome::Memory) {
          slide_.memoryCut = true;
          slide_.memoryStates = search.memoryStates;
          memoryShared_ = memoryShared_ || sharedNow(o);
        }
      }
      if (path) {
        // Several starts can slide to the same finished picture. A search
        // never walks in a circle, but a worse start takes more moves to
        // reach that picture. The shortest path to each ending is kept.
        const std::string key = sliding::finalPlacementKey(*path);
        const unsigned int moves = path->getMoves();
        int longer = -1;
        bool keep = true;
        for (unsigned int i = 0; i < problem_.getNumberOfSavedSolutions(); i++) {
          const Separation* old = problem_.getSavedSolution(i).getDisassembly();
          if (old == nullptr || sliding::finalPlacementKey(*old) != key) {
            continue;
          }
          if (old->getMoves() <= moves) {
            keep = false;
          } else {
            longer = static_cast<int>(i);
          }
          break;
        }
        if (keep) {
          // A shorter path to an ending already counted is the same
          // solution: it takes that solution's number. The count of
          // solutions is the count of distinct endings.
          std::uint64_t solNum = problem_.getNumSolutions();
          if (longer >= 0) {
            solNum = problem_.getSavedSolution(static_cast<unsigned int>(longer))
                         .getSolutionNumber();
            problem_.removeSolution(static_cast<unsigned int>(longer));
          }
          problem_.addSolution(std::move(assembly), std::move(*path),
                               problem_.getNumAssemblies(), solNum);
          if (longer < 0) {
            problem_.incNumSolutions();
          }
        }
      }
    } else {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.addSolution(std::move(assembly));
    }
  }
  const std::lock_guard<std::mutex> lock(resultsMutex_);
  problem_.incNumAssemblies();
  trimSavedSolutions(true);
  applyLiveSort();
  return true;
}

// The problem keeps the times: a move that ends one deeper than the times
// known is timed; any other depth is a search beginning (a new start, at
// 0) or carried on (from its saved depth), and the times are cut to it or
// padded with untimed moves (a search saved before the times were kept).
// Then the next move's clock starts.
void SolveSession::onSlideDepth(std::uint64_t depth) {
  const auto now = std::chrono::steady_clock::now();
  const std::lock_guard<std::mutex> lock(resultsMutex_);
  std::vector<std::uint64_t> ms = problem_.getSlideLevelMs();
  if (slide_.levelTiming && depth == ms.size() + 1) {
    ms.push_back(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - slide_.levelStart).count()));
  } else {
    ms.resize(static_cast<std::size_t>(depth), kUntimedMs);
  }
  problem_.setSlideLevelMs(std::move(ms));
  slide_.levelStart = now;
  slide_.levelTiming = true;
}

// Under resultsMutex_. Whether a missing solution is proven or the search
// gave up (solveThread_c::slidingSummary, word for word).
std::string SolveSession::slidingSummary(const SolveOptions& o) const {
  if (slide_.startsCut > 0) {
    std::string msg = withCommas(slide_.startsCut) +
                      (slide_.startsCut == 1 ? " start layout was" : " start layouts were") +
                      " not searched completely";
    if (!slide_.diskError.empty()) {
      msg += ". " + slide_.diskError.substr(0, slide_.diskError.size() - 1);
    } else {
      msg += ": the search reached its memory limit of " + withCommas(slide_.memoryStates) +
             " arrangements";
    }
    msg += ". There may be solutions the solver did not find.";
    if (slide_.savedUnread.load(std::memory_order_acquire)) {
      msg += " Continue tries to carry the saved search on again; Solve throws it away and "
             "searches from the beginning.";
    } else if (!o.settingsPlace.empty() && !slide_.diskError.empty()) {
      msg += slide_.diskHeldToFree
                 ? " Free some disk space to let it go further."
                 : " " + o.settingsPlace + " can give it more Sliding disk space.";
      msg += slide_.diskSaved ? " The search is saved: press Continue to carry it on."
                             : " The search could not be saved: Continue searches this start "
                               "again from the beginning.";
    } else if (!o.settingsPlace.empty() && slide_.memoryCut) {
      msg += memoryShared_ ? " " + sharedMemoryWords(o)
                           : " " + o.settingsPlace + " can give it more Search memory.";
    } else if (slide_.memoryCut && !o.sliding.highMemory) {
      msg += " Try Enable High Memory.";
    }
    return msg;
  }
  if (problem_.getNumSolutions() == 0 && problem_.getNumAssemblies() > 0 && slide_.grid > 1) {
    return "Every arrangement optimized paths reach was searched: with the pieces stopping "
           "only on a " +
           std::to_string(slide_.grid) +
           "-cell grid, none reaches the goal. A full search, which tries every stop, may still "
           "find a solution.";
  }
  if (problem_.getNumSolutions() == 0 && problem_.getNumAssemblies() > 0) {
    std::string msg =
        "Every reachable arrangement was searched: this puzzle has no solution "
        "with the current settings.";
    if (!o.sliding.nested && !o.sliding.partialNested) {
      msg += " If a piece can carry pieces that sit inside it, try Allow Nested Slides.";
    } else if (!o.sliding.partialNested) {
      msg += " If a piece can carry pieces that stick out of it, try Allow Partially Nested.";
    }
    // the original's trays were one layer deep, so its text stays for those
    if (!o.sliding.verticalMoves && problem_.resultValid() &&
        problem_.resultShape(puzzle_).getZ() > 1) {
      msg += " If a piece can be lifted over the others, try Allow Vertical Moves.";
    }
    return msg;
  }
  return "";
}

}  // namespace sl
