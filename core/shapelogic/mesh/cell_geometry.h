// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The cells of a grid as convex polyhedra, and what follows from them:
/// which cells share a face, an edge or a corner, the other face of a
/// cell at an edge, the face of a neighbour that coincides with one of
/// ours, and the run of filled cells around an edge. Everything comes
/// from connectionFace at zero bevel and offset (grid_geometry.h) and
/// getNeighbor's face list, so it holds for any grid whose cells are
/// convex polyhedra whose shared faces coincide (every grid but the
/// spheres' balls, whose faces are those of the rhombic dodecahedron
/// around each ball and still give the face neighbours).
/// This is the runtime half of docs/V2-IDEAS-PLAN.md's idea 2: BurrTools
/// V2 derives the same from each grid's registered polyhedra
/// (src/lib/grid.cpp); here the derivation serves the uniform mesher
/// (uniform_mesh.h), checks the hand-written tables
/// (tests/unit/grid/test_grid_oracle.cpp) and is what the derived movement
/// checks (idea 4: cell_sweep.h, solve/movement_checks.h) are built on.

#include "shapelogic/base/error.h"
#include "shapelogic/mesh/vec3.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// A cell by its coordinates.
struct CellAt {
  int x = 0;
  int y = 0;
  int z = 0;
  bool operator==(const CellAt&) const = default;
  auto operator<=>(const CellAt&) const = default;
};

/// Whether two points are one point: the corners come from float tables,
/// exactly equal between neighbouring cells; the tolerance covers the
/// computed ones (1e-5 mesh units).
[[nodiscard]] bool samePoint(const Vec3& a, const Vec3& b);

/// Lexicographic order of two points that are not the same point: tells
/// the two faces at an edge apart (each runs it the other way).
[[nodiscard]] bool pointBefore(const Vec3& a, const Vec3& b);

/// A face of a cell's polyhedron.
struct CellFace {
  /// The corners, counter clockwise seen from outside.
  std::vector<Vec3> corners;
  /// The unit normal, outward.
  Vec3 normal;
  /// The cell across the face (getNeighbor's face neighbour).
  CellAt across;

  [[nodiscard]] std::size_t size() const { return corners.size(); }
  /// Corner i, cyclic.
  [[nodiscard]] const Vec3& pos(std::size_t i) const { return corners[i % corners.size()]; }
  [[nodiscard]] bool has(const Vec3& p) const;
};

/// A cell as a convex polyhedron.
struct CellPolyhedron {
  std::vector<CellFace> faces;
  /// The distinct corners.
  std::vector<Vec3> corners;
  /// The mean of the corners: a point inside.
  Vec3 centroid;

  [[nodiscard]] bool hasCorner(const Vec3& p) const;
  /// The face with exactly these corners (in any order), or -1.
  [[nodiscard]] int faceWith(const std::vector<Vec3>& poly) const;
  /// The other face of the cell at the edge from a to b (it runs the
  /// edge from b to a): the face and the index of b in it.
  [[nodiscard]] std::optional<std::pair<int, int>> otherFace(int face, const Vec3& a,
                                                              const Vec3& b) const;
  /// The face that coincides with `other` (a neighbour's face towards
  /// this cell), or none.
  [[nodiscard]] std::optional<int> matchingFace(const CellFace& other) const;
};

/// The polyhedron of a cell; with `stretch` the corners are scaled along
/// the axes first. Unsupported when the grid's cells have no face
/// polygons (fewer than four faces, or a face without three corners),
/// Internal when a face has no area, is wound inwards or lacks the second
/// face an edge needs.
[[nodiscard]] Result<CellPolyhedron> cellPolyhedron(const Voxel& v, CellAt c,
                                                    const float* stretch = nullptr);

/// An edge of a cell: from corner `edge` of face `face` to the next.
struct EdgeAt {
  CellAt cell;
  int face = 0;
  int edge = 0;
};

/// The end of a run of filled cells around an edge, and the sum of the
/// cells' angles at it (below pi: the solid is convex there).
struct EdgeRun {
  EdgeAt end;
  double angle = 0;
};

/// The cells of one voxel space as polyhedra, built on first use, and the
/// walks between them. Cells outside the space count as empty.
class CellGeometry {
 public:
  /// `stretch` scales every cell's corners along the axes (kept, not
  /// copied: it must outlive the object).
  explicit CellGeometry(const Voxel& v, const float* stretch = nullptr);

  [[nodiscard]] const Voxel& voxel() const { return v_; }

  /// The polyhedron of a cell, or nullptr after a failure (see error()).
  [[nodiscard]] const CellPolyhedron* cell(CellAt c);

  /// Whether a cell is empty (or outside the space).
  [[nodiscard]] bool empty(CellAt c) const;

  /// From the face `start` through the cell behind it, across the next
  /// face and so on to the last filled cell before an empty one: that
  /// cell's face towards the empty one, with the index of the edge in
  /// it, and the sum of the cells' angles at the edge. None after a
  /// failure (see error()); `start` must be a face of a filled cell
  /// towards an empty one.
  [[nodiscard]] std::optional<EdgeRun> run(const EdgeAt& start);

  /// The neighbours of a cell of a kind (0: share a face, 1: two or more
  /// corners but no face, 2: one corner), from the corners the cells
  /// share, among the cells of the space within `reach` coordinates: what
  /// getNeighbor should list, in coordinate order. For kind 0 also the
  /// face of `c` shared with each, in the same order.
  [[nodiscard]] Result<std::vector<CellAt>> derivedNeighbours(CellAt c, unsigned int kind,
                                                              int reach = 7,
                                                              std::vector<int>* faces = nullptr);

  /// The first failure, if any.
  [[nodiscard]] const std::optional<Error>& error() const { return error_; }

 private:
  struct Hash {
    std::size_t operator()(const CellAt& c) const noexcept;
  };

  const Voxel& v_;
  const float* stretch_;
  std::unordered_map<CellAt, CellPolyhedron, Hash> cells_;
  std::optional<Error> error_;
};

}  // namespace bt::mesh
