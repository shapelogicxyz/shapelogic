// SPDX-License-Identifier: GPL-3.0-or-later
// After createMeshShape of BurrTools V2 (src/lib/polygen.cpp: MeshBuilder,
// HoleHandler) and calculateBevelOffsetVertices of its src/lib/grid.cpp,
// on this core's cell geometry (cell_geometry.h).
#include "shapelogic/mesh/uniform_mesh.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cell_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bt::mesh {

namespace {

// Two planes whose normals agree to this are one plane (cos of the angle).
constexpr double kSamePlane = 0.9999;
// How far a hole's points may lie off their plane and still be one polygon.
constexpr double kPlanar = 1e-4;

// A vector in double precision, for the plane arithmetic.
struct Vec3d {
  double x = 0;
  double y = 0;
  double z = 0;
  explicit Vec3d(const Vec3& v)
      : x(static_cast<double>(v.x)), y(static_cast<double>(v.y)), z(static_cast<double>(v.z)) {}
  Vec3d(double px, double py, double pz) : x(px), y(py), z(pz) {}
  Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3d operator*(double f) const { return {x * f, y * f, z * f}; }
  [[nodiscard]] double dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
  [[nodiscard]] Vec3d cross(const Vec3d& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  [[nodiscard]] double length() const { return std::sqrt(dot(*this)); }
  [[nodiscard]] Vec3 toFloat() const {
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
  }
};

// The plane n . p = d.
struct Plane {
  Vec3d n{0, 0, 0};
  double d = 0;
};

// The point on all three planes, or none when two of them are parallel.
std::optional<Vec3d> intersect(const Plane& a, const Plane& b, const Plane& c) {
  const double det = a.n.dot(b.n.cross(c.n));
  if (std::abs(det) < 1e-9) {
    return std::nullopt;
  }
  const Vec3d p = (b.n.cross(c.n) * a.d + c.n.cross(a.n) * b.d + a.n.cross(b.n) * c.d) * (1 / det);
  return p;
}

// Where a corner of a cell's face moves per unit of bevel at the edge
// before it (which slides it along the edge after it), per unit of bevel
// at the edge after it, and per unit of offset.
struct CornerVectors {
  Vec3 bevelPrev;
  Vec3 bevelNext;
  Vec3 offset;
};

// Finds or adds vertices by position, points within the tables' tolerance
// being one vertex (VertexList matches exactly; the corners here are
// computed).
class NearVertices {
 public:
  explicit NearVertices(Polyhedron& poly) : poly_(poly) {}

  int get(const Vec3& p) {
    const std::array<long long, 3> key = keyOf(p);
    for (long long dx = -1; dx <= 1; dx++) {
      for (long long dy = -1; dy <= 1; dy++) {
        for (long long dz = -1; dz <= 1; dz++) {
          const auto it = known_.find({key[0] + dx, key[1] + dy, key[2] + dz});
          if (it != known_.end() && samePoint(poly_.vertex(it->second).pos, p)) {
            return it->second;
          }
        }
      }
    }
    const int idx = poly_.addVertex(p);
    known_.emplace(key, idx);
    return idx;
  }

 private:
  static std::array<long long, 3> keyOf(const Vec3& p) {
    constexpr double kCell = 1e-4;
    return {static_cast<long long>(std::floor(static_cast<double>(p.x) / kCell)),
            static_cast<long long>(std::floor(static_cast<double>(p.y) / kCell)),
            static_cast<long long>(std::floor(static_cast<double>(p.z) / kCell))};
  }

  Polyhedron& poly_;
  std::map<std::array<long long, 3>, int> known_;
};

// Collects the loose side edges of the bevel and gap strips and chains
// them into the polygons that close the corners (V2's HoleHandler, on
// vertex ids instead of positions). An edge from e1 to e2 is the side of
// a strip whose face has the given normal; the chain runs the other way
// round the corner than the strips do, so it comes out facing outwards.
class HoleChainer {
 public:
  explicit HoleChainer(Polyhedron& poly) : poly_(poly) {}

  void addEdge(int e1, int e2, const Vec3d& normal, std::uint32_t voxel, std::uint32_t type,
               std::uint8_t colour) {
    if (e1 == e2) {
      return;
    }
    const auto itEnd = byEnd_.find(e1);
    const auto itStart = byStart_.find(e2);
    const std::size_t id1 = itEnd == byEnd_.end() ? kNoHole : itEnd->second;
    const std::size_t id2 = itStart == byStart_.end() ? kNoHole : itStart->second;

    if (id1 == kNoHole && id2 == kNoHole) {
      // a new chain
      Hole h;
      h.points.push_back({e1, normal});
      h.points.push_back({e2, normal});
      h.voxel = voxel;
      h.type = type;
      h.colour = colour;
      byStart_[e1] = holes_.size();
      byEnd_[e2] = holes_.size();
      holes_.push_back(std::move(h));
    } else if (id1 == kNoHole) {
      // in front of a chain
      Hole& h = holes_[id2];
      byStart_.erase(itStart);
      h.points.insert(h.points.begin(), {e1, normal});
      byStart_[e1] = id2;
    } else if (id2 == kNoHole) {
      // behind a chain
      Hole& h = holes_[id1];
      byEnd_.erase(itEnd);
      h.points.back().normal = normal;
      h.points.push_back({e2, normal});
      byEnd_[e2] = id1;
    } else if (id1 != id2) {
      // two chains become one
      Hole& h1 = holes_[id1];
      Hole& h2 = holes_[id2];
      byEnd_.erase(itEnd);
      byStart_.erase(itStart);
      h1.points.back().normal = normal;
      h1.points.insert(h1.points.end(), h2.points.begin(), h2.points.end());
      byEnd_[h1.points.back().vertex] = id1;
      h2.points.clear();
    } else {
      // the chain closes: a polygon
      Hole& h = holes_[id1];
      byEnd_.erase(itEnd);
      byStart_.erase(itStart);
      h.points.back().normal = normal;
      close(h);
      h.points.clear();
    }
  }

  // Adds the polygons of the closed chains to the mesh (an open chain
  // stays a hole, which finalize() will close).
  void emit() {
    for (const Finished& f : finished_) {
      const int id = poly_.addFace(f.vertices);
      Face& face = poly_.face(id);
      face.flags = kFaceWireframe | f.type | (f.flat ? kFaceOffset : kFaceBevel);
      face.voxel = f.voxel;
      face.voxelFace = -1;
      face.colour = f.colour;
    }
    finished_.clear();
  }

 private:
  static constexpr std::size_t kNoHole = static_cast<std::size_t>(-1);

  struct Point {
    int vertex;
    Vec3d normal;  // of the strip outside the edge from this point to the next
  };
  struct Hole {
    std::vector<Point> points;
    std::uint32_t voxel = 0;
    std::uint32_t type = 0;
    std::uint8_t colour = 0;
  };
  struct Finished {
    std::vector<int> vertices;
    bool flat = false;
    std::uint32_t voxel = 0;
    std::uint32_t type = 0;
    std::uint8_t colour = 0;
  };

  [[nodiscard]] Vec3d pos(int vertex) const { return Vec3d(poly_.vertex(vertex).pos); }

  // The unit normal of the polygon through the points, from the three
  // that span the largest triangle; none when they are all in a line.
  [[nodiscard]] std::optional<Vec3d> polygonNormal(const std::vector<Point>& pts) const {
    double best = 0;
    Vec3d normal{0, 0, 0};
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; i++) {
      const Vec3d a = pos(pts[i].vertex);
      const Vec3d b = pos(pts[(i + 1) % n].vertex);
      const Vec3d c = pos(pts[(i + 2) % n].vertex);
      const Vec3d x = (b - a).cross(c - a);
      const double l = x.dot(x);
      if (l > best) {
        best = l;
        normal = x;
      }
    }
    if (best < 1e-20) {
      return std::nullopt;
    }
    return normal * (1 / std::sqrt(best));
  }

  [[nodiscard]] bool coplanar(const std::vector<Point>& pts, const Vec3d& normal) const {
    const double d = normal.dot(pos(pts[0].vertex));
    return std::ranges::all_of(pts, [&](const Point& p) {
      return std::abs(normal.dot(pos(p.vertex)) - d) < kPlanar;
    });
  }

  void finish(std::vector<int> vertices, bool flat, const Hole& h) {
    finished_.push_back({std::move(vertices), flat, h.voxel, h.type, h.colour});
  }

  void close(const Hole& h) {
    std::vector<Point> pts = h.points;
    if (pts.size() < 3) {
      return;
    }
    const std::optional<Vec3d> pn = polygonNormal(pts);
    if (!pn) {
      return;
    }
    if (coplanar(pts, *pn)) {
      // one polygon; flat when it lies in the plane of every strip around it
      std::vector<int> ids;
      bool flat = true;
      for (const Point& p : pts) {
        ids.push_back(p.vertex);
        if (p.normal.dot(*pn) < 0.99) {
          flat = false;
        }
      }
      finish(std::move(ids), flat, h);
      return;
    }

    // A bent corner. First snip off the triangles whose two strips lie
    // in one plane with them (a flat part of the corner), then fan what
    // is left around its centre.
    bool found = true;
    while (found && pts.size() > 3) {
      found = false;
      const std::size_t n = pts.size();
      for (std::size_t i = 0; i < n; i++) {
        const Point& p0 = pts[i];
        const Point& p1 = pts[(i + 1) % n];
        const Point& p2 = pts[(i + 2) % n];
        if (p0.normal.dot(p1.normal) <= kSamePlane) {
          continue;
        }
        Vec3d t = (pos(p1.vertex) - pos(p0.vertex)).cross(pos(p2.vertex) - pos(p0.vertex));
        const double l = t.length();
        if (l < 1e-12) {
          continue;
        }
        t = t * (1 / l);
        if (t.dot(p0.normal) > kSamePlane) {
          finish({p0.vertex, p1.vertex, p2.vertex}, true, h);
          pts.erase(pts.begin() + static_cast<std::ptrdiff_t>((i + 1) % n));
          found = true;
          break;
        }
      }
    }
    if (pts.size() < 3) {
      return;
    }
    if (pts.size() == 3) {
      finish({pts[0].vertex, pts[1].vertex, pts[2].vertex}, false, h);
      return;
    }
    Vec3d centre{0, 0, 0};
    for (const Point& p : pts) {
      centre = centre + pos(p.vertex);
    }
    centre = centre * (1.0 / static_cast<double>(pts.size()));
    const int c = poly_.addVertex(centre.toFloat());
    for (std::size_t i = 0; i < pts.size(); i++) {
      finish({c, pts[i].vertex, pts[(i + 1) % pts.size()].vertex}, false, h);
    }
  }

  Polyhedron& poly_;
  std::vector<Hole> holes_;
  std::unordered_map<int, std::size_t> byStart_;
  std::unordered_map<int, std::size_t> byEnd_;
  std::vector<Finished> finished_;
};

class Mesher {
 public:
  Mesher(const Voxel& v, const UniformMeshOptions& o)
      : v_(v),
        o_(o),
        // the chamfer's width: the cut into each face of a right-angle
        // edge, across the diagonal
        bevel_(o.bevel < 1e-5 ? 0.0F : static_cast<float>(o.bevel * std::numbers::sqrt2)),
        offset_(o.offset < 1e-5 ? 0.0F : static_cast<float>(o.offset)),
        vertices_(poly_),
        holes_(poly_),
        geometry_(v, o.stretch) {}

  Result<Polyhedron> run() {
    for (unsigned int z = 0; z < v_.getZ() && !error_; z++) {
      for (unsigned int y = 0; y < v_.getY() && !error_; y++) {
        for (unsigned int x = 0; x < v_.getX() && !error_; x++) {
          if (v_.isEmpty(x, y, z)) {
            continue;
          }
          const CellAt c{asInt(x), asInt(y), asInt(z)};
          if (o_.bumpy) {
            bumpyCell(c);
          } else {
            plainCell(c);
          }
        }
      }
    }
    if (error_) {
      return std::unexpected(*error_);
    }
    holes_.emit();
    poly_.finalize();
    return std::move(poly_);
  }

 private:
  // ---- the cells' geometry ----

  void failWith(Errc code, std::string why) {
    if (!error_) {
      error_ = Error{code, std::move(why)};
    }
  }

  // a failure of the geometry becomes this mesher's
  void failFromGeometry() {
    if (!error_ && geometry_.error()) {
      error_ = *geometry_.error();
    }
  }

  [[nodiscard]] unsigned int indexOf(const CellAt& c) const {
    return v_.getIndex(asUnsigned(c.x), asUnsigned(c.y), asUnsigned(c.z));
  }

  // The polyhedron of a filled cell; nullptr after a failure (error_ set).
  const CellPolyhedron* cell(const CellAt& c) {
    const CellPolyhedron* p = geometry_.cell(c);
    if (p == nullptr) {
      failFromGeometry();
    }
    return p;
  }

  // The bevel and offset directions of every corner of every face of a
  // cell, worked out once: the two faces across the edges at the corner
  // moved in by one unit, and all three.
  const std::vector<std::vector<CornerVectors>>* vectorsOf(const CellAt& c) {
    const unsigned int idx = indexOf(c);
    const auto it = vectors_.find(idx);
    if (it != vectors_.end()) {
      return &it->second;
    }
    const CellPolyhedron* g = cell(c);
    if (g == nullptr) {
      return nullptr;
    }
    std::vector<std::vector<CornerVectors>> all(g->faces.size());
    for (std::size_t fi = 0; fi < g->faces.size(); fi++) {
      const CellFace& f = g->faces[fi];
      const std::size_t n = f.size();
      all[fi].resize(n);
      for (std::size_t i = 0; i < n; i++) {
        const Vec3& p = f.corners[i];
        // the faces across the edge before and after this corner
        const std::optional<std::pair<int, int>> prev =
            g->otherFace(asInt(fi), f.pos(i + n - 1), p);
        const std::optional<std::pair<int, int>> next = g->otherFace(asInt(fi), p, f.pos(i + 1));
        if (!prev || !next || prev->first == next->first) {
          failWith(Errc::Internal, "a cell edge without a second face");
          return nullptr;
        }
        Plane planes[3];
        planes[0].n = Vec3d(f.normal);
        planes[1].n = Vec3d(g->faces[asUnsigned(prev->first)].normal);
        planes[2].n = Vec3d(g->faces[asUnsigned(next->first)].normal);
        for (Plane& pl : planes) {
          pl.d = pl.n.dot(Vec3d(p));
        }
        const double angle1 = std::acos(std::clamp(planes[0].n.dot(planes[1].n), -1.0, 1.0));
        const double angle2 = std::acos(std::clamp(planes[0].n.dot(planes[2].n), -1.0, 1.0));

        const std::optional<Vec3d> base = intersect(planes[0], planes[1], planes[2]);
        if (!base || !samePoint(base->toFloat(), p)) {
          failWith(Errc::Internal, "the planes at a cell corner do not meet there");
          return nullptr;
        }
        // each plane moved in by one unit: the corner slides along the
        // other two; all three moved in: the offset
        planes[1].d -= 1;
        const std::optional<Vec3d> bPrev = intersect(planes[0], planes[1], planes[2]);
        planes[1].d += 1;
        planes[2].d -= 1;
        const std::optional<Vec3d> bNext = intersect(planes[0], planes[1], planes[2]);
        planes[1].d -= 1;
        planes[0].d -= 1;
        const std::optional<Vec3d> off = intersect(planes[0], planes[1], planes[2]);
        if (!bPrev || !bNext || !off) {
          failWith(Errc::Internal, "the planes at a cell corner do not meet");
          return nullptr;
        }
        // scaled so that the chamfer face at an edge is as wide at any
        // angle of the edge
        all[fi][i].bevelPrev = ((*bPrev - *base) * std::sin(angle1 / 2)).toFloat();
        all[fi][i].bevelNext = ((*bNext - *base) * std::sin(angle2 / 2)).toFloat();
        all[fi][i].offset = (*off - *base).toFloat();
      }
    }
    return &vectors_.emplace(idx, std::move(all)).first->second;
  }

  // ---- the cells around an edge ----

  // Whether the edges of a face are bevelled (1) or not (0): all of them,
  // unless only convex edges are, when the cells around the edge decide.
  const std::vector<float>& mults(const CellAt& c, int face) {
    const std::uint64_t key = (static_cast<std::uint64_t>(indexOf(c)) << 8) |
                              static_cast<std::uint64_t>(static_cast<unsigned int>(face) & 0xff);
    const auto it = mults_.find(key);
    if (it != mults_.end()) {
      return it->second;
    }
    const CellPolyhedron* g = cell(c);
    BT_ASSERT(g != nullptr);
    const std::size_t n = g->faces[asUnsigned(face)].size();
    std::vector<float> m(n, 1.0F);
    if (o_.convexBevelOnly && !o_.bumpy && bevel_ > 0) {
      for (std::size_t e = 0; e < n; e++) {
        const std::optional<EdgeRun> a = geometry_.run({c, face, asInt(e)});
        if (!a) {
          failFromGeometry();
          break;
        }
        m[e] = a->angle < std::numbers::pi + 1e-3 ? 1.0F : 0.0F;
      }
    }
    return mults_.emplace(key, std::move(m)).first->second;
  }

  // The vertex of a face's corner, moved by the offset and the bevels
  // of the two edges at it.
  int vertexOf(const CellAt& c, int face, int corner) {
    const CellPolyhedron* g = cell(c);
    BT_ASSERT(g != nullptr);
    const CellFace& f = g->faces[asUnsigned(face)];
    const std::size_t n = f.size();
    const std::size_t i = asUnsigned(corner) % n;
    const std::tuple<unsigned int, int, int> key{indexOf(c), face, asInt(i)};
    const auto it = vertexOf_.find(key);
    if (it != vertexOf_.end()) {
      return it->second;
    }
    const std::vector<std::vector<CornerVectors>>* vecs = vectorsOf(c);
    BT_ASSERT(vecs != nullptr);
    const std::vector<float>& m = mults(c, face);
    const CornerVectors& k = (*vecs)[asUnsigned(face)][i];
    const Vec3 p = f.corners[i] + k.offset * offset_ + k.bevelPrev * (bevel_ * m[(i + n - 1) % n]) +
                   k.bevelNext * (bevel_ * m[i]);
    const int id = vertices_.get(p);
    vertexOf_.emplace(key, id);
    return id;
  }

  // ---- building the faces ----

  struct CellInfo {
    std::uint32_t idx = 0;
    std::uint32_t type = 0;
    std::uint8_t colour = 0;
    bool variable = false;
  };

  [[nodiscard]] CellInfo infoOf(const CellAt& c) const {
    const auto x = asUnsigned(c.x);
    const auto y = asUnsigned(c.y);
    const auto z = asUnsigned(c.z);
    CellInfo info;
    info.idx = v_.getIndex(x, y, z);
    info.type = ((x + y + z) & 1) == 0 ? kFaceColorLight : 0;
    info.colour = static_cast<std::uint8_t>(v_.getColor(x, y, z));
    info.variable = v_.isVariable(x, y, z);
    return info;
  }

  // A face of the mesh through distinct vertices (consecutive repeats
  // dropped); kNone when fewer than three are left.
  int addPolygon(std::vector<int> ids, const CellInfo& info, std::uint32_t flags, int side) {
    std::vector<int> distinct;
    for (std::size_t i = 0; i < ids.size(); i++) {
      if (distinct.empty() || distinct.back() != ids[i]) {
        distinct.push_back(ids[i]);
      }
    }
    while (distinct.size() > 1 && distinct.front() == distinct.back()) {
      distinct.pop_back();
    }
    if (distinct.size() < 3) {
      return kNone;
    }
    const int id = poly_.addFace(distinct);
    Face& f = poly_.face(id);
    f.flags = flags;
    f.voxel = info.idx;
    f.voxelFace = side;
    f.colour = info.colour;
    return id;
  }

  [[nodiscard]] Vec3d normalOf(int face) const {
    const Vec3 n = poly_.faceNormal(face);
    return Vec3d(n);
  }

  // The strip [b1, a1, a2, b2] between the edge a1-b1 of one face and
  // a2-b2 (the same edge seen from the other face, so a2 is near a1):
  // bent, it is a bevel; flat, the gap between two cells. Its sides join
  // the chains that close the corners.
  void addStrip(int b1, int a1, int a2, int b2, const Vec3& faceNormal, const CellInfo& info,
                bool chain) {
    const int id = addPolygon({b1, a1, a2, b2}, info, 0, -1);
    if (id == kNone) {
      return;
    }
    const Vec3d n = normalOf(id);
    const bool flat = n.dot(Vec3d(faceNormal)) > kSamePlane;
    poly_.face(id).flags = kFaceWireframe | info.type | (flat ? kFaceOffset : kFaceBevel);
    if (chain) {
      holes_.addEdge(a2, a1, n, info.idx, info.type, info.colour);
      holes_.addEdge(b1, b2, n, info.idx, info.type, info.colour);
    }
  }

  void plainCell(const CellAt& c) {
    const CellPolyhedron* g = cell(c);
    if (g == nullptr) {
      return;
    }
    const CellInfo info = infoOf(c);
    for (std::size_t n = 0; n < g->faces.size() && !error_; n++) {
      const CellFace& f = g->faces[n];
      if (!geometry_.empty(f.across)) {
        continue;
      }
      const int face = asInt(n);
      std::vector<int> ids;
      for (std::size_t i = 0; i < f.size(); i++) {
        ids.push_back(vertexOf(c, face, asInt(i)));
      }
      addPolygon(std::move(ids), info, info.type | (info.variable ? kFaceVariableMark : 0), face);

      if (bevel_ <= 0 && offset_ <= 0) {
        continue;
      }
      // the strip at each edge, drawn by the face that sees the edge
      // running upwards (the face at its other end sees it the other way)
      for (std::size_t e = 0; e < f.size() && !error_; e++) {
        const Vec3& a = f.pos(e);
        const Vec3& b = f.pos(e + 1);
        if (!pointBefore(a, b)) {
          continue;
        }
        const std::optional<EdgeRun> runEnd = geometry_.run({c, face, asInt(e)});
        if (!runEnd) {
          failFromGeometry();
          return;
        }
        const EdgeAt& end = runEnd->end;
        const int a1 = vertexOf(c, face, asInt(e));
        const int b1 = vertexOf(c, face, asInt(e + 1));
        const int b2 = vertexOf(end.cell, end.face, end.edge);
        const int a2 = vertexOf(end.cell, end.face, end.edge + 1);
        addStrip(b1, a1, a2, b2, f.normal, info, true);
      }
    }
  }

  void bumpyCell(const CellAt& c) {
    const CellPolyhedron* g = cell(c);
    if (g == nullptr) {
      return;
    }
    const CellInfo info = infoOf(c);
    for (std::size_t n = 0; n < g->faces.size() && !error_; n++) {
      const CellFace& f = g->faces[n];
      const int face = asInt(n);
      const bool empty = geometry_.empty(f.across);
      if (empty) {
        std::vector<int> ids;
        for (std::size_t i = 0; i < f.size(); i++) {
          ids.push_back(vertexOf(c, face, asInt(i)));
        }
        addPolygon(std::move(ids), info, info.type | (info.variable ? kFaceVariableMark : 0),
                   face);
      }
      if (bevel_ > 0) {
        // the bevel between this face and the next one of the cell at
        // each edge, once per pair
        for (std::size_t e = 0; e < f.size(); e++) {
          const std::optional<std::pair<int, int>> other =
              g->otherFace(face, f.pos(e), f.pos(e + 1));
          if (!other) {
            failWith(Errc::Internal, "a cell edge without a second face");
            return;
          }
          if (face >= other->first) {
            continue;
          }
          const int a1 = vertexOf(c, face, asInt(e));
          const int b1 = vertexOf(c, face, asInt(e + 1));
          const int b2 = vertexOf(c, other->first, other->second);
          const int a2 = vertexOf(c, other->first, other->second + 1);
          addStrip(b1, a1, a2, b2, f.normal, info, true);
        }
      }
      if (!empty && offset_ > 0) {
        // the neck to the filled neighbour, drawn by the lower cell
        const CellAt d = f.across;
        if (!(c < d)) {
          continue;
        }
        const CellPolyhedron* gd = cell(d);
        if (gd == nullptr) {
          return;
        }
        const std::optional<int> fd = gd->matchingFace(f);
        if (!fd) {
          failWith(Errc::Internal, "neighbouring cells whose shared faces differ");
          return;
        }
        const CellFace& fOther = gd->faces[asUnsigned(*fd)];
        for (std::size_t e = 0; e < f.size(); e++) {
          const Vec3& a = f.pos(e);
          const Vec3& b = f.pos(e + 1);
          int k = -1;
          for (std::size_t j = 0; j < fOther.size(); j++) {
            if (samePoint(fOther.pos(j), b) && samePoint(fOther.pos(j + 1), a)) {
              k = asInt(j);
              break;
            }
          }
          if (k < 0) {
            failWith(Errc::Internal, "neighbouring cells whose shared faces differ");
            return;
          }
          const int a1 = vertexOf(c, face, asInt(e));
          const int b1 = vertexOf(c, face, asInt(e + 1));
          const int b2 = vertexOf(d, *fd, k);
          const int a2 = vertexOf(d, *fd, k + 1);
          addPolygon({a1, b1, b2, a2}, info, kFaceWireframe | info.type | kFaceOffset, -1);
        }
      }
    }
  }

  const Voxel& v_;
  const UniformMeshOptions& o_;
  const float bevel_;
  const float offset_;
  Polyhedron poly_;
  NearVertices vertices_;
  HoleChainer holes_;
  CellGeometry geometry_;
  std::unordered_map<unsigned int, std::vector<std::vector<CornerVectors>>> vectors_;
  std::unordered_map<std::uint64_t, std::vector<float>> mults_;
  std::map<std::tuple<unsigned int, int, int>, int> vertexOf_;
  std::optional<Error> error_;
};

}  // namespace

Result<Polyhedron> uniformMesh(const Voxel& v, const UniformMeshOptions& options) {
  Mesher m(v, options);
  return m.run();
}

}  // namespace bt::mesh
