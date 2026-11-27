// SPDX-License-Identifier: GPL-3.0-or-later
// From Document::solveLaunch and prepareShapesForSolving (cleanup item
// 11.5), which follow the original's Solve (mainWindow_c::cb_BtnStart) and
// cb_BtnCont for the stacking hotspots.
#include "shapelogic/app/solve_setup.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/stacking.h"

#include <string>

namespace sl::app {

Result<void> solveRefusal(const Puzzle& puzzle, const Problem& problem) {
  if (!sliding::isSliding(puzzle)) {
    return {};
  }
  if (problem.resultValid() && sliding::isStartGoalShape(&puzzle.getShape(problem.getResultId()))) {
    if (const std::string why = sliding::startGoalError(puzzle, problem.getResultId(), &problem);
        !why.empty()) {
      return fail(Errc::InvalidArgument, why);
    }
  }
  if (const std::string overflow = sliding::stampOverflowMessage(puzzle, problem);
      !overflow.empty()) {
    return fail(Errc::InvalidArgument, overflow);
  }
  // what the session refuses too, asked before the start locks are set up
  if (!problem.resultValid()) {
    return fail(Errc::InvalidArgument, "a result shape must be defined");
  }
  if (problem.getNumberOfPieces() == 0) {
    return fail(Errc::InvalidArgument, "the problem has no pieces");
  }
  return {};
}

void prepareShapesForSolving(Puzzle& puzzle) {
  const bool stack = stacking::isStacking(puzzle);
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    Voxel& v = puzzle.getShape(i);
    if (stack) {
      (void)stacking::placeSolvingHotspot(v);
    } else if (!v.hasInitialHotspot()) {
      v.initHotspot();
    }
    v.warmCaches();
  }
}

}  // namespace sl::app
