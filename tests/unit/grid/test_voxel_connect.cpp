// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_voxel_connect.cpp of the original.

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "grid_test_helpers.h"

#include <catch2/catch_test_macros.hpp>



#include <cstddef>
#include <memory>
#include <random>
#include <vector>

using namespace sl;
using namespace sl::test;

TEST_CASE("voxel: a solid block is face connected", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  std::unique_ptr<Voxel> v = fromLayers(gt, {
    { "##",
      "##" },
  });

  /* type 0 is face connectivity; check the filled voxels hang together */
  REQUIRE(v->connected(0, true, Voxel::kEmpty));
}

TEST_CASE("voxel: two separated voxels are not connected", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  /* a gap between the two filled cells */
  std::unique_ptr<Voxel> v = fromLayers(gt, {
    { "#.#" },
  });

  REQUIRE_FALSE(v->connected(0, true, Voxel::kEmpty));
}

TEST_CASE("voxel: diagonal voxels are not face connected but are edge connected", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  /* corner-to-corner touch only: (0,0,0) and (1,1,0) share only the edge
     between them, not a face */
  std::unique_ptr<Voxel> v = fromLayers(gt, {
    { "#.",
      ".#" },
  });

  /* type 0 is face connectivity: no shared face, so not connected */
  REQUIRE_FALSE(v->connected(0, true, Voxel::kEmpty));

  /* type 1 is edge connectivity. voxel_0_c::getNeighbor's type-1 branch
     (src/lib/voxel_0.cpp:172-188) lists the 12 edge neighbors of a cell,
     including idx 0: (x-1, y-1, z) -- exactly the relationship between
     (1,1,0) and (0,0,0) in this fixture, so under edge connectivity the
     SAME shape IS connected. */
  REQUIRE(v->connected(1, true, Voxel::kEmpty));
}

TEST_CASE("voxel: an L shape is connected", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  std::unique_ptr<Voxel> v = fromLayers(gt, {
    { "#..",
      "#..",
      "###" },
  });

  REQUIRE(v->connected(0, true, Voxel::kEmpty));
}

TEST_CASE("voxel: connectivity spans layers", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  /* one voxel per layer, stacked in z */
  std::unique_ptr<Voxel> stacked = fromLayers(gt, {
    { "#" },
    { "#" },
  });
  REQUIRE(stacked->connected(0, true, Voxel::kEmpty));

  /* one voxel per layer, offset so they only touch at an edge */
  std::unique_ptr<Voxel> offset = fromLayers(gt, {
    { "#." },
    { ".#" },
  });
  REQUIRE_FALSE(offset->connected(0, true, Voxel::kEmpty));
}

TEST_CASE("voxel: fillHoles closes an enclosed cavity", "[voxel][connect]") {
  GridType gt(GridKind::Bricks);

  /* a ring in a single layer: the center is enclosed in x and y but open
     in z, so a 3D hole fill must not close it */
  std::unique_ptr<Voxel> ring = fromLayers(gt, {
    { "###",
      "#.#",
      "###" },
  });

  const unsigned int before = ring->countState(Voxel::kFilled);
  ring->fillHoles(0);

  /* open in z, so nothing is enclosed and nothing is filled */
  REQUIRE(ring->countState(Voxel::kFilled) == before);

  /* a fully enclosed 3x3x3 shell has one trapped cell at the center */
  std::unique_ptr<Voxel> shell = fromLayers(gt, {
    { "###",
      "###",
      "###" },
    { "###",
      "#.#",
      "###" },
    { "###",
      "###",
      "###" },
  });

  REQUIRE(shell->isEmpty(1, 1, 1));
  REQUIRE(shell->countState(Voxel::kFilled) == 26);

  shell->fillHoles(0);

  REQUIRE(shell->countState(Voxel::kFilled) == 27);
  REQUIRE_FALSE(shell->isEmpty(1, 1, 1));
}

TEST_CASE("voxel: counting states adds up to the whole space", "[voxel][connect]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);
    std::unique_ptr<Voxel> v = makeVoxel(gt, 2, 2, 2);

    v->setState(0, 0, 0, Voxel::kFilled);
    v->setState(1, 0, 0, Voxel::kFilled);
    v->setState(0, 1, 0, Voxel::kVariable);

    const unsigned int total = v->countState(Voxel::kEmpty)
                             + v->countState(Voxel::kFilled)
                             + v->countState(Voxel::kVariable);

    REQUIRE(total == v->getXYZ());
    REQUIRE(v->countState(Voxel::kFilled) == 2);
    REQUIRE(v->countState(Voxel::kVariable) == 1);
  }
}

namespace {

// What Voxel::connected and fillHoles are defined by, without a union-find:
// the cells selected (`inverse` ? not `value` : `value`) and the outside as
// nodes, an edge from a valid selected cell to each selected neighbor
// before it in x, y or z and to the outside where a neighbor is off the
// space (in z only with outsideZ), and a flood over them. group[i] is the
// node's component (cell i, or the space's size for the outside), -1 for a
// node that no edge reaches and that is not a valid selected cell.
std::vector<int> floodGroups(const Voxel& v, unsigned int type, bool inverse, VoxelType value,
                             bool outsideZ) {
  const auto n = static_cast<std::size_t>(v.getXYZ());
  const bool mergeOutside = (inverse && value != 0) || (!inverse && value == 0);
  auto selected = [&](VoxelType c) { return inverse ? c != value : c == value; };
  const int sx = static_cast<int>(v.getX());
  const int sy = static_cast<int>(v.getY());
  const int sz = static_cast<int>(v.getZ());
  std::vector<std::vector<std::size_t>> edges(n + 1);
  std::vector<bool> node(n + 1, false);
  for (int x = 0; x < sx; x++) {
    for (int y = 0; y < sy; y++) {
      for (int z = 0; z < sz; z++) {
        const auto ux = static_cast<unsigned int>(x);
        const auto uy = static_cast<unsigned int>(y);
        const auto uz = static_cast<unsigned int>(z);
        if (!v.validCoordinate(x, y, z) || !selected(v.get(ux, uy, uz))) {
          continue;
        }
        const std::size_t me = v.getIndex(ux, uy, uz);
        node[me] = true;
        for (unsigned int t = 0; t <= type; t++) {
          int x2 = 0;
          int y2 = 0;
          int z2 = 0;
          for (unsigned int idx = 0; v.getNeighbor(idx, t, x, y, z, &x2, &y2, &z2); idx++) {
            std::size_t other = n;
            if (x2 >= 0 && x2 < sx && y2 >= 0 && y2 < sy && z2 >= 0 && z2 < sz) {
              const auto c = v.get(static_cast<unsigned int>(x2), static_cast<unsigned int>(y2),
                                   static_cast<unsigned int>(z2));
              if (!(x2 < x || y2 < y || z2 < z) || !selected(c)) {
                continue;
              }
              other = v.getIndex(static_cast<unsigned int>(x2), static_cast<unsigned int>(y2),
                                 static_cast<unsigned int>(z2));
            } else if (!mergeOutside || !(outsideZ || (z2 >= 0 && z2 < sz))) {
              continue;
            }
            edges[me].push_back(other);
            edges[other].push_back(me);
          }
        }
      }
    }
  }
  std::vector<int> group(n + 1, -1);
  for (std::size_t start = 0; start <= n; start++) {
    if (group[start] != -1 || (!node[start] && edges[start].empty())) {
      continue;
    }
    std::vector<std::size_t> todo{start};
    group[start] = static_cast<int>(start);
    while (!todo.empty()) {
      const std::size_t at = todo.back();
      todo.pop_back();
      for (const std::size_t next : edges[at]) {
        if (group[next] == -1) {
          group[next] = static_cast<int>(start);
          todo.push_back(next);
        }
      }
    }
  }
  return group;
}

bool floodConnected(const Voxel& v, unsigned int type, bool inverse, VoxelType value,
                    bool outsideZ) {
  const std::vector<int> group = floodGroups(v, type, inverse, value, outsideZ);
  const bool mergeOutside = (inverse && value != 0) || (!inverse && value == 0);
  int first = -1;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    unsigned int x = 0;
    unsigned int y = 0;
    unsigned int z = 0;
    REQUIRE(v.indexToXYZ(i, &x, &y, &z));
    if (!v.validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) ||
        (inverse ? v.get(i) == value : v.get(i) != value)) {
      continue;
    }
    if (first == -1) {
      first = group[i];
    } else if (group[i] != first) {
      return false;
    }
  }
  // the outside is a group of its own when no cell touches it
  return first == -1 || !mergeOutside || group[v.getXYZ()] == first;
}

}  // namespace

// Cleanup 17.3: the union-find behind connected() and fillHoles() halves
// its paths. The groups must be the ones the definition gives.
TEST_CASE("voxel: connected and fillHoles give a flood's groups on every grid",
          "[voxel][connect]") {
  std::mt19937 rng(20261125);
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);
    for (int round = 0; round < 120; round++) {
      const unsigned int sx = 1 + rng() % 5;
      const unsigned int sy = 1 + rng() % 5;
      const unsigned int sz = 1 + rng() % 4;
      std::unique_ptr<Voxel> v = makeVoxel(gt, sx, sy, sz);
      // from sparse to nearly full, some variable and some colored
      const unsigned int density = 2 + rng() % 7;
      {
        const Voxel::BatchEdit batch(*v);
        for (unsigned int i = 0; i < v->getXYZ(); i++) {
          unsigned int x = 0;
          unsigned int y = 0;
          unsigned int z = 0;
          REQUIRE(v->indexToXYZ(i, &x, &y, &z));
          if (!v->validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) ||
              rng() % 10 >= density) {
            continue;
          }
          v->setState(i, rng() % 6 == 0 ? Voxel::kVariable : Voxel::kFilled);
          if (rng() % 8 == 0) {
            v->setColor(i, 1 + rng() % 3);
          }
        }
      }
      INFO("round " << round << ", " << sx << " x " << sy << " x " << sz);
      for (unsigned int type = 0; type <= 2; type++) {
        INFO("type " << type);
        const char ctype = static_cast<char>(type);
        // the status window's five questions, and the two a file's check asks
        CHECK(v->connected(ctype, true, 0) == floodConnected(*v, type, true, 0, true));
        CHECK(v->connected(ctype, false, 0) == floodConnected(*v, type, false, 0, true));
        CHECK(v->connected(ctype, false, 0, false) == floodConnected(*v, type, false, 0, false));
        CHECK(v->connected(ctype, true, Voxel::kFilled) ==
              floodConnected(*v, type, true, Voxel::kFilled, true));

        const std::vector<int> group = floodGroups(*v, type, true, Voxel::kFilled, true);
        std::unique_ptr<Voxel> filled = v->clone();
        filled->fillHoles(ctype);
        for (unsigned int i = 0; i < v->getXYZ(); i++) {
          unsigned int x = 0;
          unsigned int y = 0;
          unsigned int z = 0;
          REQUIRE(v->indexToXYZ(i, &x, &y, &z));
          const bool hole =
              v->validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) &&
              v->get(i) != Voxel::kFilled && group[i] != group[v->getXYZ()];
          INFO("cell " << i);
          CHECK(filled->get(i) == (hole ? VoxelType{Voxel::kFilled} : v->get(i)));
        }
      }
    }
  }
}

// Cleanup 17.2 and 17.3: Fill holes worked the bounding box out again for
// every cell it filled (a pass over the space each), and the union-find's
// trees were chains: a hollow box of 30 cells a side took a second in the
// release build, and a block of 40 a side five to say what it is connected
// to. These sizes are here to be noticed when either comes back: the case
// runs in the quick loop.
TEST_CASE("voxel: Fill holes and the connection checks on shapes of a few hundred thousand cells",
          "[voxel][connect]") {
  GridType gt(GridKind::Bricks);
  const unsigned int n = 40;
  std::unique_ptr<Voxel> box = gt.makeVoxel(n, n, n, Voxel::kFilled);
  {
    const Voxel::BatchEdit batch(*box);
    for (unsigned int x = 1; x + 1 < n; x++) {
      for (unsigned int y = 1; y + 1 < n; y++) {
        for (unsigned int z = 1; z + 1 < n; z++) {
          box->setState(x, y, z, Voxel::kEmpty);
        }
      }
    }
  }
  REQUIRE(box->countState(Voxel::kFilled) == (n * n * n) - ((n - 2) * (n - 2) * (n - 2)));
  CHECK(box->connected(0, true, 0));
  CHECK_FALSE(box->connected(0, false, 0));  // the inside is cut off from the outside
  box->fillHoles(0);
  CHECK(box->countState(Voxel::kFilled) == n * n * n);
  CHECK(box->boundX2() == n - 1);

  const unsigned int m = 60;
  const std::unique_ptr<Voxel> block = gt.makeVoxel(m, m, m, Voxel::kFilled);
  CHECK(block->connected(0, true, 0));
  CHECK(block->connected(1, true, 0));
  CHECK(block->connected(2, true, 0));
  CHECK(block->connected(0, false, 0, false));
  CHECK(block->connected(0, false, 0));
}
