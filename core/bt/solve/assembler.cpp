// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler.cpp (assembler_c) and the solver hooks of
// src/lib/problem.cpp (setAssembler, canStartSolving).
#include "bt/solve/assembler.h"

#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

namespace bt {

std::string_view errorMessage(AssemblerError e) noexcept {
  switch (e) {
    case AssemblerError::None:
      return "No error";
    case AssemblerError::TooManyUnits:
      return "Pieces contain too many units";
    case AssemblerError::TooFewUnits:
      return "Pieces contain too few units";
    case AssemblerError::CannotPlace:
      return "A piece cannot be placed in the target shape";
    case AssemblerError::CannotRestoreVersion:
      return "Impossible to restore saved state: internal format changed";
    case AssemblerError::CannotRestoreSyntax:
      return "Impossible to restore saved state: corrupt data";
    case AssemblerError::CannotRestoreInterrupted:
      // The original named only the first cause; BurrTools 2's positions are
      // refused on one thread too (decision 0009).
      return "The saved search can not be continued (it was stopped while running on several "
             "cores, or it is a BurrTools 2 search); it has been reset and must be started again";
    case AssemblerError::PuzzleUnhandable:
      return "Puzzle contains unhandable definitions";
  }
  return "Unknown error";
}

void warmShapeCaches(const Puzzle& puzzle, const Problem& problem) {
  problem.resultShape(puzzle).warmCaches();
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    problem.partShape(puzzle, i).warmCaches();
  }
}

bool canStartSolving(const Problem& problem) {
  return problem.getSolveState() == ProblemState::Unsolved ||
         (problem.getSolveState() == ProblemState::Solving &&
          !problem.getAssemblerState().empty());
}

AssemblerError beginSolving(Problem& problem, Assembler& assembler) {
  if (!problem.getAssemblerState().empty()) {
    // A position on a problem that is not being solved can only come from
    // a hand-edited file; the original asserted. Treated as unreadable.
    const AssemblerError err =
        problem.getSolveState() == ProblemState::Solving
            ? assembler.restorePosition(problem.getAssemblerState(),
                                        problem.getAssemblerVersion())
            : AssemblerError::CannotRestoreSyntax;

    if (err != AssemblerError::None) {
      // The search can only be redone from the start. The partial results
      // came from it, and keeping them would report them a second time.
      problem.removeAllSolutions();
      problem.setAssemblerState({}, {});
      return err;
    }

    problem.clearAssemblerState();
  } else {
    BT_ASSERT(problem.getSolveState() == ProblemState::Unsolved);
    problem.markSolving();
  }
  return AssemblerError::None;
}

void storePosition(Problem& problem, const Assembler& assembler) {
  BT_ASSERT(problem.getSolveState() == ProblemState::Solving);
  SavedPosition p = assembler.savePosition();
  problem.setAssemblerState(std::move(p.text), std::move(p.version));
}

}  // namespace bt
