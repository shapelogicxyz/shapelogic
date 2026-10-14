// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel_4.cpp (everything except the meshes).

#include "shapelogic/grid/voxel_tetra_octa.h"

#include "shapelogic/base/int_cast.h"

namespace bt {

// All this grid needs is a size that is a multiple of 6; then the cube
// transformation does the rest.
bool VoxelTetraOcta::transform(unsigned int nr) {
  {
    int sx = asInt(getX());
    int sy = asInt(getY());
    int sz = asInt(getZ());

    sx += 5;
    sx -= sx % 6;
    sy += 5;
    sy -= sy % 6;
    sz += 5;
    sz -= sz % 6;

    resize(asUnsigned(sx), asUnsigned(sy), asUnsigned(sz), 0);
  }

  return VoxelBricks::transform(nr);
}

bool VoxelTetraOcta::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                                 int* yn, int* zn) const {
  // index 0: position of the cell, index 1: block type (even or odd block),
  // index 2: first entry the cell, then its neighbours, index 3: x, y, z
  // clang-format off
  static constexpr int kFaceNeighbors[5][2][5][3] = {
    {
      { {1, 1, 1}, /* cube position */ {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, 1, 1} },
      { {1, 1, 1}, /* cube position */ {-1, -1, -1}, {1, 1, -1}, {1, -1, 1}, {-1, 1, 1} }
    },
    {
      { {2, 0, 0}, /* cube position */ {-1, 1, 1}, {0, 0, -1}, {1, 0, 0}, {0, -1, 0} },
      { {0, 0, 0}, /* cube position */ {1, 1, 1}, {0, 0, -1}, {0, -1, 0}, {-1, 0, 0} }
    },
    {
      { {0, 2, 0}, /* cube position */ {1, -1, 1}, {0, 0, -1}, {0, 1, 0}, {-1, 0, 0} },
      { {2, 2, 0}, /* cube position */ {-1, -1, 1}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0} }
    },
    {
      { {0, 0, 2}, /* cube position */ {0, -1, 0}, {-1, 0, 0}, {1, 1, -1}, {0, 0, 1} },
      { {2, 0, 2}, /* cube position */ {0, -1, 0}, {1, 0, 0}, {-1, 1, -1}, {0, 0, 1} }
    },
    {
      { {2, 2, 2}, /* cube position */ {1, 0, 0}, {0, 1, 0}, {-1, -1, -1}, {0, 0, 1} },
      { {0, 2, 2}, /* cube position */ {-1, 0, 0}, {0, 1, 0}, {1, -1, -1}, {0, 0, 1} }
    },
  };

  // made with a helper function and the edge detection of the original's
  // voxel drawer; the unlisted rows stay zero, as in the original
  static constexpr int kEdgeNeighbors[24][18+1][3] = {
    { {0, 0, 2}, /* cube position */
      {0, -1, 1}, {0, 2, -2}, {-1, 0, 1}, {-1, -1, 0}, {1, 1, 2}, {1, -2, -1},
      {-1, 2, -2}, {2, 0, -2}, {-2, 1, -1}, {2, -1, -2}, {2, 2, 0}, {2, 2, 1},
    },
    { {0, 2, 0}, /* cube position */
      {0, 1, -1}, {0, -2, 2}, {-1, 0, -1}, {-1, 1, 0}, {1, -1, -2}, {1, 2, 1},
      {-1, -2, 2}, {2, 0, 2}, {-2, -1, 1}, {2, 1, 2}, {2, -2, 0}, {2, -2, -1},
    },
    { {1, 1, 1}, /* cube position */
      {0, 0, 3}, {0, 0, -3}, {0, -3, 0}, {0, 3, 0}, {-1, -1, 2}, {-1, 1, -2},
      {1, -1, -2}, {1, 1, 2}, {-1, -2, 1}, {-1, 2, -1}, {1, -2, -1}, {1, 2, 1},
      {-2, -1, 1}, {-2, 1, -1}, {2, -1, -1}, {2, 1, 1}, {-3, 0, 0}, {3, 0, 0},
    },
    { {2, 0, 0}, /* cube position */
      {0, -1, -1}, {0, 2, 2}, {1, 0, -1}, {1, -1, 0}, {-1, 1, -2}, {-1, -2, 1},
      {1, 2, 2}, {-2, 0, 2}, {2, 1, 1}, {-2, -1, 2}, {-2, 2, 0}, {-2, 2, -1},
    },
    { {2, 2, 2}, /* cube position */
      {0, 1, 1}, {0, -2, -2}, {1, 0, 1}, {1, 1, 0}, {-1, -1, 2}, {-1, 2, -1},
      {1, -2, -2}, {-2, 0, -2}, {2, -1, -1}, {-2, 1, -2}, {-2, -2, 0}, {-2, -2, 1},
    },


    { {3, 0, 0}, /* cube position */
      {0, -1, -1}, {0, 2, 2}, {-1, 0, -1}, {-1, -1, 0}, {1, 1, -2}, {1, -2, 1},
      {-1, 2, 2}, {2, 0, 2}, {-2, 1, 1}, {2, -1, 2}, {2, 2, 0}, {2, 2, -1},
    },
    { {3, 2, 2}, /* cube position */
      {0, 1, 1}, {0, -2, -2}, {-1, 0, 1}, {-1, 1, 0}, {1, -1, 2}, {1, 2, -1},
      {-1, -2, -2}, {2, 0, -2}, {-2, -1, -1}, {2, 1, -2}, {2, -2, 0}, {2, -2, 1},
    },
    { {4, 1, 1}, /* cube position */
      {0, 0, 3}, {0, 0, -3}, {0, -3, 0}, {0, 3, 0}, {-1, -1, -2}, {-1, 1, 2},
      {1, -1, 2}, {1, 1, -2}, {-1, -2, -1}, {-1, 2, 1}, {1, -2, 1}, {1, 2, -1},
      {-2, -1, -1}, {-2, 1, 1}, {2, -1, 1}, {2, 1, -1}, {-3, 0, 0}, {3, 0, 0},
    },
    { {5, 0, 2}, /* cube position */
      {0, -1, 1}, {0, 2, -2}, {1, 0, 1}, {1, -1, 0}, {-1, 1, 2}, {-1, -2, -1},
      {1, 2, -2}, {-2, 0, -2}, {2, 1, -1}, {-2, -1, -2}, {-2, 2, 0}, {-2, 2, 1},
    },
    { {5, 2, 0}, /* cube position */
      {0, 1, -1}, {0, -2, 2}, {1, 0, -1}, {1, 1, 0}, {-1, -1, -2}, {-1, 2, 1},
      {1, -2, 2}, {-2, 0, 2}, {2, -1, 1}, {-2, 1, 2}, {-2, -2, 0}, {-2, -2, -1},
    }
  };
  // clang-format on

  const int xc = intDivFloor(x, 3);
  const int yc = intDivFloor(y, 3);
  const int zc = intDivFloor(z, 3);

  int xs = x - 3 * xc;
  const int ys = y - 3 * yc;
  const int zs = z - 3 * zc;

  const int cubeType = (xc + yc + zc) & 1;

  switch (typ) {
    case 0:
      if (idx >= 4) {
        return false;
      }
      for (const auto& f : kFaceNeighbors) {
        const auto& e = f[cubeType];
        if (e[0][0] == xs && e[0][1] == ys && e[0][2] == zs) {
          *xn = x + e[idx + 1][0];
          *yn = y + e[idx + 1][1];
          *zn = z + e[idx + 1][2];
          return true;
        }
      }
      return false;

    case 1:
      if (xs == 1 && ys == 1 && zs == 1) {
        if (idx >= 18) {
          return false;
        }
      } else {
        if (idx >= 12) {
          return false;
        }
      }

      if (cubeType) {
        xs += 3;
      }

      for (const auto& e : kEdgeNeighbors) {
        if (e[0][0] == xs && e[0][1] == ys && e[0][2] == zs) {
          *xn = x + e[idx + 1][0];
          *yn = y + e[idx + 1][1];
          *zn = z + e[idx + 1][2];
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

// Cutting planes divide a block into its parts: find on which side of each
// plane a cell lies, then take the cells on the same sides of the scaled
// planes.
void VoxelTetraOcta::scale(unsigned int amount, bool /*grid*/) {
  // the coordinates of the cells of a 3x3x3 block, per block type
  // clang-format off
  static constexpr int kVoxelCoords[2][5][3] = {
    { { 1, 1, 1 }, { 0, 2, 0 }, { 2, 0, 0 }, { 0, 0, 2 }, { 2, 2, 2 } },
    { { 1, 1, 1 }, { 0, 0, 0 }, { 2, 2, 0 }, { 2, 0, 2 }, { 0, 2, 2 } },
  };

  // the 2x4 cutting planes
  static constexpr int kPlanes[2][4][3] = {
    { {-1, 1, -1}, {1, -1, -1}, {-1, -1, 1}, {1, 1, 1} },
    { {1, 1, -1}, {-1, -1, -1}, {-1, 1, 1}, {1, -1, 1} },
  };
  // clang-format on

  const unsigned int nsx = ((sx_ + 2) / 3) * amount * 3;
  const unsigned int nsy = ((sy_ + 2) / 3) * amount * 3;
  const unsigned int nsz = ((sz_ + 2) / 3) * amount * 3;
  std::vector<VoxelType> s2(nsx * nsy * nsz, kEmpty);

  // each 3x3x3 block is scaled
  const unsigned int bsx = (sx_ + 2) / 3;
  const unsigned int bsy = (sy_ + 2) / 3;
  const unsigned int bsz = (sz_ + 2) / 3;

  const double am = amount;

  for (unsigned int bx = 0; bx < bsx; bx++) {
    for (unsigned int by = 0; by < bsy; by++) {
      for (unsigned int bz = 0; bz < bsz; bz++) {
        // the type of the source block
        const unsigned int blockType = (bx + by + bz) & 1;

        // every cell of the source block
        for (const auto& c1 : kVoxelCoords[blockType]) {
          const int x = c1[0];
          const int y = c1[1];
          const int z = c1[2];

          const int px = asInt(3 * bx) + x;
          const int py = asInt(3 * by) + y;
          const int pz = asInt(3 * bz) + z;
          if (!get2(px, py, pz)) {
            continue;
          }

          // where the source cell is relative to the cutting planes
          int planeMask = 0;
          for (unsigned int i = 0; i < 4; i++) {
            const int* pl = kPlanes[blockType][i];
            if ((x - 1) * pl[0] + (y - 1) * pl[1] + (z - 1) * pl[2] <= 1) {
              planeMask |= 1 << i;
            }
          }

          // every destination block of this source block
          for (unsigned int amx = 0; amx < amount; amx++) {
            for (unsigned int amy = 0; amy < amount; amy++) {
              for (unsigned int amz = 0; amz < amount; amz++) {
                // the type of that block
                const unsigned int blockType2 = (amount * (bx + by + bz) + amx + amy + amz) & 1;

                // every cell of that block
                for (const auto& c2 : kVoxelCoords[blockType2]) {
                  const int ax = asInt(3 * amx) + c2[0];
                  const int ay = asInt(3 * amy) + c2[1];
                  const int az = asInt(3 * amz) + c2[2];

                  bool useVoxel = true;
                  for (unsigned int i = 0; i < 4; i++) {
                    const int* pl = kPlanes[blockType][i];
                    if (((ax - 1.5 * am + 0.5) * pl[0] + (ay - 1.5 * am + 0.5) * pl[1] +
                             (az - 1.5 * am + 0.5) * pl[2] <
                         (1.5 * am - 0.5)) != ((planeMask & (1 << i)) != 0)) {
                      useVoxel = false;
                      break;
                    }
                  }

                  if (useVoxel) {
                    s2[(bx * amount * 3 + asUnsigned(ax)) +
                       nsx * ((by * amount * 3 + asUnsigned(ay)) +
                              nsy * (bz * amount * 3 + asUnsigned(az)))] =
                        get(asUnsigned(px), asUnsigned(py), asUnsigned(pz));
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  space_ = std::move(s2);

  sx_ = nsx;
  sy_ = nsy;
  sz_ = nsz;

  hx_ = hy_ = hz_ = 0;

  voxels_ = sx_ * sy_ * sz_;

  recalcBoundingBox();
}

bool VoxelTetraOcta::scaleDown(unsigned char /*by*/, bool /*action*/) { return false; }

// Grows in multiples of 6 at the low end, so blocks keep their type.
void VoxelTetraOcta::resizeInclude(int& px, int& py, int& pz) {
  int nsx = asInt(getX());
  int nsy = asInt(getY());
  int nsz = asInt(getZ());
  int tx = 0;
  int ty = 0;
  int tz = 0;

  while (px < 0) {
    nsx += 6;
    tx += 6;
    px += 6;
  }
  while (py < 0) {
    nsy += 6;
    ty += 6;
    py += 6;
  }
  while (pz < 0) {
    nsz += 6;
    tz += 6;
    pz += 6;
  }
  if (px >= asInt(getX())) {
    nsx += px - asInt(getX()) + 1;
  }
  if (py >= asInt(getY())) {
    nsy += py - asInt(getY()) + 1;
  }
  if (pz >= asInt(getZ())) {
    nsz += pz - asInt(getZ()) + 1;
  }

  if (nsx % 6) {
    nsx += 6 - nsx % 6;
  }
  if (nsy % 6) {
    nsy += 6 - nsy % 6;
  }
  if (nsz % 6) {
    nsz += 6 - nsz % 6;
  }

  resize(asUnsigned(nsx), asUnsigned(nsy), asUnsigned(nsz), 0);
  translate(tx, ty, tz, 0);
}

void VoxelTetraOcta::minimizePiece() {
  // the low corner must come back to the same position modulo 6
  const unsigned int x = bx1_ % 6;
  const unsigned int y = by1_ % 6;
  const unsigned int z = bz1_ % 6;

  Voxel::minimizePiece();

  if (x || y || z) {
    resize(sx_ + x, sy_ + y, sz_ + z, 0);
    translate(asInt(x), asInt(y), asInt(z), 0);
  }

  // and the size must be a multiple of 3
  if (sx_ % 3 || sy_ % 3 || sz_ % 3) {
    resize(sx_ + (3 - sx_ % 3) % 3, sy_ + (3 - sy_ % 3) % 3, sz_ + (3 - sz_ % 3) % 3, 0);
  }
}

bool VoxelTetraOcta::validCoordinate(int x, int y, int z) const {
  const int xc = intDivFloor(x, 3);
  const int yc = intDivFloor(y, 3);
  const int zc = intDivFloor(z, 3);

  const int xs = x - 3 * xc;
  const int ys = y - 3 * yc;
  const int zs = z - 3 * zc;

  if (xs == 1 && ys == 1 && zs == 1) {
    return true;
  }

  if ((xc + yc + zc) & 1) {
    return (xs == 0 && ys == 0 && zs == 0) || (xs == 2 && ys == 2 && zs == 0) ||
           (xs == 0 && ys == 2 && zs == 2) || (xs == 2 && ys == 0 && zs == 2);
  }
  return (xs == 2 && ys == 0 && zs == 0) || (xs == 0 && ys == 2 && zs == 0) ||
         (xs == 2 && ys == 2 && zs == 2) || (xs == 0 && ys == 0 && zs == 2);
}

bool VoxelTetraOcta::identicalInBB(const Voxel& op, bool includeColors) const {
  // the low corners of the boxes must be at the same place within a block,
  // and in blocks of the same type
  return bx1_ % 3 == op.boundX1() % 3 && by1_ % 3 == op.boundY1() % 3 &&
         bz1_ % 3 == op.boundZ1() % 3 &&
         ((bx1_ / 3 + by1_ / 3 + bz1_ / 3) & 1) ==
             ((op.boundX1() / 3 + op.boundY1() / 3 + op.boundZ1() / 3) & 1) &&
         Voxel::identicalInBB(op, includeColors);
}

bool VoxelTetraOcta::onGrid(int x, int y, int z) const {
  // the shape does not fit when off the 3-raster or in a block of the other type
  return x % 3 == 0 && y % 3 == 0 && z % 3 == 0 && ((x / 3 + y / 3 + z / 3) & 1) == 0;
}

}  // namespace bt
