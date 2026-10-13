// SPDX-License-Identifier: GPL-3.0-or-later
// A puzzle, as the Python module shows it.
// Ported from src/python/py_puzzle.h.
#pragma once

#include "py_problem.h"
#include "py_shape.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bt {
class Puzzle;
}  // namespace bt

namespace bt::python {

/// A puzzle (the module's `Puzzle`), shared with its shapes and problems.
class PyPuzzle {
 public:
  /// An empty puzzle on the grid named `grid` (std::invalid_argument for
  /// a name that is none of the grids').
  explicit PyPuzzle(const std::string& grid = "bricks");
  explicit PyPuzzle(std::shared_ptr<Puzzle> puz);

  /// std::runtime_error (RuntimeError) when the file cannot be read.
  static std::shared_ptr<PyPuzzle> load(const std::string& filename);
  /// The puzzle from a PuzzleSolver3D (.puz) or a Puzzlecad (.scad) file.
  static std::shared_ptr<PyPuzzle> importPuzzleSolver3D(const std::string& filename);
  static std::shared_ptr<PyPuzzle> importPuzzlecad(const std::string& filename);

  void save(const std::string& filename) const;

  [[nodiscard]] std::string getComment() const;
  void setComment(const std::string& com);

  /// The grid's name ("bricks", "sliding", ...).
  [[nodiscard]] std::string getGrid() const;

  [[nodiscard]] unsigned int getNumShapes() const;
  [[nodiscard]] unsigned int getNumProblems() const;

  PyShape addShape(unsigned int sx, unsigned int sy, unsigned int sz, const std::string& name = "");
  [[nodiscard]] std::vector<PyShape> getShapes() const;
  [[nodiscard]] PyShape getShape(unsigned int idx) const;

  PyProblem addProblem(const std::string& name = "");
  [[nodiscard]] std::vector<PyProblem> getProblems() const;
  [[nodiscard]] PyProblem getProblem(unsigned int idx) const;

  /// The Puzzlecad file of a problem's pieces (every shape without one),
  /// with the refusals of the original's File > Export > Puzzlecad.
  [[nodiscard]] std::string exportPuzzlecad(std::optional<unsigned int> problem,
                                            const std::string& name) const;
  /// The grids this puzzle converts to, and a copy converted to one.
  [[nodiscard]] std::vector<std::string> convertTargets() const;
  [[nodiscard]] std::shared_ptr<PyPuzzle> convertGrid(const std::string& grid) const;

 private:
  std::shared_ptr<Puzzle> puzzle_;
};

}  // namespace bt::python
