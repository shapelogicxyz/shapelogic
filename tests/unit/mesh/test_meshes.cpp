// SPDX-License-Identifier: GPL-3.0-or-later
// The meshes. The first case is ported from the original's
// test/test_halfedge.cpp; the others are new: closed meshes and the cell tags on every grid, and the
// plain triangle lists. tests/unit/app/test_app_dump.cpp compares the
// meshes of every fixture shape with the original's.
#include "../grid/grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/types.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/draw_mesh.h"
#include "shapelogic/mesh/grid_geometry.h"
#include "shapelogic/mesh/polyhedron.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

using namespace bt;
using namespace bt::mesh;

namespace {

// Every half-edge has a twin that points back.
void requireClosed(const Polyhedron& p) {
  for (int i = 0; i < p.numHalfEdges(); i++) {
    const int h = p.halfEdgeAt(i);
    REQUIRE(p.he(h).twin != kNone);
    REQUIRE(p.he(h).twin != h);
    REQUIRE(p.he(p.he(h).twin).twin == h);
  }
}

int euler(const Polyhedron& p) { return p.numVertices() - p.numHalfEdges() / 2 + p.numFaces(); }

// A space of the grid with one cell (the first valid one) filled.
std::unique_ptr<Voxel> oneCell(GridKind kind, unsigned int& index) {
  const GridType gt(kind);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 6, 6, 6);
  const auto coords = test::validCoordinates(*v, 6);
  REQUIRE_FALSE(coords.empty());
  const auto [x, y, z] = coords.front();
  v->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
              static_cast<unsigned int>(z), Voxel::kFilled);
  index = v->getIndex(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                      static_cast<unsigned int>(z));
  return v;
}

}  // namespace

// A crash in the original's 3D view: two cubes that touch only along an
// edge put four half-edges on that edge, and finalize() could leave one
// without a twin.
TEST_CASE("polyhedron: finalize gives every halfedge of a voxel mesh a twin", "[mesh]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 6, 6, 6);
  v->setState(2, 2, 0, Voxel::kFilled);
  v->setState(3, 3, 0, Voxel::kFilled);

  const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
  const Polyhedron wire = shapeMesh(*v, MeshStyle::Wireframe);
  const Polyhedron stl = shapeMesh(*v, MeshStyle::Stl);
  for (const Polyhedron* m : {&flat, &wire, &stl}) {
    requireClosed(*m);
  }
  // the flat mesh is closed: closeSurface() had nothing to add
  CHECK(flat.numHoles() == 0);
}

TEST_CASE("meshes: one cell of every grid is a closed solid with its faces tagged",
          "[mesh]") {
  for (const GridKind kind : test::kGeometryGrids) {
    INFO(toString(kind));
    unsigned int index = 0;
    const std::unique_ptr<Voxel> v = oneCell(kind, index);

    const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
    requireClosed(flat);
    CHECK(flat.numHoles() == 0);
    // a sphere: genus 0
    CHECK(euler(flat) == 2);
    std::set<int> sides;
    for (int i = 0; i < flat.numFaces(); i++) {
      const Face& f = flat.face(flat.faceAt(i));
      CHECK(f.voxel == index);
      CHECK(f.voxelFace >= 0);
      sides.insert(f.voxelFace);
    }
    // one face per neighbor
    CHECK(static_cast<int>(sides.size()) == flat.numFaces());

    if (kind != GridKind::Spheres) {
      const Polyhedron wire = shapeMesh(*v, MeshStyle::Wireframe);
      requireClosed(wire);
      CHECK(wire.numHoles() == 0);
      CHECK(euler(wire) == 2);
    }

    // the drawing mesh: every face tagged with the cell; the bevel faces
    // with side -1
    const Polyhedron draw = shapeMesh(*v, MeshStyle::Drawing);
    CHECK(draw.numFaces() > flat.numFaces());
    for (int i = 0; i < draw.numFaces(); i++) {
      const Face& f = draw.face(draw.faceAt(i));
      CHECK(f.voxel == index);
    }
  }
}

TEST_CASE("meshes: the faces of a cube and of a prism", "[mesh]") {
  unsigned int index = 0;
  {
    const std::unique_ptr<Voxel> v = oneCell(GridKind::Bricks, index);
    const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
    CHECK(flat.numFaces() == 6);
    CHECK(flat.numVertices() == 8);
    // 6 faces, 12 edges, 8 corners: a bevelled cube has a face per edge
    // and per corner too
    const Polyhedron draw = shapeMesh(*v, MeshStyle::Drawing);
    CHECK(draw.numFaces() == 6 + 12 + 8);
  }
  {
    const std::unique_ptr<Voxel> v = oneCell(GridKind::TriangularPrism, index);
    const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
    CHECK(flat.numFaces() == 5);
    CHECK(flat.numVertices() == 6);
  }
}

TEST_CASE("meshes: a ring has a hole through it, two cubes apart are two solids", "[mesh]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> ring = test::makeVoxel(gt, 3, 3, 1);
  for (unsigned int x = 0; x < 3; x++) {
    for (unsigned int y = 0; y < 3; y++) {
      if (x != 1 || y != 1) {
        ring->setState(x, y, 0, Voxel::kFilled);
      }
    }
  }
  CHECK(euler(shapeMesh(*ring, MeshStyle::Flat)) == 0);

  std::unique_ptr<Voxel> two = test::makeVoxel(gt, 3, 1, 1);
  two->setState(0, 0, 0, Voxel::kFilled);
  two->setState(2, 0, 0, Voxel::kFilled);
  CHECK(euler(shapeMesh(*two, MeshStyle::Flat)) == 4);
}

TEST_CASE("meshes: the plain triangle list", "[mesh]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 2, 1, 1);
  v->setState(0, 0, 0, Voxel::kFilled);
  v->setState(1, 0, 0, Voxel::kVariable);
  v->setColor(1, 0, 0, 3);

  const DrawMesh flat = shapeDrawMesh(*v, MeshStyle::Flat);
  // 10 quads (two cells joined): two triangles each
  CHECK(flat.triangleCount() == 20);
  CHECK(flat.voxel.size() == flat.triangleCount());
  CHECK(flat.flags.size() == flat.triangleCount());
  CHECK(flat.polygon.back() == 9);
  for (const std::uint32_t i : flat.indices) {
    CHECK(i < flat.vertexCount());
  }
  for (std::size_t i = 0; i < flat.normals.size(); i += 3) {
    const double len =
        std::hypot(static_cast<double>(flat.normals[i]), static_cast<double>(flat.normals[i + 1]),
                   static_cast<double>(flat.normals[i + 2]));
    CHECK(std::fabs(len - 1) < 1e-5);
  }
  // the box's 12 edges, the 4 long ones in two pieces (one per cell); the
  // faces of the two cells side by side are coplanar and get no line
  CHECK(flat.edgeLines.size() == 16 * 6);
  CHECK(flat.lo[0] == 0.0F);
  CHECK(flat.hi[0] == 2.0F);

  // the variable cell's faces are marked, and carry its color
  bool marked = false;
  for (std::size_t t = 0; t < flat.triangleCount(); t++) {
    if (flat.voxel[t] == v->getIndex(1, 0, 0)) {
      CHECK((flat.flags[t] & kFaceVariableMark) != 0);
      CHECK(flat.color[t] == 3);
      marked = true;
    } else {
      CHECK((flat.flags[t] & kFaceVariableMark) == 0);
    }
  }
  CHECK(marked);
}

TEST_CASE("meshes: sizes and coordinates in mesh units", "[mesh]") {
  float x = 0;
  float y = 0;
  float z = 0;
  const GridType bricks(GridKind::Bricks);
  calculateSize(*test::makeVoxel(bricks, 2, 3, 4), &x, &y, &z);
  CHECK(x == 2.0F);
  CHECK(y == 3.0F);
  CHECK(z == 4.0F);
  const GridType rhombic(GridKind::Rhombic);
  calculateSize(*test::makeVoxel(rhombic, 10, 5, 5), &x, &y, &z);
  CHECK(x == 4.0F);
  CHECK(y == 2.0F);
  x = 5;
  y = 5;
  z = 10;
  recalcSpaceCoordinates(*test::makeVoxel(rhombic, 10, 5, 5), &x, &y, &z);
  CHECK(x == 2.0F);
  CHECK(z == 4.0F);
}

// engine.info sends spaceRule for the front end, which places and frames
// meshes by it: on every grid, it gives what the two functions give
TEST_CASE("meshes: spaceRule is calculateSize and recalcSpaceCoordinates", "[mesh]") {
  const auto near = [](double a, float b) {
    return std::abs(a - static_cast<double>(b)) <= 1e-5 * std::max(1.0, std::abs(a));
  };
  for (const GridKind kind : test::kAllGrids) {
    const SpaceRule r = spaceRule(kind);
    const GridType gt(kind);
    CAPTURE(static_cast<int>(kind));
    for (unsigned int sx = 1; sx <= 13; sx += 3) {
      for (unsigned int sy = 1; sy <= 13; sy += 2) {
        for (unsigned int sz = 1; sz <= 7; sz += 3) {
          const auto voxel = test::makeVoxel(gt, sx, sy, sz);
          float x = 0;
          float y = 0;
          float z = 0;
          calculateSize(*voxel, &x, &y, &z);
          const std::array<double, 3> u = {static_cast<double>(wholeUnits(sx, r.extentUnit)),
                                           static_cast<double>(wholeUnits(sy, r.extentUnit)),
                                           static_cast<double>(wholeUnits(sz, r.extentUnit))};
          const std::array<float, 3> got = {x, y, z};
          for (std::size_t i = 0; i < 3; i++) {
            const auto& row = r.extentMatrix[i];
            CAPTURE(sx, sy, sz, i);
            CHECK(near(r.extentOffset[i] + row[0] * u[0] + row[1] * u[1] + row[2] * u[2],
                       got[i]));
          }
        }
      }
    }
    const auto voxel = test::makeVoxel(gt, 4, 4, 4);
    for (int cx = -5; cx <= 5; cx += 2) {
      for (int cy = -4; cy <= 6; cy += 3) {
        for (int cz = -3; cz <= 3; cz += 3) {
          auto x = static_cast<float>(cx);
          auto y = static_cast<float>(cy);
          auto z = static_cast<float>(cz);
          recalcSpaceCoordinates(*voxel, &x, &y, &z);
          const std::array<float, 3> got = {x, y, z};
          for (std::size_t i = 0; i < 3; i++) {
            const auto& row = r.cellToMesh[i];
            CAPTURE(cx, cy, cz, i);
            CHECK(near(row[0] * cx + row[1] * cy + row[2] * cz, got[i]));
          }
        }
      }
    }
  }
}
