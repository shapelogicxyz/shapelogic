// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/converter.cpp.

#include "shapelogic/model/grid_convert.h"

#include "shapelogic/grid/voxel.h"

#include <memory>
#include <utility>

namespace bt {

namespace {

// The cells a cube becomes along each axis.
unsigned int scaleOf(GridKind target) {
  return target == GridKind::Rhombic ? 5 : 3;
}

}  // namespace

bool canConvertGrid(GridKind src, GridKind dst) noexcept {
  return (src == GridKind::Bricks && dst == GridKind::Rhombic) ||
         (src == GridKind::Bricks && dst == GridKind::TetraOcta);
}

std::vector<GridKind> convertTargets(GridKind src) {
  std::vector<GridKind> out;
  for (int i = 0; i < kNumGridKinds; i++) {
    const auto g = static_cast<GridKind>(i);
    if (canConvertGrid(src, g)) {
      out.push_back(g);
    }
  }
  return out;
}

Result<Puzzle> convertGrid(const Puzzle& p, GridKind target, std::uint64_t maxCells) {
  if (!canConvertGrid(p.getGridType().getType(), target)) {
    return fail(Errc::Unsupported, "There are no grids that this puzzle can be converted to");
  }

  const GridType gt(target);
  const unsigned int f = scaleOf(target);

  std::uint64_t cells = 0;
  for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
    const Voxel& v = p.getShape(i);
    cells += std::uint64_t{v.getX()} * f * v.getY() * f * v.getZ() * f;
  }
  if (cells > maxCells) {
    return fail(Errc::InvalidArgument, "the converted shapes would be too large");
  }

  Puzzle pNew(gt);

  // convert the shapes: each cube becomes the block of cells filling it
  // (bricks to rhombic: 5x5x5, bricks to tetra-octa: 3x3x3)
  for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
    const Voxel& v = p.getShape(i);

    std::unique_ptr<Voxel> vn = gt.makeVoxel(v.getX() * f, v.getY() * f, v.getZ() * f, 0);
    vn->skipRecalcBoundingBox(true);

    for (unsigned int x = 0; x < v.getX(); x++) {
      for (unsigned int y = 0; y < v.getY(); y++) {
        for (unsigned int z = 0; z < v.getZ(); z++) {
          const VoxelType st = v.get(x, y, z);

          if (st != 0) {
            for (unsigned int ax = 0; ax < f; ax++) {
              for (unsigned int ay = 0; ay < f; ay++) {
                for (unsigned int az = 0; az < f; az++) {
                  const unsigned int cx = (f * x) + ax;
                  const unsigned int cy = (f * y) + ay;
                  const unsigned int cz = (f * z) + az;
                  if (vn->validCoordinate(static_cast<int>(cx), static_cast<int>(cy),
                                          static_cast<int>(cz))) {
                    vn->set(cx, cy, cz, st);
                  }
                }
              }
            }
          }
        }
      }
    }

    vn->skipRecalcBoundingBox(false);
    vn->setName(v.getName());
    pNew.addShape(std::move(vn));
  }

  // the colours
  for (unsigned int i = 0; i < p.colorNumber(); i++) {
    const Rgb c = p.getColor(i);
    pNew.addColor(c.r, c.g, c.b);
  }

  // the notes
  pNew.setComment(p.getComment());
  pNew.setCommentPopup(p.getCommentPopup());

  // the problems as copies of the old puzzle's
  for (unsigned int i = 0; i < p.getNumberOfProblems(); i++) {
    const Problem& prob = p.getProblem(i);
    const unsigned int n = pNew.addProblem(prob);
    pNew.getProblem(n).setName(prob.getName());
  }

  return pNew;
}

}  // namespace bt
