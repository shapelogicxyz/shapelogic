// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/gridtype.cpp (the voxel and symmetry factories and the
// capabilities).

#include "shapelogic/grid/grid_type.h"

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
  switch (k) {
    case GridKind::Bricks: return "bricks";
    case GridKind::TriangularPrism: return "triangular prisms";
    case GridKind::Spheres: return "spheres";
    case GridKind::Rhombic: return "rhombic";
    case GridKind::TetraOcta: return "tetra-octa";
    case GridKind::Sliding: return "sliding";
    case GridKind::Stacking: return "stacking";
    case GridKind::Hexagonal: return "hexagonal prisms";
    case GridKind::Cairo: return "cairo prisms";
  }
  return "unknown";
}

unsigned int GridType::getCapabilities() const noexcept {
  switch (kind_) {
    case GridKind::Bricks:
    case GridKind::TriangularPrism: return kCapAssemble | kCapDisassemble | kCapStlExport;
    case GridKind::Spheres:
    case GridKind::Rhombic:
    case GridKind::TetraOcta:
    // no movement analysis yet (docs/V2-IDEAS-PLAN.md idea 4)
    case GridKind::Hexagonal:
    case GridKind::Cairo: return kCapAssemble | kCapStlExport;
    // assembling finds the start placements; a sliding search replaces take-apart
    case GridKind::Sliding: return kCapAssemble | kCapStlExport;
    // the stacking search is a solver of its own
    case GridKind::Stacking: return kCapStlExport;
  }
  return 0;
}

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
  return kind == GridKind::Cairo ? static_cast<const Symmetries&>(cairoSymmetries())
                                 : static_cast<const Symmetries&>(hexagonalSymmetries());
}
}  // namespace detail

}  // namespace bt
