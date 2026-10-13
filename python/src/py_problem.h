// SPDX-License-Identifier: GPL-3.0-or-later
// A problem of a puzzle, as the Python module shows it.
// Ported from src/python/py_problem.h.
#pragma once

#include "solution_iterator.h"

#include <memory>
#include <string>

namespace bt {
class Puzzle;
class Problem;
}  // namespace bt

namespace bt::python {

class PyShape;

/// A problem of a puzzle (the module's `Problem`): the puzzle and the
/// problem's index.
class PyProblem {
 public:
  /// std::out_of_range (IndexError) when the puzzle has no problem `idx`.
  PyProblem(std::shared_ptr<Puzzle> puz, unsigned int idx);

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  [[nodiscard]] unsigned int getNumPieces() const;
  [[nodiscard]] unsigned int getIndex() const { return problemIdx_; }

  void setResult(unsigned int shapeIdx);
  void setResultShape(const PyShape& shape);
  /// The result shape's index, or -1 when none is set.
  [[nodiscard]] int getResult() const;

  void setPieceCount(unsigned int shapeIdx, unsigned int count);
  void setPieceCountShape(const PyShape& shape, unsigned int count);
  void setPieceRange(unsigned int shapeIdx, unsigned int minCount, unsigned int maxCount);
  void setPieceRangeShape(const PyShape& shape, unsigned int minCount, unsigned int maxCount);
  [[nodiscard]] unsigned int getPieceMin(unsigned int shapeIdx) const;
  [[nodiscard]] unsigned int getPieceMax(unsigned int shapeIdx) const;

  /// A solve of a copy of the puzzle as it is now, on a thread of its own.
  [[nodiscard]] std::unique_ptr<SolutionIterator> solve(const IteratorOptions& options) const;

 private:
  std::shared_ptr<Puzzle> puzzle_;
  unsigned int problemIdx_;
  [[nodiscard]] Problem& problem() const;
  void checkShape(unsigned int shapeIdx) const;
};

}  // namespace bt::python
