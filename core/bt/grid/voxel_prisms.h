// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The triangular prism grid. Ported from src/lib/voxel_1.h.

#include "bt/grid/voxel.h"

namespace bt {

/// Voxel space of the triangular prism grid: stacked planes of triangles. Half the triangles of a
/// plane point up, half down, so steps along x and y keep the cell type only in twos; z steps are
/// single.
class VoxelPrisms : public Voxel {
 public:
  VoxelPrisms(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : Voxel(x, y, z, gt, init) {}
  VoxelPrisms(const VoxelPrisms&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelPrisms>(*this);
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
  void scale(unsigned int amount, bool grid) override;
};

}  // namespace bt
