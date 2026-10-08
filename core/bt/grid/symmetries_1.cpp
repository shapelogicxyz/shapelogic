// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/symmetries_1.cpp: the symmetry tables for triangular prisms.

#include "bt/grid/symmetries_tables.h"

#include "bt/grid/bitfield.h"
#include "bt/grid/symmetries_impl.h"
#include "bt/grid/voxel.h"

#include <cstdint>

namespace bt::detail {
namespace {

#include "bt/grid/tabs_1/tablesizes.inc"

constexpr unsigned int kTransMult[NUM_TRANSFORMATIONS_MIRROR][NUM_TRANSFORMATIONS_MIRROR] = {
#include "bt/grid/tabs_1/transmult.inc"
};

constexpr std::uint64_t kSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "bt/grid/tabs_1/symmetries.inc"
};

constexpr std::uint64_t kUnifiedSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "bt/grid/tabs_1/unifiedsym.inc"
};

constexpr unsigned char kTransformationMinimizer[NUM_SYMMETRY_GROUPS][NUM_TRANSFORMATIONS_MIRROR] = {
#include "bt/grid/tabs_1/transformmini.inc"
};

constexpr std::uint64_t kUniqueSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "bt/grid/tabs_1/uniquesym.inc"
};

Symmetry calculate(const Voxel& pp) {
  // the original tree ignores whether the transformation succeeded; on these grids it always does
  auto same = [&pp](unsigned int t) {
    auto v = pp.clone();
    v->transform(t);
    return pp.identicalInBB(*v);
  };
#include "bt/grid/tabs_1/symcalc.inc"
}

using Impl = TableSymmetries<std::uint64_t, NUM_TRANSFORMATIONS, NUM_TRANSFORMATIONS_MIRROR,
                             NUM_SYMMETRY_GROUPS, unsigned int>;

constinit const Impl kInstance(Impl::Tables{kTransMult, kSymmetries, kUnifiedSymmetries,
                                            kTransformationMinimizer, kUniqueSymmetries},
                               &calculate);

}  // namespace

constinit const Symmetries* const kSymmetriesTable1 = &kInstance;

}  // namespace bt::detail
