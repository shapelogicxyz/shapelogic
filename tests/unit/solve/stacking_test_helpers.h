// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for the stacking (Panex, Hanoi) tests.

#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"

namespace sl::test {

/// Adds a disc of `size` to the puzzle, used exactly once in `prob`.
inline void addSized(Puzzle& puz, Problem& prob, unsigned int size) {
  const unsigned int id = stacking::addDisk(puz, size);
  prob.setShapeMaximum(puz, id, 1);
  prob.setShapeMinimum(puz, id, 1);
}

}  // namespace sl::test
