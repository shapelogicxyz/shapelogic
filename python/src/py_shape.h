// SPDX-License-Identifier: GPL-3.0-or-later
// A shape of a puzzle, as the Python module shows it: the puzzle and the
// shape's index, so it always reads the puzzle as it is now.
// Ported from src/python/py_shape.h.
#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace bt {
class Puzzle;
class Voxel;
}  // namespace bt

namespace bt::python {

/// A shape of a puzzle (the module's `Shape`).
class PyShape {
 public:
  /// std::out_of_range (IndexError) when the puzzle has no shape `idx`.
  PyShape(std::shared_ptr<Puzzle> puz, unsigned int idx);

  [[nodiscard]] unsigned int getIndex() const { return shapeIdx_; }

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  [[nodiscard]] unsigned int getSizeX() const;
  [[nodiscard]] unsigned int getSizeY() const;
  [[nodiscard]] unsigned int getSizeZ() const;
  [[nodiscard]] std::tuple<unsigned int, unsigned int, unsigned int> getDimensions() const;

  /// The cell's whole value (state in the low two bits, colour above).
  [[nodiscard]] int get(unsigned int x, unsigned int y, unsigned int z) const;
  void set(unsigned int x, unsigned int y, unsigned int z, int val);

  [[nodiscard]] int getState(unsigned int x, unsigned int y, unsigned int z) const;
  void setState(unsigned int x, unsigned int y, unsigned int z, int state);

  [[nodiscard]] unsigned int getColor(unsigned int x, unsigned int y, unsigned int z) const;
  void setColor(unsigned int x, unsigned int y, unsigned int z, unsigned int color);

  [[nodiscard]] bool isFilled(unsigned int x, unsigned int y, unsigned int z) const;
  [[nodiscard]] bool isEmpty(unsigned int x, unsigned int y, unsigned int z) const;
  [[nodiscard]] bool isVariable(unsigned int x, unsigned int y, unsigned int z) const;

  /// Fills the cells at the coordinates; at one outside the shape, those
  /// before it are filled and std::runtime_error is thrown.
  void fill(const std::vector<std::tuple<unsigned int, unsigned int, unsigned int>>& coords);
  [[nodiscard]] unsigned int countFilled() const;
  [[nodiscard]] unsigned int countEmpty() const;

 private:
  std::shared_ptr<Puzzle> puzzle_;
  unsigned int shapeIdx_;
  [[nodiscard]] Voxel& voxel() const;
  /// The voxel, after checking the cell is inside it (std::runtime_error,
  /// RuntimeError, as the original's assertion was).
  [[nodiscard]] Voxel& cell(unsigned int x, unsigned int y, unsigned int z) const;
};

}  // namespace bt::python
