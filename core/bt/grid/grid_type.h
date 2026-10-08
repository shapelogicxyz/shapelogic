// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The grid a puzzle is built on, and the factory for its voxel spaces.
/// Ported from src/lib/gridtype.{h,cpp} (the assembler, movement-cache and
/// STL-exporter factories move to the stages that port those parts; XML
/// reading and writing lives in bt/io).

#include "bt/grid/types.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace bt {

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
  Sliding = 5,          ///< 2D brick tray for sliding puzzles (not ported yet)
  Stacking = 6,         ///< discs on rods (not ported yet)
};

/// The number of grid kinds; `type` attributes at or above it are invalid.
inline constexpr int kNumGridKinds = 7;

/// A readable name, e.g. "bricks", for messages and tests.
[[nodiscard]] std::string_view toString(GridKind k) noexcept;

/// What the original program can do with a grid (gridType_c::capabilities).
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

}  // namespace bt
