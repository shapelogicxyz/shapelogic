// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The tilted tray of a Tilted Square puzzle (decision 0045): a tray of
/// straight unit squares, `width` by `height` of them (a square, or since
/// stage E any rectangle), some of them walls, with the puzzle's square grid
/// turned inside it. From the tray's settings this works out which
/// cells of the turned grid are whole, and where the grid sits to keep the
/// most of them. New code; the geometry is that of the research page
/// "Tilted Square Geometry" (site/public/research/tilted-square-geometry.html).
///
/// Two frames are used. The tray's: its squares are [x, x+1] x [y, y+1] for
/// x in 0 .. width-1, y in 0 .. height-1. The grid's: cell (i, j) is the unit square at (i, j).
/// A grid point g lies at shift + R(g) in the tray, R the turn by
/// atan2(down, over).

#include "shapelogic/base/stop.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace bt::tilted {

/// The settings of a tilted tray. A plain value.
struct Tray {
  /// The largest tray side, in cells.
  static constexpr unsigned int kMaxSize = 64;
  /// The largest `over` (and so `down`) of a slope.
  static constexpr unsigned int kMaxSlope = 64;
  /// The largest allowance, in cell widths.
  static constexpr double kMaxAllowance = 0.25;
  /// The farthest a shift may lie from zero, in x and in y. Every board has
  /// a shift within one cell of zero (canonicalShift()); the bound keeps a
  /// file's number from being too large to work with.
  static constexpr double kMaxShift = 1024.0;

  /// How the grid is placed in the tray.
  enum class Placement : std::uint8_t {
    /// Where the most cells are whole (bestShift()).
    Best,
    /// Where the user put it.
    Custom,
  };

  /// The tray's sides, in cells: 1 .. kMaxSize each.
  unsigned int width = 8;
  unsigned int height = 8;
  /// The slope down/over, in lowest terms, 0 <= down <= over: "1 down, n
  /// over" is down 1, over n; a straight tray is down 0, over 1.
  unsigned int over = 1;
  unsigned int down = 0;
  /// How far a cell may reach past the tray's edge or into a wall, in cell
  /// widths: 0 (strict) .. kMaxAllowance.
  double allowance = 0.0;
  Placement placement = Placement::Best;
  /// The shift in use, in the tray's frame: where grid point (0, 0) lies,
  /// within kMaxShift of zero. Kept for either placement, so a file opens
  /// to the same board.
  double shiftX = 0.0;
  double shiftY = 0.0;
  /// The walls: width * height squares, row by row from y = 0 (index
  /// y * width + x), nonzero a wall; or empty, no walls.
  std::vector<std::uint8_t> walls;

  /// True when square (x, y) of the tray is a wall.
  [[nodiscard]] bool isWall(unsigned int x, unsigned int y) const {
    return !walls.empty() && walls[(y * width) + x] != 0;
  }
  /// True when every setting is in range, the slope is in lowest terms and
  /// the walls are none or one a square.
  [[nodiscard]] bool valid() const;
  /// True when the tray is as wide as it is high.
  [[nodiscard]] bool square() const { return width == height; }
  /// The turn of the grid in the tray, in degrees: atan2(down, over).
  [[nodiscard]] double degrees() const;

  bool operator==(const Tray&) const = default;
};

/// `tray` made `width` by `height` (1 .. Tray::kMaxSize each): the walls
/// still inside the tray stay where they are, the others go.
[[nodiscard]] Tray withSize(const Tray& tray, unsigned int width, unsigned int height);

/// What a cell of the board's box is to the tray.
enum class Cell : std::uint8_t {
  /// It does not reach into the tray.
  Outside,
  /// The tray's edge or a wall cuts it: no piece can lie there.
  Cut,
  /// Usable: every point of it is within the allowance of an open square.
  Whole,
};

/// A point, in whichever frame its holder says.
struct Point {
  double x = 0.0;
  double y = 0.0;
};

/// The board a tray gives at its shift: the box of every cell that reaches
/// into the tray, each cell's state, and the tray drawn in the box's frame
/// (cell (x, y) of the box is the unit square at (x, y)).
struct Layout {
  unsigned int sizeX = 0;
  unsigned int sizeY = 0;
  /// sizeX * sizeY states, row by row from y = 0.
  std::vector<Cell> cells;
  /// The tray's corners (0, 0), (width, 0), (width, height), (0, height) in
  /// the box's frame.
  std::array<Point, 4> corners{};
  /// Each wall square's corners in the box's frame, in the walls' order.
  std::vector<std::array<Point, 4>> walls;
  /// The counts of whole and cut cells.
  unsigned int whole = 0;
  unsigned int cut = 0;

  [[nodiscard]] Cell at(unsigned int x, unsigned int y) const { return cells[y * sizeX + x]; }
};

/// The board of `tray` (which must be valid()) at its shift.
[[nodiscard]] Layout layout(const Tray& tray);

/// A placement of the grid and the whole cells it keeps.
struct Placed {
  double shiftX = 0.0;
  double shiftY = 0.0;
  unsigned int whole = 0;
};

/// The shift that keeps the most whole cells of `tray` (valid(); its own
/// shift and placement are not looked at). Exact: the count changes only
/// where a cell's corner meets an edge of the tray or of a wall, or a
/// wall's corner meets a cell's edge, and every crossing of two such lines
/// is tried. Among equal counts a placement that is symmetric about the
/// tray's center is taken when there is one, and the answer is the same
/// each time it is asked. The shift is the one of its kind nearest grid
/// point (0, 0) (canonicalShift()).
[[nodiscard]] Placed bestShift(const Tray& tray);

/// `shift` moved by whole grid steps to the place where the tray's corner
/// (0, 0) lies in grid cell (0, 0): every shift that gives the same board
/// has one such form.
[[nodiscard]] Point canonicalShift(const Tray& tray, Point shift);

/// How long placements() may look. A tray of ordinary size is far below
/// both; a large one at a steep slope with many walls is cut short.
struct PlacementLimits {
  /// The most distinct boards kept while looking.
  std::size_t maxBoards = 4096;
  /// The most looking: cells looked at along the lines swept (20 million
  /// are about a second).
  std::uint64_t maxWork = 20'000'000;
};

/// Every board a tray can give that is worth trying (placements()).
struct Placements {
  /// The boards, the one with the most whole cells first (among equal
  /// counts the order is fixed, the same each time): each with a shift that
  /// gives it, in its canonical form.
  std::vector<Placed> boards;
  /// False when the looking was cut short (PlacementLimits, or `stop`):
  /// boards may be missing.
  bool complete = true;
  /// The board the tray's own shift gives, when it is one of `boards` (as
  /// it lies, or turned).
  std::optional<std::size_t> inUse;
};

/// The boards of `tray` (valid(); its placement is not looked at) over every
/// place the grid can lie, each board once.
///
/// As the grid slides, a cell stays whole up to and on the lines where its
/// corner meets an edge of the tray or of a wall, or a wall's corner meets
/// its edge. So the cells whole at any shift are all whole at some point of
/// one of those lines, at the end of a stretch on which nothing changes:
/// each line is swept and the board of each such stretch taken. Two boards
/// are one when their cells are the same, moved by whole cells or turned by
/// quarter turns (a piece can be turned as well), and a board that is part
/// of another in that sense is left out: the pieces need only fit, so it
/// holds nothing the larger one cannot. A mirrored board is kept: not every
/// problem lets its pieces be turned over.
///
/// The best placement (bestShift()) is among the boards, with its shift.
/// Exact up to `limits`; `stop` cuts it short too.
[[nodiscard]] Placements placements(const Tray& tray, const StopToken& stop = {},
                                    const PlacementLimits& limits = {});

}  // namespace bt::tilted
