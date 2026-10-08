// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/halfedge/polyhedron.cpp, face.cpp, halfedge.cpp and
// vertex.cpp (Polyhedron, eraseFaces, closeSurface, Face::normal,
// HalfEdge::prev, vertexList_c).
#include "bt/mesh/polyhedron.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numbers>
#include <stack>
#include <utility>

namespace bt::mesh {

namespace {

// The projections finalize() sorts the faces around an edge by: the
// intersection of the line from `start` along `dir` with a coordinate plane.
void intersectionXY(Vec3 start, Vec3 dir, float& x, float& y) {
  const float t = -start.z / dir.z;
  x = start.x + t * dir.x;
  y = start.y + t * dir.y;
}

void intersectionXZ(Vec3 start, Vec3 dir, float& x, float& y) {
  const float t = -start.y / dir.y;
  y = start.x + t * dir.x;
  x = start.z + t * dir.z;
}

void intersectionYZ(Vec3 start, Vec3 dir, float& x, float& y) {
  const float t = -start.x / dir.x;
  x = start.y + t * dir.y;
  y = start.z + t * dir.z;
}

struct HeInfo {
  int h;
  float angle;
};

// The half-edges without a twin, by their vertices (findUnmatchedHalfEdges).
std::map<std::pair<int, int>, int> unmatchedHalfEdges(const Polyhedron& poly) {
  std::map<std::pair<int, int>, int> connections;
  for (int i = 0; i < poly.numHalfEdges(); i++) {
    const int h = poly.halfEdgeAt(i);
    if (poly.he(h).twin == kNone) {
      const int a = poly.he(poly.prev(h)).dst;
      const int b = poly.he(h).dst;
      BT_ASSERT(a != b);
      // repeated half-edge after processing: only manifolds are supported
      BT_ASSERT(!connections.contains({a, b}));
      BT_ASSERT(!connections.contains({b, a}));
      connections[{a, b}] = h;
    } else {
      BT_ASSERT(poly.he(poly.he(h).twin).twin == h);
    }
  }
  return connections;
}

}  // namespace

int Polyhedron::addVertex(const Vec3& pos) {
  const int i = numVertices();
  vertices_.push_back(Vertex{pos, kNone, i});
  return i;
}

int Polyhedron::addFace(const std::vector<int>& corners) {
  const int f = static_cast<int>(faceStore_.size());
  faceStore_.push_back(Face{});
  faceStore_.back().index = numFaces();
  faces_.push_back(f);

  // one half-edge per pair of neighbouring corners
  const std::size_t nc = corners.size();
  std::vector<int> edges;
  edges.reserve(nc);
  int h = kNone;
  for (std::size_t i = 0; i < nc; i++) {
    const int c1 = corners[i];
    const int c2 = corners[(i + 1) % nc];
    h = static_cast<int>(heStore_.size());
    heStore_.push_back(HalfEdge{c2, f, kNone, kNone, numHalfEdges()});
    halfEdges_.push_back(h);
    vertex(c1).edge = h;
    edges.push_back(h);
  }
  face(f).edge = h;

  for (std::size_t e = 0; e < nc; e++) {
    he(edges[e]).next = edges[(e + 1) % nc];
  }
  return f;
}

void Polyhedron::eraseFace(int f) {
  const std::size_t i = asUnsigned(face(f).index);
  if (faces_[i] != faces_.back()) {
    std::swap(faces_[i], faces_.back());
    face(faces_[i]).index = asInt(i);
  }
  faces_.pop_back();
  face(f).index = kNone;
}

void Polyhedron::eraseHalfEdge(int h) {
  const std::size_t i = asUnsigned(he(h).index);
  if (halfEdges_[i] != halfEdges_.back()) {
    std::swap(halfEdges_[i], halfEdges_.back());
    he(halfEdges_[i]).index = asInt(i);
  }
  halfEdges_.pop_back();
  he(h).index = kNone;
}

int Polyhedron::numHoles() const {
  int n = 0;
  for (const int f : faces_) {
    n += face(f).hole ? 1 : 0;
  }
  return n;
}

int Polyhedron::prev(int h) const {
  int p = he(h).next;
  BT_ASSERT(p != kNone);
  while (he(p).next != h) {
    p = he(p).next;
    BT_ASSERT(p != kNone);
  }
  return p;
}

Vec3 Polyhedron::faceNormal(int f) const {
  const int h1 = face(f).edge;
  const int h2 = he(h1).next;
  const int h3 = he(h2).next;
  const Vec3 v0 = dstPos(h1);
  const Vec3 v1 = dstPos(h2);
  const Vec3 v2 = dstPos(h3);
  Vec3 n = (v1 - v0).cross(v2 - v0);
  n.normalize();
  return n;
}

int Polyhedron::faceSize(int f) const {
  int n = 0;
  const int start = face(f).edge;
  int h = start;
  do {
    n++;
    h = he(h).next;
  } while (h != start);
  return n;
}

void Polyhedron::finalize() {
  BT_ASSERT(check(false));

  // Joins the half-edges: after all the addFace calls their twins are
  // missing. The library's version took only two half-edges between two
  // vertices; meshes of voxels can have more, always an even number.
  std::multimap<std::pair<int, int>, int> connections;
  for (int i = 0; i < numHalfEdges(); i++) {
    const int h = halfEdgeAt(i);
    int a = he(prev(h)).dst;
    int b = he(h).dst;
    if (a > b) {
      std::swap(a, b);
    }
    connections.insert({{a, b}, h});
  }

  std::set<std::pair<int, int>> handled;
  for (int i = 0; i < numHalfEdges(); i++) {
    const int h0 = halfEdgeAt(i);
    int a = he(prev(h0)).dst;
    int b = he(h0).dst;
    if (a > b) {
      std::swap(a, b);
    }
    const std::pair<int, int> idx(a, b);
    if (!handled.insert(idx).second) {
      continue;
    }

    // equal_range: the half-edges of this edge, in the order they were met
    const auto range = connections.equal_range(idx);
    auto cit = range.first;
    const auto n = std::distance(range.first, range.second);

    // one: it is closed by a hole below
    if (n == 1) {
      continue;
    }

    if (n == 2) {
      const int h1 = cit->second;
      ++cit;
      const int h2 = cit->second;
      BT_ASSERT(h1 != h2);
      he(h1).twin = h2;
      he(h2).twin = h1;
    } else {
      // an odd number of faces at one edge cannot be paired
      BT_ASSERT(n % 2 == 0);

      // Sorted by the angle of their faces around the edge: neighbouring
      // faces (by angle) that enclose the same filled region are paired.
      // Seen along the edge, the angle of each face is that of another of
      // its corners, projected onto a coordinate plane the edge is not
      // parallel to (only the order of the angles matters).
      const Vec3 axis = dstPos(prev(cit->second)) - dstPos(cit->second);

      void (*projection)(Vec3, Vec3, float&, float&) = nullptr;
      bool inv = false;
      if (std::fabs(axis.z) > 0.0001F) {
        projection = intersectionXY;
        inv = axis.z < 0;
      } else if (std::fabs(axis.y) > 0.0001F) {
        projection = intersectionXZ;
        inv = axis.y < 0;
      } else {
        projection = intersectionYZ;
        inv = axis.x < 0;
      }

      float ox = 0;
      float oy = 0;
      projection(dstPos(cit->second), axis, ox, oy);
      float baseA = -1;

      std::vector<HeInfo> info;
      for (; cit != range.second; ++cit) {
        float px = 0;
        float py = 0;
        projection(dstPos(he(cit->second).next), axis, px, py);
        px -= ox;
        py -= oy;

        float angle = std::atan2(py, px);
        if (inv) {
          angle = -angle;
        }
        if (baseA == -1) {
          baseA = angle;
        }
        angle -= baseA;
        while (angle < 0) {
          angle = static_cast<float>(static_cast<double>(angle) + 2 * std::numbers::pi);
        }
        info.push_back(HeInfo{cit->second, angle});
      }

      std::sort(info.begin(), info.end(),
                [](const HeInfo& x, const HeInfo& y) { return x.angle < y.angle; });

      for (std::size_t k = 0; k < info.size(); k += 2) {
        // the two run in opposite directions
        BT_ASSERT(he(prev(info[k].h)).dst == he(info[k + 1].h).dst);
        BT_ASSERT(he(prev(info[k + 1].h)).dst == he(info[k].h).dst);
        he(info[k].h).twin = info[k + 1].h;
        he(info[k + 1].h).twin = info[k].h;
      }
    }
  }

  BT_ASSERT(check(false));
  closeSurface();
  BT_ASSERT(check(true));
}

// Adds the hole faces that make the surface closed (the free function
// closeSurface of the original).
void Polyhedron::closeSurface() {
  std::map<std::pair<int, int>, int> connections = unmatchedHalfEdges(*this);

  // half-edges around each boundary, each connected set becoming a hole
  while (!connections.empty()) {
    const auto pit = connections.begin();
    const std::pair<int, int> pts = pit->first;
    int e = pit->second;

    // pts.second is the dst of a surface half-edge
    const int start = pts.second;
    int v = start;
    std::stack<int> s;
    s.push(pts.second);
    s.push(pts.first);
    std::set<int> visited;
    visited.insert(pts.first);
    visited.insert(pts.second);
    BT_ASSERT(visited.size() == s.size());
    do {
      // the next half-edge of the hole, through pinched vertices
      e = prev(e);
      std::set<int> seen;
      while (he(e).twin != kNone && !seen.contains(e)) {
        seen.insert(e);
        e = prev(he(e).twin);
      }
      BT_ASSERT(he(e).face != kNone);
      if (seen.contains(e)) {
        break;
      }

      // a vertex seen before: back along the stack to it, making a face
      v = he(prev(e)).dst;
      if (visited.contains(v)) {
        std::vector<int> corners;
        corners.push_back(v);
        BT_ASSERT(s.top() != v);
        do {
          const int p = s.top();
          corners.push_back(p);
          s.pop();
          BT_ASSERT(visited.contains(p));
          visited.erase(p);
          BT_ASSERT(visited.size() == s.size());
        } while (s.top() != v);
        BT_ASSERT(s.top() == v);
        s.pop();

        std::reverse(corners.begin(), corners.end());

        const int f = addFace(corners);
        face(f).hole = true;

        // its half-edges are the twins of the open ones
        int h = face(f).edge;
        const int sentinel = h;
        do {
          const int aux = prev(h);
          const int a = he(aux).dst;
          const int b = he(h).dst;
          BT_ASSERT(a != b);
          const auto t = connections.find({b, a});
          BT_ASSERT(t != connections.end());
          const int tw = t->second;
          connections.erase(t);
          BT_ASSERT(tw != h);
          BT_ASSERT(he(tw).dst != he(h).dst);
          he(tw).twin = h;
          he(h).twin = tw;
          BT_ASSERT(src(h) == a);
          h = aux;
        } while (h != sentinel);
      }

      visited.insert(v);
      s.push(v);
      BT_ASSERT(visited.size() == s.size());
    } while (v != start);
  }
}

bool Polyhedron::check(bool holesFilled) const {
  for (int i = 0; i < numVertices(); i++) {
    BT_ASSERT(vertex(i).index == i);
  }
  for (int i = 0; i < numHalfEdges(); i++) {
    const int h = halfEdgeAt(i);
    const HalfEdge& e = he(h);
    BT_ASSERT(e.index == i);
    BT_ASSERT(e.next != kNone);
    BT_ASSERT(e.face != kNone);
    BT_ASSERT(e.dst != kNone);
    BT_ASSERT(he(prev(h)).next == h);
    BT_ASSERT(he(prev(h)).dst != e.dst);
    if (holesFilled) {
      BT_ASSERT(e.twin != kNone);
      BT_ASSERT(e.twin != h);
      BT_ASSERT(he(e.twin).twin == h);
      BT_ASSERT(he(prev(h)).dst == he(e.twin).dst);
      // no two holes side by side
      BT_ASSERT(!face(e.face).hole || !face(he(e.twin).face).hole);
    }
  }
  for (int i = 0; i < numFaces(); i++) {
    const int f = faceAt(i);
    BT_ASSERT(face(f).index == i);
    std::set<int> visited;
    int h = face(f).edge;
    const int sentinel = h;
    do {
      BT_ASSERT(he(h).face == f);
      BT_ASSERT(visited.insert(h).second);
      h = he(h).next;
    } while (h != sentinel);
  }
  return true;
}

VertexList::VertexList(Polyhedron& poly) : poly_(poly) {
  // the mesh is empty: nothing to look up
  BT_ASSERT(poly.numVertices() == 0);
}

int VertexList::get(float x, float y, float z) {
  const Vec3 v(x, y, z);
  const auto it = known_.find(v);
  if (it != known_.end()) {
    return it->second;
  }
  const int idx = poly_.addVertex(v);
  known_.emplace(v, idx);
  return idx;
}

void eraseFaces(Polyhedron& poly, const std::set<int>& faces) {
  for (const int f : faces) {
    int h = poly.face(f).edge;
    const int sentinel = h;
    do {
      const int h2 = poly.he(h).next;
      if (poly.he(h).twin != kNone) {
        poly.he(poly.he(h).twin).twin = kNone;
      }
      Vertex& d = poly.vertex(poly.he(h).dst);
      if (d.edge == h2) {
        d.edge = kNone;
      }
      poly.eraseHalfEdge(h);
      h = h2;
    } while (h != sentinel);
    poly.eraseFace(f);
  }
  for (int i = 0; i < poly.numHalfEdges(); i++) {
    const HalfEdge& e = poly.he(poly.halfEdgeAt(i));
    if (poly.vertex(e.dst).edge == kNone && e.twin != kNone) {
      poly.vertex(e.dst).edge = e.twin;
    }
  }
}

}  // namespace bt::mesh
