// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/movementcache_1.cpp.
#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/solve/movement_cache.h"

namespace bt {

namespace {

constexpr unsigned int kDirections = 4;

// One step in each direction.
constexpr int kDirs[kDirections][3] = {
    {0, 0, 1},
    {2, 0, 0},
    {1, 1, 0},
    {-1, 1, 0},
};

// For each direction and each of the two triangle kinds, the cells (up to
// three, ended by -10) that must be empty for the triangle to move one step
// that way (made by hand in the original).
constexpr int kChecks[kDirections][2][3][3] = {
    {
        // direction 0: 0 0 1
        {{0, 0, 1}, {-10, 0, 0}},
        {{0, 0, 1}, {-10, 0, 0}},
    },
    {
        // direction 1: 2 0 0
        {{1, 0, 0}, {2, 0, 0}, {-10, 0, 0}},
        {{1, 0, 0}, {2, 0, 0}, {-10, 0, 0}},
    },
    {
        // direction 2: 1 1 0
        {{1, 0, 0}, {1, 1, 0}, {-10, 0, 0}},
        {{0, 1, 0}, {1, 1, 0}, {-10, 0, 0}},
    },
    {
        // direction 3: -1 1 0
        {{-1, 0, 0}, {-1, 1, 0}, {-10, 0, 0}},
        {{0, 1, 0}, {-1, 1, 0}, {-10, 0, 0}},
    },
};

}  // namespace

MovementCachePrisms::MovementCachePrisms(const Puzzle& puzzle, const Problem& problem)
    : MovementCache(puzzle, problem) {}

std::vector<unsigned int> MovementCachePrisms::moCalcValues(const Voxel& sh1, const Voxel& sh2,
                                                            int dx, int dy, int dz) {
  // the offset is between the hotspots: make it one between the spaces
  dx += sh1.getHx() - sh2.getHx();
  dy += sh1.getHy() - sh2.getHy();
  dz += sh1.getHz() - sh2.getHz();

  std::vector<unsigned int> move(kDirections);

  for (unsigned int dir = 0; dir < kDirections; dir++) {
    unsigned int m = 32000;

    for (unsigned int x = sh1.boundX1(); x <= sh1.boundX2(); x++) {
      for (unsigned int y = sh1.boundY1(); y <= sh1.boundY2(); y++) {
        for (unsigned int z = sh1.boundZ1(); z <= sh1.boundZ2(); z++) {
          if (sh1.isEmpty(x, y, z)) {
            continue;
          }

          const unsigned int voxel = (x + y) % 2;

          int xp = asInt(x) - dx;
          int yp = asInt(y) - dy;
          int zp = asInt(z) - dz;

          BT_ASSERT(sh2.isEmpty2(xp, yp, zp));

          // only up to the smallest distance found so far
          for (unsigned int d = 0; d < m; d++) {
            // past the second shape's box (with room), nothing can be hit
            if (kDirs[dir][0] > 0 && xp > asInt(sh2.boundX2()) + 2) {
              break;
            }
            if (kDirs[dir][0] < 0 && xp < asInt(sh2.boundX1()) - 2) {
              break;
            }
            if (kDirs[dir][1] > 0 && yp > asInt(sh2.boundY2()) + 2) {
              break;
            }
            if (kDirs[dir][1] < 0 && yp < asInt(sh2.boundY1()) - 2) {
              break;
            }
            if (kDirs[dir][2] > 0 && zp > asInt(sh2.boundZ2()) + 2) {
              break;
            }
            if (kDirs[dir][2] < 0 && zp < asInt(sh2.boundZ1()) - 2) {
              break;
            }

            for (const int(*ch)[3] = kChecks[dir][voxel]; (*ch)[0] != -10; ch++) {
              if (sh2.isFilled2(xp + (*ch)[0], yp + (*ch)[1], zp + (*ch)[2])) {
                if (d < m) {
                  m = d;
                }
                break;
              }
            }

            xp += kDirs[dir][0];
            yp += kDirs[dir][1];
            zp += kDirs[dir][2];
          }

          // nothing can be smaller than 0: end all three loops
          if (m == 0) {
            x = sh1.boundX2();
            y = sh1.boundY2();
            z = sh1.boundZ2();
          }
        }
      }
    }

    move[dir] = m;
  }

  return move;
}

void MovementCachePrisms::getDirection(unsigned int dir, int* x, int* y, int* z) const {
  BT_ASSERT(dir < kDirections);
  *x = kDirs[dir][0];
  *y = kDirs[dir][1];
  *z = kDirs[dir][2];
}

}  // namespace bt
