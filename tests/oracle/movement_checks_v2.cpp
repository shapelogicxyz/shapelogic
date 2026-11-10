// SPDX-License-Identifier: GPL-3.0-or-later
// movement_checks_v2: the movement checks BurrTools V2 computes for its
// octahedral and rhombic grids (src/lib/grid.cpp, calculateMovementChecks:
// the cells that must be empty for a cell to move one step along each
// movement vector), one line per cell kind and vector. Built by
// `v2_movement_checks.py` against V2's library, with the grids of V2's
// data/scripts/grid_*.lua as C++ initialisers (grids.inc, made by the
// script); its output is tests/data/movement_checks_by_v2.txt, which
// tests/unit/solve/test_movement_checks.cpp compares with the checks this
// core derives (decision 0025). Cells are written as four times their
// centroid in V2's physical coordinates (whole numbers), so the
// comparison needs no coordinates in common: V2's rhombic cells are
// halves of ours.

#include "grid.h"
#include "symmetry.h"
#include "transformation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using G = BurrTools::PuzzleLibrary::Grid;
#include "grids.inc"

namespace {

void dump(const char* name, G::RegisterInformation&& reg) {
  const auto id = reg.storageId;
  const std::string err = G::registerGrid(std::move(reg));
  if (!err.empty()) {
    std::fprintf(stderr, "%s: %s\n", name, err.c_str());
    std::exit(1);
  }
  G grid(id);
  const auto info = grid.getExpensiveInformation();
  const auto& pt = info->physicalTranslations;

  // four times the centroid of the cell at a voxel position
  const auto centroid = [&](const BurrTools::Maths::Vector<int>& pos, long out[3]) {
    const auto [base, ep] = grid.getBaseForVoxelPosition(pos);
    if (base < 0) {
      std::fprintf(stderr, "%s: no cell at %d %d %d\n", name, pos.x(), pos.y(), pos.z());
      std::exit(1);
    }
    const auto& sh = info->physicalShapes[static_cast<size_t>(base)];
    double c[3] = {0, 0, 0};
    for (const auto& v : sh.vertices) {
      c[0] += v.x();
      c[1] += v.y();
      c[2] += v.z();
    }
    for (int r = 0; r < 3; r++) {
      const double t = pt[3 * r] * ep.x() + pt[3 * r + 1] * ep.y() + pt[3 * r + 2] * ep.z();
      const double four = c[r] * 4.0 / static_cast<double>(sh.vertices.size()) + 4.0 * t;
      out[r] = std::lround(four);
      if (std::fabs(four - static_cast<double>(out[r])) > 1e-3) {
        std::fprintf(stderr, "%s: a centroid off the quarter lattice\n", name);
        std::exit(1);
      }
    }
  };

  std::printf("grid %s\n", name);
  // the three moves the grid repeats by (times four, as everything)
  for (int k = 0; k < 3; k++) {
    std::printf("lattice %ld %ld %ld\n", std::lround(4 * pt[k]), std::lround(4 * pt[3 + k]),
                std::lround(4 * pt[6 + k]));
  }
  for (size_t d = 0; d < info->movementVectors.size(); d++) {
    long a[3];
    long b[3];
    centroid(info->basePositions[0], a);
    centroid(info->basePositions[0] + info->movementVectors[d], b);
    std::printf("move %zu %ld %ld %ld\n", d, b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  }
  for (size_t base = 0; base < info->basePositions.size(); base++) {
    long c[3];
    centroid(info->basePositions[base], c);
    std::printf("cell %zu %ld %ld %ld\n", base, c[0], c[1], c[2]);
    for (size_t d = 0; d < info->movementVectors.size(); d++) {
      std::printf("check %zu %zu", base, d);
      for (const auto& diff : info->movementChecks[base][d]) {
        long q[3];
        centroid(info->basePositions[base] +
                     BurrTools::Maths::Vector<int>(diff.x(), diff.y(), diff.z()),
                 q);
        std::printf(" %ld %ld %ld", q[0], q[1], q[2]);
      }
      std::printf("\n");
    }
  }
}

}  // namespace

int main() {
  BurrTools::PuzzleLibrary::TRotation::init_tables();
  BurrTools::PuzzleLibrary::RotationalSymmetryGroup::init_tables();
  std::printf("# The movement checks of BurrTools V2 (calculateMovementChecks, src/lib/grid.cpp)\n"
              "# for its octahedral and rhombic grids. Recorded by\n"
              "# tests/oracle/v2_movement_checks.py; do not edit.\n"
              "# Every triple is four times a point in V2's physical coordinates:\n"
              "#   lattice x y z      a move the grid repeats by (three of them)\n"
              "#   move d x y z       movement vector d\n"
              "#   cell b x y z       the centroid of cell kind b\n"
              "#   check b d x y z .. the centroids of the cells that must be empty for\n"
              "#                      that cell to move one step along vector d\n");
  dump("tetra-octa", octa());
  dump("rhombic", rhombic());
  return 0;
}
