// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The rhombic grid. Ported from src/lib/voxel_3.h.

#include "bt/grid/voxel_bricks.h"

namespace bt {

/// Voxel space of the rhombic grid, emulated on cubes. Cutting a cube along
/// its six diagonal planes gives 24 identical small tetrahedra, the cells of
/// this grid. One such cube is a 5x5x5 block of the cube grid, its 24 cells
/// sitting as little diamonds on the six faces. Shapes stay aligned to the
/// 5-block: rotations, translations and sizes go in steps of 5. Rhombic
/// dodecahedra can be built from these cells.
class VoxelRhombic : public VoxelBricks {
 public:
  VoxelRhombic(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : VoxelBricks(x, y, z, gt, init) {}
  VoxelRhombic(const VoxelRhombic&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelRhombic>(*this);
  }

  bool transform(unsigned int nr) override;
  bool getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn, int* yn,
                   int* zn) const override;
  void scale(unsigned int amount, bool grid) override;
  bool scaleDown(unsigned char by, bool action) override;
  void resizeInclude(int& px, int& py, int& pz) override;
  void minimizePiece() override;
  [[nodiscard]] bool validCoordinate(int x, int y, int z) const override;
  [[nodiscard]] bool identicalInBB(const Voxel& op, bool includeColors = true) const override;
  [[nodiscard]] bool onGrid(int x, int y, int z) const override;
};

}  // namespace bt
