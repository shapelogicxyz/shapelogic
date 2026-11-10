// SPDX-License-Identifier: GPL-3.0-or-later
// The walk of src/lib/movementcache_1.cpp (movement_cache_prisms.cpp) over
// derived directions and check cells, as the MovementCache of BurrTools V2
// does it (src/lib/movementcache.h). New.
#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/movement_checks.h"

namespace bt {

MovementCacheDerived::MovementCacheDerived(const Puzzle& puzzle, const Problem& problem,
                                           const MovementChecks& checks)
    : MovementCache(puzzle, problem), checks_(checks) {}

unsigned int MovementCacheDerived::numDirections() const {
  return static_cast<unsigned int>(checks_.directions.size());
}

void MovementCacheDerived::getDirection(unsigned int dir, int* x, int* y, int* z) const {
  BT_ASSERT(dir < checks_.directions.size());
  *x = checks_.directions[dir][0];
  *y = checks_.directions[dir][1];
  *z = checks_.directions[dir][2];
}

// Each cell of the first shape is walked step by step in each direction
// until a cell it would pass through is one of the second shape, or until
// it has left the second shape's box for good. The smallest number of
// free steps over the cells is the value; 32000 when nothing is in the way.
std::vector<unsigned int> MovementCacheDerived::moCalcValues(const Voxel& sh1, const Voxel& sh2,
                                                             int dx, int dy, int dz) {
  // the offset is between the hotspots: make it one between the spaces
  dx += sh1.getHx() - sh2.getHx();
  dy += sh1.getHy() - sh2.getHy();
  dz += sh1.getHz() - sh2.getHz();

  const std::size_t directions = checks_.directions.size();
  std::vector<unsigned int> move(directions);

  // the second shape's box, widened by how far a check reaches
  const int x1 = asInt(sh2.boundX1()) - checks_.extent[0];
  const int x2 = asInt(sh2.boundX2()) + checks_.extent[0];
  const int y1 = asInt(sh2.boundY1()) - checks_.extent[1];
  const int y2 = asInt(sh2.boundY2()) + checks_.extent[1];
  const int z1 = asInt(sh2.boundZ1()) - checks_.extent[2];
  const int z2 = asInt(sh2.boundZ2()) + checks_.extent[2];

  for (std::size_t dir = 0; dir < directions; dir++) {
    const std::array<int, 3>& step = checks_.directions[dir];
    unsigned int m = 32000;

    for (unsigned int x = sh1.boundX1(); x <= sh1.boundX2(); x++) {
      for (unsigned int y = sh1.boundY1(); y <= sh1.boundY2(); y++) {
        for (unsigned int z = sh1.boundZ1(); z <= sh1.boundZ2(); z++) {
          if (sh1.isEmpty(x, y, z)) {
            continue;
          }

          // The shapes lie a move of the grid apart, so the cell is of
          // the same kind in both spaces, and stays so step after step.
          const std::vector<std::array<int, 3>>& cells = checks_.at(dir, checks_.kindOf(x, y, z));

          int xp = asInt(x) - dx;
          int yp = asInt(y) - dy;
          int zp = asInt(z) - dz;

          BT_ASSERT(sh2.isEmpty2(xp, yp, zp));

          // only up to the smallest distance found so far
          for (unsigned int d = 0; d < m; d++) {
            // past the second shape's box (with room) and moving away,
            // nothing can be hit
            if ((step[0] > 0 && xp > x2) || (step[0] < 0 && xp < x1) ||
                (step[1] > 0 && yp > y2) || (step[1] < 0 && yp < y1) ||
                (step[2] > 0 && zp > z2) || (step[2] < 0 && zp < z1)) {
              break;
            }

            bool hit = false;
            for (const std::array<int, 3>& ch : cells) {
              if (sh2.isFilled2(xp + ch[0], yp + ch[1], zp + ch[2])) {
                hit = true;
                break;
              }
            }
            if (hit) {
              m = d;
              break;
            }

            xp += step[0];
            yp += step[1];
            zp += step[2];
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

}  // namespace bt
