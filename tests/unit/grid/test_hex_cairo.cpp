// SPDX-License-Identifier: GPL-3.0-or-later
// The hexagonal and cairo prism grids (decision 0023) end to end: their
// lattices, their derived symmetry tables, and the assembler on them,
// checked against a brute-force count of the tilings of a shape with
// dominoes. New (the original had no such grids).
#include "../solve/solve_test_helpers.h"
#include "grid_test_helpers.h"

#include "shapelogic/grid/cairo_lattice.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/hex_lattice.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/symmetries_derived.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/assembler_1.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

using namespace bt;
using namespace bt::test;

namespace {

using Cell = std::tuple<int, int, int>;

constexpr GridKind kNewGrids[] = {GridKind::Hexagonal, GridKind::Cairo};

// A shape of these cells in a space just big enough (from the origin).
std::unique_ptr<Voxel> shapeOf(const GridType& gt, const std::vector<Cell>& cells) {
  int mx = 0;
  int my = 0;
  int mz = 0;
  for (const auto& [x, y, z] : cells) {
    mx = std::max(mx, x);
    my = std::max(my, y);
    mz = std::max(mz, z);
  }
  std::unique_ptr<Voxel> v = makeVoxel(gt, static_cast<unsigned int>(mx + 1),
                                       static_cast<unsigned int>(my + 1),
                                       static_cast<unsigned int>(mz + 1));
  for (const auto& [x, y, z] : cells) {
    REQUIRE(v->validCoordinate(x, y, z));
    v->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                static_cast<unsigned int>(z), Voxel::kFilled);
  }
  return v;
}

// The face neighbours of a cell.
std::vector<Cell> faceNeighbours(const Voxel& v, const Cell& c) {
  std::vector<Cell> out;
  int xn = 0;
  int yn = 0;
  int zn = 0;
  for (unsigned int n = 0; v.getNeighbor(n, 0, std::get<0>(c), std::get<1>(c), std::get<2>(c), &xn,
                                          &yn, &zn);
       n++) {
    out.emplace_back(xn, yn, zn);
  }
  return out;
}

// The number of ways to cover `cells` with the domino `piece` (two cells
// sharing a face, placed in any of the grid's orientations but not
// mirrored): the perfect matchings of the cells along the pairs the piece
// fits. Brute force, for small shapes.
std::size_t tilings(const GridType& gt, const std::vector<Cell>& cells, const Voxel& piece) {
  const std::set<Cell> all(cells.begin(), cells.end());
  // the pairs the piece fits: the same shape under some rotation
  std::vector<std::pair<Cell, Cell>> pairs;
  const std::unique_ptr<Voxel> probe = makeVoxel(gt, 1, 1, 1);
  for (const Cell& a : cells) {
    for (const Cell& b : faceNeighbours(*probe, a)) {
      if (a < b && all.contains(b)) {
        // the pair as a shape of its own, moved to the origin's kind
        std::vector<Cell> two{a, b};
        const int ox = std::min(std::get<0>(a), std::get<0>(b));
        const int oy = std::min(std::get<1>(a), std::get<1>(b));
        const int oz = std::min(std::get<2>(a), std::get<2>(b));
        // keep the cells' kinds: move by a lattice vector only
        int dx = ox;
        int dy = oy;
        if (gt.getType() == GridKind::Cairo) {
          const cairo::Fix f = cairo::fixToLattice(-ox, -oy);
          dx = ox - f.a;
          dy = oy - f.b;
        }
        for (Cell& c : two) {
          c = {std::get<0>(c) - dx, std::get<1>(c) - dy, std::get<2>(c) - oz};
        }
        const std::unique_ptr<Voxel> pairShape = shapeOf(gt, two);
        if (piece.identicalWithRots(*pairShape, false, false)) {
          pairs.emplace_back(a, b);
        }
      }
    }
  }
  // count the matchings: always match the first uncovered cell
  std::set<Cell> covered;
  std::size_t count = 0;
  const auto rec = [&](auto& self) -> void {
    Cell first{};
    bool any = false;
    for (const Cell& c : cells) {
      if (!covered.contains(c)) {
        first = c;
        any = true;
        break;
      }
    }
    if (!any) {
      count++;
      return;
    }
    for (const auto& [a, b] : pairs) {
      const Cell* other = a == first ? &b : (b == first ? &a : nullptr);
      if (other == nullptr || covered.contains(*other)) {
        continue;
      }
      covered.insert(first);
      covered.insert(*other);
      self(self);
      covered.erase(first);
      covered.erase(*other);
    }
  };
  rec(rec);
  return count;
}

// A puzzle: the result and `count` copies of one piece.
Puzzle puzzleOf(const GridType& gt, const std::vector<Cell>& result, const std::vector<Cell>& piece,
                unsigned int count) {
  Puzzle p(gt);
  const unsigned int r = p.addShape(shapeOf(gt, result));
  const unsigned int s = p.addShape(shapeOf(gt, piece));
  p.addProblem();
  Problem& pr = p.getProblem(0);
  pr.setResultId(p, r);
  pr.setShapeMinimum(p, s, count);
  pr.setShapeMaximum(p, s, count);
  return p;
}

// Assembler 1 takes several pieces of one shape (Assembler 0 one per part).
std::size_t assembliesOf(const Puzzle& p) {
  Assembler1 a(p, p.getProblem(0));
  REQUIRE(a.createMatrix({}) == AssemblerError::None);
  const RunResult r = runOnce(a, searchOptions(1, nullptr));
  REQUIRE(r.end == RunEnd::Finished);
  return r.found.size();
}

}  // namespace

TEST_CASE("new grids: the lattices and the derived symmetry tables", "[grid][newgrids]") {
  SECTION("hexagonal: a cell's centre is found again, the rows are sheared") {
    for (int y = -3; y <= 3; y++) {
      for (int x = -3; x <= 3; x++) {
        const hex::HexCell c = hex::cellAt(hex::centreX(x, y), hex::centreY(y), hex::centreZ(2));
        CHECK(c.x == x);
        CHECK(c.y == y);
        CHECK(c.z == 2);
      }
    }
    CHECK(hex::centreX(0, 2) == 1.5);
    // 12 plain transformations and 24 in all, as the triangular prism
    // grid, and the same 54 groups
    const Symmetries& sym = GridType(GridKind::Hexagonal).getSymmetries();
    CHECK(sym.getNumTransformations() == 12);
    CHECK(sym.getNumTransformationsMirror() == 24);
    CHECK(sym.getNumSymmetryGroups() == 54);
    CHECK(sym.getNumSymmetryGroups() ==
          GridType(GridKind::TriangularPrism).getSymmetries().getNumSymmetryGroups());
  }
  SECTION("cairo: the cells, their kinds and the lattice") {
    int cells = 0;
    std::array<int, 4> kinds{};
    for (int y = -4; y < 4; y++) {
      for (int x = -4; x < 4; x++) {
        const int k = cairo::kindOf(x, y);
        CHECK((k >= 0) == cairo::isCell(x, y));
        if (k >= 0) {
          cells++;
          kinds[static_cast<std::size_t>(k)]++;
          // the pentagon has five sides of one length
          const auto p = cairo::pentagon(x, y);
          for (std::size_t i = 0; i < 5; i++) {
            const double dx = p[i].x.value() - p[(i + 1) % 5].x.value();
            const double dy = p[i].y.value() - p[(i + 1) % 5].y.value();
            CHECK(std::abs(std::sqrt(dx * dx + dy * dy) - std::sqrt(1 + cairo::kOfs * cairo::kOfs)) <
                  1e-9);
          }
        }
      }
    }
    CHECK(cells == 32);
    for (const int k : kinds) {
      CHECK(k == 8);
    }
    CHECK(cairo::keepsKinds(2, 2));
    CHECK(cairo::keepsKinds(4, 0));
    CHECK_FALSE(cairo::keepsKinds(2, 0));
    CHECK_FALSE(cairo::keepsKinds(1, 1));
    const Symmetries& sym = GridType(GridKind::Cairo).getSymmetries();
    CHECK(sym.getNumTransformations() == 8);
    CHECK(sym.getNumTransformationsMirror() == 16);
    // the 35 subgroups of the square prism's group (D4h)
    CHECK(sym.getNumSymmetryGroups() == 35);
  }
  SECTION("every subgroup of the group is a symmetry group of the table") {
    // the derived tables hold every subgroup by construction; checked here
    // by a second enumeration, through every pair of transformations
    for (const GridKind kind : kNewGrids) {
      INFO(toString(kind));
      const Symmetries& sym = GridType(kind).getSymmetries();
      const unsigned int n = sym.getNumTransformationsMirror();
      std::set<std::uint64_t> groups;
      for (unsigned int s = 0; s < sym.getNumSymmetryGroups(); s++) {
        std::uint64_t mask = 0;
        for (unsigned int t = 0; t < n; t++) {
          if (sym.symmetrieContainsTransformation(static_cast<Symmetry>(s), t)) {
            mask |= std::uint64_t{1} << t;
          }
        }
        groups.insert(mask);
      }
      const auto closure = [&](std::uint64_t set) {
        bool grown = true;
        while (grown) {
          grown = false;
          for (unsigned int a = 0; a < n; a++) {
            for (unsigned int b = 0; b < n; b++) {
              if ((set & (std::uint64_t{1} << a)) != 0 && (set & (std::uint64_t{1} << b)) != 0) {
                const std::uint64_t bit =
                    std::uint64_t{1}
                    << sym.transAdd(static_cast<unsigned char>(a), static_cast<unsigned char>(b));
                if ((set & bit) == 0) {
                  set |= bit;
                  grown = true;
                }
              }
            }
          }
        }
        return set;
      };
      for (unsigned int a = 0; a < n; a++) {
        for (unsigned int b = a; b < n; b++) {
          CHECK(groups.contains(closure(1 | (std::uint64_t{1} << a) | (std::uint64_t{1} << b))));
        }
      }
    }
  }
}

TEST_CASE("new grids: every shape's symmetries are a known group", "[grid][newgrids]") {
  // random shapes of each grid: calculateSymmetry answers, the answer is
  // the shape's own list, and a turned copy has a conjugate group of the
  // same size
  for (const GridKind kind : kNewGrids) {
    INFO(toString(kind));
    const GridType gt(kind);
    const Symmetries& sym = gt.getSymmetries();
    std::uint32_t seed = 12345;
    const auto next = [&seed] {
      seed = seed * 1103515245U + 12345U;
      return (seed >> 16) & 0x7fff;
    };
    for (int shape = 0; shape < 40; shape++) {
      std::unique_ptr<Voxel> v = makeVoxel(gt, 5, 5, 3);
      const auto coords = validCoordinates(*v, 5);
      for (int i = 0; i < 6; i++) {
        const auto [x, y, z] = coords[next() % coords.size()];
        if (z < 3) {
          v->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                      static_cast<unsigned int>(z), Voxel::kFilled);
        }
      }
      if (v->countState(Voxel::kFilled) == 0) {
        continue;
      }
      REQUIRE(sym.symmetryKnown(*v));
      const Symmetry s = v->selfSymmetries();
      unsigned int size = 0;
      for (unsigned int t = 0; t < sym.getNumTransformationsMirror(); t++) {
        auto copy = v->clone();
        REQUIRE(copy->transform(t));
        const bool same = v->identicalInBB(*copy);
        CHECK(same == sym.symmetrieContainsTransformation(s, t));
        size += same ? 1 : 0;
      }
      CHECK(size >= 1);
      auto turned = v->clone();
      REQUIRE(turned->transform(1));
      unsigned int sizeTurned = 0;
      const Symmetry st = turned->selfSymmetries();
      for (unsigned int t = 0; t < sym.getNumTransformationsMirror(); t++) {
        sizeTurned += sym.symmetrieContainsTransformation(st, t) ? 1 : 0;
      }
      CHECK(sizeTurned == size);
    }
  }
}

TEST_CASE("new grids: the assembler tiles shapes with dominoes as a brute-force count says",
          "[grid][newgrids][solve]") {
  // results of two layers without any symmetry (so the assembler drops no
  // turned duplicates) tiled by identical dominoes
  struct Case {
    GridKind kind;
    std::vector<Cell> result;
    std::vector<Cell> piece;
  };
  const std::vector<Case> cases = {
      // hexagonal: an L of four in a row and a cell north-east of its end,
      // over a second layer of two
      {GridKind::Hexagonal,
       {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 0, 1}, {1, 0, 1}},
       {{0, 0, 0}, {1, 0, 0}}},
      // hexagonal: four columns of two at different heights, the domino
      // standing up
      {GridKind::Hexagonal,
       {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 2}, {2, 0, 0}, {2, 0, 1}, {2, 1, 1}, {2, 1, 2}},
       {{0, 0, 0}, {0, 0, 1}}},
      // cairo: six cells of two layers, the domino of two kinds across a side
      {GridKind::Cairo,
       {{1, 0, 0}, {2, 1, 0}, {3, 0, 0}, {4, 1, 0}, {1, 0, 1}, {2, 1, 1}},
       {{1, 0, 0}, {2, 1, 0}}},
      // cairo: four columns of two at different heights, the domino
      // standing up
      {GridKind::Cairo,
       {{1, 0, 0}, {1, 0, 1}, {2, 1, 1}, {2, 1, 2}, {3, 0, 0}, {3, 0, 1}, {0, 1, 1}, {0, 1, 2}},
       {{1, 0, 0}, {1, 0, 1}}},
  };
  for (const Case& c : cases) {
    INFO(toString(c.kind) << ", " << c.result.size() << " cells");
    const GridType gt(c.kind);
    const std::unique_ptr<Voxel> result = shapeOf(gt, c.result);
    const std::unique_ptr<Voxel> piece = shapeOf(gt, c.piece);
    // the pieces touch at a face
    REQUIRE(piece->connected(0, true, Voxel::kEmpty));
    const std::size_t expected = tilings(gt, c.result, *piece);
    INFO("tilings by brute force: " << expected);
    REQUIRE(expected > 0);
    const Symmetry s = result->selfSymmetries();
    INFO("the result's symmetry group " << s);
    REQUIRE(unSymmetric(s));
    const Puzzle p = puzzleOf(gt, c.result, c.piece,
                              static_cast<unsigned int>(c.result.size() / 2));
    CHECK(assembliesOf(p) == expected);
  }
}
