// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The hexagonal prism grid (decision 0023). New: the original had no
/// such grid; after the hexagonal grid of BurrTools V2.

#include "shapelogic/grid/voxel.h"

namespace bt {

/// Voxel space of the hexagonal prism grid: stacked planes of hexagons in
/// rows, each row half a cell to the right of the one below
/// (hex_lattice.h), so that every move by whole coordinates keeps a
/// shape's look and a space is a parallelogram of cells. Cells touch at
/// their six sides, their top and bottom, and at the twelve edges with
/// the cells of the rows above and below, never at a corner alone.
/// Transformations turn about the centre of cell (0, 0, 0).
class VoxelHex : public Voxel {
 public:
  VoxelHex(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : Voxel(x, y, z, gt, init) {}
  VoxelHex(const VoxelHex&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelHex>(*this);
  }

  void transformPoint(int* x, int* y, int* z, unsigned int trans) const override;
  void getTransformMatrix(unsigned int trans, double m[9]) const override;
  bool transform(unsigned int nr) override;
  bool getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn, int* yn,
                   int* zn) const override;
  void resizeInclude(int& px, int& py, int& pz) override;
  [[nodiscard]] bool validCoordinate(int x, int y, int z) const override;
  [[nodiscard]] bool onGrid(int x, int y, int z) const override;
};

}  // namespace bt
