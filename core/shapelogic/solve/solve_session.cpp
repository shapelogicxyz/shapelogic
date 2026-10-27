// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/solvethread.cpp (solveThread_c: run, runStacking, the
// take-apart queue and its workers, processDisassembly, the assembly
// callback with its sliding branch, slidingSummary, trimSavedSolutions, the
// progress snapshot and the statistics) and from the original GUI: Solve,
// Prepare, Continue and Abort, with their stacking branches
// (src/gui/mainwindow_solve.cpp), recordSearchStats (src/gui/mainwindow.cpp).
#include "shapelogic/solve/solve_session.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace bt {

namespace {

std::int64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::uint64_t msSince(std::int64_t originNs) {
  const std::int64_t ns = nowNs() - originNs;
  return ns > 0 ? static_cast<std::uint64_t>(ns / 1000000) : 0;
}

// The take-apart workers (chooseDisasmWorkerCount): asked for, or with a
// thread count the take-apart share of it, or every core less two (for the
// assembly and the caller), at most 16 so that a large machine does not run
// dozens of searches at once.
unsigned int chooseWorkers(const SolveOptions& o, unsigned int budget) {
  if (o.takeApartWorkers > 0) {
    return std::min(o.takeApartWorkers, 64U);
  }
  if (o.threads > 0) {
    return splitThreads(budget).disassembly;
  }
  if (budget <= 2) {
    return 1;
  }
  return std::min(budget - 2, 16U);
}

// The assembly search's cores in the take-apart pool's load, for as long
// as it runs (load_c of the solve thread). While it waits at a full queue
// it gives them up (enqueue), and `held` says how many are left to return.
class AssemblyLoad {
 public:
  AssemblyLoad(ThreadPool* pool, int cores, std::atomic<int>& held) : pool_(pool), held_(held) {
    if (pool_ != nullptr) {
      pool_->addLoad(cores);
      held_.store(cores, std::memory_order_release);
    }
  }
  ~AssemblyLoad() {
    if (pool_ != nullptr) {
      pool_->addLoad(-held_.exchange(0, std::memory_order_acq_rel));
    }
  }
  AssemblyLoad(const AssemblyLoad&) = delete;
  AssemblyLoad& operator=(const AssemblyLoad&) = delete;

 private:
  ThreadPool* pool_;
  std::atomic<int>& held_;
};

}  // namespace

SolveSession::SolveSession(Puzzle& puzzle, Problem& problem, SolveOptions options)
    : puzzle_(puzzle),
      problem_(problem),
      options_(std::move(options)),
      sliding_(sliding::isSliding(puzzle)),
      stacking_(stacking::isStacking(puzzle)) {}

SolveSession::~SolveSession() {
  pause();
  const std::lock_guard<std::mutex> control(controlMutex_);
  joinRunner();
}

Result<void> SolveSession::setOptions(const SolveOptions& options) {
  const std::lock_guard<std::mutex> control(controlMutex_);
  if (isActive(state_.load())) {
    return fail(Errc::InvalidState, "the problem is being solved");
  }
  options_ = options;
  return {};
}

Result<void> SolveSession::start() { return launch(Mode::Start); }

Result<void> SolveSession::prepare() { return launch(Mode::Prepare); }

Result<void> SolveSession::resume() { return launch(Mode::Resume); }

void SolveSession::requestStop(const StopSource& source, StopKind kind) {
  // With the queue's mutex held: a thread that has looked at the stop under
  // it and is about to sleep cannot miss the wake-up below.
  {
    const std::lock_guard<std::mutex> lock(queueMutex_);
    source.request(kind);
  }
  queueCv_.notify_all();
  spaceCv_.notify_all();
}

void SolveSession::pause() {
  StopSource s;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    s = runStop_;
  }
  requestStop(s, StopKind::Pause);
}

void SolveSession::pauseAtSavePoint() {
  const SolveState st = state_.load();
  if (stacking_ || (st != SolveState::Assembling && st != SolveState::Draining)) {
    return;
  }
  StopSource s;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    s = assemblyStop_;
  }
  s.request(StopKind::Pause);
}

void SolveSession::abort() {
  StopSource s;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    s = runStop_;
  }
  requestStop(s, StopKind::Abort);

  const std::lock_guard<std::mutex> control(controlMutex_);
  joinRunner();
  // A run that ended on its own before it saw the request ends Paused,
  // Finished or Error: it is thrown away all the same.
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    problem_.removeAllSolutions();
  }
  dropAssembler();
  // a stacking search saves itself as it stops: that goes too (cb_BtnAbort)
  if (stacking_) {
    panex::discardSaved(puzzle_, problem_, options_.stacking.workDir);
  }
  if (state_.load() != SolveState::Idle) {
    state_.advance(SolveState::Idle);
  }
}

void SolveSession::wait() {
  const std::lock_guard<std::mutex> control(controlMutex_);
  joinRunner();
}

void SolveSession::joinRunner() {
  if (runner_.joinable()) {
    runner_.join();
  }
}

void SolveSession::dropAssembler() {
  std::unique_ptr<Assembler> old;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    if (published_ != nullptr) {
      lastFinished_.store(published_->finished(), std::memory_order_relaxed);
      published_ = nullptr;
    }
    old = std::move(assembler_);
  }
}

void SolveSession::publishAssembler(Assembler* assembler) {
  const std::lock_guard<std::mutex> lock(runMutex_);
  if (assembler == nullptr && published_ != nullptr) {
    lastFinished_.store(published_->finished(), std::memory_order_relaxed);
  }
  published_ = assembler;
}

Result<void> SolveSession::launch(Mode mode) {
  const std::lock_guard<std::mutex> control(controlMutex_);
  if (isActive(state_.load())) {
    return fail(Errc::InvalidState, "the problem is being solved");
  }
  joinRunner();

  if (stacking_) {
    return launchStacking(mode);
  }

  // what the original GUI checked before it made the solve thread
  const GridType& grid = puzzle_.getGridType();
  if ((grid.getCapabilities() & kCapAssemble) == 0) {
    return fail(Errc::Unsupported, "this space grid doesn't have an assembler");
  }
  // a sliding puzzle's "take apart" is the slide search
  if (options_.disassemble && !sliding_ && !Disassembler::canDisassemble(puzzle_)) {
    return fail(Errc::Unsupported,
                "this space grid doesn't have a disassembler: turn off disassembly");
  }
  if (!problem_.resultValid()) {
    return fail(Errc::InvalidArgument, "a result shape must be defined");
  }
  if (problem_.getNumberOfPieces() == 0) {
    return fail(Errc::InvalidArgument, "the problem has no pieces");
  }

  // A sliding start is judged against a fixed goal, so a rotated or
  // mirrored start is a different start, not a duplicate: every one is
  // kept; and the start locks are colours the matrix matches strictly (the
  // solve thread's constructor and run).
  SolveOptions o = options_;
  if (sliding_) {
    // the sliding menu lists its own solvers: the assembler is Classic's
    o.solver = SolverType::Classic;
    o.matrix.keepMirrors = true;
    o.matrix.keepRotations = true;
    o.matrix.completeRotations = false;
    o.matrix.strictColors = true;
  }

  SolveState st = state_.load();
  if (mode == Mode::Resume && st == SolveState::Finished) {
    return fail(Errc::InvalidState, "the problem is solved: start() solves it again");
  }
  if (mode != Mode::Resume) {
    // Solve and Prepare start afresh
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
    }
    dropAssembler();
  }
  if (st == SolveState::Finished || st == SolveState::Error) {
    state_.advance(SolveState::Idle);
    st = SolveState::Idle;
  }


  // The search to carry on with (the problem's assembler in the original).
  // A matrix bakes the colour rule in: made with the other one, or unknown
  // (a saved position does not say), the solve starts over.
  bool reuse = false;
  if (assembler_ && problem_.getSolveState() == ProblemState::Solving) {
    if (assembler_->usesStrictColors() == o.matrix.strictColors) {
      reuse = true;
      // this run's mirror and rotation filter (the solve thread set it on
      // the problem's assembler before every run)
      assembler_->applySolutionFilterFlags(o.matrix.keepMirrors, o.matrix.keepRotations,
                                           o.matrix.completeRotations);
    } else {
      {
        const std::lock_guard<std::mutex> lock(resultsMutex_);
        problem_.removeAllSolutions();
      }
      dropAssembler();
    }
  } else {
    dropAssembler();
    if (o.matrix.strictColors) {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
    }
  }
  if (!reuse && !canStartSolving(problem_)) {
    return fail(Errc::InvalidState,
                "the problem has no solve to carry on (solved, edited, or saved without its "
                "position): start() solves it again");
  }
  // an assembler is only kept across a pause
  BT_ASSERT(!reuse || st == SolveState::Paused);

  // What a sliding run sets up first (the solve thread did it as it began):
  // the problem's maximum of holes and the start locks; once nothing can
  // refuse the run any more. The owner makes sure no other run reads the
  // shapes meanwhile (the document does it first, and this changes
  // nothing then).
  if (sliding_) {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    (void)sliding::prepareForSolving(puzzle_, problem_);
  }

  Plan plan;
  plan.options = o;
  plan.prepare = !reuse;
  plan.prepareOnly = mode == Mode::Prepare;

  // a new run: its own stop, nothing carried over from the last one
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    runStop_ = StopSource();
    assemblyStop_ = runStop_.child();
    runOptions_ = o;
    plan.assemblyToken = assemblyStop_.token();
  }
  if (o.justCount) {
    action_ = o.disassemble ? Action::CountSolutions : Action::CountAssemblies;
  } else {
    action_ = o.disassemble ? Action::SaveSolutions : Action::SaveAssemblies;
  }
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    failure_.reset();
    workerError_ = nullptr;
    note_.clear();
  }
  // a sliding run counts its searches afresh (a new solve thread did)
  slideProgress_.store(0, std::memory_order_relaxed);
  searchDepth_.store(0, std::memory_order_relaxed);
  slideStartsCut_ = 0;
  slideMemoryCut_ = false;
  slideMemoryStates_ = 0;
  slideDiskError_.clear();
  slideDiskHeldToFree_ = false;
  assemblerThreads_.store(1, std::memory_order_relaxed);
  workerCount_.store(0, std::memory_order_relaxed);
  pending_.store(0, std::memory_order_relaxed);
  peakPending_.store(0, std::memory_order_relaxed);
  completed_.store(0, std::memory_order_relaxed);
  disasmMsTotal_.store(0, std::memory_order_relaxed);
  inseparable_.store(0, std::memory_order_relaxed);
  prepareMs_.store(0, std::memory_order_relaxed);
  reduceMs_.store(0, std::memory_order_relaxed);
  assemblyMs_.store(0, std::memory_order_relaxed);
  drainMs_.store(0, std::memory_order_relaxed);
  phase_.store(Phase::None, std::memory_order_relaxed);
  progressShown_.store(0, std::memory_order_relaxed);
  // the last solve's assembler is gone; this run's has done nothing yet
  lastFinished_.store(0, std::memory_order_relaxed);
  runOriginNs_.store(nowNs(), std::memory_order_relaxed);
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    runTimeAdded_ = false;
  }

  // The first move is made here, before the thread exists, so that the
  // caller sees an active state as soon as this returns.
  state_.advance(reuse ? SolveState::Assembling : SolveState::Preparing);
  runner_ = std::thread([this, plan = std::move(plan)] { run(plan); });
  return {};
}

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
  // The hotspots where a stacking solve has them: a disc's at its centre
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
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    runStop_ = StopSource();
    assemblyStop_ = runStop_.child();
    runOptions_ = options_;
    plan.assemblyToken = assemblyStop_.token();
  }
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    failure_.reset();
    workerError_ = nullptr;
    note_.clear();
  }
  slideProgress_.store(0, std::memory_order_relaxed);
  searchDepth_.store(0, std::memory_order_relaxed);
  searchTraced_.store(0, std::memory_order_relaxed);
  assemblerThreads_.store(0, std::memory_order_relaxed);
  workerCount_.store(0, std::memory_order_relaxed);
  pending_.store(0, std::memory_order_relaxed);
  peakPending_.store(0, std::memory_order_relaxed);
  completed_.store(0, std::memory_order_relaxed);
  disasmMsTotal_.store(0, std::memory_order_relaxed);
  inseparable_.store(0, std::memory_order_relaxed);
  prepareMs_.store(0, std::memory_order_relaxed);
  reduceMs_.store(0, std::memory_order_relaxed);
  assemblyMs_.store(0, std::memory_order_relaxed);
  drainMs_.store(0, std::memory_order_relaxed);
  phase_.store(Phase::None, std::memory_order_relaxed);
  progressShown_.store(0, std::memory_order_relaxed);
  lastFinished_.store(0, std::memory_order_relaxed);
  runOriginNs_.store(nowNs(), std::memory_order_relaxed);
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    runTimeAdded_ = false;
  }

  state_.advance(SolveState::Preparing);
  runner_ = std::thread([this, plan = std::move(plan)] { run(plan); });
  return {};
}

// ---- the run -----------------------------------------------------------------------

void SolveSession::run(const Plan& plan) {
  if (stacking_) {
    try {
      runStacking(plan);
    } catch (...) {
      failRun(std::current_exception());
    }
    return;
  }
  try {
    // The matrix places the pieces by their hotspots and the take-apart
    // reads the shapes as they are: reset before the matrix, as burrTxt and
    // burrTxt2 do (the solve thread reset them after it, and a shape with
    // its hotspot elsewhere was taken apart where it was not placed), and
    // on every run, as the solve thread did (a matrix kept across a pause
    // was made with them reset). Before the disassemblers are made: a
    // hotspot change clears the caches they warm, so only a hotspot that
    // is not where initHotspot() puts it is reset (warming them again on
    // every run made each run slow to start; on the sphere grid that place
    // is the sphere nearest the origin, not the origin).
    for (unsigned int i = 0; i < puzzle_.getNumberOfShapes(); i++) {
      Voxel& shape = puzzle_.getShape(i);
      if (!shape.hasInitialHotspot()) {
        shape.initHotspot();
      }
    }
    if (plan.prepare && !prepareSearch(plan)) {
      return;
    }
    if (plan.prepareOnly) {
      {
        const std::lock_guard<std::mutex> lock(resultsMutex_);
        storePosition(problem_, *assembler_);
      }
      state_.advance(SolveState::Paused);
      return;
    }
    assembleAndTakeApart(plan);
  } catch (...) {
    failRun(std::current_exception());
  }
}

bool SolveSession::prepareSearch(const Plan& plan) {
  const SolveOptions& o = plan.options;

  beginPhase(Phase::Prepare);

  std::unique_ptr<Assembler> a = makeAssembler(puzzle_, problem_, o.solver);
  const AssemblerKind kind = chooseAssembler(problem_, o.solver);

  // others read its progress while it is made; it is withdrawn before it
  // can go, however this ends
  struct Withdraw {
    SolveSession& s;
    bool armed = true;
    ~Withdraw() {
      if (armed) {
        s.publishAssembler(nullptr);
      }
    }
  } withdraw{*this};
  publishAssembler(a.get());

  const AssemblerError err = a->createMatrix(o.matrix);
  endPhase(prepareMs_);
  if (err != AssemblerError::None) {
    {
      const std::lock_guard<std::mutex> lock(failureMutex_);
      failure_ = SolveFailure{err, a->errorParam(), std::string(errorMessage(err)), {}, 0};
    }
    state_.advance(SolveState::Error);
    return false;
  }

  if (!runStopRequested() && o.reduce) {
    beginPhase(Phase::Reduce);
    (void)a->reduce(runToken());
    endPhase(reduceMs_);
  }

  if (runStopRequested()) {
    // the new search is not kept: the next run prepares again
    if (runStopKind() == StopKind::Abort) {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
      state_.advance(SolveState::Idle);
    } else {
      // a solve carried on keeps the time this run spent preparing
      {
        const std::lock_guard<std::mutex> lock(resultsMutex_);
        if (problem_.getSolveState() == ProblemState::Solving) {
          problem_.addTimeMs(runMs());
        }
        runTimeAdded_ = true;
      }
      state_.advance(SolveState::Paused);
    }
    return false;
  }

  // Start the solve, or go to the position the problem holds. One that
  // cannot be restored has dropped everything the problem held: the solve
  // ends in Error, and the next starts from scratch.
  AssemblerError restored = AssemblerError::None;
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    restored = beginSolving(problem_, *a);
  }
  if (restored != AssemblerError::None) {
    {
      const std::lock_guard<std::mutex> lock(failureMutex_);
      failure_ = SolveFailure{restored, 0, std::string(errorMessage(restored)), {}, 0};
    }
    state_.advance(SolveState::Error);
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    assembler_ = std::move(a);
    assemblerKind_ = kind;
    published_ = assembler_.get();
  }
  withdraw.armed = false;
  return true;
}

void SolveSession::assembleAndTakeApart(const Plan& plan) {
  const SolveOptions& o = plan.options;
  if (state_.load() == SolveState::Preparing) {
    state_.advance(SolveState::Assembling);
  }

  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    // the position is the running assembler's now; the one a pause wrote
    // would be behind it
    problem_.clearAssemblerState();
    // solutions found with turns are saved where older versions skip them
    if (o.checkRotations) {
      problem_.setSolutionsWithRotations(true);
    }
    // a solve that carries on thins its kept solutions as far as one that
    // had found as many would have (start() of the solve thread)
    dropMultiplicator_ = 1;
    std::uint64_t n = 0;
    if (action_ == Action::SaveAssemblies || action_ == Action::CountAssemblies) {
      n = problem_.numAssembliesKnown() ? problem_.getNumAssemblies() : 0;
    } else {
      n = problem_.numSolutionsKnown() ? problem_.getNumSolutions() : 0;
    }
    const std::uint64_t drop = std::max(o.solutionDrop, 1U);
    while (o.solutionLimit != 0 && n + drop > 2 * std::uint64_t{o.solutionLimit} * drop) {
      dropMultiplicator_ *= 2;
      n = (n + 1) / 2;
    }
  }

  // The threads: with a count, the assembly search gets its share of it
  // while it runs beside the take-apart; without, every core, as the
  // original without a thread limit.
  const unsigned int budget = o.threads > 0 ? std::min(o.threads, kMaxThreads) : hardwareThreads();
  unsigned int requested = 0;
  if (o.threads > 0) {
    requested = o.disassemble ? splitThreads(budget).assembly : budget;
  }
  const unsigned int assemblyThreads = requested > 0 ? requested : budget;
  assemblerThreads_.store(assemblyThreads, std::memory_order_relaxed);
  ThreadPool assemblyPool(assemblyThreads);
  SearchOptions search = o.search;
  search.threads = assemblyThreads;
  search.pool = &assemblyPool;

  if (o.disassemble && !sliding_) {
    startWorkers(plan, chooseWorkers(o, budget), budget);
  }
  // a sliding run searches each start in the assembly's callback; a full
  // search spreads its levels over the solve's threads (solveThreadBudget)
  if (o.disassemble && sliding_) {
    slidePool_ = std::make_unique<ThreadPool>(budget);
  }

  beginPhase(Phase::Assemble);

  // What a paused run had not finished with comes first: assemblies it had
  // counted go straight to be taken apart, the others as if just found.
  std::vector<PendingAssembly> pend;
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    pend = problem_.takePending();
  }
  for (PendingAssembly& p : pend) {
    if (runStopRequested()) {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.addPending(std::move(p));
    } else if (!p.counted) {
      (void)onAssembly(std::move(p.assembly));
    } else if (!o.disassemble || sliding_) {
      // counted for a take-apart this run does not do: kept for one that does
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.addPending(std::move(p));
    } else {
      enqueue(Task{std::move(p.assembly), p.assemblyNumber, p.solutionNumber});
    }
  }

  RunEnd end = RunEnd::Stopped;
  {
    const AssemblyLoad load(o.disassemble && !sliding_ ? takeApartPool_.get() : nullptr,
                            static_cast<int>(assemblyThreads), assemblyLoad_);
    end = assembler_->assemble([this](Assembly a) { return onAssembly(std::move(a)); }, search,
                               plan.assemblyToken);
  }
  endPhase(assemblyMs_);

  if (!runStopRequested()) {
    beginPhase(Phase::Drain);
    if (o.disassemble && pending_.load(std::memory_order_acquire) > 0) {
      state_.advance(SolveState::Draining);
    }
    waitDrained();
    endPhase(drainMs_);
  }

  stopWorkers();

  std::exception_ptr workerError;
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    workerError = workerError_;
  }
  if (workerError) {
    std::rethrow_exception(workerError);
  }
  slidePool_.reset();

  // whether "no solution" is proven, or the search gave up
  if (sliding_ && o.disassemble && !runStopRequested()) {
    std::string note;
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      note = slidingSummary(o);
    }
    const std::lock_guard<std::mutex> lock(failureMutex_);
    note_ = std::move(note);
  }

  endRun(end);
}

void SolveSession::endRun(RunEnd end) {
  const std::uint64_t ms = runMs();
  const StopKind stop = runStopKind();

  if (stop == StopKind::Abort) {
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
    }
    dropAssembler();
    state_.advance(SolveState::Idle);
    return;
  }

  // A pause, a save point, or the search is through and everything taken
  // apart. Assemblies still waiting (counted for a take-apart this run did
  // not do) keep the solve open: Paused with the search through, and the
  // next run that takes apart finishes it.
  bool finished = false;
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    finished = stop == StopKind::None && end == RunEnd::Finished && problem_.getPending().empty();
    problem_.addTimeMs(ms);
    runTimeAdded_ = true;
    // what the GUI recorded when a solve stopped (recordSearchStats): a
    // sliding search counts afresh each run, the assembler carries its
    // count on
    if (sliding_) {
      problem_.setSearchStats(
          problem_.getSearched() + slideProgress_.load(std::memory_order_relaxed),
          std::max(problem_.getSearchDepth(), searchDepth_.load(std::memory_order_relaxed)),
          problem_.getTriedApart());
    } else {
      problem_.setSearchStats(std::max(problem_.getSearched(), assembler_->iterations()),
                              problem_.getSearchDepth(),
                              problem_.getTriedApart() + completed_.load(std::memory_order_relaxed));
    }
    if (finished) {
      problem_.finishedSolving();
    } else {
      storePosition(problem_, *assembler_);
    }
  }
  state_.advance(finished ? SolveState::Finished : SolveState::Paused);
}

// The solve thread's runStacking: one search from the start stacking to the
// goal, the Panex search whenever it can hold the puzzle (it can pause and
// carry on), else findStackPath; a found path is the one solution, its
// assembly the start stacking. The notes word for word.
void SolveSession::runStacking(const Plan& plan) {
  const SolveOptions& o = plan.options;
  const bool panexSolver = o.stacking.solver == stacking::StackingSolver::Panex;
  const bool countOnly = o.justCount;
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
  if (panexSolver || panex::unsupported(puzzle_, problem_, true).empty()) {
    // the levels on the solve's threads, a saved level written beside them
    // (the original's writer thread)
    ThreadPool pool(std::min(budget + 1, kMaxThreads));
    panex::PanexSearch search;
    search.anyRules = !panexSolver;
    search.highMemory = o.stacking.highMemory;
    search.memoryBytes = o.stacking.memoryBytes;
    search.stop = runToken();
    search.progress = &slideProgress_;
    search.depth = &searchDepth_;
    search.traced = &searchTraced_;
    search.resume = true;
    search.autosaveMinutes = o.stacking.autosave ? 20 : 0;
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
    if (path) {
      // no note
    } else if (stopped && search.saved) {
      note = "Paused " + std::to_string(search.depthReached) +
             " moves deep. The search is saved: press Continue to carry on, even after quitting "
             "ShapeLogic.";
    } else if (stopped) {
      note = "Stopped before the search was complete.";
    } else if (search.outcome == panex::Outcome::Memory) {
      note = "The search outgrew its memory after " + std::to_string(search.found) +
             " stackings.";
      if (!o.settingsPlace.empty()) {
        note += search.saved ? " It is saved: give it more Search memory in " + o.settingsPlace +
                                   " and press Continue to carry on."
                             : " " + o.settingsPlace + " can give it more Search memory.";
      } else {
        note += search.saved ? std::string(" It is saved:") +
                                   (search.highMemory ? "" : " turn on Enable High Memory and") +
                                   " press Continue to carry on."
                             : (search.highMemory ? "" : " Try Enable High Memory.");
      }
    } else if (search.outcome == panex::Outcome::Error) {
      note = search.error;
    } else {
      note = "Every reachable stacking was searched: this puzzle has no solution.";
    }
  } else {
    stacking::StackSearch search;
    search.highMemory = o.stacking.highMemory;
    search.memoryBytes = o.stacking.memoryBytes;
    search.stop = runToken();
    search.progress = &slideProgress_;
    path = stacking::findStackPath(puzzle_, problem_, search);
    stopped = search.outcome == stacking::StackOutcome::Stopped;
    if (path) {
      // no note
    } else if (stopped) {
      note = "Stopped before the search was complete.";
    } else if (search.outcome == stacking::StackOutcome::Memory) {
      note = "The search reached its memory limit of " + std::to_string(search.memoryStates) +
             " stackings. There may be a solution the solver did not find." +
             (!o.settingsPlace.empty() ? " " + o.settingsPlace + " can give it more Search memory."
              : search.highMemory      ? ""
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
    state_.advance(SolveState::Idle);
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
  state_.advance(stopped ? SolveState::Paused : SolveState::Finished);
}

void SolveSession::failRun(std::exception_ptr error) {
  // Everything stops; what the solve found is thrown away, as the original
  // did after an assert in its solve thread.
  requestStop(runStop_, StopKind::Abort);
  stopWorkers();

  // an exception out of a phase skipped its end: no time runs on
  phase_.store(Phase::None, std::memory_order_release);

  SolveFailure f;
  try {
    std::rethrow_exception(error);
  } catch (const InternalError& e) {
    f.message = e.what();
    f.file = e.file();
    f.line = e.line();
  } catch (const std::exception& e) {
    f.message = e.what();
  } catch (...) {
    f.message = "unknown error";
  }

  if (assembler_) {
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.removeAllSolutions();
    }
    dropAssembler();
  }
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    failure_ = std::move(f);
  }
  if (isActive(state_.load())) {
    state_.advance(SolveState::Error);
  }
}

StopToken SolveSession::runToken() const { return runStop_.token(); }

bool SolveSession::runStopRequested() const { return runStop_.stopRequested(); }

StopKind SolveSession::runStopKind() const { return runStop_.kind(); }

// ---- what is found -----------------------------------------------------------------

bool SolveSession::onAssembly(Assembly assembly) {
  // A piece with starts for only some of its copies may also sit on
  // neutral cells: a layout that leaves one of its starts empty is no start.
  if (sliding_ && !sliding::startFillsStamps(puzzle_, problem_, assembly)) {
    return true;
  }
  // Found after a pause was asked for: the search will not report it
  // again, so it is kept for the next run.
  if (runStopRequested()) {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    problem_.addPending(PendingAssembly{std::move(assembly), false, 0, 0});
    return true;
  }

  if (sliding_) {
    return onSlidingAssembly(std::move(assembly));
  }

  switch (action_) {
    case Action::CountAssemblies:
      break;

    case Action::SaveAssemblies: {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      const std::uint64_t drop = std::uint64_t{std::max(runOptions_.solutionDrop, 1U)} *
                                 dropMultiplicator_;
      if (problem_.getNumAssemblies() % drop == 0) {
        problem_.addSolution(std::move(assembly));
      }
      break;
    }

    case Action::CountSolutions:
    case Action::SaveSolutions:
      if (assembly.placementCount() <= 1) {
        // one piece: nothing to take apart
        const std::lock_guard<std::mutex> lock(resultsMutex_);
        if (action_ == Action::SaveSolutions) {
          problem_.addSolution(std::move(assembly));
        }
        problem_.incNumSolutions();
      } else {
        Task task{std::move(assembly), 0, 0};
        {
          const std::lock_guard<std::mutex> lock(resultsMutex_);
          task.assemblyNumber = problem_.getNumAssemblies();
          task.solutionNumber = problem_.getNumSolutions();
        }
        enqueue(std::move(task));
      }
      break;
  }

  const std::lock_guard<std::mutex> lock(resultsMutex_);
  problem_.incNumAssemblies();
  trimSavedSolutions();
  applyLiveSort();
  return true;
}

// The sliding branch of the assembly callback: Find Solutions keeps a start
// only when it slides to the goal, and of the starts that end in the same
// picture the one with the fewest moves.
bool SolveSession::onSlidingAssembly(Assembly assembly) {
  const SolveOptions& o = runOptions_;
  if (!o.justCount) {
    if (o.disassemble) {
      sliding::SlideSearch search = sliding::slideSearchFor(o.sliding);
      // a save point lets the search under way finish (stopSoft)
      search.stop = runToken();
      search.progress = &slideProgress_;
      search.depthProgress = &searchDepth_;
      search.pool = slidePool_.get();
      // The search reads the problem's definition and the shapes, which
      // nothing changes while the run is active; the lock is for what the
      // run writes, and is not held over a search.
      std::optional<Separation> path =
          sliding::findSlidePath(puzzle_, problem_, assembly, search);
      searchDepth_.store(0, std::memory_order_relaxed);

      const std::lock_guard<std::mutex> lock(resultsMutex_);
      // paused in the middle of this start's search: it is searched again
      // on the next run instead of counted as one with no solution
      if (search.outcome == sliding::SlideOutcome::Stopped) {
        problem_.addPending(PendingAssembly{std::move(assembly), false, 0, 0});
        return true;
      }
      if (search.outcome == sliding::SlideOutcome::Limit ||
          search.outcome == sliding::SlideOutcome::Memory ||
          search.outcome == sliding::SlideOutcome::Disk) {
        slideStartsCut_++;
        if (search.outcome == sliding::SlideOutcome::Disk) {
          slideDiskError_ = search.error;
          slideDiskHeldToFree_ = search.diskHeldToFree;
        }
        if (search.outcome == sliding::SlideOutcome::Memory) {
          slideMemoryCut_ = true;
          slideMemoryStates_ = search.memoryStates;
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

// Under resultsMutex_. Whether a missing solution is proven or the search
// gave up (solveThread_c::slidingSummary, word for word).
std::string SolveSession::slidingSummary(const SolveOptions& o) const {
  const sliding::SlidingSolver solver = o.sliding.solver;
  if (slideStartsCut_ > 0) {
    std::string msg = std::to_string(slideStartsCut_) +
                      (slideStartsCut_ == 1 ? " start layout was" : " start layouts were") +
                      " not searched completely";
    if (!slideDiskError_.empty()) {
      msg += ". " + slideDiskError_.substr(0, slideDiskError_.size() - 1);
    } else if (slideMemoryCut_) {
      msg += ": the search reached its memory limit of " + std::to_string(slideMemoryStates_) +
             " arrangements";
    } else if (solver == sliding::SlidingSolver::Deep) {
      msg += ": the search reached its limit of 1,000,000 arrangements";
    } else {
      msg += ": the search reached its limit of 250,000 arrangements";
    }
    msg += ". There may be solutions the solver did not find.";
    if (!o.settingsPlace.empty() && !slideDiskError_.empty()) {
      msg += slideDiskHeldToFree_
                 ? " Free some disk space to let it go further."
                 : " " + o.settingsPlace + " can give it more Sliding disk space.";
    } else if (!o.settingsPlace.empty() && slideMemoryCut_) {
      msg += " " + o.settingsPlace + " can give it more Search memory.";
    } else if (slideMemoryCut_ && !o.sliding.highMemory) {
      msg += " Try Enable High Memory.";
    }
    if (!slideMemoryCut_ && slideDiskError_.empty() && solver != sliding::SlidingSolver::Full) {
      msg += solver == sliding::SlidingSolver::Deep ? " Try the Sliding Full Solver."
                                                    : " Try the Sliding Deep or Full Solver.";
    }
    return msg;
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

void SolveSession::enqueue(Task task) {
  const unsigned int p = pending_.fetch_add(1, std::memory_order_acq_rel) + 1;
  unsigned int peak = peakPending_.load(std::memory_order_relaxed);
  while (p > peak &&
         !peakPending_.compare_exchange_weak(peak, p, std::memory_order_relaxed)) {
  }

  {
    // When assemblies come faster than they are taken apart, the thread
    // that found this one waits here until there is room: the queue (and
    // the memory it takes) stays small and the cores go to the take-aparts.
    // A pause ends the wait; what is queued is kept.
    std::unique_lock<std::mutex> lock(queueMutex_);
    const auto room = [this] {
      return queue_.size() < queueLimit_ || runStopRequested() || workersStop_;
    };
    if (!room()) {
      // The assembly search stands still while this waits (its other
      // threads soon wait behind this one): its cores are not in use, and
      // the take-aparts may borrow them for their levels.
      const int given = assemblyLoad_.exchange(0, std::memory_order_acq_rel);
      if (given != 0) {
        takeApartPool_->addLoad(-given);
      }
      spaceCv_.wait(lock, room);
      if (given != 0) {
        takeApartPool_->addLoad(given);
        assemblyLoad_.fetch_add(given, std::memory_order_acq_rel);
      }
    }
    queue_.push_back(std::move(task));
  }
  queueCv_.notify_one();
}

void SolveSession::processTask(Task& task, Disassembler& disassembler) {
  const std::int64_t t0 = nowNs();
  const auto done = [this, t0] {
    disasmMsTotal_.fetch_add(msSince(t0), std::memory_order_relaxed);
    completed_.fetch_add(1, std::memory_order_relaxed);
  };

  if (task.assembly.placementCount() <= 1) {
    {
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      if (action_ == Action::SaveSolutions) {
        problem_.addSolution(std::move(task.assembly), task.assemblyNumber);
      }
      problem_.incNumSolutions();
    }
    done();
    return;
  }

  TakeApartResult r = disassembler.disassemble(task.assembly, runToken());

  if (!r.separation) {
    if (r.stopped) {
      // stopped part way is not "cannot come apart": again on the next run
      const std::lock_guard<std::mutex> lock(resultsMutex_);
      problem_.addPending(PendingAssembly{std::move(task.assembly), true, task.assemblyNumber,
                                          task.solutionNumber});
      return;
    }
    inseparable_.fetch_add(1, std::memory_order_relaxed);
    done();
    return;
  }

  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    if (action_ == Action::SaveSolutions) {
      addSolution(task, std::move(*r.separation));
      applyLiveSort();
    }
    problem_.incNumSolutions();
  }
  done();
}

// Under resultsMutex_.
void SolveSession::addSolution(Task& task, Separation separation) {
  const SolveOptions& o = runOptions_;
  const std::uint64_t asmNum = task.assemblyNumber;

  // Where it goes in a sorted list: after every solution with a better or
  // equal key, and among equal keys by assembly number (the original
  // placed equals in the order its workers finished).
  const auto sortedIndex = [this, asmNum](unsigned int key, auto keyOf) {
    unsigned int lo = 0;
    unsigned int hi = problem_.getNumberOfSavedSolutions();
    while (lo < hi) {
      const unsigned int mid = lo + (hi - lo) / 2;
      const Solution& sol = problem_.getSavedSolution(mid);
      const Disassembly* s2 = sol.getDisassemblyInfo();
      const bool before = s2 != nullptr && (keyOf(*s2) < key || (keyOf(*s2) == key &&
                                                                 sol.getAssemblyNumber() > asmNum));
      if (before) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo;
  };

  unsigned int pos = 0xFFFFFFFF;
  switch (o.order) {
    case SolutionOrder::Moves:
      pos = sortedIndex(separation.sumMoves(), [](const Disassembly& d) { return d.sumMoves(); });
      break;
    case SolutionOrder::Rotations:
      pos = sortedIndex(separation.sumRotations(),
                        [](const Disassembly& d) { return d.sumRotations(); });
      break;
    case SolutionOrder::Level:
      for (unsigned int i = 0; i < problem_.getNumberOfSavedSolutions(); i++) {
        const Solution& sol = problem_.getSavedSolution(i);
        const Disassembly* s2 = sol.getDisassemblyInfo();
        if (s2 == nullptr) {
          continue;
        }
        const int c = s2->compare(separation);
        if (c < 0 || (c == 0 && sol.getAssemblyNumber() > asmNum)) {
          pos = i;
          break;
        }
      }
      break;
    case SolutionOrder::Unsorted: {
      // every drop-th; trimSavedSolutions thins the rest
      const std::uint64_t drop = std::uint64_t{std::max(o.solutionDrop, 1U)} * dropMultiplicator_;
      if (task.solutionNumber % drop != 0) {
        return;
      }
      break;
    }
  }

  if (o.dropDisassemblies) {
    SeparationInfo info(separation);
    problem_.addSolution(std::move(task.assembly), std::move(info), asmNum, task.solutionNumber,
                         pos);
  } else {
    problem_.addSolution(std::move(task.assembly), std::move(separation), asmNum,
                         task.solutionNumber, pos);
  }

  // the sorted lists keep the best solutionLimit
  if (o.order != SolutionOrder::Unsorted && o.solutionLimit != 0 &&
      problem_.getNumberOfSavedSolutions() > o.solutionLimit) {
    problem_.removeSolution(problem_.getNumberOfSavedSolutions() - 1);
  }
}

// Under resultsMutex_. Past the limit, one of the kept solutions goes, so
// that those kept stay spread evenly over all found; when a whole round
// has gone, every other one is kept from then on. A sliding run counts by
// the solutions whatever it saves (the solve thread's SOL_DISASM), and the
// index is the solve thread's unsigned int (no solution yet makes it
// 0xFFFFFFFF there).
void SolveSession::trimSavedSolutions(bool bySolutions) {
  const unsigned int limit = runOptions_.solutionLimit;
  if (limit == 0 || problem_.getNumberOfSavedSolutions() <= limit) {
    return;
  }
  const std::uint64_t drop = std::uint64_t{std::max(runOptions_.solutionDrop, 1U)} *
                             dropMultiplicator_;
  std::uint64_t idx = static_cast<unsigned int>(
      action_ == Action::SaveAssemblies && !bySolutions ? problem_.getNumAssemblies() - 1
                                                         : problem_.getNumSolutions() - 1);
  idx = (idx % (limit * drop)) / drop;
  if (idx == limit - 1) {
    dropMultiplicator_ *= 2;
  }
  problem_.removeSolution(static_cast<unsigned int>(idx + 1));
}

// Under resultsMutex_.
void SolveSession::applyLiveSort() {
  const int method = liveSort_.load(std::memory_order_relaxed);
  if (method >= 0 && problem_.getNumberOfSavedSolutions() >= 2) {
    problem_.keepSolutionsSorted(method);
  }
}

// ---- the take-apart workers --------------------------------------------------------

void SolveSession::startWorkers(const Plan& plan, unsigned int workers, unsigned int budget) {
  const SolveOptions& o = plan.options;
  workerCount_.store(workers, std::memory_order_relaxed);
  queueLimit_ = std::max<std::size_t>(64, std::size_t{8} * workers);
  {
    const std::lock_guard<std::mutex> lock(queueMutex_);
    workersStop_ = false;
    queue_.clear();
  }

  // A take-apart with no others waiting spreads each level of its search
  // over the cores nothing else is using. The shapes are final here (the
  // hotspots were reset before the matrix): the disassemblers warm their
  // caches.
  auto pool = std::make_unique<ThreadPool>(budget);
  DisassemblerOptions d = o.takeApart;
  d.rotations = o.checkRotations;
  d.solver = o.solver;
  d.pool = pool.get();
  std::vector<std::unique_ptr<Disassembler>> made;
  made.reserve(workers);
  for (unsigned int i = 0; i < workers; i++) {
    made.push_back(std::make_unique<Disassembler>(puzzle_, problem_, d));
  }
  {
    // the last run's disassemblers go before the pool they used
    const std::lock_guard<std::mutex> lock(runMutex_);
    disassemblers_ = std::move(made);
    takeApartPool_ = std::move(pool);
  }

  workers_.reserve(workers);
  for (unsigned int i = 0; i < workers; i++) {
    Disassembler& dis = *disassemblers_[i];
    workers_.emplace_back([this, &dis] { workerLoop(dis); });
  }
}

void SolveSession::workerLoop(Disassembler& disassembler) {
  for (;;) {
    std::optional<Task> task;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueCv_.wait(lock, [this] { return !queue_.empty() || workersStop_; });
      if (queue_.empty()) {
        return;
      }
      task.emplace(std::move(queue_.front()));
      queue_.pop_front();
    }
    spaceCv_.notify_one();

    if (runStopRequested()) {
      // counted already; taken apart on the next run
      {
        const std::lock_guard<std::mutex> lock(resultsMutex_);
        problem_.addPending(PendingAssembly{std::move(task->assembly), true,
                                            task->assemblyNumber, task->solutionNumber});
      }
      taskDone();
      continue;
    }

    // this thread uses a core now: fewer to lend to the others' levels
    takeApartPool_->addLoad(1);
    try {
      processTask(*task, disassembler);
    } catch (...) {
      recordFailure(std::current_exception());
    }
    takeApartPool_->addLoad(-1);
    taskDone();
  }
}

// One assembly less to take apart; wakes waitDrained() when it was the
// last. The mutex is taken for a moment before waking: without it the
// waiter could look at the count, be overtaken here, and sleep through the
// wake-up.
void SolveSession::taskDone() {
  if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    { const std::lock_guard<std::mutex> lock(queueMutex_); }
    queueCv_.notify_all();
  }
}

void SolveSession::waitDrained() {
  std::unique_lock<std::mutex> lock(queueMutex_);
  queueCv_.wait(lock, [this] {
    return runStopRequested() ||
           (queue_.empty() && pending_.load(std::memory_order_acquire) == 0);
  });
}

void SolveSession::stopWorkers() {
  {
    const std::lock_guard<std::mutex> lock(queueMutex_);
    workersStop_ = true;
  }
  queueCv_.notify_all();
  spaceCv_.notify_all();
  for (std::thread& t : workers_) {
    t.join();
  }
  workers_.clear();

  // what is still queued was counted: it goes first on the next run
  std::deque<Task> left;
  {
    const std::lock_guard<std::mutex> lock(queueMutex_);
    left.swap(queue_);
  }
  if (!left.empty()) {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    for (Task& t : left) {
      problem_.addPending(
          PendingAssembly{std::move(t.assembly), true, t.assemblyNumber, t.solutionNumber});
    }
  }
  pending_.store(0, std::memory_order_relaxed);
}

void SolveSession::recordFailure(std::exception_ptr error) {
  {
    const std::lock_guard<std::mutex> lock(failureMutex_);
    if (!workerError_) {
      workerError_ = std::move(error);
    }
  }
  requestStop(runStop_, StopKind::Abort);
}

// ---- progress and statistics -------------------------------------------------------

void SolveSession::beginPhase(Phase phase) {
  phaseOriginNs_.store(nowNs(), std::memory_order_relaxed);
  phase_.store(phase, std::memory_order_release);
}

void SolveSession::endPhase(std::atomic<std::uint64_t>& total) {
  total.store(msSince(phaseOriginNs_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
  phase_.store(Phase::None, std::memory_order_release);
}

std::uint64_t SolveSession::runMs() const {
  const std::int64_t origin = runOriginNs_.load(std::memory_order_relaxed);
  return origin != 0 ? msSince(origin) : 0;
}

std::uint64_t SolveSession::phaseMs() const {
  return msSince(phaseOriginNs_.load(std::memory_order_relaxed));
}

float SolveSession::averageDisassemblySeconds() const {
  const unsigned int n = completed_.load(std::memory_order_relaxed);
  if (n == 0) {
    return 0;
  }
  return static_cast<float>(disasmMsTotal_.load(std::memory_order_relaxed)) / 1000.0F /
         static_cast<float>(n);
}

std::optional<SolveFailure> SolveSession::failure() const {
  const std::lock_guard<std::mutex> lock(failureMutex_);
  return failure_;
}

std::string SolveSession::note() const {
  const std::lock_guard<std::mutex> lock(failureMutex_);
  return note_;
}

SolveProgress SolveSession::progress() const {
  SolveProgress p;
  p.elapsedMs = runMs();

  const SolveState st = state_.load();
  bool stopping = false;
  bool rotations = false;
  bool slideRun = false;
  float af = lastFinished_.load(std::memory_order_relaxed);
  unsigned int piece = 0;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    stopping = isActive(st) && runStop_.stopRequested();
    rotations = runOptions_.checkRotations;
    slideRun = runOptions_.disassemble;
    p.panexSearch = stacking_ && runOptions_.stacking.solver == stacking::StackingSolver::Panex;
    if (published_ != nullptr) {
      af = published_->finished();
      p.iterations = published_->iterations();
      piece = published_->reducePiece();
    }
    // the take-aparts under way
    for (const std::unique_ptr<Disassembler>& d : disassemblers_) {
      const DisassemblyProgress dp = d->progress();
      if (dp.active) {
        p.running.push_back(dp);
      }
    }
    p.disassembly = runOptions_.disassemble && !disassemblers_.empty();
  }
  af = std::clamp(af, 0.0F, 1.0F);
  p.assemblyFraction = af;

  double usedSeconds = 0;
  unsigned int parts = 0;
  unsigned int pieces = 0;
  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    p.assemblies = problem_.numAssembliesKnown() ? problem_.getNumAssemblies() : 0;
    p.solutions = problem_.numSolutionsKnown() ? problem_.getNumSolutions() : 0;
    if (problem_.usedTimeKnown()) {
      usedSeconds = static_cast<double>(problem_.getUsedMs()) / 1000.0;
      p.usedMs = problem_.getUsedMs();
    }
    // this run's time until its end adds it to the problem's
    if (isActive(st) && !runTimeAdded_) {
      p.usedMs += p.elapsedMs;
    }
    parts = problem_.getNumberOfParts();
    pieces = problem_.getNumberOfPieces();
  }
  p.searched = slideProgress_.load(std::memory_order_relaxed);
  p.searchDepth = searchDepth_.load(std::memory_order_relaxed);
  p.searchTraced = searchTraced_.load(std::memory_order_relaxed);
  p.disasmCompleted = completed_.load(std::memory_order_relaxed);
  p.disasmPending = pending_.load(std::memory_order_relaxed);
  p.disasmWorkers = workerCount_.load(std::memory_order_relaxed);
  p.assemblerThreads = assemblerThreads_.load(std::memory_order_relaxed);

  const Phase phase = phase_.load(std::memory_order_acquire);
  switch (st) {
    case SolveState::Idle:
      p.stage = SolveStage::Idle;
      break;
    case SolveState::Preparing:
      if (phase == Phase::Reduce) {
        p.stage = SolveStage::Reduce;
        p.pieces = pieces;
      } else {
        p.stage = SolveStage::Prepare;
        p.pieces = parts;
      }
      p.piece = std::min(piece + 1, p.pieces);
      break;
    case SolveState::Assembling:
      // the slide search and the stacking search report in their own way
      // (searched, searchDepth, searchTraced)
      p.stage = (sliding_ && slideRun) || stacking_ ? SolveStage::Other : SolveStage::Assemble;
      break;
    case SolveState::Draining:
      p.stage = SolveStage::Disassemble;
      break;
    case SolveState::Paused:
      p.stage = SolveStage::Paused;
      break;
    case SolveState::Finished:
      p.stage = SolveStage::Done;
      break;
    case SolveState::Error:
      p.stage = SolveStage::Error;
      break;
  }
  if (stopping) {
    p.stage = SolveStage::Stopping;
  }

  // the take-aparts under way, the longest running first
  std::sort(p.running.begin(), p.running.end(),
            [](const DisassemblyProgress& a, const DisassemblyProgress& b) {
              return a.elapsedMs > b.elapsedMs;
            });
  if (!p.running.empty() && p.running[0].levelSize > 0) {
    p.levelFraction = static_cast<float>(p.running[0].levelDone) /
                      static_cast<float>(p.running[0].levelSize);
  }

  // how long the assembly search has run, for what it has left; the
  // fraction covers what earlier runs of a continued solve did, too
  double asmSeconds = static_cast<double>(assemblyMs_.load(std::memory_order_relaxed)) / 1000.0;
  if (phase == Phase::Assemble) {
    asmSeconds += static_cast<double>(phaseMs()) / 1000.0;
  }
  asmSeconds += usedSeconds;

  const bool searching = p.stage == SolveStage::Assemble || p.stage == SolveStage::Disassemble;

  double asmLeft = -1;
  if (af >= 0.999F) {
    asmLeft = 0;
  } else if (af > 0.001F) {
    asmLeft = asmSeconds * (1.0 - static_cast<double>(af)) / static_cast<double>(af);
  }

  if (p.stage == SolveStage::Done) {
    p.overall = 1;
    p.secondsLeft = 0;

  } else if (!p.disassembly) {
    p.overall = af;
    if (searching) {
      p.secondsLeft = asmLeft;
    }

  } else {
    // assemblies the search has still to find, at the rate so far
    double future = 0;
    if (af > 0.0001F && af < 0.999F && p.assemblies > 0) {
      future = static_cast<double>(p.assemblies) * (1.0 - static_cast<double>(af)) /
               static_cast<double>(af);
    }

    // A take-apart under way counts by the separations it has found of the
    // ones it needs: the one thing known about how far it is.
    double partDone = 0;
    double runningSeconds = 0;
    for (const DisassemblyProgress& d : p.running) {
      if (d.pieces > 1) {
        partDone += static_cast<double>(d.separations) / static_cast<double>(d.pieces - 1);
      }
      runningSeconds += static_cast<double>(d.elapsedMs) / 1000.0;
    }
    partDone = std::min(partDone, static_cast<double>(p.disasmPending));

    const double done = static_cast<double>(p.disasmCompleted) + partDone;
    const double left = static_cast<double>(p.disasmPending) - partDone + future;
    const double total = done + left;

    double disasmFrac = 0;
    if (total < 1.0) {
      // none seen: with the search through, there is nothing to take apart
      disasmFrac = af >= 0.999F ? 1.0 : 0.0;
    } else {
      disasmFrac = done / total;
    }

    // how long a take-apart takes is only known once one is through
    const double avg = static_cast<double>(averageDisassemblySeconds());

    // The two sides count by what they cost: the whole assembly search at
    // the rate so far (thread-seconds) against all the take-aparts at the
    // average of the ones done. The bar then moves with the time spent of
    // the time the solve will take, whichever side is the slow one (the
    // idea is upstream's progressModel_c, Tom Burns). Before either can be
    // measured: half and half, or with turns, where taking apart is much
    // the slower side, one to four.
    double asmWeight = rotations ? 0.2 : 0.5;
    if (af > 0.02F && asmSeconds > 0 && avg > 0 && p.disasmCompleted > 0 && total >= 1.0) {
      const double threads =
          p.assemblerThreads > 0 ? static_cast<double>(p.assemblerThreads) : 1.0;
      const double asmCost = asmSeconds * threads / static_cast<double>(af);
      const double disCost = avg * total;
      asmWeight = asmCost / (asmCost + disCost);
    }
    p.overall = static_cast<float>(asmWeight * static_cast<double>(af) +
                                   (1.0 - asmWeight) * disasmFrac);
    double disLeft = -1;
    if (p.disasmPending == 0 && future < 0.5) {
      disLeft = 0;
    } else if (p.disasmCompleted > 0) {
      const double count = static_cast<double>(p.disasmPending) + future;
      const double busy = avg * static_cast<double>(p.running.size());
      double work = avg * count - std::min(runningSeconds, busy);
      work = std::max(work, 0.0);
      const double workers =
          std::clamp(static_cast<double>(p.disasmWorkers), 1.0, std::max(count, 1.0));
      disLeft = work / workers;
    }

    p.overallKnown = disLeft >= 0;

    if (searching && asmLeft >= 0 && disLeft >= 0) {
      p.secondsLeft = std::max(asmLeft, disLeft);
    }
  }

  if (p.stage != SolveStage::Done) {
    p.overall = std::clamp(p.overall, 0.0F, 0.999F);
    // an estimate that is corrected downwards does not take the bar back,
    // whichever thread asks
    p.overall = std::max(p.overall, progressShown_.load(std::memory_order_relaxed));
  }
  float shown = progressShown_.load(std::memory_order_relaxed);
  while (p.overall > shown &&
         !progressShown_.compare_exchange_weak(shown, p.overall, std::memory_order_relaxed)) {
  }

  return p;
}

SolveStats SolveSession::stats() const {
  SolveStats s;
  s.state = state_.load();
  s.hardwareThreads = hardwareThreads();
  s.assemblerThreads = std::max(assemblerThreads_.load(std::memory_order_relaxed), 1U);
  s.disasmWorkers = workerCount_.load(std::memory_order_relaxed);
  s.inseparable = inseparable_.load(std::memory_order_relaxed);
  s.pending = pending_.load(std::memory_order_relaxed);
  s.peakPending = peakPending_.load(std::memory_order_relaxed);
  s.disasmCompleted = completed_.load(std::memory_order_relaxed);
  s.disasmWorkMs = disasmMsTotal_.load(std::memory_order_relaxed);
  s.avgDisasmSeconds = averageDisassemblySeconds();
  s.assemblyProgress = lastFinished_.load(std::memory_order_relaxed);

  std::uint64_t rotationUs = 0;
  std::uint64_t linearUs = 0;
  {
    const std::lock_guard<std::mutex> lock(runMutex_);
    s.solver = runOptions_.solver;
    s.disassemblyEnabled = runOptions_.disassemble;
    s.rotationsEnabled = runOptions_.checkRotations;
    if (published_ != nullptr) {
      s.iterations = published_->iterations();
      s.assemblyProgress = published_->finished();
    }
    for (const std::unique_ptr<Disassembler>& d : disassemblers_) {
      rotationUs += d->getRotationSearchUs();
      linearUs += d->getLinearSearchUs();
      const DisassemblyProgress dp = d->progress();
      if (dp.active) {
        s.running.push_back(dp);
      }
    }
  }
  s.rotationSearchMs = rotationUs / 1000;
  s.linearSearchMs = linearUs / 1000;

  {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    s.assembliesFound = problem_.numAssembliesKnown() ? problem_.getNumAssemblies() : 0;
    s.solutionsFound = problem_.numSolutionsKnown() ? problem_.getNumSolutions() : 0;
  }

  s.prepareMs = prepareMs_.load(std::memory_order_relaxed);
  s.reduceMs = reduceMs_.load(std::memory_order_relaxed);
  s.assemblyMs = assemblyMs_.load(std::memory_order_relaxed);
  s.drainMs = drainMs_.load(std::memory_order_relaxed);
  switch (phase_.load(std::memory_order_acquire)) {
    case Phase::Prepare:
      s.prepareMs += phaseMs();
      break;
    case Phase::Reduce:
      s.reduceMs += phaseMs();
      break;
    case Phase::Assemble:
      s.assemblyMs += phaseMs();
      break;
    case Phase::Drain:
      s.drainMs += phaseMs();
      break;
    case Phase::None:
      break;
  }
  s.elapsedMs = runMs();
  return s;
}

}  // namespace bt
