// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/python/py_solution_iterator.cpp. Sliding and stacking
// puzzles, which the original's iterator assembled as brick puzzles, are
// new: they run through a SolveSession.
#include "solution_iterator.h"

#include "shapelogic/app/solve_setup.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"

#include <pybind11/pybind11.h>

#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace py = pybind11;

namespace sl::python {

namespace {

// The placements of an assembly, as the original listed them.
std::vector<PyPlacement> placementsOf(const Assembly& a) {
  std::vector<PyPlacement> out;
  out.reserve(a.placementCount());
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    PyPlacement p;
    p.pieceId = i;
    p.isPlaced = a.isPlaced(i);
    if (p.isPlaced) {
      p.x = a.getX(i);
      p.y = a.getY(i);
      p.z = a.getZ(i);
      p.transformation = a.getTransformation(i);
    } else {
      p.transformation = 0xff;
    }
    out.push_back(p);
  }
  return out;
}

void setDisassembly(PySolution& sol, const Separation& s) {
  sol.hasDisassembly = true;
  sol.totalMoves = s.sumMoves();
  sol.level = s.getMoves();
  sol.movesText = s.movesText();
}

// A stacking path: three frames a transfer (lift, cross, drop); counted in
// transfers, as bt and the engine count them (the original's logicalMoves)
void setStackingPath(PySolution& sol, const Separation& s) {
  const unsigned int transfers = stacking::logicalMoves(s);
  sol.hasDisassembly = true;
  sol.totalMoves = transfers;
  sol.level = transfers;
  sol.movesText = std::to_string(transfers);
}

}  // namespace

SolutionIterator::SolutionIterator(const Puzzle& puzzle, unsigned int problemIdx,
                                   IteratorOptions options)
    : puzzle_(std::make_unique<Puzzle>(puzzle)),
      problemIdx_(problemIdx),
      options_(std::move(options)) {
  if (problemIdx_ >= puzzle_->getNumberOfProblems()) {
    throw std::out_of_range("Problem index out of range");
  }
  worker_ = std::thread(&SolutionIterator::workerRun, this);
}

SolutionIterator::~SolutionIterator() {
  try {
    stop();
  } catch (...) {
    // a destructor has no one to tell, and the worker may be left running
    // on this object: the end an exception out of a destructor always had
    std::terminate();
  }
}

std::uint64_t SolutionIterator::getIterations() const {
  if (const Assembler* a = activeAssembler_.load(std::memory_order_acquire)) {
    return a->iterations();
  }
  if (const SolveSession* s = activeSession_.load(std::memory_order_acquire)) {
    return s->stats().iterations;
  }
  return iterations_.load(std::memory_order_relaxed);
}

void SolutionIterator::stop() {
  bool expected = false;
  if (!stopRequested_.compare_exchange_strong(expected, true)) {
    return;
  }
  stopSource_.request(StopKind::Abort);
  {
    const std::lock_guard<std::mutex> lock(queueMutex_);
    canPush_.notify_all();
    canPop_.notify_all();
  }
  if (worker_.joinable()) {
    // The worker can take a while to see the stop (building the matrix,
    // the wait after an abort): the other Python threads go on meanwhile.
    // Called with the GIL (the binding, next(), the destructor as Python
    // drops the iterator) or without it; the join touches no Python object.
    if (PyGILState_Check() != 0) {
      const py::gil_scoped_release release;
      worker_.join();
    } else {
      worker_.join();
    }
  }
}

void SolutionIterator::pushItem(QueueItem&& item) {
  std::unique_lock<std::mutex> lock(queueMutex_);
  canPush_.wait(lock, [this] { return queue_.size() < kMaxQueueSize || stopRequested_.load(); });
  if (stopRequested_.load()) {
    return;
  }
  queue_.push_back(std::move(item));
  canPop_.notify_one();
}

PySolution SolutionIterator::next() {
  while (true) {
    std::optional<QueueItem> item;
    bool ended = false;
    {
      // the GIL is let go before the queue's lock is taken, so a thread
      // that holds the lock never waits for the GIL
      const py::gil_scoped_release release;
      std::unique_lock<std::mutex> lock(queueMutex_);
      if (queue_.empty() && !finished_.load() && !stopRequested_.load()) {
        canPop_.wait_for(lock, std::chrono::milliseconds(50));
      }
      if (!queue_.empty()) {
        item = std::move(queue_.front());
        queue_.pop_front();
        canPush_.notify_one();
      } else if (finished_.load() || stopRequested_.load()) {
        ended = true;
      }
    }
    if (item) {
      switch (item->type) {
        case QueueItem::Type::Solution:
          return std::move(item->solution);
        case QueueItem::Type::Finished:
          finished_.store(true);
          throw py::stop_iteration();
        case QueueItem::Type::Error:
          finished_.store(true);
          throw std::runtime_error(item->errorMessage);
      }
    }
    if (ended) {
      throw py::stop_iteration();
    }
    if (PyErr_CheckSignals() != 0) {
      stop();
      throw py::error_already_set();
    }
  }
}

void SolutionIterator::workerRun() {
  try {
    if (stopRequested_.load(std::memory_order_acquire)) {
      return;
    }
    Problem& problem = puzzle_->getProblem(problemIdx_);
    if (sliding::isSliding(*puzzle_) || stacking::isStacking(*puzzle_)) {
      runSession(problem);
    } else {
      runAssembler(problem);
    }
  } catch (const std::exception& e) {
    activeAssembler_.store(nullptr, std::memory_order_release);
    activeSession_.store(nullptr, std::memory_order_release);
    pushItem(QueueItem{QueueItem::Type::Error, {}, e.what()});
  } catch (...) {
    activeAssembler_.store(nullptr, std::memory_order_release);
    activeSession_.store(nullptr, std::memory_order_release);
    pushItem(
        QueueItem{QueueItem::Type::Error, {}, "Unknown C++ exception occurred during solving"});
  }
}

void SolutionIterator::runAssembler(Problem& problem) {
  // the original's assembler failed this assertion
  if (!problem.resultValid()) {
    pushItem(QueueItem{QueueItem::Type::Error, {}, "problem.resultValid()"});
    return;
  }
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    puzzle_->getShape(i).initHotspot();
  }

  // The matrix as every solve builds it (solveSetup; cleanup item 15.2:
  // this copy of the rules lacked the tilted tray's, so a Tilted Square
  // problem whose pieces stay face up was solved with them turned over).
  MatrixOptions base;
  base.keepMirrors = options_.keepMirror;
  base.keepRotations = options_.keepRotations;
  const SolveSetup setup = solveSetup(*puzzle_, problem, std::move(base), SolverType::Classic,
                                      SetupFlags{false, false, options_.flipPieces});

  // gridType_c::findAssembler: assembler 0 when every part is one piece,
  // else assembler 1
  assembler_ = makeAssembler(*puzzle_, problem, setup.solver);
  activeAssembler_.store(assembler_.get(), std::memory_order_release);

  // the iterator's own stop ends the build (a closed iterator waited for it)
  if (const AssemblerError err = assembler_->createMatrix(setup.matrix, stopSource_.token());
      err != AssemblerError::None) {
    activeAssembler_.store(nullptr, std::memory_order_release);
    if (err == AssemblerError::Stopped) {
      return;
    }
    pushItem(QueueItem{QueueItem::Type::Error,
                       {},
                       std::string("Matrix creation error in assembler: ") +
                           std::string(errorMessage(err))});
    return;
  }
  if (stopRequested_.load(std::memory_order_acquire)) {
    activeAssembler_.store(nullptr, std::memory_order_release);
    return;
  }
  if (options_.reduce) {
    (void)assembler_->reduce(stopSource_.token());
  }
  if (stopRequested_.load(std::memory_order_acquire)) {
    activeAssembler_.store(nullptr, std::memory_order_release);
    return;
  }

  const unsigned int budget = options_.threads > 0 ? options_.threads : hardwareThreads();
  // the take-apart's levels spread over threads of their own: the
  // assembly's are busy (bt's solve does the same)
  std::unique_ptr<ThreadPool> takeApartPool;
  std::unique_ptr<Disassembler> disassembler;
  if (options_.disassemble && Disassembler::canDisassemble(*puzzle_)) {
    takeApartPool = std::make_unique<ThreadPool>(budget);
    DisassemblerOptions d;
    d.pool = takeApartPool.get();
    disassembler = std::make_unique<Disassembler>(*puzzle_, problem, d);
  }

  ThreadPool pool(budget);
  SearchOptions search;
  search.threads = budget;
  search.pool = &pool;

  std::uint64_t assemblies = 0;
  std::uint64_t solutions = 0;
  const StopToken stop = stopSource_.token();

  (void)assembler_->assemble(
      [&](const Assembly& a) {
        if (stopRequested_.load(std::memory_order_relaxed)) {
          return false;
        }
        assemblies++;

        PySolution sol;
        sol.assemblyNumber = assemblies;
        sol.placements = placementsOf(a);

        if (disassembler) {
          const TakeApartResult r = disassembler->disassemble(a, stop);
          if (r.tooDeep) {
            // raised in Python, as the errors of the matrix are
            pushItem(QueueItem{QueueItem::Type::Error, {}, Disassembler::tooDeepMessage()});
            return false;
          }
          if (!r.separation) {
            return !stopRequested_.load(std::memory_order_relaxed);
          }
          solutions++;
          sol.solutionNumber = solutions;
          setDisassembly(sol, *r.separation);
        } else {
          sol.solutionNumber = assemblies;
        }

        pushItem(QueueItem{QueueItem::Type::Solution, std::move(sol), ""});
        return !stopRequested_.load(std::memory_order_relaxed);
      },
      search, stop);

  iterations_.store(assembler_->iterations(), std::memory_order_relaxed);
  activeAssembler_.store(nullptr, std::memory_order_release);

  pushItem(QueueItem{QueueItem::Type::Finished, {}, ""});
}

void SolutionIterator::runSession(Problem& problem) {
  const bool stack = stacking::isStacking(*puzzle_);
  // what the app checks and sets up before a solve (Document::solveLaunch)
  if (const Result<void> refused = app::solveRefusal(*puzzle_, problem); !refused) {
    pushItem(QueueItem{QueueItem::Type::Error, {}, refused.error().message});
    return;
  }
  // (a sliding problem's holes are the session's to set up as it starts)
  app::prepareShapesForSolving(*puzzle_);

  SolveOptions so;
  // a stacking run is its search: Find Solutions is what it does
  so.disassemble = options_.disassemble || stack;
  so.reduce = options_.reduce;
  so.matrix.keepMirrors = options_.keepMirror;
  so.matrix.keepRotations = options_.keepRotations;
  so.flipPieces = options_.flipPieces.value_or(storedFlipPieces(problem));
  so.solutionLimit = 0;
  so.threads = options_.threads;
  so.sliding = options_.sliding;
  so.stacking = options_.stacking;

  session_ = std::make_unique<SolveSession>(*puzzle_, problem, so);
  activeSession_.store(session_.get(), std::memory_order_release);
  if (const Result<void> r = session_->start(); !r) {
    activeSession_.store(nullptr, std::memory_order_release);
    pushItem(QueueItem{QueueItem::Type::Error, {}, r.error().message});
    return;
  }
  while (isActive(session_->state())) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    canPush_.wait_for(lock, std::chrono::milliseconds(50),
                      [this] { return stopRequested_.load(); });
    if (stopRequested_.load()) {
      lock.unlock();
      session_->abort();
      break;
    }
  }
  session_->wait();
  iterations_.store(session_->stats().iterations, std::memory_order_relaxed);
  activeSession_.store(nullptr, std::memory_order_release);
  if (stopRequested_.load()) {
    return;
  }
  if (session_->state() == SolveState::Error) {
    const std::optional<SolveFailure> f = session_->failure();
    pushItem(QueueItem{QueueItem::Type::Error, {}, f ? f->message : "the solve failed"});
    return;
  }
  // the run has ended: nothing writes the problem any more
  for (unsigned int i = 0; i < problem.getNumberOfSavedSolutions(); i++) {
    const Solution& s = problem.getSavedSolution(i);
    PySolution sol;
    sol.assemblyNumber = s.getAssemblyNumber();
    sol.solutionNumber = s.getSolutionNumber();
    sol.placements = placementsOf(s.getAssembly());
    if (const Separation* sep = s.getDisassembly()) {
      if (stack) {
        setStackingPath(sol, *sep);
      } else {
        setDisassembly(sol, *sep);
      }
    }
    pushItem(QueueItem{QueueItem::Type::Solution, std::move(sol), ""});
  }
  pushItem(QueueItem{QueueItem::Type::Finished, {}, ""});
}

}  // namespace sl::python
