// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// bt's command line: burrTxt's options (src/burrTxt.cpp), burrTxt2's
/// solve-and-save mode under --save (src/burrTxt2.cpp), and the A/B
/// switches for benchmarks.

#include "bt/solve/solver_type.h"

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

  // burrTxt2's mode
  const char* saveTo = nullptr;       ///< --save FILE
  bool restart = false;               ///< --restart (burrTxt2 -R)
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
  unsigned int vectorMemoryMb = 256;
  bool originalDrawings = false;  ///< --original-drawings

  const char* file = nullptr;
};

/// Reads the command line into `o`. Returns the exit code when the program
/// ends here (help, version, a mistake, reported), or nothing to go on.
[[nodiscard]] std::optional<int> parseArgs(int argc, char** argv, Options& o);

/// burrTxt's help text, with that puzzle type's options (-h TYPE).
void usage(HelpKind kind = HelpKind::Any);

}  // namespace bt::cli
