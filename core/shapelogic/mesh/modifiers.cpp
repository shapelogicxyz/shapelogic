// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/halfedge/modifiers.cpp (fillPolyhedronHoles and its
// helpers, scalePolyhedron, mergeCoplanarFaces and its helpers; copyright
// the BurrTools team) and src/halfedge/volume.cpp (volume, get_area). The original kept its sets of
// faces and vertices ordered by address; here they are ordered by the
// order the faces and vertices were made.
#include "shapelogic/mesh/modifiers.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/mesh/polyhedron.h"

#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace bt::mesh {

namespace {

constexpr float kEpsilon = 1.0e-5F;

// The faces of a mesh as triangle fans, 9 floats per triangle: to test
// whether a point lies inside the original solid.
using TriSnapshot = std::vector<float>;

TriSnapshot snapshotFaces(const Polyhedron& poly) {
  TriSnapshot snap;
  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    if (poly.face(f).hole) {
      continue;
    }
    int e = poly.face(f).edge;
    const int sentinel = e;
    e = poly.he(e).next;
    const Vec3 start = poly.dstPos(e);
    e = poly.he(e).next;
    do {
      const Vec3 b = poly.dstPos(e);
      e = poly.he(e).next;
      const Vec3 c = poly.dstPos(e);
      for (const Vec3* v : {&start, &b, &c}) {
        snap.push_back(v->x);
        snap.push_back(v->y);
        snap.push_back(v->z);
      }
    } while (e != sentinel);
  }
  return snap;
}

// A ray along +x against the snapshot, counting crossings. The callers
// jitter the points a little, so hitting an edge exactly is no concern.
bool insideSnapshot(const TriSnapshot& snap, float px, float py, float pz) {
  int crossings = 0;
  for (std::size_t i = 0; i < snap.size(); i += 9) {
    const float* a = &snap[i];
    const float* b = &snap[i + 3];
    const float* c = &snap[i + 6];

    // projected on the yz plane
    const float y0 = a[1] - py;
    const float z0 = a[2] - pz;
    const float y1 = b[1] - py;
    const float z1 = b[2] - pz;
    const float y2 = c[1] - py;
    const float z2 = c[2] - pz;

    const float d0 = y0 * z1 - y1 * z0;
    const float d1 = y1 * z2 - y2 * z1;
    const float d2 = y2 * z0 - y0 * z2;

    if ((d0 > 0 && d1 > 0 && d2 > 0) || (d0 < 0 && d1 < 0 && d2 < 0)) {
      const float area = d0 + d1 + d2;
      // x at the hit, from the barycentric weights
      const float x = (a[0] * d1 + b[0] * d2 + c[0] * d0) / area;
      if (x > px) {
        crossings++;
      }
    }
  }
  return (crossings & 1) != 0;
}

// The distance from p along the unit direction d to the nearest snapshot
// triangle, or -1 when nothing is hit (Moeller-Trumbore).
float rayDepthSnapshot(const TriSnapshot& snap, const Vec3& p, const Vec3& d) {
  float best = -1;
  for (std::size_t i = 0; i < snap.size(); i += 9) {
    const Vec3 a(snap[i], snap[i + 1], snap[i + 2]);
    const Vec3 b(snap[i + 3], snap[i + 4], snap[i + 5]);
    const Vec3 c(snap[i + 6], snap[i + 7], snap[i + 8]);

    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 pv = d.cross(e2);
    const float det = e1.dot(pv);
    if (det > -1e-9F && det < 1e-9F) {
      continue;
    }
    const float inv = 1.0F / det;
    const Vec3 tv = p - a;
    const float uu = (tv.dot(pv)) * inv;
    if (uu < -1e-4F || uu > 1.0001F) {
      continue;
    }
    const Vec3 qv = tv.cross(e1);
    const float vv = (d.dot(qv)) * inv;
    if (vv < -1e-4F || uu + vv > 1.0001F) {
      continue;
    }
    const float t = (e2.dot(qv)) * inv;
    if (t > 1e-5F && (best < 0 || t < best)) {
      best = t;
    }
  }
  return best;
}

// Whether a replacement or cap face lies on the solid: points just below
// its centre and towards its corners must be inside the original surface.
// A face failing this bridges a concave corner and adds material outside.
bool faceOnSolid(const TriSnapshot& snap, const std::vector<Vec3>& pts) {
  Vec3 cen(0, 0, 0);
  for (const Vec3& p : pts) {
    cen += p;
  }
  cen /= static_cast<float>(pts.size());

  Vec3 nrm = (pts[1] - pts[0]).cross(pts[2] - pts[0]);
  const float l = std::sqrt(nrm.dot(nrm));
  if (l <= 0) {
    return true;
  }
  nrm /= l;

  float mine = -1;
  for (std::size_t i = 0; i < pts.size(); i++) {
    const Vec3 d = pts[(i + 1) % pts.size()] - pts[i];
    const float e = std::sqrt(d.dot(d));
    if (mine < 0 || e < mine) {
      mine = e;
    }
  }
  // Probes just below the surface, inside the face (half way to the
  // corners) and only a little below it, so bevelled edges of thin
  // material nearby give no false alarms.
  const float eps = 0.08F * mine;

  for (std::size_t pr = 0; pr <= pts.size(); pr++) {
    Vec3 q0 = cen;
    if (pr > 0) {
      q0 = (cen + pts[pr - 1]) * 0.5F;
    }
    const Vec3 q = q0 - nrm * eps;
    if (insideSnapshot(snap, q.x, q.y + 1.1e-4F, q.z + 1.7e-4F)) {
      continue;
    }
    // Nothing below this probe: fine over a shallow groove (a floor close
    // below) and at a convex corner (no floor at all), but a floor far
    // below means the face bridges a concave junction.
    const float depth = rayDepthSnapshot(snap, q0, -nrm);
    if (depth >= 0 && depth > 1.5F * mine) {
      return false;
    }
  }
  return true;
}

// The normal of the face a vertex's half-edge belongs to.
Vec3 vertexFaceNormal(const Polyhedron& poly, int v) {
  return poly.faceNormal(poly.he(poly.vertex(v).edge).face);
}

// A good triangle or quad to fill part of a hole with, by comparing the
// normals of the faces at its border with those of the candidates.
// Returns the corners (3 or 4) and the offset of the first in `vs`, or 0
// when a triangle fan is best.
int findBestTriOrQuad(const Polyhedron& poly, const std::vector<int>& vs, int& offset) {
  const int vsize = static_cast<int>(vs.size());
  const auto at = [&vs, vsize](int i) { return vs[static_cast<std::size_t>(i % vsize)]; };

  if (vsize < 4) {
    offset = 0;
    return vsize;
  }

  int goodQuad = vsize;

  for (offset = 0; offset < vsize; offset++) {
    const Vec3 v0 = poly.vertex(at(offset)).pos;
    const Vec3 v1 = poly.vertex(at(offset + 1)).pos;
    const Vec3 v2 = poly.vertex(at(offset + 2)).pos;

    Vec3 n = (v1 - v0).cross(v2 - v0);

    if (n.squaredModule() < static_cast<double>(kEpsilon * kEpsilon)) {
      // colinear segments: best filled with a triangle fan
      return 0;
    }

    n.normalize();

    const Vec3 n0 = vertexFaceNormal(poly, at(offset + 1));
    const Vec3 n1 = vertexFaceNormal(poly, at(offset + 2));

    const auto angle0 = static_cast<float>(std::fabs(static_cast<double>(n.dot(n0)) - 1.0));
    const auto angle1 = static_cast<float>(std::fabs(static_cast<double>(n.dot(n1)) - 1.0));

    const Vec3 v3 = poly.vertex(at(offset + 3)).pos;

    // the distance of the fourth point from the plane of the first three
    const float dist = std::fabs(n.dot(v3 - v0));

    if (angle0 < kEpsilon && angle1 < kEpsilon) {
      return dist < kEpsilon ? 4 : 3;
    }
    if (dist < kEpsilon) {
      goodQuad = offset;
    }
  }

  if (goodQuad < vsize) {
    offset = goodQuad;
    return 4;
  }
  return 0;
}

// Caps a hole (its corners in the order the hole runs) with triangles and
// quads where they fit, else with a triangle fan.
void findOptimizedFaces(Polyhedron& poly, const std::vector<int>& corners,
                        const TriSnapshot& snap) {
  std::vector<int> work;
  std::uint32_t flags = 0;
  std::uint8_t colour = 0;

  // the hole runs the other way round
  for (auto rit = corners.rbegin(); rit != corners.rend(); ++rit) {
    work.push_back(*rit);
    const Face& f = poly.face(poly.he(poly.vertex(*rit).edge).face);
    flags |= f.flags;
    if (colour == 0 && f.colour != 0) {
      colour = f.colour;
    }
  }

  const auto addCap = [&poly, flags, colour](const std::vector<int>& pts) {
    const int f = poly.addFace(pts);
    poly.face(f).flags = flags;
    poly.face(f).voxelFace = -1;
    poly.face(f).colour = colour;
  };

  while (work.size() > 2) {
    int offset = 0;
    int ret = findBestTriOrQuad(poly, work, offset);
    const int oldSize = static_cast<int>(work.size());
    const auto idx = [oldSize](int i) { return static_cast<std::ptrdiff_t>(i % oldSize); };

    if (ret != 0) {
      // the cap must lie on the solid, otherwise it would bridge a
      // concave corner: then the triangle fan
      std::vector<Vec3> cand;
      for (int j = 0; j < ret; j++) {
        cand.push_back(poly.vertex(work[static_cast<std::size_t>(idx(offset + j))]).pos);
      }
      if (!faceOnSolid(snap, cand)) {
        ret = 0;
      }
    }

    if (ret != 0) {
      std::vector<int> pts;
      for (int j = 0; j < ret; j++) {
        pts.push_back(work[static_cast<std::size_t>(idx(offset + j))]);
      }
      addCap(pts);

      if (ret == 4) {
        if (idx(offset + 2) > idx(offset + 1)) {
          work.erase(work.begin() + idx(offset + 2));
          work.erase(work.begin() + idx(offset + 1));
        } else {
          work.erase(work.begin() + idx(offset + 1));
          work.erase(work.begin() + idx(offset + 2));
        }
      } else {
        work.erase(work.begin() + idx(offset + 1));
      }
    } else {
      // a triangle fan around the average of the corners
      Vec3 center;
      for (const int v : work) {
        center += poly.vertex(v).pos;
      }
      center /= static_cast<float>(work.size());
      const int v = poly.addVertex(center);

      for (std::size_t j = 0; j < work.size(); j++) {
        addCap({work[j], work[(j + 1) % work.size()], v});
      }
      return;
    }
  }
}

}  // namespace

void fillPolyhedronHoles(Polyhedron& poly, bool fillOutsides) {
  constexpr std::uint32_t kStrip = kFaceOffset | kFaceBevel;
  std::set<int> facesToRemove;

  // The original surface: replacement faces must never add material
  // outside it (they did at concave corners, and pieces stacked in a
  // puzzle went into each other).
  const TriSnapshot snap = snapshotFaces(poly);

  // faces added below are visited too, as the original's index iterator did
  for (int fi = 0; fi < poly.numFaces(); fi++) {
    const int start = poly.faceAt(fi);
    // start at a face of a cell (not a bevel or gap face)
    if ((poly.face(start).flags & kStrip) != 0) {
      continue;
    }
    int ei = poly.face(start).edge;
    const int sentinel = ei;
    int f = start;
    std::set<int> faces;
    const std::uint8_t colour = poly.face(start).colour;

    do {
      int edge = ei;
      std::uint32_t newflag = 0;
      // faces may have been erased on the way: check
      if (poly.he(ei).twin != kNone) {
        // the strip of bevel and gap faces from here to the next cell face
        do {
          Face& ff = poly.face(f);
          if ((ff.flags & kStrip) != 0 && (ff.flags & kFaceProcessed) == 0) {
            ff.flags |= kFaceProcessed;
            newflag |= ff.flags;
            if (fillOutsides) {
              faces.insert(f);
            }
            ff.colour = colour;
          }
          f = poly.he(poly.he(edge).twin).face;
          edge = poly.he(poly.he(poly.he(edge).twin).next).next;
        } while ((poly.face(f).flags & kStrip) != 0);
      }

      // several faces become one
      if (faces.size() > 1) {
        // the angle between the face started from and the one reached
        const Vec3 n0 = poly.faceNormal(f);
        const Vec3 n1 = poly.faceNormal(start);
        const auto angle =
            static_cast<float>(std::fabs(static_cast<double>(n0.dot(n1)) - 1.0));

        std::vector<int> face4(4);
        face4[0] = poly.he(ei).dst;
        face4[1] = poly.src(ei);
        edge = poly.prev(poly.prev(edge));
        face4[2] = poly.he(edge).dst;
        face4[3] = poly.src(edge);

        // At a concave junction the replacement face would add material
        // outside the solid: the strip stays. Concave is decided from the
        // two rims: there each lies above the plane of the other rim's
        // face (a purely local test, the same all along a lattice edge).
        if (angle >= kEpsilon) {
          const Vec3 p0 = poly.vertex(face4[0]).pos;
          const Vec3 p1 = poly.vertex(face4[1]).pos;
          const Vec3 p2 = poly.vertex(face4[2]).pos;
          const Vec3 p3 = poly.vertex(face4[3]).pos;

          // rim p0/p1 borders the face with normal n1, rim p2/p3 the one
          // with normal n0
          constexpr float kTol = 1e-4F;
          if ((p2 - p1).dot(n1) > kTol || (p3 - p0).dot(n1) > kTol || (p1 - p2).dot(n0) > kTol ||
              (p0 - p3).dot(n0) > kTol) {
            faces.clear();
            ei = poly.he(ei).next;
            continue;
          }
        }

        facesToRemove.insert(faces.begin(), faces.end());

        const int nf = poly.addFace(face4);
        // a flat face hides its wire-frame edge
        poly.face(nf).flags = angle < kEpsilon ? (newflag & ~std::uint32_t{kFaceWireframe}) : newflag;
        // no tubes connected here
        poly.face(nf).voxelFace = -1;
        poly.face(nf).colour = colour;
      }

      faces.clear();
      ei = poly.he(ei).next;
    } while (ei != sentinel);
  }

  if (!facesToRemove.empty()) {
    eraseFaces(poly, facesToRemove);
    facesToRemove.clear();
  }

  // unprocessed faces left with loose edges go: bevel corners with an
  // edge that lost its twin, and gap faces that were not replaced
  for (;;) {
    for (int fi = 0; fi < poly.numFaces(); fi++) {
      const int f = poly.faceAt(fi);
      const Face& ff = poly.face(f);
      if ((ff.flags & kFaceProcessed) != 0) {
        continue;
      }
      if ((ff.flags & kFaceBevel) != 0) {
        int e = ff.edge;
        const int sentinel = e;
        do {
          if (poly.he(e).twin == kNone) {
            break;
          }
          e = poly.he(e).next;
        } while (e != sentinel);
        if (poly.he(e).twin == kNone) {
          facesToRemove.insert(f);
        }
      } else if ((ff.flags & kFaceOffset) != 0) {
        facesToRemove.insert(f);
      }
    }
    if (facesToRemove.empty()) {
      break;
    }
    eraseFaces(poly, facesToRemove);
    facesToRemove.clear();
  }

  // The half-edges without a twin border holes to be capped. Pairs that
  // lost each other in the removal are joined again (as finalize does).
  std::map<std::pair<int, int>, int> conn;
  for (int i = 0; i < poly.numHalfEdges(); i++) {
    const int h = poly.halfEdgeAt(i);
    if (poly.he(h).twin != kNone) {
      continue;
    }
    const int d = poly.he(h).dst;
    const int s = poly.he(poly.prev(h)).dst;
    const auto cit = conn.find({d, s});
    if (cit == conn.end()) {
      conn.emplace(std::pair<int, int>(s, d), h);
    } else {
      poly.he(cit->second).twin = h;
      poly.he(h).twin = cit->second;
      conn.erase(cit);
    }
  }

  // each open half-edge by the vertex it starts at
  std::map<int, int> seams;
  for (const auto& [verts, h] : conn) {
    seams.emplace(verts.first, h);
  }

  std::set<int> handled;
  std::vector<int> pts;
  for (const auto& [v0, e0] : seams) {
    int v = v0;
    int e = e0;
    // around the hole until it closes
    while (!handled.contains(v)) {
      handled.insert(v);
      pts.push_back(v);
      poly.vertex(v).edge = e;
      v = poly.he(e).dst;
      e = seams.find(v)->second;
    }
    if (!pts.empty()) {
      findOptimizedFaces(poly, pts, snap);
      pts.clear();
    }
  }

  // join the edges of the caps
  poly.finalize();
}

void scalePolyhedron(Polyhedron& poly, float x, float y, float z) {
  for (int i = 0; i < poly.numVertices(); i++) {
    Vec3& p = poly.vertex(i).pos;
    p = Vec3(p.x * x, p.y * y, p.z * z);
  }
}

namespace {

// A 2D point of the retriangulation of merged face groups.
struct MergePoint {
  double x;
  double y;
};

// twice the signed area of the triangle a, b, c
double triArea2(const MergePoint& a, const MergePoint& b, const MergePoint& c) {
  return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

bool samePoint(const MergePoint& a, const MergePoint& b) { return a.x == b.x && a.y == b.y; }

// is p inside the closed ccw triangle a, b, c
bool pointInTriangle(const MergePoint& p, const MergePoint& a, const MergePoint& b,
                     const MergePoint& c, double eps) {
  return triArea2(a, b, p) >= -eps && triArea2(b, c, p) >= -eps && triArea2(c, a, p) >= -eps;
}

// do the open segments a-b and c-d properly cross one another
bool segmentsCross(const MergePoint& a, const MergePoint& b, const MergePoint& c,
                   const MergePoint& d) {
  const double d1 = triArea2(c, d, a);
  const double d2 = triArea2(c, d, b);
  const double d3 = triArea2(a, b, c);
  const double d4 = triArea2(a, b, d);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

// Triangulates the weakly simple ccw polygon `poly` (indices into pts) by
// ear clipping, appending index triples to tris. False when no ear can
// be clipped any more though more than 2 corners are left.
bool earClip(std::vector<int> poly, const std::vector<MergePoint>& pts, std::vector<int>& tris,
             double eps, double fatEps) {
  const auto pt = [&pts](int i) -> const MergePoint& { return pts[asUnsigned(i)]; };
  unsigned int i = 0;
  while (poly.size() > 2) {
    bool clipped = false;
    // First an ear with a decent area; only when there is none a sliver:
    // that keeps nearly collinear corners from giving degenerate
    // triangles when there is a better choice.
    for (int pass = 0; pass < 2 && !clipped; pass++) {
      const double minArea = pass == 0 ? fatEps : eps;
      for (unsigned int tries = 0; tries < poly.size(); tries++, i++) {
        const int ia = poly[i % poly.size()];
        const int ib = poly[(i + 1) % poly.size()];
        const int ic = poly[(i + 2) % poly.size()];
        const MergePoint& a = pt(ia);
        const MergePoint& b = pt(ib);
        const MergePoint& c = pt(ic);
        // the ear tip must be strictly convex
        if (triArea2(a, b, c) <= minArea) {
          continue;
        }
        // no other corner of the polygon may lie within the ear
        bool blocked = false;
        for (const int j : poly) {
          const MergePoint& p = pt(j);
          if (samePoint(p, a) || samePoint(p, b) || samePoint(p, c)) {
            continue;
          }
          if (pointInTriangle(p, a, b, c, eps)) {
            blocked = true;
            break;
          }
        }
        if (blocked) {
          continue;
        }
        tris.push_back(ia);
        tris.push_back(ib);
        tris.push_back(ic);
        poly.erase(poly.begin() + static_cast<std::ptrdiff_t>((i + 1) % poly.size()));
        clipped = true;
        break;
      }
    }
    if (!clipped) {
      return false;
    }
  }
  return true;
}

float component(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

// The corners of a face, from the end of its first half-edge on.
std::vector<int> faceCorners(const Polyhedron& poly, int f) {
  std::vector<int> corners;
  int e = poly.face(f).edge;
  const int sentinel = e;
  do {
    corners.push_back(poly.he(e).dst);
    e = poly.he(e).next;
  } while (e != sentinel);
  return corners;
}

void copyAttributes(Face& dst, const Face& src) {
  dst.flags = src.flags;
  dst.colour = src.colour;
  dst.voxel = src.voxel;
  dst.voxelFace = src.voxelFace;
}

// copies one face of the source mesh into the destination
void copyFace(Polyhedron& dst, VertexList& vl, const Polyhedron& src, int f) {
  std::vector<int> corners;
  for (const int c : faceCorners(src, f)) {
    const Vec3& p = src.vertex(c).pos;
    corners.push_back(vl.get(p.x, p.y, p.z));
  }
  const int f2 = dst.addFace(corners);
  copyAttributes(dst.face(f2), src.face(f));
}

// Merges one group of connected coplanar faces into fewer, larger
// triangles added to the destination. False when anything goes wrong;
// nothing has been added then.
bool mergeGroup(Polyhedron& dst, VertexList& vl, const Polyhedron& src,
                const std::vector<int>& group, const std::set<int>& inGroup) {
  // The boundary edges of the group: those whose face on the other side
  // is not in the group, keyed by the vertex they start at to keep
  // everything deterministic.
  std::multimap<int, std::pair<int, int>> boundary;
  for (const int f : group) {
    int e = src.face(f).edge;
    const int sentinel = e;
    do {
      const int tw = src.he(e).twin;
      if (tw == kNone || src.he(tw).face == kNone || src.face(src.he(tw).face).hole ||
          !inGroup.contains(src.he(tw).face)) {
        const int from = src.he(src.prev(e)).dst;
        boundary.insert({from, {from, src.he(e).dst}});
      }
      e = src.he(e).next;
    } while (e != sentinel);
  }

  // the boundary edges assembled into closed loops
  std::vector<std::vector<int>> loops;
  while (!boundary.empty()) {
    std::vector<int> loop;
    const int start = boundary.begin()->second.first;
    int cur = start;
    do {
      const auto it = boundary.find(cur);
      if (it == boundary.end()) {
        return false;
      }
      loop.push_back(cur);
      cur = it->second.second;
      boundary.erase(it);
    } while (cur != start);
    if (loop.size() < 3) {
      return false;
    }
    loops.push_back(loop);
  }
  if (loops.empty()) {
    return false;
  }

  // a projection onto the plane of the group that keeps the outline
  // counter clockwise
  const Vec3 n = src.faceNormal(group[0]);
  int u = 0;
  int v = 0;
  if (std::fabs(n.x) >= std::fabs(n.y) && std::fabs(n.x) >= std::fabs(n.z)) {
    u = 1;
    v = 2;
    if (n.x < 0) {
      u = 2;
      v = 1;
    }
  } else if (std::fabs(n.y) >= std::fabs(n.z)) {
    u = 2;
    v = 0;
    if (n.y < 0) {
      u = 0;
      v = 2;
    }
  } else {
    u = 0;
    v = 1;
    if (n.z < 0) {
      u = 1;
      v = 0;
    }
  }

  // all loops projected to 2d
  std::vector<MergePoint> pts;
  std::vector<int> ptVertex;
  std::vector<std::vector<int>> loops2;
  std::vector<double> loopArea;
  double totalArea = 0;
  for (const std::vector<int>& loop : loops) {
    std::vector<int> l2;
    for (const int vx : loop) {
      const Vec3& p = src.vertex(vx).pos;
      l2.push_back(static_cast<int>(pts.size()));
      pts.push_back({static_cast<double>(component(p, u)), static_cast<double>(component(p, v))});
      ptVertex.push_back(vx);
    }
    double area = 0;
    for (std::size_t i = 0; i < l2.size(); i++) {
      const MergePoint& a = pts[asUnsigned(l2[i])];
      const MergePoint& b = pts[asUnsigned(l2[(i + 1) % l2.size()])];
      area += (a.x * b.y) - (b.x * a.y);
    }
    area /= 2;
    loops2.push_back(l2);
    loopArea.push_back(area);
    totalArea += area;
  }
  if (totalArea <= 0) {
    return false;
  }

  // the loop with the largest area is the outline; all other loops must
  // be holes and run the other way around
  std::size_t outer = 0;
  for (std::size_t l = 1; l < loops2.size(); l++) {
    if (loopArea[l] > loopArea[outer]) {
      outer = l;
    }
  }
  if (loopArea[outer] <= 0) {
    return false;
  }
  for (std::size_t l = 0; l < loops2.size(); l++) {
    if (l != outer && loopArea[l] >= 0) {
      return false;
    }
  }
  std::vector<int> polygon = loops2[outer];

  // The holes connected to the outline with bridges: one big weakly
  // simple polygon that the ear clipper can handle.
  std::vector<std::size_t> holeIdx;
  for (std::size_t l = 0; l < loops2.size(); l++) {
    if (l != outer) {
      holeIdx.push_back(l);
    }
  }
  const auto pt = [&pts](int i) -> const MergePoint& { return pts[asUnsigned(i)]; };
  for (std::size_t h = 0; h < holeIdx.size(); h++) {
    const std::vector<int>& hole = loops2[holeIdx[h]];
    // the closest pair of corners between the polygon so far and the hole
    // whose connecting line crosses no edge
    int bestP = -1;
    int bestH = -1;
    double bestDist = 0;
    for (std::size_t a = 0; a < polygon.size(); a++) {
      for (std::size_t b = 0; b < hole.size(); b++) {
        const MergePoint& pa = pt(polygon[a]);
        const MergePoint& pb = pt(hole[b]);
        const double dist = ((pa.x - pb.x) * (pa.x - pb.x)) + ((pa.y - pb.y) * (pa.y - pb.y));
        if (bestP != -1 && dist >= bestDist) {
          continue;
        }
        // the bridge must not cross the polygon, this hole or any of the
        // holes still waiting for their bridge
        bool crosses = false;
        for (std::size_t i = 0; i < polygon.size() && !crosses; i++) {
          if (segmentsCross(pa, pb, pt(polygon[i]), pt(polygon[(i + 1) % polygon.size()]))) {
            crosses = true;
          }
        }
        for (std::size_t h2 = h; h2 < holeIdx.size() && !crosses; h2++) {
          const std::vector<int>& hl = loops2[holeIdx[h2]];
          for (std::size_t i = 0; i < hl.size() && !crosses; i++) {
            if (segmentsCross(pa, pb, pt(hl[i]), pt(hl[(i + 1) % hl.size()]))) {
              crosses = true;
            }
          }
        }
        if (!crosses) {
          bestP = static_cast<int>(a);
          bestH = static_cast<int>(b);
          bestDist = dist;
        }
      }
    }
    if (bestP == -1) {
      return false;
    }
    // the hole spliced into the polygon: the 2 bridge corners appear twice
    std::vector<int> merged;
    for (int i = 0; i <= bestP; i++) {
      merged.push_back(polygon[asUnsigned(i)]);
    }
    for (std::size_t i = 0; i <= hole.size(); i++) {
      merged.push_back(hole[(asUnsigned(bestH) + i) % hole.size()]);
    }
    for (std::size_t i = asUnsigned(bestP); i < polygon.size(); i++) {
      merged.push_back(polygon[i]);
    }
    polygon = merged;
  }

  // triangulate
  std::vector<int> tris;
  double eps = 1e-12 * loopArea[outer];
  if (eps < 1e-20) {
    eps = 1e-20;
  }
  const double fatEps = 1e-7 * loopArea[outer];
  if (!earClip(polygon, pts, tris, eps, fatEps)) {
    return false;
  }

  // the areas of the triangles must add up to the area of the group
  double triArea = 0;
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    triArea += triArea2(pt(tris[t]), pt(tris[t + 1]), pt(tris[t + 2])) / 2;
  }
  if (std::fabs(triArea - totalArea) > 1e-4 * totalArea) {
    return false;
  }

  // no triangle may be exactly degenerate: it would get a NaN normal
  const auto pos = [&](int i) { return src.vertex(ptVertex[asUnsigned(i)]).pos; };
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    const Vec3 a = pos(tris[t]);
    const Vec3 b = pos(tris[t + 1]);
    const Vec3 c = pos(tris[t + 2]);
    const Vec3 cr = (b - a).cross(c - a);
    if (cr.dot(cr) == 0) {
      return false;
    }
  }

  // all went well: the triangles added
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    std::vector<int> corners;
    for (std::size_t c = 0; c < 3; c++) {
      const Vec3 p = pos(tris[t + c]);
      corners.push_back(vl.get(p.x, p.y, p.z));
    }
    const int f2 = dst.addFace(corners);
    copyAttributes(dst.face(f2), src.face(group[0]));
  }
  return true;
}

// The area of a face (get_area): the fan of its corners from the first.
float faceArea(const Polyhedron& poly, int f) {
  Vec3 sum;
  int e = poly.face(f).edge;
  const int sentinel = e;
  const Vec3 p0 = poly.dstPos(e);
  e = poly.he(e).next;
  Vec3 v = poly.dstPos(e) - p0;
  e = poly.he(e).next;
  while (e != sentinel) {
    const Vec3 v2 = poly.dstPos(e) - p0;
    sum += v.cross(v2);
    v = v2;
    e = poly.he(e).next;
  }
  return static_cast<float>(sum.module() * 0.5);
}

}  // namespace

Polyhedron mergeCoplanarFaces(const Polyhedron& src) {
  // the groups of connected coplanar faces
  std::map<int, std::size_t> groupOf;
  std::vector<std::vector<int>> groups;
  for (int i = 0; i < src.numFaces(); i++) {
    const int f = src.faceAt(i);
    if (src.face(f).hole || groupOf.contains(f)) {
      continue;
    }
    const Vec3 n = src.faceNormal(f);
    const auto d = static_cast<double>(n.dot(src.dstPos(src.face(f).edge)));
    std::vector<int> group;
    std::vector<int> stack;
    groupOf[f] = groups.size();
    stack.push_back(f);
    while (!stack.empty()) {
      const int cur = stack.back();
      stack.pop_back();
      group.push_back(cur);
      int e = src.face(cur).edge;
      const int sentinel = e;
      do {
        const int tw = src.he(e).twin;
        if (tw != kNone && src.he(tw).face != kNone && !src.face(src.he(tw).face).hole &&
            !groupOf.contains(src.he(tw).face)) {
          const int cand = src.he(tw).face;
          // the candidate belongs to the group when it lies in the same plane
          if (static_cast<double>(n.dot(src.faceNormal(cand))) > 1 - 1e-6) {
            bool inPlane = true;
            int e2 = src.face(cand).edge;
            const int sentinel2 = e2;
            do {
              if (std::fabs(static_cast<double>(n.dot(src.dstPos(e2))) - d) > 1e-5) {
                inPlane = false;
                break;
              }
              e2 = src.he(e2).next;
            } while (e2 != sentinel2);
            if (inPlane) {
              groupOf[cand] = groups.size();
              stack.push_back(cand);
            }
          }
        }
        e = src.he(e).next;
      } while (e != sentinel);
    }
    groups.push_back(group);
  }

  // the new mesh
  Polyhedron res;
  VertexList vl(res);
  for (const std::vector<int>& group : groups) {
    if (group.size() > 1) {
      const std::set<int> inGroup(group.begin(), group.end());
      // a failed merge must not leave half a group behind
      const int facesBefore = res.numFaces();
      if (mergeGroup(res, vl, src, group, inGroup)) {
        continue;
      }
      BT_ASSERT(res.numFaces() == facesBefore);
    }
    for (const int f : group) {
      copyFace(res, vl, src, f);
    }
  }
  return res;
}

float volume(const Polyhedron& poly) {
  float vol = 0;
  // a point, any point, as the reference
  const Vec3 p0 = poly.dstPos(poly.halfEdgeAt(0));
  // the pyramids from it over each face
  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    const Vec3 p = poly.dstPos(poly.face(f).edge) - p0;
    const Vec3 n = poly.faceNormal(f);
    // the dot product: the distance from the point to the plane
    const float height = p.dot(n);
    const float area = faceArea(poly, f);
    vol += area * height;
  }
  return vol / 3;
}

}  // namespace bt::mesh
