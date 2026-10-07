// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_voxel_connect.cpp of the original.

#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "grid_test_helpers.h"

#include <catch2/catch_test_macros.hpp>



#include <memory>

using namespace bt;
using namespace bt::test;

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
     (src/lib/voxel_0.cpp:172-188) lists the 12 edge neighbours of a cell,
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

  /* a ring in a single layer: the centre is enclosed in x and y but open
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

  /* a fully enclosed 3x3x3 shell has one trapped cell at the centre */
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
