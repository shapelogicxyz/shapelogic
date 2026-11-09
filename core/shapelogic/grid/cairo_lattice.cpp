// SPDX-License-Identifier: GPL-3.0-or-later
// New (decision 0023), after grid_cairo.lua of BurrTools V2.
#include "shapelogic/grid/cairo_lattice.h"

#include "shapelogic/base/assert.h"

#include <algorithm>

namespace bt::cairo {

namespace {

// V2's four pentagons, one per kind, about the kind's base cell (its
// first corner is the 4-fold centre at (0, 0)); V2 lists them clockwise.
constexpr Point kShapes[4][5] = {
    // kind 0, base (0, -1)
    {{{0, 0}, {0, 0}}, {{1, 0}, {0, -1}}, {{1, 0}, {-2, 1}}, {{0, 0}, {-2, 0}}, {{0, -1}, {-1, 0}}},
    // kind 1, base (1, 0)
    {{{0, 0}, {0, 0}}, {{0, 1}, {1, 0}}, {{2, -1}, {1, 0}}, {{2, 0}, {0, 0}}, {{1, 0}, {0, -1}}},
    // kind 2, base (-1, 0)
    {{{0, 0}, {0, 0}}, {{0, -1}, {-1, 0}}, {{-2, 1}, {-1, 0}}, {{-2, 0}, {0, 0}}, {{-1, 0}, {0, 1}}},
    // kind 3, base (0, 1)
    {{{0, 0}, {0, 0}}, {{-1, 0}, {0, 1}}, {{-1, 0}, {2, -1}}, {{0, 0}, {2, 0}}, {{0, 1}, {1, 0}}},
};

}  // namespace

int kindOf(int x, int y) noexcept {
  if (!isCell(x, y)) {
    return -1;
  }
  for (int k = 0; k < 4; k++) {
    if (keepsKinds(x - kBase[k][0], y - kBase[k][1])) {
      return k;
    }
  }
  return -1;
}

std::array<Point, 5> pentagon(int x, int y) noexcept {
  const int k = kindOf(x, y);
  BT_ASSERT(k >= 0);
  const int dx = x - kBase[k][0];
  const int dy = y - kBase[k][1];
  std::array<Point, 5> p{};
  // counter clockwise: V2's order reversed, the shared corner first
  for (int i = 0; i < 5; i++) {
    const Point& s = kShapes[k][(5 - i) % 5];
    p[static_cast<std::size_t>(i)] = Point{{s.x.a + dx, s.x.o}, {s.y.a + dy, s.y.o}};
  }
  return p;
}

Fix fixToLattice(int u, int v) noexcept {
  Fix best{0, 0};
  int bestSum = 100;
  for (int a = 0; a < 4; a++) {
    for (int b = 0; b < 4; b++) {
      if (keepsKinds(u + a, v + b) && a + b < bestSum) {
        best = {a, b};
        bestSum = a + b;
      }
    }
  }
  BT_ASSERT(bestSum < 100);
  return best;
}

const Around& around(int kind) noexcept {
  static const std::array<Around, 4> all = [] {
    std::array<Around, 4> out{};
    for (int k = 0; k < 4; k++) {
      const int bx = kBase[k][0];
      const int by = kBase[k][1];
      const std::array<Point, 5> p = pentagon(bx, by);
      Around& a = out[static_cast<std::size_t>(k)];
      std::array<bool, 5> found{};
      for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
          if ((dx == 0 && dy == 0) || !isCell(bx + dx, by + dy)) {
            continue;
          }
          const std::array<Point, 5> q = pentagon(bx + dx, by + dy);
          // the sides shared (the other cell runs them the other way)
          bool side = false;
          for (std::size_t i = 0; i < 5; i++) {
            const Point& s0 = p[i];
            const Point& s1 = p[(i + 1) % 5];
            for (std::size_t j = 0; j < 5; j++) {
              if (q[j] == s1 && q[(j + 1) % 5] == s0) {
                a.sides[i] = {dx, dy};
                found[i] = true;
                side = true;
              }
            }
          }
          if (side) {
            continue;
          }
          const bool corner = std::ranges::any_of(p, [&](const Point& c) {
            return std::ranges::find(q, c) != q.end();
          });
          if (corner) {
            a.corners.push_back({dx, dy});
          }
        }
      }
      BT_ASSERT(std::ranges::all_of(found, [](bool f) { return f; }));
      BT_ASSERT(!a.corners.empty());
    }
    return out;
  }();
  return all[static_cast<std::size_t>(kind)];
}

}  // namespace bt::cairo
