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

#include "shapelogic/base/error.h"
#include "shapelogic/base/file_lock.h"
#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/search_sample.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/stack_search.h"
#include "shapelogic/solve/solver_type.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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
  /// colors (PAR_KEEP_MIRROR, PAR_KEEP_ROTATIONS, PAR_COMPLETE_ROTATIONS,
  /// PAR_STRICT_COLORS).
  MatrixOptions matrix;
  /// Take each assembly apart; only those that come apart are solutions
  /// (PAR_DISASSM).
  bool disassemble = false;
  /// Try 90 degree turns while taking apart (PAR_CHECK_ROTATIONS).
  bool checkRotations = false;
  /// Count only, keep no solutions (PAR_JUST_COUNT).
  bool justCount = false;
  /// A Tilted Square puzzle (decision 0045): the pieces may be turned over.
  /// Off, a piece is placed only in the four quarter turns in the tray's
  /// plane (MatrixOptions::partTransforms, so without the symmetry breaker:
  /// what the rotation filter does not drop is counted). No part on a
  /// puzzle without a tray.
  bool flipPieces = true;
  /// With `disassemble`: also keep the assemblies that do not come apart,
  /// without a disassembly, after every solution (not in the original).
  bool keepAssemblies = false;
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
  /// to kMaxThreads. With a limit and a take-apart, the two share it as
  /// takeApartThreads() says: a quarter take apart while the search runs
  /// beside them, all of them when the search waits for the take-aparts or
  /// is over.
  unsigned int threads = 0;
  /// Take-apart workers; 0 chooses (SHAPELOGIC_DISASM_WORKERS of the
  /// original). At most 64.
  unsigned int takeApartWorkers = 0;
  /// The assembly search's switches: vectorSearch, kernels,
  /// vectorMemoryBudget. Its threads and pool are the session's.
  SearchOptions search;
  /// The take-apart's switches: levelThreads, levelThreadCostUs, analysis.
  /// Its rotations, solver and pool come from the fields above.
  DisassemblerOptions takeApart;
  /// A sliding puzzle's search: nested slides, high memory, where the
  /// search keeps its levels. A sliding run always keeps
  /// mirrored and rotated starts and matches colors strictly (the start
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
  /// Where the program's user sets the memory and disk sliding and
  /// stacking searches may take ("Settings ▸ Solver"), for the notes of a
  /// search that ran out of them; empty: the notes suggest Enable High
  /// Memory, as the original's did.
  std::string settingsPlace;
  /// The memory the sliding and stacking searches of every solve running
  /// at once share (decision 0028): a sliding run that takes apart and a
  /// stacking run hold a lease on it while they run, and their searches
  /// fill only their share. Null: each search has the memory its own
  /// options give it (the CLI, the Python module).
  std::shared_ptr<MemoryShare> memoryShare;
};

/// What start(), prepare() and resume() are refused with (IoError) while
/// another solve of the same problem works in its search folders: a copy
/// of the problem in this program, or the problem in another one
/// (decision 0035).
inline constexpr std::string_view kSolvedElsewhere =
    "another solve of this problem is under way (a copy of the problem, another window, or "
    "shapelogic_cli): pause it or let it finish first";

/// A sliding or stacking run that paused before its search began because
/// the folder the search keeps its files in could not be written (a drive
/// that is gone, a folder without write access).
struct FolderFailure {
  /// The folder (SlidingOptions::workDir or StackingOptions::workDir).
  std::string folder;
  /// What was wrong with it (folderWriteError).
  std::string reason;
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
/// only through readProblem(), and nobody edits the puzzle but to add a
/// problem (which moves no other). The puzzle and the problem must outlive
/// the session and stay where they are.
///
/// A pause keeps everything: assemblies queued or under way go back to the
/// problem's pending list, the assembler's position is written into the
/// problem (so that the puzzle can be saved), and the next run carries on
/// in memory with the same assembler. A problem loaded with a position a
/// run cannot carry on from ends in Error (CannotRestoreInterrupted) and
/// keeps what it held (decision 0041); a solve begun anew starts from scratch.
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
  /// Why resume() with these options would not carry the paused solve on:
  /// a sliding solve paused here whose searches are saved, and options
  /// that name another search than its last run's (the nested slides, the
  /// vertical moves, the orientations, the optimized paths' grid). Such a
  /// run would find nothing saved under its own name and search from the
  /// first move, with the saved search left where it is. Empty when the
  /// options carry it on, when nothing is saved, and for any other solve.
  /// The owner asks before setOptions(); not while a run is active.
  [[nodiscard]] std::string resumeRefusal(const SolveOptions& options) const;

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
  /// calls resume(). A sliding run's level search stops at the end of the
  /// level it is on (SlideSearch::savePoint), saved in its folder, with
  /// its start kept for the next run: no level is cut short, so however
  /// often a save point is asked for the search gets further each run. A
  /// run that paused here and for no other reason says so
  /// (SolveProgress::atSavePoint). Only while assembling or taking apart;
  /// otherwise nothing happens (the next autosave will do). Nothing
  /// happens on a stacking run either: its search saves itself (the GUI
  /// did not autosave stacking solves). Does not wait. Any thread.
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

  /// Why the last run paused before its search began: the search's folder
  /// could not be written; empty otherwise. Continue tries again. Any
  /// thread.
  [[nodiscard]] std::optional<FolderFailure> folderFailure() const;

  /// Whether the last run of a sliding solve paused on a start whose saved
  /// search could not be read back (it is kept, decision 0041): Continue
  /// tries it again. Any thread.
  [[nodiscard]] bool slideSavedUnread() const {
    return slideSavedUnread_.load(std::memory_order_acquire);
  }

  /// Where the solve stands. Any thread; the overall figure never goes
  /// back during a run, whoever asks.
  [[nodiscard]] SolveProgress progress() const;

  /// The counts and times of the current or last run. Any thread.
  [[nodiscard]] SolveStats stats() const;

  /// Asks the searches of the run for a picture of where they are (a live
  /// preview): the slide or stacking search, or a take-apart under way,
  /// puts one the next time it can; takeSample() has it then. The
  /// assembly search gives none. Any thread.
  void requestSample() noexcept { sample_.request(); }

  /// The newest picture asked for with requestSample(), if one has come
  /// since the last call. Any thread.
  [[nodiscard]] std::optional<SearchSnapshot> takeSample() { return sample_.take(); }

  /// The lines for the application's log since the last call, oldest
  /// first: what the slide searches told of carrying a saved search on,
  /// starting one again, stopping short of the end, and how each start
  /// layout's search ended. Any thread.
  [[nodiscard]] std::vector<LogLine> takeLog();

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

  /// Whether files can be written in a search's folder (none: yes); when
  /// not, folderFailure() says why. On the run's thread.
  bool folderWritable(const std::string& folder);

  /// Takes the lock of the problem's search folders into `lock` (decision
  /// 0035): a sliding solve that searches and a stacking solve the Panex
  /// search takes work in folders named by the problem, where a second
  /// solve of it would carry on, overwrite and delete the first one's
  /// files. Busy when another solve holds it, in this program or another;
  /// Held otherwise, `lock` left empty when the solve has no such folder
  /// or the lock's file cannot be made (folderWritable then says why the
  /// run cannot begin).
  [[nodiscard]] FileLock::Outcome lockSearches(FileLock& lock) const;

  /// The end of a run, on its thread: the lock of the search folders goes
  /// before the state says the run is over, so that whoever sees it over
  /// can start a solve of the problem at once.
  void leave(SolveState state);

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
  void failRun(const std::exception_ptr& error);
  // the run's stop, read by the run's threads (it does not change while a
  // run is active)
  [[nodiscard]] StopToken runToken() const;
  [[nodiscard]] bool runStopRequested() const;
  [[nodiscard]] StopKind runStopKind() const;
  // the assembly search's stop (the save points), and whether a save
  // point, and no pause or abort, was asked for
  [[nodiscard]] StopToken assemblyToken() const;
  [[nodiscard]] bool savePointRequested() const;

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
  void startWorkers(const Plan& plan, unsigned int workers, unsigned int beside,
                    unsigned int budget);
  void workerLoop(Disassembler& disassembler);
  void stopWorkers();
  void waitDrained();
  void taskDone();
  void recordFailure(const std::exception_ptr& error);
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
  /// The current run's time is in the problem's time used; under resultsMutex_.
  bool runTimeAdded_ = false;
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
  /// The workers, and how many of them may be at work while the search
  /// runs beside them (takeApartThreads(): fewer only with a thread
  /// limit); the workers at work; whether the search still runs, and
  /// whether the take-aparts fell behind it (enqueue() had to wait; until
  /// the queue is nearly empty again). Under queueMutex_.
  unsigned int workersTotal_ = 0;
  unsigned int workersBeside_ = 0;
  unsigned int working_ = 0;
  bool searchBeside_ = false;
  bool behind_ = false;
  /// The cores the assembly search counts for in the take-apart pool's
  /// load (given away while it waits at a full queue).
  std::atomic<int> assemblyLoad_{0};

  /// The first exception of a run's threads.
  /// The lines for the log (takeLog), written by the search threads.
  mutable std::mutex logMutex_;
  std::vector<LogLine> log_;
  void addLog(LogLevel level, std::string text);
  mutable std::mutex failureMutex_;
  std::optional<SolveFailure> failure_;
  std::exception_ptr workerError_;

  /// The slide searches of a run: the pool the search spreads its levels
  /// over (made per run, with the solve's thread budget), the
  /// arrangements the solve's searches have visited (its runs before this
  /// one included: a run starts it at the problem's count, and its end
  /// puts it back there) and how deep the search under way is, and what
  /// the run's searches came to (the solve thread's slide members; the
  /// counts under resultsMutex_).
  std::unique_ptr<ThreadPool> slidePool_;
  std::atomic<std::uint64_t> slideProgress_{0};
  std::atomic<std::uint64_t> searchDepth_{0};
  /// Since when a slide search of the run has been taking up its saved
  /// search (SlideSearch::pickingUpSince); 0 while none does.
  std::atomic<std::int64_t> slidePickingUpSince_{0};
  /// The part of a move the run's slide search is in while it visits no
  /// arrangement (SlideSearch::phase).
  sliding::SlidePhaseProgress slidePhase_;
  /// A stacking run's search reports here too: the stackings it has found
  /// (slideProgress_), how deep both ends are (searchDepth_), and the moves
  /// traced back once they met.
  std::atomic<std::uint64_t> searchTraced_{0};
  /// The slide searches under way, and what one does once the run is
  /// stopped (what it writes as it saves itself).
  std::atomic<unsigned int> slideSearching_{0};
  sliding::SlideStopProgress slideStop_;
  unsigned int slideStartsCut_ = 0;
  bool slideMemoryCut_ = false;
  /// The grid this run's slide searches hold their stops to (optimized
  /// paths, sliding::searchGrid); 0 for every stop. Set as the run begins.
  int slideGrid_ = 0;
  /// The timing of the slide search's moves (the problem keeps their
  /// times, Problem::getSlideLevelMs): when the move under way began, and
  /// whether a search is timing one. Under resultsMutex_.
  std::chrono::steady_clock::time_point levelStart_;
  bool levelTiming_ = false;
  /// The slide search is `depth` moves deep (SlideSearch::onDepth): the
  /// move just finished is timed, or the times are made to fit a search
  /// that begins or is carried on.
  void onSlideDepth(std::uint64_t depth);
  std::uint64_t slideMemoryStates_ = 0;
  std::string slideDiskError_;
  bool slideDiskHeldToFree_ = false;
  /// A search that filled its disk budget was saved to be carried on.
  bool slideDiskSaved_ = false;
  /// The search did not run: its start's saved search could not be read
  /// back and is kept as it is (SlideSearch::savedUnread). Atomic for
  /// slideSavedUnread().
  std::atomic<bool> slideSavedUnread_{false};
  /// The run carries saved searches on (Continue). A solve begun anew has
  /// thrown the problem's saved searches away, and throws away what is
  /// left in a start's folder too: one whose state could not be read is in
  /// nobody's list (discardSaved), and would stop every solve of its start.
  bool slideResume_ = true;
  /// That search's start went back as pending and the run pauses: no more
  /// starts this run (written by the callback, read on the run's thread
  /// after the assembler returns).
  bool slideDiskHalted_ = false;
  /// A start's search stopped at a save point (SlideSearch::savePoint) and
  /// its start went back as pending: no more starts this run, and no note
  /// (written by the callback, read on the run's thread).
  bool slideSavePointCut_ = false;
  /// The last run paused at a save point and for no other reason
  /// (SolveProgress::atSavePoint); written as the run ends.
  std::atomic<bool> atSavePoint_{false};
  /// note(), under failureMutex_.
  std::string note_;
  /// folderFailure(), under failureMutex_.
  std::optional<FolderFailure> folderFailure_;
  /// The lock of the problem's search folders while a run is under way
  /// (lockSearches): taken by launch() under controlMutex_ with no run
  /// active, let go by the run's thread as it ends (leave()).
  FileLock searchLock_;
  /// A search of this run stopped at its memory limit while other searches
  /// shared the memory (written by the run's thread, read by it for the
  /// notes).
  bool memoryShared_ = false;

  /// Where requestSample() and the searches' pictures meet.
  SearchSample sample_;

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
