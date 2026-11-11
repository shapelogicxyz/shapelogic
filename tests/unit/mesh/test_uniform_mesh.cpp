// SPDX-License-Identifier: GPL-3.0-or-later
// The uniform mesher (uniform_mesh.h): closed meshes on every polyhedral
// grid, bevels of one width, the gap, the convex-only rule, the bumpy
// look, and its place in the mesh styles and the STL parameters. New
// (nothing in the original to compare with).
#include "../grid/grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/modifiers.h"
#include "shapelogic/mesh/polyhedron.h"
#include "shapelogic/mesh/stl.h"
#include "shapelogic/mesh/uniform_mesh.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace bt;
using namespace bt::mesh;

namespace {

// Every half-edge has a twin that points back, and nothing was closed
// with a hole face: the mesher's own faces are watertight.
void requireWatertight(const Polyhedron& p) {
  REQUIRE(p.numFaces() > 0);
  for (int i = 0; i < p.numHalfEdges(); i++) {
    const int h = p.halfEdgeAt(i);
    REQUIRE(p.he(h).twin != kNone);
    REQUIRE(p.he(p.he(h).twin).twin == h);
  }
  CHECK(p.numHoles() == 0);
}

int euler(const Polyhedron& p) { return p.numVertices() - p.numHalfEdges() / 2 + p.numFaces(); }

// Every face's normal points away from the inside of a convex solid.
void requireOutward(const Polyhedron& p, const Vec3& inside) {
  for (int i = 0; i < p.numFaces(); i++) {
    const int f = p.faceAt(i);
    if (p.face(f).hole) {
      continue;
    }
    const Vec3 n = p.faceNormal(f);
    const Vec3 c = p.dstPos(p.face(f).edge);
    CHECK((c - inside).dot(n) > 0.0F);
  }
}

// The grids whose cells are polyhedra (the sphere grid's balls are not,
// though their faces are those of a rhombic dodecahedron).
constexpr GridKind kPolyGrids[] = {GridKind::Bricks, GridKind::TriangularPrism, GridKind::Rhombic,
                                   GridKind::TetraOcta};

// The width of a bevel quad [b1, a1, a2, b2]: the distance from the
// corner a1 on one face to the line a2-b2 on the other (the quad's ends
// may be slanted where the edges at a corner have different angles, so
// a side's length is not the width).
double quadWidth(const Polyhedron& p, int f) {
  const int e0 = p.face(f).edge;
  const int e1 = p.he(e0).next;
  const int e2 = p.he(e1).next;
  const int e3 = p.he(e2).next;
  const Vec3 a1 = p.dstPos(e1);
  const Vec3 a2 = p.dstPos(e2);
  const Vec3 b2 = p.dstPos(e3);
  Vec3 along = b2 - a2;
  along.normalize();
  const Vec3 d = a1 - a2;
  const Vec3 across = d - along * d.dot(along);
  return across.module();
}

// The width of the first bevel quad of a mesh.
double chamferWidth(const Polyhedron& p) {
  for (int i = 0; i < p.numFaces(); i++) {
    const int f = p.faceAt(i);
    const Face& face = p.face(f);
    if ((face.flags & kFaceBevel) != 0 && p.faceSize(f) == 4) {
      return quadWidth(p, f);
    }
  }
  return 0;
}

}  // namespace

TEST_CASE("uniform mesh: one cell of every polyhedral grid is watertight and outward", "[mesh]") {
  for (const GridKind kind : kPolyGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    std::unique_ptr<Voxel> v = test::makeVoxel(gt, 6, 6, 6);
    const auto coords = test::validCoordinates(*v, 6);
    REQUIRE_FALSE(coords.empty());
    const auto [x, y, z] = coords.front();
    v->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                static_cast<unsigned int>(z), Voxel::kFilled);
    const unsigned int index = v->getIndex(static_cast<unsigned int>(x),
                                           static_cast<unsigned int>(y),
                                           static_cast<unsigned int>(z));

    for (const bool bumpy : {false, true}) {
      INFO((bumpy ? "bumpy" : "plain"));
      UniformMeshOptions o;
      o.bevel = 0.04;
      o.offset = 0.005;
      o.bumpy = bumpy;
      const Result<Polyhedron> m = uniformMesh(*v, o);
      REQUIRE(m);
      requireWatertight(*m);
      CHECK(euler(*m) == 2);
      // every face belongs to the cell; the cell faces carry their side
      int sides = 0;
      Vec3 centre;
      int corners = 0;
      for (int i = 0; i < m->numFaces(); i++) {
        const Face& f = m->face(m->faceAt(i));
        CHECK(f.voxel == index);
        if (f.voxelFace >= 0) {
          sides++;
          CHECK((f.flags & (kFaceBevel | kFaceOffset)) == 0);
        } else {
          CHECK((f.flags & kFaceWireframe) != 0);
          CHECK((f.flags & (kFaceBevel | kFaceOffset)) != 0);
        }
      }
      for (int i = 0; i < m->numVertices(); i++) {
        centre += m->vertex(i).pos;
        corners++;
      }
      centre /= static_cast<float>(corners);
      requireOutward(*m, centre);
      // one face per side of the cell, as the flat mesh has
      const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
      CHECK(sides == flat.numFaces());
      CHECK(volume(*m) < volume(flat));
      CHECK(volume(*m) > 0.5F * volume(flat));
    }
  }
}

TEST_CASE("uniform mesh: without bevel and gap it is the flat mesh", "[mesh]") {
  const GridType gt(GridKind::TriangularPrism);
  std::unique_ptr<Voxel> v = test::legalShape(gt, 4, 5);
  REQUIRE(v != nullptr);
  const Result<Polyhedron> m = uniformMesh(*v, UniformMeshOptions{});
  REQUIRE(m);
  requireWatertight(*m);
  const Polyhedron flat = shapeMesh(*v, MeshStyle::Flat);
  CHECK(m->numFaces() == flat.numFaces());
  CHECK(m->numVertices() == flat.numVertices());
  CHECK(volume(*m) == Catch::Approx(volume(flat)).margin(1e-5));
}

TEST_CASE("uniform mesh: a cube's chamfer is as wide as asked, a prism's too", "[mesh]") {
  UniformMeshOptions o;
  o.bevel = 0.1;
  // the bevel cuts 0.1 into each face of a right-angle edge: a chamfer
  // 0.1 * sqrt 2 wide, there and at every other edge
  const double width = 0.1 * std::sqrt(2.0);
  {
    const GridType gt(GridKind::Bricks);
    std::unique_ptr<Voxel> v = test::makeVoxel(gt, 3, 3, 3);
    v->setState(1, 1, 1, Voxel::kFilled);
    const Result<Polyhedron> m = uniformMesh(*v, o);
    REQUIRE(m);
    // 6 faces, 12 edge bevels, 8 corner fills
    CHECK(m->numFaces() == 6 + 12 + 8);
    CHECK(chamferWidth(*m) == Catch::Approx(width).epsilon(1e-3));
    // the drawing mesh's bevel of 0.03 along each face is the same cut:
    // the uniform style gives a cube the drawing mesh's chamfer
    const Polyhedron draw = shapeMesh(*v, MeshStyle::Drawing);
    const Polyhedron uni = shapeMesh(*v, MeshStyle::Uniform);
    CHECK(chamferWidth(draw) == Catch::Approx(0.03 * std::sqrt(2.0)).epsilon(1e-3));
    CHECK(chamferWidth(uni) == Catch::Approx(chamferWidth(draw)).epsilon(1e-3));
  }
  {
    // the 60 degree edges of a triangular prism get the same width
    const GridType gt(GridKind::TriangularPrism);
    std::unique_ptr<Voxel> v = test::legalShape(gt, 3, 1);
    REQUIRE(v != nullptr);
    const Result<Polyhedron> m = uniformMesh(*v, o);
    REQUIRE(m);
    requireWatertight(*m);
    double narrowest = 1e9;
    double widest = 0;
    for (int i = 0; i < m->numFaces(); i++) {
      const int f = m->faceAt(i);
      if ((m->face(f).flags & kFaceBevel) == 0 || m->faceSize(f) != 4) {
        continue;
      }
      const double w = quadWidth(*m, f);
      narrowest = std::min(narrowest, w);
      widest = std::max(widest, w);
    }
    CHECK(narrowest == Catch::Approx(width).epsilon(1e-3));
    CHECK(widest == Catch::Approx(width).epsilon(1e-3));
  }
}

TEST_CASE("uniform mesh: two cubes side by side share a flat gap, an L has a concave edge",
          "[mesh]") {
  const GridType gt(GridKind::Bricks);
  UniformMeshOptions o;
  o.bevel = 0.05;
  o.offset = 0.01;
  {
    std::unique_ptr<Voxel> v = test::makeVoxel(gt, 4, 3, 3);
    v->setState(1, 1, 1, Voxel::kFilled);
    v->setState(2, 1, 1, Voxel::kFilled);
    const Result<Polyhedron> m = uniformMesh(*v, o);
    REQUIRE(m);
    requireWatertight(*m);
    CHECK(euler(*m) == 2);
    int gaps = 0;
    for (int i = 0; i < m->numFaces(); i++) {
      const Face& f = m->face(m->faceAt(i));
      if ((f.flags & kFaceOffset) != 0) {
        gaps++;
        CHECK(f.voxelFace == -1);
      }
    }
    // the four flat strips across the shared edges, and the four corner
    // fills between them, lie in the planes of the faces: flagged as the gap
    CHECK(gaps >= 4);
    // no side face between the two cells
    int sides = 0;
    for (int i = 0; i < m->numFaces(); i++) {
      if (m->face(m->faceAt(i)).voxelFace >= 0) {
        sides++;
      }
    }
    CHECK(sides == 10);
  }
  {
    // an L: the inner edge is concave; bevelled, its chamfer fills the
    // corner in (material added); with convex-only bevels it stays sharp
    std::unique_ptr<Voxel> v = test::makeVoxel(gt, 4, 4, 3);
    v->setState(1, 1, 1, Voxel::kFilled);
    v->setState(2, 1, 1, Voxel::kFilled);
    v->setState(1, 2, 1, Voxel::kFilled);
    const Result<Polyhedron> all = uniformMesh(*v, o);
    REQUIRE(all);
    requireWatertight(*all);
    CHECK(euler(*all) == 2);
    o.convexBevelOnly = true;
    const Result<Polyhedron> convex = uniformMesh(*v, o);
    REQUIRE(convex);
    requireWatertight(*convex);
    CHECK(euler(*convex) == 2);
    CHECK(volume(*all) > volume(*convex));
  }
}

TEST_CASE("uniform mesh: a shape of every polyhedral grid, plain and bumpy, is watertight",
          "[mesh]") {
  for (const GridKind kind : kPolyGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    std::unique_ptr<Voxel> v = test::legalShape(gt, test::kLegalShapeBox, 9);
    REQUIRE(v != nullptr);
    for (const bool convexOnly : {false, true}) {
      for (const bool bumpy : {false, true}) {
        INFO((convexOnly ? "convex only, " : "all edges, ") << (bumpy ? "bumpy" : "plain"));
        UniformMeshOptions o;
        o.bevel = 0.03;
        o.offset = 0.004;
        o.convexBevelOnly = convexOnly;
        o.bumpy = bumpy;
        const Result<Polyhedron> m = uniformMesh(*v, o);
        REQUIRE(m);
        requireWatertight(*m);
        CHECK(volume(*m) > 0.0F);
      }
    }
  }
}

TEST_CASE("uniform mesh: variable cells carry the mark, colours and stretch come through",
          "[mesh]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 3, 3, 3);
  v->setState(1, 1, 1, Voxel::kVariable);
  v->setColor(1, 1, 1, 3);
  UniformMeshOptions o;
  o.bevel = 0.04;
  o.stretch[2] = 2;
  const Result<Polyhedron> m = uniformMesh(*v, o);
  REQUIRE(m);
  int marked = 0;
  float zmax = 0;
  for (int i = 0; i < m->numFaces(); i++) {
    const Face& f = m->face(m->faceAt(i));
    CHECK(f.colour == 3);
    if (f.voxelFace >= 0) {
      CHECK((f.flags & kFaceVariableMark) != 0);
      marked++;
    } else {
      CHECK((f.flags & kFaceVariableMark) == 0);
    }
  }
  CHECK(marked == 6);
  for (int i = 0; i < m->numVertices(); i++) {
    zmax = std::max(zmax, m->vertex(i).pos.z);
  }
  // the cell from z 1 to 2 stretched to z 2 to 4
  CHECK(zmax == Catch::Approx(4.0).margin(1e-5));
}

TEST_CASE("uniform mesh: the Uniform style and the STL switches", "[mesh][stl]") {
  const GridType gt(GridKind::Rhombic);
  std::unique_ptr<Voxel> v = test::legalShape(gt, test::kLegalShapeBox, 6);
  REQUIRE(v != nullptr);
  const Polyhedron styled = shapeMesh(*v, MeshStyle::Uniform);
  requireWatertight(styled);
  CHECK(styled.numFaces() > shapeMesh(*v, MeshStyle::Flat).numFaces());

  // the sphere grid draws its balls
  const GridType spheres(GridKind::Spheres);
  std::unique_ptr<Voxel> s = test::legalShape(spheres, test::kLegalShapeBox, 2);
  REQUIRE(s != nullptr);
  CHECK(shapeMesh(*s, MeshStyle::Uniform).numFaces() ==
        shapeMesh(*s, MeshStyle::Drawing).numFaces());

  // the STL parameters: the original's six, then the two switches, off
  const auto params = stlParameters(GridKind::Rhombic);
  REQUIRE(params.size() == 8);
  CHECK(params[6].name == "Uniform bevels");
  CHECK(params[7].name == "Unit Beveled");
  CHECK(params[6].defaultValue == 0.0);
  CHECK(params[7].defaultValue == 0.0);
  std::vector<double> values;
  for (const StlParameter& p : params) {
    values.push_back(p.defaultValue);
  }
  const Result<Polyhedron> original = stlMesh(*v, values);
  REQUIRE(original);
  values[6] = 1;
  const Result<Polyhedron> uniform = stlMesh(*v, values);
  REQUIRE(uniform);
  requireWatertight(*uniform);
  CHECK(uniform->numFaces() != original->numFaces());
  // in the unit size: the mesh spans several units
  float xmax = 0;
  for (int i = 0; i < uniform->numVertices(); i++) {
    xmax = std::max(xmax, uniform->vertex(i).pos.x);
  }
  CHECK(xmax > 5.0F);
  values[7] = 1;
  const Result<Polyhedron> bumpy = stlMesh(*v, values);
  REQUIRE(bumpy);
  requireWatertight(*bumpy);
  // the unit sizes stretch the cells before the bevel: still watertight
  values[1] = 15;
  values[2] = 20;
  const Result<Polyhedron> stretched = stlMesh(*v, values);
  REQUIRE(stretched);
  requireWatertight(*stretched);
  // variable cells are refused as before
  std::unique_ptr<Voxel> var = test::legalShape(gt, test::kLegalShapeBox, 2);
  REQUIRE(var != nullptr);
  const auto coords = test::validCoordinates(*var, test::kLegalShapeBox);
  const auto [x, y, z] = coords.front();
  var->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                static_cast<unsigned int>(z), Voxel::kVariable);
  CHECK_FALSE(stlMesh(*var, values));
}
