// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The grid a puzzle is built on, and the factory for its voxel spaces.
/// Ported from src/lib/gridtype.{h,cpp} (the assembler, movement-cache and
/// STL-exporter factories move to the stages that port those parts; XML
/// reading and writing lives in shapelogic/io).

#include "shapelogic/grid/types.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace sl {

class Symmetries;
class Voxel;

namespace detail {
// The grids' symmetry tables (symmetries_0/1/2.cpp; see symmetries_tables.h).
extern const Symmetries* const kSymmetriesTable0;
extern const Symmetries* const kSymmetriesTable1;
extern const Symmetries* const kSymmetriesTable2;
}  // namespace detail

/// The grid types. The numbers are the `type` attribute in .xmpuzzle files.
enum class GridKind : std::uint8_t {
  Bricks = 0,           ///< cubes
  TriangularPrism = 1,  ///< triangles stacked in z
  Spheres = 2,          ///< tightly packed spheres
  Rhombic = 3,          ///< a cut cube grid that builds rhombic dodecahedra
  TetraOcta = 4,        ///< tetrahedra and octahedra, also a cut cube grid
  Sliding = 5,          ///< 2D brick tray for sliding puzzles (shapelogic/model/sliding.h)
  Stacking = 6,         ///< discs on rods (shapelogic/model/stacking.h)
  Hexagonal = 7,        ///< hexagonal prisms in rows (decision 0023; not in the original)
  Cairo = 8,            ///< cairo pentagon prisms (decision 0023; not in the original)
};

/// The number of grid kinds; `type` attributes at or above it are invalid.
inline constexpr int kNumGridKinds = 9;

namespace detail {
/// True for a value the enum names. The switch has a case for every kind
/// and no default, so a kind added to the enum must add its case here
/// (-Wswitch, an error in the build); the assert below then fails until
/// kNumGridKinds counts it (cleanup item 11.3).
constexpr bool isGridKind(GridKind k) noexcept {
  switch (k) {
    case GridKind::Bricks:
    case GridKind::TriangularPrism:
    case GridKind::Spheres:
    case GridKind::Rhombic:
    case GridKind::TetraOcta:
    case GridKind::Sliding:
    case GridKind::Stacking:
    case GridKind::Hexagonal:
    case GridKind::Cairo: return true;
  }
  return false;
}
}  // namespace detail
static_assert(detail::isGridKind(static_cast<GridKind>(kNumGridKinds - 1)) &&
                  !detail::isGridKind(static_cast<GridKind>(kNumGridKinds)),
              "kNumGridKinds must be one past the last GridKind");

namespace detail {
/// The symmetries of the hexagonal and cairo grids, derived at first use
/// from their matrices (symmetries_derived.h).
const Symmetries& derivedSymmetries(GridKind kind);
}  // namespace detail

/// A readable name, e.g. "bricks", for messages and tests.
[[nodiscard]] std::string_view toString(GridKind k) noexcept;

/// What can be done with a grid (gridType_c::capabilities; the take-apart
/// on more grids than the original's: solve/movement_checks.h).
enum GridCapability : unsigned int {
  kCapAssemble = 1,     ///< the grid has an assembler
  kCapDisassemble = 2,  ///< the grid has a disassembler
  kCapStlExport = 4,    ///< the grid can export STL
};

/// A grid type. A small value type: copy it freely. Its symmetry tables are
/// shared, immutable and live for the whole program.
class GridType {
 public:
  /// The cube grid.
  constexpr GridType() noexcept = default;
  constexpr explicit GridType(GridKind kind) noexcept : kind_(kind) {}

  /// The grid kind.
  [[nodiscard]] constexpr GridKind getType() const noexcept { return kind_; }

  /// A combination of GridCapability flags.
  [[nodiscard]] unsigned int getCapabilities() const noexcept;

  /// The symmetry and transformation tables of this grid.
  [[nodiscard]] const Symmetries& getSymmetries() const noexcept {
    switch (kind_) {
      case GridKind::TriangularPrism:
        return *detail::kSymmetriesTable1;
      case GridKind::Spheres:
        return *detail::kSymmetriesTable2;
      case GridKind::Hexagonal:
      case GridKind::Cairo:
        return detail::derivedSymmetries(kind_);
      case GridKind::Bricks:
      case GridKind::Rhombic:
      case GridKind::TetraOcta:
      case GridKind::Sliding:
      case GridKind::Stacking:
        break;
    }
    return *detail::kSymmetriesTable0;
  }

  /// A new voxel space of this grid, of the given size, every cell `init`.
  [[nodiscard]] std::unique_ptr<Voxel> makeVoxel(unsigned int x, unsigned int y, unsigned int z,
                                                 VoxelType init = 0) const;

  constexpr bool operator==(const GridType&) const noexcept = default;

 private:
  GridKind kind_ = GridKind::Bricks;
};

}  // namespace sl
