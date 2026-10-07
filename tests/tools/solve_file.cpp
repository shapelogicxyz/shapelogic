// SPDX-License-Identifier: GPL-3.0-or-later
// bt_solve_file [options] <in> <out>: solves one problem of a puzzle file and
// writes the solved file, as the original's burrTxt2 (and its solve thread)
// do, for `oracle.py compare-files`.
//
//   -d            take apart; only what comes apart is saved, sorted by
//                 moves (the solve thread's default sort), the most first
//   --rotations   with 90 degree turns
//   --solver T    classic, crowell or bt2
//   --restart     drop what the file found before (burrTxt2 -R)
//   -b N          the problem (default 0)
//   -t N          threads (default all)
//   --limit N     keep the N best solutions (the solve thread's 10, the
//                 default); 0 keeps all
//
// The order of burrTxt2: the hotspots reset, then the solve thread's: the
// matrix, reduce, the saved position, the hotspots reset again (nothing to
// do then), the search. (The solve thread alone, the GUI's path, resets
// them only after the matrix: a shape with its hotspot elsewhere is then
// taken apart where the matrix did not place it, and solutions are lost.)
// Without -d only the counts are kept (the solve thread's thinning of saved
// assemblies is stage 5's). Stage 5's solve session replaces this.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solver_type.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace {

bool parseCount(const char* s, unsigned int* out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != 0 || v < 0) {
    return false;
  }
  *out = static_cast<unsigned int>(v);
  return true;
}

// Where a solution of `moves` goes: after every saved one with as many or
// more (findInsertIndexByMoves of the solve thread).
unsigned int insertIndexByMoves(const bt::Problem& problem, unsigned int moves) {
  unsigned int lo = 0;
  unsigned int hi = problem.getNumberOfSavedSolutions();
  while (lo < hi) {
    const unsigned int mid = lo + (hi - lo) / 2;
    const bt::Disassembly* s2 = problem.getSavedSolution(mid).getDisassemblyInfo();
    if (s2 != nullptr && s2->sumMoves() < moves) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

}  // namespace

int main(int argc, char** argv) {
  bool disassemble = false;
  bool rotations = false;
  bool restart = false;
  unsigned int problemNr = 0;
  unsigned int threads = 0;
  unsigned int limit = 10;
  bt::SolverType solver = bt::SolverType::Classic;
  const char* in = nullptr;
  const char* out = nullptr;

  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    if (a == "-d") {
      disassemble = true;
    } else if (a == "--rotations") {
      rotations = true;
    } else if (a == "--restart") {
      restart = true;
    } else if (a == "--solver" && i + 1 < argc) {
      const auto t = bt::solverTypeFromName(argv[++i]);
      if (!t) {
        std::fprintf(stderr, "unknown solver %s\n", argv[i]);
        return 2;
      }
      solver = *t;
    } else if ((a == "-b" || a == "-t" || a == "--limit") && i + 1 < argc) {
      unsigned int v = 0;
      if (!parseCount(argv[i + 1], &v)) {
        std::fprintf(stderr, "%s needs a number\n", argv[i]);
        return 2;
      }
      (a == "-b" ? problemNr : a == "-t" ? threads : limit) = v;
      i++;
    } else if (in == nullptr) {
      in = argv[i];
    } else if (out == nullptr) {
      out = argv[i];
    } else {
      std::fputs("usage: bt_solve_file [options] <in> <out>\n", stderr);
      return 2;
    }
  }
  if (in == nullptr || out == nullptr) {
    std::fputs("usage: bt_solve_file [options] <in> <out>\n", stderr);
    return 2;
  }

  bt::Result<bt::Puzzle> loaded = bt::io::loadPuzzle(in);
  if (!loaded) {
    std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
    return 2;
  }
  bt::Puzzle& puzzle = *loaded;
  if (problemNr >= puzzle.getNumberOfProblems()) {
    std::fprintf(stderr, "there is no problem %u\n", problemNr);
    return 2;
  }
  if (disassemble && !bt::Disassembler::canDisassemble(puzzle)) {
    std::fputs("this grid has no take-apart: drop -d\n", stderr);
    return 2;
  }

  // burrTxt2, before it makes the solve thread
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }

  bt::Problem& problem = puzzle.getProblem(problemNr);
  int exitCode = 0;

  if (restart) {
    problem.removeAllSolutions();
  }

  if (!bt::canStartSolving(problem)) {
    std::printf("problem %u is solved already or has nothing to go on from\n", problemNr);
  } else {
    if (rotations) {
      problem.setSolutionsWithRotations(true);
    }
    const auto start = std::chrono::steady_clock::now();

    std::unique_ptr<bt::Assembler> assembler = bt::makeAssembler(puzzle, problem, solver);
    const bt::AssemblerKind kind = bt::chooseAssembler(problem, solver);
    bool ok = assembler->createMatrix({}) == bt::AssemblerError::None;
    if (ok) {
      (void)assembler->reduce(bt::StopToken{});
      // a saved position that cannot be restored is dropped with what it
      // had found, and the solve ends with an error (the solve thread's
      // ACT_ERROR); the next run starts from scratch
      ok = bt::beginSolving(problem, *assembler) == bt::AssemblerError::None;
    }
    if (!ok) {
      std::puts("error in solver");
      exitCode = 1;
    } else {

      // the solve thread, after the matrix
      for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
        puzzle.getShape(i).initHotspot();
      }

      const unsigned int budget = threads > 0 ? threads : bt::hardwareThreads();
      bt::ThreadPool pool(budget);
      bt::ThreadPool takeApartPool(budget);
      std::unique_ptr<bt::Disassembler> disassembler;
      if (disassemble) {
        bt::DisassemblerOptions d;
        d.rotations = rotations;
        d.solver = solver;
        d.pool = &takeApartPool;
        disassembler = std::make_unique<bt::Disassembler>(puzzle, problem, d);
      }

      bt::SearchOptions search;
      search.threads = kind == bt::AssemblerKind::Bt2 ? bt::bt2Threads(threads) : threads;
      search.pool = &pool;

      const bt::RunEnd end = assembler->assemble(
          [&](const bt::Assembly& a) {
            if (disassembler) {
              const std::uint64_t asmNum = problem.getNumAssemblies();
              const std::uint64_t solNum = problem.getNumSolutions();
              if (a.placementCount() <= 1) {
                problem.addSolution(a, asmNum);
                problem.incNumSolutions();
              } else {
                bt::TakeApartResult r = disassembler->disassemble(a);
                if (r.separation) {
                  const unsigned int moves = r.separation->sumMoves();
                  problem.addSolution(a, std::move(*r.separation), asmNum, solNum,
                                      insertIndexByMoves(problem, moves));
                  if (limit != 0 && problem.getNumberOfSavedSolutions() > limit) {
                    problem.removeSolution(problem.getNumberOfSavedSolutions() - 1);
                  }
                  problem.incNumSolutions();
                }
              }
            }
            problem.incNumAssemblies();
            return true;
          },
          search, bt::StopToken{});

      problem.addTimeMs(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count()));
      if (end == bt::RunEnd::Finished) {
        problem.finishedSolving();
      } else {
        bt::storePosition(problem, *assembler);
      }
    }
  }

  const bt::Result<void> saved = bt::io::savePuzzle(puzzle, out);
  if (!saved) {
    std::fprintf(stderr, "save failed: %s\n", saved.error().message.c_str());
    return 2;
  }
  return exitCode;
}
