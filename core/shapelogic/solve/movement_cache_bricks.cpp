// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/movementcache_0.cpp.
#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/solve/movement_cache.h"

#include <algorithm>

namespace bt {

MovementCacheBricks::MovementCacheBricks(const Puzzle& puzzle, const Problem& problem)
    : MovementCache(puzzle, problem) {}

// The smallest gap, along each axis, between a cube of the first piece and
// a cube of the second after it; 32000 when nothing is in the way.
std::vector<unsigned int> MovementCacheBricks::moCalcValues(const Voxel& sh1, const Voxel& sh2,
                                                            int dx, int dy, int dz) {
  // the offset is between the hotspots: make it one between the spaces
  dx += sh1.getHx() - sh2.getHx();
  dy += sh1.getHy() - sh2.getHy();
  dz += sh1.getHz() - sh2.getHz();

  std::vector<unsigned int> move(3);

  // the boxes where both pieces are, and where either is in the direction
  // looked along
  const int x1i = std::max(asInt(sh1.boundX1()), asInt(sh2.boundX1()) + dx);
  const int x2i = std::min(asInt(sh1.boundX2()), asInt(sh2.boundX2()) + dx);
  const int y1i = std::max(asInt(sh1.boundY1()), asInt(sh2.boundY1()) + dy);
  const int y2i = std::min(asInt(sh1.boundY2()), asInt(sh2.boundY2()) + dy);
  const int z1i = std::max(asInt(sh1.boundZ1()), asInt(sh2.boundZ1()) + dz);
  const int z2i = std::min(asInt(sh1.boundZ2()), asInt(sh2.boundZ2()) + dz);

  const int x1u = asInt(sh1.boundX1());
  const int x2u = std::max(asInt(sh1.boundX2()), asInt(sh2.boundX2()) + dx);
  const int y1u = asInt(sh1.boundY1());
  const int y2u = std::max(asInt(sh1.boundY2()), asInt(sh2.boundY2()) + dy);
  const int z1u = asInt(sh1.boundZ1());
  const int z2u = std::max(asInt(sh1.boundZ2()), asInt(sh2.boundZ2()) + dz);

  unsigned int mx = 32000;
  unsigned int my = 32000;
  unsigned int mz = 32000;

  // Along each line through both boxes: a cube of piece 1 starts a gap, a
  // cube of piece 2 ends it. The gap starts large so that a cube of piece 2
  // before any of piece 1 counts for nothing.
  for (int y = y1i; y <= y2i; y++) {
    for (int z = z1i; z <= z2i; z++) {
      unsigned int gap = 32000;
      for (int x = x1u; x <= x2u; x++) {
        if (sh1.isFilled2(x, y, z)) {
          gap = 0;
        } else if (sh2.isFilled2(x - dx, y - dy, z - dz)) {
          mx = std::min(mx, gap);
        } else {
          gap++;
        }
      }
    }
  }

  for (int x = x1i; x <= x2i; x++) {
    for (int z = z1i; z <= z2i; z++) {
      unsigned int gap = 32000;
      for (int y = y1u; y <= y2u; y++) {
        if (sh1.isFilled2(x, y, z)) {
          gap = 0;
        } else if (sh2.isFilled2(x - dx, y - dy, z - dz)) {
          my = std::min(my, gap);
        } else {
          gap++;
        }
      }
    }
  }

  for (int x = x1i; x <= x2i; x++) {
    for (int y = y1i; y <= y2i; y++) {
      unsigned int gap = 32000;
      for (int z = z1u; z <= z2u; z++) {
        if (sh1.isFilled2(x, y, z)) {
          gap = 0;
        } else if (sh2.isFilled2(x - dx, y - dy, z - dz)) {
          mz = std::min(mz, gap);
        } else {
          gap++;
        }
      }
    }
  }

  move[0] = mx;
  move[1] = my;
  move[2] = mz;

  return move;
}

void MovementCacheBricks::getDirection(unsigned int dir, int* x, int* y, int* z) const {
  switch (dir) {
    case 0:
      *x = 1;
      *y = *z = 0;
      break;
    case 1:
      *y = 1;
      *x = *z = 0;
      break;
    case 2:
      *z = 1;
      *x = *y = 0;
      break;
    default:
      BT_ASSERT_MSG(false, "a direction the cube grid does not have");
      break;
  }
}

}  // namespace bt
