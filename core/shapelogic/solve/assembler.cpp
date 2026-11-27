// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler.cpp (assembler_c) and the solver hooks of
// src/lib/problem.cpp (setAssembler, canStartSolving).
#include "shapelogic/solve/assembler.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <algorithm>

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
    // The problem keeps what the paused solve held (decision 0041): the
    // original's words, and what is left to do.
    case AssemblerError::CannotRestoreVersion:
      return "Impossible to restore saved state: internal format changed. What the solve found "
             "so far is kept; Solve starts the search again from the beginning";
    case AssemblerError::CannotRestoreSyntax:
      return "Impossible to restore saved state: corrupt data. What the solve found so far is "
             "kept; Solve starts the search again from the beginning";
    case AssemblerError::CannotRestoreInterrupted:
      return "The saved search can not be continued (it was stopped while running on several "
             "cores). What it found so far is kept; Solve starts the search again from the "
             "beginning";
    case AssemblerError::PuzzleUnhandable:
      return "Puzzle contains unhandable definitions";
    case AssemblerError::Stopped:
      return "The preparation was stopped";
  }
  return "Unknown error";
}

unsigned int transformEnd(const std::vector<std::uint64_t>& partTransforms,
                          const Symmetries& sym) {
  return partTransforms.empty() ? sym.getNumTransformations()
                                : std::min(64U, sym.getNumTransformationsMirror());
}

bool transformAllowed(const std::vector<std::uint64_t>& partTransforms, unsigned int part,
                      unsigned int t) {
  if (partTransforms.empty()) {
    return true;
  }
  return part < partTransforms.size() && t < 64 && ((partTransforms[part] >> t) & 1U) != 0;
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
      // The search can only be redone from the start, and that is the
      // user's to choose (decision 0041): the position and the partial
      // results stay, where the original dropped both here. A solve begun
      // anew removes them before it searches, so nothing is reported a
      // second time; a solve carried on comes here again.
      return err;
    }

    problem.clearAssemblerState();
  } else {
    SL_ASSERT(problem.getSolveState() == ProblemState::Unsolved);
    problem.markSolving();
  }
  return AssemblerError::None;
}

void storePosition(Problem& problem, const Assembler& assembler) {
  SL_ASSERT(problem.getSolveState() == ProblemState::Solving);
  SavedPosition p = assembler.savePosition();
  problem.setAssemblerState(std::move(p.text), std::move(p.version));
}

}  // namespace bt
