// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/python/py_problem.cpp.
#include "py_problem.h"

#include "py_shape.h"

#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <stdexcept>
#include <utility>

namespace bt::python {

PyProblem::PyProblem(std::shared_ptr<Puzzle> puz, unsigned int idx)
    : puzzle_(std::move(puz)), problemIdx_(idx) {
  if (!puzzle_) {
    throw std::invalid_argument("Puzzle pointer is null");
  }
  if (problemIdx_ >= puzzle_->getNumberOfProblems()) {
    throw std::out_of_range("Problem index out of range");
  }
}

Problem& PyProblem::problem() const {
  // the problem may have gone since (the puzzle is shared)
  if (problemIdx_ >= puzzle_->getNumberOfProblems()) {
    throw std::out_of_range("Problem index out of range");
  }
  return puzzle_->getProblem(problemIdx_);
}

void PyProblem::checkShape(unsigned int shapeIdx) const {
  if (shapeIdx >= puzzle_->getNumberOfShapes()) {
    throw std::out_of_range("Shape index out of range");
  }
}

std::string PyProblem::getName() const { return problem().getName(); }

void PyProblem::setName(const std::string& name) { problem().setName(name); }

unsigned int PyProblem::getNumPieces() const { return problem().getNumberOfPieces(); }

void PyProblem::setResult(unsigned int shapeIdx) {
  checkShape(shapeIdx);
  problem().setResultId(*puzzle_, shapeIdx);
}

void PyProblem::setResultShape(const PyShape& shape) { setResult(shape.getIndex()); }

int PyProblem::getResult() const {
  const Problem& p = problem();
  if (!p.resultValid()) {
    return -1;
  }
  return static_cast<int>(p.getResultId());
}

void PyProblem::setPieceRange(unsigned int shapeIdx, unsigned int minCount,
                              unsigned int maxCount) {
  checkShape(shapeIdx);
  Problem& p = problem();
  p.setShapeMinimum(*puzzle_, shapeIdx, minCount);
  p.setShapeMaximum(*puzzle_, shapeIdx, maxCount);
}

void PyProblem::setPieceRangeShape(const PyShape& shape, unsigned int minCount,
                                   unsigned int maxCount) {
  setPieceRange(shape.getIndex(), minCount, maxCount);
}

void PyProblem::setPieceCount(unsigned int shapeIdx, unsigned int count) {
  setPieceRange(shapeIdx, count, count);
}

void PyProblem::setPieceCountShape(const PyShape& shape, unsigned int count) {
  setPieceRange(shape.getIndex(), count, count);
}

unsigned int PyProblem::getPieceMin(unsigned int shapeIdx) const {
  checkShape(shapeIdx);
  return problem().getShapeMinimum(shapeIdx);
}

unsigned int PyProblem::getPieceMax(unsigned int shapeIdx) const {
  checkShape(shapeIdx);
  return problem().getShapeMaximum(shapeIdx);
}

std::unique_ptr<SolutionIterator> PyProblem::solve(const IteratorOptions& options) const {
  (void)problem();
  return std::make_unique<SolutionIterator>(*puzzle_, problemIdx_, options);
}

}  // namespace bt::python
