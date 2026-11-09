// SPDX-License-Identifier: GPL-3.0-or-later
// The grid tables rebuilt from geometry (docs/V2-IDEAS-PLAN.md, idea 2):
// the hand-written neighbour tables of every grid against what the cells'
// face polygons say, the transformations against the cells' geometry, and
// the generated symmetry tables against the group they describe.
// BurrTools V2 derives all of this at start-up from each grid's polyhedra
// (its src/lib/grid.cpp); here the same derivation is the oracle and the
// ported tables are what it checks. New (the original had no such test).
#include "grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/grid_geometry.h"
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

namespace {

using Coord = std::tuple<int, int, int>;
using Vec = std::array<double, 3>;

// Positions this close (mesh units) are one point: the corners come from
// float tables, exactly equal between neighbouring cells.
constexpr double kNear = 1e-4;

bool near(const Vec& a, const Vec& b) {
  return std::abs(a[0] - b[0]) < kNear && std::abs(a[1] - b[1]) < kNear &&
         std::abs(a[2] - b[2]) < kNear;
}

std::string str(const Coord& c) {
  std::ostringstream s;
  s << "(" << std::get<0>(c) << ", " << std::get<1>(c) << ", " << std::get<2>(c) << ")";
  return s.str();
}

std::string str(const std::set<Coord>& cs) {
  std::string s;
  for (const Coord& c : cs) {
    s += str(c) + " ";
  }
  return s;
}

// The geometry of one cell: its face polygons (corners counter clockwise
// seen from outside) and its distinct corners, from connectionFace alone.
struct CellGeo {
  std::vector<std::vector<Vec>> faces;
  std::vector<Vec> corners;

  [[nodiscard]] bool hasCorner(const Vec& p) const {
    return std::ranges::any_of(corners, [&](const Vec& c) { return near(c, p); });
  }
  // the index of the face with exactly these corners (any order), or -1
  [[nodiscard]] int faceOf(const std::vector<Vec>& poly) const {
    for (std::size_t f = 0; f < faces.size(); f++) {
      if (faces[f].size() != poly.size()) {
        continue;
      }
      const bool all = std::ranges::all_of(poly, [&](const Vec& p) {
        return std::ranges::any_of(faces[f], [&](const Vec& q) { return near(p, q); });
      });
      if (all) {
        return static_cast<int>(f);
      }
    }
    return -1;
  }
  [[nodiscard]] Vec centroid() const {
    Vec c{0, 0, 0};
    for (const Vec& p : corners) {
      for (int i = 0; i < 3; i++) {
        c[static_cast<std::size_t>(i)] += p[static_cast<std::size_t>(i)];
      }
    }
    for (double& x : c) {
      x /= static_cast<double>(corners.size());
    }
    return c;
  }
};

// The cell's faces: connectionFace for n = 0, 1, ... until it gives none
// (independent of getNeighbor, whose count it checks).
CellGeo geometryOf(const Voxel& v, const Coord& c) {
  CellGeo g;
  std::vector<float> corners;
  for (int n = 0; n < 64; n++) {
    corners.clear();
    mesh::connectionFace(v, std::get<0>(c), std::get<1>(c), std::get<2>(c), n, 0, 0, corners);
    if (corners.empty()) {
      break;
    }
    std::vector<Vec> poly;
    for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
      const Vec p{static_cast<double>(corners[i]), static_cast<double>(corners[i + 1]),
                  static_cast<double>(corners[i + 2])};
      poly.push_back(p);
      if (!g.hasCorner(p)) {
        g.corners.push_back(p);
      }
    }
    g.faces.push_back(std::move(poly));
  }
  return g;
}

Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec& a, const Vec& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double length(const Vec& a) { return std::sqrt(dot(a, a)); }

// m (row major) times p.
Vec rotated(const double m[9], const Vec& p) {
  return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2], m[3] * p[0] + m[4] * p[1] + m[5] * p[2],
          m[6] * p[0] + m[7] * p[1] + m[8] * p[2]};
}

// The neighbours of a kind that getNeighbor lists.
std::vector<Coord> listed(const Voxel& v, const Coord& c, unsigned int typ) {
  std::vector<Coord> out;
  int xn = 0;
  int yn = 0;
  int zn = 0;
  for (unsigned int n = 0;
       v.getNeighbor(n, typ, std::get<0>(c), std::get<1>(c), std::get<2>(c), &xn, &yn, &zn);
       n++) {
    out.emplace_back(xn, yn, zn);
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
    for (const Coord& c : validCoordinates(*v, box)) {
      INFO("cell " << str(c));
      const CellGeo g = geometryOf(*v, c);
      REQUIRE(g.faces.size() >= 4);
      cells++;
      // as many faces as face neighbours
      CHECK(g.faces.size() == listed(*v, c, 0).size());
      const Vec centre = g.centroid();
      // every directed edge has exactly one reverse (a closed surface);
      // each face looks away from the centre and has every corner of the
      // cell behind it (convex)
      std::map<std::pair<int, int>, int> edges;
      const auto cornerIndex = [&](const Vec& p) {
        for (std::size_t i = 0; i < g.corners.size(); i++) {
          if (near(g.corners[i], p)) {
            return static_cast<int>(i);
          }
        }
        return -1;
      };
      for (const std::vector<Vec>& f : g.faces) {
        REQUIRE(f.size() >= 3);
        Vec n = cross(sub(f[1], f[0]), sub(f[2], f[0]));
        const double l = length(n);
        REQUIRE(l > 1e-9);
        for (double& x : n) {
          x /= l;
        }
        CHECK(dot(n, sub(f[0], centre)) > 1e-6);
        for (const Vec& p : g.corners) {
          CHECK(dot(n, sub(p, f[0])) < kNear);
        }
        for (std::size_t i = 0; i < f.size(); i++) {
          const int a = cornerIndex(f[i]);
          const int b = cornerIndex(f[(i + 1) % f.size()]);
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
  // cells share: a whole face (face neighbour), two or more corners
  // (edge), one corner (corner). The box is big enough for every
  // neighbour of its middle cells to lie inside.
  constexpr int kReach = 7;
  for (const GridKind kind : kGeometryGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const int period = periodOf(kind);
    const int box = 2 * kReach + period;
    const std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box),
                                               static_cast<unsigned int>(box));
    std::map<Coord, CellGeo> geo;
    for (const Coord& c : validCoordinates(*v, box)) {
      geo.emplace(c, geometryOf(*v, c));
    }
    int checked = 0;
    for (const auto& [c, g] : geo) {
      const auto [x, y, z] = c;
      if (x < kReach || y < kReach || z < kReach || x >= kReach + period ||
          y >= kReach + period || z >= kReach + period) {
        continue;
      }
      INFO("cell " << str(c));
      checked++;
      std::set<Coord> face;
      std::set<Coord> edge;
      std::set<Coord> corner;
      std::map<Coord, int> faceOf;  // the face of c towards each face neighbour
      for (int dz = -kReach; dz <= kReach; dz++) {
        for (int dy = -kReach; dy <= kReach; dy++) {
          for (int dx = -kReach; dx <= kReach; dx++) {
            if (dx == 0 && dy == 0 && dz == 0) {
              continue;
            }
            const Coord d{x + dx, y + dy, z + dz};
            const auto it = geo.find(d);
            if (it == geo.end()) {
              continue;
            }
            const CellGeo& h = it->second;
            int shared = 0;
            for (const Vec& p : g.corners) {
              if (h.hasCorner(p)) {
                shared++;
              }
            }
            if (shared == 0) {
              continue;
            }
            int sharedFace = -1;
            for (std::size_t f = 0; f < g.faces.size(); f++) {
              if (h.faceOf(g.faces[f]) >= 0) {
                sharedFace = static_cast<int>(f);
              }
            }
            if (sharedFace >= 0) {
              face.insert(d);
              faceOf[d] = sharedFace;
            } else if (shared >= 2) {
              edge.insert(d);
            } else {
              corner.insert(d);
            }
          }
        }
      }
      const std::vector<Coord> faceList = listed(*v, c, 0);
      const std::set<Coord> faceListed(faceList.begin(), faceList.end());
      INFO("faces by geometry: " << str(face));
      INFO("faces by table:    " << str(faceListed));
      CHECK(faceListed == face);
      // face n of the table is the face shared with neighbour n
      for (std::size_t n = 0; n < faceList.size(); n++) {
        const auto it = faceOf.find(faceList[n]);
        if (it != faceOf.end()) {
          CHECK(it->second == static_cast<int>(n));
        }
      }
      if (kind == GridKind::Spheres) {
        // balls touch only at the faces: the table lists no edge or
        // corner neighbours
        CHECK(listed(*v, c, 1).empty());
        CHECK(listed(*v, c, 2).empty());
        continue;
      }
      const std::vector<Coord> edgeList = listed(*v, c, 1);
      const std::set<Coord> edgeListed(edgeList.begin(), edgeList.end());
      INFO("edges by geometry: " << str(edge));
      INFO("edges by table:    " << str(edgeListed));
      CHECK(edgeListed == edge);
      const std::vector<Coord> cornerList = listed(*v, c, 2);
      const std::set<Coord> cornerListed(cornerList.begin(), cornerList.end());
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
    const std::vector<Coord> cells = validCoordinates(*v, box);
    REQUIRE_FALSE(cells.empty());
    // validCoordinate is periodic: a shift by a multiple of every period
    // (30), beyond the box, brings a transformed cell back to the
    // non-negative range it checks
    const int shift = 30;
    const unsigned int n = gt.getSymmetries().getNumTransformationsMirror();
    // whether a whole-cell offset d makes transformPoint a rigid motion of
    // every cell: the moved cell is a cell, and its corners are the
    // matrix's image of the old corners, all moved by one translation
    const auto rigidWith = [&](unsigned int t, const double m[9], const Coord& d,
                               std::string* why) {
      bool first = true;
      Vec shiftOf{0, 0, 0};
      for (const Coord& c : cells) {
        int x = std::get<0>(c);
        int y = std::get<1>(c);
        int z = std::get<2>(c);
        v->transformPoint(&x, &y, &z, t);
        x += std::get<0>(d);
        y += std::get<1>(d);
        z += std::get<2>(d);
        if (!v->validCoordinate(x + shift, y + shift, z + shift)) {
          *why = "cell " + str(c) + " lands between cells at " + str({x, y, z});
          return false;
        }
        const CellGeo g = geometryOf(*v, c);
        const CellGeo h = geometryOf(*v, {x, y, z});
        if (g.corners.size() != h.corners.size()) {
          *why = "cell " + str(c) + " lands on another kind of cell";
          return false;
        }
        const Vec s = sub(h.centroid(), rotated(m, g.centroid()));
        if (first) {
          shiftOf = s;
          first = false;
        } else if (!near(s, shiftOf)) {
          *why = "cell " + str(c) + " is moved by another translation than the first cell";
          return false;
        }
        for (const Vec& p : g.corners) {
          Vec q = rotated(m, p);
          for (int i = 0; i < 3; i++) {
            q[static_cast<std::size_t>(i)] += shiftOf[static_cast<std::size_t>(i)];
          }
          if (!h.hasCorner(q)) {
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
      Coord offset{0, 0, 0};
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
      if (offset != Coord{0, 0, 0}) {
        offsets++;
      }
      const bool cutCube = kind == GridKind::Rhombic || kind == GridKind::TetraOcta;
      if (!cutCube) {
        CHECK(offset == Coord{0, 0, 0});
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
    for (const Coord& c : validCoordinates(*v, box)) {
      INFO("cell " << str(c));
      std::unique_ptr<Voxel> one = makeVoxel(gt, static_cast<unsigned int>(box),
                                             static_cast<unsigned int>(box),
                                             static_cast<unsigned int>(box));
      one->setState(static_cast<unsigned int>(std::get<0>(c)),
                    static_cast<unsigned int>(std::get<1>(c)),
                    static_cast<unsigned int>(std::get<2>(c)), Voxel::kFilled);
      const mesh::Polyhedron p = mesh::cellMesh(*one, 0.03, 0, false);
      CHECK(p.numHoles() == 0);
      CHECK(p.numVertices() - p.numHalfEdges() / 2 + p.numFaces() == 2);
    }
  }
}
