// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/cubemesh.cpp (cubeMesh::generate and its helpers)
// and src/lib/cubepoly.cpp (cubePolyhedron); the tables
// src/lib/cubetable.h and cubetable_nofill.h copied unchanged into
// tabs_cube/.
#include "bt/mesh/cube_mesh.h"

#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/mesh/triangulate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace bt::mesh {

namespace {

// The generated tables: for each vertex body (canonical mask) and r/g
// regime, the chamfer surface inside the dual cell about the vertex
// (1,1,1) of the 2x2x2 body, as polygons whose vertex coordinates are
// a + b*g + c*r.
#include "bt/mesh/tabs_cube/cubetable.inc"
#include "bt/mesh/tabs_cube/cubetable_nofill.inc"

// The regime of a case for this ratio: the interval containing it, or at
// a tie ratio (skipped by the table generator) the neighbour whose
// boundary is nearest. The last regime runs to infinity: every regime
// boundary is a finite ratio, so beyond the last one the structure is the
// g = 0 structure, whatever ratio the table generation stopped at.
const cubeRegime_s* regimeOf(const cubeCase_s& c, double ratio) {
  const cubeRegime_s* best = nullptr;
  double bd = 1e100;
  const cubeRegime_s* last = nullptr;
  for (int i = 0; i < c.nRegime; i++) {
    if (last == nullptr || c.regime[i].hi > last->hi) {
      last = &c.regime[i];
    }
  }
  if (last != nullptr && ratio >= last->lo) {
    return last;
  }
  for (int i = 0; i < c.nRegime; i++) {
    const cubeRegime_s& R = c.regime[i];
    if (ratio >= R.lo && ratio <= R.hi) {
      return &R;
    }
    const double d = ratio < R.lo ? R.lo - ratio : ratio - R.hi;
    if (d < bd) {
      bd = d;
      best = &R;
    }
  }
  return bd < 2e-3 ? best : nullptr;
}

// Vertex welding by proximity: a point within kTol of a stored point is
// that point. Buckets of kTol*4; a lookup checks the 27 around it.
struct Weld {
  static constexpr double kTol = 1e-7;
  std::vector<DVec3> pts;
  std::map<long long, std::vector<int>> buckets;

  static long long key(long long a, long long b, long long c) {
    return ((a & 0x1fffffLL) << 42) | ((b & 0x1fffffLL) << 21) | (c & 0x1fffffLL);
  }
  int add(const DVec3& p) {
    const auto ix = static_cast<long long>(std::floor(p.x / (kTol * 4)));
    const auto iy = static_cast<long long>(std::floor(p.y / (kTol * 4)));
    const auto iz = static_cast<long long>(std::floor(p.z / (kTol * 4)));
    for (long long dx = -1; dx <= 1; dx++) {
      for (long long dy = -1; dy <= 1; dy++) {
        for (long long dz = -1; dz <= 1; dz++) {
          const auto it = buckets.find(key(ix + dx, iy + dy, iz + dz));
          if (it == buckets.end()) {
            continue;
          }
          for (const int k : it->second) {
            const DVec3& q = pts[asUnsigned(k)];
            if (std::fabs(q.x - p.x) < kTol && std::fabs(q.y - p.y) < kTol &&
                std::fabs(q.z - p.z) < kTol) {
              return k;
            }
          }
        }
      }
    }
    const int id = static_cast<int>(pts.size());
    pts.push_back(p);
    buckets[key(ix, iy, iz)].push_back(id);
    return id;
  }
};

// The 48 symmetries of the cube about the vertex, the canonical case of
// every vertex mask and the transform that takes the case's body to it,
// computed at first use (the tables hold the geometry, the combinatorics
// is code). A transform maps a point p of the 2x2x2 body about (1,1,1) to
// q[a] = flip[a] ? 2 - p[perm[a]] : p[perm[a]], and a cell (bit 4x+2y+z)
// the same way.
struct Sym {
  struct T {
    int perm[3];
    int flip[3];
  };
  T t[48] = {};
  // the canonical mask, 0 when the mask has no case (not face-connected,
  // or empty)
  int canonOf[256] = {};
  int transformOf[256] = {};

  static int apply(int mask, const T& tr) {
    int out = 0;
    for (int c = 0; c < 8; c++) {
      if ((mask & (1 << c)) == 0) {
        continue;
      }
      const int p[3] = {(c >> 2) & 1, (c >> 1) & 1, c & 1};
      int q[3];
      for (int a = 0; a < 3; a++) {
        q[a] = tr.flip[a] != 0 ? 1 - p[tr.perm[a]] : p[tr.perm[a]];
      }
      out |= 1 << ((4 * q[0]) + (2 * q[1]) + q[2]);
    }
    return out;
  }
  Sym() {
    int n = 0;
    const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& perm : perms) {
      for (int f = 0; f < 8; f++) {
        for (int a = 0; a < 3; a++) {
          t[n].perm[a] = perm[a];
          t[n].flip[a] = (f >> (2 - a)) & 1;
        }
        n++;
      }
    }
    for (int mask = 0; mask < 256; mask++) {
      int best = mask;
      int bt = 0;
      for (const T& k : t) {
        const int m = apply(mask, k);
        if (m < best) {
          best = m;
        }
      }
      canonOf[mask] = 0;
      transformOf[mask] = 0;
      for (int c = 0; c < cubeNumCases; c++) {
        if (cubeCases[c].mask == best) {
          canonOf[mask] = best;
        }
      }
      if (canonOf[mask] == 0) {
        continue;
      }
      for (int k = 0; k < 48; k++) {
        if (apply(best, t[k]) == mask) {
          bt = k;
          break;
        }
      }
      transformOf[mask] = bt;
    }
  }
};

const Sym& sym() {
  static const Sym s;
  return s;
}

// The face-connected pieces of a vertex's cells: cells touching only at
// an edge or a corner are separate bodies 2g apart, and the chamfer
// follows the rules for each on its own.
int pieces(int mask, int out[8]) {
  int n = 0;
  int left = mask;
  while (left != 0) {
    const int seed = left & -left;
    int comp = seed;
    int grow = seed;
    while (grow != 0) {
      int c = 0;
      while (((grow >> c) & 1) == 0) {
        c++;
      }
      grow &= ~(1 << c);
      for (int d = 1; d <= 4; d <<= 1) {
        const int nb = c ^ d;
        if (((left >> nb) & 1) != 0 && ((comp >> nb) & 1) == 0) {
          comp |= 1 << nb;
          grow |= 1 << nb;
        }
      }
    }
    out[n++] = comp;
    left &= ~comp;
  }
  return n;
}

// A polygon as emitted for one dual cell: a loop of welded vertex ids,
// and for a non-planar hole patch its triangulation (index triples into
// the loop); planar polygons are triangulated after consolidation.
struct RawPoly {
  std::vector<int> loop;
  std::vector<int> tris;
};

DVec3 sub(const DVec3& a, const DVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
DVec3 cross(const DVec3& a, const DVec3& b) {
  return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}
double dot(const DVec3& a, const DVec3& b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
double len(const DVec3& a) { return std::sqrt(dot(a, a)); }

const DVec3& at(const std::vector<DVec3>& pts, int i) { return pts[asUnsigned(i)]; }

// Newell normal of a loop (unit length, or zero for a degenerate loop)
DVec3 loopNormal(const std::vector<int>& loop, const std::vector<DVec3>& pts) {
  DVec3 n = {0, 0, 0};
  for (std::size_t q = 0; q < loop.size(); q++) {
    const DVec3& a = at(pts, loop[q]);
    const DVec3& b = at(pts, loop[(q + 1) % loop.size()]);
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  const double ln = len(n);
  if (ln < 1e-14) {
    return {0, 0, 0};
  }
  n.x /= ln;
  n.y /= ln;
  n.z /= ln;
  return n;
}

// Ear clipping of a planar face (outer loop CCW about n, holes CW) into
// index triples of vertex ids.
bool triangulateFace(const std::vector<std::vector<int>>& loops, const DVec3& n,
                     const std::vector<DVec3>& pts, std::vector<int>& tris, std::string& why) {
  DVec3 ax = {1, 0, 0};
  if (std::fabs(n.y) < std::fabs(n.x)) {
    ax = {0, 1, 0};
  }
  if (std::fabs(n.z) < std::fabs(dot(ax, n))) {
    ax = {0, 0, 1};
  }
  const double d = dot(ax, n);
  DVec3 u1 = {ax.x - (d * n.x), ax.y - (d * n.y), ax.z - (d * n.z)};
  const double l1 = len(u1);
  u1.x /= l1;
  u1.y /= l1;
  u1.z /= l1;
  const DVec3 u2 = cross(n, u1);
  std::vector<Point2> p2;
  std::vector<int> flat;
  std::vector<std::vector<int>> idx(loops.size());
  for (std::size_t l = 0; l < loops.size(); l++) {
    for (const int v : loops[l]) {
      const DVec3& a = at(pts, v);
      idx[l].push_back(static_cast<int>(p2.size()));
      p2.push_back({dot(a, u1), dot(a, u2)});
      flat.push_back(v);
    }
  }
  std::vector<int> tt;
  const char* reason = nullptr;
  if (!triangulateLoops(p2, idx, tt, reason)) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "a face of %zu loops, %zu vertices would not triangulate: %s",
                  loops.size(), flat.size(), reason);
    why = buf;
    return false;
  }
  for (const int q : tt) {
    tris.push_back(flat[asUnsigned(q)]);
  }
  return true;
}

// The consolidation pass: the dual cells' polygons merged into whole
// faces. Coplanar polygons sharing an edge (the edge one way in one, the
// other way in the other: the output is watertight, so the cut across a
// dual-cell boundary is the same segment on both sides) are one face; a
// face's outline is the edges of its polygons that are not shared inside
// it, linked into loops (one outer, CCW, plus holes, CW). Then a vertex
// that lies inside a straight edge of EVERY loop it is on (the points
// where the dual-cell boundaries cut the edges) is dropped everywhere: it
// was nobody's corner. Non-planar hole patches keep their loops and
// triangulation; a face whose outline touches itself at a vertex is left
// as its polygons.
bool consolidate(const std::vector<RawPoly>& raw0, CubeSurface& out, std::string& err) {
  const std::vector<DVec3>& pts = out.verts;
  // With r = 0 the bevel strips and corner cuts collapse: welded ids
  // repeat along a loop and whole polygons have no area. Repeats are
  // removed and empty polygons dropped (a patch's triangles with a
  // repeated id likewise); what remains is the gap step's surface.
  std::vector<RawPoly> raw;
  for (const RawPoly& p0 : raw0) {
    RawPoly P = p0;
    std::vector<int> L;
    for (const int v : P.loop) {
      if (L.empty() || L.back() != v) {
        L.push_back(v);
      }
    }
    while (L.size() > 1 && L.front() == L.back()) {
      L.pop_back();
    }
    if (!P.tris.empty()) {
      std::vector<int> T;
      for (std::size_t t = 0; t + 2 < P.tris.size(); t += 3) {
        const int a = P.loop[asUnsigned(P.tris[t])];
        const int b = P.loop[asUnsigned(P.tris[t + 1])];
        const int c = P.loop[asUnsigned(P.tris[t + 2])];
        if (a == b || b == c || a == c) {
          continue;
        }
        T.push_back(a);
        T.push_back(b);
        T.push_back(c);
      }
      if (T.empty()) {
        continue;
      }
      P.tris = T;  // now vertex ids, not loop indices
      P.loop = L;
      raw.push_back(P);
      continue;
    }
    if (L.size() < 3) {
      continue;
    }
    P.loop = L;
    const DVec3 n = loopNormal(P.loop, pts);
    if (len(n) < 0.5) {
      // zero area, or nearly: a collapsed strip (r = 0); anything else is
      // an error
      DVec3 a = {0, 0, 0};
      for (std::size_t q = 0; q < L.size(); q++) {
        const DVec3 c = cross(at(pts, L[q]), at(pts, L[(q + 1) % L.size()]));
        a.x += c.x;
        a.y += c.y;
        a.z += c.z;
      }
      if (len(a) > 1e-12) {
        err = "a degenerate polygon";
        return false;
      }
      continue;
    }
    raw.push_back(P);
  }
  const std::size_t N = raw.size();
  std::vector<DVec3> nrm(N);
  std::vector<char> planar(N, 0);
  for (std::size_t p = 0; p < N; p++) {
    planar[p] = raw[p].tris.empty() ? 1 : 0;
    if (planar[p] != 0) {
      nrm[p] = loopNormal(raw[p].loop, pts);
    }
  }
  // union-find over shared edges between coplanar polygons
  std::vector<int> parent(N);
  for (std::size_t p = 0; p < N; p++) {
    parent[p] = static_cast<int>(p);
  }
  const auto find = [&parent](int i) {
    while (parent[asUnsigned(i)] != i) {
      parent[asUnsigned(i)] = parent[asUnsigned(parent[asUnsigned(i)])];
      i = parent[asUnsigned(i)];
    }
    return i;
  };
  std::map<std::pair<int, int>, int> owner;  // directed edge -> polygon
  for (std::size_t p = 0; p < N; p++) {
    if (planar[p] == 0) {
      continue;
    }
    const std::vector<int>& L = raw[p].loop;
    for (std::size_t q = 0; q < L.size(); q++) {
      const std::pair<int, int> e(L[q], L[(q + 1) % L.size()]);
      if (owner.contains(e)) {
        err = "a directed edge occurs twice";
        return false;
      }
      owner[e] = static_cast<int>(p);
    }
  }
  for (const auto& [edge, a] : owner) {
    const auto rv = owner.find({edge.second, edge.first});
    if (rv == owner.end()) {
      continue;
    }
    const int b = rv->second;
    if (dot(nrm[asUnsigned(a)], nrm[asUnsigned(b)]) < 1 - 1e-9) {
      continue;
    }
    const int ra = find(a);
    const int rb = find(b);
    if (ra != rb) {
      parent[asUnsigned(ra)] = rb;
    }
  }
  std::map<int, std::vector<int>> groups;
  for (std::size_t p = 0; p < N; p++) {
    if (planar[p] != 0) {
      groups[find(static_cast<int>(p))].push_back(static_cast<int>(p));
    }
  }
  // faces: loops (outer first) + the normal; patches go through as they are
  struct FaceLoops {
    std::vector<std::vector<int>> loops;
    DVec3 n{};
    std::vector<int> tris;
  };
  std::vector<FaceLoops> faces;
  for (std::size_t p = 0; p < N; p++) {
    if (planar[p] == 0) {
      FaceLoops f;
      f.loops.push_back(raw[p].loop);
      f.n = {0, 0, 0};
      f.tris = raw[p].tris;
      faces.push_back(f);
    }
  }
  for (const auto& [root, members] : groups) {
    const std::set<int> inGroup(members.begin(), members.end());
    std::map<int, int> next;  // outline: vertex -> next vertex
    bool pinch = false;
    for (std::size_t m = 0; m < members.size() && !pinch; m++) {
      const std::vector<int>& L = raw[asUnsigned(members[m])].loop;
      for (std::size_t q = 0; q < L.size(); q++) {
        const int a = L[q];
        const int b = L[(q + 1) % L.size()];
        const auto rv = owner.find({b, a});
        if (rv != owner.end() && inGroup.contains(rv->second)) {
          continue;  // shared inside the face
        }
        if (next.contains(a)) {
          pinch = true;
          break;
        }
        next[a] = b;
      }
    }
    FaceLoops f;
    f.n = nrm[asUnsigned(members[0])];
    if (pinch) {
      // the outline touches itself: the polygons are kept as they are
      for (const int m : members) {
        FaceLoops s;
        s.n = nrm[asUnsigned(m)];
        s.loops.push_back(raw[asUnsigned(m)].loop);
        faces.push_back(s);
      }
      continue;
    }
    std::set<int> seen;
    for (const auto& [first, nextVertex] : next) {
      if (seen.contains(first)) {
        continue;
      }
      std::vector<int> loop;
      int v = first;
      while (!seen.contains(v)) {
        seen.insert(v);
        loop.push_back(v);
        v = next[v];
      }
      if (v != first) {
        err = "a face outline does not close";
        return false;
      }
      f.loops.push_back(loop);
    }
    // the outer loop first: the one with positive area about the normal
    int outer = -1;
    for (std::size_t l = 0; l < f.loops.size(); l++) {
      DVec3 a = {0, 0, 0};
      const std::vector<int>& L = f.loops[l];
      for (std::size_t q = 0; q < L.size(); q++) {
        const DVec3 c = cross(at(pts, L[q]), at(pts, L[(q + 1) % L.size()]));
        a.x += c.x;
        a.y += c.y;
        a.z += c.z;
      }
      if (dot(a, f.n) > 0) {
        if (outer >= 0) {
          err = "a face with two outer loops";
          return false;
        }
        outer = static_cast<int>(l);
      }
    }
    if (outer < 0) {
      err = "a face without an outer loop";
      return false;
    }
    std::swap(f.loops[0], f.loops[asUnsigned(outer)]);
    faces.push_back(f);
  }
  // corners: a vertex is kept where it is a corner of some loop; patch
  // vertices always are
  std::vector<char> corner(pts.size(), 0);
  for (const FaceLoops& face : faces) {
    for (const std::vector<int>& L : face.loops) {
      for (std::size_t q = 0; q < L.size(); q++) {
        const int v = L[q];
        if (!face.tris.empty()) {
          corner[asUnsigned(v)] = 1;
          continue;
        }
        const DVec3& a = at(pts, L[(q + L.size() - 1) % L.size()]);
        const DVec3& b = at(pts, v);
        const DVec3& c = at(pts, L[(q + 1) % L.size()]);
        const DVec3 d = sub(c, a);
        const double ld = len(d);
        if (ld < 1e-12) {
          corner[asUnsigned(v)] = 1;
          continue;
        }
        const double off = len(cross(sub(b, a), d)) / ld;  // distance of b from the line a-c
        const double t = dot(sub(b, a), d);
        if (off > 1e-6 || t <= 0 || t >= ld * ld) {
          corner[asUnsigned(v)] = 1;
        }
      }
    }
  }
  out.tris.clear();
  for (FaceLoops& f : faces) {
    for (std::vector<int>& loop : f.loops) {
      std::vector<int> kept;
      for (const int v : loop) {
        if (corner[asUnsigned(v)] != 0) {
          kept.push_back(v);
        }
      }
      if (kept.size() < 3) {
        err = "a face loop lost its corners";
        return false;
      }
      loop = kept;
    }
    if (!f.tris.empty()) {
      out.tris.insert(out.tris.end(), f.tris.begin(), f.tris.end());
      continue;
    }
    std::string why;
    if (!triangulateFace(f.loops, f.n, pts, out.tris, why)) {
      err = why;
      return false;
    }
  }
  return true;
}

// g = r = 0: every exposed cell face as one quad, welded; no table
CubeSurface plainSurface(int nx, int ny, int nz, const std::vector<char>& filled) {
  CubeSurface out;
  Weld weld;
  const int d[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  const auto isFilled = [&](int x, int y, int z) {
    return filled[asUnsigned(x + (nx * (y + (ny * z))))] != 0;
  };
  for (int z = 0; z < nz; z++) {
    for (int y = 0; y < ny; y++) {
      for (int x = 0; x < nx; x++) {
        if (!isFilled(x, y, z)) {
          continue;
        }
        for (int f = 0; f < 6; f++) {
          const int xn = x + d[f][0];
          const int yn = y + d[f][1];
          const int zn = z + d[f][2];
          if (xn >= 0 && yn >= 0 && zn >= 0 && xn < nx && yn < ny && zn < nz &&
              isFilled(xn, yn, zn)) {
            continue;
          }
          // the face's corners, counter-clockwise seen from outside
          const int a = f / 2;  // axis
          const int s = f % 2;  // side (0 = low, 1 = high)
          const int u = (a + 1) % 3;
          const int v = (a + 2) % 3;
          std::vector<int> loop;
          for (int k = 0; k < 4; k++) {
            int cu = (k == 1 || k == 2) ? 1 : 0;
            int cv = (k >= 2) ? 1 : 0;
            if (s == 0) {
              std::swap(cu, cv);
            }
            double p[3] = {static_cast<double>(x), static_cast<double>(y),
                           static_cast<double>(z)};
            p[a] += s;
            p[u] += cu;
            p[v] += cv;
            loop.push_back(weld.add({p[0], p[1], p[2]}));
          }
          out.tris.push_back(loop[0]);
          out.tris.push_back(loop[1]);
          out.tris.push_back(loop[2]);
          out.tris.push_back(loop[0]);
          out.tris.push_back(loop[2]);
          out.tris.push_back(loop[3]);
        }
      }
    }
  }
  out.verts = weld.pts;
  return out;
}

}  // namespace

Result<CubeSurface> cubeSurface(int nx, int ny, int nz, const std::vector<char>& filled, double g,
                                double r, bool fills) {
  // the table for the variant: with interior (concave) chamfers, or without
  const cubeCase_s* cases = fills ? cubeCases : cubeNFCases;
  const int nCases = fills ? cubeNumCases : cubeNFNumCases;
  if (static_cast<long long>(filled.size()) != static_cast<long long>(nx) * ny * nz) {
    return fail(Errc::InvalidArgument, "filled has the wrong size");
  }
  if (!(g >= 0) || !(r >= 0)) {
    return fail(Errc::InvalidArgument, "need g >= 0 and r >= 0");
  }
  if (g + r > 0.5) {
    return fail(Errc::InvalidArgument, "gap + bevel must stay below half a cell");
  }
  // The special cases: r = 0 is the gap step alone and goes through the
  // tables at ratio 0, where every regime starts and the collapsed strips
  // are dropped in consolidate(); g = 0 is the r/g -> infinity limit: the
  // last regime of each case evaluated at g = 0 (its formulas are affine,
  // and every regime boundary is a finite ratio, all below 5, so the
  // structure beyond the last boundary is the g = 0 structure); g = r = 0
  // is the plain cell surface and is built without the tables.
  if (g == 0 && r == 0) {
    return plainSurface(nx, ny, nz, filled);
  }
  const double ratio = g > 0 ? r / g : 1e300;
  // a case's regime is looked up when the shape first needs the case; a
  // shape without a given vertex body is not held to that body's table
  std::vector<const cubeRegime_s*> regime(asUnsigned(nCases), nullptr);
  std::vector<char> resolved(asUnsigned(nCases), 0);
  Weld weld;
  std::vector<int> ids;
  std::vector<RawPoly> raw;  // every dual cell's polygons, merged afterwards
  for (int k = 0; k <= nz; k++) {
    for (int j = 0; j <= ny; j++) {
      for (int i = 0; i <= nx; i++) {
        // the 8 cells about vertex (i,j,k): bit 4cx + 2cy + cz for the
        // cell at (i-1+cx, j-1+cy, k-1+cz)
        int mask = 0;
        for (int cx = 0; cx < 2; cx++) {
          for (int cy = 0; cy < 2; cy++) {
            for (int cz = 0; cz < 2; cz++) {
              const int x = i - 1 + cx;
              const int y = j - 1 + cy;
              const int z = k - 1 + cz;
              if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) {
                continue;
              }
              if (filled[asUnsigned(x + (nx * (y + (ny * z))))] != 0) {
                mask |= 1 << ((4 * cx) + (2 * cy) + cz);
              }
            }
          }
        }
        if (mask == 255) {
          continue;  // an interior vertex: no surface here
        }
        int comps[8];
        const int nc = pieces(mask, comps);
        for (int pc = 0; pc < nc; pc++) {
          const int piece = comps[pc];
          int ci = -1;
          for (int c = 0; c < nCases; c++) {
            if (cases[c].mask == sym().canonOf[piece]) {
              ci = c;
            }
          }
          if (ci < 0) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                          "no table for the vertex body at (%d,%d,%d) (mask %d, piece %d)", i, j, k,
                          mask, piece);
            return fail(Errc::InvalidArgument, buf);
          }
          const std::size_t uci = asUnsigned(ci);
          if (resolved[uci] == 0) {
            resolved[uci] = 1;
            regime[uci] = regimeOf(cases[ci], ratio);
          }
          if (regime[uci] == nullptr) {
            char buf[300];
            const cubeCase_s& C = cases[ci];
            int n = 0;
            n += std::snprintf(buf + n, sizeof(buf) - asUnsigned(n),
                               "r/g = %.4f is outside the table for the vertex body at (%d,%d,%d) "
                               "(mask %d, piece %d, case mask %d; tabulated:",
                               ratio, i, j, k, mask, piece, C.mask);
            for (int q = 0; q < C.nRegime && n < static_cast<int>(sizeof(buf)) - 30; q++) {
              n += std::snprintf(buf + n, sizeof(buf) - asUnsigned(n), " %.3f..%.3f",
                                 C.regime[q].lo, C.regime[q].hi);
            }
            std::snprintf(buf + n, sizeof(buf) - asUnsigned(n), "%s",
                          C.nRegime != 0 ? ")" : " nothing)");
            return fail(Errc::InvalidArgument, buf);
          }
          const cubeRegime_s& R = *regime[uci];
          const Sym::T& T = sym().t[sym().transformOf[piece]];
          // orientation of the transform: a permutation's parity times
          // the flips'
          int par = 1;
          for (int a = 0; a < 3; a++) {
            for (int b = a + 1; b < 3; b++) {
              if (T.perm[a] > T.perm[b]) {
                par = -par;
              }
            }
          }
          for (const int flip : T.flip) {
            if (flip != 0) {
              par = -par;
            }
          }
          // the regime's vertices, placed
          ids.resize(asUnsigned(R.nVert));
          for (int v = 0; v < R.nVert; v++) {
            const double* c = R.vert + (9 * v);
            const double p[3] = {c[0] + (c[1] * g) + (c[2] * r), c[3] + (c[4] * g) + (c[5] * r),
                                 c[6] + (c[7] * g) + (c[8] * r)};
            double q[3];
            for (int a = 0; a < 3; a++) {
              q[a] = T.flip[a] != 0 ? 2.0 - p[T.perm[a]] : p[T.perm[a]];
            }
            ids[asUnsigned(v)] = weld.add({q[0] + (i - 1), q[1] + (j - 1), q[2] + (k - 1)});
          }
          for (int pi = 0; pi < R.nPoly; pi++) {
            const int s = R.polyStart[pi];
            const int e = R.polyStart[pi + 1];
            RawPoly P;
            for (int q = s; q < e; q++) {
              P.loop.push_back(ids[asUnsigned(R.polyVert[q])]);
            }
            if (par < 0) {
              std::reverse(P.loop.begin(), P.loop.end());
            }
            const int ts = R.triStart[pi];
            const int te = R.triStart[pi + 1];
            if (te > ts) {
              // A non-planar hole patch: its own triangulation, as index
              // triples into the loop (the loop was reversed under a
              // reflection: index n-1-a, and the triangle's winding too).
              const int n = e - s;
              for (int t = ts; t + 2 < te; t += 3) {
                int a = R.triVert[t];
                int b = R.triVert[t + 1];
                int c2 = R.triVert[t + 2];
                if (par < 0) {
                  a = n - 1 - a;
                  b = n - 1 - b;
                  c2 = n - 1 - c2;
                  std::swap(b, c2);
                }
                P.tris.push_back(a);
                P.tris.push_back(b);
                P.tris.push_back(c2);
              }
            }
            raw.push_back(P);
          }
        }
      }
    }
  }
  CubeSurface out;
  out.verts = weld.pts;
  std::string err;
  if (!consolidate(raw, out, err)) {
    return fail(Errc::InvalidArgument, err);
  }
  return out;
}

Result<Polyhedron> cubePolyhedron(const Voxel& v, double g, double r, bool fills) {
  const int nx = asInt(v.getX());
  const int ny = asInt(v.getY());
  const int nz = asInt(v.getZ());
  std::vector<char> filled(asUnsigned(nx * ny * nz), 0);
  for (int z = 0; z < nz; z++) {
    for (int y = 0; y < ny; y++) {
      for (int x = 0; x < nx; x++) {
        filled[asUnsigned(x + (nx * (y + (ny * z))))] =
            v.isFilled(asUnsigned(x), asUnsigned(y), asUnsigned(z)) ? 1 : 0;
      }
    }
  }
  Result<CubeSurface> mesh = cubeSurface(nx, ny, nz, filled, g, r, fills);
  if (!mesh) {
    return std::unexpected(mesh.error());
  }
  const CubeSurface& m = *mesh;
  Polyhedron poly;
  {
    VertexList vl(poly);
    for (std::size_t t = 0; t + 2 < m.tris.size(); t += 3) {
      const DVec3& a = at(m.verts, m.tris[t]);
      const DVec3& b = at(m.verts, m.tris[t + 1]);
      const DVec3& c = at(m.verts, m.tris[t + 2]);
      const int ia = vl.get(static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z));
      const int ib = vl.get(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z));
      const int ic = vl.get(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z));
      const int fi = poly.addFace({ia, ib, ic});
      const double n[3] = {((b.y - a.y) * (c.z - a.z)) - ((b.z - a.z) * (c.y - a.y)),
                           ((b.z - a.z) * (c.x - a.x)) - ((b.x - a.x) * (c.z - a.z)),
                           ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x))};
      const double ln = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
      int side = -1;
      for (int ax = 0; ax < 3 && ln > 0; ax++) {
        if (n[ax] > (1 - 1e-6) * ln) {
          side = 3 + ax;
        }
        if (n[ax] < -(1 - 1e-6) * ln) {
          side = ax;
        }
      }
      Face& f = poly.face(fi);
      f.voxelFace = -1;
      f.voxel = 0;
      f.flags = 0;
      // The voxel this face belongs to (the 3D view removes it on a
      // click): a side's face plane is cell + 1 - g (positive side) or
      // cell + g (negative), the other two coordinates from the centroid;
      // a bevel or fill belongs to the filled cell its centroid is in or,
      // for a fill (which bulges into the empty cell at a concave edge),
      // the filled cell it sticks out of least.
      const double cen[3] = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
      int cell[3];
      for (int q = 0; q < 3; q++) {
        cell[q] = static_cast<int>(std::floor(cen[q]));
      }
      if (side >= 0) {
        const int ax = side % 3;
        cell[ax] = side >= 3 ? static_cast<int>(std::floor(cen[ax] + g + 0.5)) - 1
                             : static_cast<int>(std::floor(cen[ax] - g + 0.5));
      } else {
        f.flags |= kFaceBevel;
        double best = 1e300;
        int bestCell[3] = {-1, -1, -1};
        for (int dx = -1; dx <= 1; dx++) {
          for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
              const int cx = cell[0] + dx;
              const int cy = cell[1] + dy;
              const int cz = cell[2] + dz;
              if (cx < 0 || cy < 0 || cz < 0 || cx >= nx || cy >= ny || cz >= nz) {
                continue;
              }
              if (!v.isFilled(asUnsigned(cx), asUnsigned(cy), asUnsigned(cz))) {
                continue;
              }
              double outside = 0;
              outside = std::max(outside, std::max(cx - cen[0], cen[0] - (cx + 1)));
              outside = std::max(outside, std::max(cy - cen[1], cen[1] - (cy + 1)));
              outside = std::max(outside, std::max(cz - cen[2], cen[2] - (cz + 1)));
              if (outside < best) {
                best = outside;
                bestCell[0] = cx;
                bestCell[1] = cy;
                bestCell[2] = cz;
              }
            }
          }
        }
        for (int q = 0; q < 3; q++) {
          cell[q] = bestCell[q];
        }
      }
      if (cell[0] < 0 || cell[1] < 0 || cell[2] < 0 || cell[0] >= nx || cell[1] >= ny ||
          cell[2] >= nz) {
        continue;
      }
      f.voxel = v.getIndex(asUnsigned(cell[0]), asUnsigned(cell[1]), asUnsigned(cell[2]));
      if (side < 0) {
        continue;
      }
      f.voxelFace = side;
      if (((cell[0] + cell[1] + cell[2]) & 1) != 0) {
        f.flags |= kFaceColorLight;
      }
    }
  }
  poly.finalize();
  return poly;
}

}  // namespace bt::mesh
