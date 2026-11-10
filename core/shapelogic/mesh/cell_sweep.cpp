// SPDX-License-Identifier: GPL-3.0-or-later
// After calculateMovementChecks of BurrTools V2 (src/lib/grid.cpp), with
// the separating axis theorem in place of its shrunk Minkowski sum. New.
#include "shapelogic/mesh/cell_sweep.h"

#include "shapelogic/grid/voxel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bt::mesh {

namespace {

using Vec = std::array<double, 3>;

// Two bodies that overlap less than this along some direction only touch:
// well above the float corners' error, far below any real overlap (the
// cells are about 1 across and their corners lie on a lattice).
constexpr double kTouch = 1e-4;

// Directions shorter than this (after the cross product of two unit
// vectors) are no direction: the two were parallel.
constexpr double kParallel = 1e-6;

Vec toVec(const Vec3& p) {
  return {static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)};
}
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec& a, const Vec& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// The vector at length 1; false when it has no length to speak of.
bool unit(Vec& v) {
  const double l = std::sqrt(dot(v, v));
  if (l < kParallel) {
    return false;
  }
  v = {v[0] / l, v[1] / l, v[2] / l};
  return true;
}

// The directions of a cell's edges, each once (either way round).
std::vector<Vec> edgeDirections(const CellPolyhedron& p) {
  std::vector<Vec> out;
  for (const CellFace& f : p.faces) {
    for (std::size_t i = 0; i < f.size(); i++) {
      Vec e = sub(toVec(f.pos(i + 1)), toVec(f.pos(i)));
      if (!unit(e)) {
        continue;
      }
      const bool known = std::ranges::any_of(
          out, [&](const Vec& o) { return std::abs(std::abs(dot(o, e)) - 1) < kParallel; });
      if (!known) {
        out.push_back(e);
      }
    }
  }
  return out;
}

struct Range {
  double lo;
  double hi;
};

Range extentAlong(const std::vector<Vec>& corners, const Vec& n) {
  Range r{dot(corners[0], n), dot(corners[0], n)};
  for (const Vec& c : corners) {
    const double v = dot(c, n);
    r.lo = std::min(r.lo, v);
    r.hi = std::max(r.hi, v);
  }
  return r;
}

std::vector<Vec> cornersOf(const CellPolyhedron& p) {
  std::vector<Vec> out;
  out.reserve(p.corners.size());
  for (const Vec3& c : p.corners) {
    out.push_back(toVec(c));
  }
  return out;
}

}  // namespace

// The swept cell is the convex hull of the cell and the moved cell. Its
// faces are faces of the cell (at either end) and faces spanned by an
// edge of the cell and the move; its edges are edges of the cell and
// lines along the move. Two convex polyhedra share inner points unless
// one of these separates them: a face plane of either, or a plane along
// an edge of each.
bool sweepEnters(const CellPolyhedron& cell, const Vec3& move, const CellPolyhedron& other) {
  const std::vector<Vec> a = cornersOf(cell);
  const std::vector<Vec> b = cornersOf(other);
  const Vec d = toVec(move);

  const auto apart = [&](Vec n) {
    if (!unit(n)) {
      return false;
    }
    Range ra = extentAlong(a, n);
    const double along = dot(n, d);
    if (along > 0) {
      ra.hi += along;
    } else {
      ra.lo += along;
    }
    const Range rb = extentAlong(b, n);
    return std::min(ra.hi, rb.hi) - std::max(ra.lo, rb.lo) < kTouch;
  };

  // the coordinate axes first: they are no part of the proof, but most
  // cells asked about are far away and these tell so at once
  if (apart({1, 0, 0}) || apart({0, 1, 0}) || apart({0, 0, 1})) {
    return false;
  }

  for (const CellFace& f : cell.faces) {
    if (apart(toVec(f.normal))) {
      return false;
    }
  }
  for (const CellFace& f : other.faces) {
    if (apart(toVec(f.normal))) {
      return false;
    }
  }

  const std::vector<Vec> ea = edgeDirections(cell);
  const std::vector<Vec> eb = edgeDirections(other);
  for (const Vec& e : ea) {
    if (apart(cross(e, d))) {
      return false;
    }
  }
  for (const Vec& e : eb) {
    if (apart(cross(d, e))) {
      return false;
    }
    for (const Vec& e2 : ea) {
      if (apart(cross(e2, e))) {
        return false;
      }
    }
  }
  return true;
}

Result<std::vector<CellAt>> sweptCells(CellGeometry& g, CellAt c, CellAt step, CellAt reach) {
  const Voxel& v = g.voxel();
  const CellAt to{c.x + step.x, c.y + step.y, c.z + step.z};
  if (!v.validCoordinate(c.x, c.y, c.z) || !v.validCoordinate(to.x, to.y, to.z)) {
    return fail(Errc::InvalidArgument, "a move from or to a position that is no cell");
  }
  const CellPolyhedron* from = g.cell(c);
  const CellPolyhedron* end = g.cell(to);
  if (from == nullptr || end == nullptr) {
    return std::unexpected(*g.error());
  }

  // the move in mesh units, and whether it carries the cell onto the one
  // it ends in
  const Vec3 move = end->centroid - from->centroid;
  const bool same =
      from->corners.size() == end->corners.size() &&
      std::ranges::all_of(from->corners, [&](const Vec3& p) { return end->hasCorner(p + move); });
  if (!same) {
    return fail(Errc::InvalidArgument, "the step does not move the grid onto itself");
  }

  std::vector<CellAt> out;
  for (int x = c.x + std::min(0, step.x) - reach.x; x <= c.x + std::max(0, step.x) + reach.x;
       x++) {
    for (int y = c.y + std::min(0, step.y) - reach.y; y <= c.y + std::max(0, step.y) + reach.y;
         y++) {
      for (int z = c.z + std::min(0, step.z) - reach.z; z <= c.z + std::max(0, step.z) + reach.z;
           z++) {
        const CellAt q{x, y, z};
        if (q == c || !v.validCoordinate(x, y, z)) {
          continue;
        }
        const CellPolyhedron* other = g.cell(q);
        if (other == nullptr) {
          return std::unexpected(*g.error());
        }
        if (sweepEnters(*from, move, *other)) {
          out.push_back({x - c.x, y - c.y, z - c.z});
        }
      }
    }
  }
  return out;
}

}  // namespace bt::mesh
