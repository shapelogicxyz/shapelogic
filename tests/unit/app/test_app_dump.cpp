// SPDX-License-Identifier: GPL-3.0-or-later
// The app dump of tests/oracle/app_dump_old.cpp, made with the new core,
// compared line by line with the original's recording
// (tests/data/app_dump.txt, `oracle.py app-dump`): the meshes of every
// fixture shape (drawing and flat exactly; of the wire frame, which the
// original builds in an order that changes from run to run, the Euler
// characteristic), the status window's table, the fit line of each
// problem, and the playback tracks of the first solutions. Keep the two
// dumps in step.

#include "bt/app/playback.h"
#include "bt/app/queries.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/mesh/voxel_mesh.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace bt;
using namespace bt::app;
using namespace bt::mesh;

constexpr unsigned int kPoseSolutions = 2;
constexpr unsigned int kSamples = 4;

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

__attribute__((format(printf, 1, 2))) std::string format(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}

void dumpMesh(std::ostream& out, const char* style, unsigned int shape, const Polyhedron& poly) {
  if (std::string(style) == "wireframe") {
    out << format("mesh %u %s euler %d holes %d\n", shape, style,
                  poly.numVertices() - poly.numHalfEdges() / 2 + poly.numFaces(),
                  poly.numHoles());
    return;
  }
  Fnv ordered;
  std::vector<std::uint64_t> each;
  for (int i = 0; i < poly.numFaces(); i++) {
    const Face& f = poly.face(poly.faceAt(i));
    std::vector<std::array<float, 3>> pts;
    int e = f.edge;
    const int sentinel = e;
    do {
      const Vec3& p = poly.dstPos(e);
      pts.push_back({p.x, p.y, p.z});
      e = poly.he(e).next;
    } while (e != sentinel);
    Fnv h;
    h.add<std::uint8_t>(f.hole ? 1 : 0);
    h.add<std::uint32_t>(f.flags);
    h.add<std::uint32_t>(f.voxel);
    h.add<std::int32_t>(f.voxelFace);
    h.add<std::uint8_t>(f.colour);
    Fnv c = h;
    for (const auto& p : pts) {
      h.bytes(p.data(), sizeof(float) * 3);
    }
    h.add<std::uint32_t>(static_cast<std::uint32_t>(pts.size()));
    const auto first = static_cast<std::size_t>(std::min_element(pts.begin(), pts.end()) -
                                                pts.begin());
    for (std::size_t k = 0; k < pts.size(); k++) {
      c.bytes(pts[(first + k) % pts.size()].data(), sizeof(float) * 3);
    }
    c.add<std::uint32_t>(static_cast<std::uint32_t>(pts.size()));
    ordered.add<std::uint64_t>(h.h);
    each.push_back(c.h);
  }
  std::sort(each.begin(), each.end());
  Fnv canonical;
  for (const std::uint64_t v : each) {
    canonical.add<std::uint64_t>(v);
  }
  out << format(
      "mesh %u %s faces %d vertices %d halfedges %d holes %d ordered %016llx canonical %016llx\n",
      shape, style, poly.numFaces(), poly.numVertices(), poly.numHalfEdges(), poly.numHoles(),
      static_cast<unsigned long long>(ordered.h), static_cast<unsigned long long>(canonical.h));
}

std::string opt(const std::optional<unsigned int>& v) {
  return v ? std::to_string(*v) : std::string("-");
}

void dumpShapes(std::ostream& out, const Puzzle& p) {
  const std::vector<ShapeInfo> table = shapeTable(p);
  for (unsigned int s = 0; s < p.getNumberOfShapes(); s++) {
    const ShapeInfo& r = table[s];
    out << "shape " << s << " cells " << r.fixed << ' ' << r.variable << " mirror "
        << opt(r.identicalMirror) << " same " << opt(r.identicalShape) << " complete "
        << opt(r.identicalComplete) << " conn " << int(r.connectedFace) << ' '
        << int(r.connectedEdge) << ' ' << int(r.connectedCorner) << " holes " << int(r.holes2D)
        << ' ' << int(r.holes3D);
    if (r.notchable) {
      out << " notch " << int(*r.notchable) << " mill " << int(*r.millable);
    } else {
      out << " notch - mill -";
    }
    if (r.symmetry) {
      out << " sym " << int(*r.symmetry) << '\n';
    } else {
      out << " sym ---\n";
    }
    const Voxel& v = p.getShape(s);
    dumpMesh(out, "drawing", s, shapeMesh(v, MeshStyle::Drawing));
    dumpMesh(out, "wireframe", s, shapeMesh(v, MeshStyle::Wireframe));
    dumpMesh(out, "flat", s, shapeMesh(v, MeshStyle::Flat));
  }
}

void dumpFit(std::ostream& out, const Puzzle& p) {
  for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
    const FitStats f = problemFit(p, prob);
    if (f.verdict == FitVerdict::NoResult) {
      out << "fit " << prob << " -\n";
    } else {
      out << "fit " << prob << ' ' << f.statusLine << '\n';
    }
  }
}

float plusZero(float v) { return v + 0.0F; }

void dumpTrack(std::ostream& out, const Puzzle& p, unsigned int prob, unsigned int sol) {
  Result<PlaybackSampler> s = PlaybackSampler::make(p, prob, sol);
  if (!s) {
    out << "track " << prob << ' ' << sol << " fail\n";
    return;
  }
  TrackOptions o;
  o.samplesPerStep = kSamples;
  const Result<PlaybackTrack> t = s->track(o);
  if (!t) {
    out << "track " << prob << ' ' << sol << " fail\n";
    return;
  }
  out << format("track %u %u pieces %zu keys %zu\n", prob, sol, s->pieces().size(),
                t->times.size());
  for (std::size_t j = 0; j < t->times.size(); j++) {
    out << format("key %zu %.4f", j, static_cast<double>(t->times[j]));
    for (std::size_t i = 0; i < s->pieces().size(); i++) {
      const PiecePose& ps = t->poses[j][i];
      out << format(" %u %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f", s->pieces()[i].piece,
                    ps.visible ? 1 : 0, static_cast<double>(plusZero(ps.translation[0])),
                    static_cast<double>(plusZero(ps.translation[1])),
                    static_cast<double>(plusZero(ps.translation[2])),
                    static_cast<double>(plusZero(ps.rotation[0])),
                    static_cast<double>(plusZero(ps.rotation[1])),
                    static_cast<double>(plusZero(ps.rotation[2])),
                    static_cast<double>(plusZero(ps.rotation[3])));
    }
    out << '\n';
  }
}

void dumpTracks(std::ostream& out, const Puzzle& p) {
  const GridKind gt = p.getGridType().getType();
  if (gt == GridKind::Sliding || gt == GridKind::Stacking) {
    return;
  }
  for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
    const Problem& pr = p.getProblem(prob);
    if (!pr.resultValid()) {
      continue;
    }
    unsigned int done = 0;
    for (unsigned int s = 0; s < pr.getNumberOfSavedSolutions() && done < kPoseSolutions; s++) {
      if (pr.getSavedSolution(s).getDisassembly() == nullptr) {
        continue;
      }
      dumpTrack(out, p, prob, s);
      done++;
    }
  }
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> v;
  std::istringstream in(text);
  for (std::string l; std::getline(in, l);) {
    v.push_back(l);
  }
  return v;
}

}  // namespace

namespace {

// The dump of each fixture file, by file name, made by the new core or
// read from the recording.
std::map<std::string, std::vector<std::string>> sections(const std::string& text) {
  std::map<std::string, std::vector<std::string>> out;
  std::string current;
  for (const std::string& l : lines(text)) {
    if (l.starts_with("file ")) {
      current = l.substr(5);
    }
    out[current].push_back(l);
  }
  return out;
}

// Compares the dumps of the fixtures `keep` accepts.
void compareDumps(bool (*keep)(const std::string&)) {
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator("tests/data/puzzles")) {
    if (e.path().extension() == ".xmpuzzle") {
      files.push_back(e.path());
    }
  }
  std::sort(files.begin(), files.end());
  REQUIRE(files.size() >= 19);

  std::ifstream in("tests/data/app_dump.txt");
  REQUIRE(in.good());
  std::stringstream recorded;
  recorded << in.rdbuf();
  const auto want = sections(recorded.str());

  for (const auto& f : files) {
    const std::string name = f.filename().string();
    if (!keep(name)) {
      continue;
    }
    INFO(name);
    Result<Puzzle> p = io::loadPuzzle(f);
    REQUIRE(p.has_value());
    std::ostringstream mine;
    mine << "file " << name << '\n';
    dumpShapes(mine, *p);
    dumpFit(mine, *p);
    dumpTracks(mine, *p);
    const std::vector<std::string> got = lines(mine.str());
    REQUIRE(want.contains(name));
    const std::vector<std::string>& w = want.at(name);
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < std::min(w.size(), got.size()); i++) {
      if (w[i] != got[i] && mismatches++ < 5) {
        INFO("line " << i + 1 << " of the file's dump");
        CHECK(got[i] == w[i]);
      }
    }
    CHECK(mismatches == 0);
    CHECK(got.size() == w.size());
  }
}

// The quick suite's fixtures: every grid but the rhombic one (its shapes
// are large; test_meshes.cpp has a cell of it), playback with turns. The
// others (some take seconds at -O0: the hole filling of a wire frame is
// quadratic in its faces, as the original's) are [slow].
bool isQuickFixture(const std::string& name) {
  return name == "DemoMirrorParadox.xmpuzzle" || name == "Bermuda.xmpuzzle" ||
         name == "BallRoom.xmpuzzle" || name == "FourPieceTetrahedron.xmpuzzle" ||
         name == "test_rotation_solvers_solved.xmpuzzle";
}

}  // namespace

TEST_CASE("app dump: meshes, status table, fit line and playback match the original",
          "[app][mesh][playback][dump]") {
  compareDumps(isQuickFixture);
}

TEST_CASE("app dump: the other fixtures match the original too",
          "[app][mesh][playback][dump][slow]") {
  compareDumps([](const std::string& name) { return !isQuickFixture(name); });
}
