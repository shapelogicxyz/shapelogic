// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/python/py_shape.cpp.
#include "py_shape.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/puzzle.h"

#include <stdexcept>
#include <utility>

namespace bt::python {

PyShape::PyShape(std::shared_ptr<Puzzle> puz, unsigned int idx)
    : puzzle_(std::move(puz)), shapeIdx_(idx) {
  if (!puzzle_) {
    throw std::invalid_argument("Puzzle pointer is null");
  }
  if (shapeIdx_ >= puzzle_->getNumberOfShapes()) {
    throw std::out_of_range("Shape index out of range");
  }
}

Voxel& PyShape::voxel() const {
  // the shape may have gone since (the puzzle is shared)
  if (shapeIdx_ >= puzzle_->getNumberOfShapes()) {
    throw std::out_of_range("Shape index out of range");
  }
  return puzzle_->getShape(shapeIdx_);
}

Voxel& PyShape::cell(unsigned int x, unsigned int y, unsigned int z) const {
  Voxel& v = voxel();
  if (x >= v.getX() || y >= v.getY() || z >= v.getZ()) {
    // the original's assertion text, as its RuntimeError said it
    throw std::runtime_error("(x<sx)&&(y<sy)&&(z<sz)");
  }
  return v;
}

std::string PyShape::getName() const { return voxel().getName(); }

void PyShape::setName(const std::string& name) { voxel().setName(name); }

unsigned int PyShape::getSizeX() const { return voxel().getX(); }

unsigned int PyShape::getSizeY() const { return voxel().getY(); }

unsigned int PyShape::getSizeZ() const { return voxel().getZ(); }

std::tuple<unsigned int, unsigned int, unsigned int> PyShape::getDimensions() const {
  const Voxel& v = voxel();
  return {v.getX(), v.getY(), v.getZ()};
}

int PyShape::get(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).get(x, y, z);
}

void PyShape::set(unsigned int x, unsigned int y, unsigned int z, int val) {
  // a byte, as the original's voxel_type took it
  cell(x, y, z).set(x, y, z, static_cast<VoxelType>(val));
}

int PyShape::getState(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).getState(x, y, z);
}

void PyShape::setState(unsigned int x, unsigned int y, unsigned int z, int state) {
  cell(x, y, z).setState(x, y, z, state);
}

unsigned int PyShape::getColor(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).getColor(x, y, z);
}

void PyShape::setColor(unsigned int x, unsigned int y, unsigned int z, unsigned int color) {
  Voxel& v = cell(x, y, z);
  if (color >= 64) {
    throw std::runtime_error("color < 64");
  }
  v.setColor(x, y, z, color);
}

bool PyShape::isFilled(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).isFilled(x, y, z);
}

bool PyShape::isEmpty(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).isEmpty(x, y, z);
}

bool PyShape::isVariable(unsigned int x, unsigned int y, unsigned int z) const {
  return cell(x, y, z).isVariable(x, y, z);
}

void PyShape::fill(
    const std::vector<std::tuple<unsigned int, unsigned int, unsigned int>>& coords) {
  // The cells before one outside the shape are filled, as in the original;
  // but the bounding box is recalculated then too (the original's stayed
  // switched off, so the shape's empty cells kept their colours from then on)
  struct BatchEdit {
    Voxel& v;
    explicit BatchEdit(Voxel& voxel) : v(voxel) { v.skipRecalcBoundingBox(true); }
    ~BatchEdit() { v.skipRecalcBoundingBox(false); }
    BatchEdit(const BatchEdit&) = delete;
    BatchEdit& operator=(const BatchEdit&) = delete;
  };
  const BatchEdit batch(voxel());
  for (const auto& [x, y, z] : coords) {
    cell(x, y, z).set(x, y, z, Voxel::kFilled);
  }
}

unsigned int PyShape::countFilled() const { return voxel().countState(Voxel::kFilled); }

unsigned int PyShape::countEmpty() const { return voxel().countState(Voxel::kEmpty); }

}  // namespace bt::python
