// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The cairo prism grid (decision 0023). New: the original had no such
/// grid; after the cairo grid of BurrTools V2.

#include "shapelogic/grid/voxel.h"

namespace bt {

/// Voxel space of the cairo prism grid: stacked planes of the Cairo
/// pentagonal tiling (cairo_lattice.h). Only the coordinates with x + y
/// odd are cells, of four kinds; a shape keeps its look when moved by a
/// lattice vector (both of x and y even, their difference a multiple of
/// 4) and by any number of layers. Transformations turn about the 4-fold
/// centre at the lattice point (0, 0); the mirrors and turns over, which
/// are glides of the tiling, land on the right cells by an offset found
/// once from the pentagons.
class VoxelCairo : public Voxel {
 public:
  VoxelCairo(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : Voxel(x, y, z, gt, init) {}
  VoxelCairo(const VoxelCairo&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelCairo>(*this);
  }

  void transformPoint(int* x, int* y, int* z, unsigned int trans) const override;
  void getTransformMatrix(unsigned int trans, double m[9]) const override;
  bool transform(unsigned int nr) override;
  void minimizePiece() override;
  bool getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn, int* yn,
                   int* zn) const override;
  void resizeInclude(int& px, int& py, int& pz) override;
  [[nodiscard]] bool validCoordinate(int x, int y, int z) const override;
  [[nodiscard]] bool onGrid(int x, int y, int z) const override;
  [[nodiscard]] bool identicalInBB(const Voxel& op, bool includeColors = true) const override;
};

}  // namespace bt
