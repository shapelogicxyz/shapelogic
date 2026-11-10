// SPDX-License-Identifier: GPL-3.0-or-later
// The movement checks derived from the cells (decision 0025) and the
// movement cache that walks them: against the original's hand-made tables
// where it has them (cubes and triangular prisms), and against the cells
// themselves (movement_test_helpers.h) on every grid, and against the
// tables BurrTools V2 computes for its rhombic and octahedral grids
// (tests/data/movement_checks_by_v2.txt). New: the original has no such
// tables to port tests of.
#include "movement_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/movement_checks.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace bt;

namespace {

using Offsets = std::set<std::array<int, 3>>;

const std::array<GridKind, 6> kGrids = {GridKind::Bricks,    GridKind::TriangularPrism,
                                        GridKind::Rhombic,   GridKind::TetraOcta,
                                        GridKind::Hexagonal, GridKind::Cairo};

const char* nameOf(GridKind kind) {
  switch (kind) {
    case GridKind::Bricks:
      return "cubes";
    case GridKind::TriangularPrism:
      return "triangular prisms";
    case GridKind::Rhombic:
      return "rhombic";
    case GridKind::TetraOcta:
      return "tetra-octa";
    case GridKind::Hexagonal:
      return "hexagonal";
    case GridKind::Cairo:
      return "cairo";
    default:
      return "?";
  }
}

Offsets asSet(const std::vector<std::array<int, 3>>& cells) {
  return {cells.begin(), cells.end()};
}

// A puzzle of `count` random shapes (each cell of a box filled with one
// chance in `oneIn`), every shape a piece of its one problem.
Puzzle randomShapes(GridKind kind, std::array<int, 3> box, unsigned int oneIn, unsigned int count,
                    std::mt19937& rng) {
  Puzzle puz{GridType{kind}};
  const unsigned int prob = puz.addProblem();
  const std::unique_ptr<Voxel> grid = GridType(kind).makeVoxel(1, 1, 1);
  const test::Cells all = test::cellsInBox(*grid, box[0], box[1], box[2]);
  for (unsigned int i = 0; i < count; i++) {
    test::Cells cells;
    while (cells.size() < 2) {
      cells.clear();
      for (const mesh::CellAt& c : all) {
        if (rng() % oneIn == 0) {
          cells.push_back(c);
        }
      }
    }
    const unsigned int id = test::addShapeOf(puz, box[0], box[1], box[2], cells);
    puz.getProblem(prob).setShapeMaximum(puz, id, 1);
    puz.getProblem(prob).setShapeMinimum(puz, id, 1);
  }
  return puz;
}

// Two of a puzzle's pieces, turned, the second's space a move of the grid
// from the first's and no cell shared.
struct Pair {
  unsigned int p1 = 0;
  unsigned int p2 = 0;
  unsigned char t1 = 0;
  unsigned char t2 = 0;
  /// From the first's space to the second's.
  std::array<int, 3> space{};
  /// From the first's hotspot to the second's: what getMoValue takes.
  std::array<int, 3> hotspot{};
};

// The box sizes and densities the random shapes of each grid have.
struct Recipe {
  std::array<int, 3> box;
  unsigned int oneIn;
};

Recipe recipeOf(GridKind kind) {
  switch (kind) {
    case GridKind::Bricks:
      return {{4, 4, 4}, 4};
    case GridKind::TriangularPrism:
      return {{6, 6, 3}, 5};
    case GridKind::Rhombic:
      return {{10, 10, 10}, 12};
    case GridKind::TetraOcta:
      return {{6, 6, 6}, 5};
    case GridKind::Hexagonal:
      return {{4, 4, 3}, 4};
    default:
      return {{8, 8, 2}, 5};
  }
}

// A turn the shape takes (a clone is tried: the cache asserts it).
unsigned char randomTurn(const Voxel& shape, unsigned int turns, std::mt19937& rng) {
  for (;;) {
    const auto t = static_cast<unsigned char>(rng() % turns);
    if (t == 0 || shape.clone()->transform(t)) {
      return t;
    }
  }
}

std::optional<Pair> randomPair(MovementCache& cache, unsigned int pieces, unsigned int turns,
                               std::array<int, 3> range, std::mt19937& rng) {
  Pair p;
  p.p1 = rng() % pieces;
  p.p2 = rng() % pieces;
  p.t1 = randomTurn(cache.getTransformedShape(cache.getShapeOfPiece(p.p1), 0), turns, rng);
  p.t2 = randomTurn(cache.getTransformedShape(cache.getShapeOfPiece(p.p2), 0), turns, rng);
  const Voxel& sh1 = cache.getTransformedShape(cache.getShapeOfPiece(p.p1), p.t1);
  const Voxel& sh2 = cache.getTransformedShape(cache.getShapeOfPiece(p.p2), p.t2);

  for (unsigned int tries = 0; tries < 100000; tries++) {
    for (std::size_t a = 0; a < 3; a++) {
      p.space[a] = static_cast<int>(rng() % static_cast<unsigned int>(2 * range[a] + 1)) - range[a];
    }
    if (!sh1.onGrid(p.space[0], p.space[1], p.space[2])) {
      continue;
    }
    bool shared = false;
    for (const mesh::CellAt& c : test::cellsOf(sh1, 0, 0, 0)) {
      shared = shared || sh2.isFilled2(c.x - p.space[0], c.y - p.space[1], c.z - p.space[2]);
    }
    if (shared) {
      continue;
    }
    p.hotspot = {p.space[0] - sh1.getHx() + sh2.getHx(), p.space[1] - sh1.getHy() + sh2.getHy(),
                 p.space[2] - sh1.getHz() + sh2.getHz()};
    return p;
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("movement checks: a cube needs the cell it moves to", "[solve][disasm][movementchecks]") {
  const MovementChecks* checks = movementChecks(GridKind::Bricks);
  REQUIRE(checks != nullptr);
  CHECK(checks->period == std::array<int, 3>{1, 1, 1});
  REQUIRE(checks->directions ==
          std::vector<std::array<int, 3>>{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
  for (std::size_t dir = 0; dir < 3; dir++) {
    CHECK(checks->at(dir, 0) == std::vector<std::array<int, 3>>{checks->directions[dir]});
  }
  CHECK(checks->extent == std::array<int, 3>{1, 1, 1});
}

// movementcache_1.cpp's table (kChecks in movement_cache_prisms.cpp), made
// by hand there: the derivation has to find exactly these cells.
TEST_CASE("movement checks: the triangular prism's are the original's table",
          "[solve][disasm][movementchecks]") {
  const MovementChecks* checks = movementChecks(GridKind::TriangularPrism);
  REQUIRE(checks != nullptr);
  REQUIRE(checks->period == std::array<int, 3>{2, 2, 1});
  REQUIRE(checks->directions ==
          std::vector<std::array<int, 3>>{{0, 0, 1}, {2, 0, 0}, {1, 1, 0}, {-1, 1, 0}});

  // per direction, for the triangle kinds 0 and 1 (the parity of x + y)
  const std::array<std::array<Offsets, 2>, 4> original = {{
      {{{{0, 0, 1}}, {{0, 0, 1}}}},
      {{{{1, 0, 0}, {2, 0, 0}}, {{1, 0, 0}, {2, 0, 0}}}},
      {{{{1, 0, 0}, {1, 1, 0}}, {{0, 1, 0}, {1, 1, 0}}}},
      {{{{-1, 0, 0}, {-1, 1, 0}}, {{0, 1, 0}, {-1, 1, 0}}}},
  }};
  for (std::size_t dir = 0; dir < 4; dir++) {
    for (unsigned int y = 0; y < 2; y++) {
      for (unsigned int x = 0; x < 2; x++) {
        INFO("direction " << dir << ", cell " << x << " " << y);
        CHECK(asSet(checks->at(dir, checks->kindOf(x, y, 0))) == original[dir][(x + y) % 2]);
      }
    }
  }
  CHECK(checks->extent == std::array<int, 3>{2, 1, 1});
}

// A hexagon moved across a side passes between the two hexagons beside
// the way; moved along a side (to the next cell but one) it passes
// through the two it slides between.
TEST_CASE("movement checks: the hexagonal prism's, by hand", "[solve][disasm][movementchecks]") {
  const MovementChecks* checks = movementChecks(GridKind::Hexagonal);
  REQUIRE(checks != nullptr);
  REQUIRE(checks->directions.size() == 7);
  CHECK(asSet(checks->at(0, 0)) == Offsets{{0, 0, 1}});

  test::CellWorld w(GridKind::Hexagonal);
  const auto distance = [&](const std::array<int, 3>& d) {
    const std::array<double, 3> v = w.vectorOf(d);
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  };
  const double across = distance(checks->directions[1]);
  for (std::size_t dir = 1; dir < 4; dir++) {
    INFO("direction " << dir);
    // to a neighbour
    CHECK(std::abs(distance(checks->directions[dir]) - across) < 1e-4);
    const Offsets cells = asSet(checks->at(dir, 0));
    CHECK(cells.size() == 3);
    CHECK(cells.contains(checks->directions[dir]));
    for (const std::array<int, 3>& c : cells) {
      CHECK(std::abs(distance(c) - across) < 1e-4);
    }
  }
  for (std::size_t dir = 4; dir < 7; dir++) {
    INFO("direction " << dir);
    // to the next cell but one, between two neighbours
    CHECK(std::abs(distance(checks->directions[dir]) - across * std::sqrt(3.0)) < 1e-4);
    const Offsets cells = asSet(checks->at(dir, 0));
    CHECK(cells.contains(checks->directions[dir]));
    unsigned int neighbours = 0;
    for (const std::array<int, 3>& c : cells) {
      neighbours += std::abs(distance(c) - across) < 1e-4 ? 1 : 0;
    }
    CHECK(neighbours == 2);
  }
}

TEST_CASE("movement checks: what the tables of every grid keep to",
          "[solve][disasm][movementchecks]") {
  for (const GridKind kind : kGrids) {
    INFO(nameOf(kind));
    const MovementChecks* checks = movementChecks(kind);
    REQUIRE(checks != nullptr);
    const std::optional<MovementDirections> moves = movementDirections(kind);
    REQUIRE(moves.has_value());
    CHECK(checks->period == moves->period);
    CHECK(checks->directions == moves->directions);
    REQUIRE(checks->checks.size() == checks->directions.size() * checks->kinds());

    const std::unique_ptr<Voxel> grid = GridType(kind).makeVoxel(1, 1, 1);
    // far enough from the origin that no position looked at is negative
    const std::array<int, 3> far = {4 * checks->period[0], 4 * checks->period[1],
                                    4 * checks->period[2]};
    std::array<int, 3> extent{0, 0, 0};
    unsigned int cells = 0;
    for (int z = 0; z < checks->period[2]; z++) {
      for (int y = 0; y < checks->period[1]; y++) {
        for (int x = 0; x < checks->period[0]; x++) {
          const std::size_t cellKind =
              checks->kindOf(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                             static_cast<unsigned int>(z));
          const bool cell = grid->validCoordinate(x, y, z);
          cells += cell ? 1 : 0;
          for (std::size_t dir = 0; dir < checks->directions.size(); dir++) {
            INFO("cell " << x << " " << y << " " << z << ", direction " << dir);
            const std::vector<std::array<int, 3>>& list = checks->at(dir, cellKind);
            if (!cell) {
              CHECK(list.empty());
              continue;
            }
            const Offsets set = asSet(list);
            // each cell once, the cell it ends in among them, never itself
            CHECK(set.size() == list.size());
            CHECK(set.contains(checks->directions[dir]));
            CHECK_FALSE(set.contains({0, 0, 0}));
            for (const std::array<int, 3>& c : list) {
              CHECK(grid->validCoordinate(x + far[0] + c[0], y + far[1] + c[1], z + far[2] + c[2]));
              for (std::size_t a = 0; a < 3; a++) {
                extent[a] = std::max(extent[a], std::abs(c[a]));
              }
            }
          }
        }
      }
    }
    CHECK(cells > 0);
    CHECK(checks->extent == extent);

    // a direction moves the grid onto itself: every cell onto one of its kind
    for (const std::array<int, 3>& d : checks->directions) {
      CHECK(grid->onGrid(d[0], d[1], d[2]));
    }
  }
}

namespace {

// The tables found among the cells of two period boxes around the move
// are those found within one: nothing further off is met.
void widerSearchFindsTheSame(GridKind kind) {
  INFO(nameOf(kind));
  const MovementChecks* checks = movementChecks(kind);
  REQUIRE(checks != nullptr);
  const Result<MovementChecks> wide = deriveMovementChecks(kind, *movementDirections(kind), 2);
  REQUIRE(wide.has_value());
  REQUIRE(wide->checks.size() == checks->checks.size());
  for (std::size_t i = 0; i < checks->checks.size(); i++) {
    CHECK(asSet(wide->checks[i]) == asSet(checks->checks[i]));
  }
  CHECK(wide->extent == checks->extent);
}

}  // namespace

TEST_CASE("movement checks: a wider search finds no more cells",
          "[solve][disasm][movementchecks]") {
  for (const GridKind kind :
       {GridKind::Bricks, GridKind::TriangularPrism, GridKind::Hexagonal, GridKind::Cairo}) {
    widerSearchFindsTheSame(kind);
  }
}

TEST_CASE("movement checks: a wider search finds no more cells, the grids with many kinds",
          "[solve][disasm][movementchecks][slow]") {
  widerSearchFindsTheSame(GridKind::Rhombic);
  widerSearchFindsTheSame(GridKind::TetraOcta);
}

TEST_CASE("movement checks: a direction that is no move of the grid is refused",
          "[solve][disasm][movementchecks]") {
  // one triangle on: the triangle there points the other way
  const Result<MovementChecks> prisms =
      deriveMovementChecks(GridKind::TriangularPrism, {{2, 2, 1}, {{1, 0, 0}}});
  REQUIRE_FALSE(prisms.has_value());
  CHECK(prisms.error().code == Errc::InvalidArgument);

  // one cube of the tetra-octa grid on: its mirror image
  const Result<MovementChecks> tetraOcta =
      deriveMovementChecks(GridKind::TetraOcta, {{6, 6, 6}, {{3, 0, 0}}});
  REQUIRE_FALSE(tetraOcta.has_value());

  // spheres are no polyhedra
  CHECK_FALSE(movementDirections(GridKind::Spheres).has_value());
  CHECK(movementChecks(GridKind::Spheres) == nullptr);
  CHECK(movementChecks(GridKind::Sliding) == nullptr);
  CHECK(movementChecks(GridKind::Stacking) == nullptr);
}

namespace {

// A point of the recording: four times V2's physical coordinates.
using Point = std::array<long, 3>;

Point plus(const Point& a, const Point& b, long times = 1) {
  return {a[0] + times * b[0], a[1] + times * b[1], a[2] + times * b[2]};
}

// One grid of tests/data/movement_checks_by_v2.txt.
struct V2Grid {
  /// The moves the grid repeats by.
  std::vector<Point> lattice;
  std::vector<Point> moves;
  /// The centroid of each kind of cell.
  std::vector<Point> cells;
  /// Per kind of cell and move, the centroids of the cells that must be
  /// empty.
  std::vector<std::vector<std::vector<Point>>> checks;
};

std::map<std::string, V2Grid> readV2Checks() {
  // the tests run from the source root
  std::ifstream in("tests/data/movement_checks_by_v2.txt");
  REQUIRE(in.good());
  std::map<std::string, V2Grid> out;
  V2Grid* g = nullptr;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream words(line);
    std::string what;
    words >> what;
    if (what == "grid") {
      std::string name;
      words >> name;
      g = &out[name];
      continue;
    }
    REQUIRE(g != nullptr);
    const auto point = [&]() {
      Point p{};
      words >> p[0] >> p[1] >> p[2];
      return p;
    };
    std::size_t index = 0;
    if (what == "lattice") {
      g->lattice.push_back(point());
    } else if (what == "move") {
      words >> index;
      REQUIRE(index == g->moves.size());
      g->moves.push_back(point());
    } else if (what == "cell") {
      words >> index;
      REQUIRE(index == g->cells.size());
      g->cells.push_back(point());
      g->checks.emplace_back();
    } else if (what == "check") {
      std::size_t move = 0;
      words >> index >> move;
      REQUIRE(index + 1 == g->cells.size());
      REQUIRE(move == g->checks.back().size());
      std::vector<Point> cells;
      for (Point p = point(); words; p = point()) {
        cells.push_back(p);
      }
      g->checks.back().push_back(std::move(cells));
    } else {
      FAIL("a line of no known kind: " << line);
    }
  }
  return out;
}

// How a grid of ours lies on V2's. Both are cuts of a grid of cubes, the
// axes the same; the cube at the origin of ours is the cube at the origin
// of V2's. The coordinates have nothing else in common (and V2 cuts the
// rhombic grid's cells in two), so the cells are matched by where they
// are: V2's cell is the cell of ours its centroid lies in.
struct V2Match {
  GridKind kind;
  /// The grid's name in the recording.
  const char* name;
  /// The side of a cube in our coordinates and in the recording's units.
  int cube;
  long v2Cube;
  /// V2's cells in one cell of ours.
  unsigned int parts;
  /// The cells of our period box.
  unsigned int cells;
};

std::string listOf(const std::set<mesh::CellAt>& cells) {
  std::string out;
  for (const mesh::CellAt& c : cells) {
    out += "(" + std::to_string(c.x) + " " + std::to_string(c.y) + " " + std::to_string(c.z) + ") ";
  }
  return out;
}

void sameChecksAsV2(const V2Match& m, const V2Grid& v2) {
  INFO(m.name);
  const MovementChecks* checks = movementChecks(m.kind);
  REQUIRE(checks != nullptr);
  REQUIRE(v2.lattice.size() == 3);
  REQUIRE_FALSE(v2.cells.empty());
  for (const std::vector<std::vector<Point>>& ofCell : v2.checks) {
    REQUIRE(ofCell.size() == v2.moves.size());
  }
  test::CellWorld w(m.kind);
  const std::array<int, 3> period = checks->period;

  // some grids take no negative coordinates: ask about the same cell two
  // period boxes from the origin
  const auto isCell = [&](const mesh::CellAt& c) {
    const auto wrap = [](int v, int p) { return ((v % p) + p) % p + 2 * p; };
    return w.voxel().validCoordinate(wrap(c.x, period[0]), wrap(c.y, period[1]),
                                     wrap(c.z, period[2]));
  };
  // our cells with a corner of their box at `from`, `size` along each axis
  const auto cellsIn = [&](int from, int size) {
    std::vector<mesh::CellAt> out;
    for (int z = from; z < from + size; z++) {
      for (int y = from; y < from + size; y++) {
        for (int x = from; x < from + size; x++) {
          if (isCell({x, y, z})) {
            out.push_back({x, y, z});
          }
        }
      }
    }
    return out;
  };

  // the recording's unit in the mesh's, and where the centre of the cube
  // at the origin is in the mesh
  const std::array<double, 3> side = w.vectorOf({m.cube, 0, 0});
  REQUIRE(side[0] > 0);
  REQUIRE(std::abs(side[1]) + std::abs(side[2]) < 1e-6);
  const double scale = side[0] / static_cast<double>(m.v2Cube);
  std::array<double, 3> centre{};
  const std::vector<mesh::CellAt> cube = cellsIn(0, m.cube);
  REQUIRE_FALSE(cube.empty());
  for (const mesh::CellAt& c : cube) {
    for (std::size_t a = 0; a < 3; a++) {
      centre[a] += w.cell(c).centre[a] / static_cast<double>(cube.size());
    }
  }
  const auto toMesh = [&](const Point& p) {
    std::array<double, 3> at{};
    for (std::size_t a = 0; a < 3; a++) {
      at[a] = centre[a] + scale * (static_cast<double>(p[a]) - static_cast<double>(m.v2Cube) / 2);
    }
    return at;
  };
  // well inside: V2's centroids are no nearer a face of ours than a
  // twentieth of a cell
  const auto inside = [](const test::CellWorld::Placed& cell, const std::array<double, 3>& p) {
    double d2 = 0;
    for (std::size_t a = 0; a < 3; a++) {
      d2 += (p[a] - cell.centre[a]) * (p[a] - cell.centre[a]);
    }
    if (d2 > cell.radius * cell.radius) {
      return false;
    }
    return std::ranges::all_of(cell.poly.faces, [&](const mesh::CellFace& f) {
      const mesh::Vec3& on = f.pos(0);
      const double out = static_cast<double>(f.normal.x) * (p[0] - static_cast<double>(on.x)) +
                         static_cast<double>(f.normal.y) * (p[1] - static_cast<double>(on.y)) +
                         static_cast<double>(f.normal.z) * (p[2] - static_cast<double>(on.z));
      return out < -1e-3;
    });
  };

  // V2's cells in each cell of our period box; together they are the cell
  // (their centroids' mean is its centroid)
  struct Part {
    std::size_t kind;
    Point shift;
  };
  const std::vector<mesh::CellAt> box = cellsIn(0, period[0]);
  REQUIRE(period[0] == period[1]);
  REQUIRE(period[0] == period[2]);
  REQUIRE(box.size() == m.cells);
  std::map<mesh::CellAt, std::vector<Part>> parts;
  for (std::size_t kind = 0; kind < v2.cells.size(); kind++) {
    for (long n0 = -3; n0 <= 3; n0++) {
      for (long n1 = -3; n1 <= 3; n1++) {
        for (long n2 = -3; n2 <= 3; n2++) {
          const Point shift =
              plus(plus(plus({0, 0, 0}, v2.lattice[0], n0), v2.lattice[1], n1), v2.lattice[2], n2);
          const std::array<double, 3> at = toMesh(plus(v2.cells[kind], shift));
          for (const mesh::CellAt& c : box) {
            if (inside(w.cell(c), at)) {
              parts[c].push_back({kind, shift});
            }
          }
        }
      }
    }
  }
  for (const mesh::CellAt& c : box) {
    INFO("cell " << c.x << " " << c.y << " " << c.z);
    REQUIRE(parts[c].size() == m.parts);
    std::array<double, 3> mean{};
    for (const Part& part : parts[c]) {
      const std::array<double, 3> at = toMesh(plus(v2.cells[part.kind], part.shift));
      for (std::size_t a = 0; a < 3; a++) {
        mean[a] += at[a] / static_cast<double>(m.parts);
      }
    }
    for (std::size_t a = 0; a < 3; a++) {
      REQUIRE(std::abs(mean[a] - w.cell(c).centre[a]) < 1e-3);
    }
  }

  // our cells as far as a move from the period box reaches, and the one a
  // point of the recording lies in
  struct Ours {
    mesh::CellAt at;
    const test::CellWorld::Placed* cell;
  };
  std::vector<Ours> around;
  for (const mesh::CellAt& c : cellsIn(-2 * period[0], 5 * period[0])) {
    around.push_back({c, &w.cell(c)});
  }
  std::map<Point, mesh::CellAt> known;
  const auto cellAt = [&](const Point& p) {
    const auto it = known.find(p);
    if (it != known.end()) {
      return it->second;
    }
    const std::array<double, 3> at = toMesh(p);
    std::optional<mesh::CellAt> found;
    for (const Ours& o : around) {
      if (inside(*o.cell, at)) {
        REQUIRE_FALSE(found.has_value());
        found = o.at;
      }
    }
    INFO("V2's cell at " << p[0] << " " << p[1] << " " << p[2]);
    REQUIRE(found.has_value());
    known.emplace(p, *found);
    return *found;
  };

  std::set<std::size_t> movesMet;
  unsigned int compared = 0;
  for (std::size_t dir = 0; dir < checks->directions.size(); dir++) {
    const std::array<int, 3>& step = checks->directions[dir];
    INFO("direction " << step[0] << " " << step[1] << " " << step[2]);
    // V2's move along our step: the same, or two of V2's to one of ours
    // (V2's cells of the rhombic grid move half a space diagonal), or the
    // same the other way (V2 keeps one of the two)
    const std::array<double, 3> ours = w.vectorOf(step);
    std::size_t move = 0;
    long steps = 0;
    unsigned int found = 0;
    for (std::size_t i = 0; i < v2.moves.size(); i++) {
      for (const long k : {1L, 2L, -1L, -2L}) {
        bool same = true;
        for (std::size_t a = 0; a < 3; a++) {
          same = same && std::abs(ours[a] - scale * static_cast<double>(k * v2.moves[i][a])) < 1e-4;
        }
        if (same) {
          move = i;
          steps = k;
          found++;
        }
      }
    }
    REQUIRE(found == 1);
    movesMet.insert(move);

    for (const mesh::CellAt& c : box) {
      // what V2's parts of the cell pass through, step after step
      std::set<mesh::CellAt> expected;
      for (const Part& part : parts[c]) {
        for (long n = 0; n < std::abs(steps); n++) {
          for (const Point& q : v2.checks[part.kind][move]) {
            expected.insert(cellAt(plus(plus(q, part.shift), v2.moves[move], n)));
          }
        }
      }
      if (steps < 0) {
        // V2 has the move the other way: the cell passes through what the
        // cell a step behind it passes through coming here (itself left
        // out), and through that cell
        expected.insert(c);
        std::set<mesh::CellAt> back;
        for (const mesh::CellAt& x : expected) {
          back.insert({x.x + step[0], x.y + step[1], x.z + step[2]});
        }
        expected = std::move(back);
      }
      // the parts are in each other's way; the cell is not in its own
      expected.erase(c);

      std::set<mesh::CellAt> derived;
      for (const std::array<int, 3>& o :
           checks->at(dir, checks->kindOf(static_cast<unsigned int>(c.x),
                                          static_cast<unsigned int>(c.y),
                                          static_cast<unsigned int>(c.z)))) {
        derived.insert({c.x + o[0], c.y + o[1], c.z + o[2]});
      }
      INFO("cell " << c.x << " " << c.y << " " << c.z << "\n  derived: " << listOf(derived)
                   << "\n  V2's:    " << listOf(expected));
      CHECK(derived == expected);
      compared++;
    }
  }
  // every move of V2's is one of ours
  CHECK(movesMet.size() == v2.moves.size());
  CHECK(compared == m.cells * checks->directions.size());
}

}  // namespace

// BurrTools V2 computes the same tables its own way (a shrunk cell's
// Minkowski sum with the move against the neighbours, on its own grids
// read from Lua scripts): the one other answer there is for these grids.
TEST_CASE("movement checks: the tetra-octa grid's are those BurrTools V2 computes",
          "[solve][disasm][movementchecks]") {
  const std::map<std::string, V2Grid> v2 = readV2Checks();
  REQUIRE(v2.contains("tetra-octa"));
  sameChecksAsV2({GridKind::TetraOcta, "tetra-octa", 3, 4, 1, 40}, v2.at("tetra-octa"));
}

// V2's rhombic grid is finer than the original's and ours (each cell cut
// in two), and its cells also move half a space diagonal, which ours
// cannot: a whole step of ours is two of those.
TEST_CASE("movement checks: the rhombic grid's are those BurrTools V2 computes",
          "[solve][disasm][movementchecks]") {
  const std::map<std::string, V2Grid> v2 = readV2Checks();
  REQUIRE(v2.contains("rhombic"));
  sameChecksAsV2({GridKind::Rhombic, "rhombic", 5, 8, 2, 24}, v2.at("rhombic"));
}

// The A/B switch's claim: on cubes and triangular prisms the derived cache
// answers as the ported ones do, for any two shapes however they lie.
TEST_CASE("movement cache: the derived cache gives the ported caches' values",
          "[solve][disasm][movementcache]") {
  std::mt19937 rng(11);
  for (const GridKind kind : {GridKind::Bricks, GridKind::TriangularPrism}) {
    INFO(nameOf(kind));
    const Recipe recipe = recipeOf(kind);
    const Puzzle puz = randomShapes(kind, recipe.box, recipe.oneIn, 8, rng);
    const Problem& problem = puz.getProblem(0);
    const std::unique_ptr<MovementCache> ported = makeMovementCache(puz, problem);
    const std::unique_ptr<MovementCache> derived = makeMovementCache(puz, problem, true);
    REQUIRE(ported != nullptr);
    REQUIRE(derived != nullptr);
    REQUIRE(ported->numDirections() == derived->numDirections());
    const unsigned int turns = puz.getGridType().getSymmetries().getNumTransformations();

    std::map<unsigned int, unsigned int> seen;
    for (unsigned int n = 0; n < 2000; n++) {
      const std::optional<Pair> p =
          randomPair(*ported, 8, turns, recipe.box, rng);
      REQUIRE(p.has_value());
      std::array<unsigned int, 4> a{};
      std::array<unsigned int, 4> b{};
      ported->getMoValue(p->hotspot[0], p->hotspot[1], p->hotspot[2], p->t1, p->t2, p->p1, p->p2,
                         a.data());
      derived->getMoValue(p->hotspot[0], p->hotspot[1], p->hotspot[2], p->t1, p->t2, p->p1, p->p2,
                          b.data());
      INFO("pieces " << p->p1 << " " << p->p2 << " turned " << int{p->t1} << " " << int{p->t2}
                     << " at " << p->hotspot[0] << " " << p->hotspot[1] << " " << p->hotspot[2]);
      CHECK(a == b);
      for (unsigned int d = 0; d < ported->numDirections(); d++) {
        seen[a[d]]++;
      }
    }
    // blocked at once, after some steps, and never
    CHECK(seen[0] > 100);
    CHECK(seen[1] + seen[2] + seen[3] > 100);
    CHECK(seen[32000] > 100);
  }
}

namespace {

// The cache's values for random pairs of shapes against what the cells
// allow when moved step by step (freeSteps: no tables).
void cacheAgreesWithTheCells(GridKind kind, unsigned int pairs, std::uint32_t seed) {
  INFO(nameOf(kind));
  std::mt19937 rng(seed);
  const Recipe recipe = recipeOf(kind);
  const Puzzle puz = randomShapes(kind, recipe.box, recipe.oneIn, 6, rng);
  const Problem& problem = puz.getProblem(0);
  const std::unique_ptr<MovementCache> cache = makeMovementCache(puz, problem, true);
  REQUIRE(cache != nullptr);
  const unsigned int turns = puz.getGridType().getSymmetries().getNumTransformations();
  test::CellWorld w(kind);
  REQUIRE(cache->numDirections() == w.directions().size());

  std::map<unsigned int, unsigned int> seen;
  for (unsigned int n = 0; n < pairs; n++) {
    const std::optional<Pair> p =
        randomPair(*cache, 6, turns, recipe.box, rng);
    REQUIRE(p.has_value());
    std::vector<unsigned int> values(cache->numDirections());
    cache->getMoValue(p->hotspot[0], p->hotspot[1], p->hotspot[2], p->t1, p->t2, p->p1, p->p2,
                      values.data());

    const test::Cells first =
        test::cellsOf(cache->getTransformedShape(cache->getShapeOfPiece(p->p1), p->t1), 0, 0, 0);
    const test::Cells second =
        test::cellsOf(cache->getTransformedShape(cache->getShapeOfPiece(p->p2), p->t2),
                      p->space[0], p->space[1], p->space[2]);
    for (unsigned int d = 0; d < cache->numDirections(); d++) {
      INFO("pieces " << p->p1 << " " << p->p2 << " turned " << int{p->t1} << " " << int{p->t2}
                     << " at " << p->space[0] << " " << p->space[1] << " " << p->space[2]
                     << ", direction " << d);
      CHECK(values[d] == test::freeSteps(w, first, w.directions()[d], second));
      seen[values[d]]++;
    }
  }
  // blocked at once, after some steps, and never: all are among them
  CHECK(seen[0] > pairs / 4);
  CHECK(seen[1] + seen[2] + seen[3] > pairs / 20);
  CHECK(seen[32000] > pairs);
}

}  // namespace

TEST_CASE("movement cache: the derived values are what the cells themselves allow",
          "[solve][disasm][movementcache]") {
  std::uint32_t seed = 21;
  for (const GridKind kind : kGrids) {
    cacheAgreesWithTheCells(kind, 25, seed++);
  }
}

TEST_CASE("movement cache: the derived values are what the cells allow, many pairs",
          "[solve][disasm][movementcache][slow]") {
  std::uint32_t seed = 31;
  for (const GridKind kind : kGrids) {
    cacheAgreesWithTheCells(kind, 600, seed++);
  }
}
