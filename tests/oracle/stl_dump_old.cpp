// SPDX-License-Identifier: GPL-3.0-or-later
// stl_dump_old <files...>: the original's STL meshes of every shape of
// the puzzle files, with each grid's default parameters: the triangles
// its STL writer writes (stlExporter_c::write, binary: getMesh, then the
// coplanar faces merged), their distinct vertices and the triangles as
// index triples (a summary of the box and the sums for meshes of more than
// kFullTriangles triangles, the sphere grid's), the 3D view's "STL" style
// mesh (getSTLMesh: its faces' attributes and box), and the text STL of
// the first shape of the first file whose mesh is small. Built by
// `oracle.py stl-dump` against the original's library; its output is
// tests/data/stl_dump.txt, which tests/unit/mesh/test_stl_dump.cpp
// compares with the same dump of the new core (positions within 1e-5).
// Keep the two dumps line for line in step.

#include "halfedge/face.h"
#include "halfedge/halfedge.h"
#include "halfedge/polyhedron.h"
#include "halfedge/vertex.h"
#include "lib/gridtype.h"
#include "lib/puzzle.h"
#include "lib/stl.h"
#include "lib/voxel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

constexpr std::size_t kFullTriangles = 5000;
constexpr std::size_t kTextTriangles = 60;

struct Fnv {
  std::uint64_t h = 1469598103934665603ULL;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; i++) {
      h ^= b[i];
      h *= 1099511628211ULL;
    }
  }
  template <class T>
  void add(T v) {
    bytes(&v, sizeof v);
  }
};

std::vector<unsigned char> readFile(const char* name) {
  std::vector<unsigned char> b;
  FILE* f = std::fopen(name, "rb");
  if (f == nullptr) {
    return b;
  }
  unsigned char buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    b.insert(b.end(), buf, buf + n);
  }
  std::fclose(f);
  return b;
}

// The triangles of a binary STL file: their distinct vertices in the
// order of first use and index triples, or for a large mesh its box and
// the sums of its coordinates.
void dumpBinary(const std::vector<unsigned char>& b) {
  std::uint32_t count = 0;
  std::memcpy(&count, b.data() + 80, 4);
  std::map<std::array<float, 3>, std::size_t> index;
  std::vector<std::array<float, 3>> verts;
  std::vector<std::size_t> tris;
  double lo[3] = {1e30, 1e30, 1e30};
  double hi[3] = {-1e30, -1e30, -1e30};
  double sum[3] = {0, 0, 0};
  for (std::uint32_t t = 0; t < count; t++) {
    const unsigned char* p = b.data() + 84 + (50 * t) + 12;
    for (int c = 0; c < 3; c++) {
      std::array<float, 3> v;
      std::memcpy(v.data(), p + (12 * c), 12);
      for (int a = 0; a < 3; a++) {
        lo[a] = std::min(lo[a], static_cast<double>(v[a]));
        hi[a] = std::max(hi[a], static_cast<double>(v[a]));
        sum[a] += v[a];
      }
      auto it = index.find(v);
      if (it == index.end()) {
        it = index.emplace(v, verts.size()).first;
        verts.push_back(v);
      }
      tris.push_back(it->second);
    }
  }
  if (count > kFullTriangles) {
    std::printf("summary %u lo %.4f %.4f %.4f hi %.4f %.4f %.4f sum %.1f %.1f %.1f\n", count, lo[0],
                lo[1], lo[2], hi[0], hi[1], hi[2], sum[0], sum[1], sum[2]);
    return;
  }
  std::printf("triangles %u vertices %zu\n", count, verts.size());
  for (const auto& v : verts) {
    std::printf("v %.6f %.6f %.6f\n", v[0], v[1], v[2]);
  }
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    std::printf("t %zu %zu %zu\n", tris[t], tris[t + 1], tris[t + 2]);
  }
}

// the view's STL mesh: face attributes in order, exactly, and the box
void dumpView(Polyhedron* poly) {
  Fnv h;
  int holes = 0;
  float lo[3] = {1e30F, 1e30F, 1e30F};
  float hi[3] = {-1e30F, -1e30F, -1e30F};
  for (Polyhedron::const_face_iterator it = poly->fBegin(); it != poly->fEnd(); ++it) {
    const Face* f = *it;
    int corners = 0;
    Face::const_edge_circulator e = f->begin();
    Face::const_edge_circulator sentinel = e;
    do {
      const Vector3Df& p = (*e)->dst()->position();
      for (int a = 0; a < 3; a++) {
        lo[a] = std::min(lo[a], p[a]);
        hi[a] = std::max(hi[a], p[a]);
      }
      corners++;
      ++e;
    } while (e != sentinel);
    if (f->hole()) {
      holes++;
    }
    h.add<std::uint8_t>(f->hole() ? 1 : 0);
    h.add<std::uint32_t>(f->_flags);
    h.add<std::uint32_t>(static_cast<std::uint32_t>(f->_fb_index));
    h.add<std::int32_t>(f->_fb_face);
    h.add<std::uint8_t>(static_cast<std::uint8_t>(f->_color));
    h.add<std::int32_t>(corners);
  }
  std::printf("view faces %d holes %d vertices %d attributes %016llx lo %.4f %.4f %.4f hi %.4f %.4f "
              "%.4f\n",
              poly->numFaces(), holes, poly->numVertices(), static_cast<unsigned long long>(h.h),
              lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
}

}  // namespace

int main(int argc, char** argv) {
  // the STL writer writes files: into a folder of its own, named without
  // a path (the binary header holds the name)
  char dir[] = "/tmp/stl_dump_XXXXXX";
  if (mkdtemp(dir) == nullptr || chdir(dir) != 0) {
    return 1;
  }
  bool textDone = false;
  for (int i = 1; i < argc; i++) {
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[i]);
    if (!p) {
      std::printf("file %s unreadable\n", argv[i]);
      continue;
    }
    std::printf("file %s\n", std::filesystem::path(argv[i]).filename().c_str());
    for (unsigned int s = 0; s < p->getNumberOfShapes(); s++) {
      voxel_c* v = p->getShape(s);
      std::unique_ptr<stlExporter_c> ex(p->getGridType()->getStlExporter());
      std::printf("shape %u\n", s);
      try {
        ex->write("shape.stl", *v);
      } catch (stlException_c e) {
        std::printf("error %s\n", e.comment);
        continue;
      }
      const std::vector<unsigned char> b = readFile("shape.stl");
      dumpBinary(b);
      std::uint32_t count = 0;
      std::memcpy(&count, b.data() + 80, 4);
      std::unique_ptr<Polyhedron> view(v->getSTLMesh());
      dumpView(view.get());
      if (!textDone && count <= kTextTriangles) {
        textDone = true;
        ex->setBinaryMode(false);
        ex->write("shape.stl", *v);
        const std::vector<unsigned char> t = readFile("shape.stl");
        std::printf("text %zu\n", t.size());
        std::fwrite(t.data(), 1, t.size(), stdout);
      }
    }
  }
  std::remove("shape.stl");
  chdir("/");
  rmdir(dir);
  return 0;
}
