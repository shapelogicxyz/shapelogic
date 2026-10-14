// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/minkmesh.cpp (minkMesh::handles, minkMesh::polyhedron
// and their helpers), on the same release of the Manifold library (3.5.2,
// cmake/Dependencies.cmake).
#include "shapelogic/mesh/mink_mesh.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/grid_geometry.h"

#include <manifold/manifold.h>
#include <manifold/polygon.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bt::mesh {

namespace {

using manifold::Manifold;
using manifold::vec3;
namespace la = manifold::la;

constexpr double kSqrt2 = 1.4142135623730951;
constexpr double kPi = std::numbers::pi;
constexpr std::uint64_t kNoIndex = ~static_cast<std::uint64_t>(0);

// the corner at k of a list of face corners (x, y, z floats)
vec3 cornerAt(const std::vector<float>& c, std::size_t k) {
  return {static_cast<double>(c[k]), static_cast<double>(c[k + 1]), static_cast<double>(c[k + 2])};
}

// The shape's surface, exactly: the cell faces whose neighbour is not
// filled (at bevel 0 and offset 0, the same enumeration cellMesh uses).
// The faces between two filled cells cancel by identity here and never
// reach a boolean, whose float arithmetic would have to rediscover them
// and now and then leaves a seam. Corners are shared between a cell's own
// faces and across the faces shared with filled neighbours (their corner
// coordinates agree exactly); two cells that touch only along an edge or
// at a corner keep separate vertices there, which is what a manifold mesh
// needs.
Manifold exactBody(const Voxel& v, std::string& err) {
  std::vector<vec3> corners;
  std::vector<std::size_t> parent;
  const auto find = [&parent](std::size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  // (cell, exact position) -> corner
  std::map<std::pair<unsigned, std::tuple<float, float, float>>, std::size_t> slot;
  const auto corner = [&](unsigned cell, const vec3& p) {
    const auto key = std::make_pair(cell, std::make_tuple(static_cast<float>(p.x),
                                                          static_cast<float>(p.y),
                                                          static_cast<float>(p.z)));
    const auto it = slot.find(key);
    if (it != slot.end()) {
      return it->second;
    }
    corners.push_back(p);
    parent.push_back(corners.size() - 1);
    slot[key] = corners.size() - 1;
    return corners.size() - 1;
  };
  const auto filled = [&v](int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < asInt(v.getX()) && y < asInt(v.getY()) &&
           z < asInt(v.getZ()) && v.validCoordinate(x, y, z) &&
           v.isFilled(asUnsigned(x), asUnsigned(y), asUnsigned(z));
  };
  std::vector<std::vector<std::size_t>> faces;
  std::vector<float> fc;
  int nx = 0;
  int ny = 0;
  int nz = 0;
  for (unsigned z = 0; z < v.getZ(); z++) {
    for (unsigned y = 0; y < v.getY(); y++) {
      for (unsigned x = 0; x < v.getX(); x++) {
        if (!filled(asInt(x), asInt(y), asInt(z))) {
          continue;
        }
        const unsigned cell = v.getIndex(x, y, z);
        for (unsigned n = 0; v.getNeighbor(n, 0, asInt(x), asInt(y), asInt(z), &nx, &ny, &nz);
             n++) {
          fc.clear();
          connectionFace(v, asInt(x), asInt(y), asInt(z), asInt(n), 0, 0, fc);
          if (fc.size() < 9) {
            continue;
          }
          std::vector<std::size_t> face;
          for (std::size_t k = 0; k + 2 < fc.size(); k += 3) {
            face.push_back(corner(cell, cornerAt(fc, k)));
          }
          if (filled(nx, ny, nz)) {
            // an interior face: its corners are the neighbour's too
            const unsigned other = v.getIndex(asUnsigned(nx), asUnsigned(ny), asUnsigned(nz));
            for (const std::size_t c : face) {
              const std::size_t o = corner(other, corners[c]);
              parent[find(c)] = find(o);
            }
          } else {
            faces.push_back(face);
          }
        }
      }
    }
  }
  manifold::MeshGL64 mesh;
  mesh.numProp = 3;
  std::vector<std::uint64_t> index(corners.size(), kNoIndex);
  const auto vertex = [&](std::size_t c) {
    const std::size_t r = find(c);
    if (index[r] == kNoIndex) {
      index[r] = mesh.vertProperties.size() / 3;
      mesh.vertProperties.push_back(corners[r].x);
      mesh.vertProperties.push_back(corners[r].y);
      mesh.vertProperties.push_back(corners[r].z);
    }
    return index[r];
  };
  for (const std::vector<std::size_t>& face : faces) {
    for (std::size_t k = 1; k + 1 < face.size(); k++) {
      mesh.triVerts.push_back(vertex(face[0]));
      mesh.triVerts.push_back(vertex(face[k]));
      mesh.triVerts.push_back(vertex(face[k + 1]));
    }
  }
  Manifold m(mesh);
  if (m.Status() != Manifold::Error::NoError) {
    err = "the shape's cell faces do not close up into a solid";
    return {};
  }
  if (m.Volume() < 0) {
    // the faces were wound the other way round
    for (std::size_t t = 0; t + 2 < mesh.triVerts.size(); t += 3) {
      std::swap(mesh.triVerts[t + 1], mesh.triVerts[t + 2]);
    }
    m = Manifold(mesh);
  }
  return m;
}

// the distinct unit face normals of one cell (the grid's facet directions
// as far as this cell shows them)
void cellNormals(const Voxel& v, int x, int y, int z, std::vector<vec3>& out) {
  std::vector<float> corners;
  int nx = 0;
  int ny = 0;
  int nz = 0;
  for (unsigned n = 0; v.getNeighbor(n, 0, x, y, z, &nx, &ny, &nz); n++) {
    corners.clear();
    connectionFace(v, x, y, z, asInt(n), 0, 0, corners);
    if (corners.size() < 9) {
      continue;
    }
    const vec3 a = cornerAt(corners, 0);
    const vec3 b = cornerAt(corners, 3);
    const vec3 c = cornerAt(corners, 6);
    vec3 nn = la::cross(b - a, c - a);
    const double l = la::length(nn);
    if (l < 1e-12) {
      continue;
    }
    nn = nn / l;
    bool seen = false;
    for (const vec3& o : out) {
      if (la::dot(o, nn) > 1 - 1e-6) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      out.push_back(nn);
    }
  }
}

// The vertex sets at unit size: the offset bit (reach 1 along every facet
// direction), the chamfer bit of the variant without interior chamfers
// and the one of the variant with them; k and kFill their multipliers.
struct Bits {
  std::vector<vec3> offset;
  std::vector<vec3> chamfer;
  std::vector<vec3> chamferFill;
  double k = 0;
  double kFill = 0;
  bool prism = false;  // the prism grid's bits, with their own sequences
};

// rhombic and tetra-octa: cube and cuboctahedron
Bits cubeBits() {
  Bits b;
  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      for (int sz = -1; sz <= 1; sz += 2) {
        b.offset.emplace_back(sx, sy, sz);
      }
    }
  }
  // the polar body of (100), (110) at 1 and (111) at (4 - sqrt2)/sqrt6:
  // the 24 vertices (+-1, +-(sqrt2-1), +-(sqrt2-1)) and permutations
  const double q = kSqrt2 - 1;
  for (int ax = 0; ax < 3; ax++) {
    for (int s0 = -1; s0 <= 1; s0 += 2) {
      for (int s1 = -1; s1 <= 1; s1 += 2) {
        for (int s2 = -1; s2 <= 1; s2 += 2) {
          double p[3];
          p[ax] = s0;
          p[(ax + 1) % 3] = s1 * q;
          p[(ax + 2) % 3] = s2 * q;
          b.chamfer.emplace_back(p[0], p[1], p[2]);
        }
      }
    }
  }
  b.k = 1 + (1 / kSqrt2);
  // with interior chamfers the octahedron, vertices at 1 on the axes: its
  // edges sweep a cube's edges to bevels with legs exactly the size
  for (int ax = 0; ax < 3; ax++) {
    for (int s0 = -1; s0 <= 1; s0 += 2) {
      double p[3] = {0, 0, 0};
      p[ax] = s0;
      b.chamferFill.emplace_back(p[0], p[1], p[2]);
    }
  }
  b.kFill = 1;
  b.prism = false;
  return b;
}

// prism: the hexagonal prism with faces on the grid's facet directions and
// the hexagonal bipyramid aligned to it, from the in-plane facet normals
// of a cell and the prism axis
bool prismBits(const std::vector<vec3>& normals, Bits& b, std::string& err) {
  // The axis: the coordinate direction every facet normal is either
  // parallel or perpendicular to (an in-plane facet can be axis aligned
  // too, but the other in-plane facets are not perpendicular to it).
  int axis = -1;
  for (int a = 0; a < 3 && axis < 0; a++) {
    bool ok = true;
    for (const vec3& n : normals) {
      const double d = std::fabs(n[a]);
      if (d > 1e-6 && std::fabs(d - 1) > 1e-6) {
        ok = false;
      }
    }
    if (ok) {
      axis = a;
    }
  }
  if (axis < 0) {
    err = "prism grid: no prism axis among the facet normals";
    return false;
  }
  const int u = (axis + 1) % 3;
  const int w = (axis + 2) % 3;
  std::vector<double> az;  // in-plane facet azimuths, both signs
  for (const vec3& n : normals) {
    if (std::fabs(n[axis]) > 1e-6) {
      continue;
    }
    for (int s = -1; s <= 1; s += 2) {
      const double a = std::atan2(s * n[w], s * n[u]);
      bool seen = false;
      for (const double o : az) {
        if (std::fabs(std::remainder(o - a, 2 * kPi)) < 1e-6) {
          seen = true;
        }
      }
      if (!seen) {
        az.push_back(a);
      }
    }
  }
  if (az.size() != 6) {
    err = "prism grid: expected 6 in-plane facet directions";
    return false;
  }
  std::sort(az.begin(), az.end());
  const double c30 = std::cos(kPi / 6);
  for (int s = -1; s <= 1; s += 2) {
    for (const double a : az) {
      // the prism's vertices lie between adjacent facet directions
      const double m = a + (kPi / 6);
      const double r = 1 / c30;
      double p[3];
      p[u] = r * std::cos(m);
      p[w] = r * std::sin(m);
      p[axis] = s;
      b.offset.emplace_back(p[0], p[1], p[2]);
    }
  }
  for (const double a : az) {
    // The bipyramid's equatorial vertices between the facet directions,
    // apexes at the equatorial apothem: faces on the facet directions at
    // 45 degrees, reach 1 along every facet normal and along the axis.
    const double m = a + (kPi / 6);
    const double r = 1 / c30;
    double p[3];
    p[u] = r * std::cos(m);
    p[w] = r * std::sin(m);
    p[axis] = 0;
    b.chamfer.emplace_back(p[0], p[1], p[2]);
  }
  for (int s = -1; s <= 1; s += 2) {
    double p[3] = {0, 0, 0};
    p[axis] = s;
    b.chamfer.emplace_back(p[0], p[1], p[2]);
  }
  b.k = 1;
  // the bipyramid carries no face on a facet direction, so it is the
  // chamfer bit of both variants
  b.chamferFill = b.chamfer;
  b.kFill = 1;
  b.prism = true;
  return true;
}

// The bit's vertices at the given size, turned by a hundredth of a
// microradian about a skew axis. Untouched, the bits' faces are exactly
// parallel to the grid's facets, and the sweeps of different pieces pile
// up several exactly coincident faces on one plane wherever faces meet;
// Manifold's boolean then leaves garbage there now and then: slivers
// facing each other across nothing, which the next step opens into a
// slice through the shape, and unions whose volume depends on the order
// of the operands. Turned, no two sweeps of different kinds are coplanar
// any more, the unions agree to a part in a billion and carry no such
// slivers; the turn moves nothing by more than a nanometre per metre, far
// inside what the rebuild snaps away.
std::vector<vec3> bitPoints(const std::vector<vec3>& unit, double size, int attempt) {
  // a later attempt turns a little more, and about a different axis, which
  // changes every tie the boolean has to break
  const double angle = 1e-8 * (1 + attempt);
  const vec3 axis = la::normalize(
      vec3(0.3 + (0.17 * attempt), 0.5 - (0.11 * attempt), 0.8 + (0.07 * attempt)));
  const double c = std::cos(angle);
  const double s = std::sin(angle);
  std::vector<vec3> pts;
  for (const vec3& p : unit) {
    // Rodrigues
    const vec3 r = p * c + la::cross(axis, p) * s + axis * (la::dot(axis, p) * (1 - c));
    pts.push_back(r * size);
  }
  return pts;
}

// The planar faces of a solid, from its triangle mesh, and the solid
// rebuilt from them.
//
// The booleans leave every face shattered: slivers along the seams where
// the hulls of the sweep meet in one plane, vertices a few nanometres
// apart, triangles whose normals mean nothing. Fed back into the next step
// as they are, the slivers sweep into garbage-direction facets and the
// fragmentation compounds (a small prism shape reached forty thousand
// triangles). So after every boolean the solid is put back onto its
// planes: vertices welded, the planes taken from the fat triangles, every
// triangle assigned to the plane it lies on (a sliver to its neighbour's),
// every vertex snapped onto the intersection of its planes, and each
// planar face re-triangulated from its outline loops. All loop vertices
// are kept, so the faces stay edge-consistent with their neighbours and
// the rebuilt mesh is a manifold; the slivers have become exactly flat and
// are gone.
//
// The same faces give the sweep of the boundary by a convex bit: the union
// of one hull per convex piece of each face, of which a face has few (one
// hull per triangle of the shattered mesh bogs the union down; Manifold's
// built-in Minkowski does exactly that).
struct Faces {
  std::vector<vec3> pos;                   // welded, snapped onto their planes
  std::vector<std::uint64_t> tri;          // the rebuilt triangulation, index triples
  std::vector<std::uint64_t> orig;         // the input's own triangulation, welded
  std::vector<std::vector<vec3>> pieces;   // convex pieces of the faces
};

// triangles of one planar face (CCW in the 2D frame) merged greedily
// across shared edges while the union stays convex; collinear outline
// vertices are allowed
void convexPieces(const std::vector<manifold::vec2>& p2, const std::vector<int>& tt,
                  std::vector<std::vector<int>>& out) {
  const auto P = [&p2](int i) -> const manifold::vec2& { return p2[asUnsigned(i)]; };
  std::vector<std::vector<int>> poly;
  for (std::size_t t = 0; t + 2 < tt.size(); t += 3) {
    std::vector<int> p = {tt[t], tt[t + 1], tt[t + 2]};
    const double a = ((P(p[1]).x - P(p[0]).x) * (P(p[2]).y - P(p[0]).y)) -
                     ((P(p[1]).y - P(p[0]).y) * (P(p[2]).x - P(p[0]).x));
    if (std::fabs(a) < 1e-18) {
      continue;
    }
    if (a < 0) {
      std::swap(p[1], p[2]);
    }
    poly.push_back(p);
  }
  std::vector<bool> alive(poly.size(), true);
  std::map<std::pair<int, int>, std::size_t> edgeOwner;
  for (std::size_t i = 0; i < poly.size(); i++) {
    for (std::size_t c = 0; c < 3; c++) {
      edgeOwner[std::make_pair(poly[i][c], poly[i][(c + 1) % 3])] = i;
    }
  }
  const auto convex = [&](const std::vector<int>& p) {
    std::set<int> seen;
    for (const int v : p) {
      if (!seen.insert(v).second) {
        return false;
      }
    }
    double scale = 0;
    for (const int i : p) {
      scale = std::max(scale, std::fabs(P(i).x) + std::fabs(P(i).y));
    }
    for (std::size_t i = 0; i < p.size(); i++) {
      const manifold::vec2& a = P(p[i]);
      const manifold::vec2& b = P(p[(i + 1) % p.size()]);
      const manifold::vec2& c = P(p[(i + 2) % p.size()]);
      const double cr = ((b.x - a.x) * (c.y - b.y)) - ((b.y - a.y) * (c.x - b.x));
      if (cr < -1e-12 * scale * scale) {
        return false;
      }
    }
    return true;
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < poly.size(); i++) {
      if (!alive[i]) {
        continue;
      }
      for (std::size_t c = 0; c < poly[i].size(); c++) {
        const int a = poly[i][c];
        const int b = poly[i][(c + 1) % poly[i].size()];
        const auto it = edgeOwner.find(std::make_pair(b, a));
        if (it == edgeOwner.end() || it->second == i || !alive[it->second]) {
          continue;
        }
        const std::vector<int>& q = poly[it->second];
        // q runs b -> a -> q1 .. qk -> b; the merged outline replaces
        // a -> b in poly[i] by a -> q1 .. qk -> b
        std::size_t ia = 0;
        while (q[ia] != a) {
          ia++;
        }
        std::vector<int> merged;
        for (std::size_t d = 1; d <= poly[i].size(); d++) {
          merged.push_back(poly[i][(c + d) % poly[i].size()]);  // b .. a
        }
        for (std::size_t d = 1; d + 1 < q.size(); d++) {
          merged.push_back(q[(ia + d) % q.size()]);  // q1 .. qk
        }
        if (!convex(merged)) {
          continue;
        }
        const std::size_t j = it->second;
        for (std::size_t d = 0; d < q.size(); d++) {
          edgeOwner.erase(std::make_pair(q[d], q[(d + 1) % q.size()]));
        }
        for (std::size_t d = 0; d < poly[i].size(); d++) {
          edgeOwner.erase(std::make_pair(poly[i][d], poly[i][(d + 1) % poly[i].size()]));
        }
        poly[i] = merged;
        alive[j] = false;
        for (std::size_t d = 0; d < merged.size(); d++) {
          edgeOwner[std::make_pair(merged[d], merged[(d + 1) % merged.size()])] = i;
        }
        changed = true;
        break;
      }
    }
  }
  for (std::size_t i = 0; i < poly.size(); i++) {
    if (alive[i]) {
      out.push_back(poly[i]);
    }
  }
}

void extractFaces(const Manifold& m, Faces& out) {
  std::vector<vec3>& pos = out.pos;
  std::vector<std::uint64_t>& tv = out.orig;
  out.tri.clear();
  out.orig.clear();
  out.pieces.clear();
  pos.clear();
  {
    const manifold::MeshGL64 mesh = m.GetMeshGL64();
    const std::size_t np = mesh.numProp;
    for (std::size_t i = 0; i * np < mesh.vertProperties.size(); i++) {
      pos.emplace_back(mesh.vertProperties[i * np], mesh.vertProperties[(i * np) + 1],
                       mesh.vertProperties[(i * np) + 2]);
    }
    tv = mesh.triVerts;
  }
  // Short edges contracted: kWeld is far above the noise the booleans
  // leave (nanometres) and far below any feature of a chamfer. Contracting
  // edges keeps the mesh a manifold; welding by proximity alone does not,
  // since two vertices of a crack a few nanometres wide are not joined by
  // an edge, and merging them folds the surface.
  const double kWeld = 2e-6;
  const auto contract = [&]() {
    const std::size_t nv = pos.size();
    std::vector<std::uint64_t> parentV(nv);
    for (std::size_t i = 0; i < nv; i++) {
      parentV[i] = i;
    }
    const auto findV = [&parentV](std::uint64_t i) {
      while (parentV[i] != i) {
        parentV[i] = parentV[parentV[i]];
        i = parentV[i];
      }
      return i;
    };
    // An edge is contracted only when its two vertices have exactly the
    // two apexes of its triangles as common neighbours (the link
    // condition): otherwise the collapse folds the surface, and two
    // triangles end up sharing a directed edge, which cuts the faces apart
    // in everything that follows.
    std::vector<std::set<std::uint64_t>> adj(nv);
    for (std::size_t k = 0; k + 2 < tv.size(); k += 3) {
      for (std::size_t c = 0; c < 3; c++) {
        adj[tv[k + c]].insert(tv[k + ((c + 1) % 3)]);
        adj[tv[k + ((c + 1) % 3)]].insert(tv[k + c]);
      }
    }
    for (std::size_t k = 0; k + 2 < tv.size(); k += 3) {
      for (std::size_t c = 0; c < 3; c++) {
        const std::uint64_t a = findV(tv[k + c]);
        const std::uint64_t b = findV(tv[k + ((c + 1) % 3)]);
        if (a == b || la::length(pos[a] - pos[b]) > kWeld) {
          continue;
        }
        std::size_t common = 0;
        for (const std::uint64_t x : adj[a]) {
          if (x != b && adj[b].contains(x)) {
            common++;
          }
        }
        if (common != 2) {
          continue;
        }
        // b into a
        for (const std::uint64_t x : adj[b]) {
          adj[x].erase(b);
          if (x != a) {
            adj[x].insert(a);
            adj[a].insert(x);
          }
        }
        adj[a].erase(b);
        adj[b].clear();
        parentV[b] = a;
      }
    }
    std::vector<std::uint64_t> remap(nv, kNoIndex);
    std::vector<vec3> np;
    for (std::size_t i = 0; i < nv; i++) {
      const std::uint64_t r = findV(i);
      if (remap[r] == kNoIndex) {
        remap[r] = np.size();
        np.push_back(pos[r]);
      }
      remap[i] = remap[r];
    }
    std::vector<std::uint64_t> nt;
    for (std::size_t k = 0; k + 2 < tv.size(); k += 3) {
      const std::uint64_t a = remap[tv[k]];
      const std::uint64_t b = remap[tv[k + 1]];
      const std::uint64_t c = remap[tv[k + 2]];
      if (a == b || b == c || a == c) {
        continue;
      }
      nt.push_back(a);
      nt.push_back(b);
      nt.push_back(c);
    }
    pos.swap(np);
    tv.swap(nt);
  };
  std::size_t nt = 0;
  const auto P = [&](std::size_t t, std::size_t c) -> const vec3& { return pos[tv[(3 * t) + c]]; };
  std::vector<double> area;
  std::vector<double> altitude;
  std::vector<vec3> nrm;
  struct Plane {
    vec3 n{};
    double d = 0;
  };
  std::vector<Plane> planes;
  const double kTol = 1e-6;
  std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> owner;
  std::vector<int> plane;
  const auto onPlane = [&](std::size_t t, const Plane& p) {
    for (std::size_t c = 0; c < 3; c++) {
      if (std::fabs(la::dot(p.n, P(t, c)) - p.d) > kTol) {
        return false;
      }
    }
    return true;
  };
  const auto neighbour = [&](std::size_t t, std::size_t c) -> long {
    const auto rv =
        owner.find(std::make_pair(tv[(3 * t) + ((c + 1) % 3)], tv[(3 * t) + c]));
    return rv == owner.end() ? -1 : static_cast<long>(rv->second);
  };
  // The planes, each from its largest triangle; only triangles with a
  // decent altitude define one (a sliver's normal is noise), and the
  // planes of a Minkowski result on a grid are degrees apart, so a plane
  // already seen is recognised generously. Every triangle to the plane it
  // lies on, nearest its own normal; a triangle on no plane (a sliver
  // whose vertices are on several) takes the plane of a neighbour across
  // an edge that it does lie on. A triangle on no plane at all (a spike
  // the boolean left, with a vertex well off every plane) is kept as it
  // is: put into a planar group it would make that group's outline leave
  // the plane, and the re-triangulation folds. Then the vertices snapped
  // onto their planes: the least displacement that puts the vertex on
  // every plane it is already close to (up to three independent ones; the
  // rest agree at a true vertex). A vertex that would have to move far is
  // left alone.
  const auto fit = [&]() {
    nt = tv.size() / 3;
    area.assign(nt, 0);
    altitude.assign(nt, 0);
    nrm.assign(nt, vec3(0, 0, 0));
    std::vector<std::size_t> order;
    for (std::size_t t = 0; t < nt; t++) {
      const vec3 n = la::cross(P(t, 1) - P(t, 0), P(t, 2) - P(t, 0));
      const double l = la::length(n);
      area[t] = l / 2;
      nrm[t] = l > 1e-300 ? n / l : vec3(0, 0, 0);
      double longest = 0;
      for (std::size_t c = 0; c < 3; c++) {
        longest = std::max(longest, la::length(P(t, (c + 1) % 3) - P(t, c)));
      }
      altitude[t] = longest > 0 ? l / longest : 0;
      order.push_back(t);
    }
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return area[a] > area[b]; });
    planes.clear();
    for (const std::size_t t : order) {
      if (altitude[t] < 1e-4) {
        continue;
      }
      const double d = la::dot(nrm[t], P(t, 0));
      bool found = false;
      for (const Plane& p : planes) {
        if (la::dot(p.n, nrm[t]) > 1 - 1e-6 && std::fabs(p.d - d) < kTol) {
          found = true;
          break;
        }
      }
      if (!found) {
        planes.push_back({nrm[t], d});
      }
    }
    owner.clear();
    for (std::size_t t = 0; t < nt; t++) {
      for (std::size_t c = 0; c < 3; c++) {
        owner[std::make_pair(tv[(3 * t) + c], tv[(3 * t) + ((c + 1) % 3)])] = t;
      }
    }
    plane.assign(nt, -1);
    for (std::size_t t = 0; t < nt; t++) {
      double best = -2;
      int bk = -1;
      for (std::size_t k = 0; k < planes.size(); k++) {
        const double d = la::dot(planes[k].n, nrm[t]);
        if (d > best && onPlane(t, planes[k])) {
          best = d;
          bk = static_cast<int>(k);
        }
      }
      plane[t] = bk;
    }
    {
      bool changed = true;
      while (changed) {
        changed = false;
        for (std::size_t t = 0; t < nt; t++) {
          if (plane[t] >= 0) {
            continue;
          }
          for (std::size_t c = 0; c < 3 && plane[t] < 0; c++) {
            const long u = neighbour(t, c);
            if (u < 0 || plane[static_cast<std::size_t>(u)] < 0) {
              continue;
            }
            if (onPlane(t, planes[asUnsigned(plane[static_cast<std::size_t>(u)])])) {
              plane[t] = plane[static_cast<std::size_t>(u)];
              changed = true;
            }
          }
        }
      }
    }
    // Every plane the vertex is close to counts, not only those of its own
    // triangles: a vertex on the line of two faces whose own triangles
    // happen to lie on only one of them would otherwise be left a few
    // nanometres off that line, and the triangle along the line stays a
    // sliver instead of going flat.
    for (std::size_t v = 0; v < pos.size(); v++) {
      std::vector<vec3> N;
      std::vector<double> D;
      std::vector<vec3> basis;
      for (const Plane& pl : planes) {
        if (std::fabs(la::dot(pl.n, pos[v]) - pl.d) > kTol) {
          continue;
        }
        vec3 r = pl.n;
        for (const vec3& e : basis) {
          r = r - e * la::dot(e, r);
        }
        if (la::length(r) < 0.1) {
          continue;
        }
        basis.push_back(r / la::length(r));
        N.push_back(pl.n);
        D.push_back(pl.d);
        if (N.size() == 3) {
          break;
        }
      }
      if (N.empty()) {
        continue;
      }
      // (N N^T) lambda = D - N v, x = v + N^T lambda; Gaussian elimination, r <= 3
      const std::size_t r = N.size();
      double A[3][4];
      for (std::size_t i = 0; i < r; i++) {
        for (std::size_t j = 0; j < r; j++) {
          A[i][j] = la::dot(N[i], N[j]);
        }
        A[i][r] = D[i] - la::dot(N[i], pos[v]);
      }
      for (std::size_t i = 0; i < r; i++) {
        std::size_t piv = i;
        for (std::size_t j = i + 1; j < r; j++) {
          if (std::fabs(A[j][i]) > std::fabs(A[piv][i])) {
            piv = j;
          }
        }
        for (std::size_t j = 0; j <= r; j++) {
          std::swap(A[i][j], A[piv][j]);
        }
        for (std::size_t j = i + 1; j < r; j++) {
          const double f = A[j][i] / A[i][i];
          for (std::size_t k = i; k <= r; k++) {
            A[j][k] -= f * A[i][k];
          }
        }
      }
      double lambda[3];
      for (std::size_t i = r; i-- > 0;) {
        double s = A[i][r];
        for (std::size_t j = i + 1; j < r; j++) {
          s -= A[i][j] * lambda[j];
        }
        lambda[i] = s / A[i][i];
      }
      vec3 x = pos[v];
      for (std::size_t i = 0; i < r; i++) {
        x = x + N[i] * lambda[i];
      }
      if (la::length(x - pos[v]) > 1e-5) {
        continue;
      }
      pos[v] = x;
    }
  };
  // twice: snapping can put two vertices of a sliver onto the same point,
  // and the second contraction merges them
  for (int pass = 0; pass < 2; pass++) {
    contract();
    fit();
  }
  // union-find over shared edges between triangles on the same plane
  std::vector<std::size_t> parent(nt);
  for (std::size_t t = 0; t < nt; t++) {
    parent[t] = t;
  }
  const auto find = [&parent](std::size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  for (std::size_t t = 0; t < nt; t++) {
    for (std::size_t c = 0; c < 3; c++) {
      const long u = neighbour(t, c);
      if (u >= 0 && plane[t] >= 0 && plane[static_cast<std::size_t>(u)] == plane[t]) {
        parent[find(t)] = find(static_cast<std::size_t>(u));
      }
    }
  }
  std::map<std::size_t, std::vector<std::size_t>> groups;
  for (std::size_t t = 0; t < nt; t++) {
    groups[find(t)].push_back(t);
  }
  // a group that cannot be rebuilt keeps its own triangles (still
  // consistent with its neighbours) and hulls each of them
  const auto raw = [&](const std::vector<std::size_t>& members) {
    for (const std::size_t t : members) {
      for (std::size_t c = 0; c < 3; c++) {
        out.tri.push_back(tv[(3 * t) + c]);
      }
      const vec3 n = la::cross(P(t, 1) - P(t, 0), P(t, 2) - P(t, 0));
      if (la::length(n) < 2e-10) {
        continue;  // flat: its neighbours' hulls cover it
      }
      std::vector<vec3> piece;
      for (std::size_t c = 0; c < 3; c++) {
        piece.push_back(P(t, c));
      }
      out.pieces.push_back(piece);
    }
  };
  for (const auto& g : groups) {
    const std::vector<std::size_t>& members = g.second;
    if (plane[members[0]] < 0) {
      raw(members);
      continue;
    }
    const vec3 n = planes[asUnsigned(plane[members[0]])].n;
    // The outline: directed edges without their reverse inside the group,
    // walked into loops; where the outline touches itself a vertex has
    // several outgoing edges and the walk takes the sharpest left turn
    // (about the normal), which keeps each loop simple.
    const std::set<std::size_t> inGroup(members.begin(), members.end());
    std::multimap<std::uint64_t, std::uint64_t> next;
    for (const std::size_t t : members) {
      for (std::size_t c = 0; c < 3; c++) {
        const long u = neighbour(t, c);
        if (u >= 0 && inGroup.contains(static_cast<std::size_t>(u))) {
          continue;
        }
        next.insert(std::make_pair(tv[(3 * t) + c], tv[(3 * t) + ((c + 1) % 3)]));
      }
    }
    std::vector<std::vector<std::uint64_t>> loops;
    std::set<std::pair<std::uint64_t, std::uint64_t>> used;
    bool bad = false;
    for (const auto& e0 : next) {
      if (used.contains(e0)) {
        continue;
      }
      std::vector<std::uint64_t> loop;
      std::pair<std::uint64_t, std::uint64_t> e = e0;
      std::size_t guard = 0;
      bool closed = false;
      while (true) {
        used.insert(e);
        loop.push_back(e.first);
        const std::uint64_t v = e.second;
        if (v == e0.first) {
          closed = true;
          break;
        }
        const auto range = next.equal_range(v);
        std::pair<std::uint64_t, std::uint64_t> pick(0, 0);
        bool have = false;
        double bestAng = -10;
        const vec3 din = pos[v] - pos[e.first];
        for (auto it = range.first; it != range.second; ++it) {
          const std::pair<std::uint64_t, std::uint64_t> cand(it->first, it->second);
          if (used.contains(cand)) {
            continue;
          }
          const vec3 dout = pos[cand.second] - pos[v];
          const double ang = std::atan2(la::dot(la::cross(din, dout), n), la::dot(din, dout));
          if (!have || ang > bestAng) {
            bestAng = ang;
            pick = cand;
            have = true;
          }
        }
        if (!have) {
          break;
        }
        e = pick;
        if (++guard > next.size() + 1) {
          break;
        }
      }
      if (!closed) {
        bad = true;
        break;
      }
      loops.push_back(loop);
    }
    if (bad || loops.empty()) {
      raw(members);
      continue;
    }
    // Loops too short to bound anything mean a walk gone wrong; a group
    // whose loops all run backwards (a sheet facing away from its plane's
    // normal, a sliver artefact) is kept as it is; otherwise every loop
    // goes to the triangulator, which sorts outlines from holes by their
    // orientation, and accepts several outlines touching at a vertex.
    bool degenerate = false;
    bool forward = false;
    std::vector<std::vector<std::uint64_t>> kept;
    for (const std::vector<std::uint64_t>& loop : loops) {
      if (loop.size() < 3) {
        degenerate = true;
        break;
      }
      vec3 a(0, 0, 0);
      for (std::size_t q = 0; q < loop.size(); q++) {
        a = a + la::cross(pos[loop[q]], pos[loop[(q + 1) % loop.size()]]);
      }
      if (la::dot(a, n) > 1e-14) {
        forward = true;
      }
      kept.push_back(loop);
    }
    if (degenerate || !forward) {
      raw(members);
      continue;
    }
    // the loops must enclose exactly the group's own area, or the walk has
    // gone wrong somewhere and the pieces would sweep phantom surface into
    // the solid
    {
      double own = 0;
      double enclosed = 0;
      for (const std::size_t t : members) {
        own += la::length(la::cross(P(t, 1) - P(t, 0), P(t, 2) - P(t, 0))) / 2;
      }
      for (const std::vector<std::uint64_t>& loop : kept) {
        vec3 a(0, 0, 0);
        for (std::size_t q = 0; q < loop.size(); q++) {
          a = a + la::cross(pos[loop[q]], pos[loop[(q + 1) % loop.size()]]);
        }
        enclosed += la::dot(a, n) / 2;
      }
      if (std::fabs(enclosed - own) > 1e-9 * std::max(1.0, own)) {
        raw(members);
        continue;
      }
    }
    // 2D projection and triangulation
    vec3 ax(1, 0, 0);
    if (std::fabs(n.y) < std::fabs(n.x)) {
      ax = vec3(0, 1, 0);
    }
    if (std::fabs(n.z) < std::fabs(la::dot(ax, n))) {
      ax = vec3(0, 0, 1);
    }
    vec3 u1 = ax - n * la::dot(ax, n);
    u1 = u1 / la::length(u1);
    const vec3 u2 = la::cross(n, u1);
    std::vector<manifold::vec2> p2;
    std::vector<std::uint64_t> flat;
    std::vector<std::vector<int>> idx(kept.size());
    for (std::size_t l = 0; l < kept.size(); l++) {
      for (const std::uint64_t v : kept[l]) {
        const manifold::vec2 q(la::dot(pos[v], u1), la::dot(pos[v], u2));
        idx[l].push_back(static_cast<int>(p2.size()));
        p2.push_back(q);
        flat.push_back(v);
      }
    }
    // Manifold's own polygon triangulator: it keeps every loop edge (a
    // collinear vertex gives a flat triangle, flipped away below), so the
    // face stays consistent with its neighbours across the outline.
    std::vector<int> tt;
    {
      manifold::Polygons polys(kept.size());
      for (std::size_t l = 0; l < kept.size(); l++) {
        for (const int i : idx[l]) {
          polys[l].push_back(p2[asUnsigned(i)]);
        }
      }
      std::vector<manifold::ivec3> tri3;
      try {
        tri3 = manifold::Triangulate(polys, 1e-9);
      } catch (...) {
        raw(members);
        continue;
      }
      if (tri3.empty()) {
        raw(members);
        continue;
      }
      for (const manifold::ivec3& t3 : tri3) {
        tt.push_back(t3.x);
        tt.push_back(t3.y);
        tt.push_back(t3.z);
      }
    }
    // and the triangulation must cover exactly that area
    {
      double covered = 0;
      for (std::size_t k = 0; k + 2 < tt.size(); k += 3) {
        const manifold::vec2& a = p2[asUnsigned(tt[k])];
        const manifold::vec2& b = p2[asUnsigned(tt[k + 1])];
        const manifold::vec2& c = p2[asUnsigned(tt[k + 2])];
        covered += (((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x))) / 2;
      }
      double own = 0;
      for (const std::size_t t : members) {
        own += la::length(la::cross(P(t, 1) - P(t, 0), P(t, 2) - P(t, 0))) / 2;
      }
      if (std::fabs(covered - own) > 1e-9 * std::max(1.0, own)) {
        raw(members);
        continue;
      }
    }
    for (const int i : tt) {
      out.tri.push_back(flat[asUnsigned(i)]);
    }
    std::vector<std::vector<int>> cp;
    convexPieces(p2, tt, cp);
    for (const std::vector<int>& p : cp) {
      std::vector<vec3> piece;
      for (const int i : p) {
        piece.push_back(pos[flat[asUnsigned(i)]]);
      }
      out.pieces.push_back(piece);
    }
  }
}

Vec3 toVec3(const vec3& p) {
  return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
}

// Flat triangles flipped away: the triangulation of a face with a vertex
// on a straight run of its outline can carry a triangle with no area
// across that vertex (a needle along the line), which single precision
// cannot tell from nothing and the STL writer chokes on. The edge opposite
// the middle vertex is flipped with the neighbour across it, which is on
// the same plane, giving two triangles with area. A flat triangle whose
// neighbour is not coplanar is left (and reported later).
void flipFlat(const std::vector<vec3>& pos, std::vector<std::uint64_t>& tri) {
  std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> owner;
  const auto own = [&](std::size_t t) {
    for (std::size_t c = 0; c < 3; c++) {
      owner[std::make_pair(tri[(3 * t) + c], tri[(3 * t) + ((c + 1) % 3)])] = t;
    }
  };
  const auto disown = [&](std::size_t t) {
    for (std::size_t c = 0; c < 3; c++) {
      owner.erase(std::make_pair(tri[(3 * t) + c], tri[(3 * t) + ((c + 1) % 3)]));
    }
  };
  for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
    own(t / 3);
  }
  const auto flat = [&](std::size_t t) {
    const vec3& a = pos[tri[3 * t]];
    const vec3& b = pos[tri[(3 * t) + 1]];
    const vec3& c = pos[tri[(3 * t) + 2]];
    const double l = la::length(la::cross(b - a, c - a));
    const double e = std::max(la::length(b - a), std::max(la::length(c - b), la::length(a - c)));
    const Vec3 fa = toVec3(a);
    const Vec3 fb = toVec3(b);
    const Vec3 fc = toVec3(c);
    return l < 1e-12 * e * e || (fb - fa).cross(fc - fa).squaredModule() == 0;
  };
  for (int round = 0; round < 8; round++) {
    bool any = false;
    for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
      const std::size_t ti = t / 3;
      if (!flat(ti)) {
        continue;
      }
      // the middle vertex is opposite the longest edge
      std::size_t m = 0;
      double longest = -1;
      for (std::size_t c = 0; c < 3; c++) {
        const double e = la::length(pos[tri[t + ((c + 1) % 3)]] - pos[tri[t + ((c + 2) % 3)]]);
        if (e > longest) {
          longest = e;
          m = c;
        }
      }
      const std::uint64_t M = tri[t + m];
      const std::uint64_t A = tri[t + ((m + 1) % 3)];
      const std::uint64_t B = tri[t + ((m + 2) % 3)];
      const auto it = owner.find(std::make_pair(B, A));
      if (it == owner.end() || it->second == ti) {
        continue;
      }
      const std::size_t u = it->second;
      std::uint64_t D = 0;
      for (std::size_t c = 0; c < 3; c++) {
        if (tri[(3 * u) + c] != A && tri[(3 * u) + c] != B) {
          D = tri[(3 * u) + c];
        }
      }
      // the neighbour on the same plane, and the flip giving two triangles
      // with area
      vec3 n = la::cross(pos[B] - pos[A], pos[D] - pos[A]);
      const double ln = la::length(n);
      if (ln < 1e-300) {
        continue;
      }
      n = n / ln;
      if (std::fabs(la::dot(n, pos[M] - pos[A])) > 1e-6) {
        continue;
      }
      disown(ti);
      disown(u);
      tri[t] = A;
      tri[t + 1] = D;
      tri[t + 2] = M;
      tri[3 * u] = D;
      tri[(3 * u) + 1] = B;
      tri[(3 * u) + 2] = M;
      own(ti);
      own(u);
      any = true;
    }
    if (!any) {
      break;
    }
  }
}

// Whether every directed edge of the triangles is used as often as the
// other way round: what an oriented (even) manifold needs. Manifold 3.5.2
// refuses a mesh without it (Status NotManifold), but on the way reads a
// half-edge's pair that was never set, at an index far outside its
// arrays (UBSan: vec_view.h, from Impl::IsManifold); so such a mesh is
// not handed to it. Not in the original, which handed it over: the
// outcome is the same refusal.
bool balanced(const std::vector<std::uint64_t>& tri) {
  std::map<std::pair<std::uint64_t, std::uint64_t>, long> uses;
  for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
    // a triangle with a corner twice is dropped by Manifold before it pairs
    if (tri[t] == tri[t + 1] || tri[t + 1] == tri[t + 2] || tri[t + 2] == tri[t]) {
      continue;
    }
    for (std::size_t c = 0; c < 3; c++) {
      const std::uint64_t a = tri[t + c];
      const std::uint64_t b = tri[t + ((c + 1) % 3)];
      // one count per edge: up for a -> b with a < b, down for the reverse
      if (a < b) {
        uses[{a, b}]++;
      } else {
        uses[{b, a}]--;
      }
    }
  }
  for (const auto& [edge, n] : uses) {
    if (n != 0) {
      return false;
    }
  }
  return true;
}

Manifold fromTriangles(const std::vector<vec3>& pos, const std::vector<std::uint64_t>& tri) {
  if (!balanced(tri)) {
    return {};  // empty: rebuilt() takes it as refused
  }
  manifold::MeshGL64 mesh;
  mesh.numProp = 3;
  mesh.vertProperties.reserve(pos.size() * 3);
  for (const vec3& p : pos) {
    mesh.vertProperties.push_back(p.x);
    mesh.vertProperties.push_back(p.y);
    mesh.vertProperties.push_back(p.z);
  }
  mesh.triVerts = tri;
  return Manifold(mesh);
}

// the solid rebuilt from its faces; if that is not a valid manifold the
// snapped original triangulation, and failing that the input itself
Manifold rebuilt(Faces& f, const Manifold& input) {
  if (f.tri.empty()) {
    return input;
  }
  flipFlat(f.pos, f.tri);
  Manifold r = fromTriangles(f.pos, f.tri);
  if (r.Status() == Manifold::Error::NoError && !r.IsEmpty()) {
    return r;
  }
  r = fromTriangles(f.pos, f.orig);
  if (r.Status() == Manifold::Error::NoError && !r.IsEmpty()) {
    return r;
  }
  return input;
}

// the sweep of the solid's boundary by the bit: one hull per convex piece
// of its planar faces
Manifold boundaryTube(const Faces& f, const std::vector<vec3>& bitPts) {
  std::vector<Manifold> hulls;
  for (const std::vector<vec3>& piece : f.pieces) {
    // A sliver piece (a notch or a seam a boolean left, thinner than any
    // feature) sweeps into a wedge whose faces are a hair off its
    // neighbours' and can end up facing each other across nothing; its
    // neighbours' hulls cover its sweep to within that hair, so it is left
    // out. Width: twice the area over the perimeter.
    vec3 c(0, 0, 0);
    for (const vec3& p : piece) {
      c = c + p;
    }
    c = c / static_cast<double>(piece.size());
    double area2 = 0;
    double perimeter = 0;
    for (std::size_t i = 0; i < piece.size(); i++) {
      const vec3& a = piece[i];
      const vec3& b = piece[(i + 1) % piece.size()];
      area2 += la::length(la::cross(a - c, b - c));
      perimeter += la::length(b - a);
    }
    if (perimeter <= 0 || area2 / perimeter < 1e-5) {
      continue;
    }
    std::vector<vec3> pts;
    for (const vec3& p : piece) {
      for (const vec3& b : bitPts) {
        pts.push_back(p + b);
      }
    }
    hulls.push_back(Manifold::Hull(pts));
  }
  return Manifold::BatchBoolean(hulls, manifold::OpType::Add);
}

// A fold in a solid: two of its triangles nearly face to face, within a
// micron of one plane, one's centroid inside the other, both wider than a
// thousandth of a cell (a needle lying on a face is not a fold, only a
// hair). No feature of a chamfered shape is that thin; a boolean leaves
// such pleats now and then where many coplanar faces met.
bool folded(const Manifold& m) {
  const manifold::MeshGL64 g = m.GetMeshGL64();
  const std::size_t np = g.numProp;
  std::vector<vec3> n;
  std::vector<vec3> c;
  std::vector<vec3> p0;
  std::vector<vec3> p1;
  std::vector<vec3> p2;
  std::vector<double> d;
  for (std::size_t t = 0; t + 2 < g.triVerts.size(); t += 3) {
    vec3 q[3];
    for (std::size_t k = 0; k < 3; k++) {
      q[k] = vec3(g.vertProperties[g.triVerts[t + k] * np],
                  g.vertProperties[(g.triVerts[t + k] * np) + 1],
                  g.vertProperties[(g.triVerts[t + k] * np) + 2]);
    }
    const vec3 nn = la::cross(q[1] - q[0], q[2] - q[0]);
    const double l = la::length(nn);
    const double longest = std::max(la::length(q[1] - q[0]),
                                    std::max(la::length(q[2] - q[1]), la::length(q[0] - q[2])));
    if (l < 1e-16 || l / longest < 1e-3) {
      continue;
    }
    n.push_back(nn / l);
    c.push_back((q[0] + q[1] + q[2]) / 3.0);
    d.push_back(la::dot(nn / l, q[0]));
    p0.push_back(q[0]);
    p1.push_back(q[1]);
    p2.push_back(q[2]);
  }
  const auto inside = [&](std::size_t i, const vec3& q) {
    return la::dot(la::cross(p1[i] - p0[i], q - p0[i]), n[i]) >= -1e-12 &&
           la::dot(la::cross(p2[i] - p1[i], q - p1[i]), n[i]) >= -1e-12 &&
           la::dot(la::cross(p0[i] - p2[i], q - p2[i]), n[i]) >= -1e-12;
  };
  // candidates by a coarse grid, so this stays cheap
  const double kCell = 0.1;
  const auto cellOf = [kCell](double x) { return static_cast<long>(std::floor(x / kCell)); };
  std::map<std::tuple<long, long, long>, std::vector<std::size_t>> grid;
  for (std::size_t i = 0; i < c.size(); i++) {
    grid[std::make_tuple(cellOf(c[i].x), cellOf(c[i].y), cellOf(c[i].z))].push_back(i);
  }
  for (std::size_t i = 0; i < c.size(); i++) {
    const long cx = cellOf(c[i].x);
    const long cy = cellOf(c[i].y);
    const long cz = cellOf(c[i].z);
    for (long dx = -1; dx <= 1; dx++) {
      for (long dy = -1; dy <= 1; dy++) {
        for (long dz = -1; dz <= 1; dz++) {
          const auto it = grid.find(std::make_tuple(cx + dx, cy + dy, cz + dz));
          if (it == grid.end()) {
            continue;
          }
          for (const std::size_t j : it->second) {
            if (j <= i || la::dot(n[i], n[j]) > -0.999) {
              continue;
            }
            if (std::fabs(la::dot(n[i], c[j]) - d[i]) > 1e-4) {
              continue;
            }
            if (inside(i, c[j]) || inside(j, c[i])) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

// One step: the solid plus or minus the sweep of its boundary by the bit
// at the given size, the result put back onto its planes; f carries the
// faces of m in and of the result out. A step of size zero does nothing
// (and is skipped outright: gap or bevel zero must not depend on how a
// hull of a flat point set behaves).
Manifold step(const Manifold& m, Faces& f, const std::vector<vec3>& unit, double size, bool add,
              std::string& err) {
  if (size <= 0) {
    return m;
  }
  // A fold in the rebuilt result means the boolean broke a tie badly: the
  // step is redone with the bit turned a little differently, which changes
  // every tie; a few times, then it is an error.
  const int kAttempts = 6;
  Manifold r;
  int attempt = 0;
  for (; attempt < kAttempts; attempt++) {
    const Manifold tube = boundaryTube(f, bitPoints(unit, size, attempt));
    const Manifold u = (add ? m + tube : m - tube).Simplify(1e-5);
    Faces g;
    extractFaces(u, g);
    r = rebuilt(g, u);
    if (!folded(r)) {
      break;
    }
  }
  if (attempt == kAttempts) {
    err = "a Minkowski step left a fold in the surface, even with the bit turned";
    return {};
  }
  // The pieces for the next sweep come from the solid as rebuilt, not from
  // the faces it was rebuilt from: Manifold drops what it cannot hold (a
  // zero-thickness flap the boolean left, say), and a piece of that would
  // sweep a spike into the solid.
  extractFaces(r, f);
  return r;
}

// the construction for one body
bool construct(const Manifold& body, const Bits& b, double g, double r, bool fills, Manifold& out,
               std::string& err) {
  Faces f;
  extractFaces(body, f);
  Manifold m = rebuilt(f, body).Simplify(1e-7);
  extractFaces(m, f);
  // one step of the sequence; false when the step failed (an empty result
  // without a message is a shape that vanished, checked where that can
  // happen)
  const auto go = [&](const std::vector<vec3>& unit, double size, bool add) {
    m = step(m, f, unit, size, add, err);
    return !(m.IsEmpty() && !err.empty());
  };
  if (!b.prism) {
    if (fills) {
      // cube in 2b, octahedron out 2b and in b, cube out b, then the gap
      // with the cuboctahedron
      const double bp = b.kFill * r;
      if (!go(b.offset, 2 * bp, false)) {
        return false;
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the bevel step (twice the bevel too large for "
              "its cells)";
        return false;
      }
      if (!go(b.chamferFill, 2 * bp, true)) {
        return false;
      }
      if (!go(b.chamferFill, bp, false)) {
        return false;
      }
      if (!go(b.offset, bp, true)) {
        return false;
      }
      if (!go(b.chamfer, g, false)) {
        return false;
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the gap step";
        return false;
      }
    } else {
      // cube in b', cuboctahedron out b' - g (in by the difference when the
      // gap is the larger, nothing when they are equal)
      const double bp = b.k * r;
      if (!go(b.offset, bp, false)) {
        return false;
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the bevel step (bevel too large for its cells)";
        return false;
      }
      if (bp > g) {
        if (!go(b.chamfer, bp - g, true)) {
          return false;
        }
      } else if (g > bp) {
        if (!go(b.chamfer, g - bp, false)) {
          return false;
        }
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the gap step";
        return false;
      }
    }
  } else {
    if (fills) {
      // prism in 2b, bipyramid out 2b, bipyramid in b, prism out b - g (in
      // by the difference when the gap is the larger, nothing when they
      // are equal)
      if (!go(b.offset, 2 * r, false)) {
        return false;
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the bevel step (twice the bevel too large for "
              "its cells)";
        return false;
      }
      if (!go(b.chamfer, 2 * r, true)) {
        return false;
      }
      if (!go(b.chamfer, r, false)) {
        return false;
      }
      if (r > g) {
        if (!go(b.offset, r - g, true)) {
          return false;
        }
      } else if (g > r) {
        if (!go(b.offset, g - r, false)) {
          return false;
        }
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the gap step";
        return false;
      }
    } else {
      // prism in b + g, bipyramid out b
      if (!go(b.offset, r + g, false)) {
        return false;
      }
      if (m.IsEmpty()) {
        err = "nothing is left of the shape after the gap step (gap + bevel too large for its "
              "cells)";
        return false;
      }
      if (!go(b.chamfer, r, true)) {
        return false;
      }
    }
  }
  // The result simplified (collinear vertices and features below a
  // millionth of a cell removed) and put back onto its planes once more,
  // so that nothing thinner than single precision resolves is left.
  if (!m.IsEmpty()) {
    m = m.Simplify(1e-6);
    extractFaces(m, f);
    m = rebuilt(f, m);
  }
  if (m.Status() != Manifold::Error::NoError) {
    err = "the Minkowski construction produced an invalid solid";
    return false;
  }
  out = m;
  return true;
}

// One cell of the shape, for tagging the faces: its index and its faces
// as outward planes, in getNeighbor's numbering.
struct CellFaces {
  unsigned index = 0;
  struct CellFace {
    int n = 0;
    vec3 normal{};
    double d = 0;
  };
  std::vector<CellFace> faces;
};

// The manifold's triangles into the polyhedron, one polyhedron vertex per
// manifold vertex (the manifold's mesh is a manifold with shared vertex
// indices; welding by position would merge vertices that only float
// precision tells apart and break that). Facet faces (normal on a facet
// direction) plain, everything else flagged as bevel. Every face is tagged
// with the voxel whose cell holds its centroid (the 3D view removes that
// voxel on a click), and a face lying on one of that cell's faces, g
// inside it, with the number of that cell face (the view adds the
// neighbour across it).
bool addFaces(Polyhedron& poly, const Manifold& m, const std::vector<vec3>& facets,
              const std::vector<CellFaces>& cells, double g, std::string& err) {
  const manifold::MeshGL64 mesh = m.GetMeshGL64();
  const std::size_t np = mesh.numProp;
  const std::size_t nv = mesh.vertProperties.size() / np;
  std::vector<int> id(nv);
  std::vector<Vec3> fp(nv);
  for (std::size_t i = 0; i < nv; i++) {
    fp[i] = Vec3(static_cast<float>(mesh.vertProperties[i * np]),
                 static_cast<float>(mesh.vertProperties[(i * np) + 1]),
                 static_cast<float>(mesh.vertProperties[(i * np) + 2]));
    id[i] = poly.addVertex(fp[i]);
  }
  const auto point = [&](std::size_t i) {
    return vec3(mesh.vertProperties[i * np], mesh.vertProperties[(i * np) + 1],
                mesh.vertProperties[(i * np) + 2]);
  };
  for (std::size_t t = 0; t + 2 < mesh.triVerts.size(); t += 3) {
    const std::size_t i0 = mesh.triVerts[t];
    const std::size_t i1 = mesh.triVerts[t + 1];
    const std::size_t i2 = mesh.triVerts[t + 2];
    const vec3 p0 = point(i0);
    const vec3 p1 = point(i1);
    const vec3 p2 = point(i2);
    vec3 n = la::cross(p1 - p0, p2 - p0);
    const double l = la::length(n);
    // the polyhedron holds single precision, and a face that has no area
    // there has no normal (the STL writer asserts on it)
    const Vec3 fn = (fp[i1] - fp[i0]).cross(fp[i2] - fp[i0]);
    if (l < 1e-14 || fn.squaredModule() == 0) {
      err = "the Minkowski construction left a face without area";
      return false;
    }
    bool facet = false;
    n = n / l;
    for (const vec3& d : facets) {
      if (la::dot(d, n) > 1 - 1e-4) {
        facet = true;
      }
    }
    // the cell holding the centroid: the one it violates least
    const vec3 c = (p0 + p1 + p2) / 3.0;
    std::size_t best = 0;
    double bestViolation = 1e300;
    for (std::size_t k = 0; k < cells.size(); k++) {
      double violation = -1e300;
      for (const CellFaces::CellFace& cf : cells[k].faces) {
        violation = std::max(violation, la::dot(cf.normal, c) - cf.d);
      }
      if (violation < bestViolation) {
        bestViolation = violation;
        best = k;
      }
    }
    int side = -1;
    if (!cells.empty()) {
      for (const CellFaces::CellFace& cf : cells[best].faces) {
        if (la::dot(cf.normal, n) > 1 - 1e-4 &&
            std::fabs(la::dot(cf.normal, c) - (cf.d - g)) < 1e-6) {
          side = cf.n;
        }
      }
    }
    const int fi = poly.addFace({id[i0], id[i1], id[i2]});
    Face& f = poly.face(fi);
    f.voxelFace = side;
    f.voxel = cells.empty() ? 0 : cells[best].index;
    f.flags = facet ? 0 : kFaceBevel;
  }
  return true;
}

}  // namespace

bool minkHandles(const Voxel& v) {
  const GridKind t = v.getGridType().getType();
  return t == GridKind::TriangularPrism || t == GridKind::Rhombic || t == GridKind::TetraOcta;
}

Result<Polyhedron> minkPolyhedron(const Voxel& v, double g, double r, bool fills) {
  if (!minkHandles(v)) {
    return fail(Errc::InvalidArgument, "grid not handled by the Minkowski mesher");
  }
  // the cells, the facet directions, the bits
  std::vector<vec3> facets;
  std::vector<CellFaces> cellFaces;
  for (unsigned z = 0; z < v.getZ(); z++) {
    for (unsigned y = 0; y < v.getY(); y++) {
      for (unsigned x = 0; x < v.getX(); x++) {
        if (!v.validCoordinate(asInt(x), asInt(y), asInt(z)) || !v.isFilled(x, y, z)) {
          continue;
        }
        cellNormals(v, asInt(x), asInt(y), asInt(z), facets);
        // the cell's faces as outward planes
        CellFaces cf;
        cf.index = v.getIndex(x, y, z);
        std::vector<std::pair<int, std::vector<vec3>>> polys;
        vec3 centre(0, 0, 0);
        std::size_t np = 0;
        std::vector<float> corners;
        int nx = 0;
        int ny = 0;
        int nz = 0;
        for (unsigned n = 0; v.getNeighbor(n, 0, asInt(x), asInt(y), asInt(z), &nx, &ny, &nz);
             n++) {
          corners.clear();
          connectionFace(v, asInt(x), asInt(y), asInt(z), asInt(n), 0, 0, corners);
          std::vector<vec3> pts;
          for (std::size_t k = 0; k + 2 < corners.size(); k += 3) {
            pts.push_back(cornerAt(corners, k));
            centre = centre + pts.back();
            np++;
          }
          polys.emplace_back(asInt(n), pts);
        }
        centre = centre / static_cast<double>(np);
        for (const auto& pn : polys) {
          const std::vector<vec3>& pts = pn.second;
          if (pts.size() < 3) {
            continue;
          }
          vec3 nrm(0, 0, 0);
          for (std::size_t k = 0; k < pts.size(); k++) {
            nrm = nrm + la::cross(pts[k], pts[(k + 1) % pts.size()]);
          }
          const double ln = la::length(nrm);
          if (ln < 1e-300) {
            continue;
          }
          nrm = nrm / ln;
          if (la::dot(nrm, pts[0] - centre) < 0) {
            nrm = nrm * -1.0;
          }
          cf.faces.push_back({pn.first, nrm, la::dot(nrm, pts[0])});
        }
        cellFaces.push_back(cf);
      }
    }
  }
  if (cellFaces.empty()) {
    return fail(Errc::InvalidArgument, "the shape has no voxels");
  }
  for (std::size_t i = 0, n = facets.size(); i < n; i++) {
    const vec3 m = facets[i] * -1.0;
    bool seen = false;
    for (const vec3& o : facets) {
      if (la::dot(o, m) > 1 - 1e-6) {
        seen = true;
      }
    }
    if (!seen) {
      facets.push_back(m);
    }
  }
  std::string err;
  Bits b;
  if (v.getGridType().getType() == GridKind::TriangularPrism) {
    if (!prismBits(facets, b, err)) {
      return fail(Errc::InvalidArgument, err);
    }
  } else {
    b = cubeBits();
  }
  const Manifold body = exactBody(v, err);
  if (body.IsEmpty()) {
    return fail(Errc::InvalidArgument, err);
  }
  Manifold outer;
  if (!construct(body, b, g, r, fills, outer, err)) {
    return fail(Errc::InvalidArgument, err);
  }
  Polyhedron poly;
  if (!addFaces(poly, outer, facets, cellFaces, g, err)) {
    return fail(Errc::InvalidArgument, err);
  }
  poly.finalize();
  return poly;
}

}  // namespace bt::mesh
