// SPDX-License-Identifier: GPL-3.0-or-later
// The app dump of tests/oracle/app_dump_old.cpp, made with the new core,
// compared line by line with the original's recording
// (tests/data/app_dump.txt, `oracle.py app-dump`): the meshes of every
// fixture shape (drawing and flat exactly; of the wire frame, which the
// original builds in an order that changes from run to run, the Euler
// characteristic), the status window's table, the fit line of each
// problem, and the playback tracks of the first solutions. Keep the two
// dumps in step. One deliberate difference (decision 0013, item 7): a
// disassembly read from a file without orientations (<dt>) plays as the
// assembly placed its pieces, where the original turned them to
// orientation 0; the poses of those pieces are left out of the comparison
// and checked against the same tree with the orientations given instead.

#include "app_test_helpers.h"

#include "shapelogic/app/playback.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/mesh/voxel_mesh.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"

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

// The places (in a track's key lines) of the pieces of a solution read
// from a file without orientations that its assembly turned: the original
// played them in orientation 0, this core as the assembly placed them.
// Empty for a tree with orientations.
std::vector<std::size_t> turnedWithoutOrientations(const Puzzle& p, unsigned int prob,
                                                   unsigned int sol) {
  const Separation* tree = p.getProblem(prob).getSavedSolution(sol).getDisassembly();
  if (tree == nullptr || tree->getState(0).hasOrientations()) {
    return {};
  }
  Result<PlaybackSampler> s = PlaybackSampler::make(p, prob, sol);
  if (!s) {
    return {};
  }
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < s->pieces().size(); i++) {
    if (s->pieces()[i].orientation != 0) {
      out.push_back(i);
    }
  }
  return out;
}

// A key line with the poses (translation, rotation) of the pieces at
// `slots` replaced by "*"; the piece numbers and visibility stay.
std::string maskPoses(const std::string& line, const std::vector<std::size_t>& slots) {
  std::vector<std::string> f;
  std::istringstream in(line);
  for (std::string w; in >> w;) {
    f.push_back(w);
  }
  for (const std::size_t slot : slots) {
    for (std::size_t k = 2; k < 9; k++) {
      const std::size_t i = 3 + 9 * slot + k;
      if (i < f.size()) {
        f[i] = "*";
      }
    }
  }
  std::string out;
  for (const std::string& w : f) {
    out += out.empty() ? w : " " + w;
  }
  return out;
}

// Masks, in both dumps, the poses of the pieces the two programs place
// differently on purpose; returns the number of key lines masked.
std::size_t maskTracks(const Puzzle& p, std::vector<std::string>& got,
                       std::vector<std::string>& want) {
  std::vector<std::size_t> slots;
  std::size_t masked = 0;
  for (std::size_t i = 0; i < got.size(); i++) {
    unsigned int prob = 0;
    unsigned int sol = 0;
    if (std::sscanf(got[i].c_str(), "track %u %u", &prob, &sol) == 2) {
      slots = turnedWithoutOrientations(p, prob, sol);
      continue;
    }
    if (!got[i].starts_with("key ")) {
      slots.clear();
      continue;
    }
    if (slots.empty()) {
      continue;
    }
    got[i] = maskPoses(got[i], slots);
    if (i < want.size()) {
      want[i] = maskPoses(want[i], slots);
    }
    masked++;
  }
  return masked;
}

// The tracks the dump holds of solutions read without orientations equal
// those of the same trees with the assembly's orientations given (what the
// solve stored before the file dropped them), the path the other fixtures
// check against the original.
void checkGivenOrientations(const Puzzle& p) {
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
    for (unsigned int sol = 0; sol < pr.getNumberOfSavedSolutions() && done < kPoseSolutions;
         sol++) {
      const Solution& sn = pr.getSavedSolution(sol);
      if (sn.getDisassembly() == nullptr) {
        continue;
      }
      done++;
      if (sn.getDisassembly()->getState(0).hasOrientations()) {
        continue;
      }
      INFO("problem " << prob << " solution " << sol);
      Puzzle given = p;
      given.getProblem(prob).getSavedSolution(sol).setDisassembly(
          test::withOrientations(*sn.getDisassembly(), sn.getAssembly()));
      TrackOptions o;
      o.samplesPerStep = kSamples;
      const PlaybackTrack a = PlaybackSampler::make(p, prob, sol).value().track(o).value();
      const PlaybackTrack b = PlaybackSampler::make(given, prob, sol).value().track(o).value();
      CHECK(a.times == b.times);
      REQUIRE(a.poses.size() == b.poses.size());
      for (std::size_t k = 0; k < a.poses.size(); k++) {
        REQUIRE(a.poses[k].size() == b.poses[k].size());
        for (std::size_t i = 0; i < a.poses[k].size(); i++) {
          CHECK(a.poses[k][i].translation == b.poses[k][i].translation);
          CHECK(a.poses[k][i].rotation == b.poses[k][i].rotation);
          CHECK(a.poses[k][i].visible == b.poses[k][i].visible);
        }
      }
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

// Compares the dumps of the fixtures `keep` accepts; returns the number of
// key lines masked (see maskTracks).
std::size_t compareDumps(bool (*keep)(const std::string&)) {
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

  std::size_t masked = 0;
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
    std::vector<std::string> got = lines(mine.str());
    REQUIRE(want.contains(name));
    std::vector<std::string> w = want.at(name);
    masked += maskTracks(*p, got, w);
    checkGivenOrientations(*p);
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
  return masked;
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
  // these hold the solutions read without orientations (PelikanBurr,
  // CubeInCage, ...): some poses are masked
  CHECK(compareDumps([](const std::string& name) { return !isQuickFixture(name); }) > 0);
}
