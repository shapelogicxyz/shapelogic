// SPDX-License-Identifier: GPL-3.0-or-later
// The grid tables rebuilt from geometry (docs/V2-IDEAS-PLAN.md, idea 2):
// the hand-written neighbour tables of every grid against what the cells'
// face polygons say, the transformations against the cells' geometry, and
// the generated symmetry tables against the group they describe.
// BurrTools V2 derives all of this at start-up from each grid's polyhedra
// (its src/lib/grid.cpp); here mesh/cell_geometry.h derives it from
// connectionFace and the ported tables are what it checks. New (the
// original had no such test).
#include "grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cell_geometry.h"
#include "shapelogic/mesh/polyhedron.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace bt;
using namespace bt::test;
using bt::mesh::CellAt;
using bt::mesh::CellFace;
using bt::mesh::CellGeometry;
using bt::mesh::CellPolyhedron;
using bt::mesh::Vec3;

namespace {

using Vec = std::array<double, 3>;

// Positions this close (mesh units) are one point, as cell_geometry.h sees it.
constexpr double kNear = 1e-4;

Vec toVec(const Vec3& p) {
  return {static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)};
}
Vec3 toVec3(const Vec& p) {
  return {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
}

std::string str(const CellAt& c) {
  std::ostringstream s;
  s << "(" << c.x << ", " << c.y << ", " << c.z << ")";
  return s.str();
}

std::string str(const std::set<CellAt>& cs) {
  std::string s;
  for (const CellAt& c : cs) {
    s += str(c) + " ";
  }
  return s;
}

CellAt at(const std::tuple<int, int, int>& t) {
  return {std::get<0>(t), std::get<1>(t), std::get<2>(t)};
}

Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec& a, const Vec& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double length(const Vec& a) { return std::sqrt(dot(a, a)); }
bool near(const Vec& a, const Vec& b) { return length(sub(a, b)) < kNear; }

// m (row major) times p.
Vec rotated(const double m[9], const Vec& p) {
  return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2], m[3] * p[0] + m[4] * p[1] + m[5] * p[2],
          m[6] * p[0] + m[7] * p[1] + m[8] * p[2]};
}

// The polyhedron of a cell, which every grid must give.
CellPolyhedron polyhedronOf(const Voxel& v, const CellAt& c) {
  const Result<CellPolyhedron> p = mesh::cellPolyhedron(v, c);
  INFO("cell " << str(c) << ": " << (p ? "" : p.error().message));
  REQUIRE(p);
  return *p;
}

// The neighbours of a kind that getNeighbor lists.
std::vector<CellAt> listed(const Voxel& v, const CellAt& c, unsigned int typ) {
  std::vector<CellAt> out;
  int xn = 0;
  int yn = 0;
  int zn = 0;
  for (unsigned int n = 0; v.getNeighbor(n, typ, c.x, c.y, c.z, &xn, &yn, &zn); n++) {
    out.push_back({xn, yn, zn});
  }
  return out;
}

// The grids whose cells are polyhedra with corners shared with their
// neighbours. The sphere grid's "faces" are those of the rhombic
// dodecahedron around each ball: they still tell the face neighbours.
constexpr GridKind kPolyGrids[] = {GridKind::Bricks, GridKind::TriangularPrism, GridKind::Rhombic,
                                   GridKind::TetraOcta};

// The period of each grid's cell pattern along each axis: cells this far
// apart are the same kind of cell.
int periodOf(GridKind k) {
  switch (k) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      return 1;
    case GridKind::TriangularPrism:
    case GridKind::Spheres:
      return 2;
    case GridKind::Rhombic:
      return 5;
    case GridKind::TetraOcta:
      // the 3x3x3 blocks alternate between two kinds
      return 6;
  }
  return 1;
}

// The number of symmetry groups in the grid's generated table
// (tabs_N/tablesizes.inc; the tables are not asked, so a change there
// shows up here).
unsigned int groupsOf(GridKind k) {
  switch (k) {
    case GridKind::TriangularPrism:
      return 54;
    case GridKind::Spheres:
      return 241;
    default:
      return 98;
  }
}

}  // namespace

TEST_CASE("grid oracle: every cell is a closed convex polyhedron, its faces outward", "[grid]") {
  for (const GridKind kind : kGeometryGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const int period = periodOf(kind);
    const int box = period * 2;
    const std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box));
    int cells = 0;
    for (const auto& t : validCoordinates(*v, box)) {
      const CellAt c = at(t);
      INFO("cell " << str(c));
      const CellPolyhedron g = polyhedronOf(*v, c);
      REQUIRE(g.faces.size() >= 4);
      cells++;
      // as many faces as face neighbours, each across the face it says
      const std::vector<CellAt> across = listed(*v, c, 0);
      REQUIRE(g.faces.size() == across.size());
      for (std::size_t n = 0; n < across.size(); n++) {
        CHECK(g.faces[n].across == across[n]);
      }
      const Vec centre = toVec(g.centroid);
      // every directed edge has exactly one reverse (a closed surface);
      // each face looks away from the centre and has every corner of the
      // cell behind it (convex); the stored normal is that of the corners
      std::map<std::pair<int, int>, int> edges;
      const auto cornerIndex = [&](const Vec3& p) {
        for (std::size_t i = 0; i < g.corners.size(); i++) {
          if (mesh::samePoint(g.corners[i], p)) {
            return static_cast<int>(i);
          }
        }
        return -1;
      };
      for (const CellFace& face : g.faces) {
        REQUIRE(face.size() >= 3);
        const Vec f0 = toVec(face.corners[0]);
        Vec n = cross(sub(toVec(face.corners[1]), f0), sub(toVec(face.corners[2]), f0));
        const double l = length(n);
        REQUIRE(l > 1e-9);
        for (double& x : n) {
          x /= l;
        }
        CHECK(near(n, toVec(face.normal)));
        CHECK(dot(n, sub(f0, centre)) > 1e-6);
        for (const Vec3& p : g.corners) {
          CHECK(dot(n, sub(toVec(p), f0)) < kNear);
        }
        for (std::size_t i = 0; i < face.size(); i++) {
          const int a = cornerIndex(face.pos(i));
          const int b = cornerIndex(face.pos(i + 1));
          REQUIRE(a >= 0);
          REQUIRE(b >= 0);
          REQUIRE(a != b);
          edges[{a, b}]++;
        }
      }
      for (const auto& [e, count] : edges) {
        CHECK(count == 1);
        CHECK(edges.count({e.second, e.first}) == 1);
      }
    }
    CHECK(cells > 0);
  }
}

TEST_CASE("grid oracle: the neighbour tables are what the cells' geometry says", "[grid]") {
  // a cell's neighbours of every kind, rebuilt from the corners the
  // cells share (CellGeometry::derivedNeighbours): a whole face, two or
  // more corners, one corner. The box is big enough for every neighbour
  // of its middle cells to lie inside.
  constexpr int kReach = 7;
  for (const GridKind kind : kGeometryGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const int period = periodOf(kind);
    const int box = 2 * kReach + period;
    const std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box));
    CellGeometry geometry(*v);
    int checked = 0;
    for (const auto& t : validCoordinates(*v, box)) {
      const CellAt c = at(t);
      if (c.x < kReach || c.y < kReach || c.z < kReach || c.x >= kReach + period ||
          c.y >= kReach + period || c.z >= kReach + period) {
        continue;
      }
      INFO("cell " << str(c));
      checked++;
      const auto derived = [&](unsigned int kindOf, std::vector<int>* faces = nullptr) {
        const Result<std::vector<CellAt>> r = geometry.derivedNeighbours(c, kindOf, kReach, faces);
        REQUIRE(r);
        return std::set<CellAt>(r->begin(), r->end());
      };
      std::vector<int> sharedFaces;
      const std::set<CellAt> face = derived(0, &sharedFaces);
      const std::vector<CellAt> faceList = listed(*v, c, 0);
      const std::set<CellAt> faceListed(faceList.begin(), faceList.end());
      INFO("faces by geometry: " << str(face));
      INFO("faces by table:    " << str(faceListed));
      CHECK(faceListed == face);
      // face n of the table is the face shared with neighbour n
      {
        const Result<std::vector<CellAt>> r = geometry.derivedNeighbours(c, 0, kReach, &sharedFaces);
        REQUIRE(r);
        for (std::size_t i = 0; i < r->size(); i++) {
          const auto it = std::ranges::find(faceList, (*r)[i]);
          if (it != faceList.end()) {
            CHECK(sharedFaces[i] == static_cast<int>(it - faceList.begin()));
          }
        }
      }
      if (kind == GridKind::Spheres) {
        // balls touch only at the faces: the table lists no edge or
        // corner neighbours
        CHECK(listed(*v, c, 1).empty());
        CHECK(listed(*v, c, 2).empty());
        continue;
      }
      const std::set<CellAt> edge = derived(1);
      const std::vector<CellAt> edgeList = listed(*v, c, 1);
      const std::set<CellAt> edgeListed(edgeList.begin(), edgeList.end());
      INFO("edges by geometry: " << str(edge));
      INFO("edges by table:    " << str(edgeListed));
      CHECK(edgeListed == edge);
      const std::set<CellAt> corner = derived(2);
      const std::vector<CellAt> cornerList = listed(*v, c, 2);
      const std::set<CellAt> cornerListed(cornerList.begin(), cornerList.end());
      INFO("corners by geometry: " << str(corner));
      INFO("corners by table:    " << str(cornerListed));
      if (kind == GridKind::Rhombic || kind == GridKind::TetraOcta) {
        // A known gap, the original's too: the cut-cube grids have no
        // corner-neighbour table (getNeighbor answers false for kind 2),
        // though their cells do touch at corners. A corner-connectivity
        // check on them sees only faces and edges. The geometry here is
        // what such a table would hold.
        CHECK(cornerListed.empty());
        CHECK_FALSE(corner.empty());
      } else {
        CHECK(cornerListed == corner);
      }
      // listed once each
      CHECK(faceList.size() == faceListed.size());
      CHECK(edgeList.size() == edgeListed.size());
      CHECK(cornerList.size() == cornerListed.size());
    }
    CHECK(checked > 0);
  }
}

TEST_CASE("grid oracle: the run of cells around an edge ends at the other exposed face",
          "[grid]") {
  // CellGeometry::run from an exposed face of a filled cell goes through
  // the filled cells sharing the edge to the face on the other side of
  // the run; from that face it comes back; the angles add up the same
  // way, and for a lone cell the sum is the cell's own angle at the edge
  for (const GridKind kind : kPolyGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    std::unique_ptr<Voxel> v = legalShape(gt, kLegalShapeBox, 7);
    REQUIRE(v != nullptr);
    CellGeometry geometry(*v);
    int runs = 0;
    for (const auto& t : validCoordinates(*v, kLegalShapeBox)) {
      const CellAt c = at(t);
      if (v->isEmpty2(c.x, c.y, c.z)) {
        continue;
      }
      const CellPolyhedron* g = geometry.cell(c);
      REQUIRE(g != nullptr);
      for (std::size_t f = 0; f < g->faces.size(); f++) {
        if (!geometry.empty(g->faces[f].across)) {
          continue;
        }
        for (std::size_t e = 0; e < g->faces[f].size(); e++) {
          const std::optional<mesh::EdgeRun> there =
              geometry.run({c, static_cast<int>(f), static_cast<int>(e)});
          REQUIRE(there);
          runs++;
          const CellPolyhedron* h = geometry.cell(there->end.cell);
          REQUIRE(h != nullptr);
          const CellFace& endFace = h->faces[static_cast<std::size_t>(there->end.face)];
          // the end face is exposed and runs the same edge the other way
          CHECK(geometry.empty(endFace.across));
          CHECK(mesh::samePoint(endFace.pos(static_cast<std::size_t>(there->end.edge)),
                                g->faces[f].pos(e + 1)));
          CHECK(mesh::samePoint(endFace.pos(static_cast<std::size_t>(there->end.edge) + 1),
                                g->faces[f].pos(e)));
          const std::optional<mesh::EdgeRun> back = geometry.run(there->end);
          REQUIRE(back);
          CHECK(back->end.cell == c);
          CHECK(back->end.face == static_cast<int>(f));
          CHECK(back->end.edge == static_cast<int>(e));
          CHECK(std::abs(back->angle - there->angle) < 1e-9);
          CHECK(there->angle > 0);
          CHECK(there->angle < 2 * 3.14159265358979);
        }
      }
    }
    CHECK(runs > 0);
  }
}

TEST_CASE("grid oracle: every transformation moves the cells as its matrix does", "[grid]") {
  // transformPoint on a cell and getTransformMatrix on its corners agree
  // up to one translation common to every cell: the transformation is a
  // rigid motion of the grid's geometry, and the matrix table is that
  // motion. On the cut-cube grids (rhombic, tetra-octa) transformPoint
  // turns the coordinates about their origin as cubes', and that origin
  // is half a cell off the grids' symmetry centres (a cube's corner is at
  // coordinate -0.5), so some turns land cells between cells or on cells
  // of another kind: one whole-cell offset per transformation puts them
  // right, and Assembly::transform supplies it (the hotspot's real move
  // minus its transformPoint). The offset is searched for here and must
  // be the same for every cell; on the brick and prism grids it is zero.
  // (The sphere grid rounds most of its 240 transformations to the
  // lattice and is left out, as test_grid_invariants explains.)
  for (const GridKind kind : kPolyGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const int period = periodOf(kind);
    const int box = period * 2;
    const std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box));
    std::vector<CellAt> cells;
    for (const auto& t : validCoordinates(*v, box)) {
      cells.push_back(at(t));
    }
    REQUIRE_FALSE(cells.empty());
    // validCoordinate is periodic: a shift by a multiple of every period
    // (30), beyond the box, brings a transformed cell back to the
    // non-negative range it checks
    const int shift = 30;
    const unsigned int n = gt.getSymmetries().getNumTransformationsMirror();
    // whether a whole-cell offset d makes transformPoint a rigid motion of
    // every cell: the moved cell is a cell, and its corners are the
    // matrix's image of the old corners, all moved by one translation
    const auto rigidWith = [&](unsigned int t, const double m[9], const CellAt& d,
                               std::string* why) {
      bool first = true;
      Vec shiftOf{0, 0, 0};
      for (const CellAt& c : cells) {
        int x = c.x;
        int y = c.y;
        int z = c.z;
        v->transformPoint(&x, &y, &z, t);
        const CellAt moved{x + d.x, y + d.y, z + d.z};
        if (!v->validCoordinate(moved.x + shift, moved.y + shift, moved.z + shift)) {
          *why = "cell " + str(c) + " lands between cells at " + str(moved);
          return false;
        }
        const Result<CellPolyhedron> g = mesh::cellPolyhedron(*v, c);
        const Result<CellPolyhedron> h = mesh::cellPolyhedron(*v, moved);
        if (!g || !h) {
          *why = "cell " + str(c) + " or its image has no polyhedron";
          return false;
        }
        if (g->corners.size() != h->corners.size()) {
          *why = "cell " + str(c) + " lands on another kind of cell";
          return false;
        }
        const Vec s = sub(toVec(h->centroid), rotated(m, toVec(g->centroid)));
        if (first) {
          shiftOf = s;
          first = false;
        } else if (!near(s, shiftOf)) {
          *why = "cell " + str(c) + " is moved by another translation than the first cell";
          return false;
        }
        for (const Vec3& p : g->corners) {
          Vec q = rotated(m, toVec(p));
          for (int i = 0; i < 3; i++) {
            q[static_cast<std::size_t>(i)] += shiftOf[static_cast<std::size_t>(i)];
          }
          if (!h->hasCorner(toVec3(q))) {
            *why = "cell " + str(c) + " lands on a cell with other corners";
            return false;
          }
        }
      }
      return true;
    };
    int offsets = 0;
    for (unsigned int t = 0; t < n; t++) {
      INFO("transformation " << t);
      double m[9];
      v->getTransformMatrix(t, m);
      std::string why;
      bool found = false;
      CellAt offset{0, 0, 0};
      // the zero offset first, then the small ones
      for (int r = 0; r <= 2 && !found; r++) {
        for (int dz = -r; dz <= r && !found; dz++) {
          for (int dy = -r; dy <= r && !found; dy++) {
            for (int dx = -r; dx <= r && !found; dx++) {
              if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) {
                continue;
              }
              std::string reason;
              if (rigidWith(t, m, {dx, dy, dz}, &reason)) {
                found = true;
                offset = {dx, dy, dz};
              } else if (r == 0) {
                why = reason;
              }
            }
          }
        }
      }
      INFO("without an offset: " << why);
      CHECK(found);
      if (offset != CellAt{0, 0, 0}) {
        offsets++;
      }
      const bool cutCube = kind == GridKind::Rhombic || kind == GridKind::TetraOcta;
      if (!cutCube) {
        CHECK(offset == CellAt{0, 0, 0});
      }
    }
    if (kind == GridKind::Rhombic || kind == GridKind::TetraOcta) {
      // the turns that need the offset exist (else the comment above is
      // stale)
      CHECK(offsets > 0);
    }
  }
}

TEST_CASE("grid oracle: the transformation tables form the group of the matrices", "[grid]") {
  // transAdd(t1, t2) is "t1 then t2": its matrix is m2 * m1; every
  // transformation has a matrix of its own, an inverse, and the identity
  // is 0 (the sphere grid's 240 are not a group: transAdd is undefined
  // for some pairs there, which test_symmetries covers).
  for (const GridKind kind : {GridKind::Bricks, GridKind::TriangularPrism}) {
    INFO(toString(kind));
    const GridType gt(kind);
    const Symmetries& sym = gt.getSymmetries();
    const std::unique_ptr<Voxel> v = makeVoxel(gt, 2, 2, 2);
    const unsigned int n = sym.getNumTransformationsMirror();
    std::vector<std::array<double, 9>> mats(n);
    for (unsigned int t = 0; t < n; t++) {
      v->getTransformMatrix(t, mats[t].data());
    }
    const auto same = [](const std::array<double, 9>& a, const std::array<double, 9>& b) {
      for (int i = 0; i < 9; i++) {
        if (std::abs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]) > 1e-6) {
          return false;
        }
      }
      return true;
    };
    for (unsigned int a = 0; a < n; a++) {
      for (unsigned int b = a + 1; b < n; b++) {
        INFO("transformations " << a << " and " << b);
        CHECK_FALSE(same(mats[a], mats[b]));
      }
    }
    // the identity
    for (int i = 0; i < 9; i++) {
      CHECK(std::abs(mats[0][static_cast<std::size_t>(i)] - (i % 4 == 0 ? 1.0 : 0.0)) < 1e-9);
    }
    // the plain rotations first, then the mirrors
    for (unsigned int t = 0; t < n; t++) {
      const auto& m = mats[t];
      const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                         m[2] * (m[3] * m[7] - m[4] * m[6]);
      INFO("transformation " << t);
      CHECK(std::abs(det - (t < sym.getNumTransformations() ? 1.0 : -1.0)) < 1e-6);
    }
    for (unsigned int t1 = 0; t1 < n; t1++) {
      bool inverse = false;
      for (unsigned int t2 = 0; t2 < n; t2++) {
        INFO("transformations " << t1 << " then " << t2);
        const unsigned char r = sym.transAdd(static_cast<unsigned char>(t1),
                                             static_cast<unsigned char>(t2));
        REQUIRE(r != kTransformUndefined);
        REQUIRE(r < n);
        if (r == 0) {
          inverse = true;
        }
        std::array<double, 9> prod{};
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++) {
              s += mats[t2][static_cast<std::size_t>(3 * i + k)] *
                   mats[t1][static_cast<std::size_t>(3 * k + j)];
            }
            prod[static_cast<std::size_t>(3 * i + j)] = s;
          }
        }
        CHECK(same(prod, mats[r]));
      }
      CHECK(inverse);
    }
  }
}

TEST_CASE("grid oracle: the symmetry groups are subgroups, the unique and minimal "
          "transformations their coset representatives",
          "[grid]") {
  // A group's set is closed under transAdd and holds the identity. A
  // shape with group s looks the same under t1 and t2 when t1 then the
  // inverse of t2 is in s; isTransformationUnique picks one
  // transformation per such class, minimizeTransformation the smallest.
  for (const GridKind kind : {GridKind::Bricks, GridKind::TriangularPrism}) {
    INFO(toString(kind));
    const GridType gt(kind);
    const Symmetries& sym = gt.getSymmetries();
    const unsigned int n = sym.getNumTransformationsMirror();
    const unsigned int groups = groupsOf(kind);
    std::vector<unsigned int> inverse(n);
    for (unsigned int t = 0; t < n; t++) {
      for (unsigned int u = 0; u < n; u++) {
        if (sym.transAdd(static_cast<unsigned char>(t), static_cast<unsigned char>(u)) == 0) {
          inverse[t] = u;
        }
      }
    }
    // only the identity group is unsymmetric
    CHECK(unSymmetric(0));
    std::set<std::vector<bool>> seen;
    for (unsigned int s = 0; s < groups; s++) {
      INFO("group " << s);
      std::vector<bool> in(n);
      unsigned int size = 0;
      for (unsigned int t = 0; t < n; t++) {
        in[t] = sym.symmetrieContainsTransformation(static_cast<Symmetry>(s), t);
        size += in[t] ? 1 : 0;
      }
      REQUIRE(in[0]);
      CHECK((size == 1) == (s == 0));
      // each group once
      CHECK(seen.insert(in).second);
      for (unsigned int a = 0; a < n; a++) {
        for (unsigned int b = 0; b < n; b++) {
          if (in[a] && in[b]) {
            CHECK(in[sym.transAdd(static_cast<unsigned char>(a), static_cast<unsigned char>(b))]);
          }
        }
      }
      CHECK(n % size == 0);
      // t1 and t2 give the same look: t1 then inverse(t2) in s
      const auto sameLook = [&](unsigned int t1, unsigned int t2) {
        return in[sym.transAdd(static_cast<unsigned char>(t1),
                               static_cast<unsigned char>(inverse[t2]))];
      };
      unsigned int unique = 0;
      for (unsigned int t = 0; t < n; t++) {
        if (sym.isTransformationUnique(static_cast<Symmetry>(s), t)) {
          unique++;
        }
      }
      CHECK(unique * size == n);
      for (unsigned int t = 0; t < n; t++) {
        INFO("transformation " << t);
        unsigned int representatives = 0;
        unsigned int smallest = n;
        for (unsigned int u = 0; u < n; u++) {
          if (sameLook(t, u)) {
            smallest = std::min(smallest, u);
            if (sym.isTransformationUnique(static_cast<Symmetry>(s), u)) {
              representatives++;
            }
          }
        }
        CHECK(representatives == 1);
        CHECK(sym.minimizeTransformation(static_cast<Symmetry>(s), static_cast<unsigned char>(t)) ==
              smallest);
      }
      // a mirror in the group: any transformation at or above the plain ones
      bool mirror = false;
      for (unsigned int t = sym.getNumTransformations(); t < n; t++) {
        mirror = mirror || in[t];
      }
      CHECK(sym.symmetryContainsMirror(static_cast<Symmetry>(s)) == mirror);
    }
    CHECK(seen.size() == groups);
  }
}

TEST_CASE("grid oracle: a cell's bevel faces close it", "[grid][mesh]") {
  // the bevel faces of the mesh tables (connectionFace with n < 0) and
  // the cell faces together are a closed solid for every kind of cell
  for (const GridKind kind : kPolyGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const int period = periodOf(kind);
    const int box = period * 2;
    const std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box));
    for (const auto& t : validCoordinates(*v, box)) {
      const CellAt c = at(t);
      INFO("cell " << str(c));
      std::unique_ptr<Voxel> one = makeVoxel(gt, static_cast<unsigned int>(box),
                                             static_cast<unsigned int>(box),
                                             static_cast<unsigned int>(box));
      one->setState(static_cast<unsigned int>(c.x), static_cast<unsigned int>(c.y),
                    static_cast<unsigned int>(c.z), Voxel::kFilled);
      const mesh::Polyhedron p = mesh::cellMesh(*one, 0.03, 0, false);
      CHECK(p.numHoles() == 0);
      CHECK(p.numVertices() - p.numHalfEdges() / 2 + p.numFaces() == 2);
    }
  }
}
