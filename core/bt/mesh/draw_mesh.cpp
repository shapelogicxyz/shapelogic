// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxelFrame_c::drawShape (src/gui/voxelframe.cpp) and buildMesh
// (src/lib/gltfexport.cpp).
#include "bt/mesh/draw_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace bt::mesh {

DrawMesh toDrawMesh(const Polyhedron& poly, bool edgeLines) {
  DrawMesh m;
  std::map<std::array<float, 6>, std::uint32_t> seen;
  bool first = true;

  const auto add = [&](const Vec3& p, const Vec3& n) {
    const std::array<float, 6> key{p.x, p.y, p.z, n.x, n.y, n.z};
    const auto it = seen.find(key);
    if (it != seen.end()) {
      m.indices.push_back(it->second);
      return;
    }
    const auto i = static_cast<std::uint32_t>(m.positions.size() / 3);
    seen.emplace(key, i);
    m.indices.push_back(i);
    m.positions.insert(m.positions.end(), {p.x, p.y, p.z});
    m.normals.insert(m.normals.end(), {n.x, n.y, n.z});
    const float q[3] = {p.x, p.y, p.z};
    for (int c = 0; c < 3; c++) {
      if (first) {
        m.lo[c] = m.hi[c] = q[c];
      }
      m.lo[c] = std::min(m.lo[c], q[c]);
      m.hi[c] = std::max(m.hi[c], q[c]);
    }
    first = false;
  };

  std::uint32_t polygon = 0;
  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    const Face& face = poly.face(f);
    if (face.hole) {
      continue;
    }
    // inside faces face the other way (the view turned their normal)
    Vec3 n = poly.faceNormal(f);
    if ((face.flags & kFaceInside) != 0) {
      n = -n;
    }
    if (std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z)) {
      continue;
    }

    // the fan the view draws: from the second corner
    int e = face.edge;
    const int sentinel = e;
    e = poly.he(e).next;
    const Vec3 start = poly.dstPos(e);
    e = poly.he(e).next;
    do {
      add(start, n);
      add(poly.dstPos(e), n);
      e = poly.he(e).next;
      add(poly.dstPos(e), n);
      m.voxel.push_back(face.voxel);
      m.voxelFace.push_back(face.voxelFace);
      m.flags.push_back(face.flags);
      m.colour.push_back(face.colour);
      m.polygon.push_back(polygon);
    } while (e != sentinel);
    polygon++;
  }

  if (edgeLines) {
    // where two faces with different normals meet; coplanar cell
    // boundaries get no line
    for (int i = 0; i < poly.numHalfEdges(); i++) {
      const int h = poly.halfEdgeAt(i);
      const int tw = poly.he(h).twin;
      if (tw == kNone || poly.he(tw).index < i) {
        continue;
      }
      const Face& f1 = poly.face(poly.he(h).face);
      const Face& f2 = poly.face(poly.he(tw).face);
      if (f1.hole || f2.hole) {
        continue;
      }
      if (static_cast<double>(
              poly.faceNormal(poly.he(h).face).dot(poly.faceNormal(poly.he(tw).face))) > 0.99) {
        continue;
      }
      const Vec3& a = poly.dstPos(tw);
      const Vec3& b = poly.dstPos(h);
      m.edgeLines.insert(m.edgeLines.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
    }
  }
  return m;
}

DrawMesh shapeDrawMesh(const Voxel& v, MeshStyle style) {
  return toDrawMesh(shapeMesh(v, style), style == MeshStyle::Flat);
}

}  // namespace bt::mesh
