// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxel_c::getMeshInternal, getDrawingMesh, getWireframeMesh
// and getFlatMesh (src/lib/voxel.cpp), voxel_2_c::getDrawingMesh and
// getWireframeMesh (src/lib/voxel_2_mesh.cpp), and getSTLMesh of voxel_c,
// voxel_0_c and voxel_2_c.
#include "bt/mesh/voxel_mesh.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/mesh/cube_mesh.h"
#include "bt/mesh/grid_geometry.h"
#include "bt/mesh/mink_mesh.h"
#include "bt/mesh/modifiers.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace bt::mesh {

namespace {

// A face through the corners in `c` (3 or 4 of them), or kNone when
// there are none.
int addCornerFace(Polyhedron& poly, VertexList& vl, const std::vector<float>& c) {
  if (c.empty()) {
    return kNone;
  }
  BT_ASSERT(c.size() == 9 || c.size() == 12);
  std::vector<int> corners;
  for (std::size_t i = 0; i < c.size(); i += 3) {
    corners.push_back(vl.get(c[i], c[i + 1], c[i + 2]));
  }
  return poly.addFace(corners);
}

}  // namespace

Polyhedron cellMesh(const Voxel& v, double bevel, double offset, bool fast) {
  Polyhedron res;
  {
    VertexList vl(res);
    std::vector<float> faceCorners;
    std::vector<float> faceCorners2;

    // too small a bevel or gap is none
    if (bevel < 1e-5) {
      bevel = 0;
    }
    if (offset < 1e-5) {
      offset = 0;
    }

    for (unsigned int z = 0; z < v.getZ(); z++) {
      for (unsigned int y = 0; y < v.getY(); y++) {
        for (unsigned int x = 0; x < v.getX(); x++) {
          if (v.isEmpty(x, y, z)) {
            continue;
          }
          const int ix = asInt(x);
          const int iy = asInt(y);
          const int iz = asInt(z);
          int nx = 0;
          int ny = 0;
          int nz = 0;

          // the fast mesh skips cells without an empty neighbour
          if (fast) {
            bool hasEmptyN = false;
            for (unsigned int n = 0; v.getNeighbor(n, 0, ix, iy, iz, &nx, &ny, &nz); n++) {
              if (v.isEmpty2(nx, ny, nz)) {
                hasEmptyN = true;
                break;
              }
            }
            if (!hasEmptyN) {
              continue;
            }
          }

          const std::uint32_t type = ((x + y + z) & 1) == 0 ? kFaceColorLight : 0;
          const std::uint32_t idx = v.getIndex(x, y, z);
          const auto colour = static_cast<std::uint8_t>(v.getColor(x, y, z));

          // the bevel faces along the cell's edges and corners
          if (bevel > 0) {
            int n = 1;
            do {
              faceCorners.clear();
              connectionFace(v, ix, iy, iz, -n, bevel, offset, faceCorners);
              const int f = addCornerFace(res, vl, faceCorners);
              n++;
              if (f != kNone) {
                Face& ff = res.face(f);
                ff.flags = kFaceWireframe | type | kFaceBevel;
                ff.voxel = idx;
                ff.voxelFace = -1;
                ff.colour = colour;
              }
            } while (!faceCorners.empty());
          }

          for (unsigned int n = 0; v.getNeighbor(n, 0, ix, iy, iz, &nx, &ny, &nz); n++) {
            if (v.isEmpty2(nx, ny, nz)) {
              // a face towards an empty neighbour
              faceCorners.clear();
              connectionFace(v, ix, iy, iz, asInt(n), bevel, offset, faceCorners);
              BT_ASSERT(!faceCorners.empty());
              const int f = addCornerFace(res, vl, faceCorners);
              Face& ff = res.face(f);
              ff.flags = type;
              if (v.isVariable(x, y, z)) {
                ff.flags |= kFaceVariableMark;
              }
              ff.voxel = idx;
              ff.voxelFace = asInt(n);
              ff.colour = colour;
            } else if (!fast && offset > 0 &&
                       (nx > ix || (nx == ix && ny > iy) || (nx == ix && ny == iy && nz > iz))) {
              // The gap to a filled neighbour (once per pair): a ring of
              // faces from this cell's face to the neighbour's face back.
              // Which neighbour this cell is of the neighbour:
              unsigned int n2 = 0;
              bool found = false;
              int mx = 0;
              int my = 0;
              int mz = 0;
              while (v.getNeighbor(n2, 0, nx, ny, nz, &mx, &my, &mz)) {
                if (mx == ix && my == iy && mz == iz) {
                  found = true;
                  break;
                }
                n2++;
              }
              BT_ASSERT(found);

              faceCorners.clear();
              connectionFace(v, ix, iy, iz, asInt(n), bevel, offset, faceCorners);
              faceCorners2.clear();
              connectionFace(v, nx, ny, nz, asInt(n2), bevel, offset, faceCorners2);

              BT_ASSERT(faceCorners.size() == faceCorners2.size());
              BT_ASSERT(faceCorners.size() % 3 == 0);

              const std::size_t corners = faceCorners.size() / 3;
              for (std::size_t i = 0; i < corners; i++) {
                const std::size_t f1c1 = 3 * i;
                const std::size_t f1c2 = 3 * ((i + 1) % corners);
                const std::size_t f2c1 = 3 * ((corners - i) % corners);
                const std::size_t f2c2 = 3 * ((corners - i + corners - 1) % corners);

                const std::vector<int> face4 = {
                    vl.get(faceCorners[f1c1], faceCorners[f1c1 + 1], faceCorners[f1c1 + 2]),
                    vl.get(faceCorners[f1c2], faceCorners[f1c2 + 1], faceCorners[f1c2 + 2]),
                    vl.get(faceCorners2[f2c2], faceCorners2[f2c2 + 1], faceCorners2[f2c2 + 2]),
                    vl.get(faceCorners2[f2c1], faceCorners2[f2c1 + 1], faceCorners2[f2c1 + 2]),
                };
                const int f = res.addFace(face4);
                Face& ff = res.face(f);
                ff.flags = kFaceWireframe | type | kFaceOffset;
                ff.voxel = idx;
                ff.voxelFace = -1;
                ff.colour = colour;
              }
            }
          }
        }
      }
    }
  }

  if (!fast) {
    res.finalize();
  }
  return res;
}

Polyhedron shapeMesh(const Voxel& v, MeshStyle style) {
  if (v.getGridType().getType() == GridKind::Spheres) {
    if (style == MeshStyle::Flat) {
      return cellMesh(v, 0, 0, false);
    }
    if (style == MeshStyle::Stl) {
      // sphere radius, connection radius, round, offset, recursion, inner
      // radius, hole diameter
      return sphereMesh(v, 0.5F, 0.7F, 1, 0.02F, 2, 0, 0, false);
    }
    // sphere radius, connection radius, round, offset, recursion, inner
    // radius, hole diameter
    return sphereMesh(v, 0.5F, 0.7F, 1, 0.01F, 1, 0, 0, true);
  }
  switch (style) {
    case MeshStyle::Drawing:
      return cellMesh(v, 0.03, 0.005, true);
    case MeshStyle::Wireframe: {
      Polyhedron p = cellMesh(v, 0.03, 0.005, false);
      fillPolyhedronHoles(p, true);
      return p;
    }
    case MeshStyle::Flat:
      return cellMesh(v, 0, 0, false);
    case MeshStyle::Stl: {
      // the defaults in cell units: gap 0.02, bevel 0.05, with the interior
      // chamfers. A draw path must not fail: where the mesher does, the
      // flat mesh of the edge-line style is shown (the original also said
      // why on stderr).
      const GridKind kind = v.getGridType().getType();
      Result<Polyhedron> p = fail(Errc::Unsupported, "grid without a chamfer mesher");
      if (kind == GridKind::Bricks || kind == GridKind::Sliding || kind == GridKind::Stacking) {
        p = cubePolyhedron(v, 0.02, 0.05, true);
      } else if (minkHandles(v)) {
        p = minkPolyhedron(v, 0.02, 0.05, true);
      }
      if (p) {
        return std::move(*p);
      }
      return cellMesh(v, 0, 0, false);
    }
  }
  BT_ASSERT_MSG(false, "unknown mesh style");
  return {};
}

}  // namespace bt::mesh
