// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The cube grid. Ported from src/lib/voxel_0.h.

#include "bt/grid/voxel.h"

namespace bt {

/// Voxel space of cubes. The rhombic and tetra-octa grids derive from it:
/// they lay a coarser super-grid over cubes and reuse its transformations.
class VoxelBricks : public Voxel {
 public:
  VoxelBricks(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : Voxel(x, y, z, gt, init) {}
  VoxelBricks(const VoxelBricks&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelBricks>(*this);
  }

  void transformPoint(int* x, int* y, int* z, unsigned int trans) const override;
  void getTransformMatrix(unsigned int trans, double m[9]) const override;
  bool transform(unsigned int nr) override;
  bool getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn, int* yn,
                   int* zn) const override;
  void scale(unsigned int amount, bool grid) override;
  bool scaleDown(unsigned char by, bool action) override;
  void resizeInclude(int& px, int& py, int& pz) override;
  [[nodiscard]] bool validCoordinate(int x, int y, int z) const override;
  [[nodiscard]] bool onGrid(int x, int y, int z) const override;
};

}  // namespace bt
