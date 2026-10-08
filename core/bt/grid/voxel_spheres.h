// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The sphere grid. Ported from src/lib/voxel_2.h.

#include "bt/grid/voxel.h"

namespace bt {

/// Voxel space of the sphere grid: tightly packed spheres, placed on the cells of a 3D checkerboard
/// (x+y+z even). It is the only grid where a transformation can fail: many of its 120 rotations map
/// some shapes off the lattice.
class VoxelSpheres : public Voxel {
 public:
  VoxelSpheres(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init = 0)
      : Voxel(x, y, z, gt, init) {}
  VoxelSpheres(const VoxelSpheres&) = default;

  [[nodiscard]] std::unique_ptr<Voxel> clone() const override {
    return std::make_unique<VoxelSpheres>(*this);
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
  void initHotspot() override;
  [[nodiscard]] bool hasInitialHotspot() const override;

 private:
  bool nearestSphere(int& hx, int& hy, int& hz) const;
};

}  // namespace bt
