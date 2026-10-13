// SPDX-License-Identifier: GPL-3.0-or-later
// What the Python module hands out for an assembly or a solution: plain
// values, copied out of the solve.
// Ported from src/python/py_solution.h.
#pragma once

#include <string>
#include <vector>

namespace bt::python {

/// One piece of an assembly: where it is put, and in which orientation.
struct PyPlacement {
  unsigned int pieceId = 0;
  int x = 0;
  int y = 0;
  int z = 0;
  /// The orientation; 0xff for a piece not placed.
  unsigned char transformation = 0;
  bool isPlaced = false;
};

/// An assembly the solver found, with its take-apart when one was asked for.
struct PySolution {
  /// The assembly's number in the order they were found (from 1).
  unsigned int assemblyNumber = 0;
  /// Its number among the solutions (from 1); the assembly's number when no
  /// take-apart was asked for.
  unsigned int solutionNumber = 0;
  std::vector<PyPlacement> placements;
  bool hasDisassembly = false;
  /// The level text, e.g. "99.3.4.2".
  std::string movesText;
  /// The moves of the whole take-apart.
  unsigned int totalMoves = 0;
  /// The moves until the first piece comes off.
  unsigned int level = 0;
};

}  // namespace bt::python
