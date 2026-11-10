// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Helpers for the tests of the derived movement checks (decision 0025).
/// The original has nothing to compare the take-apart of the rhombic,
/// tetra-octa, hexagonal and cairo grids with, so these give the answers
/// from the cells themselves: the cells of placed shapes as polyhedra,
/// moved and tested with the sweep test (mesh/cell_sweep.h) directly. They
/// use neither the check tables (solve/movement_checks.h), nor the movement
/// cache, nor the movement analysis: those are what they are the check of.

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cell_geometry.h"
#include "shapelogic/mesh/cell_sweep.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_checks.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bt::test {

using Cells = std::vector<mesh::CellAt>;
using Step = std::array<int, 3>;

/// The cells of a shape whose space has its origin at (ox, oy, oz).
inline Cells cellsOf(const Voxel& sh, int ox, int oy, int oz) {
  Cells out;
  for (unsigned int z = 0; z < sh.getZ(); z++) {
    for (unsigned int y = 0; y < sh.getY(); y++) {
      for (unsigned int x = 0; x < sh.getX(); x++) {
        if (!sh.isEmpty(x, y, z)) {
          out.push_back({static_cast<int>(x) + ox, static_cast<int>(y) + oy,
                         static_cast<int>(z) + oz});
        }
      }
    }
  }
  return out;
}

/// The cells of a grid as polyhedra, anywhere (negative coordinates too:
/// a cell is the cell at its place in the period box, moved).
class CellWorld {
 public:
  /// A cell in place: its polyhedron, its centre and how far its farthest
  /// corner is from that.
  struct Placed {
    mesh::CellPolyhedron poly;
    std::array<double, 3> centre{};
    double radius = 0;
  };

  explicit CellWorld(GridKind kind)
      : voxel_(GridType(kind).makeVoxel(1, 1, 1, Voxel::kEmpty)) {
    const std::optional<MovementDirections> moves = movementDirections(kind);
    REQUIRE(moves.has_value());
    period_ = moves->period;
    directions_ = moves->directions;

    // what a period along each axis is in the mesh's units, from a cell
    // and the same cell one period on
    mesh::CellAt first{};
    bool found = false;
    for (int z = 0; z < period_[2] && !found; z++) {
      for (int y = 0; y < period_[1] && !found; y++) {
        for (int x = 0; x < period_[0] && !found; x++) {
          if (voxel_->validCoordinate(x + 2 * period_[0], y + 2 * period_[1],
                                      z + 2 * period_[2])) {
            first = {x + 2 * period_[0], y + 2 * period_[1], z + 2 * period_[2]};
            found = true;
          }
        }
      }
    }
    REQUIRE(found);
    const std::array<double, 3> from = centreOf(first);
    for (std::size_t a = 0; a < 3; a++) {
      mesh::CellAt next = first;
      (a == 0 ? next.x : a == 1 ? next.y : next.z) += period_[a];
      const std::array<double, 3> to = centreOf(next);
      for (std::size_t k = 0; k < 3; k++) {
        axis_[a][k] = (to[k] - from[k]) / period_[a];
      }
    }
  }

  [[nodiscard]] const Voxel& voxel() const { return *voxel_; }
  [[nodiscard]] const Step& period() const { return period_; }
  /// The directions pieces move in on the grid (one step each).
  [[nodiscard]] const std::vector<Step>& directions() const { return directions_; }

  /// A move of the grid onto itself, in the mesh's units (the moves are a
  /// lattice, so this is linear in the coordinates).
  [[nodiscard]] std::array<double, 3> vectorOf(const Step& t) const {
    std::array<double, 3> v{};
    for (std::size_t a = 0; a < 3; a++) {
      for (std::size_t k = 0; k < 3; k++) {
        v[k] += axis_[a][k] * t[a];
      }
    }
    return v;
  }

  /// The cell at `c`.
  const Placed& cell(mesh::CellAt c) {
    auto it = cells_.find(c);
    if (it != cells_.end()) {
      return it->second;
    }
    // the same cell in the period box (two boxes from the origin: some
    // grids take no negative coordinates), then moved to its place
    const auto wrap = [](int v, int p) { return ((v % p) + p) % p + 2 * p; };
    const mesh::CellAt base{wrap(c.x, period_[0]), wrap(c.y, period_[1]), wrap(c.z, period_[2])};
    REQUIRE(voxel_->validCoordinate(base.x, base.y, base.z));
    const Result<mesh::CellPolyhedron> p = mesh::cellPolyhedron(*voxel_, base);
    REQUIRE(p.has_value());
    const std::array<double, 3> by = vectorOf({c.x - base.x, c.y - base.y, c.z - base.z});
    const auto moved = [&](const mesh::Vec3& v) {
      return mesh::Vec3{static_cast<float>(static_cast<double>(v.x) + by[0]),
                        static_cast<float>(static_cast<double>(v.y) + by[1]),
                        static_cast<float>(static_cast<double>(v.z) + by[2])};
    };
    Placed out;
    out.poly = *p;
    for (mesh::CellFace& f : out.poly.faces) {
      for (mesh::Vec3& corner : f.corners) {
        corner = moved(corner);
      }
    }
    for (mesh::Vec3& corner : out.poly.corners) {
      corner = moved(corner);
    }
    out.poly.centroid = moved(out.poly.centroid);
    out.centre = {static_cast<double>(out.poly.centroid.x),
                  static_cast<double>(out.poly.centroid.y),
                  static_cast<double>(out.poly.centroid.z)};
    for (const mesh::Vec3& corner : out.poly.corners) {
      const mesh::Vec3 d = corner - out.poly.centroid;
      out.radius = std::max(out.radius, std::sqrt(static_cast<double>(d.squaredModule())));
    }
    return cells_.emplace(c, std::move(out)).first->second;
  }

  /// Whether one of the cells `moving`, all moved `steps` times `step` in
  /// a straight line, passes through one of the cells `staying`.
  bool enters(const Cells& moving, const Step& step, int steps, const Cells& staying) {
    std::array<double, 3> m = vectorOf(step);
    for (double& v : m) {
      v *= steps;
    }
    const double length2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
    const mesh::Vec3 move{static_cast<float>(m[0]), static_cast<float>(m[1]),
                          static_cast<float>(m[2])};
    for (const mesh::CellAt& a : moving) {
      const Placed& pa = cell(a);
      for (const mesh::CellAt& b : staying) {
        const Placed& pb = cell(b);
        // nowhere near the way: no need to ask
        const std::array<double, 3> to{pb.centre[0] - pa.centre[0], pb.centre[1] - pa.centre[1],
                                       pb.centre[2] - pa.centre[2]};
        double t = length2 > 0 ? (to[0] * m[0] + to[1] * m[1] + to[2] * m[2]) / length2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        const std::array<double, 3> off{to[0] - t * m[0], to[1] - t * m[1], to[2] - t * m[2]};
        const double reach = pa.radius + pb.radius + 1e-3;
        if (off[0] * off[0] + off[1] * off[1] + off[2] * off[2] > reach * reach) {
          continue;
        }
        if (mesh::sweepEnters(pa.poly, move, pb.poly)) {
          return true;
        }
      }
    }
    return false;
  }

  /// A ball around cells: its centre and radius.
  struct Ball {
    std::array<double, 3> centre{};
    double radius = 0;
  };
  Ball ballAround(const Cells& cells) {
    Ball b;
    for (const mesh::CellAt& c : cells) {
      const Placed& p = cell(c);
      for (std::size_t k = 0; k < 3; k++) {
        b.centre[k] += p.centre[k] / static_cast<double>(cells.size());
      }
    }
    for (const mesh::CellAt& c : cells) {
      const Placed& p = cell(c);
      const double d = std::sqrt((p.centre[0] - b.centre[0]) * (p.centre[0] - b.centre[0]) +
                                 (p.centre[1] - b.centre[1]) * (p.centre[1] - b.centre[1]) +
                                 (p.centre[2] - b.centre[2]) * (p.centre[2] - b.centre[2]));
      b.radius = std::max(b.radius, d + p.radius);
    }
    return b;
  }

 private:
  std::array<double, 3> centreOf(mesh::CellAt c) const {
    const Result<mesh::CellPolyhedron> p = mesh::cellPolyhedron(*voxel_, c);
    REQUIRE(p.has_value());
    return {static_cast<double>(p->centroid.x), static_cast<double>(p->centroid.y),
            static_cast<double>(p->centroid.z)};
  }

  std::unique_ptr<Voxel> voxel_;
  Step period_{1, 1, 1};
  std::vector<Step> directions_;
  /// The mesh vector of one coordinate along each axis.
  std::array<std::array<double, 3>, 3> axis_{};
  std::map<mesh::CellAt, Placed> cells_;
};

/// What MovementCache answers with when nothing is ever in the way.
inline constexpr unsigned int kFreeForGood = 32000;

/// How many steps the cells `moving` can go along `step` before they would
/// pass through one of `staying`; kFreeForGood when they never do.
inline unsigned int freeSteps(CellWorld& w, const Cells& moving, const Step& step,
                              const Cells& staying) {
  const CellWorld::Ball still = w.ballAround(staying);
  const CellWorld::Ball start = w.ballAround(moving);
  const std::array<double, 3> m = w.vectorOf(step);

  Cells at = moving;
  for (unsigned int n = 0; n < 1000; n++) {
    if (w.enters(at, step, 1, staying)) {
      return n;
    }
    for (mesh::CellAt& c : at) {
      c.x += step[0];
      c.y += step[1];
      c.z += step[2];
    }
    // apart, and going further apart: nothing can be met any more
    const std::array<double, 3> gap{start.centre[0] + (n + 1) * m[0] - still.centre[0],
                                    start.centre[1] + (n + 1) * m[1] - still.centre[1],
                                    start.centre[2] + (n + 1) * m[2] - still.centre[2]};
    const double far = start.radius + still.radius;
    if (gap[0] * gap[0] + gap[1] * gap[1] + gap[2] * gap[2] > far * far &&
        gap[0] * m[0] + gap[1] * m[1] + gap[2] * m[2] >= 0) {
      return kFreeForGood;
    }
  }
  FAIL("a move that never ends: " << moving.size() << " cells along " << step[0] << " " << step[1]
                                  << " " << step[2] << " against " << staying.size() << ", first at " << moving[0].x << " " << moving[0].y << " " << moving[0].z << " and " << staying[0].x << " " << staying[0].y << " " << staying[0].z);
  return 0;
}

namespace detail {

/// The cells of each piece of an assembly (by piece number; empty for a
/// piece not placed) when its hotspot is at the origin.
inline std::vector<Cells> pieceCells(const Puzzle& puzzle, const Problem& problem,
                                     const Assembly& assembly) {
  std::vector<Cells> out(assembly.placementCount());
  for (unsigned int i = 0; i < assembly.placementCount(); i++) {
    if (!assembly.isPlaced(i)) {
      continue;
    }
    std::unique_ptr<Voxel> pc = problem.partShape(puzzle, problem.getPartIdToPieceId(i)).clone();
    REQUIRE(pc->transform(assembly.getTransformation(i)));
    out[i] = cellsOf(*pc, -pc->getHx(), -pc->getHy(), -pc->getHz());
  }
  return out;
}

inline Cells placedAt(const Cells& cells, const State& s, unsigned int i) {
  Cells out = cells;
  for (mesh::CellAt& c : out) {
    c.x += s.getX(i);
    c.y += s.getY(i);
    c.z += s.getZ(i);
  }
  return out;
}

inline std::optional<std::string> replayNode(CellWorld& w, const std::vector<Cells>& shapes,
                                             const Assembly& assembly, const Separation& node,
                                             const std::string& where, bool groups) {
  const unsigned int n = node.getPieceNumber();
  if (n < 2 || node.getNumStates() < 2) {
    return where + ": a separation of fewer than two pieces or states";
  }
  const auto fail = [&](unsigned int state, const std::string& what) {
    return std::optional<std::string>(where + " state " + std::to_string(state) + ": " + what);
  };

  // nothing shares a cell at the start, nothing has turned, nothing is gone
  {
    const State& s = node.getState(0);
    std::set<mesh::CellAt> filled;
    for (unsigned int i = 0; i < n; i++) {
      const unsigned int piece = node.getPieceName(i);
      if (piece >= shapes.size() || shapes[piece].empty()) {
        return fail(0, "a piece the assembly does not place");
      }
      if (s.pieceRemoved(i)) {
        return fail(0, "a piece removed at the start");
      }
      for (const mesh::CellAt& c : placedAt(shapes[piece], s, i)) {
        if (!w.voxel().validCoordinate(((c.x % w.period()[0]) + w.period()[0]) % w.period()[0],
                                       ((c.y % w.period()[1]) + w.period()[1]) % w.period()[1],
                                       ((c.z % w.period()[2]) + w.period()[2]) % w.period()[2])) {
          return fail(0, "a piece off the grid");
        }
        if (!filled.insert(c).second) {
          return fail(0, "two pieces in one cell");
        }
      }
    }
  }

  const unsigned int last = node.getNumStates() - 1;
  std::vector<bool> removed(n, false);
  for (unsigned int k = 0; k < last; k++) {
    const State& a = node.getState(k);
    const State& b = node.getState(k + 1);
    const bool removal = k + 1 == last;

    std::optional<Step> delta;
    std::vector<bool> moves(n, false);
    for (unsigned int i = 0; i < n; i++) {
      if (b.hasOrientations() &&
          b.getOrient(i) != assembly.getTransformation(node.getPieceName(i))) {
        return fail(k + 1, "a piece turned");
      }
      if (b.pieceRemoved(i) != (removal && (b.getX(i) != a.getX(i) || b.getY(i) != a.getY(i) ||
                                            b.getZ(i) != a.getZ(i)))) {
        return fail(k + 1, "a piece removed before the end, or moved in the removal");
      }
      const Step d{b.getX(i) - a.getX(i), b.getY(i) - a.getY(i), b.getZ(i) - a.getZ(i)};
      if (d == Step{0, 0, 0}) {
        continue;
      }
      moves[i] = true;
      if (delta && *delta != d) {
        return fail(k + 1, "pieces moved different ways");
      }
      delta = d;
    }
    if (!delta) {
      return fail(k + 1, "no piece moved");
    }
    if (std::ranges::all_of(moves, [](bool m) { return m; })) {
      return fail(k + 1, "every piece moved");
    }

    // the move is whole steps in one of the grid's directions; a removal
    // is 20000 of them (Disassembler::checkSubproblems), which here goes
    // as far as it takes to be clear of everything
    std::optional<Step> step;
    int steps = 0;
    for (const Step& dir : w.directions()) {
      for (const int sign : {1, -1}) {
        int count = 0;
        bool whole = true;
        for (std::size_t c = 0; c < 3; c++) {
          const int unit = sign * dir[c];
          if (unit == 0) {
            whole = whole && (*delta)[c] == 0;
          } else if ((*delta)[c] % unit != 0 || (*delta)[c] / unit <= 0 ||
                     (count != 0 && count != (*delta)[c] / unit)) {
            whole = false;
          } else {
            count = (*delta)[c] / unit;
          }
        }
        if (whole && count > 0) {
          step = Step{sign * dir[0], sign * dir[1], sign * dir[2]};
          steps = count;
        }
      }
    }
    if (!step) {
      return fail(k + 1, "a move in no direction of the grid");
    }
    if (removal != (steps == 20000)) {
      return fail(k + 1, "a removal that is not 20000 steps, or a move that is");
    }

    Cells moving;
    Cells staying;
    for (unsigned int i = 0; i < n; i++) {
      const Cells cells = placedAt(shapes[node.getPieceName(i)], a, i);
      (moves[i] ? moving : staying).insert((moves[i] ? moving : staying).end(), cells.begin(),
                                           cells.end());
    }
    if (removal) {
      const CellWorld::Ball going = w.ballAround(moving);
      const CellWorld::Ball still = w.ballAround(staying);
      const std::array<double, 3> v = w.vectorOf(*step);
      const double stepLength = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      const double between =
          std::sqrt((going.centre[0] - still.centre[0]) * (going.centre[0] - still.centre[0]) +
                    (going.centre[1] - still.centre[1]) * (going.centre[1] - still.centre[1]) +
                    (going.centre[2] - still.centre[2]) * (going.centre[2] - still.centre[2]));
      steps = static_cast<int>((between + going.radius + still.radius) / stepLength) + 2;
      removed = moves;
    }
    if (w.enters(moving, *step, steps, staying)) {
      return fail(k + 1, "a piece moved through another");
    }
  }
  if (std::ranges::none_of(removed, [](bool r) { return r; })) {
    return fail(last, "nothing removed at the end");
  }

  // the two parts: each a piece, or taken apart further from where the
  // pieces were before the removal
  const State& before = node.getState(last - 1);
  for (const bool gone : {true, false}) {
    std::vector<unsigned int> part;
    for (unsigned int i = 0; i < n; i++) {
      if (removed[i] == gone) {
        part.push_back(i);
      }
    }
    const Separation* sub = gone ? node.getRemoved() : node.getLeft();
    const std::string name = where + (gone ? "r" : "l");
    if (sub == nullptr) {
      if (part.size() != 1 && !groups) {
        return name + ": several pieces left together";
      }
      continue;
    }
    if (sub->getPieceNumber() != part.size()) {
      return name + ": not the pieces of its part";
    }
    for (unsigned int j = 0; j < part.size(); j++) {
      const unsigned int i = part[j];
      const State& first = sub->getState(0);
      if (sub->getPieceName(j) != node.getPieceName(i) || first.getX(j) != before.getX(i) ||
          first.getY(j) != before.getY(i) || first.getZ(j) != before.getZ(i)) {
        return name + ": does not start where its pieces were";
      }
    }
    if (std::optional<std::string> err = replayNode(w, shapes, assembly, *sub, name, groups)) {
      return err;
    }
  }
  return std::nullopt;
}

}  // namespace detail

/// Plays a take-apart of `assembly` back on the cells themselves: it
/// starts at the assembly, every move is whole steps in a direction of the
/// grid by some of the pieces together, no cell of a moved piece passes
/// through a cell of a piece that stays, every separation ends with a
/// part pulled clear of the rest, and the parts are taken apart on from
/// where they were, down to single pieces (with `groups`, a part may
/// stay together: the problem's piece groups are not looked at). What is
/// wrong with it, or nothing. For take-aparts without turns (the cube
/// grid's 90 degree turns).
inline std::optional<std::string> replayError(const Puzzle& puzzle, const Problem& problem,
                                              const Assembly& assembly, const Separation& root,
                                              bool groups = false) {
  CellWorld w(puzzle.getGridType().getType());
  const std::vector<Cells> shapes = detail::pieceCells(puzzle, problem, assembly);

  unsigned int placed = 0;
  for (unsigned int i = 0; i < assembly.placementCount(); i++) {
    placed += assembly.isPlaced(i) ? 1 : 0;
  }
  if (root.getPieceNumber() != placed) {
    return std::string("not every placed piece is taken apart");
  }
  const State& first = root.getState(0);
  for (unsigned int i = 0; i < root.getPieceNumber(); i++) {
    const unsigned int piece = root.getPieceName(i);
    if (piece >= assembly.placementCount() || !assembly.isPlaced(piece) ||
        first.getX(i) != assembly.getX(piece) || first.getY(i) != assembly.getY(piece) ||
        first.getZ(i) != assembly.getZ(piece)) {
      return std::string("does not start at the assembly");
    }
  }
  return detail::replayNode(w, shapes, assembly, root, "root", groups);
}

/// How far one piece can go against another: the two pieces (numbers of
/// the assembly), the second's position less the first's, a direction of
/// the grid (its number) and whether the first goes against it. The free
/// steps of the first, kFreeForGood when the second is never in its way.
using FreeFn = std::function<unsigned int(unsigned int, unsigned int, const Step&, unsigned int,
                                          bool)>;

/// A take-apart search written for the tests, to hold the movement
/// analysis and the disassembler against: breadth first over the
/// positions of the pieces, a move being any set of pieces that goes some
/// steps in one direction together without one of them meeting a piece
/// that stays. It knows none of the analysis's ways (no matrices, no
/// pushing from one piece, no merging of moves, no heavier side), only
/// what two pieces allow each other (FreeFn). For up to 16 pieces, and
/// without piece groups. Pieces that lie loose have more positions than
/// such a search can go through: one that meets more than its budget
/// gives up, and says so (gaveUp).
class ReferenceTakeApart {
 public:
  /// Pieces (numbers of the assembly) and where each is.
  struct Part {
    std::vector<unsigned int> pieces;
    std::vector<Step> at;
  };

  ReferenceTakeApart(std::size_t directions, FreeFn free)
      : directions_(directions), free_(std::move(free)) {}

  /// The directions as steps, for the positions after a move.
  void setSteps(std::vector<Step> steps) { steps_ = std::move(steps); }

  /// The positions one question may go through before the search gives
  /// up.
  void setBudget(std::size_t positions) { budget_ = positions; }

  /// True when the last question was given up: its answer says nothing.
  [[nodiscard]] bool gaveUp() const { return gaveUp_; }

  /// The fewest moves after which the part is in two, the move that pulls
  /// them apart counted (what Separation::getMoves() says of the node of
  /// these pieces); none when it never is, or is not within `atMost`
  /// moves (0: however many).
  std::optional<unsigned int> movesToSeparate(const Part& part, unsigned int atMost = 0) {
    std::optional<unsigned int> out;
    left_ = budget_;
    gaveUp_ = !search(part, atMost, [&](unsigned int moves, const Part&, const Part&) {
      out = moves;
      return true;
    });
    return out;
  }

  /// Whether the part comes apart into single pieces some way. As in the
  /// disassembler, a position that falls in two is taken apart there (no
  /// piece that could be taken off is slid about first: it could be for
  /// ever). The disassembler takes the first such position it meets and
  /// the first way that falls in two, and gives up when a part of that
  /// is stuck; this tries them all, so it may say yes where the
  /// disassembler says no, never the other way round.
  bool comesApart(const Part& part) {
    left_ = budget_;
    gaveUp_ = false;
    return apart(part);
  }

  /// The positions searched so far (to tell that a test asked something).
  [[nodiscard]] std::size_t positions() const { return positions_; }

 private:
  static std::vector<Step> relative(const std::vector<Step>& at) {
    std::vector<Step> out = at;
    for (Step& s : out) {
      for (std::size_t c = 0; c < 3; c++) {
        s[c] -= at[0][c];
      }
    }
    return out;
  }

  bool apart(const Part& part) {
    if (part.pieces.size() < 2) {
      return true;
    }
    const std::pair<std::vector<unsigned int>, std::vector<Step>> key{part.pieces,
                                                                      relative(part.at)};
    if (const auto it = apart_.find(key); it != apart_.end()) {
      return it->second;
    }
    bool ok = false;
    const bool through = search(part, 0, [&](unsigned int, const Part& gone, const Part& left) {
      ok = apart(gone) && apart(left);
      return ok || gaveUp_;
    });
    if (!through || gaveUp_) {
      gaveUp_ = true;
      return false;
    }
    apart_[key] = ok;
    return ok;
  }

  unsigned int freeOf(unsigned int a, unsigned int b, const Step& between, unsigned int dir,
                      bool against) {
    const auto key = std::make_tuple(a, b, between, dir, against);
    const auto it = known_.find(key);
    if (it != known_.end()) {
      return it->second;
    }
    const unsigned int v = free_(a, b, between, dir, against);
    known_.emplace(key, v);
    return v;
  }

  // Breadth first from `start`, `atMost` moves deep (0: all the way);
  // onSeparation(moves, gone, left) for every way a position falls in
  // two, until it says true. False when the budget ran out.
  template <class OnSeparation>
  bool search(const Part& start, unsigned int atMost, OnSeparation onSeparation) {
    const std::size_t n = start.pieces.size();
    REQUIRE(n >= 2);
    REQUIRE(n <= 16);
    REQUIRE(steps_.size() == directions_);
    const std::uint32_t all = (std::uint32_t{1} << n) - 1;

    std::map<std::vector<Step>, unsigned int> depth{{relative(start.at), 0U}};
    std::vector<std::vector<Step>> order{relative(start.at)};
    std::vector<unsigned int> free(n * n);
    std::vector<std::uint32_t> pushed(std::size_t{1} << n);

    // the sets of pieces that can go `steps` together: each takes along
    // every piece it would meet on the way, and none is left to meet
    const auto movable = [&](unsigned int steps) {
      std::vector<std::uint32_t> out;
      pushed[0] = 0;
      for (std::uint32_t mask = 1; mask <= all; mask++) {
        const auto i = static_cast<std::size_t>(std::countr_zero(mask));
        std::uint32_t takes = 0;
        for (std::size_t j = 0; j < n; j++) {
          if (j != i && free[i * n + j] < steps) {
            takes |= std::uint32_t{1} << j;
          }
        }
        pushed[mask] = pushed[mask & (mask - 1)] | takes;
        if (mask != all && (pushed[mask] & ~mask) == 0) {
          out.push_back(mask);
        }
      }
      return out;
    };

    for (std::size_t next = 0; next < order.size(); next++) {
      const std::vector<Step> at = order[next];
      const unsigned int moves = depth.at(at);
      if (atMost != 0 && moves >= atMost) {
        break;
      }
      if (left_ == 0) {
        return false;
      }
      left_--;
      positions_++;

      bool falls = false;
      std::vector<std::vector<Step>> slides;
      for (unsigned int dir = 0; dir < directions_; dir++) {
        for (const bool against : {false, true}) {
          unsigned int longest = 0;
          for (std::size_t i = 0; i < n; i++) {
            for (std::size_t j = 0; j < n; j++) {
              if (i == j) {
                continue;
              }
              const Step between{at[j][0] - at[i][0], at[j][1] - at[i][1], at[j][2] - at[i][2]};
              const unsigned int v =
                  freeOf(start.pieces[i], start.pieces[j], between, dir, against);
              free[i * n + j] = v;
              if (v < kFreeForGood) {
                longest = std::max(longest, v);
              }
            }
          }

          for (const std::uint32_t mask : movable(kFreeForGood)) {
            falls = true;
            Part gone;
            Part left;
            for (std::size_t i = 0; i < n; i++) {
              Part& to = ((mask >> i) & 1U) != 0 ? gone : left;
              to.pieces.push_back(start.pieces[i]);
              to.at.push_back(at[i]);
            }
            if (onSeparation(moves + 1, gone, left)) {
              return true;
            }
          }

          for (unsigned int steps = 1; steps <= longest; steps++) {
            const int by = against ? -static_cast<int>(steps) : static_cast<int>(steps);
            for (const std::uint32_t mask : movable(steps)) {
              std::vector<Step> to = at;
              for (std::size_t i = 0; i < n; i++) {
                if (((mask >> i) & 1U) != 0) {
                  for (std::size_t c = 0; c < 3; c++) {
                    to[i][c] += by * steps_[dir][c];
                  }
                }
              }
              slides.push_back(relative(to));
            }
          }
        }
      }
      if (falls) {
        continue;
      }
      for (std::vector<Step>& to : slides) {
        if (depth.emplace(to, moves + 1).second) {
          order.push_back(std::move(to));
        }
      }
    }
    return true;
  }

  std::size_t directions_;
  FreeFn free_;
  std::vector<Step> steps_;
  std::map<std::tuple<unsigned int, unsigned int, Step, unsigned int, bool>, unsigned int> known_;
  std::map<std::pair<std::vector<unsigned int>, std::vector<Step>>, bool> apart_;
  std::size_t positions_ = 0;
  std::size_t budget_ = 5000;
  std::size_t left_ = 0;
  bool gaveUp_ = false;
};

/// The cells of each piece of an assembly where the assembly has them
/// (by piece number; empty for a piece not placed).
inline std::vector<Cells> placedCells(const Puzzle& puzzle, const Problem& problem,
                                      const Assembly& assembly) {
  std::vector<Cells> out = detail::pieceCells(puzzle, problem, assembly);
  for (unsigned int i = 0; i < assembly.placementCount(); i++) {
    for (mesh::CellAt& c : out[i]) {
      c.x += assembly.getX(i);
      c.y += assembly.getY(i);
      c.z += assembly.getZ(i);
    }
  }
  return out;
}

/// A reference take-apart that asks the cells themselves (freeSteps):
/// `cells` are the cells of each piece where the assembly has it
/// (placedCells; `assembly` must outlive the search).
inline ReferenceTakeApart referenceOnCells(CellWorld& w, const std::vector<Cells>& cells,
                                           const Assembly& assembly) {
  ReferenceTakeApart ref(
      w.directions().size(),
      [&w, &cells, &assembly](unsigned int a, unsigned int b, const Step& between,
                              unsigned int dir, bool against) {
        // the first stays where the assembly has it; the second goes to
        // where it is as seen from the first
        const Step by{between[0] - (assembly.getX(b) - assembly.getX(a)),
                      between[1] - (assembly.getY(b) - assembly.getY(a)),
                      between[2] - (assembly.getZ(b) - assembly.getZ(a))};
        Cells other = cells[b];
        for (mesh::CellAt& c : other) {
          c.x += by[0];
          c.y += by[1];
          c.z += by[2];
        }
        const Step& d = w.directions()[dir];
        const Step step = against ? Step{-d[0], -d[1], -d[2]} : d;
        return freeSteps(w, cells[a], step, other);
      });
  ref.setSteps(w.directions());
  return ref;
}

/// The whole of an assembly as a part: its placed pieces where they are.
inline ReferenceTakeApart::Part partOf(const Assembly& assembly) {
  ReferenceTakeApart::Part part;
  for (unsigned int i = 0; i < assembly.placementCount(); i++) {
    if (assembly.isPlaced(i)) {
      part.pieces.push_back(i);
      part.at.push_back({assembly.getX(i), assembly.getY(i), assembly.getZ(i)});
    }
  }
  return part;
}

/// The pieces of a node of a separation tree where its first state has
/// them.
inline ReferenceTakeApart::Part partOf(const Separation& node) {
  ReferenceTakeApart::Part part;
  const State& s = node.getState(0);
  for (unsigned int i = 0; i < node.getPieceNumber(); i++) {
    part.pieces.push_back(node.getPieceName(i));
    part.at.push_back({s.getX(i), s.getY(i), s.getZ(i)});
  }
  return part;
}

/// Every node of a separation tree, the root first.
inline void eachNode(const Separation& node, const std::function<void(const Separation&)>& f) {
  f(node);
  if (node.getRemoved() != nullptr) {
    eachNode(*node.getRemoved(), f);
  }
  if (node.getLeft() != nullptr) {
    eachNode(*node.getLeft(), f);
  }
}

/// The cells of a grid within a box of the given size: every position
/// that is a cell.
inline Cells cellsInBox(const Voxel& grid, int sx, int sy, int sz) {
  Cells out;
  for (int z = 0; z < sz; z++) {
    for (int y = 0; y < sy; y++) {
      for (int x = 0; x < sx; x++) {
        if (grid.validCoordinate(x, y, z)) {
          out.push_back({x, y, z});
        }
      }
    }
  }
  return out;
}

/// A shape of the puzzle's grid with the given cells filled (coordinates
/// within the box of the given size); its number in the puzzle.
inline unsigned int addShapeOf(Puzzle& puzzle, int sx, int sy, int sz, const Cells& cells) {
  const unsigned int id =
      puzzle.addShape(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy),
                      static_cast<unsigned int>(sz));
  for (const mesh::CellAt& c : cells) {
    REQUIRE(puzzle.getShape(id).validCoordinate(c.x, c.y, c.z));
    puzzle.getShape(id).setState(static_cast<unsigned int>(c.x), static_cast<unsigned int>(c.y),
                                 static_cast<unsigned int>(c.z), Voxel::kFilled);
  }
  return id;
}

}  // namespace bt::test
