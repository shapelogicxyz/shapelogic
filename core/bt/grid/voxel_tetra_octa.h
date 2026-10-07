// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The tetrahedra-octahedra grid. Ported from src/lib/voxel_4.h.

#include "bt/grid/voxel_bricks.h"

namespace bt {

/// Voxel space of the tetrahedra-octahedra grid, emulated on cubes. A
/// regular tetrahedron inscribed in a cube leaves four small irregular
/// tetrahedra at the corners; four of those make an octahedron. Each such
/// cube is a 3x3x3 block of the cube grid: the centre cell is the regular
/// tetrahedron, four corners the small ones. Blocks come in two mirror
/// orientations in a checkerboard pattern, so shapes stay aligned to steps
/// of 6.
class VoxelTetraOcta : public VoxelBricks {
 public:
  VoxelTetraOcta(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : VoxelBricks(x, y, z, gt, init) {}
  VoxelTetraOcta(const VoxelTetraOcta&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelTetraOcta>(*this);
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
