// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_symmetries.cpp of the original.

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/symmetries_2.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/puzzle.h"
#include "../model/model_test_helpers.h"
#include "grid_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace bt;
using namespace bt::test;

namespace {

/**
 * A deliberately lopsided shape with a trivial rotation stabiliser,
 * including mirrors: no non-identity transformation maps it onto itself.
 *
 * Note: the brief's original fixture here ((0,0,0),(1,0,0),(0,1,0),(0,0,1)
 * -- an L-tromino plus one cube stacked on top of the corner) is NOT
 * actually asymmetric: it has one non-empty cell one step out along each
 * of the three axes from a shared corner, so it is invariant under the
 * 120-degree rotation about the corner's space diagonal that cyclically
 * permutes x/y/z. ShapeLogic correctly detects that symmetry
 * (selfSymmetries comes back non-trivial for it), so using it would have
 * made a test that relies on trivial symmetry assert something false.
 * This shape is a genuine spiral with no repeated arm lengths, which has
 * no non-trivial self symmetry under the cube's rotation group.
 */
std::unique_ptr<Voxel> lopsidedSpiral(const GridType& gt) {
  return fromLayers(gt, {
    { "#.",
      ".." },
    { "##",
      ".." },
    { ".#",
      ".#" },
  });
}

} // namespace

TEST_CASE("symmetries: every grid reports a transformation count", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);
    const Symmetries& sym = gt.getSymmetries();

    const unsigned int plain  = sym.getNumTransformations();
    const unsigned int mirror = sym.getNumTransformationsMirror();

    REQUIRE(plain > 0);
    /* the mirrored count includes the plain ones, so it is never smaller */
    REQUIRE(mirror >= plain);
  }
}

TEST_CASE("symmetries: transformation 0 is the identity", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);

    /* a 2-cell domino has a rotation stabiliser of order 8: transform(0)
       could secretly apply any of those 8 rotations and this case would
       still pass. lopsidedSpiral has a TRIVIAL stabiliser (including
       mirrors), so transform(0) producing anything other than the true
       identity would show up as a mismatch below. */
    std::unique_ptr<Voxel> v = lopsidedSpiral(gt);

    std::unique_ptr<Voxel> copy = v->clone();

    REQUIRE(copy->transform(0));

    /* GT_RHOMBIC and GT_TETRA_OCTA pad the voxel space to a multiple of an
       internal cell size (5 / 6 respectively) before any transform,
       including the identity, so the space can grow; the content within
       the bounding box is still untouched, so compare with identicalInBB
       rather than ==.

       KNOWN LIMITATION, GT_TRIANGULAR_PRISM and GT_SPHERES only:
       voxel_1_c::transform() (src/lib/voxel_1.cpp:47) and
       voxel_2_c::transform() (src/lib/voxel_2.cpp:33) both open with
       `if (nr == 0) return true;` -- for these two grids transform(0) is a
       hardcoded no-op that never reaches the rotation math at all. No
       choice of fixture can turn this REQUIRE into a real check of
       "transform 0 behaves as the identity" on those two grids: the code
       guarantees `v` is untouched before this assertion even runs, so the
       assertion below is trivially true there by construction. It stays
       in the loop (rather than being skipped) so the case still documents
       the actual, if trivial, per-grid behaviour instead of silently
       narrowing ALL_GRIDS down to three. */
    REQUIRE(copy->identicalInBB(*v));
  }
}

TEST_CASE("symmetries: transAdd composes transformations", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);
    const Symmetries& sym = gt.getSymmetries();

    const unsigned int n = sym.getNumTransformations();

    /* applying t1 then t2 must equal applying transAdd(t1, t2) in one go,
       whenever the composed transformation exists.

       The fixture is built only from cells with (x+y+z) even, because the
       spheres grid (GT_SPHERES) only has sphere centres on that half of a
       cubic lattice; the other cells are not valid sphere positions, and
       voxel_2_c::transform() silently drops filled cells that don't land on
       one (src/lib/voxel_2.cpp, the `if (((x+y+z) & 1) == 0)` guard around
       the per-voxel rotation loop). Using only even-sum cells keeps the
       fixture meaningful on every grid, spheres included.

       KNOWN COVERAGE GAP, GT_SPHERES only: this fixture only survives
       voxel_2_c::transform() for transformations 0-23 (the 24 plain cube
       rotations; transMult rows 0-23 are exactly the ones with no TND
       entries). Transformations 24-119 either fail transform() on this
       fixture's cell layout or land on a TND composition, so they
       get NO composition coverage here at all -- the 576 sphere pairs
       actually asserted below are precisely t1,t2 in {0..23}, already
       covered three times over by GT_BRICKS/GT_RHOMBIC/GT_TETRA_OCTA.
       Building an FCC-aligned fixture to close this gap is out of scope
       for this task; this comment exists so nobody mistakes the loop
       bound `n` for actual coverage of all `n` sphere transformations. */
    unsigned int checked = 0;
    for (unsigned int t1 = 0; t1 < n; t1++)
      for (unsigned int t2 = 0; t2 < n; t2++) {
        unsigned char combined = sym.transAdd(static_cast<unsigned char>(t1), static_cast<unsigned char>(t2));
        if (combined == kTransformUndefined) continue;

        INFO("t1 " << t1 << " t2 " << t2 << " combined " << static_cast<int>(combined));

        std::unique_ptr<Voxel> stepwise = fromLayers(gt, {
          { "#.#",
            ".#.",
            "..." },
        });
        std::unique_ptr<Voxel> direct = stepwise->clone();

        if (!stepwise->transform(t1)) continue;
        if (!stepwise->transform(t2)) continue;
        if (!direct->transform(combined)) continue;

        checked++;
        REQUIRE(stepwise->identicalInBB(*direct));
      }

    /* REQUIRE(checked > 0) alone floors this branch's most valuable
       coverage at a single assertion: the continues just above skip
       silently on any transform() failure, so a regression that made
       transform() always return false would collapse `checked` from
       thousands down to 1 and this case would still go green. Pin the
       measured value exactly, so a silent collapse in coverage fails
       loudly instead of passing quietly.
       Measured directly from this test body (printf, three repeated runs,
       all identical -- the loop is purely deterministic, no RNG involved)
       before being encoded here. GT_BRICKS, GT_SPHERES, GT_RHOMBIC and
       GT_TETRA_OCTA all measure 576 (n=24, and voxel_0_c::transform, which
       GT_RHOMBIC/GT_TETRA_OCTA delegate to after padding, never fails);
       GT_TRIANGULAR_PRISM measures fewer because voxel_1_c::transform can
       legitimately return false (src/lib/voxel_1.cpp:429). */
    const unsigned int expectedChecked =
      (t == GridKind::TriangularPrism) ? 144u : 576u;
    REQUIRE(checked == expectedChecked);
  }
}

TEST_CASE("symmetries: a shape is identical to its own rotations", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);
    const Symmetries& sym = gt.getSymmetries();

    /* built only from (x+y+z)-even cells; see the note in the transAdd
       case above on why the fixture must avoid the sphere grid's invalid
       lattice positions */
    std::unique_ptr<Voxel> original = fromLayers(gt, {
      { "#.#",
        ".#.",
        "..." },
    });

    for (unsigned int tr = 0; tr < sym.getNumTransformations(); tr++) {
      INFO("transformation " << tr);

      std::unique_ptr<Voxel> rotated = original->clone();
      if (!rotated->transform(tr)) continue;

      /* a rotation never adds or removes material */
      REQUIRE(rotated->countState(Voxel::kFilled)
              == original->countState(Voxel::kFilled));

      /* and the shape is still recognisably the same one */
      REQUIRE(original->identicalWithRots(*rotated, false, false));
    }
  }
}

TEST_CASE("symmetries: identicalWithRots is colour-sensitive", "[symmetry][color]") {
  GridType gt(GridKind::Bricks);

  /* lopsidedSpiral has a trivial rotation stabiliser, including mirrors:
     no non-identity transform maps it onto itself geometrically. That
     means transform 0 is the ONLY transform under which these two copies
     can possibly agree, which isolates the includeColors flag as the only
     thing that can flip the result below -- a coincidental geometric match
     at some other transform cannot smuggle a false positive/negative in. */
  std::unique_ptr<Voxel> original = lopsidedSpiral(gt);
  std::unique_ptr<Voxel> recoloured = original->clone();
  recoloured->setColor(0, 0, 0, 1);

  /* state (geometry) alone still matches, via transform 0 */
  REQUIRE(original->identicalWithRots(*recoloured, false, false));
  /* with colours included, no transform makes the two agree */
  REQUIRE_FALSE(original->identicalWithRots(*recoloured, false, true));
}

TEST_CASE("symmetries: a cube is symmetric under every transformation", "[symmetry]") {
  /* deliberately GT_BRICKS only, unlike the ALL_GRIDS loop its siblings
     use: a filled 2x2x2 space is a geometric cube only under GT_BRICKS. On
     the other four grids the same voxel extents describe a lattice
     fragment (a rhombic/tetrahedral-octahedral/etc. cell cluster) with no
     such symmetry, so iterating ALL_GRIDS here would assert something
     FALSE. Do not "fix" this into an ALL_GRIDS loop. */
  GridType gt(GridKind::Bricks);
  const Symmetries& sym = gt.getSymmetries();

  std::unique_ptr<Voxel> cube = makeVoxel(gt, 2, 2, 2);
  cube->setAll(Voxel::kFilled);

  const Symmetry s = cube->selfSymmetries();

  /* a solid cube maps onto itself under every rotation the grid has */
  for (unsigned int tr = 0; tr < sym.getNumTransformations(); tr++) {
    INFO("transformation " << tr);
    REQUIRE(sym.symmetrieContainsTransformation(s, tr));
  }
}

TEST_CASE("symmetries: an asymmetric shape has the trivial symmetry group", "[symmetry]") {
  GridType gt(GridKind::Bricks);

  /* see lopsidedSpiral's doc comment above for why this particular shape,
     and not the brief's original fixture, is the one that is genuinely
     asymmetric. */
  std::unique_ptr<Voxel> lopsided = lopsidedSpiral(gt);

  const Symmetry s = lopsided->selfSymmetries();

  /* transformation 0 always maps a shape onto itself */
  REQUIRE(gt.getSymmetries().symmetrieContainsTransformation(s, 0));
  REQUIRE(unSymmetric(s));
}

TEST_CASE("symmetries: normalizeTransformation returns a usable transformation", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);

    /* built only from (x+y+z)-even cells; see the note in the transAdd
       case above on why the fixture must avoid the sphere grid's invalid
       lattice positions, now that this case applies transform() itself */
    std::unique_ptr<Voxel> v = fromLayers(gt, {
      { "#.#",
        ".#.",
        "..." },
    });

    const unsigned int n = gt.getSymmetries().getNumTransformationsMirror();

    for (unsigned int tr = 0; tr < n; tr++) {
      const unsigned char norm = v->normalizeTransformation(static_cast<unsigned char>(tr));
      INFO("transformation " << tr << " normalized to " << static_cast<int>(norm));
      REQUIRE(norm < n);

      /* REQUIRE(norm < n) alone is satisfied by ANY in-range value,
         including a stub `return 0;`. voxel.h:573-575 documents the real
         contract: norm must be the smallest transformation number that
         results in a shape IDENTICAL to applying tr. Assert that directly:
         transforming one copy by tr and another by norm must land on the
         same shape. norm <= tr is also part of "smallest", but by itself
         it is satisfied by the same `return 0;` stub, so it is checked in
         addition to, never instead of, the identity check. */
      REQUIRE(norm <= tr);

      std::unique_ptr<Voxel> byTr = v->clone();
      if (!byTr->transform(tr)) continue;

      std::unique_ptr<Voxel> byNorm = v->clone();
      REQUIRE(byNorm->transform(norm));

      REQUIRE(byTr->identicalInBB(*byNorm));
    }
  }
}

// ---- the sphere grid's symmetry registry ---------------------------------------
// From the original's 95b49d32.

namespace {

// The generated tables, read a second time here so the run-time derivation
// can be checked against them entry by entry.
namespace sphere_tables {

constexpr unsigned int kNt = detail::kSphereTransformations;
constexpr unsigned int kNg = 241;

constexpr detail::SphereSymmetryList kSymmetries[kNg] = {
#include "shapelogic/grid/tabs_2/symmetries.inc"
};
constexpr detail::SphereSymmetryList kUnified[kNg] = {
#include "shapelogic/grid/tabs_2/unifiedsym.inc"
};
constexpr detail::SphereSymmetryList kUnique[kNg] = {
#include "shapelogic/grid/tabs_2/uniquesym.inc"
};
constexpr unsigned char kMinimizer[kNg][kNt] = {
#include "shapelogic/grid/tabs_2/transformmini.inc"
};

}  // namespace sphere_tables

// The filled cells the grid accepts as coordinates.
unsigned int countValidFilled(const Voxel& v) {
  unsigned int n = 0;
  for (unsigned int x = 0; x < v.getX(); x++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int z = 0; z < v.getZ(); z++) {
        if (v.validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) &&
            v.getState(x, y, z) == Voxel::kFilled) {
          n++;
        }
      }
    }
  }
  return n;
}

// Check every answer the symmetry tables give for one shape against the
// shape itself: transform it every way the grid allows and compare.
void checkSymmetryTables(const GridType& gt, const Voxel& shape) {
  const Symmetries& sym = gt.getSymmetries();
  const Symmetry s = shape.selfSymmetries();
  const unsigned int nt = sym.getNumTransformationsMirror();

  // the shape in every orientation the grid can hold, null where it cannot
  std::vector<std::unique_ptr<Voxel>> orient(nt);
  for (unsigned int t = 0; t < nt; t++) {
    std::unique_ptr<Voxel> v = shape.clone();
    if (v->transform(t)) {
      orient[t] = std::move(v);
    }
  }
  auto same = [&](unsigned int a, unsigned int b) {
    return orient[a] && orient[b] && orient[a]->identicalInBB(*orient[b]);
  };

  bool mirror = false;
  for (unsigned int t = 0; t < nt; t++) {
    INFO("transformation " << t);
    const bool self = orient[t] && shape.identicalInBB(*orient[t]);
    REQUIRE(sym.symmetrieContainsTransformation(s, t) == self);
    if (self && t >= sym.getNumTransformations()) {
      mirror = true;
    }
  }
  REQUIRE(sym.symmetryContainsMirror(s) == mirror);

  for (unsigned int t = 0; t < nt; t++) {
    if (!orient[t]) {
      continue;
    }
    INFO("transformation " << t);

    // exactly one of the unique transformations gives this orientation
    unsigned int uniques = 0;
    for (unsigned int u = 0; u < nt; u++) {
      if (sym.isTransformationUnique(s, u) && same(u, t)) {
        uniques++;
      }
    }
    REQUIRE(uniques == 1);

    // the minimizer names the lowest transformation with this orientation
    const unsigned int m = sym.minimizeTransformation(s, static_cast<unsigned char>(t));
    REQUIRE(m <= t);
    REQUIRE(same(m, t));
    REQUIRE(sym.minimizeTransformation(s, static_cast<unsigned char>(m)) == m);
    for (unsigned int lower = 0; lower < m; lower++) {
      REQUIRE_FALSE(same(lower, t));
    }
  }
}

}  // namespace

TEST_CASE("spheres: run-time table derivation reproduces the generated tables",
          "[symmetry][spheres]") {
  using namespace sphere_tables;

  GridType gt(GridKind::Spheres);
  const Symmetries& sym = gt.getSymmetries();
  REQUIRE(sym.getNumTransformationsMirror() == kNt);

  for (unsigned int g = 0; g < kNg; g++) {
    INFO("symmetry list " << g);

    detail::SphereSymmetryList uni;
    detail::SphereSymmetryList unq;
    unsigned char mini[kNt];
    detail::deriveSphereTables(kSymmetries[g], uni, unq, mini);
    REQUIRE(uni == kUnified[g]);
    REQUIRE(unq == kUnique[g]);
    REQUIRE(std::equal(mini, mini + kNt, kMinimizer[g]));

    // and the grid hands out the generated entries unchanged
    const auto s = static_cast<Symmetry>(g);
    for (unsigned int t = 0; t < kNt; t++) {
      REQUIRE(sym.symmetrieContainsTransformation(s, t) == kSymmetries[g].get(t));
      REQUIRE(sym.isTransformationUnique(s, t) == kUnique[g].get(t));
      REQUIRE(sym.minimizeTransformation(s, static_cast<unsigned char>(t)) == kMinimizer[g][t]);
    }
    const unsigned int other = (g * 7 + 3) % kNg;
    REQUIRE(sym.countSymmetryIntersection(s, static_cast<Symmetry>(other)) ==
            (kUnified[g] & kSymmetries[other]).countBits());
  }
}

TEST_CASE("symmetries: the tables answer like the shape itself", "[symmetry]") {
  for (GridKind t : kAllGrids) {
    INFO("grid " << toString(t));
    GridType gt(t);

    // the spiral is drawn on cube coordinates; where some of them are not
    // grid positions (spheres) its transformed copies drop them and the
    // comparison would be against a different shape
    std::unique_ptr<Voxel> spiral = lopsidedSpiral(gt);
    if (countValidFilled(*spiral) == spiral->countState(Voxel::kFilled)) {
      checkSymmetryTables(gt, *spiral);
    }
    for (unsigned int cells = 1; cells <= 9; cells += 2) {
      INFO("legal shape with " << cells << " cells");
      std::unique_ptr<Voxel> v = legalShape(gt, kLegalShapeBox, cells);
      REQUIRE(v != nullptr);
      checkSymmetryTables(gt, *v);
    }
  }
}

TEST_CASE("spheres: a shape outside the generated symmetry lists gets its own entry",
          "[symmetry][spheres]") {
  // The orange piece of this puzzle (shape 2) maps onto itself under 12
  // transformations, a list the generator never found. Solving the puzzle
  // failed the assertion in calculateSymmetry().
  const Puzzle p = test::loadFixture("test_sphere_symmetry.xmpuzzle");
  REQUIRE(p.getGridType().getType() == GridKind::Spheres);
  const GridType& gt = p.getGridType();

  const Voxel& orange = p.getShape(2);
  REQUIRE(orange.getName() == "orange");
  REQUIRE(detail::sphereSymmetryList(orange).countBits() == 12);

  const unsigned int before = detail::numSphereSymmetryLists();
  const Symmetry s = orange.selfSymmetries();
  REQUIRE(s >= sphere_tables::kNg);
  REQUIRE(s < detail::numSphereSymmetryLists());

  // the same list, asked for again, gets the same number
  std::unique_ptr<Voxel> copy = orange.clone();
  REQUIRE(copy->selfSymmetries() == s);
  REQUIRE(gt.getSymmetries().calculateSymmetry(orange) == s);
  REQUIRE(detail::numSphereSymmetryLists() == before + (s == before ? 1 : 0));

  for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
    INFO("shape " << i << " " << p.getShape(i).getName());
    checkSymmetryTables(gt, p.getShape(i));
  }
}
