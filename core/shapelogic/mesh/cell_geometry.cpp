// SPDX-License-Identifier: GPL-3.0-or-later
// After the grid registration of BurrTools V2 (src/lib/grid.cpp:
// checkPhysicalShapes, calculateFaceNeighbourhood, the edge loops), on
// this core's cell geometry (connectionFace, getNeighbor).
#include "shapelogic/mesh/cell_geometry.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/grid_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <string>

namespace bt::mesh {

namespace {

constexpr double kNear = 1e-5;

double diff(float a, float b) { return static_cast<double>(a) - static_cast<double>(b); }

}  // namespace

bool samePoint(const Vec3& a, const Vec3& b) {
  return std::abs(diff(a.x, b.x)) < kNear && std::abs(diff(a.y, b.y)) < kNear &&
         std::abs(diff(a.z, b.z)) < kNear;
}

bool pointBefore(const Vec3& a, const Vec3& b) {
  if (std::abs(diff(a.x, b.x)) >= kNear) {
    return a.x < b.x;
  }
  if (std::abs(diff(a.y, b.y)) >= kNear) {
    return a.y < b.y;
  }
  return a.z < b.z;
}

bool CellFace::has(const Vec3& p) const {
  return std::ranges::any_of(corners, [&](const Vec3& c) { return samePoint(c, p); });
}

bool CellPolyhedron::hasCorner(const Vec3& p) const {
  return std::ranges::any_of(corners, [&](const Vec3& c) { return samePoint(c, p); });
}

int CellPolyhedron::faceWith(const std::vector<Vec3>& poly) const {
  for (std::size_t f = 0; f < faces.size(); f++) {
    if (faces[f].size() != poly.size()) {
      continue;
    }
    if (std::ranges::all_of(poly, [&](const Vec3& p) { return faces[f].has(p); })) {
      return asInt(f);
    }
  }
  return -1;
}

std::optional<std::pair<int, int>> CellPolyhedron::otherFace(int face, const Vec3& a,
                                                             const Vec3& b) const {
  for (std::size_t fj = 0; fj < faces.size(); fj++) {
    if (asInt(fj) == face) {
      continue;
    }
    const CellFace& f = faces[fj];
    for (std::size_t k = 0; k < f.size(); k++) {
      if (samePoint(f.pos(k), b) && samePoint(f.pos(k + 1), a)) {
        return std::make_pair(asInt(fj), asInt(k));
      }
    }
  }
  return std::nullopt;
}

std::optional<int> CellPolyhedron::matchingFace(const CellFace& other) const {
  const int f = faceWith(other.corners);
  if (f < 0) {
    return std::nullopt;
  }
  return f;
}

Result<CellPolyhedron> cellPolyhedron(const Voxel& v, CellAt c, const float* stretch) {
  CellPolyhedron g;
  std::vector<float> raw;
  int nx = 0;
  int ny = 0;
  int nz = 0;
  double cx = 0;
  double cy = 0;
  double cz = 0;
  std::size_t count = 0;
  for (unsigned int n = 0; v.getNeighbor(n, 0, c.x, c.y, c.z, &nx, &ny, &nz); n++) {
    raw.clear();
    connectionFace(v, c.x, c.y, c.z, asInt(n), 0, 0, raw);
    if (raw.size() < 9 || raw.size() % 3 != 0) {
      return fail(Errc::Unsupported, "the grid's cells have no face polygons");
    }
    CellFace f;
    f.across = {nx, ny, nz};
    for (std::size_t i = 0; i + 2 < raw.size(); i += 3) {
      Vec3 p(raw[i], raw[i + 1], raw[i + 2]);
      if (stretch != nullptr) {
        p = Vec3(p.x * stretch[0], p.y * stretch[1], p.z * stretch[2]);
      }
      f.corners.push_back(p);
      cx += static_cast<double>(p.x);
      cy += static_cast<double>(p.y);
      cz += static_cast<double>(p.z);
      count++;
      if (!g.hasCorner(p)) {
        g.corners.push_back(p);
      }
    }
    const Vec3 n0 = (f.corners[1] - f.corners[0]).cross(f.corners[2] - f.corners[0]);
    const double l = n0.module();
    if (l < 1e-9) {
      return fail(Errc::Internal, "a cell face without an area");
    }
    f.normal = n0 / static_cast<float>(l);
    g.faces.push_back(std::move(f));
  }
  if (g.faces.size() < 4) {
    return fail(Errc::Unsupported, "the grid's cells have no face polygons");
  }
  g.centroid = Vec3(static_cast<float>(cx / static_cast<double>(count)),
                    static_cast<float>(cy / static_cast<double>(count)),
                    static_cast<float>(cz / static_cast<double>(count)));
  for (std::size_t fi = 0; fi < g.faces.size(); fi++) {
    const CellFace& f = g.faces[fi];
    if (f.normal.dot(f.corners[0] - g.centroid) <= 0) {
      return fail(Errc::Internal, "a cell face wound the wrong way round");
    }
    for (std::size_t e = 0; e < f.size(); e++) {
      if (!g.otherFace(asInt(fi), f.pos(e), f.pos(e + 1))) {
        return fail(Errc::Internal, "a cell edge without a second face");
      }
    }
  }
  return g;
}

std::size_t CellGeometry::Hash::operator()(const CellAt& c) const noexcept {
  const auto h = [](int i) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)); };
  return static_cast<std::size_t>((h(c.x) * 0x9E3779B97F4A7C15ULL) ^ (h(c.y) * 0xC2B2AE3D27D4EB4FULL) ^
                                  (h(c.z) * 0x165667B19E3779F9ULL));
}

CellGeometry::CellGeometry(const Voxel& v, const float* stretch) : v_(v), stretch_(stretch) {}

const CellPolyhedron* CellGeometry::cell(CellAt c) {
  const auto it = cells_.find(c);
  if (it != cells_.end()) {
    return &it->second;
  }
  Result<CellPolyhedron> p = cellPolyhedron(v_, c, stretch_);
  if (!p) {
    if (!error_) {
      error_ = p.error();
    }
    return nullptr;
  }
  return &cells_.emplace(c, std::move(*p)).first->second;
}

bool CellGeometry::empty(CellAt c) const { return v_.isEmpty2(c.x, c.y, c.z); }

std::optional<EdgeRun> CellGeometry::run(const EdgeAt& start) {
  CellAt cur = start.cell;
  int face = start.face;
  const CellPolyhedron* g = cell(cur);
  if (g == nullptr) {
    return std::nullopt;
  }
  const Vec3 a = g->faces[asUnsigned(face)].pos(asUnsigned(start.edge));
  const Vec3 b = g->faces[asUnsigned(face)].pos(asUnsigned(start.edge) + 1);
  EdgeRun out;
  for (int steps = 0; steps < 256; steps++) {
    const std::optional<std::pair<int, int>> other = g->otherFace(face, a, b);
    if (!other) {
      error_ = Error{Errc::Internal, "a cell edge without a second face"};
      return std::nullopt;
    }
    const CellFace& fIn = g->faces[asUnsigned(face)];
    const CellFace& fOut = g->faces[asUnsigned(other->first)];
    const double cosine = std::clamp(static_cast<double>(fIn.normal.dot(fOut.normal)), -1.0, 1.0);
    out.angle += std::numbers::pi - std::acos(cosine);
    if (empty(fOut.across)) {
      out.end = {cur, other->first, other->second};
      return out;
    }
    const CellPolyhedron* gn = cell(fOut.across);
    if (gn == nullptr) {
      return std::nullopt;
    }
    const std::optional<int> fn = gn->matchingFace(fOut);
    if (!fn) {
      error_ = Error{Errc::Internal, "neighbouring cells whose shared faces differ"};
      return std::nullopt;
    }
    cur = fOut.across;
    g = gn;
    face = *fn;
  }
  error_ = Error{Errc::Internal, "no end to the cells around an edge"};
  return std::nullopt;
}

Result<std::vector<CellAt>> CellGeometry::derivedNeighbours(CellAt c, unsigned int kind, int reach,
                                                            std::vector<int>* faces) {
  const CellPolyhedron* g = cell(c);
  if (g == nullptr) {
    return std::unexpected(*error_);
  }
  std::vector<CellAt> out;
  if (faces != nullptr) {
    faces->clear();
  }
  for (int dz = -reach; dz <= reach; dz++) {
    for (int dy = -reach; dy <= reach; dy++) {
      for (int dx = -reach; dx <= reach; dx++) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue;
        }
        const CellAt d{c.x + dx, c.y + dy, c.z + dz};
        if (d.x < 0 || d.y < 0 || d.z < 0 || d.x >= asInt(v_.getX()) || d.y >= asInt(v_.getY()) ||
            d.z >= asInt(v_.getZ()) || !v_.validCoordinate(d.x, d.y, d.z)) {
          continue;
        }
        const CellPolyhedron* h = cell(d);
        if (h == nullptr) {
          return std::unexpected(*error_);
        }
        int shared = 0;
        for (const Vec3& p : g->corners) {
          if (h->hasCorner(p)) {
            shared++;
          }
        }
        if (shared == 0) {
          continue;
        }
        int sharedFace = -1;
        for (std::size_t f = 0; f < g->faces.size(); f++) {
          if (h->faceWith(g->faces[f].corners) >= 0) {
            sharedFace = asInt(f);
          }
        }
        const unsigned int found = sharedFace >= 0 ? 0U : (shared >= 2 ? 1U : 2U);
        if (found != kind) {
          continue;
        }
        out.push_back(d);
        if (faces != nullptr) {
          faces->push_back(sharedFace);
        }
      }
    }
  }
  return out;
}

}  // namespace bt::mesh
