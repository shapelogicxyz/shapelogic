// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/symmetries_2.cpp: the symmetry tables for spheres.

#include "shapelogic/grid/symmetries_tables.h"

#include "shapelogic/grid/bitfield.h"
#include "shapelogic/grid/symmetries_impl.h"
#include "shapelogic/grid/voxel.h"

#include <cstdint>

namespace bt::detail {
namespace {

// "undefined": the tables' value for combinations that do not exist
constexpr unsigned char TND = kTransformUndefined;  // NOLINT(readability-identifier-naming)

#include "shapelogic/grid/tabs_2/tablesizes.inc"

constexpr unsigned char kTransMult[NUM_TRANSFORMATIONS_MIRROR][NUM_TRANSFORMATIONS_MIRROR] = {
#include "shapelogic/grid/tabs_2/transmult.inc"
};

constexpr Bitfield<240> kSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/symmetries.inc"
};

constexpr Bitfield<240> kUnifiedSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/unifiedsym.inc"
};

constexpr unsigned char kTransformationMinimizer[NUM_SYMMETRY_GROUPS][NUM_TRANSFORMATIONS_MIRROR] = {
#include "shapelogic/grid/tabs_2/transformmini.inc"
};

constexpr Bitfield<240> kUniqueSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/uniquesym.inc"
};

Symmetry calculate(const Voxel& pp) {
  // a failed transformation counts as "not the same" (as in the original tabs_2 tree)
  auto same = [&pp](unsigned int t) {
    auto v = pp.clone();
    return v->transform(t) && pp.identicalInBB(*v);
  };
#include "shapelogic/grid/tabs_2/symcalc.inc"
}

using Impl = TableSymmetries<Bitfield<240>, NUM_TRANSFORMATIONS, NUM_TRANSFORMATIONS_MIRROR,
                             NUM_SYMMETRY_GROUPS, unsigned char>;

constinit const Impl kInstance(Impl::Tables{kTransMult, kSymmetries, kUnifiedSymmetries,
                                            kTransformationMinimizer, kUniqueSymmetries},
                               &calculate);

}  // namespace

constinit const Symmetries* const kSymmetriesTable2 = &kInstance;

}  // namespace bt::detail
