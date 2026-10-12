// SPDX-License-Identifier: GPL-3.0-or-later
// The STL dump of tests/oracle/stl_dump_old.cpp, made with the new core,
// compared line by line with the original's recording
// (tests/data/stl_dump.txt, `oracle.py stl-dump`): the triangles the STL
// writer writes for every fixture shape with its grid's default
// parameters (positions within 1e-5, the same triangles), the view's STL
// style (its faces' attributes exactly), and one text STL. Every written
// mesh is also checked to be a closed, consistently oriented surface
// (every edge used as often one way as the other), and a manifold one
// (each directed edge once) but for one shape: the Minkowski mesher leaves
// a needle triangle on Bermuda's shape 22 where a side face meets a bevel,
// and the merge of the coplanar faces triangulates the bevel's outline with
// the same needle the other way round, so one edge carries four triangles
// (two of them without area). The original writes the same file
// (docs/handoff/deferred-5.md, "Found in the original").
// Keep the two dumps in step.

#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/mesh/stl.h"
#include "bt/mesh/voxel_mesh.h"
#include "bt/model/puzzle.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace bt;
using namespace bt::mesh;

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

class Out {
 public:
  [[gnu::format(printf, 2, 3)]] void line(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text_ += buf;
  }
  void raw(const std::vector<std::uint8_t>& b) { text_.append(b.begin(), b.end()); }
  [[nodiscard]] const std::string& text() const { return text_; }

 private:
  std::string text_;
};

// what surface a mesh's triangles make (dumpBinary)
enum class Surface : std::uint8_t { Manifold, Closed, Open };

std::uint32_t triangleCount(const std::vector<std::uint8_t>& b) {
  std::uint32_t count = 0;
  std::memcpy(&count, b.data() + 80, 4);
  return count;
}

// The binary STL's triangles as the original's dump lists them; and what
// surface they make: every directed edge used as often as its reverse
// (closed, consistently oriented), and each just once (manifold).
Surface dumpBinary(Out& out, const std::vector<std::uint8_t>& b) {
  const std::uint32_t count = triangleCount(b);
  std::map<std::array<float, 3>, std::size_t> index;
  std::vector<std::array<float, 3>> verts;
  std::vector<std::size_t> tris;
  double lo[3] = {1e30, 1e30, 1e30};
  double hi[3] = {-1e30, -1e30, -1e30};
  double sum[3] = {0, 0, 0};
  for (std::uint32_t t = 0; t < count; t++) {
    const std::uint8_t* p = b.data() + 84 + (50 * static_cast<std::size_t>(t)) + 12;
    for (std::size_t c = 0; c < 3; c++) {
      std::array<float, 3> v{};
      std::memcpy(v.data(), p + (12 * c), 12);
      for (std::size_t a = 0; a < 3; a++) {
        lo[a] = std::min(lo[a], static_cast<double>(v[a]));
        hi[a] = std::max(hi[a], static_cast<double>(v[a]));
        sum[a] += static_cast<double>(v[a]);
      }
      auto it = index.find(v);
      if (it == index.end()) {
        it = index.emplace(v, verts.size()).first;
        verts.push_back(v);
      }
      tris.push_back(it->second);
    }
  }
  std::map<std::pair<std::size_t, std::size_t>, int> edges;
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    for (std::size_t c = 0; c < 3; c++) {
      edges[{tris[t + c], tris[t + ((c + 1) % 3)]}]++;
    }
  }
  Surface surface = Surface::Manifold;
  for (const auto& [e, n] : edges) {
    const auto rev = edges.find({e.second, e.first});
    if (rev == edges.end() || rev->second != n) {
      surface = Surface::Open;
      break;
    }
    if (n != 1) {
      surface = Surface::Closed;
    }
  }
  if (count > kFullTriangles) {
    out.line("summary %u lo %.4f %.4f %.4f hi %.4f %.4f %.4f sum %.1f %.1f %.1f\n", count, lo[0],
             lo[1], lo[2], hi[0], hi[1], hi[2], sum[0], sum[1], sum[2]);
    return surface;
  }
  out.line("triangles %u vertices %zu\n", count, verts.size());
  for (const auto& v : verts) {
    out.line("v %.6f %.6f %.6f\n", static_cast<double>(v[0]), static_cast<double>(v[1]),
             static_cast<double>(v[2]));
  }
  for (std::size_t t = 0; t < tris.size(); t += 3) {
    out.line("t %zu %zu %zu\n", tris[t], tris[t + 1], tris[t + 2]);
  }
  return surface;
}

void dumpView(Out& out, const Polyhedron& poly) {
  Fnv h;
  int holes = 0;
  float lo[3] = {1e30F, 1e30F, 1e30F};
  float hi[3] = {-1e30F, -1e30F, -1e30F};
  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    const Face& face = poly.face(f);
    int corners = 0;
    int e = face.edge;
    const int sentinel = e;
    do {
      const Vec3& p = poly.dstPos(e);
      const float c[3] = {p.x, p.y, p.z};
      for (std::size_t a = 0; a < 3; a++) {
        lo[a] = std::min(lo[a], c[a]);
        hi[a] = std::max(hi[a], c[a]);
      }
      corners++;
      e = poly.he(e).next;
    } while (e != sentinel);
    if (face.hole) {
      holes++;
    }
    h.add<std::uint8_t>(face.hole ? 1 : 0);
    h.add<std::uint32_t>(face.flags);
    h.add<std::uint32_t>(face.voxel);
    h.add<std::int32_t>(face.voxelFace);
    h.add<std::uint8_t>(face.colour);
    h.add<std::int32_t>(corners);
  }
  out.line("view faces %d holes %d vertices %d attributes %016llx lo %.4f %.4f %.4f hi %.4f %.4f "
           "%.4f\n",
           poly.numFaces(), holes, poly.numVertices(), static_cast<unsigned long long>(h.h),
           static_cast<double>(lo[0]), static_cast<double>(lo[1]), static_cast<double>(lo[2]),
           static_cast<double>(hi[0]), static_cast<double>(hi[1]), static_cast<double>(hi[2]));
}

struct Dumped {
  std::string text;
  std::vector<std::string> open;         // shapes whose STL is not a closed surface
  std::vector<std::string> nonManifold;  // shapes with an edge of more than two triangles
};

// the one shape whose STL has an edge of four triangles, as the original's
const std::string kKnownNonManifold = "Bermuda.xmpuzzle shape 22";


// The dump of the fixture files, in order (as stl_dump_old's main).
Dumped dumpFiles(const std::vector<std::string>& names, bool& textDone) {
  Out out;
  Dumped d;
  for (const std::string& name : names) {
    const std::filesystem::path path = std::filesystem::path("tests/data/puzzles") / name;
    Result<Puzzle> p = io::loadPuzzle(path);
    if (!p) {
      out.line("file %s unreadable\n", path.string().c_str());
      continue;
    }
    out.line("file %s\n", name.c_str());
    for (unsigned int s = 0; s < p->getNumberOfShapes(); s++) {
      const Voxel& v = p->getShape(s);
      std::vector<double> values;
      for (const StlParameter& q : stlParameters(v.getGridType().getType())) {
        values.push_back(q.defaultValue);
      }
      out.line("shape %u\n", s);
      Result<Polyhedron> m = stlMesh(v, values);
      if (!m) {
        out.line("error %s\n", m.error().message.c_str());
        continue;
      }
      const std::vector<std::uint8_t> b = stlFile(*m, "shape.stl", true);
      const Surface surface = dumpBinary(out, b);
      if (surface == Surface::Open) {
        d.open.push_back(name + " shape " + std::to_string(s));
      }
      if (surface != Surface::Manifold) {
        d.nonManifold.push_back(name + " shape " + std::to_string(s));
      }
      dumpView(out, shapeMesh(v, MeshStyle::Stl));
      if (!textDone && triangleCount(b) <= kTextTriangles) {
        textDone = true;
        const std::vector<std::uint8_t> t = stlFile(*m, "shape.stl", false);
        out.line("text %zu\n", t.size());
        out.raw(t);
      }
    }
  }
  d.text = out.text();
  return d;
}

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> lines;
  std::istringstream in(s);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

// The recording's lines from "file <first>" up to the next file not in
// `names`.
std::vector<std::string> recorded(const std::vector<std::string>& names) {
  std::ifstream in("tests/data/stl_dump.txt");
  std::stringstream ss;
  ss << in.rdbuf();
  std::vector<std::string> out;
  bool keep = false;
  for (const std::string& line : splitLines(ss.str())) {
    if (line.starts_with("file ")) {
      keep = std::find(names.begin(), names.end(), line.substr(5)) != names.end();
    }
    if (keep) {
      out.push_back(line);
    }
  }
  return out;
}

bool isNumber(const std::string& t, double& value) {
  if (t.find_first_of(".e") == std::string::npos) {
    return false;  // integers and words compare exactly
  }
  char* end = nullptr;
  value = std::strtod(t.c_str(), &end);
  return end != nullptr && *end == '\0' && end != t.c_str();
}

std::vector<std::string> tokens(const std::string& line) {
  std::vector<std::string> t;
  std::istringstream in(line);
  std::string w;
  while (in >> w) {
    t.push_back(w);
  }
  return t;
}

// Two lines alike: words and integers equal, numbers with a fraction
// within 1e-5 (and a millionth of their size, for the sums).
bool sameLine(const std::string& a, const std::string& b) {
  if (a == b) {
    return true;
  }
  const std::vector<std::string> ta = tokens(a);
  const std::vector<std::string> tb = tokens(b);
  if (ta.size() != tb.size()) {
    return false;
  }
  for (std::size_t i = 0; i < ta.size(); i++) {
    double x = 0;
    double y = 0;
    if (isNumber(ta[i], x) && isNumber(tb[i], y)) {
      if (std::fabs(x - y) > 1e-5 + (1e-6 * std::max(std::fabs(x), std::fabs(y)))) {
        return false;
      }
    } else if (ta[i] != tb[i]) {
      return false;
    }
  }
  return true;
}

void compareWithRecording(const std::vector<std::string>& names, bool& textDone) {
  const Dumped mine = dumpFiles(names, textDone);
  for (const std::string& s : mine.open) {
    UNSCOPED_INFO("not closed: " << s);
  }
  CHECK(mine.open.empty());
  for (const std::string& s : mine.nonManifold) {
    if (s != kKnownNonManifold) {
      UNSCOPED_INFO("not a manifold: " << s);
      CHECK(s == kKnownNonManifold);
    }
  }
  const std::vector<std::string> theirs = recorded(names);
  const std::vector<std::string> ours = splitLines(mine.text);
  REQUIRE(!theirs.empty());
  std::size_t differ = 0;
  std::string context;
  for (std::size_t i = 0; i < std::max(theirs.size(), ours.size()); i++) {
    const std::string a = i < theirs.size() ? theirs[i] : "<missing>";
    const std::string b = i < ours.size() ? ours[i] : "<missing>";
    if (a.starts_with("file ") || a.starts_with("shape ")) {
      context = a;
    }
    if (!sameLine(a, b)) {
      if (differ < 10) {
        UNSCOPED_INFO(context << " line " << i << "\n  original: " << a << "\n  new:      " << b);
      }
      differ++;
    }
  }
  CHECK(differ == 0);
}

const std::vector<std::string> kQuickFiles = {
    "AlPackino.xmpuzzle",          "Bermuda.xmpuzzle",
    "CubeInCage.xmpuzzle",         "DemoMirrorParadox.xmpuzzle",
    "DemoPieceGenerator.xmpuzzle", "DraculasDentalDesaster.xmpuzzle",
    "FourPieceTetrahedron.xmpuzzle", "PelikanBurr.xmpuzzle",
    "PiecesOfEight.xmpuzzle",      "Prisgon.xmpuzzle",
    "SolidSixPieceBurrs.xmpuzzle", "test_rotation_solvers.xmpuzzle",
    "test_rotation_solvers_solved.xmpuzzle", "test_sliding_solver.xmpuzzle",
    "test_stacking_hanoi.xmpuzzle"};

// the rhombic shapes of these take the Minkowski mesher long (a large
// shape of 12PieceSeparation about 50 s in the dev build), the sphere
// grid's are large
const std::vector<std::string> kSlowFiles = {"12PieceSeparation.xmpuzzle",
                                             "AugmentedSecondStellation.xmpuzzle",
                                             "BallRoom.xmpuzzle", "BallRoom_solved.xmpuzzle"};

}  // namespace

TEST_CASE("stl dump: the STL meshes of the fixture shapes match the original", "[mesh][stl]") {
  bool textDone = false;
  compareWithRecording(kQuickFiles, textDone);
  CHECK(textDone);
}

TEST_CASE("stl dump: the large fixture shapes match the original too", "[mesh][stl][slow]") {
  bool textDone = true;  // the text STL is in the quick files
  compareWithRecording(kSlowFiles, textDone);
}
