// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/gridtype.cpp (the voxel and symmetry factories and the
// capabilities).

#include "shapelogic/grid/grid_type.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/grid_traits.h"
#include "shapelogic/grid/symmetries_tables.h"
#include "shapelogic/grid/voxel_bricks.h"
#include "shapelogic/grid/voxel_prisms.h"
#include "shapelogic/grid/voxel_rhombic.h"
#include "shapelogic/grid/voxel_spheres.h"
#include "shapelogic/grid/voxel_tetra_octa.h"
#include "shapelogic/grid/voxel_hex.h"
#include "shapelogic/grid/voxel_cairo.h"
#include "shapelogic/grid/symmetries_derived.h"

namespace bt {

std::string_view toString(GridKind k) noexcept {
  if (!detail::isGridKind(k)) {
    return "unknown";
  }
  return gridTraits(k).label;
}

unsigned int GridType::getCapabilities() const noexcept { return gridTraits(kind_).capabilities; }

std::unique_ptr<Voxel> GridType::makeVoxel(unsigned int x, unsigned int y, unsigned int z,
                                           VoxelType init) const {
  switch (kind_) {
    case GridKind::TriangularPrism: return std::make_unique<VoxelPrisms>(x, y, z, *this, init);
    case GridKind::Spheres: return std::make_unique<VoxelSpheres>(x, y, z, *this, init);
    case GridKind::Rhombic: return std::make_unique<VoxelRhombic>(x, y, z, *this, init);
    case GridKind::TetraOcta: return std::make_unique<VoxelTetraOcta>(x, y, z, *this, init);
    case GridKind::Hexagonal: return std::make_unique<VoxelHex>(x, y, z, *this, init);
    case GridKind::Cairo: return std::make_unique<VoxelCairo>(x, y, z, *this, init);
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking: break;
  }
  return std::make_unique<VoxelBricks>(x, y, z, *this, init);
}

namespace detail {
const Symmetries& derivedSymmetries(GridKind kind) {
  switch (kind) {
    case GridKind::Hexagonal: return hexagonalSymmetries();
    case GridKind::Cairo: return cairoSymmetries();
    // the original's grids have tables (GridType::getSymmetries)
    case GridKind::Bricks:
    case GridKind::TriangularPrism:
    case GridKind::Spheres:
    case GridKind::Rhombic:
    case GridKind::TetraOcta:
    case GridKind::Sliding:
    case GridKind::Stacking: break;
  }
  SL_ASSERT_MSG(false, "the grid's symmetries are not derived");
  return hexagonalSymmetries();
}
}  // namespace detail

}  // namespace bt
