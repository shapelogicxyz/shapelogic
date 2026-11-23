// SPDX-License-Identifier: GPL-3.0-or-later
// New code (decision 0045); the counting rule and the sweep over the cells'
// allowed shifts follow the research page "Tilted Square Geometry".

#include "shapelogic/model/tilted_tray.h"

#include "shapelogic/base/assert.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <utility>

namespace bt::tilted {

namespace {

/// A cell that reaches less than this far into a wall or past the edge is
/// whole: lengths that are equal on paper differ by rounding.
constexpr double kEps = 1e-9;
/// Two lines closer than this are one.
constexpr double kSame = 1e-12;
/// Every shift has a copy, moved by whole grid steps, within this of zero
/// in x and in y (a point is at most 0.71 from a grid point).
constexpr double kWindow = 0.75;

/// A rectangle of the tray's frame.
struct Rect {
  double x0 = 0.0;
  double y0 = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
};

/// A part of the tray no cell may reach into, with its extent along the
/// grid's two axes.
struct Block {
  Rect r;
  double u0 = 0.0;
  double u1 = 0.0;
  double v0 = 0.0;
  double v1 = 0.0;
};

/// A closed interval; empty when lo > hi.
struct Span {
  double lo = -std::numeric_limits<double>::infinity();
  double hi = std::numeric_limits<double>::infinity();
  [[nodiscard]] bool empty() const { return lo > hi; }
};

/// Keeps of `sp` the part where a + lambda * b >= 0.
void keepWhere(Span& sp, double a, double b) {
  if (std::abs(b) < kSame) {
    if (a < 0.0) {
      sp.hi = -std::numeric_limits<double>::infinity();
      sp.lo = std::numeric_limits<double>::infinity();
    }
    return;
  }
  const double at = -a / b;
  if (b > 0.0) {
    sp.lo = std::max(sp.lo, at);
  } else {
    sp.hi = std::min(sp.hi, at);
  }
}

/// The tray's geometry for one set of settings: the turn, and the blocks
/// that stand for the walls and for the rim beside them.
///
/// A cell is whole when every point of it is within the allowance t, in x
/// and in y, of an open square. So the forbidden part of a wall or rim
/// square is the square less a strip of width t along each side that faces
/// an open square, and less a t by t corner where the square diagonally
/// across is open and the two beside it are not.
class Geometry {
 public:
  explicit Geometry(const Tray& tray)
      : size_(static_cast<int>(tray.size)),
        t_(tray.allowance),
        h_(std::hypot(static_cast<double>(tray.over), static_cast<double>(tray.down))),
        c_(static_cast<double>(tray.over) / h_),
        s_(static_cast<double>(tray.down) / h_),
        tray_(&tray) {
    buckets_.resize(static_cast<std::size_t>(size_ + 2) * static_cast<std::size_t>(size_ + 2));
    buildBlocks();
  }

  [[nodiscard]] int size() const { return size_; }
  [[nodiscard]] double allowance() const { return t_; }
  [[nodiscard]] double h() const { return h_; }
  [[nodiscard]] double c() const { return c_; }
  [[nodiscard]] double s() const { return s_; }
  [[nodiscard]] const std::vector<Block>& blocks() const { return blocks_; }

  /// Grid coordinates of tray point (x, y) for a shift.
  [[nodiscard]] Point toGrid(double x, double y, double sx, double sy) const {
    const double px = x - sx;
    const double py = y - sy;
    return {(px * c_) + (py * s_), (py * c_) - (px * s_)};
  }
  /// The tray point of grid point (gi, gj) at shift zero.
  [[nodiscard]] Point turned(double gi, double gj) const {
    return {(gi * c_) - (gj * s_), (gi * s_) + (gj * c_)};
  }

  /// The extent of cell (i, j) in the tray's x and y at shift zero.
  [[nodiscard]] Rect cellBox(int i, int j) const {
    const auto di = static_cast<double>(i);
    const auto dj = static_cast<double>(j);
    return {(di * c_) - ((dj + 1.0) * s_), (di * s_) + (dj * c_), ((di + 1.0) * c_) - (dj * s_),
            ((di + 1.0) * s_) + ((dj + 1.0) * c_)};
  }

  /// True when cell (i, j) at the shift reaches into rectangle `r` (whose
  /// extent along the grid's axes is u0..u1, v0..v1) by more than kEps.
  [[nodiscard]] bool reachesInto(int i, int j, double sx, double sy, const Block& b) const {
    const Rect box = cellBox(i, j);
    if (box.x1 + sx - b.r.x0 <= kEps || b.r.x1 - box.x0 - sx <= kEps ||
        box.y1 + sy - b.r.y0 <= kEps || b.r.y1 - box.y0 - sy <= kEps) {
      return false;
    }
    const double pu = (sx * c_) + (sy * s_);
    const double pv = (sy * c_) - (sx * s_);
    const auto di = static_cast<double>(i);
    const auto dj = static_cast<double>(j);
    return di + 1.0 + pu - b.u0 > kEps && b.u1 - di - pu > kEps && dj + 1.0 + pv - b.v0 > kEps &&
           b.v1 - dj - pv > kEps;
  }

  /// True when cell (i, j) is whole at the shift.
  [[nodiscard]] bool whole(int i, int j, double sx, double sy) const {
    const Rect box = cellBox(i, j);
    const auto side = static_cast<double>(size_);
    if (box.x0 + sx < -t_ - kEps || box.x1 + sx > side + t_ + kEps || box.y0 + sy < -t_ - kEps ||
        box.y1 + sy > side + t_ + kEps) {
      return false;
    }
    bool hit = false;
    forBlocksNear(box.x0 + sx, box.y0 + sy, box.x1 + sx, box.y1 + sy, [&](const Block& b) {
      hit = hit || reachesInto(i, j, sx, sy, b);
    });
    return !hit;
  }

  /// Calls `fn` with every block in a tray square the rectangle touches.
  template <typename Fn>
  void forBlocksNear(double x0, double y0, double x1, double y1, const Fn& fn) const {
    if (blocks_.empty()) {
      return;
    }
    const int a0 = std::clamp(static_cast<int>(std::floor(x0)), -1, size_);
    const int a1 = std::clamp(static_cast<int>(std::floor(x1)), -1, size_);
    const int b0 = std::clamp(static_cast<int>(std::floor(y0)), -1, size_);
    const int b1 = std::clamp(static_cast<int>(std::floor(y1)), -1, size_);
    for (int b = b0; b <= b1; b++) {
      for (int a = a0; a <= a1; a++) {
        for (const unsigned int k : buckets_[bucketOf(a, b)]) {
          fn(blocks_[k]);
        }
      }
    }
  }

  /// The cells that can be whole for some shift within kWindow of zero:
  /// i in first.lo .. first.hi, j in second.lo .. second.hi (whole numbers).
  [[nodiscard]] std::pair<Span, Span> cellRange() const {
    const double reach = (t_ + kWindow) * (c_ + s_);
    const auto side = static_cast<double>(size_);
    Span i;
    i.lo = std::floor(-reach) - 1.0;
    i.hi = std::ceil((side * (c_ + s_)) + reach);
    Span j;
    j.lo = std::floor((-side * s_) - reach) - 1.0;
    j.hi = std::ceil((side * c_) + reach);
    return {i, j};
  }

  /// The number of whole cells at a shift within kWindow of zero.
  [[nodiscard]] unsigned int countAt(double sx, double sy) const {
    const auto [ri, rj] = cellRange();
    unsigned int n = 0;
    for (int j = static_cast<int>(rj.lo); j <= static_cast<int>(rj.hi); j++) {
      for (int i = static_cast<int>(ri.lo); i <= static_cast<int>(ri.hi); i++) {
        if (whole(i, j, sx, sy)) {
          n++;
        }
      }
    }
    return n;
  }

 private:
  [[nodiscard]] std::size_t bucketOf(int a, int b) const {
    return (static_cast<std::size_t>(b + 1) * static_cast<std::size_t>(size_ + 2)) +
           static_cast<std::size_t>(a + 1);
  }

  /// True when tray square (a, b) is a wall or lies outside the tray.
  [[nodiscard]] bool forbidden(int a, int b) const {
    if (a < 0 || b < 0 || a >= size_ || b >= size_) {
      return true;
    }
    return tray_->isWall(static_cast<unsigned int>(a), static_cast<unsigned int>(b));
  }

  /// One of the three stretches of a square's side: the strip of width t at
  /// its low end (-1), the middle (0), the strip at its high end (+1).
  [[nodiscard]] std::pair<double, double> stretch(int at, int which) const {
    const auto lo = static_cast<double>(at);
    if (which < 0) {
      return {lo, lo + t_};
    }
    if (which > 0) {
      return {lo + 1.0 - t_, lo + 1.0};
    }
    return {lo + t_, lo + 1.0 - t_};
  }

  void addBlock(int a, int b, Rect r) {
    // a cell must stay within t of the tray anyway (whole()): what lies
    // farther out needs no block
    const auto side = static_cast<double>(size_);
    r.x0 = std::max(r.x0, -t_);
    r.y0 = std::max(r.y0, -t_);
    r.x1 = std::min(r.x1, side + t_);
    r.y1 = std::min(r.y1, side + t_);
    if (r.x1 - r.x0 <= kSame || r.y1 - r.y0 <= kSame) {
      return;
    }
    Block blk;
    blk.r = r;
    blk.u0 = (r.x0 * c_) + (r.y0 * s_);
    blk.u1 = (r.x1 * c_) + (r.y1 * s_);
    blk.v0 = (r.y0 * c_) - (r.x1 * s_);
    blk.v1 = (r.y1 * c_) - (r.x0 * s_);
    buckets_[bucketOf(a, b)].push_back(static_cast<unsigned int>(blocks_.size()));
    blocks_.push_back(blk);
  }

  void buildBlocks() {
    if (tray_->walls.empty()) {
      return;  // the rim alone is whole()'s first test
    }
    for (int b = -1; b <= size_; b++) {
      for (int a = -1; a <= size_; a++) {
        if (!forbidden(a, b)) {
          continue;
        }
        bool all = true;
        std::array<bool, 9> part{};
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            // the part stays forbidden when every square it faces is
            bool in = true;
            for (const int ey : {0, dy}) {
              for (const int ex : {0, dx}) {
                in = in && ((ex == 0 && ey == 0) || forbidden(a + ex, b + ey));
              }
            }
            part[static_cast<std::size_t>(((dy + 1) * 3) + dx + 1)] = in;
            all = all && in;
          }
        }
        const auto x = static_cast<double>(a);
        const auto y = static_cast<double>(b);
        if (all || t_ <= 0.0) {
          addBlock(a, b, {x, y, x + 1.0, y + 1.0});
          continue;
        }
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            if (part[static_cast<std::size_t>(((dy + 1) * 3) + dx + 1)]) {
              const auto [x0, x1] = stretch(a, dx);
              const auto [y0, y1] = stretch(b, dy);
              addBlock(a, b, {x0, y0, x1, y1});
            }
          }
        }
      }
    }
  }

  int size_;
  double t_;
  double h_;
  double c_;
  double s_;
  const Tray* tray_;
  std::vector<Block> blocks_;
  std::vector<std::vector<unsigned int>> buckets_;
};

/// A line of shifts p + lambda * d, lambda in `range`.
struct Line {
  double px = 0.0;
  double py = 0.0;
  double dx = 0.0;
  double dy = 0.0;
  Span range;
};

/// The best count along a line and where.
struct LineBest {
  unsigned int count = 0;
  double lambda = 0.0;
};

/// The values of `v` in rising order, near-equal ones once.
void sortUnique(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  std::size_t kept = 0;
  for (std::size_t k = 0; k < v.size(); k++) {
    if (kept == 0 || v[k] - v[kept - 1] > kSame) {
      v[kept++] = v[k];
    }
  }
  v.resize(kept);
}

/// The most whole cells along `line`, and the middle of the stretch that has
/// them.
///
/// Along the line each cell is whole on a closed interval (inside the rim)
/// less an open interval for every block it would reach into. The count can
/// only rise where one of the closed pieces left begins, so those starts
/// are the places tried.
LineBest bestOnLine(const Geometry& g, const Line& line) {
  const auto [ri, rj] = g.cellRange();
  const auto side = static_cast<double>(g.size());
  const double t = g.allowance();
  // the line's position and direction along the grid's axes
  const double pu = (line.px * g.c()) + (line.py * g.s());
  const double pv = (line.py * g.c()) - (line.px * g.s());
  const double du = (line.dx * g.c()) + (line.dy * g.s());
  const double dv = (line.dy * g.c()) - (line.dx * g.s());

  std::vector<double> starts;
  std::vector<double> ends;
  std::vector<std::pair<double, double>> cuts;
  for (int j = static_cast<int>(rj.lo); j <= static_cast<int>(rj.hi); j++) {
    for (int i = static_cast<int>(ri.lo); i <= static_cast<int>(ri.hi); i++) {
      const Rect box = g.cellBox(i, j);
      Span in = line.range;
      keepWhere(in, box.x0 + line.px + t + kEps, line.dx);
      keepWhere(in, side + t + kEps - box.x1 - line.px, -line.dx);
      keepWhere(in, box.y0 + line.py + t + kEps, line.dy);
      keepWhere(in, side + t + kEps - box.y1 - line.py, -line.dy);
      if (in.empty()) {
        continue;
      }
      // where the cell reaches into a block: open intervals
      cuts.clear();
      const auto di = static_cast<double>(i);
      const auto dj = static_cast<double>(j);
      g.forBlocksNear(box.x0 - kWindow, box.y0 - kWindow, box.x1 + kWindow, box.y1 + kWindow,
                      [&](const Block& b) {
                        Span sp;
                        keepWhere(sp, box.x1 + line.px - b.r.x0 - kEps, line.dx);
                        keepWhere(sp, b.r.x1 - box.x0 - line.px - kEps, -line.dx);
                        keepWhere(sp, box.y1 + line.py - b.r.y0 - kEps, line.dy);
                        keepWhere(sp, b.r.y1 - box.y0 - line.py - kEps, -line.dy);
                        keepWhere(sp, di + 1.0 + pu - b.u0 - kEps, du);
                        keepWhere(sp, b.u1 - di - pu - kEps, -du);
                        keepWhere(sp, dj + 1.0 + pv - b.v0 - kEps, dv);
                        keepWhere(sp, b.v1 - dj - pv - kEps, -dv);
                        if (sp.lo < sp.hi && sp.hi > in.lo && sp.lo < in.hi) {
                          cuts.emplace_back(sp.lo, sp.hi);
                        }
                      });
      std::sort(cuts.begin(), cuts.end());
      double start = in.lo;
      for (const auto& [a, b] : cuts) {
        if (start > in.hi) {
          break;
        }
        if (b <= start) {
          continue;
        }
        if (a >= start) {
          starts.push_back(start);
          ends.push_back(std::min(a, in.hi));
        }
        start = b;
      }
      if (start <= in.hi) {
        starts.push_back(start);
        ends.push_back(in.hi);
      }
    }
  }

  std::sort(starts.begin(), starts.end());
  std::sort(ends.begin(), ends.end());
  LineBest best;
  best.lambda = (line.range.lo + line.range.hi) / 2.0;
  for (std::size_t k = 0; k < starts.size(); k++) {
    const double at = starts[k];
    const auto begun = static_cast<std::size_t>(
        std::upper_bound(starts.begin(), starts.end(), at) - starts.begin());
    const auto firstEnd = std::lower_bound(ends.begin(), ends.end(), at);
    const auto over = static_cast<std::size_t>(firstEnd - ends.begin());
    const auto count = static_cast<unsigned int>(begun - over);
    if (count > best.count) {
      // the count holds until a piece ends or another begins
      double until = *firstEnd;
      if (begun < starts.size()) {
        until = std::min(until, starts[begun]);
      }
      best.count = count;
      best.lambda = (at + until) / 2.0;
    }
  }
  return best;
}

/// `shift` moved by whole grid steps to within half a grid step of zero.
Point nearZero(const Geometry& g, double sx, double sy) {
  const Point at = g.toGrid(sx, sy, 0.0, 0.0);
  return g.turned(at.x - std::round(at.x), at.y - std::round(at.y));
}

}  // namespace

bool Tray::valid() const {
  if (size < 1 || size > kMaxSize || over < 1 || over > kMaxSlope || down > over ||
      std::gcd(over, down) != 1) {
    return false;
  }
  if (!std::isfinite(allowance) || allowance < 0.0 || allowance > kMaxAllowance ||
      !std::isfinite(shiftX) || !std::isfinite(shiftY) || std::abs(shiftX) > kMaxShift ||
      std::abs(shiftY) > kMaxShift) {
    return false;
  }
  if (placement != Placement::Best && placement != Placement::Custom) {
    return false;
  }
  return walls.empty() || walls.size() == static_cast<std::size_t>(size) * size;
}

double Tray::degrees() const {
  return std::atan2(static_cast<double>(down), static_cast<double>(over)) * 180.0 /
         std::numbers::pi;
}

Tray withSize(const Tray& tray, unsigned int size) {
  BT_ASSERT(size >= 1 && size <= Tray::kMaxSize);
  Tray out = tray;
  out.size = size;
  out.walls.clear();
  bool any = false;
  std::vector<std::uint8_t> walls(static_cast<std::size_t>(size) * size, 0);
  for (unsigned int y = 0; y < std::min(size, tray.size); y++) {
    for (unsigned int x = 0; x < std::min(size, tray.size); x++) {
      if (tray.isWall(x, y)) {
        walls[(static_cast<std::size_t>(y) * size) + x] = 1;
        any = true;
      }
    }
  }
  if (any) {
    out.walls = std::move(walls);
  }
  return out;
}

Point canonicalShift(const Tray& tray, Point shift) {
  BT_ASSERT(tray.valid());
  const Geometry g(tray);
  // the tray's corner (0, 0) in grid coordinates; its whole part is taken off
  const Point corner = g.toGrid(0.0, 0.0, shift.x, shift.y);
  const auto whole = [](double v) {
    const double f = std::floor(v);
    return v - f > 1.0 - kEps ? f + 1.0 : f;
  };
  const Point step = g.turned(whole(corner.x), whole(corner.y));
  return {shift.x + step.x, shift.y + step.y};
}

Layout layout(const Tray& tray) {
  BT_ASSERT(tray.valid());
  const Geometry g(tray);
  const auto side = static_cast<double>(tray.size);
  const double sx = tray.shiftX;
  const double sy = tray.shiftY;

  // the grid cells the tray's square can touch
  const std::array<Point, 4> trayCorners = {
      g.toGrid(0.0, 0.0, sx, sy), g.toGrid(side, 0.0, sx, sy), g.toGrid(side, side, sx, sy),
      g.toGrid(0.0, side, sx, sy)};
  double gx0 = trayCorners[0].x;
  double gx1 = gx0;
  double gy0 = trayCorners[0].y;
  double gy1 = gy0;
  for (const Point& p : trayCorners) {
    gx0 = std::min(gx0, p.x);
    gx1 = std::max(gx1, p.x);
    gy0 = std::min(gy0, p.y);
    gy1 = std::max(gy1, p.y);
  }
  const int i0 = static_cast<int>(std::floor(gx0)) - 1;
  const int i1 = static_cast<int>(std::floor(gx1)) + 1;
  const int j0 = static_cast<int>(std::floor(gy0)) - 1;
  const int j1 = static_cast<int>(std::floor(gy1)) + 1;

  // the tray itself, as a block: a cell that reaches into it is on the board
  Block inside;
  inside.r = {0.0, 0.0, side, side};
  inside.u0 = 0.0;
  inside.u1 = side * (g.c() + g.s());
  inside.v0 = -side * g.s();
  inside.v1 = side * g.c();

  std::optional<int> minI;
  int maxI = 0;
  int minJ = 0;
  int maxJ = 0;
  for (int j = j0; j <= j1; j++) {
    for (int i = i0; i <= i1; i++) {
      if (!g.reachesInto(i, j, sx, sy, inside)) {
        continue;
      }
      if (!minI) {
        minI = maxI = i;
        minJ = maxJ = j;
      }
      minI = std::min(*minI, i);
      maxI = std::max(maxI, i);
      minJ = std::min(minJ, j);
      maxJ = std::max(maxJ, j);
    }
  }
  BT_ASSERT(minI.has_value());  // a tray of side 1 or more holds part of a cell

  Layout out;
  out.sizeX = static_cast<unsigned int>(maxI - *minI + 1);
  out.sizeY = static_cast<unsigned int>(maxJ - minJ + 1);
  out.cells.assign(static_cast<std::size_t>(out.sizeX) * out.sizeY, Cell::Outside);
  for (int j = minJ; j <= maxJ; j++) {
    for (int i = *minI; i <= maxI; i++) {
      if (!g.reachesInto(i, j, sx, sy, inside)) {
        continue;
      }
      const bool isWhole = g.whole(i, j, sx, sy);
      out.cells[(static_cast<std::size_t>(j - minJ) * out.sizeX) +
                static_cast<std::size_t>(i - *minI)] = isWhole ? Cell::Whole : Cell::Cut;
      (isWhole ? out.whole : out.cut)++;
    }
  }

  const auto inBox = [&](double x, double y) {
    const Point p = g.toGrid(x, y, sx, sy);
    return Point{p.x - static_cast<double>(*minI), p.y - static_cast<double>(minJ)};
  };
  out.corners = {inBox(0.0, 0.0), inBox(side, 0.0), inBox(side, side), inBox(0.0, side)};
  for (unsigned int y = 0; y < tray.size; y++) {
    for (unsigned int x = 0; x < tray.size; x++) {
      if (tray.isWall(x, y)) {
        const auto wx = static_cast<double>(x);
        const auto wy = static_cast<double>(y);
        out.walls.push_back({inBox(wx, wy), inBox(wx + 1.0, wy), inBox(wx + 1.0, wy + 1.0),
                             inBox(wx, wy + 1.0)});
      }
    }
  }
  return out;
}

Placed bestShift(const Tray& tray) {
  BT_ASSERT(tray.valid());
  const Geometry g(tray);
  const auto side = static_cast<double>(tray.size);
  const double t = g.allowance();
  const double h = g.h();
  const double c = g.c();
  const double s = g.s();

  // The lines the count can change on. A cell's corner is at a whole number
  // of 1/h in x and in y at shift zero, so it meets the upright line x = X
  // when the shift's x is X - m/h; a wall's corner P meets a cell's edge
  // when the shift's part along the grid's first axis is P.u - k.
  std::vector<double> xs = {-t, side + t};
  std::vector<double> ys = {-t, side + t};
  std::vector<double> us;
  for (const Block& b : g.blocks()) {
    xs.push_back(b.r.x0);
    xs.push_back(b.r.x1);
    ys.push_back(b.r.y0);
    ys.push_back(b.r.y1);
    if (s > 0.0) {
      for (const double x : {b.r.x0, b.r.x1}) {
        for (const double y : {b.r.y0, b.r.y1}) {
          us.push_back((x * c) + (y * s));
        }
      }
    }
  }
  sortUnique(xs);
  sortUnique(ys);
  sortUnique(us);

  const auto offsets = [&](const std::vector<double>& at, double step, double reach) {
    std::vector<double> out;
    for (const double v : at) {
      const auto first = static_cast<long>(std::ceil((v - reach) / step));
      const auto last = static_cast<long>(std::floor((v + reach) / step));
      for (long m = first; m <= last; m++) {
        out.push_back(v - (static_cast<double>(m) * step));
      }
    }
    sortUnique(out);
    return out;
  };

  Placed best;
  bool found = false;
  const auto tryLine = [&](const Line& line) {
    if (line.range.empty()) {
      return;
    }
    const LineBest lb = bestOnLine(g, line);
    if (!found || lb.count > best.whole) {
      found = true;
      best.whole = lb.count;
      best.shiftX = line.px + (lb.lambda * line.dx);
      best.shiftY = line.py + (lb.lambda * line.dy);
    }
  };

  Span window;
  window.lo = -kWindow;
  window.hi = kWindow;
  for (const double x : offsets(xs, 1.0 / h, kWindow)) {
    tryLine({x, 0.0, 0.0, 1.0, window});
  }
  for (const double y : offsets(ys, 1.0 / h, kWindow)) {
    tryLine({0.0, y, 1.0, 0.0, window});
  }
  for (const double u : offsets(us, 1.0, kWindow * (c + s))) {
    // shifts whose part along the first axis is u: u * (c, s) + lambda * (-s, c)
    Line line{u * c, u * s, -s, c, {}};
    keepWhere(line.range, line.px + kWindow, line.dx);
    keepWhere(line.range, kWindow - line.px, -line.dx);
    keepWhere(line.range, line.py + kWindow, line.dy);
    keepWhere(line.range, kWindow - line.py, -line.dy);
    tryLine(line);
  }

  // a placement symmetric about the tray's center, when it is as good: the
  // center on a cell's middle, on a grid point, or on the middle of an edge
  const double mid = side / 2.0;
  const std::array<Point, 4> centered = {g.turned(0.5, 0.5), g.turned(0.0, 0.0),
                                             g.turned(0.5, 0.0), g.turned(0.0, 0.5)};
  std::array<Point, 4> sym{};
  std::array<unsigned int, 4> symCount{};
  unsigned int top = best.whole;
  for (std::size_t k = 0; k < centered.size(); k++) {
    sym[k] = nearZero(g, mid - centered[k].x, mid - centered[k].y);
    symCount[k] = g.countAt(sym[k].x, sym[k].y);
    top = std::max(top, symCount[k]);
  }
  for (std::size_t k = 0; k < centered.size(); k++) {
    if (symCount[k] == top) {
      best.shiftX = sym[k].x;
      best.shiftY = sym[k].y;
      break;
    }
  }

  // counted again where it will be used
  best.whole = g.countAt(best.shiftX, best.shiftY);
  const Point canon = canonicalShift(tray, {best.shiftX, best.shiftY});
  best.shiftX = canon.x;
  best.shiftY = canon.y;
  return best;
}

}  // namespace bt::tilted
