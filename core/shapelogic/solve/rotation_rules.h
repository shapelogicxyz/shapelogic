// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Whether a 90 degree turn of a piece (or of a group of pieces moving as
/// one) is possible during a take-apart. The rules:
///
/// - the turned cells must not overlap other pieces;
/// - same-layer sandwich (before and after the turn): a moving cell must
///   not have other pieces' cells on both opposite sides in the plane of
///   the turn, on its layer;
/// - perpendicular-plane capture: on a slice of constant U (or V) with at
///   least two moving and two other cells, other cells on opposite sides in
///   V (or U), at the same coordinate along the axis, refuse the axis;
/// - along the axis, on each layer where the moving body touches another
///   piece face to face, look at the moving cells on the pivot's column
///   only and collect the other pieces' cells beside them in the plane: one
///   such layer with a neighbour on -U and another with one on +U (or the
///   same for V) refuse the turn;
/// - arc sweep: the turn is followed in 24 steps with bevelled squares
///   (0.0405 cut off, as Andrew Crowell's Fortran solver), and where it
///   clips, with small in-plane wiggles half way.
///
/// The rules give the same answer however the whole puzzle is turned in
/// space (the tests check it); the Classic generator relies on that.
/// Ported from src/lib/rotationrules.{h,cpp}; its BT_ROT_DEBUG and
/// BT_ROT_DUMP output is gone (core/ does not print).

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace bt {

/// The rules for 90 degree turns. An object belongs to one thread: it keeps
/// what it worked out for the cells it was last asked about.
class RotationRules {
 public:
  /// A cell of the cube grid.
  struct Cell {
    int x = 0, y = 0, z = 0;
    Cell() = default;
    Cell(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
  };

  /// The centre of a turn in doubled cell units: the centre of cell (x, y,
  /// z) is (2x, 2y, 2z); an odd coordinate is a face, edge or corner.
  struct Pivot {
    int hx = 0, hy = 0, hz = 0;
    Pivot() = default;
    Pivot(int hx_, int hy_, int hz_) : hx(hx_), hy(hy_), hz(hz_) {}
  };

  RotationRules();
  ~RotationRules();

  RotationRules(const RotationRules&) = delete;
  RotationRules& operator=(const RotationRules&) = delete;

  /// True when the body made of `startCells` can turn into `endCells` about
  /// `pivot`, around `axis` (0 x, 1 y, 2 z) in `sense` (0 is +90 degrees, 1
  /// is -90), among the other pieces' cells `occupied`.
  [[nodiscard]] bool allowRotation(const std::vector<Cell>& occupied,
                                   const std::vector<Cell>& startCells,
                                   const std::vector<Cell>& endCells, const Pivot& pivot,
                                   unsigned int axis, unsigned int sense) const;

  /// True when the sandwich or the perpendicular-plane rule refuses every
  /// turn of the body around `axis`, whatever the pivot.
  [[nodiscard]] bool axisBlocked(const std::vector<Cell>& occupied,
                                 const std::vector<Cell>& startCells, unsigned int axis) const;

  /// The same test for a generator that tries many pivots, axes and senses
  /// of one body among the same others: setBodies() once, then
  /// preparedAxisBlocked() once per axis and allowPrepared() per turn
  /// (axisClear when preparedAxisBlocked said false for the axis). What
  /// does not depend on the pivot is worked out once; the answers are
  /// allowRotation's.
  void setBodies(const std::vector<Cell>& occupied, const std::vector<Cell>& startCells);
  [[nodiscard]] bool preparedAxisBlocked(unsigned int axis) const;
  [[nodiscard]] bool allowPrepared(const Pivot& pivot, unsigned int axis, unsigned int sense,
                                   bool axisClear) const;

  /// `cell` after a 90 degree turn about `pivot`; false when it would not
  /// land on the grid (the pivot's coordinates in the plane differ in
  /// parity).
  static bool rotateCell(const Cell& cell, const Pivot& pivot, unsigned int axis,
                         unsigned int sense, Cell& out);

  /// The cells a picture of one turn shows (the GUI's "Debug Rotations"):
  /// other pieces' cells that break a rule (`outBlocking`), cells on the
  /// way of the turn that are empty and would block if filled
  /// (`outClearance`), and empty cells beside a moving cell, in the plane,
  /// on a layer that touches another piece (`outRestricted`).
  void collectDebugConflictCells(const std::vector<Cell>& occupied,
                                 const std::vector<Cell>& startCells, const Pivot& pivot,
                                 unsigned int axis, unsigned int sense,
                                 std::vector<Cell>& outBlocking, std::vector<Cell>& outClearance,
                                 std::vector<Cell>& outRestricted) const;

  // Defined in rotation_rules.cpp.
  struct OccupancyCache;
  struct Prepared;
  struct ArcSweep;

 private:
  /// The occupancy of the last `occupied` allowRotation() was asked about,
  /// which a generator passes again for every turn it tries.
  mutable std::unique_ptr<OccupancyCache> occCache_;
  /// The bodies of setBodies() and what was worked out for them.
  mutable std::unique_ptr<Prepared> prep_;
  /// The arc sweep's work space, so that a sweep allocates nothing (the
  /// original kept it per thread).
  mutable std::unique_ptr<ArcSweep> sweep_;
};

}  // namespace bt
