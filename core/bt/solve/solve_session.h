// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The solve of one problem: preparing the search, the assembly runs, the
/// take-apart of each assembly found (a queue and workers beside the
/// search), the saved solutions with their order and limit, pause,
/// continue and abort, progress and statistics.
///
/// Ported from src/lib/solvethread.{h,cpp} (solveThread_c) and from the
/// parts of the original GUI that drove it: Solve, Prepare, Continue and
/// Abort (mainwindow_solve.cpp), the autosave pause and recordSearchStats
/// (mainwindow.cpp). A sliding puzzle is solved the way the solve thread
/// solved it: the assembler finds the start layouts, and each is searched
/// to the goal by the slide search (bt/solve/sliding_search.h) instead of
/// taken apart (decision 0014). A stacking puzzle is solved the way the
/// solve thread's runStacking solved it: no assembler, one search from the
/// start stacking to the goal (the Panex search, bt/solve/panex_search.h,
/// or findStackPath), which saves itself when it stops and carries on from
/// there (decision 0015). The rules are decisions 0004 and 0009: one
/// StopSource per run, nothing reset, the state written by one thread at a
/// time, threads from pools the session makes.

#include "bt/base/error.h"
#include "bt/base/solve_state.h"
#include "bt/base/stop.h"
#include "bt/solve/assembler.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/sliding_search.h"
#include "bt/solve/solve_progress.h"
#include "bt/solve/stack_search.h"
#include "bt/solve/solver_type.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
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
class Separation;
class ThreadPool;

/// How the saved solutions are kept in order as they come (the solve
/// thread's sort method). The numbers are the original's SRT_ values.
enum class SolutionOrder : std::uint8_t {
  Unsorted = 0,   ///< in the order found, every solutionDrop-th kept
  Moves = 1,      ///< the most moves first (the original's default)
  Level = 2,      ///< the highest level first
  Rotations = 3,  ///< the most turns first
};

/// What a solve does (the solve thread's PAR_ flags and its settings). May
/// change between the runs of a session (setOptions).
struct SolveOptions {
  SolverType solver = SolverType::Classic;
  /// Remove placements that can never be part of an assembly before the
  /// search (PAR_REDUCE).
  bool reduce = true;
  /// Mirrors and rotations kept, the thorough rotation check, strict
  /// colours (PAR_KEEP_MIRROR, PAR_KEEP_ROTATIONS, PAR_COMPLETE_ROTATIONS,
  /// PAR_STRICT_COLORS).
  MatrixOptions matrix;
  /// Take each assembly apart; only those that come apart are solutions
  /// (PAR_DISASSM).
  bool disassemble = false;
  /// Try 90 degree turns while taking apart (PAR_CHECK_ROTATIONS).
  bool checkRotations = false;
  /// Count only, keep no solutions (PAR_JUST_COUNT).
  bool justCount = false;
  /// Keep only the summary of each take-apart (levels and moves), not the
  /// tree (PAR_DROP_DISASSEMBLIES).
  bool dropDisassemblies = false;
  /// Where a new solution goes in the saved list.
  SolutionOrder order = SolutionOrder::Moves;
  /// Keep at most this many solutions (0: all). Sorted, the best are kept;
  /// unsorted, or saved assemblies, are thinned out evenly.
  unsigned int solutionLimit = 10;
  /// Keep only every n-th solution (unsorted) or assembly; 0 is 1.
  unsigned int solutionDrop = 1;
  /// The threads of the solve, assembly and take-apart together; 0 for
  /// every hardware thread (the original without a thread limit). Clamped
  /// to kMaxThreads.
  unsigned int threads = 0;
  /// Take-apart workers; 0 chooses (BURRTOOLS_DISASM_WORKERS of the
  /// original). At most 64.
  unsigned int takeApartWorkers = 0;
  /// The assembly search's switches: vectorSearch, kernels,
  /// vectorMemoryBudget. Its threads and pool are the session's.
  SearchOptions search;
  /// The take-apart's switches: levelThreads, levelThreadCostUs, analysis.
  /// Its rotations, solver and pool come from the fields above.
  DisassemblerOptions takeApart;
  /// A sliding puzzle's search: its solver, nested slides, high memory,
  /// where a full search keeps its levels. A sliding run always keeps
  /// mirrored and rotated starts and matches colours strictly (the start
  /// locks), whatever `matrix` says; `disassemble` (the GUI's Find
  /// Solutions) searches each start to the goal; `checkRotations`,
  /// `dropDisassemblies`, `order` (a found path goes at the end) and the
  /// take-apart's settings play no part.
  sliding::SlidingOptions sliding;
  /// A stacking puzzle's search: its solver, high memory, its own autosave,
  /// where a Panex search is saved. A stacking run has no assembler:
  /// `disassemble` (Find Solutions) or `justCount` must be on, and of the
  /// rest only `justCount` and `threads` play a part.
  stacking::StackingOptions stacking;
};

/// Why a run ended in the Error state.
struct SolveFailure {
  /// The preparation (createMatrix) or the saved position failed: what it
  /// was, and errorParam() (a piece, a number of units). None for an
  /// internal error.
  AssemblerError assembler = AssemblerError::None;
  int param = 0;
  /// The original's message for the assembler error, or what the internal
  /// error (an InternalError, a broken invariant) said.
  std::string message;
  /// Where an InternalError was raised; empty for other errors.
  std::string file;
  int line = 0;
};

/// The solve of one problem, across all its runs.
///
/// The owner calls start(), prepare(), resume(), abort(), wait() and
/// setOptions() (one at a time, from one thread or under its own lock);
/// pause(), pauseAtSavePoint(), state(), progress(), stats(), failure(),
/// setLiveSort() and readProblem() may be called from any thread at any
/// time. A run works on a thread of the session's own; the session makes
/// the pools its searches take threads from (the assembly search's, and
/// one the take-aparts spread their levels over), and the take-apart
/// workers.
///
/// While a run is active the session writes the problem (counts,
/// solutions, the pending assemblies, the saved position) and the shapes'
/// hotspots (a moved one is reset as a run starts, before the matrix is
/// made); others read the problem
/// only through readProblem(), and nobody edits the puzzle. The puzzle and
/// the problem must outlive the session and stay where they are.
///
/// A pause keeps everything: assemblies queued or under way go back to the
/// problem's pending list, the assembler's position is written into the
/// problem (so that the puzzle can be saved), and the next run carries on
/// in memory with the same assembler. BurrTools 2 cannot carry on from a
/// saved position (decision 0009): a problem loaded with one ends in Error
/// (CannotRestoreInterrupted) with its results dropped, and the next run
/// starts from scratch.
class SolveSession {
 public:
  /// A session for `problem` of `puzzle`; nothing runs yet. If the problem
  /// holds a paused solve (from a file), resume() carries it on.
  SolveSession(Puzzle& puzzle, Problem& problem, SolveOptions options = {});
  /// Pauses a running solve and waits for it (the original's destructor):
  /// what it was doing is kept in the problem.
  ~SolveSession();

  SolveSession(const SolveSession&) = delete;
  SolveSession& operator=(const SolveSession&) = delete;

  /// The options of the next run.
  [[nodiscard]] const SolveOptions& options() const { return options_; }
  /// Sets the options of the next run; not while a run is active.
  Result<void> setOptions(const SolveOptions& options);

  /// Solves from the start: drops what the problem holds of earlier
  /// solving (Solve in the original). Not while a run is active.
  Result<void> start();

  /// Drops what the problem holds, prepares the search (the matrix, reduce)
  /// and pauses (Prepare in the original): the position is in the problem.
  /// A stacking search has nothing to prepare (Unsupported).
  Result<void> prepare();

  /// Carries on (Continue in the original): a paused run, a problem that
  /// holds a saved position, or an unsolved problem from the start. A
  /// solved problem (or one whose solve was edited away) has nothing to
  /// carry on from: start() solves it again. After an Error it tries
  /// again.
  Result<void> resume();

  /// Asks the run to pause where it can be continued: the assembly search,
  /// the take-aparts under way and the queue stop; nothing is lost. Does
  /// not wait. Any thread.
  void pause();

  /// Asks the assembly search alone to pause, at a point where the solve
  /// can be saved; the take-aparts under way and queued finish first, then
  /// the run is Paused with its position in the problem (the autosave
  /// pause, stopSoft in the original). The owner then saves the puzzle and
  /// calls resume(). Only while assembling or taking apart; otherwise
  /// nothing happens (the next autosave will do). Nothing happens on a
  /// stacking run either: its search saves itself (the GUI did not
  /// autosave stacking solves). Does not wait. Any thread.
  void pauseAtSavePoint();

  /// Stops a running solve and throws away everything of it: the results,
  /// the paused state and the saved position, and a stacking search's
  /// saved search (Abort in the original); the problem is unsolved and the
  /// session Idle. Waits for the run to end.
  void abort();

  /// Waits until the run (if any) has ended.
  void wait();

  /// The state of the solve. Any thread.
  [[nodiscard]] SolveState state() const { return state_.load(); }

  /// Why the last run ended in Error; empty otherwise. Any thread.
  [[nodiscard]] std::optional<SolveFailure> failure() const;

  /// What the last run of a sliding solve has to say once it is through:
  /// whether a missing solution is proven or the search gave up at a limit
  /// (the solve thread's solver note, slidingSummary); for a stacking solve
  /// why there is no solution, or where a paused search stands (the solve
  /// thread's notes of runStacking); empty otherwise. Any thread.
  [[nodiscard]] std::string note() const;

  /// Where the solve stands. Any thread; the overall figure never goes
  /// back during a run, whoever asks.
  [[nodiscard]] SolveProgress progress() const;

  /// The counts and times of the current or last run. Any thread.
  [[nodiscard]] SolveStats stats() const;

  /// Keeps the saved solutions sorted by Problem::sortSolutions(method) as
  /// they come (a sort chosen while solving stays applied); -1 is off. Any
  /// thread.
  void setLiveSort(int method) { liveSort_.store(method, std::memory_order_relaxed); }

  /// Calls f(problem) while nothing writes the problem, and returns what f
  /// returns: how a running solve's results are read (the original's
  /// SolutionsLock). f must not call into the session.
  template <class F>
  decltype(auto) readProblem(F&& f) const {
    const std::lock_guard<std::mutex> lock(resultsMutex_);
    return std::forward<F>(f)(std::as_const(problem_));
  }

 private:
  /// The tests' seam: hands assemblies to a run's callback without a run,
  /// as the original's tests called the solve thread's callback.
  friend struct SolveSessionTestAccess;

  /// An assembly waiting to be taken apart, with the numbers it was found
  /// with (disasmTask_c).
  struct Task {
    Assembly assembly;
    std::uint64_t assemblyNumber = 0;
    std::uint64_t solutionNumber = 0;
  };

  /// What a run was asked to do and the options it runs with.
  struct Plan {
    SolveOptions options;
    bool prepare = false;      ///< build the search before assembling
    bool prepareOnly = false;  ///< and pause after that
    /// What the assembly search polls: the run's stop and the save points.
    StopToken assemblyToken;
  };

  /// What happens to an assembly found (the solve thread's SOL_ values).
  enum class Action : std::uint8_t {
    CountAssemblies,
    SaveAssemblies,
    CountSolutions,
    SaveSolutions,
  };

  /// What the progress figures are timing (phase_e).
  enum class Phase : std::uint8_t { None, Prepare, Reduce, Assemble, Drain };

  enum class Mode : std::uint8_t { Start, Prepare, Resume };

  Result<void> launch(Mode mode);
  Result<void> launchStacking(Mode mode);
  void joinRunner();
  void dropAssembler();

  // the run, on the session's thread
  void run(const Plan& plan);
  bool prepareSearch(const Plan& plan);
  void assembleAndTakeApart(const Plan& plan);
  void endRun(RunEnd end);
  void runStacking(const Plan& plan);
  void endStackingRun(bool stopped);
  void failRun(std::exception_ptr error);
  // the run's stop, read by the run's threads (it does not change while a
  // run is active)
  [[nodiscard]] StopToken runToken() const;
  [[nodiscard]] bool runStopRequested() const;
  [[nodiscard]] StopKind runStopKind() const;

  // what is found
  bool onAssembly(Assembly assembly);
  bool onSlidingAssembly(Assembly assembly);
  [[nodiscard]] std::string slidingSummary(const SolveOptions& o) const;
  void enqueue(Task task);
  void processTask(Task& task, Disassembler& disassembler);
  void addSolution(Task& task, Separation separation);
  void trimSavedSolutions(bool bySolutions = false);
  void applyLiveSort();

  // the take-apart workers
  void startWorkers(const Plan& plan, unsigned int workers, unsigned int budget);
  void workerLoop(Disassembler& disassembler);
  void stopWorkers();
  void waitDrained();
  void taskDone();
  void recordFailure(std::exception_ptr error);
  void requestStop(const StopSource& source, StopKind kind);

  void beginPhase(Phase phase);
  void endPhase(std::atomic<std::uint64_t>& total);
  void publishAssembler(Assembler* assembler);
  [[nodiscard]] std::uint64_t runMs() const;
  [[nodiscard]] std::uint64_t phaseMs() const;
  [[nodiscard]] float averageDisassemblySeconds() const;

  Puzzle& puzzle_;
  Problem& problem_;
  SolveOptions options_;
  /// A sliding puzzle: runs search each start to the goal.
  bool sliding_ = false;
  /// A stacking puzzle: runs search the start stacking to the goal.
  bool stacking_ = false;

  /// The owner's calls one at a time; the run's thread.
  std::mutex controlMutex_;
  std::thread runner_;

  SolveStateCell state_;
  std::atomic<Phase> phase_{Phase::None};
  std::atomic<std::int64_t> phaseOriginNs_{0};
  std::atomic<std::int64_t> runOriginNs_{0};

  /// Guards what other threads read of the run: its stop sources, the
  /// assembler while it is being made or replaced, the disassemblers.
  mutable std::mutex runMutex_;
  StopSource runStop_;
  /// A child of runStop_: the assembly search alone (save points).
  StopSource assemblyStop_;
  Assembler* published_ = nullptr;
  /// How far the assembler was when it was withdrawn.
  std::atomic<float> lastFinished_{0};
  /// The pool the take-aparts spread their levels over, and one
  /// disassembler per worker; kept after a run for its statistics, replaced
  /// by the next.
  std::unique_ptr<ThreadPool> takeApartPool_;
  std::vector<std::unique_ptr<Disassembler>> disassemblers_;

  /// The search: made by a run, kept across pauses (a paused run carries
  /// on with it), dropped when the solve starts again or is aborted.
  /// Written by the run's thread (or the owner while nothing runs).
  std::unique_ptr<Assembler> assembler_;
  AssemblerKind assemblerKind_ = AssemblerKind::Assembler0;

  /// Guards the problem while a run is active.
  mutable std::mutex resultsMutex_;
  /// Thins kept solutions as they grow (the solve thread's
  /// dropMultiplicator); under resultsMutex_.
  unsigned int dropMultiplicator_ = 1;
  /// What the current run does with an assembly; set before it searches.
  Action action_ = Action::CountAssemblies;
  SolveOptions runOptions_;

  /// The take-apart queue: tasks, the workers' stop, the workers.
  std::mutex queueMutex_;
  /// Work arrived, the run stopped, or the last pending task is done.
  std::condition_variable queueCv_;
  /// There is room in the queue again, or the run stopped.
  std::condition_variable spaceCv_;
  std::deque<Task> queue_;
  std::size_t queueLimit_ = 64;
  bool workersStop_ = false;
  std::vector<std::thread> workers_;
  /// The cores the assembly search counts for in the take-apart pool's
  /// load (given away while it waits at a full queue).
  std::atomic<int> assemblyLoad_{0};

  /// The first exception of a run's threads.
  mutable std::mutex failureMutex_;
  std::optional<SolveFailure> failure_;
  std::exception_ptr workerError_;

  /// The slide searches of a run: the pool a full search spreads its
  /// levels over (made per run, with the solve's thread budget), the
  /// arrangements the search under way has visited and how deep it is,
  /// and what the run's searches came to (the solve thread's slide
  /// members; the counts under resultsMutex_).
  std::unique_ptr<ThreadPool> slidePool_;
  std::atomic<std::uint64_t> slideProgress_{0};
  std::atomic<std::uint64_t> searchDepth_{0};
  /// A stacking run's search reports here too: the stackings it has found
  /// (slideProgress_), how deep both ends are (searchDepth_), and the moves
  /// traced back once they met.
  std::atomic<std::uint64_t> searchTraced_{0};
  unsigned int slideStartsCut_ = 0;
  bool slideMemoryCut_ = false;
  std::uint64_t slideMemoryStates_ = 0;
  std::string slideDiskError_;
  /// note(), under failureMutex_.
  std::string note_;

  std::atomic<int> liveSort_{-1};
  /// The furthest overall progress shown in this run.
  mutable std::atomic<float> progressShown_{0};

  // counts and times of the run (disasmPending and the others of the
  // original)
  std::atomic<unsigned int> assemblerThreads_{1};
  std::atomic<unsigned int> workerCount_{0};
  std::atomic<unsigned int> pending_{0};
  std::atomic<unsigned int> peakPending_{0};
  std::atomic<unsigned int> completed_{0};
  std::atomic<std::uint64_t> disasmMsTotal_{0};
  std::atomic<std::uint64_t> inseparable_{0};
  std::atomic<std::uint64_t> prepareMs_{0};
  std::atomic<std::uint64_t> reduceMs_{0};
  std::atomic<std::uint64_t> assemblyMs_{0};
  std::atomic<std::uint64_t> drainMs_{0};
};

}  // namespace bt
