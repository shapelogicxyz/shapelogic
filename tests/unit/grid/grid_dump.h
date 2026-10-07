// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A text dump of what the grids compute: symmetry tables, and for a set of
/// generated shapes on every grid their symmetry, every transformation, the
/// hotspot and bounding-box queries, and each editing operation.
///
/// The same code runs against the original BurrTools (tests/oracle/
/// grid_dump_old.cpp, which recorded tests/data/grid_dump.txt) and against
/// this port (tests/unit/grid/test_grid_dump.cpp, which compares). The two
/// sides differ only in the adapter `A` the including file defines before
/// including this header:
///   A::Voxel, A::make(grid, x, y, z), A::copy(v), A::numTrans(grid),
///   A::numTransMirror(grid), A::numGroups(grid), A::contains(grid, s, t),
///   A::unique(grid, s, t), A::minimize(grid, s, t), A::transAdd(grid, a, b),
///   A::countIntersection(grid, a, b), A::symmetriesLeft(grid, a, b),
///   A::containsMirror(grid, s), A::identical(a, b, colors),
///   A::mirrorTransform(a, b), A::action(v, kind, inside), A::kEmpty,
///   A::kFilled, A::kVariable, and A::Table (a VoxelTable over a shape list).
/// Everything else is called by the member names both versions share.

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace grid_dump {

/// A small deterministic generator, the same on both sides.
struct Rng {
  std::uint64_t s;
  std::uint32_t next() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::uint32_t>(s >> 33);
  }
  unsigned int below(unsigned int n) { return next() % n; }
};

/// FNV-1a, for folding large tables into one line.
struct Hash {
  std::uint64_t h = 1469598103934665603ULL;
  void add(std::uint64_t v) {
    for (int i = 0; i < 8; i++) {
      h ^= (v >> (8 * i)) & 0xFF;
      h *= 1099511628211ULL;
    }
  }
};

inline constexpr int kGrids = 5;
inline constexpr int kShapesPerGrid = 12;

template <class A>
std::uint64_t contentHash(const typename A::Voxel& v) {
  Hash h;
  h.add(v.getX());
  h.add(v.getY());
  h.add(v.getZ());
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    h.add(v.get(i));
  }
  return h.h;
}

/// Size, hotspot, bounding box and content of a space, on one line.
template <class A>
std::string state(const typename A::Voxel& v) {
  std::ostringstream o;
  o << v.getX() << ' ' << v.getY() << ' ' << v.getZ() << " h " << v.getHx() << ' ' << v.getHy()
    << ' ' << v.getHz() << " b " << v.boundX1() << ' ' << v.boundX2() << ' ' << v.boundY1() << ' '
    << v.boundY2() << ' ' << v.boundZ1() << ' ' << v.boundZ2() << " c " << std::hex
    << contentHash<A>(v) << std::dec;
  return o.str();
}

/// The generated shape number `i` of grid `g`. Only valid coordinates are
/// filled; spheres get the hotspot initHotspot() picks (it must be on the
/// lattice), the other grids a random one inside the space.
template <class A>
std::unique_ptr<typename A::Voxel> makeShape(int g, int i) {
  Rng r{static_cast<std::uint64_t>(g * 1000 + i + 1)};
  unsigned int sx = 0;
  unsigned int sy = 0;
  unsigned int sz = 0;
  if (g == 3) {  // rhombic: multiples of 5
    sx = 5 * (1 + r.below(2));
    sy = 5 * (1 + r.below(2));
    sz = 5 * (1 + r.below(2));
  } else if (g == 4) {  // tetra-octa: multiples of 3
    sx = 3 * (1 + r.below(2));
    sy = 3 * (1 + r.below(2));
    sz = 3 * (1 + r.below(2));
  } else {
    sx = 1 + r.below(5);
    sy = 1 + r.below(5);
    sz = 1 + r.below(4);
  }
  auto v = A::make(g, sx, sy, sz);
  const unsigned int density = 2 + r.below(5);  // out of 10
  for (unsigned int z = 0; z < sz; z++) {
    for (unsigned int y = 0; y < sy; y++) {
      for (unsigned int x = 0; x < sx; x++) {
        if (!v->validCoordinate(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z))) {
          continue;
        }
        const unsigned int k = r.below(10);
        if (k < density) {
          v->setState(x, y, z, k == 0 ? A::kVariable : A::kFilled);
          if (r.below(4) == 0) {
            v->setColor(x, y, z, 1 + r.below(5));
          }
        }
      }
    }
  }
  if (g == 2) {
    v->initHotspot();
  } else {
    v->setHotspot(static_cast<int>(r.below(sx)), static_cast<int>(r.below(sy)),
                  static_cast<int>(r.below(sz)));
  }
  return v;
}

template <class A>
void dumpTables(std::ostream& o, int g) {
  const unsigned int nt = A::numTrans(g);
  const unsigned int nm = A::numTransMirror(g);
  const unsigned int ng = A::numGroups(g);
  o << "grid " << g << " transformations " << nt << ' ' << nm << " groups " << ng << '\n';

  Hash perGroup;
  for (unsigned int s = 0; s < ng; s++) {
    perGroup.add(A::containsMirror(g, s));
    for (unsigned int t = 0; t < nm; t++) {
      perGroup.add(A::contains(g, s, t));
      perGroup.add(A::unique(g, s, t));
      perGroup.add(A::minimize(g, s, t));
    }
  }
  Hash add;
  for (unsigned int a = 0; a < nm; a++) {
    for (unsigned int b = 0; b < nm; b++) {
      add.add(A::transAdd(g, a, b));
    }
  }
  Hash pairs;
  for (unsigned int a = 0; a < ng; a++) {
    for (unsigned int b = 0; b < ng; b++) {
      pairs.add(A::countIntersection(g, a, b));
      pairs.add(A::symmetriesLeft(g, a, b));
    }
  }
  o << std::hex << "tables " << perGroup.h << ' ' << add.h << ' ' << pairs.h << std::dec << '\n';
}

/// Neighbours of every cell of a box, for each neighbour kind.
template <class A>
void dumpNeighbours(std::ostream& o, int g) {
  const int box = g == 3 ? 10 : 6;
  auto v = A::make(g, 1, 1, 1);
  for (int typ = 0; typ < 3; typ++) {
    Hash h;
    unsigned int count = 0;
    for (int z = 0; z < box; z++) {
      for (int y = 0; y < box; y++) {
        for (int x = 0; x < box; x++) {
          int nx = 0;
          int ny = 0;
          int nz = 0;
          for (unsigned int idx = 0; v->getNeighbor(idx, static_cast<unsigned int>(typ), x, y, z,
                                                    &nx, &ny, &nz);
               idx++) {
            h.add(static_cast<std::uint64_t>(nx * 10000 + ny * 100 + nz + 50000));
            count++;
          }
          h.add(999999);
        }
      }
    }
    o << "neighbours " << typ << ' ' << count << ' ' << std::hex << h.h << std::dec << '\n';
  }
  Hash valid;
  for (int z = -3; z < box; z++) {
    for (int y = -3; y < box; y++) {
      for (int x = -3; x < box; x++) {
        valid.add(v->validCoordinate(x, y, z));
        valid.add(v->onGrid(x, y, z));
      }
    }
  }
  o << "valid " << std::hex << valid.h << std::dec << '\n';

  // points with an even coordinate sum, so the sphere grid accepts them
  const int pts[][3] = {{0, 0, 0}, {1, 1, 0}, {2, 2, 2}, {3, 1, 2}, {-1, 1, 2}, {4, 0, -2}};
  for (unsigned int t = 0; t < A::numTransMirror(g); t++) {
    o << "point " << t;
    for (const auto& p : pts) {
      int x = p[0];
      int y = p[1];
      int z = p[2];
      v->transformPoint(&x, &y, &z, t);
      o << ' ' << x << ',' << y << ',' << z;
    }
    o << '\n';
  }
}

template <class A>
void dumpShape(std::ostream& o, int g, int i) {
  using V = typename A::Voxel;
  auto base = makeShape<A>(g, i);
  o << "shape " << g << ' ' << i << ' ' << state<A>(*base) << '\n';
  o << "sym " << static_cast<int>(base->selfSymmetries()) << '\n';

  const unsigned int nm = A::numTransMirror(g);
  for (unsigned int t = 0; t < nm; t++) {
    auto v = A::copy(*base);
    const bool ok = v->transform(t);
    o << "t " << t << ' ' << ok;
    if (ok) {
      o << ' ' << state<A>(*v) << " n " << static_cast<int>(base->normalizeTransformation(
                                               static_cast<unsigned char>(t)));
    }
    // bounding box before hotspot: the order in which the original answers
    // "not possible" for both
    int x1 = 0;
    int y1 = 0;
    int z1 = 0;
    int x2 = 0;
    int y2 = 0;
    int z2 = 0;
    const auto tc = static_cast<unsigned char>(t);
    if (base->getBoundingBox(tc, &x1, &y1, &z1, &x2, &y2, &z2)) {
      o << " bb " << x1 << ' ' << y1 << ' ' << z1 << ' ' << x2 << ' ' << y2 << ' ' << z2;
    } else {
      o << " bb -";
    }
    int hx = 0;
    int hy = 0;
    int hz = 0;
    if (base->getHotspot(tc, &hx, &hy, &hz)) {
      o << " hs " << hx << ' ' << hy << ' ' << hz;
    } else {
      o << " hs -";
    }
    o << '\n';
  }

  {
    auto v = A::copy(*base);
    v->minimizePiece();
    o << "minimize " << state<A>(*v) << '\n';
  }
  for (int typ = 0; typ < 3; typ++) {
    o << "connected " << typ << ' ' << base->connected(static_cast<char>(typ), true, A::kEmpty)
      << ' ' << base->connected(static_cast<char>(typ), true, A::kEmpty, false) << ' '
      << base->connected(static_cast<char>(typ), false, A::kEmpty) << '\n';
  }
  for (int typ = 0; typ < 2; typ++) {
    auto v = A::copy(*base);
    v->fillHoles(static_cast<char>(typ));
    o << "fillholes " << typ << ' ' << state<A>(*v) << '\n';
  }
  for (int kind = 0; kind < 3; kind++) {
    for (int inside = 0; inside < 2; inside++) {
      auto v = A::copy(*base);
      A::action(*v, kind, inside != 0);
      o << "action " << kind << ' ' << inside << ' ' << state<A>(*v) << '\n';
    }
  }
  for (unsigned int amount = 2; amount <= 3; amount++) {
    for (int grid = 0; grid < 2; grid++) {
      auto v = A::copy(*base);
      v->scale(amount, grid != 0);
      o << "scale " << amount << ' ' << grid << ' ' << state<A>(*v);
      if (grid == 0) {
        auto d = A::copy(*v);
        const bool down = d->scaleDown(static_cast<unsigned char>(amount), true);
        o << " down " << down << ' ' << state<A>(*d);
      }
      o << '\n';
    }
  }
  o << "scaledown " << A::copy(*base)->scaleDown(2, false) << '\n';
  {
    auto v = A::copy(*base);
    v->translate(1, -1, 2, 0);
    o << "translate " << state<A>(*v) << '\n';
  }
  {
    const int pts[][3] = {{-1, -2, 0}, {static_cast<int>(base->getX()) + 1, 0, 2}, {-3, 1, -6}};
    for (const auto& p : pts) {
      auto v = A::copy(*base);
      int x = p[0];
      int y = p[1];
      int z = p[2];
      v->resizeInclude(x, y, z);
      o << "include " << x << ' ' << y << ' ' << z << ' ' << state<A>(*v) << '\n';
    }
  }
  {
    // the mirror image, and whether the original finds the mirror transformation
    auto m = A::copy(*base);
    const bool ok = m->transform(A::numTrans(g));
    o << "mirror " << ok;
    if (ok) {
      o << ' ' << static_cast<int>(A::mirrorTransform(*base, *m)) << ' '
        << A::identical(*base, *m, true) << ' ' << A::identical(*base, *m, false);
    }
    o << '\n';
  }
  (void)sizeof(V);
}

/// Adds every shape of grid `g` to a table and looks up each shape in a few
/// orientations.
template <class A>
void dumpTable(std::ostream& o, int g) {
  std::vector<std::unique_ptr<typename A::Voxel>> shapes;
  for (int i = 0; i < kShapesPerGrid; i++) {
    shapes.push_back(makeShape<A>(g, i));
  }
  typename A::Table table(shapes);
  for (unsigned int i = 0; i < shapes.size(); i++) {
    table.addSpace(i, 1);      // mirror
    table.addSpace(i, 1 | 2);  // mirror and colour
  }
  for (unsigned int i = 0; i < shapes.size(); i++) {
    for (unsigned int t : {0u, 1u, 5u, A::numTrans(g) + 1}) {
      auto v = A::copy(*shapes[i]);
      if (!v->transform(t)) {
        continue;
      }
      o << "lookup " << i << ' ' << t;
      for (unsigned int params = 0; params < 4; params++) {
        unsigned int index = 9999;
        unsigned char trans = 255;
        const bool found = table.getSpace(*v, &index, &trans, params);
        o << ' ' << found;
        if (found) {
          o << ':' << index << ':' << static_cast<int>(trans);
        }
      }
      o << '\n';
    }
  }
}

/// The whole dump.
template <class A>
std::string dumpAll() {
  std::ostringstream o;
  for (int g = 0; g < kGrids; g++) {
    dumpTables<A>(o, g);
    dumpNeighbours<A>(o, g);
    for (int i = 0; i < kShapesPerGrid; i++) {
      dumpShape<A>(o, g, i);
    }
    dumpTable<A>(o, g);
  }
  return o.str();
}

}  // namespace grid_dump
