// SPDX-License-Identifier: GPL-3.0-or-later
//
// shapelogic_cli: the command line. For brick puzzles it is the original's burrTxt:
// the same options and the same text on stdout (solving with -d, -p, -s,
// -q, -n, -o, taking saved solutions apart again with -x, -a), and --json
// as burrTxt --json, which the comparison harness (tests/oracle) reads.
// --save is burrTxt2's mode: a solve as the GUI runs it (a SolveSession),
// written to a file. Sliding puzzles are solved as burrTxt solved them (the
// start layouts found by the assembler, each searched to the goal), and so
// are stacking puzzles (the start stacking searched to the goal by the
// Panex search or the Stacking Solver's own; Ctrl-C stops and saves a Panex
// search, the same command carries it on).
// Ported from src/burrTxt.cpp and src/burrTxt2.cpp of the original.
#include "dot_level.h"
#include "options.h"
#include "text_print.h"

#include "shapelogic/app/solve_setup.h"
#include "shapelogic/base/file_lock.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"
#include "shapelogic/solve/stack_search.h"
#include "shapelogic/solve/take_apart_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

using bt::cli::Ask;
using bt::cli::Options;

// exit codes (burrTxt's)
constexpr int kExitOk = 0;
constexpr int kExitError = 1;  // the puzzle cannot be solved (--json), a solve failed
constexpr int kExitUsage = 2;  // bad arguments or file

enum class PuzzleKind { Brick, Sliding, Stacking };

PuzzleKind puzzleKind(const bt::Puzzle& puzzle) {
  switch (puzzle.getGridType().getType()) {
    case bt::GridKind::Sliding:
      return PuzzleKind::Sliding;
    case bt::GridKind::Stacking:
      return PuzzleKind::Stacking;
    default:
      return PuzzleKind::Brick;
  }
}

const char* kindName(PuzzleKind k) {
  switch (k) {
    case PuzzleKind::Sliding:
      return "sliding";
    case PuzzleKind::Stacking:
      return "stacking";
    case PuzzleKind::Brick:
      break;
  }
  return "brick";
}

// Lower case, letters and digits only (burrTxt's squash).
std::string squash(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    }
  }
  return out;
}

// --solver for the puzzle's type, with burrTxt's messages for a name of
// another type or of none.
bool pickSolver(PuzzleKind kind, const char* name, Options& o) {
  const std::string n = squash(name);
  if (kind == PuzzleKind::Sliding) {
    // the one sliding solver
    if (n == "sliding" || n == "slidingpuzzlesolver") {
      return true;
    }
    std::fprintf(stderr,
                 "shapelogic_cli: unknown solver '%s' for a sliding puzzle\n"
                 "    use \"Sliding Puzzle Solver\" (also: sliding)\n",
                 name);
    return false;
  }
  if (kind == PuzzleKind::Stacking) {
    if (n == "stacking" || n == "stackingsolver") {
      o.panexSolver = false;
      return true;
    }
    if (n == "panex" || n == "panexsolver") {
      o.panexSolver = true;
      return true;
    }
    std::fprintf(stderr,
                 "shapelogic_cli: unknown solver '%s' for a stacking puzzle\n"
                 "    use \"Stacking Solver\" or \"Panex Solver\" (also: stacking, panex)\n",
                 name);
    return false;
  }
  if (const auto t = bt::solverTypeFromName(name)) {
    o.solver = *t;
    return true;
  }
  std::fprintf(stderr,
               "shapelogic_cli: unknown solver '%s' for a brick puzzle\n"
               "    use \"Complete (ShapeLogic)\" or \"Fast (Crowell)\"\n"
               "    (also: classic, crowell)\n",
               name);
  return false;
}

// What findAssembler said on stderr about its choice.
const char* assemblerLine(bt::AssemblerKind kind) {
  switch (kind) {
    case bt::AssemblerKind::Assembler1:
      return "using assembler 1";
    case bt::AssemblerKind::Assembler0:
      break;
  }
  return "using assembler 0";
}

// burrTxt's messages for the errors of createMatrix: on stderr with --json
// (exit 1), else on stdout (exit 0).
int reportMatrixError(const Options& o, bt::AssemblerError err, int param) {
  std::FILE* out = o.json ? stderr : stdout;
  switch (err) {
    case bt::AssemblerError::TooManyUnits:
      std::fprintf(out, "%i units too many for the result shape\n", param);
      break;
    case bt::AssemblerError::TooFewUnits:
      std::fprintf(out, "%i units too few for the result shape\n", param);
      break;
    case bt::AssemblerError::CannotPlace:
      std::fprintf(out, "Piece %i can be place nowhere in the result shape\n", param);
      break;
    case bt::AssemblerError::PuzzleUnhandable:
      std::fputs("The puzzles contains features not yet supported by burrTxt\n", out);
      break;
    default:
      std::fputs("Oops internal error\n", out);
      break;
  }
  return o.json ? kExitError : kExitOk;
}

// Ctrl-C during a Panex search stops it, and it saves itself; a second one
// quits at once (burrTxt's onInterrupt). The handler only stores: the
// search's stop is a lock-free atomic, so asking it from here is safe.
std::atomic<bool> gInterrupted{false};
std::atomic<const bt::StopSource*> gInterruptStop{nullptr};

extern "C" void onInterrupt(int /*signal*/) {
  if (gInterrupted.exchange(true)) {
    std::_Exit(130);
  }
  if (const bt::StopSource* s = gInterruptStop.load()) {
    s->request(bt::StopKind::Pause);
  }
}

// While it lives, Ctrl-C stops `stop` (once; then it quits).
class InterruptStops {
 public:
  explicit InterruptStops(const bt::StopSource* stop) {
    gInterrupted.store(false);
    gInterruptStop.store(stop);
    was_ = std::signal(SIGINT, onInterrupt);
  }
  ~InterruptStops() {
    std::signal(SIGINT, was_);
    gInterruptStop.store(nullptr);
  }
  InterruptStops(const InterruptStops&) = delete;
  InterruptStops& operator=(const InterruptStops&) = delete;

 private:
  void (*was_)(int) = nullptr;
};

// Where Panex searches are saved: --panex-dir, else $SHAPELOGIC_PANEX_DIR,
// else the user's cache folder's "panex" (the original's library read the
// variable; the core takes the folder as an option). An empty variable
// counts as unset, as in the engine (bt::defaultSearchFolder).
std::string panexWorkDir(const Options& o) {
  if (o.panexDir != nullptr) {
    return o.panexDir;
  }
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the environment is read before a thread is started
  return bt::defaultSearchFolder(std::getenv("SHAPELOGIC_PANEX_DIR"), bt::userCacheDirectory(),
                                 "panex");
}

// The disk saved levels may take: --panex-disk, else
// $SHAPELOGIC_PANEX_DISK_GB, else 0 (half the free space, at most 200 GB).
// (main has refused a value that is no budget)
std::uint64_t panexDiskBudget(const Options& o) {
  if (o.panexDiskGB > 0) {
    return bt::gbToBytes(o.panexDiskGB).value_or(0);
  }
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the environment is read before a thread is started
  return bt::diskBudgetFromText(std::getenv("SHAPELOGIC_PANEX_DISK_GB"));
}

// The disk budgets the environment gives, read as burrTxt read them
// (atof): an error for one that is no budget (bt::gbToBytes), else empty.
std::string diskEnvironmentError() {
  for (const char* name : {"SHAPELOGIC_PANEX_DISK_GB", "SHAPELOGIC_SLIDE_DISK_GB"}) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): the environment is read before a thread is started
    if (const char* e = std::getenv(name); e != nullptr && !bt::gbToBytes(std::strtod(e, nullptr))) {
      return std::string(name) + " is \"" + e + "\": a number of GB from 0 to 1000000";
    }
  }
  return "";
}

// A stacking solve's options from the command line (the session's).
bt::stacking::StackingOptions stackingOptions(const Options& o) {
  bt::stacking::StackingOptions s;
  s.solver = o.panexSolver ? bt::stacking::StackingSolver::Panex
                           : bt::stacking::StackingSolver::Shortest;
  s.highMemory = o.highMemory;
  s.workDir = panexWorkDir(o);
  s.diskBudget = panexDiskBudget(o);
  s.towerRule = !o.noTowerRule;
  return s;
}

// One line per rod transfer of a stacking path: lift, cross and drop are
// three states, and a disc's x is the x of the rod it is on
// (printStackPlan).
void printStackPlan(const bt::Separation& path, const bt::Puzzle& puzzle,
                    const bt::Problem& problem) {
  const std::vector<int> rodX = bt::stacking::layoutBoard(puzzle, problem, false).rodX;
  const bt::stacking::RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  auto rodAt = [&](int x) {
    for (unsigned int r = 0; r < rodX.size(); r++) {
      if (rodX[r] == x) {
        return bt::stacking::isPocket(board, r) ? std::string("pocket")
                                                : "rod " + std::to_string(r + 1);
      }
    }
    return std::string("?");
  };
  std::vector<std::string> names;
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(part); j++) {
      std::string nm = "S" + std::to_string(problem.getShapeIdOfPart(part) + 1);
      if (problem.getPartMaximum(part) > 1) {
        nm += " copy " + std::to_string(j + 1);
      }
      names.push_back(nm);
    }
  }
  const unsigned int n = path.getPieceNumber();
  const unsigned int moves = bt::stacking::logicalMoves(path);
  for (unsigned int m = 0; m < moves; m++) {
    const bt::State& a = path.getState(m * bt::stacking::kStepsPerMove);
    const bt::State& b = path.getState((m + 1) * bt::stacking::kStepsPerMove);
    for (unsigned int i = 0; i < n; i++) {
      if (a.getX(i) == b.getX(i)) {
        continue;
      }
      std::printf("%3u: %s from %s to %s\n", m + 1, i < names.size() ? names[i].c_str() : "?",
                  rodAt(a.getX(i)).c_str(), rodAt(b.getX(i)).c_str());
      break;
    }
  }
}

// Takes the lock of a problem's search folders for the solve that follows
// (decision 0035): one solve of a problem at a time works in them. False,
// with the reason on stderr, when another solve holds it; true with no
// lock taken when the search has no folder (`path` empty) or the lock's
// file cannot be made (the search then says what is wrong with the folder).
bool lockSearches(bt::FileLock& lock, const std::string& path) {
  if (path.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (lock.tryAcquire(path) == bt::FileLock::Outcome::Busy) {
    std::fprintf(stderr, "shapelogic_cli: %s\n", std::string(bt::kSolvedElsewhere).c_str());
    return false;
  }
  // what solves that crashed left
  (void)bt::removeStaleLocks(std::filesystem::path(path).parent_path());
  return true;
}

// The slide search the command line asks for: burrTxt's flags, and where
// the search keeps its older levels: $SHAPELOGIC_SLIDE_DIR, or else the
// user's cache folder, and $SHAPELOGIC_SLIDE_DISK_GB (the original's
// library read them; the core takes them as options).
bt::sliding::SlidingOptions slidingOptions(const Options& o) {
  bt::sliding::SlidingOptions s;
  s.nested = o.nestedSlides;
  s.partialNested = o.partialNested;
  s.verticalMoves = o.verticalMoves;
  s.anyOrientation = o.anyOrientation;
  s.mirroredOrientations = o.mirroredOrientations;
  s.highMemory = o.highMemory;
  // (an empty variable counts as unset, as in the engine)
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the environment is read before a thread is started
  s.workDir = bt::defaultSearchFolder(std::getenv("SHAPELOGIC_SLIDE_DIR"), bt::userCacheDirectory(),
                                      "sliding");
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the environment is read before a thread is started
  s.diskBudget = bt::diskBudgetFromText(std::getenv("SHAPELOGIC_SLIDE_DISK_GB"));
  s.legacy = o.slideLegacy;
  s.likePiecesApart = o.slideApart;
  s.gridStopsApart = o.slideStopsApart;
  s.plainKeys = o.slidePlainKeys;
  s.packing = o.slidePacking;
  s.minKeyBits = o.slideKeyBits;
  s.oneEnd = o.slideOneEnd;
  s.compression = o.slideCompress;
  s.optimizedPaths = o.optimizedPaths;
  s.grid = o.slideGrid;
  return s;
}

// A slide search set up for one start, on the threads of `pool`, its stops
// held to `grid` (optimized paths; 0 for every stop).
bt::sliding::SlideSearch slideSearch(const Options& o, bt::ThreadPool* pool, int grid) {
  bt::sliding::SlideSearch search = bt::sliding::slideSearchFor(slidingOptions(o));
  search.pool = pool;
  search.grid = grid;
  // --restart: what is left in a start's folder goes too (a saved search
  // whose state could not be read is in no list that discardSaved reads)
  search.resume = !o.restart;
  return search;
}

// --slide-trace: each level of `search` on stderr as it is done, and before
// the first the key the level search runs in; and the search's lines for
// the application's log, the debug ones too (a search carried on, each
// save, what the disk has room for, and why a search stopped short).
// `search` is the one searched (the lines read what the search says of its
// key).
void traceSlide(const Options& o, bt::sliding::SlideSearch& search) {
  if (!o.slideTrace) {
    return;
  }
  search.onLog = [](bt::LogLevel level, const std::string& text) {
    std::fprintf(stderr, "%s: %s\n", std::string(bt::logLevelName(level)).c_str(), text.c_str());
  };
  search.onLevel = [&search, told = false](const std::string& line) mutable {
    if (!told) {
      told = true;
      std::fprintf(stderr, "key: %u of %u bits%s\n", search.keyBits, search.keyWidth,
                   search.packedKeys ? ", the copies of a piece as one number" : "");
    }
    std::fprintf(stderr, "%s\n", line.c_str());
  };
}

// --progress: calls `line` once a second on a thread of its own until it
// is destroyed (a ticker; the work stays on the solve's threads).
class ProgressTicker {
 public:
  ProgressTicker(bool on, std::function<void()> line) {
    if (!on) {
      return;
    }
    thread_ = std::jthread([this, line = std::move(line)](const std::stop_token& stop) {
      std::unique_lock lock(mutex_);
      for (;;) {
        // wakes after a second, or at once on a stop (it answers false for both)
        (void)cv_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
        if (stop.stop_requested()) {
          return;
        }
        line();
      }
    });
  }
  ~ProgressTicker() { stop(); }
  ProgressTicker(const ProgressTicker&) = delete;
  ProgressTicker& operator=(const ProgressTicker&) = delete;

  void stop() {
    if (thread_.joinable()) {
      thread_.request_stop();
      thread_.join();
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::jthread thread_;
};

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The take-apart's settings from the command line.
bt::DisassemblerOptions takeApartOptions(const Options& o, bt::ThreadPool* pool) {
  bt::DisassemblerOptions d;
  d.rotations = o.rotations;
  d.solver = o.solver;
  d.pool = pool;
  d.levelThreads = o.levelThreads;
  d.analysis.fast = o.fastAnalysis;
  if (o.noSimd) {
    d.analysis.kernels = bt::SimdKernels::none();
  }
  d.analysis.rotations.fast = o.fastRotations;
  d.analysis.rotations.anchor = o.rotationAnchor;
  d.analysis.derivedChecks = o.derivedChecks;
  return d;
}

// The assembly search's switches from the command line.
void setSearchSwitches(const Options& o, bt::SearchOptions& search) {
  search.vectorSearch = !o.noSimd;
  search.vectorMemoryBudget = std::uint64_t{o.vectorMemoryMb} << 20;
  search.rowRotationCheck = o.rowRotationCheck;
  search.splitTasks = o.splitTasks;
  search.splitDepth = o.splitDepth;
  if (o.noSimd) {
    search.kernels = bt::SimdKernels::none();
  }
}

// -a: the comment, or the saved solutions of every problem.
int askMode(const Options& o, const bt::Puzzle& puzzle) {
  if (o.ask == Ask::Comment) {
    std::printf("%s\n", puzzle.getComment().c_str());
    return kExitOk;
  }
  for (unsigned int i = 0; i < puzzle.getNumberOfProblems(); i++) {
    const bt::Problem& problem = puzzle.getProblem(i);
    std::printf("problem %u\n", i);
    // the saved ones: a solution limit keeps fewer than were found
    for (unsigned int s = 0; s < problem.getNumberOfSavedSolutions(); s++) {
      std::printf("%03u: ", s + 1);
      const bt::Assembly& a = problem.getSavedSolution(s).getAssembly();
      unsigned int pnum = 0;
      for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
        for (unsigned int pp = 0; pp < problem.getPartMaximum(part); pp++) {
          if (a.isPlaced(pnum)) {
            std::printf("S%u ", problem.getShapeIdOfPart(part) + 1);
          }
          pnum++;
        }
      }
      std::printf("\n");
      // drawn into the result shape: a stacking problem has none (the
      // original failed an assertion here and died)
      if (o.ask == Ask::SolutionAssemblies && problem.resultValid()) {
        bt::cli::printAssembly(stdout, puzzle, problem, a);
      }
    }
  }
  return kExitOk;
}

// The problems a run works on: -o n, or all with -o all.
struct ProblemRange {
  unsigned int first;
  unsigned int last;  // one past
};

ProblemRange problemRange(const Options& o, const bt::Puzzle& puzzle) {
  return o.allProblems ? ProblemRange{0, puzzle.getNumberOfProblems()}
                       : ProblemRange{o.problem, o.problem + 1};
}

// What --json prints at the end of a solve, summed over its problems.
struct JsonTotals {
  unsigned long assemblies = 0;
  unsigned long solutions = 0;
  bt::cli::BestLevel best;
  double seconds = 0;
  // Sliding: the starts whose search stopped at a limit, not at its end,
  // why the last of them did, and whether a search was saved to carry on.
  // A harness reading "solutions":0 must be able to tell it from a proof.
  unsigned int slideCut = 0;
  std::string slideReason;
  bool slideSaved = false;
};

// Whether a solve ended because the position a paused solve held could not
// be restored: the problem keeps it (decision 0041), and --restart starts
// over.
bool cannotRestore(bt::AssemblerError e) {
  return e == bt::AssemblerError::CannotRestoreVersion ||
         e == bt::AssemblerError::CannotRestoreSyntax ||
         e == bt::AssemblerError::CannotRestoreInterrupted;
}

// A string for --json: quotes, backslashes (a Windows path) and control
// characters escaped.
std::string jsonText(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char u[8];
      std::snprintf(u, sizeof(u), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
      out.append(u);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// The stacking branch of burrTxt's solve: no assembler, one start (the
// file's start stacks) searched to the goal with -d, by the Panex search
// when the Panex Solver is asked for or the puzzle fits that search (as the
// GUI ran the Stacking Solver), else by the Stacking Solver's own. A
// Panex search carries on a saved one (unless --restart), and Ctrl-C
// stops it and saves it. The exit code when the solve ends here.
std::optional<int> solveStacking(const Options& o, const bt::Puzzle& puzzle,
                                 const bt::Problem& problem, JsonTotals& json) {
  if (o.allProblems && !o.json) {
    std::printf("problem: %s\n", problem.getName().c_str());
  }
  const std::string err = bt::stacking::setupError(puzzle, problem);
  if (!err.empty()) {
    std::fprintf(o.json ? stderr : stdout, "%s\n", err.c_str());
    return o.json ? kExitError : kExitOk;
  }
  const auto start = std::chrono::steady_clock::now();
  std::optional<bt::Separation> path;
  bt::stacking::StackSearch search;
  search.highMemory = o.highMemory;
  bt::panex::PanexSearch px;
  px.highMemory = o.highMemory;
  px.threads = o.threads;
  const bool usePanex =
      o.disassemble && bt::panex::runsOnPanexSearch(puzzle, problem, o.panexSolver);
  // held till the search is over
  bt::FileLock searchLock;
  if (usePanex) {
    const std::string why = bt::panex::unsupported(puzzle, problem, !o.panexSolver);
    if (!why.empty()) {
      std::fprintf(stderr, "shapelogic_cli: %s\n", why.c_str());
      return kExitUsage;
    }
    px.anyRules = !o.panexSolver;
    px.workDir = panexWorkDir(o);
    if (!lockSearches(searchLock, bt::panex::solveLockPath(puzzle, problem, px.workDir))) {
      return kExitError;
    }
    px.diskBudget = panexDiskBudget(o);
    px.towerRule = !o.noTowerRule;
    if (o.panexLog) {
      px.onLevel = [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); };
    }
    if (o.restart) {
      bt::panex::discardSaved(puzzle, problem, px.workDir);
    }
    const std::string saved = bt::panex::savedSearch(puzzle, problem, px.workDir);
    if (!saved.empty() && !o.json) {
      std::printf("continuing %s\n", saved.c_str());
    }
    px.resume = true;
    const bt::StopSource stop;
    px.stop = stop.token();
    const InterruptStops ctrlC(&stop);
    path = bt::panex::solve(puzzle, problem, px);
  } else if (o.disassemble) {
    path = bt::stacking::findStackPath(puzzle, problem, search);
  }
  const double secs = secondsSince(start);
  const unsigned int moves = path ? bt::stacking::logicalMoves(*path) : 0;
  if (o.json) {
    // one start; solved or not, with its transfers (addStacking)
    json.assemblies++;
    if (path) {
      json.solutions++;
      const auto m = static_cast<int>(moves);
      if (!json.best.has || m < json.best.stats.level) {
        json.best.has = true;
        json.best.text = std::to_string(moves);
        json.best.stats = bt::cli::DotLevel{m, m, m, 0};
      }
    }
    json.seconds += secs;
    return std::nullopt;
  }
  if (path && (!o.quiet || o.allProblems)) {
    std::printf("level: %u\n", moves);
  }
  if (path && o.printDisassembly) {
    printStackPlan(*path, puzzle, problem);
  }
  std::printf("1 assemblies and %u solutions found in %.3f seconds%s", path ? 1U : 0U, secs,
              o.disassemble ? "" : " (use -d to search for the moves)");
  if (o.newline) {
    std::printf("\n");
  }
  if (usePanex) {
    std::printf("%s%llu moves deep, %llu stackings, peak %llu MB in memory",
                o.panexSolver ? "panex search: " : "stack search: ",
                static_cast<unsigned long long>(px.depthReached),
                static_cast<unsigned long long>(px.found),
                static_cast<unsigned long long>(px.peakMemory / 1048576));
    if (px.diskBytes != 0) {
      std::printf(", %llu MB on disk (%s)", static_cast<unsigned long long>(px.diskBytes / 1048576),
                  px.levelInterval == 1
                      ? "every level"
                      : ("every " + std::to_string(px.levelInterval) + " levels").c_str());
    }
    std::printf("\n");
    const bool memory = px.outcome == bt::panex::Outcome::Memory;
    if (!path && px.saved) {
      std::printf("%s; it is saved: run the same command again to carry on%s, or add --restart to "
                  "start over\n",
                  memory ? "the search outgrew its memory" : "stopped",
                  memory && !o.highMemory ? " (with --high-memory)" : "");
    } else if (!path && px.savedUnread) {
      std::printf("%s\nnothing was searched: run the same command again to try the saved "
                  "search again, or add --restart to throw it away and start over\n",
                  px.error.c_str());
    } else if (!path) {
      std::printf("%s\n", memory ? (std::string("the search outgrew its memory") +
                                    (o.highMemory ? "" : " (try --high-memory)"))
                                       .c_str()
                       : px.outcome == bt::panex::Outcome::Error
                           ? px.error.c_str()
                           : "every reachable stacking was searched: no solution");
    }
  } else if (o.disassemble && !path) {
    std::printf("%s\n", search.outcome == bt::stacking::StackOutcome::Memory
                            ? (std::string("the search reached its memory limit, so a solution "
                                           "may be missing") +
                               (o.highMemory ? "" : " (try --high-memory)"))
                                  .c_str()
                            : "every reachable stacking was searched: no solution");
  }
  return std::nullopt;
}

// The solve of burrTxt: assemble each problem, take each assembly apart
// with -d, print as asked; --json prints one object at the end.
int solveMode(const Options& o, bt::Puzzle& puzzle) {
  // as burrTxt: the hotspots are set where every orientation keeps them
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }

  if (!o.quiet && !o.json) {
    std::printf(" The puzzle:\n\n");
    bt::cli::printPuzzle(stdout, puzzle);
  }

  const unsigned int budget = o.threads > 0 ? o.threads : bt::hardwareThreads();
  // the take-apart's levels spread over threads of their own, as burrTxt's
  // helper pool: the assembly's are busy
  std::unique_ptr<bt::ThreadPool> takeApartPool;

  JsonTotals json;
  unsigned long& jsonAssemblies = json.assemblies;
  unsigned long& jsonSolutions = json.solutions;
  bt::cli::BestLevel& best = json.best;
  double& jsonSeconds = json.seconds;

  const bool sliding = bt::sliding::isSliding(puzzle);
  const bool stacking = bt::stacking::isStacking(puzzle);
  // a sliding search spreads its levels over threads of its own
  std::unique_ptr<bt::ThreadPool> slidePool;

  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    bt::Problem& problem = puzzle.getProblem(pr);
    // a stacking problem has no assembly: its start is the file's stacks
    if (stacking) {
      if (const std::optional<int> code = solveStacking(o, puzzle, problem, json)) {
        return *code;
      }
      continue;
    }
    // the original's assemblers failed an assertion on this
    if (!problem.resultValid()) {
      std::fprintf(stderr, "shapelogic_cli: problem %u (%s) has no result shape\n", pr,
                   problem.getName().c_str());
      return kExitUsage;
    }
    // a sliding problem's holes, as a solve sets them up
    if (sliding) {
      bt::sliding::syncMaxHoles(puzzle, problem);
    }

    // the matrix as every solve builds it (bt::solveSetup's rules: a
    // sliding piece held to its start and placed as drawn, or, unplaced,
    // as --any-orientation allows; a tilted tray's pieces kept one face up
    // turned only in the tray), over this program's own options
    bt::MatrixOptions base;
    base.strictColors = o.strictColors;
    base.variableHoleColumns = o.holeColumns;
    const bt::SolveSetup setup =
        bt::solveSetup(puzzle, problem, std::move(base), o.solver,
                       bt::SetupFlags{o.anyOrientation, o.mirroredOrientations, o.flip});
    const bt::MatrixOptions& matrix = setup.matrix;

    const bt::AssemblerKind kind = bt::chooseAssembler(problem, setup.solver);
    if (!o.json) {
      std::fprintf(stderr, "%s\n", assemblerLine(kind));
    }
    std::unique_ptr<bt::Assembler> assembler = bt::makeAssembler(puzzle, problem, setup.solver);
    if (const bt::AssemblerError err = assembler->createMatrix(matrix, bt::StopToken{});
        err != bt::AssemblerError::None) {
      return reportMatrixError(o, err, assembler->errorParam());
    }

    const auto start = std::chrono::steady_clock::now();

    if (o.reduce) {
      if (!o.quiet && !o.json) {
        std::printf("start reduce\n\n");
      }
      const bt::ReduceStats removed = assembler->reduce(bt::StopToken{});
      if (!o.json) {
        std::fprintf(stderr, "removed %u rows and %u columns\n", removed.rows, removed.columns);
      }
      if (!o.quiet && !o.json) {
        std::printf("finished reduce\n\n");
      }
    }

    if (o.allProblems) {
      std::printf("problem: %s\n", problem.getName().c_str());
    }

    bt::ThreadPool pool(budget);
    bt::SearchOptions search;
    search.threads = o.threads;
    search.pool = &pool;
    setSearchSwitches(o, search);

    // The take-apart, on grids that have one (the others, and a puzzle
    // with a tilted tray, report no solutions, where the original aborted;
    // Disassembler::canDisassemble); a sliding "take-apart" is the
    // slide search from each start to the goal. The assemblies are taken
    // apart side by side on threads of their own (burrTxt's disassembler
    // pool), the results printed in the order the assemblies were found; on
    // one thread, or with --no-take-apart-pool, each where it is found.
    const bool takeApart =
        o.disassemble && !sliding && bt::Disassembler::canDisassemble(puzzle);
    const bool pooled = takeApart && o.takeApartPool && budget > 1;
    std::unique_ptr<bt::Disassembler> disassembler;
    bt::TakeApartPoolOptions poolOptions;
    if (takeApart) {
      if (!takeApartPool) {
        takeApartPool = std::make_unique<bt::ThreadPool>(budget);
      }
      if (pooled) {
        // with -t n the search and the take-aparts share the n threads
        const bt::TakeApartThreads threads = bt::takeApartThreads(o.threads, budget);
        if (o.threads > 0) {
          search.threads = threads.search;
        }
        poolOptions.workers = threads.workers;
        poolOptions.besideSearch = threads.beside;
        poolOptions.searchCores = threads.search;
        poolOptions.takeApart = takeApartOptions(o, takeApartPool.get());
      } else {
        disassembler = std::make_unique<bt::Disassembler>(
            puzzle, problem, takeApartOptions(o, takeApartPool.get()));
      }
    }

    // optimized paths: the grid the problem's stops are held to, once
    int slideGrid = 0;
    // held till the problem's searches are over
    bt::FileLock searchLock;
    if (o.disassemble && sliding) {
      // a saved slide search of a start is carried on, unless --restart
      const bt::sliding::SlidingOptions so = slidingOptions(o);
      if (!lockSearches(searchLock, bt::sliding::solveLockPath(puzzle, problem, so.workDir))) {
        return kExitError;
      }
      slideGrid = bt::sliding::searchGrid(puzzle, problem, so);
      if (!o.quiet && slideGrid > 1) {
        std::printf("optimized paths: the pieces stop only on a %d-cell grid\n", slideGrid);
      } else if (!o.quiet && o.optimizedPaths && o.slideGrid <= 1) {
        std::printf("optimized paths: the board fits no grid, so every stop is searched\n");
      }
      if (o.restart) {
        bt::sliding::discardSaved(puzzle, problem, so.workDir);
      }
      if (!slidePool) {
        slidePool = std::make_unique<bt::ThreadPool>(budget);
      }
    }

    unsigned long assemblies = 0;
    unsigned long solutions = 0;
    // the fewest moves to each finished picture so far, and the starts whose
    // search stopped at a limit, not at its end
    std::map<std::string, unsigned int> slideBestMoves;
    unsigned int slideStartsCut = 0;
    bool slideMemoryCut = false;
    std::string slideDiskError;
    // a start's search ran out of disk and was saved: the run stops there,
    // and the same command carries it on
    bool slideSaved = false;
    // the saved search could not be read back, and stays as it is
    bool slideUnread = false;
    std::atomic<unsigned long> seenAssemblies{0};
    // a take-apart gave up at the depth a disassembly may have: no count of
    // solutions is true then, so the run ends in that error
    std::atomic<bool> tooDeep{false};

    // What a take-apart found: counted and printed. From one thread at a
    // time, in the order the assemblies were found (the pool sees to that).
    const auto takenApart = [&](const bt::Assembly& assembly, const bt::TakeApartResult& r) {
      if (r.tooDeep) {
        tooDeep.store(true, std::memory_order_relaxed);
      }
      if (!r.separation) {
        return;
      }
      solutions++;
      if (o.json) {
        best.consider(r.separation->movesText());
        return;
      }
      if (o.printAssembly) {
        bt::cli::printAssembly(stdout, puzzle, problem, assembly);
      }
      if (!o.quiet || o.allProblems) {
        std::printf("level: %s\n", r.separation->movesText().c_str());
      }
      if (o.printDisassembly) {
        bt::cli::printSeparation(stdout, puzzle, problem, *r.separation, assembly,
                                 o.originalDrawings);
      }
    };
    // made before the ticker that reads it, and gone after it
    std::unique_ptr<bt::TakeApartPool> takeAparts;
    if (pooled) {
      takeAparts = std::make_unique<bt::TakeApartPool>(
          puzzle, problem, poolOptions,
          [&](const bt::Assembly& a, bt::TakeApartResult& r) { takenApart(a, r); });
    }

    ProgressTicker ticker(o.progress, [&] {
      const unsigned long seen = seenAssemblies.load(std::memory_order_relaxed);
      std::fprintf(stderr, "%7.1f s  assembly %5.1f%%, %lu found", secondsSince(start),
                   100.0 * static_cast<double>(assembler->finished()), seen);
      // the take-apart under way: the first of them, with a pool
      std::optional<bt::DisassemblyProgress> shown;
      std::size_t running = 0;
      unsigned long long number = seen;
      if (takeAparts) {
        const std::vector<bt::DisassemblyProgress> all = takeAparts->progress();
        running = all.size();
        if (!all.empty()) {
          shown = all.front();
        }
        number = takeAparts->delivered() + 1;
      } else if (disassembler) {
        const bt::DisassemblyProgress dp = disassembler->progress();
        if (dp.active) {
          shown = dp;
        }
      }
      if (shown) {
        const bt::DisassemblyProgress& dp = *shown;
        std::fprintf(stderr, "  disassemble %llu: level %u: %llu/%llu, %llu positions", number,
                     dp.level, static_cast<unsigned long long>(dp.levelDone),
                     static_cast<unsigned long long>(dp.levelSize),
                     static_cast<unsigned long long>(dp.nodes));
        if (dp.pieces > 2) {
          std::fprintf(stderr, ", split %u/%u", dp.separations, dp.pieces - 1);
        }
        if (dp.threads > 1) {
          std::fprintf(stderr, ", %u threads", dp.threads);
        }
        if (running > 1) {
          std::fprintf(stderr, ", %zu take-aparts at once", running);
        }
      }
      std::fprintf(stderr, "\n");
    });

    const bt::RunEnd end = assembler->assemble(
        [&](const bt::Assembly& assembly) {
          assemblies++;
          seenAssemblies.store(assemblies, std::memory_order_relaxed);
          if (o.disassemble && sliding) {
            bt::sliding::SlideSearch search = slideSearch(o, slidePool.get(), slideGrid);
            traceSlide(o, search);
            const std::optional<bt::Separation> path =
                bt::sliding::findSlidePath(puzzle, problem, assembly, search);
            if (search.outcome == bt::sliding::SlideOutcome::Memory ||
                search.outcome == bt::sliding::SlideOutcome::Disk) {
              slideStartsCut++;
            }
            if (search.outcome == bt::sliding::SlideOutcome::Memory) {
              slideMemoryCut = true;
            }
            if (search.outcome == bt::sliding::SlideOutcome::Disk) {
              slideDiskError = search.error;
              if (search.saved || search.savedUnread) {
                // the search waits in its folder: no other start is searched
                slideSaved = true;
                slideUnread = search.savedUnread;
                return false;
              }
            }
            if (path) {
              // a start that slides to a picture counted before counts only
              // with fewer moves
              const std::string key = bt::sliding::finalPlacementKey(*path);
              const unsigned int moves = path->getMoves();
              bool countIt = true;
              const auto it = slideBestMoves.find(key);
              if (it == slideBestMoves.end()) {
                slideBestMoves.emplace(key, moves);
              } else if (moves < it->second) {
                it->second = moves;
              } else {
                countIt = false;
              }
              if (countIt) {
                solutions++;
                if (o.json) {
                  best.consider(path->movesText());
                } else {
                  if (o.printAssembly) {
                    bt::cli::printAssembly(stdout, puzzle, problem, assembly);
                  }
                  if (!o.quiet || o.allProblems) {
                    std::printf("level: %s\n", path->movesText().c_str());
                  }
                  if (o.printDisassembly) {
                    bt::cli::printSeparation(stdout, puzzle, problem, *path, assembly,
                                             o.originalDrawings);
                  }
                }
              }
            }
          } else if (takeAparts) {
            takeAparts->submit(assembly);
          } else if (disassembler) {
            takenApart(assembly, disassembler->disassemble(assembly));
          } else if (o.printAssembly && !o.disassemble) {
            bt::cli::printAssembly(stdout, puzzle, problem, assembly);
          }
          return !tooDeep.load(std::memory_order_relaxed);
        },
        search, bt::StopToken{});
    if (takeAparts) {
      // the search is over: every thread takes apart what is left
      takeAparts->finish();
    }
    ticker.stop();
    if (tooDeep.load()) {
      std::fprintf(stderr, "shapelogic_cli: %s\n", bt::Disassembler::tooDeepMessage().c_str());
      return kExitError;
    }
    if (end != bt::RunEnd::Finished && !slideSaved) {
      std::fprintf(stderr, "shapelogic_cli: the search stopped before its end\n");
      return kExitError;
    }

    const double seconds = secondsSince(start);
    if (o.json) {
      jsonAssemblies += assemblies;
      jsonSolutions += solutions;
      jsonSeconds += seconds;
      if (slideStartsCut > 0) {
        json.slideCut += slideStartsCut;
        json.slideReason = slideDiskError.empty()
                               ? "The search reached its memory limit."
                               : slideDiskError;
        json.slideSaved = json.slideSaved || slideSaved;
      }
    } else {
      std::printf("%lu assemblies and %lu solutions found with %llu iterations in %.3f seconds",
                  assemblies, solutions,
                  static_cast<unsigned long long>(assembler->iterations()), seconds);
      if (o.newline) {
        std::printf("\n");
      }
      // whether "0 solutions" is proven, or the search gave up
      if (o.disassemble && sliding) {
        if (slideStartsCut > 0) {
          std::printf(
              "%u start layout(s) not searched completely: %s, so solutions may be missing%s\n",
              slideStartsCut,
              slideDiskError.empty()
                  ? "the search limit was reached"
                  : slideDiskError.substr(0, slideDiskError.size() - 1).c_str(),
              slideMemoryCut && !o.highMemory ? " (try --high-memory)" : "");
          if (slideUnread) {
            std::printf("nothing was searched: run the same command again to try the saved "
                        "search again, or add --restart to throw it away and start over\n");
          } else if (slideSaved) {
            std::printf(
                "the search stopped there and is saved: free some disk space or raise "
                "SHAPELOGIC_SLIDE_DISK_GB, then run the same command again to carry it on, or "
                "add --restart to start over\n");
          }
        } else if (solutions == 0 && assemblies > 0 && slideGrid > 1) {
          std::printf("no solution with optimized paths (a %d-cell grid); a full search may "
                      "find one\n",
                      slideGrid);
        } else if (solutions == 0 && assemblies > 0) {
          std::printf("every reachable arrangement was searched: no solution%s\n",
                      !o.nestedSlides     ? " (without --nested-slides)"
                      : !o.partialNested ? " (without --partial-nested)"
                                          : "");
        }
      }
    }
  }

  if (o.json) {
    std::printf("{\"assemblies\":%lu,\"solutions\":%lu", jsonAssemblies, jsonSolutions);
    if (best.has) {
      std::printf(",\"dotlevel\":\"%s\",\"level\":%d,\"totalmoves\":%d", best.text.c_str(),
                  best.stats.level, best.stats.totalmoves);
      if (o.rotations) {
        std::printf(",\"moves\":%d,\"rotations\":%d", best.stats.moves, best.stats.rotations);
      }
    } else {
      std::printf(",\"dotlevel\":null,\"level\":null,\"totalmoves\":null");
      if (o.rotations) {
        std::printf(",\"moves\":null,\"rotations\":null");
      }
    }
    // only when a search stopped short: burrTxt's object otherwise
    if (json.slideCut > 0) {
      std::printf(",\"cut\":%u,\"reason\":\"%s\",\"saved\":%s", json.slideCut,
                  jsonText(json.slideReason).c_str(), json.slideSaved ? "true" : "false");
    }
    std::printf(",\"solvetime\":%.3f}\n", jsonSeconds);
  }
  return kExitOk;
}

// -x of a sliding puzzle: each saved start searched to the goal again.
int retakeSliding(const Options& o, const bt::Puzzle& puzzle) {
  bt::ThreadPool pool(o.threads > 0 ? o.threads : bt::hardwareThreads());
  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    const bt::Problem& problem = puzzle.getProblem(pr);
    bt::FileLock searchLock;
    if (!lockSearches(searchLock, bt::sliding::solveLockPath(puzzle, problem,
                                                              slidingOptions(o).workDir))) {
      return kExitError;
    }
    for (unsigned int sol = 0; sol < problem.getNumberOfSavedSolutions(); sol++) {
      const bt::Assembly& assembly = problem.getSavedSolution(sol).getAssembly();
      bt::sliding::SlideSearch search =
          slideSearch(o, &pool, bt::sliding::searchGrid(puzzle, problem, slidingOptions(o)));
      traceSlide(o, search);
      const std::optional<bt::Separation> path =
          bt::sliding::findSlidePath(puzzle, problem, assembly, search);
      if (!path) {
        continue;
      }
      if (o.printAssembly) {
        bt::cli::printAssembly(stdout, puzzle, problem, assembly);
      }
      if (!o.quiet) {
        std::printf("level: %u\n", path->getMoves());
      }
      if (o.printDisassembly) {
        bt::cli::printSeparation(stdout, puzzle, problem, *path, assembly, o.originalDrawings);
      }
    }
  }
  return kExitOk;
}

// -x of a stacking puzzle: a problem with saved solutions searched again.
int retakeStacking(const Options& o, const bt::Puzzle& puzzle) {
  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    const bt::Problem& problem = puzzle.getProblem(pr);
    if (problem.getNumberOfSavedSolutions() == 0) {
      continue;
    }
    const std::optional<bt::Separation> path = bt::stacking::findStackPath(puzzle, problem);
    if (path) {
      if (!o.quiet) {
        std::printf("level: %u\n", bt::stacking::logicalMoves(*path));
      }
      if (o.printDisassembly) {
        printStackPlan(*path, puzzle, problem);
      }
    }
  }
  return kExitOk;
}

// -x: take the saved solutions apart again and print what comes apart.
int retakeMode(const Options& o, bt::Puzzle& puzzle) {
  if (bt::sliding::isSliding(puzzle)) {
    return retakeSliding(o, puzzle);
  }
  if (bt::stacking::isStacking(puzzle)) {
    return retakeStacking(o, puzzle);
  }
  // grids without a take-apart have nothing to show (the original failed)
  if (!bt::Disassembler::canDisassemble(puzzle)) {
    return kExitOk;
  }
  bt::ThreadPool pool(o.threads > 0 ? o.threads : bt::hardwareThreads());
  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    const bt::Problem& problem = puzzle.getProblem(pr);
    bt::Disassembler disassembler(puzzle, problem, takeApartOptions(o, &pool));
    for (unsigned int sol = 0; sol < problem.getNumberOfSavedSolutions(); sol++) {
      const bt::Assembly& assembly = problem.getSavedSolution(sol).getAssembly();
      const bt::TakeApartResult r = disassembler.disassemble(assembly);
      if (r.tooDeep) {
        std::fprintf(stderr, "shapelogic_cli: %s\n", bt::Disassembler::tooDeepMessage().c_str());
        return kExitError;
      }
      if (!r.separation) {
        continue;
      }
      if (o.printAssembly) {
        bt::cli::printAssembly(stdout, puzzle, problem, assembly);
      }
      if (!o.quiet) {
        std::printf("level: %u\n", r.separation->getMoves());
      }
      if (o.printDisassembly) {
        bt::cli::printSeparation(stdout, puzzle, problem, *r.separation, assembly,
                                 o.originalDrawings);
      }
    }
  }
  return kExitOk;
}

// --save of a stacking puzzle: the GUI's Solve as a SolveSession runs it
// (burrTxt2 had no stacking solver): a saved Panex search carried on unless
// --restart, Ctrl-C pausing it (it saves itself), the solved puzzle written
// to the file with the solver's note on stdout.
int saveStacking(const Options& o, bt::Puzzle& puzzle) {
  if (!o.quiet) {
    std::printf("outputting into %s\n", o.saveTo);
  }
  bt::SolveOptions so;
  so.disassemble = o.disassemble;
  so.justCount = o.countOnly;
  so.threads = o.threads;
  so.stacking = stackingOptions(o);

  int exitCode = kExitOk;
  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    bt::Problem& problem = puzzle.getProblem(pr);
    const bool saved = !bt::panex::savedSearch(puzzle, problem, so.stacking.workDir).empty();
    if (!o.restart && !saved && problem.getSolveState() == bt::ProblemState::Solved) {
      std::printf("problem %u (%s) is already solved: %llu assemblies, %llu solutions\n", pr,
                  problem.getName().c_str(),
                  static_cast<unsigned long long>(problem.getNumAssemblies()),
                  static_cast<unsigned long long>(problem.getNumSolutions()));
      std::printf("use --restart to solve it again\n");
      continue;
    }
    bt::SolveSession session(puzzle, problem, so);
    const InterruptStops ctrlC(nullptr);
    if (const bt::Result<void> r = o.restart ? session.start() : session.resume(); !r) {
      std::printf("Could not start Solver\n");
      std::fprintf(stderr, "shapelogic_cli: %s\n", r.error().message.c_str());
      exitCode = kExitError;
      continue;
    }
    while (bt::isActive(session.state())) {
      if (gInterrupted.load()) {
        session.pause();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    session.wait();
    if (session.state() == bt::SolveState::Error) {
      std::printf("error in solver\n");
      if (const auto f = session.failure()) {
        std::fprintf(stderr, "shapelogic_cli: %s\n", f->message.c_str());
        if (cannotRestore(f->assembler)) {
          std::fprintf(stderr, "shapelogic_cli: add --restart to solve it from the beginning\n");
        }
      }
      exitCode = kExitError;
    } else if (!o.quiet) {
      std::printf("problem %u: %llu assemblies and %llu solutions found in %.3f seconds\n", pr,
                  static_cast<unsigned long long>(problem.getNumAssemblies()),
                  static_cast<unsigned long long>(problem.getNumSolutions()),
                  static_cast<double>(problem.getUsedMs()) / 1000.0);
      if (const std::string note = session.note(); !note.empty()) {
        std::printf("%s\n", note.c_str());
      }
    }
  }

  if (const bt::Result<void> written = bt::io::savePuzzle(puzzle, o.saveTo); !written) {
    std::fprintf(stderr, "shapelogic_cli: %s\n", written.error().message.c_str());
    return kExitUsage;
  }
  return exitCode;
}

// --save: burrTxt2's solve, as the GUI runs it (a SolveSession: reduce, a
// paused solve carried on, the solutions sorted by moves and limited), and
// the solved puzzle written to a file.
int saveMode(const Options& o, bt::Puzzle& puzzle) {
  const unsigned int caps = puzzle.getGridType().getCapabilities();
  if ((caps & bt::kCapAssemble) == 0) {
    std::printf("Sorry, this space grid doesn't have an assembler (yet)\n");
    return kExitUsage;
  }
  // a sliding puzzle's -d is the slide search (burrTxt2 refused it, its grid
  // having no take-apart; the solve thread it ran solved it)
  if (o.disassemble && !bt::Disassembler::canDisassemble(puzzle) &&
      !bt::sliding::isSliding(puzzle)) {
    if (puzzle.tray()) {
      // a Tilted Square puzzle: the session would refuse it (as the app's
      // Solver stays on Assemble Only)
      std::printf("%s\ndrop -d to just assemble the puzzle\n",
                  bt::Disassembler::kNoTakeApartOnTray);
    } else {
      std::printf(
          "Sorry, this space grid doesn't have a disassembler (yet)\n"
          "drop -d to just assemble the puzzle\n");
    }
    return kExitUsage;
  }
  if (!o.quiet) {
    std::printf("outputting into %s\n", o.saveTo);
  }

  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }

  bt::SolveOptions so;  // reduce, sorted by moves: burrTxt2's
  so.solver = o.solver;
  so.matrix.keepMirrors = o.keepMirrors;
  so.matrix.keepRotations = o.keepRotations;
  so.matrix.strictColors = o.strictColors;
  so.matrix.variableHoleColumns = o.holeColumns;
  so.disassemble = o.disassemble;
  so.checkRotations = o.rotations;
  so.justCount = o.countOnly;
  so.dropDisassemblies = o.dropDisassemblies;
  so.solutionLimit = o.solutionLimit.value_or(bt::cli::kDefaultSolutionLimit);
  so.threads = o.threads;
  setSearchSwitches(o, so.search);
  so.takeApart = takeApartOptions(o, nullptr);
  so.sliding = slidingOptions(o);

  int exitCode = kExitOk;
  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    bt::Problem& problem = puzzle.getProblem(pr);
    if (o.restart) {
      problem.removeAllSolutions();
    }
    // without --restart a solve carries on where an earlier one paused
    if (!bt::canStartSolving(problem)) {
      std::printf("problem %u (%s) ", pr, problem.getName().c_str());
      if (problem.getSolveState() == bt::ProblemState::Solved) {
        std::printf("is already solved: %llu assemblies, %llu solutions\n",
                    static_cast<unsigned long long>(problem.getNumAssemblies()),
                    static_cast<unsigned long long>(problem.getNumSolutions()));
      } else {
        std::printf("has no state to continue from\n");
      }
      std::printf("use --restart to solve it again\n");
      continue;
    }

    // what the app refuses before a solve, in its words
    if (const bt::Result<void> refused = bt::app::solveRefusal(puzzle, problem); !refused) {
      std::printf("Could not start Solver\n");
      std::fprintf(stderr, "shapelogic_cli: %s\n", refused.error().message.c_str());
      exitCode = kExitError;
      continue;
    }
    so.flipPieces = o.flip.value_or(bt::storedFlipPieces(problem));
    bt::SolveSession session(puzzle, problem, so);
    if (const bt::Result<void> r = session.resume(); !r) {
      std::printf("Could not start Solver\n");
      std::fprintf(stderr, "shapelogic_cli: %s\n", r.error().message.c_str());
      exitCode = kExitError;
      continue;
    }
    {
      // what the GUI's Solver tab shows, a line a second
      ProgressTicker ticker(o.progress, [&session] {
        const bt::SolveProgress pg = session.progress();
        if (!bt::isActive(session.state())) {
          return;
        }
        std::fprintf(stderr, "%7.1f s  %5.1f%%  %s", static_cast<double>(pg.elapsedMs) / 1000.0,
                     100.0 * static_cast<double>(pg.overall), pg.activity().c_str());
        if (pg.stage == bt::SolveStage::Prepare) {
          std::fprintf(stderr, "  [%llu placements]",
                       static_cast<unsigned long long>(pg.prepare.placements));
        }
        if (pg.stage == bt::SolveStage::Reduce) {
          const bt::PrepareProgress& r = pg.prepare;
          std::fprintf(stderr, "  [%s %u: %llu/%llu, %llu removed, %llu left]",
                       r.pass == 0 ? "sweep" : "pass", r.pass,
                       static_cast<unsigned long long>(r.done),
                       static_cast<unsigned long long>(r.total),
                       static_cast<unsigned long long>(r.removed),
                       static_cast<unsigned long long>(r.placements));
        }
        if (pg.stage == bt::SolveStage::Assemble) {
          std::fprintf(stderr, "  [assembly %.1f%%, %llu found]",
                       100.0 * static_cast<double>(pg.assemblyFraction),
                       static_cast<unsigned long long>(pg.assemblies));
        }
        if (pg.secondsLeft >= 0) {
          std::fprintf(stderr, "  about %.0f s left", pg.secondsLeft);
        }
        std::fprintf(stderr, "\n");
      });
      session.wait();
    }

    if (session.state() == bt::SolveState::Error) {
      std::printf("error in solver\n");
      if (const auto f = session.failure()) {
        std::fprintf(stderr, "shapelogic_cli: %s\n", f->message.c_str());
        if (cannotRestore(f->assembler)) {
          std::fprintf(stderr, "shapelogic_cli: add --restart to solve it from the beginning\n");
        }
      }
      exitCode = kExitError;
    } else if (!o.quiet) {
      session.readProblem([pr](const bt::Problem& p) {
        if (p.numAssembliesKnown() && p.numSolutionsKnown() && p.usedTimeKnown()) {
          std::printf("problem %u: %llu assemblies and %llu solutions found in %.3f seconds\n",
                      pr, static_cast<unsigned long long>(p.getNumAssemblies()),
                      static_cast<unsigned long long>(p.getNumSolutions()),
                      static_cast<double>(p.getUsedMs()) / 1000.0);
        }
      });
    }
  }

  if (const bt::Result<void> saved = bt::io::savePuzzle(puzzle, o.saveTo); !saved) {
    std::fprintf(stderr, "shapelogic_cli: %s\n", saved.error().message.c_str());
    return kExitUsage;
  }
  return exitCode;
}

// Everything main does: the arguments, the puzzle file, the mode they ask for.
int run(int argc, char** argv) {
  Options o;
  if (const std::optional<int> code = bt::cli::parseArgs(argc, argv, o)) {
    return *code;
  }
  if (const std::string err = diskEnvironmentError(); !err.empty()) {
    std::fprintf(stderr, "shapelogic_cli: %s\n", err.c_str());
    return kExitUsage;
  }
  // a slide search reads many level files at once
  (void)bt::raiseOpenFileLimit(4096);

  bt::Result<bt::Puzzle> loaded = bt::io::loadPuzzle(o.file);
  if (!loaded) {
    if (loaded.error().code == bt::Errc::IoError) {
      std::printf("could not open input file \"%s\"\n", o.file);
    } else {
      std::fprintf(stderr, "shapelogic_cli: could not read \"%s\": %s\n", o.file,
                   loaded.error().message.c_str());
    }
    return kExitUsage;
  }
  bt::Puzzle& puzzle = *loaded;

  const PuzzleKind kind = puzzleKind(puzzle);
  if ((o.verticalMoves || o.anyOrientation || o.optimizedPaths || o.slideGrid > 1) &&
      kind != PuzzleKind::Sliding) {
    std::fprintf(stderr, "shapelogic_cli: %s is for sliding puzzles; this is a %s puzzle\n",
                 o.verticalMoves    ? "--vertical-moves"
                 : o.anyOrientation ? "--any-orientation"
                 : o.optimizedPaths ? "--optimized-paths"
                                    : "--slide-grid",
                 kindName(kind));
    return kExitUsage;
  }
  if (o.nestedSlides && kind != PuzzleKind::Sliding) {
    std::fprintf(stderr, "shapelogic_cli: %s is for sliding puzzles; this is a %s puzzle\n",
                 o.partialNested ? "--partial-nested" : "--nested-slides", kindName(kind));
    return kExitUsage;
  }
  if (o.highMemory && kind == PuzzleKind::Brick) {
    std::fprintf(stderr,
                 "shapelogic_cli: --high-memory is for sliding and stacking puzzles; this is a %s puzzle\n",
                 kindName(kind));
    return kExitUsage;
  }
  if (o.flip.has_value() && !puzzle.tray()) {
    std::fputs("shapelogic_cli: --flip is for Tilted Square puzzles (a brick puzzle with a tray)\n",
               stderr);
    return kExitUsage;
  }
  if ((o.rotations || o.strictColors) && kind != PuzzleKind::Brick) {
    std::fprintf(stderr, "shapelogic_cli: -R and -C are for brick puzzles; this is a %s puzzle\n",
                 kindName(kind));
    return kExitUsage;
  }
  if (o.solverName != nullptr && !pickSolver(kind, o.solverName, o)) {
    return kExitUsage;
  }
  // a stacking or sliding puzzle's --restart throws its saved searches
  // away (burrTxt's for stacking); a brick puzzle's goes with --save
  // (burrTxt2's)
  if (o.restart && o.saveTo == nullptr && kind == PuzzleKind::Brick) {
    std::fputs(
        "shapelogic_cli: --restart, --limit, --count-only, --keep-mirrors, --keep-rotations and\n"
        "    --drop-disassemblies go with --save\n",
        stderr);
    return kExitUsage;
  }

  if (o.ask != Ask::None) {
    return askMode(o, puzzle);
  }

  if (!o.allProblems && o.problem >= puzzle.getNumberOfProblems()) {
    std::fprintf(stderr, "shapelogic_cli: there is no problem %u; the file has %u (numbered from 0)\n",
                 o.problem, puzzle.getNumberOfProblems());
    return kExitUsage;
  }
  if (o.saveTo != nullptr) {
    return kind == PuzzleKind::Stacking ? saveStacking(o, puzzle) : saveMode(o, puzzle);
  }
  if (!o.assemble) {
    return retakeMode(o, puzzle);
  }
  return solveMode(o, puzzle);
}

}  // namespace

// An exception that gets here is a broken invariant (a BT_ASSERT), most
// likely one a damaged file's numbers broke after it was read, or no
// memory for what it asks: said in a line, with the exit code of a solve
// that failed, where the program used to abort (cleanup item 16.2).
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "shapelogic_cli: internal error: %s\n", e.what());
  } catch (...) {
    std::fputs("shapelogic_cli: internal error\n", stderr);
  }
  return kExitError;
}
