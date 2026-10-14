// SPDX-License-Identifier: GPL-3.0-or-later
// The STL meshes and files. Ported from the original's
// test/test_cubemesh.cpp, test/test_minkmesh.cpp (the random-shapes stress
// case as [slow]) and test/test_manifold_smoke.cpp; then new cases: the
// parameters of each grid, the refusals with the original's messages, the
// sphere grid's mesh, the file layouts and the volume.
// tests/unit/mesh/test_stl_dump.cpp compares the meshes of every fixture
// shape with the original's.
#include "../grid/grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cube_mesh.h"
#include "shapelogic/mesh/grid_geometry.h"
#include "shapelogic/mesh/mink_mesh.h"
#include "shapelogic/mesh/modifiers.h"
#include "shapelogic/mesh/polyhedron.h"
#include "shapelogic/mesh/stl.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <manifold/manifold.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::mesh;

namespace {

// the 12 face-connected vertex bodies (bit 4x+2y+z of the 2x2x2 block)
const int kCanonical[] = {1, 3, 7, 15, 23, 27, 31, 61, 63, 111, 126, 127};

std::vector<char> block(int mask) {
  std::vector<char> filled(8, 0);
  for (int cx = 0; cx < 2; cx++) {
    for (int cy = 0; cy < 2; cy++) {
      for (int cz = 0; cz < 2; cz++) {
        if ((mask & (1 << ((4 * cx) + (2 * cy) + cz))) != 0) {
          filled[static_cast<std::size_t>(cx + (2 * (cy + (2 * cz))))] = 1;
        }
      }
    }
  }
  return filled;
}

// every directed edge exactly once, and its reverse once
bool watertight(const CubeSurface& m) {
  std::map<std::pair<int, int>, int> cnt;
  for (std::size_t t = 0; t + 2 < m.tris.size(); t += 3) {
    for (std::size_t c = 0; c < 3; c++) {
      cnt[std::make_pair(m.tris[t + c], m.tris[t + ((c + 1) % 3)])]++;
    }
  }
  for (const auto& e : cnt) {
    if (e.second != 1) {
      return false;
    }
    if (!cnt.contains(std::make_pair(e.first.second, e.first.first))) {
      return false;
    }
  }
  return true;
}

double volume(const CubeSurface& m) {
  double vol = 0;
  for (std::size_t t = 0; t + 2 < m.tris.size(); t += 3) {
    const DVec3& a = m.verts[static_cast<std::size_t>(m.tris[t])];
    const DVec3& b = m.verts[static_cast<std::size_t>(m.tris[t + 1])];
    const DVec3& c = m.verts[static_cast<std::size_t>(m.tris[t + 2])];
    vol += (a.x * ((b.y * c.z) - (b.z * c.y))) - (a.y * ((b.x * c.z) - (b.z * c.x))) +
           (a.z * ((b.x * c.y) - (b.y * c.x)));
  }
  return vol / 6;
}

CubeSurface generate(int mask, double g, double r, bool fills) {
  Result<CubeSurface> m = cubeSurface(2, 2, 2, block(mask), g, r, fills);
  INFO("mask " << mask << " g " << g << " r " << r << " fills " << fills << ": "
               << (m ? std::string() : m.error().message));
  REQUIRE(m);
  return *m;
}

// a small shape on a grid: every valid cell of a block but the first valid
// cell of the top layer, so that there are concave edges
std::unique_ptr<Voxel> shape(const GridType& gt, unsigned sx, unsigned sy, unsigned sz) {
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, sx, sy, sz);
  bool removed = false;
  for (unsigned z = 0; z < sz; z++) {
    for (unsigned y = 0; y < sy; y++) {
      for (unsigned x = 0; x < sx; x++) {
        if (!v->validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z))) {
          continue;
        }
        if (z == sz - 1 && !removed) {
          removed = true;
          continue;
        }
        v->setState(x, y, z, Voxel::kFilled);
      }
    }
  }
  return v;
}

struct GridTestDef {
  GridKind g;
  unsigned sx;
  unsigned sy;
  unsigned sz;
  const char* name;
};

const GridTestDef kMinkGrids[] = {
    {GridKind::TriangularPrism, 4, 3, 2, "prism"},
    {GridKind::Rhombic, 5, 5, 5, "rhombic"},
    {GridKind::TetraOcta, 5, 5, 5, "tetra-octa"},
};

// the default values of the grid's parameters
std::vector<double> defaults(GridKind g) {
  std::vector<double> values;
  for (const StlParameter& p : stlParameters(g)) {
    values.push_back(p.defaultValue);
  }
  return values;
}

std::string refusal(const Voxel& v, const std::vector<double>& values) {
  Result<Polyhedron> m = stlMesh(v, values);
  REQUIRE_FALSE(m.has_value());
  return m.error().message;
}

// the volume of an STL mesh, in double for the comparisons
double vol(const Polyhedron& p) { return static_cast<double>(mesh::volume(p)); }

Vec3 centroid(const Polyhedron& p, int f) {
  Vec3 cen(0, 0, 0);
  int nv = 0;
  int e = p.face(f).edge;
  const int s = e;
  do {
    cen += p.dstPos(e);
    nv++;
    e = p.he(e).next;
  } while (e != s);
  return cen / static_cast<float>(nv);
}

}  // namespace

// ---- ported from test/test_cubemesh.cpp ------------------------------

TEST_CASE("cube lookup mesher: every vertex body is watertight in both variants", "[mesh][stl]") {
  const double g = 0.05;
  for (const int mask : kCanonical) {
    for (const double ratio : {0.0, 0.7, 2.686, 4.5}) {
      for (const bool fills : {true, false}) {
        const CubeSurface m = generate(mask, g, ratio * g, fills);
        CHECK(watertight(m));
        CHECK(volume(m) > 0);
      }
    }
  }
}

TEST_CASE("cube lookup mesher: chamfers remove material, fills add it back", "[mesh][stl]") {
  const double g = 0.05;
  const double r = 0.12;
  const CubeSurface gap = generate(23, g, 0, true);
  const CubeSurface fill = generate(23, g, r, true);
  const CubeSurface nofill = generate(23, g, r, false);
  CHECK(volume(gap) < 4.0);              // four cells, gapped
  CHECK(volume(fill) < volume(gap));     // convex edges bevelled
  CHECK(volume(nofill) < volume(fill));  // and without the concave fills, less again
}

TEST_CASE("cube lookup mesher: the zero cases and interior vertices", "[mesh][stl]") {
  // a solid 3x3x3 block has an interior vertex, no surface there
  const std::vector<char> solid(27, 1);
  Result<CubeSurface> m = cubeSurface(3, 3, 3, solid, 0.05, 0.1, true);
  REQUIRE(m);
  CHECK(watertight(*m));
  // gap and bevel both zero: the plain cells
  m = cubeSurface(3, 3, 3, solid, 0, 0, true);
  REQUIRE(m);
  CHECK(watertight(*m));
  CHECK(volume(*m) == 27.0);
  // gap zero with a bevel: the r/g -> infinity limit
  CHECK(watertight(generate(27, 0, 0.1, true)));
  // out of range is refused with a message
  m = cubeSurface(2, 2, 2, block(27), 0.3, 0.3, true);
  REQUIRE_FALSE(m.has_value());
  CHECK(m.error().message == "gap + bevel must stay below half a cell");
}

TEST_CASE("cube STL exporter uses the lookup mesher", "[mesh][stl]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 3, 3, 2);
  const unsigned cells[][3] = {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {1, 0, 0}, {1, 0, 1},
                               {2, 0, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}, {0, 2, 0}};
  for (const auto& c : cells) {
    v->setState(c[0], c[1], c[2], Voxel::kFilled);
  }
  REQUIRE(stlParameters(GridKind::Bricks).size() == 6);
  std::vector<double> values = defaults(GridKind::Bricks);
  values[0] = 10;   // unit size
  values[3] = 1.0;  // bevel
  values[4] = 0.5;  // offset
  const Result<Polyhedron> withFills = stlMesh(*v, values);
  REQUIRE(withFills);
  CHECK(withFills->numFaces() > 0);
  values[5] = 0;  // interior chamfers off
  const Result<Polyhedron> noFills = stlMesh(*v, values);
  REQUIRE(noFills);
  CHECK(noFills->numFaces() > 0);
  CHECK(noFills->numFaces() != withFills->numFaces());
  // the zero cases export too
  values[3] = 0;
  values[4] = 0;
  const Result<Polyhedron> plain = stlMesh(*v, values);
  REQUIRE(plain);
  CHECK(plain->numFaces() == 80);  // 40 exposed cell faces, two triangles each
}

TEST_CASE("the 3D view's STL style uses the lookup mesher on the cube grid", "[mesh][stl]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 2, 2, 2);
  for (unsigned i = 0; i < 8; i++) {
    if ((23 & (1U << i)) != 0) {
      v->setState((i >> 2) & 1, (i >> 1) & 1, i & 1, Voxel::kFilled);
    }
  }
  const Polyhedron view = shapeMesh(*v, MeshStyle::Stl);
  const Result<Polyhedron> same = cubePolyhedron(*v, 0.02, 0.05, true);
  REQUIRE(same);
  CHECK(view.numFaces() == same->numFaces());
  CHECK(view.numFaces() > 0);
}

// ---- ported from test/test_minkmesh.cpp ------------------------------

TEST_CASE("Minkowski mesher: prism, rhombic and tetra-octa shapes, both variants",
          "[mesh][stl]") {
  for (const GridTestDef& gr : kMinkGrids) {
    const GridType gt(gr.g);
    const std::unique_ptr<Voxel> v = shape(gt, gr.sx, gr.sy, gr.sz);
    for (const bool fills : {true, false}) {
      const Result<Polyhedron> p = minkPolyhedron(*v, 0.02, 0.05, fills);
      INFO(gr.name << " fills " << fills << ": " << (p ? std::string() : p.error().message));
      REQUIRE(p);
      CHECK(p->numFaces() > 0);
      CHECK(p->check(false));
    }
  }
}

TEST_CASE("Minkowski mesher: the exporter and the view use it on the rhombic grid",
          "[mesh][stl]") {
  const GridType gt(GridKind::Rhombic);
  const std::unique_ptr<Voxel> v = shape(gt, 5, 5, 5);
  std::vector<double> values = defaults(GridKind::Rhombic);
  values[0] = 10;
  values[3] = 0.5;
  values[4] = 0.2;
  const Result<Polyhedron> withFills = stlMesh(*v, values);
  values[5] = 0;
  const Result<Polyhedron> noFills = stlMesh(*v, values);
  REQUIRE(withFills);
  REQUIRE(noFills);
  CHECK(withFills->numFaces() > 0);
  CHECK(noFills->numFaces() > 0);
  CHECK(withFills->numFaces() != noFills->numFaces());
  CHECK(shapeMesh(*v, MeshStyle::Stl).numFaces() > 0);
}

// Random shapes on the three grids, both variants, a few gap / bevel
// settings including the b = 2g tie: every result a valid polyhedron.
// BT_MINK_STRESS scales the count (the original's MINK_STRESS).
TEST_CASE("Minkowski mesher: random shapes", "[mesh][stl][slow]") {
  const char* env = std::getenv("BT_MINK_STRESS");
  const int count = env != nullptr ? std::atoi(env) : 3;
  std::mt19937 rng(12345);
  const GridTestDef grids[] = {
      {GridKind::TriangularPrism, 6, 4, 3, "prism"},
      {GridKind::Rhombic, 7, 7, 7, "rhombic"},
      {GridKind::TetraOcta, 7, 7, 7, "tetra-octa"},
  };
  // gap + 2 x 1.707 x bevel must stay below the cells' inradius (about
  // 0.29 for the tetrahedra); 0.02 / 0.04 is the b = 2g tie
  const double settings[][2] = {{0.02, 0.05}, {0.02, 0.04}, {0.03, 0.06},
                                {0.03, 0.0},  {0.0, 0.04},  {0.0, 0.0}};
  for (const GridTestDef& gr : grids) {
    const GridType gt(gr.g);
    for (int n = 0; n < count; n++) {
      // a random connected blob: grown from a seed cell through face neighbours
      std::unique_ptr<Voxel> v = test::makeVoxel(gt, gr.sx, gr.sy, gr.sz);
      std::vector<std::array<int, 3>> cells;
      for (int tries = 0; cells.empty() && tries < 1000; tries++) {
        const int x = static_cast<int>(rng() % gr.sx);
        const int y = static_cast<int>(rng() % gr.sy);
        const int z = static_cast<int>(rng() % gr.sz);
        if (v->validCoordinate(x, y, z)) {
          v->setState(static_cast<unsigned>(x), static_cast<unsigned>(y),
                      static_cast<unsigned>(z), Voxel::kFilled);
          cells.push_back({x, y, z});
        }
      }
      const unsigned target = 6 + (rng() % 20);
      for (int tries = 0; cells.size() < target && tries < 2000; tries++) {
        const std::array<int, 3>& c = cells[rng() % cells.size()];
        int nx = 0;
        int ny = 0;
        int nz = 0;
        const auto face = static_cast<unsigned>(rng() % 12);
        if (!v->getNeighbor(face, 0, c[0], c[1], c[2], &nx, &ny, &nz)) {
          continue;
        }
        if (nx < 0 || ny < 0 || nz < 0 || nx >= static_cast<int>(gr.sx) ||
            ny >= static_cast<int>(gr.sy) || nz >= static_cast<int>(gr.sz)) {
          continue;
        }
        if (!v->validCoordinate(nx, ny, nz) ||
            v->isFilled(static_cast<unsigned>(nx), static_cast<unsigned>(ny),
                        static_cast<unsigned>(nz))) {
          continue;
        }
        v->setState(static_cast<unsigned>(nx), static_cast<unsigned>(ny),
                    static_cast<unsigned>(nz), Voxel::kFilled);
        cells.push_back({nx, ny, nz});
      }
      for (const auto& s : settings) {
        for (const bool fills : {true, false}) {
          const Result<Polyhedron> p = minkPolyhedron(*v, s[0], s[1], fills);
          INFO(gr.name << " shape " << n << " (" << cells.size() << " cells) g " << s[0] << " r "
                       << s[1] << " fills " << fills << ": "
                       << (p ? std::string() : p.error().message));
          REQUIRE(p);
          CHECK(p->numFaces() > 0);
          CHECK(p->check(false));
        }
      }
    }
  }
}

// The 3D view removes the voxel a clicked face is tagged with and adds a
// voxel across the tagged cell face: every face of the view's mesh must
// lie in the cell of its voxel, and a tagged cell face must face the
// neighbour it names.
TEST_CASE("the view's STL mesh tags every face with its voxel on every grid", "[mesh][stl]") {
  const GridTestDef grids[] = {
      {GridKind::Bricks, 3, 3, 2, "bricks"},
      {GridKind::TriangularPrism, 4, 3, 2, "prism"},
      {GridKind::Rhombic, 5, 5, 5, "rhombic"},
      {GridKind::TetraOcta, 5, 5, 5, "tetra-octa"},
  };
  for (const GridTestDef& gr : grids) {
    const GridType gt(gr.g);
    const std::unique_ptr<Voxel> v = shape(gt, gr.sx, gr.sy, gr.sz);
    const Polyhedron view = shapeMesh(*v, MeshStyle::Stl);
    int tagged = 0;
    for (int i = 0; i < view.numFaces(); i++) {
      const int f = view.faceAt(i);
      const Face& face = view.face(f);
      unsigned int x = 0;
      unsigned int y = 0;
      unsigned int z = 0;
      INFO(gr.name << " face " << i << " voxel " << face.voxel << " side " << face.voxelFace);
      REQUIRE(v->indexToXYZ(face.voxel, &x, &y, &z));
      REQUIRE(v->isFilled(x, y, z));
      // the cell's faces as outward planes; the centroid inside all of them
      const Vec3 cen = centroid(view, f);
      std::vector<float> corners;
      int nx = 0;
      int ny = 0;
      int nz = 0;
      Vec3 centre(0, 0, 0);
      int nc = 0;
      std::vector<std::vector<Vec3>> polys;
      const int ix = static_cast<int>(x);
      const int iy = static_cast<int>(y);
      const int iz = static_cast<int>(z);
      for (unsigned n = 0; v->getNeighbor(n, 0, ix, iy, iz, &nx, &ny, &nz); n++) {
        corners.clear();
        connectionFace(*v, ix, iy, iz, static_cast<int>(n), 0, 0, corners);
        std::vector<Vec3> pts;
        for (std::size_t k = 0; k + 2 < corners.size(); k += 3) {
          pts.emplace_back(corners[k], corners[k + 1], corners[k + 2]);
          centre += pts.back();
          nc++;
        }
        polys.push_back(pts);
      }
      centre = centre / static_cast<float>(nc);
      for (std::size_t n = 0; n < polys.size(); n++) {
        const std::vector<Vec3>& pts = polys[n];
        if (pts.size() < 3) {
          continue;
        }
        Vec3 nrm = (pts[1] - pts[0]).cross(pts[2] - pts[0]);
        if (nrm.dot(pts[0] - centre) < 0) {
          nrm = -nrm;
        }
        nrm.normalize();
        // a fill at a concave edge bulges into the empty cell by the gap
        CHECK(static_cast<double>(nrm.dot(cen - pts[0])) < (face.voxelFace >= 0 ? 1e-4 : 0.05));
        if (face.voxelFace == static_cast<int>(n)) {
          tagged++;
          CHECK(static_cast<double>(view.faceNormal(f).dot(nrm)) > 0.99);
          // The neighbour the view would add lies across this face: its
          // centre (from its own cell geometry) is on the outside of the
          // face plane, and the face plane cuts the segment between the
          // two centres.
          REQUIRE(v->getNeighbor(static_cast<unsigned>(n), 0, ix, iy, iz, &nx, &ny, &nz));
          Vec3 nbCentre(0, 0, 0);
          int nn = 0;
          int qx = 0;
          int qy = 0;
          int qz = 0;
          for (unsigned m = 0; v->getNeighbor(m, 0, nx, ny, nz, &qx, &qy, &qz); m++) {
            corners.clear();
            connectionFace(*v, nx, ny, nz, static_cast<int>(m), 0, 0, corners);
            for (std::size_t k = 0; k + 2 < corners.size(); k += 3) {
              nbCentre += Vec3(corners[k], corners[k + 1], corners[k + 2]);
              nn++;
            }
          }
          nbCentre = nbCentre / static_cast<float>(nn);
          const double outside = static_cast<double>(nrm.dot(nbCentre - pts[0]));
          const double inside = static_cast<double>(nrm.dot(centre - pts[0]));
          INFO("neighbour " << nx << "," << ny << "," << nz << " outside " << outside
                            << " own centre inside " << inside);
          CHECK(outside > 0);
          CHECK(inside < 0);
          CHECK((nbCentre - centre).module() < 2.0);
        }
      }
    }
    CHECK(tagged > 0);
  }
}

// ---- ported from test/test_manifold_smoke.cpp ------------------------

TEST_CASE("manifold smoke: hull, minkowski, boolean", "[mesh][stl]") {
  using manifold::Manifold;
  using manifold::vec3;
  const Manifold cube = Manifold::Cube({1, 1, 1});
  const std::vector<vec3> oct = {{0.1, 0, 0},  {-0.1, 0, 0}, {0, 0.1, 0},
                                 {0, -0.1, 0}, {0, 0, 0.1},  {0, 0, -0.1}};
  const Manifold bit = Manifold::Hull(oct);
  const Manifold sum = cube.MinkowskiSum(bit);
  CHECK(sum.Status() == Manifold::Error::NoError);
  CHECK(sum.Volume() > cube.Volume());
  CHECK(sum.Genus() == 0);
  const Manifold diff = sum - cube;
  CHECK(diff.Status() == Manifold::Error::NoError);
  CHECK(std::fabs(diff.Volume() - (sum.Volume() - cube.Volume())) < 1e-9);
}

// ---- new ---------------------------------------------------------------

TEST_CASE("STL parameters: each grid's, as the original's export dialog", "[mesh][stl]") {
  for (const GridKind g : {GridKind::Bricks, GridKind::TriangularPrism, GridKind::Rhombic,
                           GridKind::TetraOcta, GridKind::Sliding, GridKind::Stacking}) {
    const auto p = stlParameters(g);
    REQUIRE(p.size() == 6);
    CHECK(p[0].name == "Unit Size");
    CHECK(p[0].defaultValue == 10);
    CHECK(p[3].name == "Bevel");
    CHECK(p[3].defaultValue == 0.3);
    CHECK(p[4].name == "Offset");
    CHECK(p[4].defaultValue == 0.01);
    CHECK(p[5].type == StlParameterType::Switch);
    CHECK(p[5].defaultValue == 1);
  }
  const auto s = stlParameters(GridKind::Spheres);
  REQUIRE(s.size() == 8);
  CHECK(s[0].name == "Sphere radius");
  CHECK(s[4].name == "Recursions");
  CHECK(s[4].type == StlParameterType::PositiveInteger);
  CHECK(s[4].defaultValue == 2);
  CHECK(s[7].name == "Square Hole");
  CHECK(s[7].type == StlParameterType::Switch);
}

TEST_CASE("STL meshes: out-of-range values are refused with the original's words",
          "[mesh][stl]") {
  const GridType bricks(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(bricks, 2, 1, 1);
  v->setState(0, 0, 0, Voxel::kFilled);
  v->setState(1, 0, 0, Voxel::kFilled);
  const std::vector<double> d = defaults(GridKind::Bricks);
  auto with = [&](std::size_t i, double value) {
    std::vector<double> values = d;
    values[i] = value;
    return refusal(*v, values);
  };
  CHECK(with(0, 0) == "Unit size too small");
  CHECK(with(1, -1) == "Unit size y too small");
  CHECK(with(1, 1e-6) == "Unit size y too small");
  CHECK(with(2, -1) == "Unit size z too small");
  CHECK(with(3, -0.1) == "Bevel cannot be negative");
  CHECK(with(4, -0.1) == "Offset cannot be negative");
  CHECK(refusal(*v, {10, 0, 0}) == "Expected 6 STL parameters, got 3");
  // bevel and offset too large for the cell (the mesher's own words)
  CHECK(with(3, 6) == "gap + bevel must stay below half a cell");
  v->setState(1, 0, 0, Voxel::kVariable);
  CHECK(refusal(*v, d) == "Shapes with variable voxels cannot be exported");

  const GridType spheres(GridKind::Spheres);
  std::unique_ptr<Voxel> s = test::makeVoxel(spheres, 2, 2, 2);
  s->setState(0, 0, 0, Voxel::kFilled);
  const std::vector<double> sd = defaults(GridKind::Spheres);
  auto sphereWith = [&](std::size_t i, double value) {
    std::vector<double> values = sd;
    values[i] = value;
    return refusal(*s, values);
  };
  CHECK(sphereWith(0, 0) == "Sphere size too small");
  CHECK(sphereWith(3, -1) == "Offset cannot be negative");
  CHECK(sphereWith(2, -1) == "Curvature radius cannot be negative");
  CHECK(sphereWith(3, 11) == "Offset must be smaller than sphere radius");
  CHECK(sphereWith(2, 1.5) == "The curvature radius is relative and must be between 0 and 1");
  CHECK(sphereWith(1, 1.5) == "The connection radius is relative and must be between 0 and 1");
  CHECK(sphereWith(5, 10) ==
        "The inner radius cannot be negative and must be less than the sphere radius");
  CHECK(sphereWith(6, 4) ==
        "The hole diameter cannot be negative or greater than 1/3 of the sphere radius");
}

TEST_CASE("STL meshes: the unit sizes scale the axes, the volume is the cells'", "[mesh][stl]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 2, 2, 2);
  for (unsigned i = 0; i < 8; i++) {
    v->setState(i & 1, (i >> 1) & 1, (i >> 2) & 1, Voxel::kFilled);
  }
  // plain cells: 8 cells of 10 x 20 x 30
  const Result<Polyhedron> plain = stlMesh(*v, std::vector<double>{10, 20, 30, 0, 0, 1});
  REQUIRE(plain);
  CHECK(vol(*plain) == Catch::Approx(8 * 10 * 20 * 30).epsilon(1e-5));
  // a bevel and an offset take material away
  const Result<Polyhedron> chamfered = stlMesh(*v, defaults(GridKind::Bricks));
  REQUIRE(chamfered);
  CHECK(vol(*chamfered) < 8000);
  CHECK(vol(*chamfered) > 7000);
}

TEST_CASE("STL files: binary and text layouts", "[mesh][stl]") {
  const GridType gt(GridKind::Bricks);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 1, 1, 1);
  v->setState(0, 0, 0, Voxel::kFilled);
  const Result<Polyhedron> m = stlMesh(*v, std::vector<double>{1, 0, 0, 0, 0, 1});
  REQUIRE(m);
  const std::vector<std::uint8_t> b = stlFile(*m, "cube.stl", true);
  // the header: the name, zeros, then the count of the 12 triangles
  REQUIRE(b.size() == 84 + (12 * 50));
  CHECK(std::string(reinterpret_cast<const char*>(b.data())) == "cube.stl");
  CHECK(b[79] == 0);
  std::uint32_t count = 0;
  std::memcpy(&count, b.data() + 80, 4);
  CHECK(count == 12);
  // a long name fills the header and no more
  const std::string longName(100, 'x');
  const std::vector<std::uint8_t> l = stlFile(*m, longName, true);
  CHECK(std::string(reinterpret_cast<const char*>(l.data()), 80) == longName.substr(0, 80));
  std::memcpy(&count, l.data() + 80, 4);
  CHECK(count == 12);

  const std::vector<std::uint8_t> t = stlFile(*m, "cube.stl", false);
  const std::string text(t.begin(), t.end());
  CHECK(text.starts_with("solid cube.stl\n  facet normal "));
  CHECK(text.ends_with("    endloop\n  endfacet\nendsolid\n"));
  std::size_t facets = 0;
  for (std::size_t p = text.find("facet normal"); p != std::string::npos;
       p = text.find("facet normal", p + 1)) {
    facets++;
  }
  CHECK(facets == 12);
  // the original's number format
  CHECK(text.find("      vertex 1.0000e+00 ") != std::string::npos);
}

TEST_CASE("STL meshes: the sphere grid's balls, hollow and with holes", "[mesh][stl]") {
  const GridType gt(GridKind::Spheres);
  std::unique_ptr<Voxel> v = test::makeVoxel(gt, 2, 2, 2);
  v->setState(0, 0, 0, Voxel::kFilled);
  const Result<Polyhedron> ball = stlMesh(*v, defaults(GridKind::Spheres));
  REQUIRE(ball);
  // a ball of radius 10 (recursion 2 makes it a little smaller)
  const double full = 4.0 / 3.0 * 3.141592653589793 * 1000;
  CHECK(vol(*ball) < full);
  CHECK(vol(*ball) > 0.9 * full);
  std::vector<double> hollow = defaults(GridKind::Spheres);
  hollow[5] = 5;  // inner radius
  const Result<Polyhedron> shell = stlMesh(*v, hollow);
  REQUIRE(shell);
  CHECK(vol(*shell) < vol(*ball));
  hollow[6] = 2;  // round holes
  REQUIRE(stlMesh(*v, hollow));
  hollow[7] = 1;  // square holes
  REQUIRE(stlMesh(*v, hollow));
}
