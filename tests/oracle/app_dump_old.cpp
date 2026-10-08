// SPDX-License-Identifier: GPL-3.0-or-later
// app_dump_old <files...>: what the original computes for the app layer
// from puzzle files: the meshes of every shape (drawing, wire frame, flat:
// counts and hashes of their faces), the status window's table, the fit
// line of each problem, and the playback track of the first solutions
// (gltfexport.cpp's sampler). Built by `oracle.py app-dump` against the
// original's library; its output is tests/data/app_dump.txt, which
// tests/unit/app/test_app_dump.cpp compares with the same dump of the new
// core (tests/unit/app/app_dump.h). Keep the two dumps line for line in
// step.

#include "halfedge/face.h"
#include "halfedge/halfedge.h"
#include "halfedge/polyhedron.h"
#include "halfedge/vertex.h"
#include "lib/assembly.h"
#include "lib/disasmtomoves.h"
#include "lib/disassembly.h"
#include "lib/gridtype.h"
#include "lib/millable.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solution.h"
#include "lib/symmetries.h"
#include "lib/voxel.h"
#include "lib/voxeltable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kPoseSolutions = 2;  // solutions with a track, per problem
constexpr unsigned int kSamples = 4;        // samples per step

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

// Faces in mesh order (ordered), and as a set of faces each read from its
// smallest corner (canonical). The original's hole filling (the wire
// frame) visits faces and vertices in the order of their addresses, so
// which faces it merges (and where it adds a vertex to cap a hole)
// changes from run to run: of a wire frame only the Euler characteristic
// (vertices - edges + faces) and the holes are recorded.
void dumpMesh(const char* style, unsigned int shape, Polyhedron* poly) {
  Fnv ordered;
  std::vector<std::uint64_t> each;
  for (Polyhedron::const_face_iterator it = poly->fBegin(); it != poly->fEnd(); ++it) {
    const Face* f = *it;
    std::vector<std::array<float, 3>> pts;
    Face::const_edge_circulator e = f->begin();
    Face::const_edge_circulator sentinel = e;
    do {
      const Vector3Df& p = (*e)->dst()->position();
      pts.push_back({p.x(), p.y(), p.z()});
      ++e;
    } while (e != sentinel);
    Fnv h;
    h.add<std::uint8_t>(f->hole() ? 1 : 0);
    h.add<std::uint32_t>(f->_flags);
    h.add<std::uint32_t>(f->_fb_index);
    h.add<std::int32_t>(f->_fb_face);
    h.add<std::uint8_t>(f->_color);
    Fnv c = h;
    for (const auto& p : pts) h.bytes(p.data(), sizeof(float) * 3);
    h.add<std::uint32_t>((std::uint32_t)pts.size());
    const std::size_t first = std::min_element(pts.begin(), pts.end()) - pts.begin();
    for (std::size_t i = 0; i < pts.size(); i++) c.bytes(pts[(first + i) % pts.size()].data(), sizeof(float) * 3);
    c.add<std::uint32_t>((std::uint32_t)pts.size());
    ordered.add<std::uint64_t>(h.h);
    each.push_back(c.h);
  }
  std::sort(each.begin(), each.end());
  Fnv canonical;
  for (std::uint64_t v : each) canonical.add<std::uint64_t>(v);
  if (std::strcmp(style, "wireframe") == 0) {
    // the wire frame's faces vary from run to run in the original; its
    // vertices and topology do not
    std::printf("mesh %u %s euler %d holes %d\n", shape, style,
                poly->numVertices() - poly->numHalfEdges() / 2 + poly->numFaces(), poly->numHoles());
    delete poly;
    return;
  }
  std::printf("mesh %u %s faces %d vertices %d halfedges %d holes %d ordered %016llx canonical %016llx\n",
              shape, style, poly->numFaces(), poly->numVertices(), poly->numHalfEdges(),
              poly->numHoles(), (unsigned long long)ordered.h, (unsigned long long)canonical.h);
  delete poly;
}

void dumpShapes(const puzzle_c& p) {
  voxelTablePuzzle_c tab(&p);
  const bool bricks = p.getGridType()->getType() == gridType_c::GT_BRICKS;
  for (unsigned int s = 0; s < p.getNumberOfShapes(); s++) {
    const voxel_c* v = p.getShape(s);
    std::printf("shape %u cells %u %u", s, v->countState(voxel_c::VX_FILLED),
                v->countState(voxel_c::VX_VARIABLE));
    unsigned int idx;
    unsigned char trans;
    if (tab.getSpace(v, &idx, &trans, voxelTable_c::PAR_MIRROR)) std::printf(" mirror %u", idx);
    else std::printf(" mirror -");
    if (tab.getSpace(v, &idx, &trans, 0)) std::printf(" same %u", idx);
    else std::printf(" same -");
    if (tab.getSpace(v, &idx, &trans, voxelTable_c::PAR_COLOUR) &&
        trans < p.getGridType()->getSymmetries()->getNumTransformations())
      std::printf(" complete %u", idx);
    else std::printf(" complete -");
    std::printf(" conn %d %d %d holes %d %d", v->connected(0, true, 0) ? 1 : 0,
                v->connected(1, true, 0) ? 1 : 0, v->connected(2, true, 0) ? 1 : 0,
                !v->connected(0, false, 0, false) ? 1 : 0, !v->connected(0, false, 0) ? 1 : 0);
    if (bricks) std::printf(" notch %d mill %d", isNotchable(v) ? 1 : 0, isMillable(v) ? 1 : 0);
    else std::printf(" notch - mill -");
    if (p.getGridType()->getSymmetries()->symmetryKnown(v))
      std::printf(" sym %d\n", (int)p.getGridType()->getSymmetries()->calculateSymmetry(v));
    else std::printf(" sym ---\n");
    tab.addSpace(s, voxelTable_c::PAR_MIRROR);
    tab.addSpace(s, voxelTable_c::PAR_MIRROR | voxelTable_c::PAR_COLOUR);

    dumpMesh("drawing", s, v->getDrawingMesh());
    dumpMesh("wireframe", s, v->getWireframeMesh());
    dumpMesh("flat", s, v->getFlatMesh());
  }
}

// mainWindow_c::StatProblemInfo
void dumpFit(const puzzle_c& p) {
  for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
    const problem_c* pr = p.getProblem(prob);
    if (!pr->resultValid()) {
      std::printf("fit %u -\n", prob);
      continue;
    }
    char txt[100];
    unsigned int cnt = 0;
    unsigned int cntMin = 0;
    for (unsigned int i = 0; i < pr->getNumberOfParts(); i++) {
      cnt += pr->getPartShape(i)->countState(voxel_c::VX_FILLED) * pr->getPartMaximum(i);
      cntMin += pr->getPartShape(i)->countState(voxel_c::VX_FILLED) * pr->getPartMinimum(i);
    }
    if (cnt == cntMin) {
      snprintf(txt, 100, "Problem P%u result can contain %u - %u voxels, pieces (n = %u) contain %u voxels", prob+1,
          getResultShape(*pr)->countState(voxel_c::VX_FILLED),
          getResultShape(*pr)->countState(voxel_c::VX_FILLED) +
          getResultShape(*pr)->countState(voxel_c::VX_VARIABLE),
          pr->getNumberOfPieces(), cnt);
    } else {
      snprintf(txt, 100, "Problem P%u result can contain %u - %u voxels, pieces (n = %u) contain %u-%u voxels", prob+1,
          getResultShape(*pr)->countState(voxel_c::VX_FILLED),
          getResultShape(*pr)->countState(voxel_c::VX_FILLED) +
          getResultShape(*pr)->countState(voxel_c::VX_VARIABLE),
          pr->getNumberOfPieces(), cntMin, cnt);
    }
    std::printf("fit %u %s\n", prob, txt);
  }
}

// ---- the sampler of gltfexport.cpp (copied: it is in an anonymous namespace there) ----

struct vec3 { double x, y, z; };

struct mat3 {
  double m[9];
  vec3 operator*(const vec3 & v) const {
    return { m[0]*v.x + m[1]*v.y + m[2]*v.z,
             m[3]*v.x + m[4]*v.y + m[5]*v.z,
             m[6]*v.x + m[7]*v.y + m[8]*v.z };
  }
  mat3 operator*(const mat3 & o) const {
    mat3 r;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        r.m[3*i+j] = m[3*i]*o.m[j] + m[3*i+1]*o.m[3+j] + m[3*i+2]*o.m[6+j];
    return r;
  }
  mat3 transposed(void) const {
    return {{ m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8] }};
  }
  double det(void) const {
    return m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
  }
  static mat3 identity(void) { return {{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }}; }
};

vec3 operator+(const vec3 & a, const vec3 & b) { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
vec3 operator-(const vec3 & a, const vec3 & b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }

mat3 axisAngle(double deg, double ax, double ay, double az) {
  double r = deg * M_PI / 180.0;
  double c = cos(r), s = sin(r), t = 1 - c;
  return {{ t*ax*ax + c,    t*ax*ay - s*az, t*ax*az + s*ay,
            t*ax*ay + s*az, t*ay*ay + c,    t*ay*az - s*ax,
            t*ax*az - s*ay, t*ay*az + s*ax, t*az*az + c }};
}

void toQuat(const mat3 & R, float q[4]) {
  const double * m = R.m;
  double tr = m[0] + m[4] + m[8];
  double x, y, z, w;
  if (tr > 0) {
    double s = sqrt(tr + 1.0) * 2;
    w = 0.25 * s; x = (m[7] - m[5]) / s; y = (m[2] - m[6]) / s; z = (m[3] - m[1]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    double s = sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
    w = (m[7] - m[5]) / s; x = 0.25 * s; y = (m[1] + m[3]) / s; z = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    double s = sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
    w = (m[2] - m[6]) / s; x = (m[1] + m[3]) / s; y = 0.25 * s; z = (m[5] + m[7]) / s;
  } else {
    double s = sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
    w = (m[3] - m[1]) / s; x = (m[2] + m[6]) / s; y = (m[5] + m[7]) / s; z = 0.25 * s;
  }
  double n = sqrt(x*x + y*y + z*z + w*w);
  q[0] = float(x / n); q[1] = float(y / n); q[2] = float(z / n); q[3] = float(w / n);
}

// the bounding box centre of the triangles of the drawing mesh (buildMesh)
bool meshCentreOf(const voxel_c & v, vec3 & c) {
  std::unique_ptr<Polyhedron> poly(v.getDrawingMesh());
  bool first = true;
  vec3 lo { 0, 0, 0 }, hi { 0, 0, 0 };
  for (Polyhedron::const_face_iterator it = poly->fBegin(); it != poly->fEnd(); ++it) {
    const Face * f = *it;
    if (f->hole() || (f->_flags & FF_INSIDE_FACE)) continue;
    Vector3Df n = f->normal();
    if (std::isnan(n.x()) || std::isnan(n.y()) || std::isnan(n.z())) continue;
    Face::const_edge_circulator e = f->begin();
    Face::const_edge_circulator sentinel = e;
    do {
      const Vector3Df & p = (*e)->dst()->position();
      vec3 q { p.x(), p.y(), p.z() };
      if (first) { lo = hi = q; first = false; }
      lo = { fmin(lo.x, q.x), fmin(lo.y, q.y), fmin(lo.z, q.z) };
      hi = { fmax(hi.x, q.x), fmax(hi.y, q.y), fmax(hi.z, q.z) };
      ++e;
    } while (e != sentinel);
  }
  if (first) return false;
  c = { (lo.x+hi.x)/2, (lo.y+hi.y)/2, (lo.z+hi.z)/2 };
  return true;
}

struct pose { float t[3] = { 0, 0, 0 }; float r[4] = { 0, 0, 0, 1 }; bool visible = false; };
struct pieceInfo { unsigned int piece = 0, part = 0, trans0 = 0; mat3 base0inv = mat3::identity(); vec3 centre0 { 0, 0, 0 }; };

void dumpTrack(const problem_c & pr, unsigned int prob, unsigned int solNum) {
  const solution_c * sol = pr.getSavedSolution(solNum);
  const separation_c * tree = sol->getDisassembly();
  const assembly_c * assm = sol->getAssembly();
  const gridType_c * gt = pr.getPuzzle().getGridType();
  const unsigned int numPieces = pr.getNumberOfPieces();

  disasmToMoves_c dtm(tree, 2 * getResultShape(pr)->getBiggestDimension(), numPieces);

  float cx, cy, cz;
  getResultShape(pr)->calculateSize(&cx, &cy, &cz);
  const vec3 centre { cx * 0.5, cy * 0.5, cz * 0.5 };

  std::vector<pieceInfo> pieces;
  {
    unsigned int piece = 0;
    for (unsigned int p = 0; p < pr.getNumberOfParts(); p++)
      for (unsigned int q = 0; q < pr.getPartMaximum(p); q++, piece++) {
        if (!assm->isPlaced(piece)) continue;
        pieceInfo pi;
        pi.piece = piece;
        pi.part = p;
        pi.trans0 = assm->getTransformation(piece);
        std::unique_ptr<voxel_c> v(gt->getVoxel(pr.getPartShape(p)));
        if (!v->transform(pi.trans0) || !meshCentreOf(*v, pi.centre0)) {
          std::printf("track %u %u fail\n", prob, solNum);
          return;
        }
        mat3 b = mat3::identity();
        v->getTransformMatrix(pi.trans0, b.m);
        pi.base0inv = b.transposed();
        pieces.push_back(pi);
      }
  }

  auto evaluate = [&](float step, std::vector<pose> & out) -> bool {
    dtm.setStep(step, false, true);
    out.assign(pieces.size(), pose());
    for (size_t i = 0; i < pieces.size(); i++) {
      const pieceInfo & pi = pieces[i];
      const unsigned int p = pi.piece;
      out[i].visible = dtm.getA(p) > 0;
      unsigned int t = dtm.getTrans(p);
      if (t == (unsigned int)-1) t = pi.trans0;
      std::unique_ptr<voxel_c> v(gt->getVoxel(pr.getPartShape(pi.part)));
      if (!v->transform(t)) return false;
      float x = dtm.getX(p), y = dtm.getY(p), z = dtm.getZ(p);
      v->recalcSpaceCoordinates(&x, &y, &z);
      float hx = v->getHx(), hy = v->getHy(), hz = v->getHz();
      v->recalcSpaceCoordinates(&hx, &hy, &hz);
      const vec3 place { x - hx, y - hy, z - hz };
      mat3 R = mat3::identity();
      vec3 d { 0, 0, 0 };
      if (t != pi.trans0) {
        mat3 mt = mat3::identity();
        v->getTransformMatrix(t, mt.m);
        R = mt * pi.base0inv;
        if (fabs(R.det() - 1) > 1e-6) return false;
        vec3 ct;
        if (!meshCentreOf(*v, ct)) return false;
        d = ct - R * pi.centre0;
      }
      mat3 P = mat3::identity();
      vec3 pl { 0, 0, 0 };
      float ang, ax, ay, az, px, py, pz;
      if (dtm.getRotationAnim(p, &ang, &ax, &ay, &az, &px, &py, &pz)) {
        P = axisAngle(ang, ax, ay, az);
        pl = vec3 { px, py, pz } - place;
      }
      vec3 tr = P * (d - pl) + pl + place - centre;
      out[i].t[0] = float(tr.x); out[i].t[1] = float(tr.y); out[i].t[2] = float(tr.z);
      toQuat(P * R, out[i].r);
    }
    return true;
  };

  const unsigned int steps = tree->sumSteps();
  const unsigned int K = kSamples;
  std::vector<float> forward;
  for (unsigned int s = 0; s < steps; s++)
    for (unsigned int k = 0; k < K; k++)
      forward.push_back(s + float(k) / K);
  forward.push_back(float(steps));

  std::vector<std::vector<pose>> fwdPoses(forward.size());
  for (size_t j = 0; j < forward.size(); j++)
    if (!evaluate(forward[j], fwdPoses[j])) {
      std::printf("track %u %u fail\n", prob, solNum);
      return;
    }

  const float eps = 1.0f / (64.0f * K);
  for (size_t j = 0; j < forward.size(); j++)
    for (size_t i = 0; i < pieces.size(); i++) {
      if (fwdPoses[j][i].visible) continue;
      std::vector<pose> near;
      if (j > 0 && fwdPoses[j-1][i].visible && evaluate(forward[j] - eps, near) && near[i].visible) {
        fwdPoses[j][i] = near[i];
      } else if (j + 1 < forward.size() && fwdPoses[j+1][i].visible && evaluate(forward[j] + eps, near) && near[i].visible) {
        fwdPoses[j][i] = near[i];
      } else {
        for (size_t dj = 1; dj < forward.size(); dj++) {
          if (j >= dj && fwdPoses[j-dj][i].visible) { fwdPoses[j][i] = fwdPoses[j-dj][i]; break; }
          if (j + dj < forward.size() && fwdPoses[j+dj][i].visible) { fwdPoses[j][i] = fwdPoses[j+dj][i]; break; }
        }
      }
      fwdPoses[j][i].visible = false;
    }

  // times with the defaults: 0.5 s a step, holds, reassembly
  const float dt = 0.5f / K;
  const float hold = 2 * dt * K;
  std::vector<float> times;
  std::vector<size_t> keys;
  times.push_back(0);
  keys.push_back(0);
  for (size_t j = 0; j < forward.size(); j++) { times.push_back(hold + j * dt); keys.push_back(j); }
  times.push_back(times.back() + hold);
  keys.push_back(forward.size() - 1);
  {
    float t0 = times.back();
    for (size_t j = 1; j < forward.size(); j++) { times.push_back(t0 + j * dt); keys.push_back(forward.size() - 1 - j); }
    times.push_back(times.back() + hold);
    keys.push_back(0);
  }

  std::printf("track %u %u pieces %zu keys %zu\n", prob, solNum, pieces.size(), keys.size());
  std::vector<pose> last(pieces.size());
  for (size_t j = 0; j < keys.size(); j++) {
    std::printf("key %zu %.4f", j, times[j]);
    for (size_t i = 0; i < pieces.size(); i++) {
      pose ps = fwdPoses[keys[j]][i];
      if (j > 0) {
        const float * q = last[i].r;
        if (q[0]*ps.r[0] + q[1]*ps.r[1] + q[2]*ps.r[2] + q[3]*ps.r[3] < 0)
          for (float & c : ps.r) c = -c;
      }
      last[i] = ps;
      std::printf(" %u %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f", pieces[i].piece, ps.visible ? 1 : 0,
                  ps.t[0] + 0.0f, ps.t[1] + 0.0f, ps.t[2] + 0.0f, ps.r[0] + 0.0f, ps.r[1] + 0.0f,
                  ps.r[2] + 0.0f, ps.r[3] + 0.0f);
    }
    std::printf("\n");
  }
}

void dumpTracks(const puzzle_c & p) {
  const int gt = p.getGridType()->getType();
  if (gt == gridType_c::GT_SLIDING || gt == gridType_c::GT_STACKING) return;
  for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
    const problem_c * pr = p.getProblem(prob);
    if (!pr->resultValid()) continue;
    unsigned int done = 0;
    for (unsigned int s = 0; s < pr->getNumberOfSavedSolutions() && done < kPoseSolutions; s++) {
      if (!pr->getSavedSolution(s)->getDisassembly()) continue;
      dumpTrack(*pr, prob, s);
      done++;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[i]);
    if (!p) {
      std::fprintf(stderr, "cannot load %s\n", argv[i]);
      return 1;
    }
    std::printf("file %s\n", std::filesystem::path(argv[i]).filename().string().c_str());
    dumpShapes(*p);
    dumpFit(*p);
    dumpTracks(*p);
  }
  return 0;
}
