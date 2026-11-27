// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What each grid kind is, in one row per kind (cleanup item 11.3): its
/// names, what can be done with it, the mesher that draws it, and the
/// editor's choices that follow from its cells. What stays per grid in its
/// own code: the voxel class, the lattice, the face polygons, the matrices,
/// the movement directions, the editor's corners and tool tables, and the
/// New dialog's entry.

#include "shapelogic/grid/grid_type.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sl {

/// The mesher that draws a grid's shapes with chamfers (the STL export's
/// and the 3D view's): cubes with bevels, the Minkowski mesher of the cut
/// and prism grids, the sphere mesher, or the uniform mesher of the new
/// grids, which have no bevel tables.
enum class GridMesher : std::uint8_t { Cubes, Minkowski, Spheres, Uniform };

/// One grid kind's row.
struct GridTraits {
  GridKind kind;
  /// The API's name (engine and Python: api/types.json's Grid).
  std::string_view apiName;
  /// A readable name, for messages and tests (toString).
  std::string_view label;
  /// GridCapability flags (GridType::getCapabilities).
  unsigned int capabilities;
  GridMesher mesher;
  /// Cells of the cube grid, as they are: the sliding and stacking grids
  /// are cubes too.
  bool cubeLike;
  /// The shape editor's scale buttons: a cell made of smaller ones.
  bool scales;
  /// Its grid scale button as well (the original's did nothing on the
  /// tetra-octa grid).
  bool gridScale;
  /// A new shape's size (the New dialog's), and the step its sizes take.
  unsigned int defaultSize;
  unsigned int sizeUnit;
};

/// Every grid kind's row, in the order of the kinds' numbers.
inline constexpr std::array<GridTraits, kNumGridKinds> kGridTraits = {{
    {GridKind::Bricks, "bricks", "bricks", kCapAssemble | kCapDisassemble | kCapStlExport,
     GridMesher::Cubes, true, true, true, 4, 1},
    {GridKind::TriangularPrism, "prisms", "triangular prisms",
     kCapAssemble | kCapDisassemble | kCapStlExport, GridMesher::Minkowski, false, true, true, 4,
     1},
    // balls have no faces to slide along: no movement analysis
    {GridKind::Spheres, "spheres", "spheres", kCapAssemble | kCapStlExport, GridMesher::Spheres,
     false, false, false, 3, 1},
    // not in the original: the cut grids, the prisms and the new grids take
    // apart with movement checks derived from their cells
    // (solve/movement_checks.h, decision 0025)
    {GridKind::Rhombic, "rhombic", "rhombic", kCapAssemble | kCapDisassemble | kCapStlExport,
     GridMesher::Minkowski, false, true, true, 5, 5},
    {GridKind::TetraOcta, "tetraOcta", "tetra-octa", kCapAssemble | kCapDisassemble | kCapStlExport,
     GridMesher::Minkowski, false, true, false, 4, 3},
    // assembling finds the start placements; a sliding search replaces
    // the take-apart
    {GridKind::Sliding, "sliding", "sliding", kCapAssemble | kCapStlExport, GridMesher::Cubes, true,
     true, true, 6, 1},
    // the stacking search is a solver of its own
    {GridKind::Stacking, "stacking", "stacking", kCapStlExport, GridMesher::Cubes, true, true, true,
     4, 1},
    // a hexagon or a cairo pentagon is not made of smaller ones: no scales
    {GridKind::Hexagonal, "hexagonal", "hexagonal prisms",
     kCapAssemble | kCapDisassemble | kCapStlExport, GridMesher::Uniform, false, false, false, 4,
     1},
    // a new shape: two blocks of the tiling's period; the pentagons repeat
    // every four cells in x and y, but a space of any size holds whole
    // cells, so the sizes take any step
    {GridKind::Cairo, "cairo", "cairo prisms", kCapAssemble | kCapDisassemble | kCapStlExport,
     GridMesher::Uniform, false, false, false, 8, 1},
}};

namespace detail {
/// True when every row is the kind of its place, so no kind lacks a row.
constexpr bool gridTraitsInOrder() {
  for (std::size_t i = 0; i < kGridTraits.size(); i++) {
    if (static_cast<std::size_t>(kGridTraits[i].kind) != i) {
      return false;
    }
  }
  return true;
}
}  // namespace detail
static_assert(detail::gridTraitsInOrder(), "kGridTraits needs a row per GridKind, in order");

/// A grid kind's row; `kind` is one of the enum's values.
[[nodiscard]] constexpr const GridTraits& gridTraits(GridKind kind) noexcept {
  return kGridTraits[static_cast<std::size_t>(kind)];
}

}  // namespace sl
