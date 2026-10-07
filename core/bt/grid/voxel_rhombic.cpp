// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxel_3.cpp (everything except the meshes).

#include "bt/grid/voxel_rhombic.h"

#include "bt/base/int_cast.h"

#include <cmath>

namespace bt {

bool VoxelRhombic::transform(unsigned int nr) {
  // first make all three sizes a multiple of 5
  {
    int sx = asInt(getX());
    int sy = asInt(getY());
    int sz = asInt(getZ());

    sx += 4;
    sx -= sx % 5;
    sy += 4;
    sy -= sy % 5;
    sz += 4;
    sz -= sz % 5;

    resize(asUnsigned(sx), asUnsigned(sy), asUnsigned(sz), 0);
  }

  return VoxelBricks::transform(nr);
}

namespace {

void coordinateToggling(int a, int b, int as, int bs, int* an, int* bn) {
  // clang-format off
  if (a == 0) {      if (b == 1) { *an = as+1; *bn = bs-1; }
                     else        { *an = as+1; *bn = bs+1; } }
  else if (a == 1) { if (b == 0) { *an = as-1; *bn = bs+1; }
                     else        { *an = as-1; *bn = bs-1; } }
  else if (a == 3) { if (b == 0) { *an = as+1; *bn = bs+1; }
                     else        { *an = as+1; *bn = bs-1; } }
  else {             if (b == 1) { *an = as-1; *bn = bs-1; }
                     else        { *an = as-1; *bn = bs+1; } }
  // clang-format on
}

}  // namespace

bool VoxelRhombic::getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                               int* yn, int* zn) const {
  // made with a helper function and the edge detection of the original's
  // voxel drawer
  // clang-format off
  static constexpr int kEdgeNeighbors[24][14+1][3] = {
    { {0, 1, 2}, // cube position
      {-1, -3, 0}, {-1, 1, -1}, {-1, 1, 1}, {-2, -1, 0}, {-2, -2, 0}, {0, -3, 0}, {0, 2, 0},
      {1, -2, 0}, {1, 1, -2}, {1, 1, 2}, {2, -1, -1}, {2, -1, 1}, {2, 0, -2}, {2, 0, 2}
    },
    { {0, 2, 1}, // cube position
      {-1, -1, 1}, {-1, 0, -3}, {-1, 1, 1}, {-2, 0, -1}, {-2, 0, -2}, {0, 0, -3}, {0, 0, 2},
      {1, -2, 1}, {1, 0, -2}, {1, 2, 1}, {2, -1, -1}, {2, -2, 0}, {2, 1, -1}, {2, 2, 0}
    },
    { {0, 2, 3}, // cube position
      {-1, -1, -1}, {-1, 0, 3}, {-1, 1, -1}, {-2, 0, 1}, {-2, 0, 2}, {0, 0, -2}, {0, 0, 3},
      {1, -2, -1}, {1, 0, 2}, {1, 2, -1}, {2, -1, 1}, {2, -2, 0}, {2, 1, 1}, {2, 2, 0}
    },
    { {0, 3, 2}, // cube position
      {-1, -1, -1}, {-1, -1, 1}, {-1, 3, 0}, {-2, 1, 0}, {-2, 2, 0}, {0, -2, 0}, {0, 3, 0},
      {1, -1, -2}, {1, -1, 2}, {1, 2, 0}, {2, 0, -2}, {2, 0, 2}, {2, 1, -1}, {2, 1, 1}
    },
    { {1, 0, 2}, // cube position
      {-1, -2, 0}, {-1, 2, -1}, {-1, 2, 1}, {-2, -2, 0}, {-2, 1, 0}, {-3, -1, 0}, {-3, 0, 0},
      {0, 2, -2}, {0, 2, 2}, {1, -1, -1}, {1, -1, 1}, {1, 1, -2}, {1, 1, 2}, {2, 0, 0}
    },
    { {1, 2, 0}, // cube position
      {-1, -1, 2}, {-1, 0, -2}, {-1, 1, 2}, {-2, 0, -2}, {-2, 0, 1}, {-3, 0, -1}, {-3, 0, 0},
      {0, -2, 2}, {0, 2, 2}, {1, -1, -1}, {1, -2, 1}, {1, 1, -1}, {1, 2, 1}, {2, 0, 0}
    },
    { {2, 0, 1}, // cube position
      {-1, -1, 1}, {-1, 2, -1}, {-2, 1, 1}, {-2, 2, 0}, {0, -1, -3}, {0, -2, -1}, {0, -2, -2},
      {0, 0, -3}, {0, 0, 2}, {0, 1, -2}, {1, -1, 1}, {1, 2, -1}, {2, 1, 1}, {2, 2, 0}
    },
    { {2, 0, 3}, // cube position
      {-1, -1, -1}, {-1, 2, 1}, {-2, 1, -1}, {-2, 2, 0}, {0, -1, 3}, {0, -2, 1}, {0, -2, 2},
      {0, 0, -2}, {0, 0, 3}, {0, 1, 2}, {1, -1, -1}, {1, 2, 1}, {2, 1, -1}, {2, 2, 0}
    },
    { {2, 1, 0}, // cube position
      {-1, -1, 2}, {-1, 1, -1}, {-2, 0, 2}, {-2, 1, 1}, {0, -1, -2}, {0, -2, -2}, {0, -2, 1},
      {0, -3, -1}, {0, -3, 0}, {0, 2, 0}, {1, -1, 2}, {1, 1, -1}, {2, 0, 2}, {2, 1, 1}
    },
    { {2, 3, 0}, // cube position
      {-1, -1, -1}, {-1, 1, 2}, {-2, -1, 1}, {-2, 0, 2}, {0, -2, 0}, {0, 1, -2}, {0, 2, -2},
      {0, 2, 1}, {0, 3, -1}, {0, 3, 0}, {1, -1, -1}, {1, 1, 2}, {2, -1, 1}, {2, 0, 2}
    },
    { {3, 0, 2}, // cube position
      {-1, -1, -1}, {-1, -1, 1}, {-1, 1, -2}, {-1, 1, 2}, {-2, 0, 0}, {0, 2, -2}, {0, 2, 2},
      {1, -2, 0}, {1, 2, -1}, {1, 2, 1}, {2, -2, 0}, {2, 1, 0}, {3, -1, 0}, {3, 0, 0}
    },
    { {3, 2, 0}, // cube position
      {-1, -1, -1}, {-1, -2, 1}, {-1, 1, -1}, {-1, 2, 1}, {-2, 0, 0}, {0, -2, 2}, {0, 2, 2},
      {1, -1, 2}, {1, 0, -2}, {1, 1, 2}, {2, 0, -2}, {2, 0, 1}, {3, 0, -1}, {3, 0, 0}
    },
    { {1, 2, 4}, // cube position
      {-1, -1, -2}, {-1, 0, 2}, {-1, 1, -2}, {-2, 0, -1}, {-2, 0, 2}, {-3, 0, 0}, {-3, 0, 1},
      {0, -2, -2}, {0, 2, -2}, {1, -1, 1}, {1, -2, -1}, {1, 1, 1}, {1, 2, -1}, {2, 0, 0},
    },
    { {1, 4, 2}, // cube position
      {-1, -2, -1}, {-1, -2, 1}, {-1, 2, 0}, {-2, -1, 0}, {-2, 2, 0}, {-3, 0, 0}, {-3, 1, 0},
      {0, -2, -2}, {0, -2, 2}, {1, -1, -2}, {1, -1, 2}, {1, 1, -1}, {1, 1, 1}, {2, 0, 0},
    },
    { {2, 1, 4}, // cube position
      {-1, -1, -2}, {-1, 1, 1}, {-2, 0, -2}, {-2, 1, -1}, {0, -1, 2}, {0, -2, -1}, {0, -2, 2},
      {0, -3, 0}, {0, -3, 1}, {0, 2, 0}, {1, -1, -2}, {1, 1, 1}, {2, 0, -2}, {2, 1, -1},
    },
    { {2, 3, 4}, // cube position
      {-1, -1, 1}, {-1, 1, -2}, {-2, -1, -1}, {-2, 0, -2}, {0, -2, 0}, {0, 1, 2}, {0, 2, -1},
      {0, 2, 2}, {0, 3, 0}, {0, 3, 1}, {1, -1, 1}, {1, 1, -2}, {2, -1, -1}, {2, 0, -2},
    },
    { {2, 4, 1}, // cube position
      {-1, -2, -1}, {-1, 1, 1}, {-2, -1, 1}, {-2, -2, 0}, {0, -1, -2}, {0, 0, -3}, {0, 0, 2},
      {0, 1, -3}, {0, 2, -1}, {0, 2, -2}, {1, -2, -1}, {1, 1, 1}, {2, -1, 1}, {2, -2, 0},
    },
    { {2, 4, 3}, // cube position
      {-1, -2, 1}, {-1, 1, -1}, {-2, -1, -1}, {-2, -2, 0}, {0, -1, 2}, {0, 0, -2}, {0, 0, 3},
      {0, 1, 3}, {0, 2, 1}, {0, 2, 2}, {1, -2, 1}, {1, 1, -1}, {2, -1, -1}, {2, -2, 0},
    },
    { {3, 2, 4}, // cube position
      {-1, -1, 1}, {-1, -2, -1}, {-1, 1, 1}, {-1, 2, -1}, {-2, 0, 0}, {0, -2, -2}, {0, 2, -2},
      {1, -1, -2}, {1, 0, 2}, {1, 1, -2}, {2, 0, -1}, {2, 0, 2}, {3, 0, 0}, {3, 0, 1},
    },
    { {3, 4, 2}, // cube position
      {-1, -1, -2}, {-1, -1, 2}, {-1, 1, -1}, {-1, 1, 1}, {-2, 0, 0}, {0, -2, -2}, {0, -2, 2},
      {1, -2, -1}, {1, -2, 1}, {1, 2, 0}, {2, -1, 0}, {2, 2, 0}, {3, 0, 0}, {3, 1, 0},
    },
    { {4, 1, 2}, // cube position
      {-1, -2, 0}, {-1, 1, -2}, {-1, 1, 2}, {-2, -1, -1}, {-2, -1, 1}, {-2, 0, -2}, {-2, 0, 2},
      {0, -3, 0}, {0, 2, 0}, {1, -3, 0}, {1, 1, -1}, {1, 1, 1}, {2, -1, 0}, {2, -2, 0},
    },
    { {4, 2, 1}, // cube position
      {-1, -2, 1}, {-1, 0, -2}, {-1, 2, 1}, {-2, -1, -1}, {-2, -2, 0}, {-2, 1, -1}, {-2, 2, 0},
      {0, 0, -3}, {0, 0, 2}, {1, -1, 1}, {1, 0, -3}, {1, 1, 1}, {2, 0, -1}, {2, 0, -2},
    },
    { {4, 2, 3}, // cube position
      {-1, -2, -1}, {-1, 0, 2}, {-1, 2, -1}, {-2, -1, 1}, {-2, -2, 0}, {-2, 1, 1}, {-2, 2, 0},
      {0, 0, -2}, {0, 0, 3}, {1, -1, -1}, {1, 0, 3}, {1, 1, -1}, {2, 0, 1}, {2, 0, 2},
    },
    { {4, 3, 2}, // cube position
      {-1, -1, -2}, {-1, -1, 2}, {-1, 2, 0}, {-2, 0, -2}, {-2, 0, 2}, {-2, 1, -1}, {-2, 1, 1},
      {0, -2, 0}, {0, 3, 0}, {1, -1, -1}, {1, -1, 1}, {1, 3, 0}, {2, 1, 0}, {2, 2, 0},
    },
  };
  // clang-format on

  const int xc = intDivFloor(x, 5);
  const int yc = intDivFloor(y, 5);
  const int zc = intDivFloor(z, 5);

  const int xs = x - 5 * xc;
  const int ys = y - 5 * yc;
  const int zs = z - 5 * zc;

  *xn = x;
  *yn = y;
  *zn = z;

  switch (typ) {
    case 0:
      switch (idx) {
        case 0:
          if (xs == 0 || xs == 4) {
            *yn = ys == 2 ? y - 1 : 5 * yc + 2;
            *zn = zs == 2 ? z - 1 : 5 * zc + 2;
          } else if (ys == 0 || ys == 4) {
            *xn = xs == 2 ? x - 1 : 5 * xc + 2;
            *zn = zs == 2 ? z - 1 : 5 * zc + 2;
          } else {
            *xn = xs == 2 ? x - 1 : 5 * xc + 2;
            *yn = ys == 2 ? y - 1 : 5 * yc + 2;
          }
          return true;

        case 1:
          if (xs == 0 || xs == 4) {
            *yn = ys == 2 ? y + 1 : 5 * yc + 2;
            *zn = zs == 2 ? z + 1 : 5 * zc + 2;
          } else if (ys == 0 || ys == 4) {
            *xn = xs == 2 ? x + 1 : 5 * xc + 2;
            *zn = zs == 2 ? z + 1 : 5 * zc + 2;
          } else {
            *xn = xs == 2 ? x + 1 : 5 * xc + 2;
            *yn = ys == 2 ? y + 1 : 5 * yc + 2;
          }
          return true;

        case 2:
          if (xs == 0) {
            *xn = x - 1;
          }
          if (xs == 4) {
            *xn = x + 1;
          }
          if (ys == 0) {
            *yn = y - 1;
          }
          if (ys == 4) {
            *yn = y + 1;
          }
          if (zs == 0) {
            *zn = z - 1;
          }
          if (zs == 4) {
            *zn = z + 1;
          }
          return true;

        case 3:
          if (xs == 2) {
            coordinateToggling(ys, zs, y, z, yn, zn);
          }
          if (ys == 2) {
            coordinateToggling(xs, zs, x, z, xn, zn);
          }
          if (zs == 2) {
            coordinateToggling(xs, ys, x, y, xn, yn);
          }
          return true;

        default:
          return false;
      }

    case 1:
      if (idx >= 14) {
        return false;
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

void VoxelRhombic::scale(unsigned int amount, bool grid) {
  // the coordinates of the cells within a 5x5x5 block
  // clang-format off
  static constexpr int kVoxelCoords[24][3] = {
    { 2, 1, 0 }, { 1, 2, 0 }, { 3, 2, 0 }, { 2, 3, 0 },
    { 2, 0, 1 }, { 0, 2, 1 }, { 4, 2, 1 }, { 2, 4, 1 },
    { 1, 0, 2 }, { 3, 0, 2 }, { 0, 1, 2 }, { 4, 1, 2 }, { 0, 3, 2 }, { 4, 3, 2 }, { 1, 4, 2 }, { 3, 4, 2 },
    { 2, 0, 3 }, { 0, 2, 3 }, { 4, 2, 3 }, { 2, 4, 3 },
    { 2, 1, 4 }, { 1, 2, 4 }, { 3, 2, 4 }, { 2, 3, 4 }
  };

  // the six cutting planes
  static constexpr int kPlanes[6][3] = {
    {0, 1, 1}, {0, 1, -1},
    {1, 0, 1}, {1, 0, -1},
    {1, 1, 0}, {1, -1, 0},
  };
  // clang-format on

  const unsigned int nsx = ((sx_ + 4) / 5) * amount * 5;
  const unsigned int nsy = ((sy_ + 4) / 5) * amount * 5;
  const unsigned int nsz = ((sz_ + 4) / 5) * amount * 5;
  std::vector<VoxelType> s2(nsx * nsy * nsz, kEmpty);

  // each 5x5x5 block is scaled
  const unsigned int bsx = (sx_ + 4) / 5;
  const unsigned int bsy = (sy_ + 4) / 5;
  const unsigned int bsz = (sz_ + 4) / 5;

  bool voxelGrid = true;  // only changes when grid is true

  const double am = amount;
  // the signed distance of cell (ax, ay, az) of the scaled block from plane i
  auto planeDist = [&](unsigned int i, unsigned int ax, unsigned int ay, unsigned int az) {
    return (ax - 2.5 * am + 0.5) * kPlanes[i][0] + (ay - 2.5 * am + 0.5) * kPlanes[i][1] +
           (az - 2.5 * am + 0.5) * kPlanes[i][2];
  };

  for (const auto& c1 : kVoxelCoords) {
    const auto x = asUnsigned(c1[0]);
    const auto y = asUnsigned(c1[1]);
    const auto z = asUnsigned(c1[2]);

    // on which side of each plane this cell is
    int planeMask = 0;
    for (unsigned int i = 0; i < 6; i++) {
      if ((x - 2.5) * kPlanes[i][0] + (y - 2.5) * kPlanes[i][1] + (z - 2.5) * kPlanes[i][2] < 0) {
        planeMask |= 1 << i;
      }
    }

    // all cells of the target block on the same sides
    for (unsigned int amx = 0; amx < amount; amx++) {
      for (unsigned int amy = 0; amy < amount; amy++) {
        for (unsigned int amz = 0; amz < amount; amz++) {
          for (const auto& c2 : kVoxelCoords) {
            const unsigned int ax = 5 * amx + asUnsigned(c2[0]);
            const unsigned int ay = 5 * amy + asUnsigned(c2[1]);
            const unsigned int az = 5 * amz + asUnsigned(c2[2]);

            bool useVoxel = true;
            for (unsigned int i = 0; i < 6; i++) {
              if (((ax - 2.5 * am) * kPlanes[i][0] + (ay - 2.5 * am) * kPlanes[i][1] +
                       (az - 2.5 * am) * kPlanes[i][2] <
                   0) != ((planeMask & (1 << i)) != 0)) {
                useVoxel = false;
                break;
              }
            }

            if (!useVoxel) {
              continue;
            }

            if (grid) {
              int planeCloseness = 0;

              for (unsigned int i = 0; i < 6; i++) {
                if (std::fabs(planeDist(i, ax, ay, az)) < 5) {
                  planeCloseness |= 1 << i;
                }
              }

              if (ax < 5) {
                planeCloseness |= 1 << 6;
              }
              if (ax >= 5 * amount - 5) {
                planeCloseness |= 1 << 7;
              }
              if (ay < 5) {
                planeCloseness |= 1 << 8;
              }
              if (ay >= 5 * amount - 5) {
                planeCloseness |= 1 << 9;
              }
              if (az < 5) {
                planeCloseness |= 1 << 10;
              }
              if (az >= 5 * amount - 5) {
                planeCloseness |= 1 << 11;
              }

              voxelGrid = false;

              // the diagonals from the centre of the cube to the corners
              if ((planeCloseness & 0x03) && (planeCloseness & 0x0C) && (planeCloseness & 0x30)) {
                voxelGrid = true;
              }

              // from the centre of the cube to the centres of the faces;
              // a diagonal cut needs the second plane of the pair too
              if ((planeCloseness & 0x03) == 0x03 &&
                  std::fabs(planeDist(0, ax, ay, az)) + std::fabs(planeDist(1, ax, ay, az)) < 5) {
                voxelGrid = true;
              }
              if ((planeCloseness & 0x0C) == 0x0C &&
                  std::fabs(planeDist(2, ax, ay, az)) + std::fabs(planeDist(3, ax, ay, az)) < 5) {
                voxelGrid = true;
              }
              if ((planeCloseness & 0x30) == 0x30 &&
                  std::fabs(planeDist(4, ax, ay, az)) + std::fabs(planeDist(5, ax, ay, az)) < 5) {
                voxelGrid = true;
              }

              // from the middle of the faces to the corners of the cube
              if ((planeCloseness & 0x03) && (planeCloseness & 0xC0)) {
                voxelGrid = true;
              }
              if ((planeCloseness & 0x0C) && (planeCloseness & 0x300)) {
                voxelGrid = true;
              }
              if ((planeCloseness & 0x30) && (planeCloseness & 0xC00)) {
                voxelGrid = true;
              }

              // the connections between the cubes
              const int tmp = planeCloseness & 0xFC0;
              int count = 0;
              float diff = 0;

              for (int i = 6; i < 12; i++) {
                if (tmp & 1 << i) {
                  count++;
                  double add = 0;
                  switch (i) {
                    case 6: add = ax + 0.5; break;
                    case 7: add = amount * 5 - ax - 0.5; break;
                    case 8: add = ay + 0.5; break;
                    case 9: add = amount * 5 - ay - 0.5; break;
                    case 10: add = az + 0.5; break;
                    case 11: add = amount * 5 - az - 0.5; break;
                    default: break;
                  }
                  // as the original's `float += double`
                  diff = static_cast<float>(static_cast<double>(diff) + add);
                }
              }

              if (count == 2 && diff < 5) {
                voxelGrid = true;
              }
            }

            // every block
            for (unsigned int bx = 0; bx < bsx; bx++) {
              for (unsigned int by = 0; by < bsy; by++) {
                for (unsigned int bz = 0; bz < bsz; bz++) {
                  const int px = asInt(5 * bx + x);
                  const int py = asInt(5 * by + y);
                  const int pz = asInt(5 * bz + z);
                  if (!isEmpty2(px, py, pz)) {
                    VoxelType value = get(5 * bx + x, 5 * by + y, 5 * bz + z);
                    if (!voxelGrid) {
                      value = 0;
                    }
                    s2[(bx * amount * 5 + ax) +
                       nsx * ((by * amount * 5 + ay) + nsy * (bz * amount * 5 + az))] = value;
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

bool VoxelRhombic::scaleDown(unsigned char /*by*/, bool /*action*/) { return false; }

void VoxelRhombic::resizeInclude(int& px, int& py, int& pz) {
  int nsx = asInt(getX());
  int nsy = asInt(getY());
  int nsz = asInt(getZ());
  int tx = 0;
  int ty = 0;
  int tz = 0;

  while (px < 0) {
    nsx += 5;
    tx += 5;
    px += 5;
  }
  while (py < 0) {
    nsy += 5;
    ty += 5;
    py += 5;
  }
  while (pz < 0) {
    nsz += 5;
    tz += 5;
    pz += 5;
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

  if (nsx % 5) {
    nsx += 5 - nsx % 5;
  }
  if (nsy % 5) {
    nsy += 5 - nsy % 5;
  }
  if (nsz % 5) {
    nsz += 5 - nsz % 5;
  }

  resize(asUnsigned(nsx), asUnsigned(nsy), asUnsigned(nsz), 0);
  translate(tx, ty, tz, 0);
}

void VoxelRhombic::minimizePiece() {
  // the low corner must come back to the same position modulo 5
  const unsigned int x = bx1_ % 5;
  const unsigned int y = by1_ % 5;
  const unsigned int z = bz1_ % 5;

  Voxel::minimizePiece();

  if (x || y || z) {
    resize(sx_ + x, sy_ + y, sz_ + z, 0);
    translate(asInt(x), asInt(y), asInt(z), 0);
  }

  // and the size must be a multiple of 5
  if (sx_ % 5 || sy_ % 5 || sz_ % 5) {
    resize(sx_ + (5 - sx_ % 5) % 5, sy_ + (5 - sy_ % 5) % 5, sz_ + (5 - sz_ % 5) % 5, 0);
  }
}

bool VoxelRhombic::validCoordinate(int x, int y, int z) const {
  x %= 5;
  y %= 5;
  z %= 5;

  if (x > 2) {
    x = 4 - x;
  }
  if (y > 2) {
    y = 4 - y;
  }
  if (z > 2) {
    z = 4 - z;
  }

  if (x != 0 && y != 0 && z != 0) {
    return false;
  }
  if (x != 1 && y != 1 && z != 1) {
    return false;
  }
  if (x != 2 && y != 2 && z != 2) {
    return false;
  }
  return true;
}

bool VoxelRhombic::identicalInBB(const Voxel& op, bool includeColors) const {
  return bx1_ % 5 == op.boundX1() % 5 && by1_ % 5 == op.boundY1() % 5 &&
         bz1_ % 5 == op.boundZ1() % 5 && Voxel::identicalInBB(op, includeColors);
}

bool VoxelRhombic::onGrid(int x, int y, int z) const {
  // the shape does not fit when it is off the 5-raster
  return x % 5 == 0 && y % 5 == 0 && z % 5 == 0;
}

}  // namespace bt
