// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// bt's command line: burrTxt's options (src/burrTxt.cpp), burrTxt2's
/// solve-and-save mode under --save (src/burrTxt2.cpp), and the A/B
/// switches for benchmarks.

#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solver_type.h"
#include "shapelogic/solve/stack_search.h"

#include <optional>

namespace bt::cli {

/// What -a asks for (burrTxt's W_SOLUTION_PIECES, W_SOLUTION_ASSM, W_COMMENT).
enum class Ask { None, SolutionPieces, SolutionAssemblies, Comment };

/// The puzzle types burrTxt's help knows (-h TYPE).
enum class HelpKind { Any, Brick, Sliding, Stacking };

/// Everything the command line says.
struct Options {
  // burrTxt
  bool json = false;              ///< --json (implies -d -q -r)
  bool disassemble = false;       ///< -d
  bool rotations = false;         ///< -R (implies -d)
  bool quiet = false;             ///< -q (turns -p and -s off)
  bool newline = true;            ///< not -n
  bool reduce = false;            ///< -r
  bool strictColors = false;      ///< -C
  bool printDisassembly = false;  ///< -p
  bool printAssembly = false;     ///< -s
  bool assemble = true;           ///< not -x
  bool allProblems = false;       ///< -o all
  bool progress = false;          ///< --progress
  Ask ask = Ask::None;            ///< -a s0 / s1 / c
  unsigned int problem = 0;       ///< -o n
  unsigned int threads = 0;       ///< -t n (0: all)
  const char* solverName = nullptr;  ///< --solver, checked once the puzzle type is known
  SolverType solver = SolverType::Classic;

  // burrTxt's sliding options
  bool nestedSlides = false;   ///< --nested-slides
  bool partialNested = false;  ///< --partial-nested (implies --nested-slides)
  bool verticalMoves = false;  ///< --vertical-moves: sliding pieces may move up and down
  bool anyOrientation = false;        ///< --any-orientation: unplaced pieces may turn
  bool mirroredOrientations = false;  ///< --mirrored-orientations (implies --any-orientation)
  bool highMemory = false;     ///< --high-memory
  bool optimizedPaths = false;  ///< --optimized-paths: stops held to the grid the board fits

  // burrTxt's stacking options (--high-memory and --restart too)
  bool panexSolver = false;          ///< --solver of a stacking puzzle: the Panex Solver
  const char* panexDir = nullptr;    ///< --panex-dir DIR
  double panexDiskGB = 0;            ///< --panex-disk GB (0: SHAPELOGIC_PANEX_DISK_GB or half the free space)

  // burrTxt2's mode
  const char* saveTo = nullptr;       ///< --save FILE
  /// --restart: burrTxt2's -R; on a stacking puzzle (with --save or not)
  /// burrTxt's: a saved Panex search is thrown away, not carried on
  bool restart = false;
  bool countOnly = false;             ///< --count-only (burrTxt2 -c)
  bool keepMirrors = false;           ///< --keep-mirrors (burrTxt2 -m)
  bool keepRotations = false;         ///< --keep-rotations (burrTxt2 -r)
  bool dropDisassemblies = false;     ///< --drop-disassemblies (burrTxt2 -p)
  unsigned int solutionLimit = 10;    ///< --limit n (0: all)

  // A/B switches
  bool noSimd = false;
  bool levelThreads = true;
  bool fastAnalysis = true;
  bool fastRotations = true;
  bool rotationAnchor = true;
  bool derivedChecks = false;  ///< --derived-checks
  unsigned int vectorMemoryMb = 256;
  bool rowRotationCheck = true;  ///< --no-row-check
  unsigned int splitTasks = 0;  ///< --split-tasks n (0: the assembler's choice)
  unsigned int splitDepth = 0;  ///< --split-depth n (0: the assembler's choice)
  bool originalDrawings = false;  ///< --original-drawings
  // the slide search's (the original's SHAPELOGIC_SLIDE_* switches)
  bool slideLegacy = false;  ///< --slide-legacy: the search over text keys
  bool slideApart = false;   ///< --slide-apart: like pieces told apart
  /// --slide-stops-apart: the stops inside a grid cell told apart
  bool slideStopsApart = false;
  /// --slide-plain-keys: with a grid, every position numbered in the keys
  bool slidePlainKeys = false;
  /// --slide-one-end: a new search goes from the start alone
  bool slideOneEnd = false;
  int slideGrid = 0;         ///< --slide-grid N: stops held to a grid of N cells (optimized paths)
  bool slideTrace = false;   ///< --slide-trace: each level of the search on stderr
  // the Panex search's (SHAPELOGIC_NO_TOWER_RULE, SHAPELOGIC_PANEX_LOG)
  bool noTowerRule = false;  ///< --no-tower-rule: the classic tower searched too
  bool panexLog = false;     ///< --panex-log: each level of a Panex search on stderr

  const char* file = nullptr;
};

/// Reads the command line into `o`. Returns the exit code when the program
/// ends here (help, version, a mistake, reported), or nothing to go on.
[[nodiscard]] std::optional<int> parseArgs(int argc, char** argv, Options& o);

/// burrTxt's help text, with that puzzle type's options (-h TYPE).
void usage(HelpKind kind = HelpKind::Any);

}  // namespace bt::cli
