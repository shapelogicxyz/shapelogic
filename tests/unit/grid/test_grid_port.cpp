// SPDX-License-Identifier: GPL-3.0-or-later
// New tests for the port: the factory, the bit set, the caches, and the
// places where the port deliberately differs from the original (see
// docs/handoff/stage-1.md).

#include "shapelogic/grid/bitfield.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/grid/voxel_bricks.h"
#include "shapelogic/grid/voxel_prisms.h"
#include "shapelogic/grid/voxel_rhombic.h"
#include "shapelogic/grid/voxel_spheres.h"
#include "shapelogic/grid/voxel_table.h"
#include "shapelogic/grid/voxel_tetra_octa.h"
#include "grid_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace bt;
using namespace bt::test;

TEST_CASE("grid: makeVoxel builds the grid's own class and clone keeps it", "[grid]") {
  auto check = [](GridKind k, auto* tag) {
    using T = std::remove_pointer_t<decltype(tag)>;
    const GridType gt(k);
    auto v = gt.makeVoxel(2, 2, 2);
    REQUIRE(dynamic_cast<T*>(v.get()) != nullptr);
    auto c = v->clone();
    REQUIRE(dynamic_cast<T*>(c.get()) != nullptr);
    REQUIRE(c->getGridType() == gt);
  };
  check(GridKind::Bricks, static_cast<VoxelBricks*>(nullptr));
  check(GridKind::TriangularPrism, static_cast<VoxelPrisms*>(nullptr));
  check(GridKind::Spheres, static_cast<VoxelSpheres*>(nullptr));
  check(GridKind::Rhombic, static_cast<VoxelRhombic*>(nullptr));
  check(GridKind::TetraOcta, static_cast<VoxelTetraOcta*>(nullptr));
  // sliding and stacking puzzles are built from cubes
  check(GridKind::Sliding, static_cast<VoxelBricks*>(nullptr));
  check(GridKind::Stacking, static_cast<VoxelBricks*>(nullptr));
}

TEST_CASE("grid: capabilities and transformation counts match the original", "[grid]") {
  CHECK(GridType(GridKind::Bricks).getCapabilities() ==
        (kCapAssemble | kCapDisassemble | kCapStlExport));
  CHECK(GridType(GridKind::TriangularPrism).getCapabilities() ==
        (kCapAssemble | kCapDisassemble | kCapStlExport));
  CHECK(GridType(GridKind::Spheres).getCapabilities() == (kCapAssemble | kCapStlExport));
  CHECK(GridType(GridKind::Rhombic).getCapabilities() == (kCapAssemble | kCapStlExport));
  CHECK(GridType(GridKind::TetraOcta).getCapabilities() == (kCapAssemble | kCapStlExport));
  CHECK(GridType(GridKind::Sliding).getCapabilities() == (kCapAssemble | kCapStlExport));
  CHECK(GridType(GridKind::Stacking).getCapabilities() == kCapStlExport);

  CHECK(GridType(GridKind::Bricks).getSymmetries().getNumTransformationsMirror() == 48);
  CHECK(GridType(GridKind::TriangularPrism).getSymmetries().getNumTransformationsMirror() == 24);
  CHECK(GridType(GridKind::Spheres).getSymmetries().getNumTransformationsMirror() == 240);
  // the cube-based grids share the cube tables
  CHECK(&GridType(GridKind::Rhombic).getSymmetries() == &GridType(GridKind::Bricks).getSymmetries());
}

TEST_CASE("grid: Bitfield reads hexadecimal like the original", "[grid]") {
  constexpr Bitfield<240> b("8000000000000000F1");
  STATIC_REQUIRE(b.get(0));
  STATIC_REQUIRE(!b.get(1));
  STATIC_REQUIRE(b.get(4) && b.get(5) && b.get(6) && b.get(7));
  STATIC_REQUIRE(b.get(71));
  STATIC_REQUIRE(b.countBits() == 6);
  constexpr Bitfield<240> c("10");
  STATIC_REQUIRE((b & c).countBits() == 1);
  STATIC_REQUIRE((b | c) == b);
  STATIC_REQUIRE(Bitfield<240>().notNull() == false);
}

TEST_CASE("grid: a transformation the sphere grid cannot do has no hotspot either", "[grid]") {
  // The original's getHotspot reported success for such a transformation
  // unless getBoundingBox had been asked first. The port answers "not
  // possible" for both, whichever is asked first.
  const GridType gt(GridKind::Spheres);
  auto v = legalShape(gt, kLegalShapeBox, 4);
  REQUIRE(v);
  v->initHotspot();

  const unsigned int n = gt.getSymmetries().getNumTransformationsMirror();
  unsigned int impossible = 0;
  for (unsigned int t = 0; t < n; t++) {
    auto c = v->clone();
    if (c->transform(t)) {
      continue;
    }
    impossible++;
    auto fresh = v->clone();  // empty caches
    int x = 0;
    int y = 0;
    int z = 0;
    CHECK_FALSE(fresh->getHotspot(static_cast<unsigned char>(t), &x, &y, &z));
    CHECK_FALSE(fresh->getBoundingBox(static_cast<unsigned char>(t), &x, &y, &z));
  }
  REQUIRE(impossible > 0);
}

TEST_CASE("grid: hotspot and bounding box queries follow a cube transform", "[grid]") {
  // The original's cube transform kept the old orientation's cached values.
  const GridType gt(GridKind::Bricks);
  auto v = fromLayers(gt, {{"##.", "#.."}});
  v->setHotspot(1, 0, 0);

  int x1 = 0;
  int y1 = 0;
  int z1 = 0;
  int x2 = 0;
  int y2 = 0;
  int z2 = 0;
  REQUIRE(v->getBoundingBox(1, &x1, &y1, &z1, &x2, &y2, &z2));  // fills the cache

  v->transform(4);
  for (unsigned char t = 0; t < 48; t++) {
    auto expected = v->clone();
    REQUIRE(expected->transform(t));
    REQUIRE(v->getBoundingBox(t, &x1, &y1, &z1, &x2, &y2, &z2));
    CHECK(x1 == static_cast<int>(expected->boundX1()));
    CHECK(x2 == static_cast<int>(expected->boundX2()));
    CHECK(y1 == static_cast<int>(expected->boundY1()));
    CHECK(y2 == static_cast<int>(expected->boundY2()));
    CHECK(z1 == static_cast<int>(expected->boundZ1()));
    CHECK(z2 == static_cast<int>(expected->boundZ2()));
    int hx = 0;
    int hy = 0;
    int hz = 0;
    REQUIRE(v->getHotspot(t, &hx, &hy, &hz));
    CHECK(hx == expected->getHx());
    CHECK(hy == expected->getHy());
    CHECK(hz == expected->getHz());
  }
}

TEST_CASE("grid: warmCaches leaves the answers unchanged", "[grid]") {
  for (GridKind k : kGeometryGrids) {
    INFO(toString(k));
    const GridType gt(k);
    auto cold = legalShape(gt, kLegalShapeBox, 5);
    REQUIRE(cold);
    cold->initHotspot();
    auto warm = cold->clone();
    warm->warmCaches();

    CHECK(warm->selfSymmetries() == cold->selfSymmetries());
    for (unsigned int t = 0; t < gt.getSymmetries().getNumTransformationsMirror(); t++) {
      const auto tc = static_cast<unsigned char>(t);
      int a[6] = {};
      int b[6] = {};
      const bool okA = cold->getBoundingBox(tc, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
      const bool okB = warm->getBoundingBox(tc, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
      REQUIRE(okA == okB);
      if (okA) {
        CHECK(std::vector<int>(a, a + 6) == std::vector<int>(b, b + 6));
      }
    }
  }
}

TEST_CASE("grid: the voxel table finds rotated and mirrored shapes", "[grid]") {
  const GridType gt(GridKind::Bricks);
  std::vector<std::unique_ptr<Voxel>> shapes;
  shapes.push_back(fromLayers(gt, {{"##", "#."}, {"#.", ".."}}));  // a tripod
  shapes.push_back(fromLayers(gt, {{"###"}}));
  VoxelTable table([&shapes](unsigned int i) -> const Voxel& { return *shapes[i]; });
  table.addSpace(0, VoxelTable::kParMirror);
  table.addSpace(1, VoxelTable::kParMirror);

  auto rotated = shapes[0]->clone();
  rotated->transform(7);
  unsigned int index = 99;
  unsigned char trans = 99;
  REQUIRE(table.getSpace(*rotated, &index, &trans));
  CHECK(index == 0);

  auto mirrored = shapes[0]->clone();
  mirrored->transform(24);
  CHECK(table.getSpace(*mirrored, &index, &trans, VoxelTable::kParMirror));
  CHECK(index == 0);

  auto other = fromLayers(gt, {{"####"}});
  CHECK_FALSE(table.getSpace(*other));
}
