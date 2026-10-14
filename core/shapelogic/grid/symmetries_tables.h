// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The three symmetry table sets (internal to the grid module; use
/// GridType::getSymmetries()). Constant-initialised pointers, so that
/// getSymmetries() is an inline load (the rotation checks call it per
/// assembly and piece).

#include "shapelogic/grid/symmetries.h"

namespace bt::detail {

/// Cubes, and the rhombic and tetra-octa grids built on cubes (tabs_0).
extern const Symmetries* const kSymmetriesTable0;
/// Triangular prisms (tabs_1).
extern const Symmetries* const kSymmetriesTable1;
/// Spheres (tabs_2).
extern const Symmetries* const kSymmetriesTable2;

}  // namespace bt::detail
