// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The sphere grid's symmetry lists (internal to the grid module, and its
/// tests; use GridType::getSymmetries()). Ported from src/lib/symmetries_2.h.
///
/// The transformations of the sphere grid do not form a group: many
/// compositions leave the grid, so a shape's symmetry list is not a
/// subgroup, and the generated table of 241 lists is only what the
/// generator happened to find. A shape with a list outside it failed the
/// assertion in calculateSymmetry(). The lists now live in a registry
/// seeded from the generated tables: an unseen list is appended, with its
/// derived tables worked out by the generator's rules (deriveSphereTables),
/// and gets the next free number. Numbers above the generated ones depend
/// on the order shapes are met in, so they are never saved.

#include "shapelogic/grid/bitfield.h"

namespace bt {
class Voxel;
}

namespace bt::detail {

/// All transformations of the sphere grid, mirrors included.
inline constexpr unsigned int kSphereTransformations = 240;

/// One symmetry list of the sphere grid: bit t set for transformation t.
using SphereSymmetryList = Bitfield<kSphereTransformations>;

/// The tables that belong to one symmetry list, as the generator in the
/// original's tabs_2/generator_2.cpp works them out: `unified` (the
/// transformations that map some orientation of the shape onto itself),
/// `unique` (one transformation for each distinct orientation) and
/// `minimizer` (for each transformation the lowest one with the same
/// orientation; kSphereTransformations entries).
void deriveSphereTables(const SphereSymmetryList& sym, SphereSymmetryList& unified,
                        SphereSymmetryList& unique, unsigned char* minimizer);

/// The symmetry list of one sphere shape: the transformations that map it
/// onto itself.
[[nodiscard]] SphereSymmetryList sphereSymmetryList(const Voxel& v);

/// How many symmetry lists the registry holds, generated and appended.
[[nodiscard]] unsigned int numSphereSymmetryLists();

}  // namespace bt::detail
