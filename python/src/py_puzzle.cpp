// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/python/py_puzzle.cpp; the imports, the Puzzlecad export
// and the grid conversion are new (the core's of deferred part 6).
#include "py_puzzle.h"

#include "shapelogic/base/error.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/imports.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/grid_convert.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace bt::python {

namespace {

GridKind gridByName(const std::string& name) {
  for (int k = 0; k < kNumGridKinds; k++) {
    const auto kind = static_cast<GridKind>(k);
    if (toString(kind) == name) {
      return kind;
    }
  }
  throw std::invalid_argument("unknown grid: " + name);
}

template <class T>
T valueOrThrow(Result<T> r) {
  if (!r) {
    throw std::runtime_error(r.error().message);
  }
  return std::move(*r);
}

}  // namespace

PyPuzzle::PyPuzzle(const std::string& grid)
    : puzzle_(std::make_shared<Puzzle>(GridType(gridByName(grid)))) {}

PyPuzzle::PyPuzzle(std::shared_ptr<Puzzle> puz) : puzzle_(std::move(puz)) {
  if (!puzzle_) {
    throw std::invalid_argument("Puzzle pointer is null");
  }
}

std::shared_ptr<PyPuzzle> PyPuzzle::load(const std::string& filename) {
  return std::make_shared<PyPuzzle>(
      std::make_shared<Puzzle>(valueOrThrow(io::loadPuzzle(filename))));
}

std::shared_ptr<PyPuzzle> PyPuzzle::importPuzzleSolver3D(const std::string& filename) {
  const std::string text = valueOrThrow(io::readPlainFile(filename));
  return std::make_shared<PyPuzzle>(
      std::make_shared<Puzzle>(valueOrThrow(io::readPuzzleSolver3D(text))));
}

std::shared_ptr<PyPuzzle> PyPuzzle::importPuzzlecad(const std::string& filename) {
  const std::string text = valueOrThrow(io::readPlainFile(filename));
  return std::make_shared<PyPuzzle>(
      std::make_shared<Puzzle>(valueOrThrow(io::readPuzzlecad(text))));
}

void PyPuzzle::save(const std::string& filename) const {
  if (const Result<void> r = io::savePuzzle(*puzzle_, filename); !r) {
    throw std::runtime_error(r.error().message);
  }
}

std::string PyPuzzle::getComment() const { return puzzle_->getComment(); }

void PyPuzzle::setComment(const std::string& com) { puzzle_->setComment(com); }

std::string PyPuzzle::getGrid() const {
  return std::string(toString(puzzle_->getGridType().getType()));
}

unsigned int PyPuzzle::getNumShapes() const { return puzzle_->getNumberOfShapes(); }

unsigned int PyPuzzle::getNumProblems() const { return puzzle_->getNumberOfProblems(); }

PyShape PyPuzzle::addShape(unsigned int sx, unsigned int sy, unsigned int sz,
                           const std::string& name) {
  const unsigned int idx = puzzle_->addShape(sx, sy, sz);
  if (!name.empty()) {
    puzzle_->getShape(idx).setName(name);
  }
  return {puzzle_, idx};
}

std::vector<PyShape> PyPuzzle::getShapes() const {
  std::vector<PyShape> result;
  result.reserve(puzzle_->getNumberOfShapes());
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    result.emplace_back(puzzle_, i);
  }
  return result;
}

PyShape PyPuzzle::getShape(unsigned int idx) const { return {puzzle_, idx}; }

PyProblem PyPuzzle::addProblem(const std::string& name) {
  const unsigned int idx = puzzle_->addProblem();
  if (!name.empty()) {
    puzzle_->getProblem(idx).setName(name);
  }
  return {puzzle_, idx};
}

std::vector<PyProblem> PyPuzzle::getProblems() const {
  std::vector<PyProblem> result;
  result.reserve(puzzle_->getNumberOfProblems());
  for (unsigned int i = 0; i < puzzle_->getNumberOfProblems(); i++) {
    result.emplace_back(puzzle_, i);
  }
  return result;
}

PyProblem PyPuzzle::getProblem(unsigned int idx) const { return {puzzle_, idx}; }

std::string PyPuzzle::exportPuzzlecad(std::optional<unsigned int> problem,
                                      const std::string& name) const {
  // Document::exportPuzzlecad's refusals, in the original's words
  if (puzzle_->getGridType().getType() != GridKind::Bricks) {
    throw std::runtime_error("Puzzlecad export is only available for cube-grid puzzles.");
  }
  if (puzzle_->getNumberOfShapes() == 0) {
    throw std::runtime_error("Nothing to export.");
  }
  const Problem* pr = nullptr;
  if (problem) {
    if (*problem >= puzzle_->getNumberOfProblems()) {
      throw std::out_of_range("Problem index out of range");
    }
    pr = &puzzle_->getProblem(*problem);
  }
  std::optional<std::string> text = io::writePuzzlecad(*puzzle_, pr, name);
  if (!text) {
    throw std::runtime_error("Could not export puzzlecad file.");
  }
  return std::move(*text);
}

std::vector<std::string> PyPuzzle::convertTargets() const {
  std::vector<std::string> names;
  for (const GridKind k : bt::convertTargets(puzzle_->getGridType().getType())) {
    names.emplace_back(toString(k));
  }
  return names;
}

std::shared_ptr<PyPuzzle> PyPuzzle::convertGrid(const std::string& grid) const {
  return std::make_shared<PyPuzzle>(
      std::make_shared<Puzzle>(valueOrThrow(bt::convertGrid(*puzzle_, gridByName(grid)))));
}

}  // namespace bt::python
