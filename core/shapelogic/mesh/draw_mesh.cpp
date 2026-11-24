// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from voxelFrame_c::drawShape (src/gui/voxelframe.cpp) and buildMesh
// (src/lib/gltfexport.cpp).
#include "shapelogic/mesh/draw_mesh.h"

#include "shapelogic/grid/voxel.h"

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
      m.color.push_back(face.color);
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

namespace {

// The picture coordinates of the vertices on faces looking up (the mesh is
// in cell units: cell (x, y) covers [x, x + 1] x [y, y + 1]): the inverse of
// ShapeImage's map, q = ((p - box corner) / box size - 1/2), (s - 1/2,
// 1/2 - t) = M^T q.
std::vector<float> imageUv(const ShapeImage& im, const DrawMesh& m) {
  const std::array<int, 4> r = ShapeImage::matrix(im.turn);
  const std::size_t n = m.vertexCount();
  std::vector<float> uv(2 * n, -1.0F);
  for (std::size_t i = 0; i < n; i++) {
    if (m.normals[3 * i + 2] < 0.5F) {
      continue;
    }
    const auto x = static_cast<double>(m.positions[3 * i]);
    const auto y = static_cast<double>(m.positions[3 * i + 1]);
    const double qx = (x - im.box[0]) / im.box[2] - 0.5;
    const double qy = (y - im.box[1]) / im.box[3] - 0.5;
    const double px = r[0] * qx + r[2] * qy;
    const double py = r[1] * qx + r[3] * qy;
    uv[2 * i] = static_cast<float>(px + 0.5);
    uv[2 * i + 1] = static_cast<float>(0.5 - py);
  }
  return uv;
}

}  // namespace

DrawMesh shapeDrawMesh(const Voxel& v, MeshStyle style) {
  DrawMesh m = toDrawMesh(shapeMesh(v, style), style == MeshStyle::Flat);
  m.hotspot[0] = v.getHx();
  m.hotspot[1] = v.getHy();
  m.hotspot[2] = v.getHz();
  // the pictures of a stack share their box and turn (decision 0047)
  if (!v.getImages().empty()) {
    m.imageUv = imageUv(v.getImages().front(), m);
  }
  return m;
}

}  // namespace bt::mesh
