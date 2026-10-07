// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The three symmetry table sets (internal to the grid module; use
/// GridType::getSymmetries()).

#include "bt/grid/symmetries.h"

namespace bt::detail {

/// Cubes, and the rhombic and tetra-octa grids built on cubes (tabs_0).
const Symmetries& symmetriesTable0();
/// Triangular prisms (tabs_1).
const Symmetries& symmetriesTable1();
/// Spheres (tabs_2).
const Symmetries& symmetriesTable2();

}  // namespace bt::detail
