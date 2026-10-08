// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt: the command line. For brick puzzles it is the original's burrTxt:
// the same options and the same text on stdout (solving with -d, -p, -s,
// -q, -n, -o, taking saved solutions apart again with -x, -a), and --json
// as burrTxt --json, which the comparison harness (tests/oracle) reads.
// --save is burrTxt2's mode: a solve as the GUI runs it (a SolveSession),
// written to a file. Sliding and stacking puzzles are refused.
// Ported from src/burrTxt.cpp and src/burrTxt2.cpp of the original.
#include "dot_level.h"
#include "options.h"
#include "text_print.h"

#include "bt/base/solve_state.h"
#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"
#include "bt/solve/assembler.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solve_session.h"
#include "bt/solve/solver_type.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
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
// another type or of none. The sliding and stacking solvers are known so
// that their names give the same answers; those puzzles are refused later.
bool pickSolver(PuzzleKind kind, const char* name, bt::SolverType* out) {
  const std::string n = squash(name);
  if (kind == PuzzleKind::Sliding) {
    if (n == "fast" || n == "slidingfast" || n == "slidingfastsolver" ||
        n == "slidingfastsolver250kdepth" || n == "deep" || n == "slidingdeep" ||
        n == "slidingdeepsolver" || n == "slidingdeepsolver1mildepth" || n == "full" ||
        n == "slidingfull" || n == "slidingfullsolver" || n == "slidingfullsolverfulldepth") {
      return true;
    }
    std::fprintf(stderr,
                 "bt: unknown solver '%s' for a sliding puzzle\n"
                 "    use \"Sliding Fast Solver\", \"Sliding Deep Solver\" or \"Sliding Full "
                 "Solver\"\n"
                 "    (also: fast, deep, full)\n",
                 name);
    return false;
  }
  if (kind == PuzzleKind::Stacking) {
    if (n == "stacking" || n == "stackingsolver" || n == "panex" || n == "panexsolver") {
      return true;
    }
    std::fprintf(stderr,
                 "bt: unknown solver '%s' for a stacking puzzle\n"
                 "    use \"Stacking Solver\" or \"Panex Solver\" (also: stacking, panex)\n",
                 name);
    return false;
  }
  if (const auto t = bt::solverTypeFromName(name)) {
    *out = *t;
    return true;
  }
  std::fprintf(stderr,
               "bt: unknown solver '%s' for a brick puzzle\n"
               "    use \"BurrTools Classic\", \"Andrew Crowell\", or \"BurrTools 2\"\n"
               "    (also: classic, crowell, bt2)\n",
               name);
  return false;
}

// What findAssembler said on stderr about its choice.
const char* assemblerLine(bt::AssemblerKind kind, bt::SolverType solver) {
  switch (kind) {
    case bt::AssemblerKind::Bt2:
      return "using assembler bt2 (dancing cells)";
    case bt::AssemblerKind::Assembler1:
      return solver == bt::SolverType::Bt2 ? "using assembler 1 (serial fallback)"
                                           : "using assembler 1";
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

// Sliding and stacking puzzles: burrTxt's message for a puzzle it cannot
// handle, and why on stderr.
int refuseKind(const Options& o, PuzzleKind kind) {
  std::fputs("The puzzles contains features not yet supported by burrTxt\n",
             o.json ? stderr : stdout);
  std::fprintf(stderr, "bt: %s puzzles cannot be solved yet\n", kindName(kind));
  return kExitError;
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
  d.analysis.rotations.fast = o.fastRotations;
  d.analysis.rotations.anchor = o.rotationAnchor;
  return d;
}

// The assembly search's switches from the command line.
void setSearchSwitches(const Options& o, bt::SearchOptions& search) {
  search.vectorSearch = !o.noSimd;
  search.vectorMemoryBudget = std::uint64_t{o.vectorMemoryMb} << 20;
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
      if (o.ask == Ask::SolutionAssemblies) {
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

  unsigned long jsonAssemblies = 0;
  unsigned long jsonSolutions = 0;
  bt::cli::BestLevel best;
  double jsonSeconds = 0;

  const ProblemRange range = problemRange(o, puzzle);
  for (unsigned int pr = range.first; pr < range.last; pr++) {
    const bt::Problem& problem = puzzle.getProblem(pr);
    // the original's assemblers failed an assertion on this
    if (!problem.resultValid()) {
      std::fprintf(stderr, "bt: problem %u (%s) has no result shape\n", pr,
                   problem.getName().c_str());
      return kExitUsage;
    }

    const bt::AssemblerKind kind = bt::chooseAssembler(problem, o.solver);
    if (!o.json) {
      std::fprintf(stderr, "%s\n", assemblerLine(kind, o.solver));
    }
    std::unique_ptr<bt::Assembler> assembler = bt::makeAssembler(puzzle, problem, o.solver);

    bt::MatrixOptions matrix;
    matrix.strictColors = o.strictColors;
    if (const bt::AssemblerError err = assembler->createMatrix(matrix);
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
    // BurrTools 2 picks its own count when none is given (bt2Threads), as
    // burrTxt's bt2ChooseAssemblerWorkers did
    search.threads =
        kind == bt::AssemblerKind::Bt2 ? bt::bt2Threads(o.threads) : o.threads;
    search.pool = &pool;
    setSearchSwitches(o, search);

    // The take-apart, on grids that have one (the others report no
    // solutions, where the original aborted)
    std::unique_ptr<bt::Disassembler> disassembler;
    if (o.disassemble && bt::Disassembler::canDisassemble(puzzle)) {
      if (!takeApartPool) {
        takeApartPool = std::make_unique<bt::ThreadPool>(budget);
      }
      disassembler = std::make_unique<bt::Disassembler>(puzzle, problem,
                                                        takeApartOptions(o, takeApartPool.get()));
    }

    unsigned long assemblies = 0;
    unsigned long solutions = 0;
    std::atomic<unsigned long> seenAssemblies{0};
    ProgressTicker ticker(o.progress, [&] {
      const unsigned long seen = seenAssemblies.load(std::memory_order_relaxed);
      std::fprintf(stderr, "%7.1f s  assembly %5.1f%%, %lu found", secondsSince(start),
                   100.0 * static_cast<double>(assembler->finished()), seen);
      if (disassembler) {
        const bt::DisassemblyProgress dp = disassembler->progress();
        if (dp.active) {
          std::fprintf(stderr, "  disassemble %lu: level %u: %llu/%llu, %llu positions", seen,
                       dp.level, static_cast<unsigned long long>(dp.levelDone),
                       static_cast<unsigned long long>(dp.levelSize),
                       static_cast<unsigned long long>(dp.nodes));
          if (dp.pieces > 2) {
            std::fprintf(stderr, ", split %u/%u", dp.separations, dp.pieces - 1);
          }
          if (dp.threads > 1) {
            std::fprintf(stderr, ", %u threads", dp.threads);
          }
        }
      }
      std::fprintf(stderr, "\n");
    });

    const bt::RunEnd end = assembler->assemble(
        [&](const bt::Assembly& assembly) {
          assemblies++;
          seenAssemblies.store(assemblies, std::memory_order_relaxed);
          if (disassembler) {
            const bt::TakeApartResult r = disassembler->disassemble(assembly);
            if (r.separation) {
              solutions++;
              if (o.json) {
                best.consider(r.separation->movesText());
              } else {
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
              }
            }
          } else if (o.printAssembly && !o.disassemble) {
            bt::cli::printAssembly(stdout, puzzle, problem, assembly);
          }
          return true;
        },
        search, bt::StopToken{});
    ticker.stop();
    if (end != bt::RunEnd::Finished) {
      std::fprintf(stderr, "bt: the search stopped before its end\n");
      return kExitError;
    }

    const double seconds = secondsSince(start);
    if (o.json) {
      jsonAssemblies += assemblies;
      jsonSolutions += solutions;
      jsonSeconds += seconds;
    } else {
      std::printf("%lu assemblies and %lu solutions found with %llu iterations in %.3f seconds",
                  assemblies, solutions,
                  static_cast<unsigned long long>(assembler->iterations()), seconds);
      if (o.newline) {
        std::printf("\n");
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
    std::printf(",\"solvetime\":%.3f}\n", jsonSeconds);
  }
  return kExitOk;
}

// -x: take the saved solutions apart again and print what comes apart.
int retakeMode(const Options& o, bt::Puzzle& puzzle) {
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

// --save: burrTxt2's solve, as the GUI runs it (a SolveSession: reduce, a
// paused solve carried on, the solutions sorted by moves and limited), and
// the solved puzzle written to a file.
int saveMode(const Options& o, bt::Puzzle& puzzle) {
  const unsigned int caps = puzzle.getGridType().getCapabilities();
  if ((caps & bt::kCapAssemble) == 0) {
    std::printf("Sorry, this space grid doesn't have an assembler (yet)\n");
    return kExitUsage;
  }
  if (o.disassemble && (caps & bt::kCapDisassemble) == 0) {
    std::printf(
        "Sorry, this space grid doesn't have a disassembler (yet)\n"
        "drop -d to just assemble the puzzle\n");
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
  so.disassemble = o.disassemble;
  so.checkRotations = o.rotations;
  so.justCount = o.countOnly;
  so.dropDisassemblies = o.dropDisassemblies;
  so.solutionLimit = o.solutionLimit;
  so.threads = o.threads;
  setSearchSwitches(o, so.search);
  so.takeApart = takeApartOptions(o, nullptr);

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

    bt::SolveSession session(puzzle, problem, so);
    if (const bt::Result<void> r = session.resume(); !r) {
      std::printf("Could not start Solver\n");
      std::fprintf(stderr, "bt: %s\n", r.error().message.c_str());
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
        std::fprintf(stderr, "bt: %s\n", f->message.c_str());
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
    std::fprintf(stderr, "bt: %s\n", saved.error().message.c_str());
    return kExitUsage;
  }
  return exitCode;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (const std::optional<int> code = bt::cli::parseArgs(argc, argv, o)) {
    return *code;
  }

  bt::Result<bt::Puzzle> loaded = bt::io::loadPuzzle(o.file);
  if (!loaded) {
    if (loaded.error().code == bt::Errc::IoError) {
      std::printf("could not open input file \"%s\"\n", o.file);
    } else {
      std::fprintf(stderr, "bt: could not read \"%s\": %s\n", o.file,
                   loaded.error().message.c_str());
    }
    return kExitUsage;
  }
  bt::Puzzle& puzzle = *loaded;

  const PuzzleKind kind = puzzleKind(puzzle);
  if ((o.rotations || o.strictColors) && kind != PuzzleKind::Brick) {
    std::fprintf(stderr, "bt: -R and -C are for brick puzzles; this is a %s puzzle\n",
                 kindName(kind));
    return kExitUsage;
  }
  if (o.solverName != nullptr && !pickSolver(kind, o.solverName, &o.solver)) {
    return kExitUsage;
  }

  if (o.ask != Ask::None) {
    return askMode(o, puzzle);
  }

  if (!o.allProblems && o.problem >= puzzle.getNumberOfProblems()) {
    std::fprintf(stderr, "bt: there is no problem %u; the file has %u (numbered from 0)\n",
                 o.problem, puzzle.getNumberOfProblems());
    return kExitUsage;
  }
  if (kind != PuzzleKind::Brick) {
    return refuseKind(o, kind);
  }

  if (o.saveTo != nullptr) {
    return saveMode(o, puzzle);
  }
  if (!o.assemble) {
    return retakeMode(o, puzzle);
  }
  return solveMode(o, puzzle);
}
