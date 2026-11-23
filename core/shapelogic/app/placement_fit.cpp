// SPDX-License-Identifier: GPL-3.0-or-later
// New code (decision 0045, stage G of docs/TILTED-SQUARE-PLAN.md).
#include "shapelogic/app/placement_fit.h"

#include "shapelogic/app/document_internal.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <exception>
#include <utility>

namespace bt::app {

PlacementFit::PlacementFit(std::unique_ptr<Puzzle> scratch, const tilted::Placements& boards,
                           SolveOptions options)
    : puzzle_(std::move(scratch)), options_(std::move(options)), complete_(boards.complete) {
  BT_ASSERT(puzzle_->tray().has_value() && puzzle_->getNumberOfShapes() >= 1 &&
            puzzle_->getNumberOfProblems() == 1);
  // the count alone: nothing kept, nothing taken apart
  options_.justCount = true;
  options_.disassemble = false;
  options_.keepAssemblies = false;
  boards_.reserve(boards.boards.size());
  for (const tilted::Placed& b : boards.boards) {
    boards_.push_back({b, false, 0});
  }
  thread_ = std::thread([this] { run(); });
}

PlacementFit::~PlacementFit() {
  stop();
  wait();
}

void PlacementFit::stop() {
  stop_.request(StopKind::Abort);
  const std::lock_guard<std::mutex> lock(mutex_);
  if (session_ != nullptr) {
    session_->pause();
  }
}

void PlacementFit::wait() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool PlacementFit::tryBoard(std::size_t index) {
  // the board of this placement, in place of the last one's
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): asserted by the constructor
  tilted::Tray tray = *puzzle_->tray();
  tray.placement = tilted::Tray::Placement::Custom;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    tray.shiftX = boards_[index].board.shiftX;
    tray.shiftY = boards_[index].board.shiftY;
  }
  const tilted::Layout lay = tilted::layout(tray);
  puzzle_->setTray(std::move(tray));
  fillTrayBoard(puzzle_->getShape(0), lay);
  Problem& problem = puzzle_->getProblem(0);
  problem.removeAllSolutions();

  SolveSession session(*puzzle_, problem, options_);
  // the solve is seen by stop() and progress() while it is here
  struct Shown {
    PlacementFit& fit;
    Shown(PlacementFit& f, SolveSession& s) : fit(f) {
      const std::lock_guard<std::mutex> lock(fit.mutex_);
      fit.session_ = &s;
    }
    ~Shown() {
      const std::lock_guard<std::mutex> lock(fit.mutex_);
      fit.session_ = nullptr;
    }
    Shown(const Shown&) = delete;
    Shown& operator=(const Shown&) = delete;
  };
  SolveState state = SolveState::Idle;
  std::uint64_t assemblies = 0;
  std::optional<std::string> failure;
  {
    const Shown shown(*this, session);
    if (const Result<void> started = session.start(); !started) {
      failure = started.error().message;
    } else {
      // a stop asked for before the solve began found nothing to pause
      if (stop_.stopRequested()) {
        session.pause();
      }
      session.wait();
      state = session.state();
      assemblies = session.progress().assemblies;
      if (state == SolveState::Error) {
        const std::optional<SolveFailure> why = session.failure();
        // A board with fewer cells than the pieces, or one a piece fits
        // nowhere on, is an error to a solve; here it is a board that takes
        // no pieces, and the next one is tried.
        if (why && (why->assembler == AssemblerError::TooManyUnits ||
                    why->assembler == AssemblerError::CannotPlace)) {
          state = SolveState::Finished;
          assemblies = 0;
        } else {
          failure = why ? why->message : std::string("the solve failed");
        }
      }
    }
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  if (failure) {
    failure_ = std::move(failure);
    return false;
  }
  boards_[index].assemblies = assemblies;
  boards_[index].counted = state == SolveState::Finished;
  return boards_[index].counted;
}

void PlacementFit::run() {
  try {
    for (std::size_t k = 0; k < boards_.size(); k++) {
      if (stop_.stopRequested() || !tryBoard(k)) {
        break;
      }
      done_.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    const std::lock_guard<std::mutex> lock(mutex_);
    failure_ = e.what();
  }
  active_.store(false, std::memory_order_release);
}

PlacementFitProgress PlacementFit::progress() const {
  PlacementFitProgress p;
  p.active = active();
  p.complete = complete_;
  p.done = done_.load(std::memory_order_relaxed);
  const std::lock_guard<std::mutex> lock(mutex_);
  p.boards = boards_;
  p.failure = failure_;
  p.stopped = !p.active && p.done < boards_.size();
  if (session_ != nullptr) {
    const SolveProgress now = session_->progress();
    p.current = now.assemblyFraction;
    // the board under way: what it has found so far
    if (p.done < p.boards.size() && !p.boards[p.done].counted) {
      p.boards[p.done].assemblies = now.assemblies;
    }
  }
  return p;
}

}  // namespace bt::app
