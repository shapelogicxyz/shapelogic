// SPDX-License-Identifier: GPL-3.0-or-later
// The sweep test the movement checks are derived from (mesh/cell_sweep.h,
// decision 0025). New: the original has nothing like it. The answers of the
// separating axis test are checked against an independent one: points of a
// cell are inside the swept cell exactly when the line through them along
// the move, clipped by the moved cell's face planes, has a piece left
// between the start and the end of the move.
#include "../grid/grid_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cell_geometry.h"
#include "shapelogic/mesh/cell_sweep.h"
#include "shapelogic/solve/movement_checks.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

using namespace bt;
using mesh::CellAt;
using mesh::CellGeometry;
using mesh::CellPolyhedron;
using mesh::Vec3;

namespace {

using P3 = std::array<double, 3>;

P3 toP3(const Vec3& v) {
  return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}
double dot(const P3& a, const P3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Whether `p` is inside the cell swept along `move`, at least `margin`
// behind every face of the cell at some moment of the move: the moments t
// (0 at the start, 1 at the end) at which p - t * move is inside the cell
// are those every face plane allows, one inequality in t per face.
bool insideSweep(const CellPolyhedron& cell, const P3& move, const P3& p, double margin) {
  double lo = 0;
  double hi = 1;
  for (const mesh::CellFace& f : cell.faces) {
    const P3 n = toP3(f.normal);
    const P3 a = toP3(f.corners[0]);
    // n . (p - t move - a) <= -margin
    const double c = dot(n, {p[0] - a[0], p[1] - a[1], p[2] - a[2]}) + margin;
    const double k = dot(n, move);
    if (std::abs(k) < 1e-12) {
      if (c > 0) {
        return false;
      }
    } else if (k > 0) {
      lo = std::max(lo, c / k);
    } else {
      hi = std::min(hi, c / k);
    }
  }
  return lo <= hi;
}

// Points inside a cell: its centroid, and weighted means of its corners
// (some of all of them, some of a few: near the faces, edges and corners),
// each pulled a little towards the centroid so that none lies on the hull.
std::vector<P3> pointsInside(const CellPolyhedron& cell, std::mt19937& rng, unsigned int count) {
  const P3 centre = toP3(cell.centroid);
  std::vector<P3> out{centre};
  out.reserve(count + 1);
  std::exponential_distribution<double> weight(1.0);
  std::uniform_int_distribution<std::size_t> pick(0, cell.corners.size() - 1);
  std::vector<double> w(cell.corners.size());
  for (unsigned int i = 0; i < count; i++) {
    std::ranges::fill(w, 0.0);
    if (i % 2 == 0) {
      for (double& v : w) {
        v = weight(rng);
      }
    } else {
      // two to four corners
      for (unsigned int k = 0; k < 2 + (i / 2) % 3; k++) {
        w[pick(rng)] += weight(rng);
      }
    }
    double sum = 0;
    P3 p{0, 0, 0};
    for (std::size_t c = 0; c < w.size(); c++) {
      const P3 corner = toP3(cell.corners[c]);
      sum += w[c];
      for (std::size_t a = 0; a < 3; a++) {
        p[a] += w[c] * corner[a];
      }
    }
    for (std::size_t a = 0; a < 3; a++) {
      p[a] = centre[a] + 0.995 * (p[a] / sum - centre[a]);
    }
    out.push_back(p);
  }
  return out;
}

// Whether the box around the swept cell and the box around the other cell
// are apart along an axis.
bool boxesApart(const CellPolyhedron& cell, const P3& move, const CellPolyhedron& other) {
  for (std::size_t a = 0; a < 3; a++) {
    double lo = 1e30;
    double hi = -1e30;
    for (const Vec3& corner : cell.corners) {
      const double v = toP3(corner)[a];
      lo = std::min({lo, v, v + move[a]});
      hi = std::max({hi, v, v + move[a]});
    }
    double olo = 1e30;
    double ohi = -1e30;
    for (const Vec3& corner : other.corners) {
      olo = std::min(olo, toP3(corner)[a]);
      ohi = std::max(ohi, toP3(corner)[a]);
    }
    if (olo > hi + 1e-3 || ohi < lo - 1e-3) {
      return true;
    }
  }
  return false;
}

// A cell far enough from the origin that every coordinate near it is not
// negative (the rhombic grid's validCoordinate takes none).
constexpr int kAway = 30;

P3 cross(const P3& a, const P3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
P3 minus(const P3& a, const P3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

using Rotation = std::array<P3, 3>;  // the rows of the matrix

constexpr Rotation kNoTurn{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

P3 turned(const Rotation& r, const P3& p) { return {dot(r[0], p), dot(r[1], p), dot(r[2], p)}; }

// A rotation picked evenly among all of them (from a random unit quaternion).
Rotation randomRotation(std::mt19937& rng) {
  std::normal_distribution<double> normal(0.0, 1.0);
  double w = normal(rng);
  double x = normal(rng);
  double y = normal(rng);
  double z = normal(rng);
  const double l = std::sqrt(w * w + x * x + y * y + z * z);
  w /= l;
  x /= l;
  y /= l;
  z /= l;
  return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}

// The cell turned about its centroid, with the centroid put at `centre`.
CellPolyhedron placed(const CellPolyhedron& p, const Rotation& r, const P3& centre) {
  const P3 from = toP3(p.centroid);
  const auto point = [&](const Vec3& v) {
    const P3 q = turned(r, minus(toP3(v), from));
    return Vec3{static_cast<float>(q[0] + centre[0]), static_cast<float>(q[1] + centre[1]),
                static_cast<float>(q[2] + centre[2])};
  };
  CellPolyhedron out;
  for (const mesh::CellFace& f : p.faces) {
    mesh::CellFace face;
    for (const Vec3& c : f.corners) {
      face.corners.push_back(point(c));
    }
    const P3 n = turned(r, toP3(f.normal));
    face.normal =
        Vec3{static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])};
    face.across = f.across;
    out.faces.push_back(std::move(face));
  }
  for (const Vec3& c : p.corners) {
    out.corners.push_back(point(c));
  }
  out.centroid = point(p.centroid);
  return out;
}

// The distance of the farthest corner from the centroid.
double radiusOf(const CellPolyhedron& p) {
  double r = 0;
  for (const Vec3& c : p.corners) {
    const P3 d = minus(toP3(c), toP3(p.centroid));
    r = std::max(r, std::sqrt(dot(d, d)));
  }
  return r;
}

// A half space: the points x with n . x <= h.
struct Plane {
  P3 n;
  double h;
};

// The convex hull of points as its face planes, the slow and plain way:
// three points span a face plane when every other point is on one side.
std::vector<Plane> hullPlanes(const std::vector<P3>& points) {
  constexpr double kOn = 1e-6;
  std::vector<Plane> out;
  for (std::size_t i = 0; i < points.size(); i++) {
    for (std::size_t j = i + 1; j < points.size(); j++) {
      for (std::size_t k = j + 1; k < points.size(); k++) {
        P3 n = cross(minus(points[j], points[i]), minus(points[k], points[i]));
        const double l = std::sqrt(dot(n, n));
        if (l < 1e-6) {
          continue;  // on a line
        }
        n = {n[0] / l, n[1] / l, n[2] / l};
        const double h = dot(n, points[i]);

        bool above = false;
        bool below = false;
        for (const P3& p : points) {
          const double v = dot(n, p) - h;
          above = above || v > kOn;
          below = below || v < -kOn;
          if (above && below) {
            break;
          }
        }
        if (above && below) {
          continue;
        }
        const Plane plane = above ? Plane{{-n[0], -n[1], -n[2]}, -h} : Plane{n, h};
        const bool known = std::ranges::any_of(out, [&](const Plane& o) {
          return dot(o.n, plane.n) > 1 - 1e-9 && std::abs(o.h - plane.h) < 1e-5;
        });
        if (!known) {
          out.push_back(plane);
        }
      }
    }
  }
  return out;
}

// Whether some point from + t * move, t from 0 to 1, lies at least `margin`
// behind every plane (a negative margin: no more than that much before
// one).
bool segmentInside(const std::vector<Plane>& planes, const P3& from, const P3& move,
                   double margin) {
  double lo = 0;
  double hi = 1;
  for (const Plane& pl : planes) {
    // n . (from + t move) <= h - margin
    const double c = pl.h - margin - dot(pl.n, from);
    const double k = dot(pl.n, move);
    if (std::abs(k) < 1e-12) {
      if (c < 0) {
        return false;
      }
    } else if (k > 0) {
      hi = std::min(hi, c / k);
    } else {
      lo = std::max(lo, c / k);
    }
  }
  return lo <= hi;
}

// Cells of every grid with polyhedra that take apart, up to three of each
// (the first of its period box: of different kinds where the grid has
// them), each at the origin.
std::vector<CellPolyhedron> cellsOfAllGrids() {
  std::vector<CellPolyhedron> out;
  for (const GridKind kind : {GridKind::Bricks, GridKind::TriangularPrism, GridKind::Rhombic,
                              GridKind::TetraOcta, GridKind::Hexagonal, GridKind::Cairo}) {
    const std::optional<MovementDirections> moves = movementDirections(kind);
    REQUIRE(moves.has_value());
    const std::array<int, 3> period = moves->period;
    const GridType gt(kind);
    const std::unique_ptr<Voxel> v = test::makeVoxel(gt, 1, 1, 1);
    unsigned int taken = 0;
    for (int kz = 0; kz < period[2] && taken < 3; kz++) {
      for (int ky = 0; ky < period[1] && taken < 3; ky++) {
        for (int kx = 0; kx < period[0] && taken < 3; kx++) {
          const CellAt c{kAway * period[0] + kx, kAway * period[1] + ky, kAway * period[2] + kz};
          if (!v->validCoordinate(c.x, c.y, c.z)) {
            continue;
          }
          const Result<CellPolyhedron> p = mesh::cellPolyhedron(*v, c);
          REQUIRE(p.has_value());
          out.push_back(placed(*p, kNoTurn, {0, 0, 0}));
          taken++;
        }
      }
    }
  }
  return out;
}

}  // namespace

TEST_CASE("cell sweep: a cube moved along an axis, a face diagonal and the space diagonal",
          "[mesh][sweep]") {
  const GridType gt(GridKind::Bricks);
  const std::unique_ptr<Voxel> v = test::makeVoxel(gt, 1, 1, 1);
  CellGeometry g(*v);
  const CellAt c{kAway, kAway, kAway};
  const CellAt reach{2, 2, 2};

  // along an axis: only the cell it ends in
  Result<std::vector<CellAt>> r = mesh::sweptCells(g, c, {1, 0, 0}, reach);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<CellAt>{{1, 0, 0}});

  r = mesh::sweptCells(g, c, {0, 0, -1}, reach);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<CellAt>{{0, 0, -1}});

  // two cells along an axis: both
  r = mesh::sweptCells(g, c, {0, 2, 0}, reach);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<CellAt>{{0, 1, 0}, {0, 2, 0}});

  // along a face diagonal the cube cuts the corners of the two cells beside
  // the way; the cells above and below only touch it
  r = mesh::sweptCells(g, c, {1, 1, 0}, reach);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<CellAt>{{0, 1, 0}, {1, 0, 0}, {1, 1, 0}});

  // along the space diagonal: the seven other cells of the 2x2x2 block
  r = mesh::sweptCells(g, c, {1, 1, 1}, reach);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<CellAt>{{0, 0, 1}, {0, 1, 0}, {0, 1, 1}, {1, 0, 0},
                                  {1, 0, 1}, {1, 1, 0}, {1, 1, 1}});
}

TEST_CASE("cell sweep: a cell that touches the swept cell is not entered", "[mesh][sweep]") {
  const GridType gt(GridKind::Bricks);
  const std::unique_ptr<Voxel> v = test::makeVoxel(gt, 1, 1, 1);
  const Result<CellPolyhedron> cell = mesh::cellPolyhedron(*v, {0, 0, 0});
  REQUIRE(cell.has_value());
  const Vec3 edge = cell->faces[0].pos(1) - cell->faces[0].pos(0);
  const float size = static_cast<float>(std::sqrt(edge.squaredModule()));

  const auto at = [&](int x, int y, int z) {
    Result<CellPolyhedron> q = mesh::cellPolyhedron(*v, {x, y, z});
    REQUIRE(q.has_value());
    return std::move(*q);
  };

  // sliding along x: the cells beside the way share a face with the sweep,
  // those across an edge of it share an edge
  const Vec3 move{size, 0, 0};
  CHECK(mesh::sweepEnters(*cell, move, at(1, 0, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, move, at(0, 1, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, move, at(1, 1, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, move, at(1, 1, 1)));
  CHECK_FALSE(mesh::sweepEnters(*cell, move, at(2, 0, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, move, at(-1, 0, 0)));

  // no move at all: only the cell itself
  CHECK(mesh::sweepEnters(*cell, Vec3{0, 0, 0}, at(0, 0, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, Vec3{0, 0, 0}, at(1, 0, 0)));

  // a move of a hair into the neighbour is one into it; a move away is not
  CHECK(mesh::sweepEnters(*cell, Vec3{size * 0.01F, 0, 0}, at(1, 0, 0)));
  CHECK_FALSE(mesh::sweepEnters(*cell, Vec3{-size * 0.01F, 0, 0}, at(1, 0, 0)));
}

TEST_CASE("cell sweep: steps that are no move of the grid are refused", "[mesh][sweep]") {
  {
    // a triangle moved one cell along x lands on a triangle that points
    // the other way
    const GridType gt(GridKind::TriangularPrism);
    const std::unique_ptr<Voxel> v = test::makeVoxel(gt, 2, 2, 1);
    CellGeometry g(*v);
    const Result<std::vector<CellAt>> r =
        mesh::sweptCells(g, {kAway, kAway, kAway}, {1, 0, 0}, {2, 2, 2});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(mesh::sweptCells(g, {kAway, kAway, kAway}, {2, 0, 0}, {2, 2, 2}).has_value());
  }
  {
    // positions that are no cell of the rhombic grid
    const GridType gt(GridKind::Rhombic);
    const std::unique_ptr<Voxel> v = test::makeVoxel(gt, 5, 5, 5);
    REQUIRE_FALSE(v->validCoordinate(kAway, kAway, kAway));
    CellGeometry g(*v);
    const Result<std::vector<CellAt>> r =
        mesh::sweptCells(g, {kAway, kAway, kAway}, {5, 0, 0}, {5, 5, 5});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
  }
}

// The separating axis test against the point test, for every kind of cell
// of a grid, every direction pieces move in on it (both ways) and every
// cell near the move.
static void compareWithPoints(GridKind kind, unsigned int points) {
  INFO("grid " << toString(kind));
  const std::optional<MovementDirections> moves = movementDirections(kind);
  REQUIRE(moves.has_value());
  const std::array<int, 3> period = moves->period;

  const GridType gt(kind);
  const std::unique_ptr<Voxel> v =
      test::makeVoxel(gt, static_cast<unsigned int>(period[0]),
                      static_cast<unsigned int>(period[1]), static_cast<unsigned int>(period[2]));
  CellGeometry g(*v);
  std::mt19937 rng(20261109);

  // whole periods away from the origin
  std::array<int, 3> base{};
  for (std::size_t a = 0; a < 3; a++) {
    base[a] = period[a] * ((kAway + period[a] - 1) / period[a]);
  }

  // the points of each cell near the moves, made once
  std::map<CellAt, std::vector<P3>> samples;

  std::size_t entered = 0;
  std::size_t touching = 0;
  std::size_t far = 0;
  for (int kz = 0; kz < period[2]; kz++) {
    for (int ky = 0; ky < period[1]; ky++) {
      for (int kx = 0; kx < period[0]; kx++) {
        const CellAt c{base[0] + kx, base[1] + ky, base[2] + kz};
        if (!v->validCoordinate(c.x, c.y, c.z)) {
          continue;
        }
        const CellPolyhedron* cell = g.cell(c);
        REQUIRE(cell != nullptr);
        const std::vector<P3> ownPoints = pointsInside(*cell, rng, points);

        for (const std::array<int, 3>& dir : moves->directions) {
          for (const int sign : {1, -1}) {
            const CellAt step{sign * dir[0], sign * dir[1], sign * dir[2]};
            const CellPolyhedron* end = g.cell({c.x + step.x, c.y + step.y, c.z + step.z});
            REQUIRE(end != nullptr);
            const Vec3 moveF = end->centroid - cell->centroid;
            const P3 move = toP3(moveF);

            const int rx = std::max(2, period[0]);
            const int ry = std::max(2, period[1]);
            const int rz = std::max(2, period[2]);
            for (int x = c.x + std::min(0, step.x) - rx; x <= c.x + std::max(0, step.x) + rx; x++) {
              for (int y = c.y + std::min(0, step.y) - ry; y <= c.y + std::max(0, step.y) + ry;
                   y++) {
                for (int z = c.z + std::min(0, step.z) - rz; z <= c.z + std::max(0, step.z) + rz;
                     z++) {
                  const CellAt q{x, y, z};
                  if (q == c || !v->validCoordinate(x, y, z)) {
                    continue;
                  }
                  const CellPolyhedron* other = g.cell(q);
                  if (other == nullptr) {
                    FAIL("no polyhedron");
                  }
                  const bool enters = mesh::sweepEnters(*cell, moveF, *other);

                  // most cells are nowhere near: no points needed
                  if (boxesApart(*cell, move, *other)) {
                    far++;
                    if (enters) {
                      INFO("cell " << c.x << "," << c.y << "," << c.z << " step " << step.x << ","
                                   << step.y << "," << step.z << " other " << q.x << "," << q.y
                                   << "," << q.z);
                      CHECK_FALSE(enters);
                    }
                    continue;
                  }

                  // points of the other cell that are inside the sweep
                  bool deep = false;
                  auto found = samples.find(q);
                  if (found == samples.end()) {
                    found = samples.emplace(q, pointsInside(*other, rng, points)).first;
                  }
                  for (const P3& p : found->second) {
                    if (insideSweep(*cell, move, p, 1e-3)) {
                      deep = true;
                      break;
                    }
                  }
                  // and points of the moved cell that get inside the other
                  // cell on the way (the sweep of the other cell backwards)
                  if (!deep && enters) {
                    const P3 back{-move[0], -move[1], -move[2]};
                    for (const P3& p : ownPoints) {
                      // p + t move inside other <=> p inside other swept by -move
                      if (insideSweep(*other, back, p, 1e-3)) {
                        deep = true;
                        break;
                      }
                    }
                  }

                  if (enters != deep) {
                    INFO("cell " << c.x << "," << c.y << "," << c.z << " step " << step.x << ","
                                 << step.y << "," << step.z << " other " << q.x << "," << q.y
                                 << "," << q.z);
                    CHECK(enters == deep);
                  }
                  if (enters) {
                    entered++;
                  } else {
                    touching++;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  // the comparison saw every answer: cells entered, cells that only touch
  // the sweep, and cells away from it
  CHECK(entered > 0);
  CHECK(touching > 0);
  CHECK(far > 0);
}

TEST_CASE("cell sweep: the separating axis test agrees with sampled points", "[mesh][sweep]") {
  compareWithPoints(GridKind::Bricks, 400);
  compareWithPoints(GridKind::TriangularPrism, 400);
  compareWithPoints(GridKind::Hexagonal, 400);
  compareWithPoints(GridKind::Cairo, 400);
}

TEST_CASE("cell sweep: the separating axis test agrees with sampled points, tetra-octa grid",
          "[mesh][sweep][slow]") {
  compareWithPoints(GridKind::TetraOcta, 200);
}

TEST_CASE("cell sweep: the separating axis test agrees with sampled points, rhombic grid",
          "[mesh][sweep][slow]") {
  compareWithPoints(GridKind::Rhombic, 200);
}

// On a grid the moves are few and the cells share their edge directions,
// which leaves parts of the test idle: the face planes are then among the
// planes along two edges, and no cell is entered only at the end of a
// move. Here a cell of any grid moves any way past a cell of any grid
// turned any way. The answer to compare with: the moved cell meets the
// other at moment t exactly when t * move, seen from the other's place,
// is a difference of a point of the other and a point of the cell, and
// those differences fill the convex hull of the corners' differences.
TEST_CASE("cell sweep: any move past a cell turned any way", "[mesh][sweep]") {
  const std::vector<CellPolyhedron> cells = cellsOfAllGrids();
  // one cube, one hexagon, three of each other grid
  REQUIRE(cells.size() == 14);

  {
    // the hull itself: two unit cubes differ by a cube of twice the size
    const std::vector<Plane> cube = hullPlanes([&] {
      std::vector<P3> points;
      for (const Vec3& b : cells[0].corners) {
        for (const Vec3& a : cells[0].corners) {
          points.push_back(minus(toP3(b), toP3(a)));
        }
      }
      return points;
    }());
    REQUIRE(cube.size() == 6);
    const double side = 2 * radiusOf(cells[0]) / std::sqrt(3.0);
    for (const Plane& pl : cube) {
      CHECK(std::abs(pl.h - side) < 1e-5);
    }
  }

  std::mt19937 rng(4);
  std::uniform_int_distribution<std::size_t> pick(0, cells.size() - 1);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::normal_distribution<double> normal(0.0, 1.0);
  const auto direction = [&] {
    P3 d{normal(rng), normal(rng), normal(rng)};
    const double l = std::sqrt(dot(d, d));
    return P3{d[0] / l, d[1] / l, d[2] / l};
  };

  std::size_t entered = 0;
  std::size_t missed = 0;
  std::size_t grazing = 0;
  for (unsigned int pair = 0; pair < 80; pair++) {
    const CellPolyhedron& cell = cells[pick(rng)];
    // every fourth as the grid has it: edges and faces parallel to the
    // moved cell's where the two are of one grid
    const Rotation turn = pair % 4 == 0 ? kNoTurn : randomRotation(rng);
    const CellPolyhedron other = placed(cells[pick(rng)], turn, {0, 0, 0});

    std::vector<P3> differences;
    for (const Vec3& b : other.corners) {
      for (const Vec3& a : cell.corners) {
        differences.push_back(minus(toP3(b), toP3(a)));
      }
    }
    const std::vector<Plane> hull = hullPlanes(differences);
    REQUIRE(hull.size() >= 4);

    const double ra = radiusOf(cell);
    const double rb = radiusOf(other);
    for (unsigned int n = 0; n < 150; n++) {
      // moves from none to two cells long; the other cell somewhere
      // around the way, as often clear of it as in it
      const double length = n % 10 == 0 ? 0.0 : 4 * ra * unit(rng);
      const P3 d = direction();
      const P3 move{d[0] * length, d[1] * length, d[2] * length};
      const P3 off = direction();
      const double along = unit(rng);
      const double away = (ra + rb) * 1.3 * unit(rng);
      const P3 where{move[0] * along + off[0] * away, move[1] * along + off[1] * away,
                     move[2] * along + off[2] * away};
      const CellPolyhedron there = placed(other, kNoTurn, where);
      // the place as the floats have it
      const P3 at = toP3(there.centroid);
      const Vec3 moveF{static_cast<float>(move[0]), static_cast<float>(move[1]),
                       static_cast<float>(move[2])};
      const P3 moved = toP3(moveF);
      const P3 from{-at[0], -at[1], -at[2]};

      const bool enters = mesh::sweepEnters(cell, moveF, there);
      if (segmentInside(hull, from, moved, 1e-3)) {
        entered++;
        if (!enters) {
          INFO("pair " << pair << " placement " << n);
          CHECK(enters);
        }
      } else if (!segmentInside(hull, from, moved, -1e-5)) {
        missed++;
        if (enters) {
          INFO("pair " << pair << " placement " << n);
          CHECK_FALSE(enters);
        }
      } else {
        // within a thousandth of touching: either answer
        grazing++;
      }
    }
  }
  CHECK(entered > 2000);
  CHECK(missed > 2000);
  CHECK(grazing < 200);
}
