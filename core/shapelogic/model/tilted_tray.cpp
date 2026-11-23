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
#include <string>
#include <unordered_set>
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
      : w_(static_cast<int>(tray.width)),
        h_(static_cast<int>(tray.height)),
        t_(tray.allowance),
        hyp_(std::hypot(static_cast<double>(tray.over), static_cast<double>(tray.down))),
        c_(static_cast<double>(tray.over) / hyp_),
        s_(static_cast<double>(tray.down) / hyp_),
        tray_(&tray) {
    buckets_.resize(static_cast<std::size_t>(w_ + 2) * static_cast<std::size_t>(h_ + 2));
    buildBlocks();
  }

  /// The tray's sides.
  [[nodiscard]] double width() const { return static_cast<double>(w_); }
  [[nodiscard]] double height() const { return static_cast<double>(h_); }
  [[nodiscard]] double allowance() const { return t_; }
  /// The length of the slope's (over, down).
  [[nodiscard]] double h() const { return hyp_; }
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
    if (box.x0 + sx < -t_ - kEps || box.x1 + sx > width() + t_ + kEps ||
        box.y0 + sy < -t_ - kEps || box.y1 + sy > height() + t_ + kEps) {
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
    const int a0 = std::clamp(static_cast<int>(std::floor(x0)), -1, w_);
    const int a1 = std::clamp(static_cast<int>(std::floor(x1)), -1, w_);
    const int b0 = std::clamp(static_cast<int>(std::floor(y0)), -1, h_);
    const int b1 = std::clamp(static_cast<int>(std::floor(y1)), -1, h_);
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
    // along the grid's axes the tray reaches from 0 to width * c + height * s
    // and from -width * s to height * c
    const double reach = (t_ + kWindow) * (c_ + s_);
    Span i;
    i.lo = std::floor(-reach) - 1.0;
    i.hi = std::ceil((width() * c_) + (height() * s_) + reach);
    Span j;
    j.lo = std::floor((-width() * s_) - reach) - 1.0;
    j.hi = std::ceil((height() * c_) + reach);
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
    return (static_cast<std::size_t>(b + 1) * static_cast<std::size_t>(w_ + 2)) +
           static_cast<std::size_t>(a + 1);
  }

  /// True when tray square (a, b) is a wall or lies outside the tray.
  [[nodiscard]] bool forbidden(int a, int b) const {
    if (a < 0 || b < 0 || a >= w_ || b >= h_) {
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
    r.x0 = std::max(r.x0, -t_);
    r.y0 = std::max(r.y0, -t_);
    r.x1 = std::min(r.x1, width() + t_);
    r.y1 = std::min(r.y1, height() + t_);
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
    for (int b = -1; b <= h_; b++) {
      for (int a = -1; a <= w_; a++) {
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

  int w_;
  int h_;
  double t_;
  double hyp_;
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

/// A stretch of a line on which one cell is whole: lambda in lo .. hi, both
/// ends included.
struct Piece {
  double lo = 0.0;
  double hi = 0.0;
  /// The cell, numbered row by row over Geometry::cellRange().
  unsigned int cell = 0;
};

/// Where each cell is whole along `line`, cell by cell, a cell's stretches in
/// rising order.
///
/// Along the line each cell is whole on a closed interval (inside the rim)
/// less an open interval for every block it would reach into: closed pieces
/// are left.
void piecesOnLine(const Geometry& g, const Line& line, std::vector<Piece>& out) {
  out.clear();
  const auto [ri, rj] = g.cellRange();
  const double t = g.allowance();
  // the line's position and direction along the grid's axes
  const double pu = (line.px * g.c()) + (line.py * g.s());
  const double pv = (line.py * g.c()) - (line.px * g.s());
  const double du = (line.dx * g.c()) + (line.dy * g.s());
  const double dv = (line.dy * g.c()) - (line.dx * g.s());

  std::vector<std::pair<double, double>> cuts;
  unsigned int cell = 0;
  for (int j = static_cast<int>(rj.lo); j <= static_cast<int>(rj.hi); j++) {
    for (int i = static_cast<int>(ri.lo); i <= static_cast<int>(ri.hi); i++, cell++) {
      const Rect box = g.cellBox(i, j);
      Span in = line.range;
      keepWhere(in, box.x0 + line.px + t + kEps, line.dx);
      keepWhere(in, g.width() + t + kEps - box.x1 - line.px, -line.dx);
      keepWhere(in, box.y0 + line.py + t + kEps, line.dy);
      keepWhere(in, g.height() + t + kEps - box.y1 - line.py, -line.dy);
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
          out.push_back({start, std::min(a, in.hi), cell});
        }
        start = b;
      }
      if (start <= in.hi) {
        out.push_back({start, in.hi, cell});
      }
    }
  }
}

/// The most whole cells along `line`, and the middle of the stretch that has
/// them. The count can only rise where one of the cells' pieces begins, so
/// those starts are the places tried.
LineBest bestOnLine(const Geometry& g, const Line& line, std::vector<Piece>& pieces) {
  piecesOnLine(g, line, pieces);
  std::vector<double> starts;
  std::vector<double> ends;
  starts.reserve(pieces.size());
  ends.reserve(pieces.size());
  for (const Piece& p : pieces) {
    starts.push_back(p.lo);
    ends.push_back(p.hi);
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

/// The lines of shifts the board can change on, within kWindow of zero: on
/// every other shift each cell is whole or cut as on the shifts around it.
///
/// A cell's corner is at a whole number of 1/h in x and in y at shift zero,
/// so it meets the upright line x = X when the shift's x is X - m/h; a
/// wall's corner P meets a cell's edge when the shift's part along the
/// grid's first axis is P.u - k. (The lines along the grid's second axis are
/// not needed: where two lines cross, one of them is of these three kinds.)
std::vector<Line> sweepLines(const Geometry& g) {
  const double t = g.allowance();
  const double h = g.h();
  const double c = g.c();
  const double s = g.s();
  std::vector<double> xs = {-t, g.width() + t};
  std::vector<double> ys = {-t, g.height() + t};
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

  std::vector<Line> lines;
  Span window;
  window.lo = -kWindow;
  window.hi = kWindow;
  for (const double x : offsets(xs, 1.0 / h, kWindow)) {
    lines.push_back({x, 0.0, 0.0, 1.0, window});
  }
  for (const double y : offsets(ys, 1.0 / h, kWindow)) {
    lines.push_back({0.0, y, 1.0, 0.0, window});
  }
  for (const double u : offsets(us, 1.0, kWindow * (c + s))) {
    // shifts whose part along the first axis is u: u * (c, s) + lambda * (-s, c)
    Line line{u * c, u * s, -s, c, {}};
    keepWhere(line.range, line.px + kWindow, line.dx);
    keepWhere(line.range, kWindow - line.px, -line.dx);
    keepWhere(line.range, line.py + kWindow, line.dy);
    keepWhere(line.range, kWindow - line.py, -line.dy);
    if (!line.range.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

/// `shift` moved by whole grid steps to within half a grid step of zero.
Point nearZero(const Geometry& g, double sx, double sy) {
  const Point at = g.toGrid(sx, sy, 0.0, 0.0);
  return g.turned(at.x - std::round(at.x), at.y - std::round(at.y));
}

/// A board's whole cells in their own box (the lowest x and y are 0).
struct BoardCells {
  unsigned int w = 0;
  unsigned int h = 0;
  /// Each cell's x and y, row by row.
  std::vector<std::array<unsigned int, 2>> cells;

  /// The cells as text, row by row with the box's size first: equal boards
  /// have equal texts.
  [[nodiscard]] std::string text() const {
    std::string out = std::to_string(w) + "x" + std::to_string(h) + ":";
    const std::size_t head = out.size();
    out.append(static_cast<std::size_t>(w) * h, '.');
    for (const std::array<unsigned int, 2>& c : cells) {
      out[head + (static_cast<std::size_t>(c[1]) * w) + c[0]] = '#';
    }
    return out;
  }

  /// The board turned a quarter: (x, y) goes to (h - 1 - y, x).
  [[nodiscard]] BoardCells turned() const {
    BoardCells out;
    out.w = h;
    out.h = w;
    out.cells.reserve(cells.size());
    for (const std::array<unsigned int, 2>& c : cells) {
      out.cells.push_back({h - 1 - c[1], c[0]});
    }
    std::sort(out.cells.begin(), out.cells.end(),
              [](const auto& a, const auto& b) { return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0]; });
    return out;
  }
};

/// The cells flagged in `in` (row by row over a `wide` grid) in their own box.
BoardCells boxed(const std::vector<std::uint8_t>& in, unsigned int wide) {
  BoardCells out;
  unsigned int x0 = wide;
  unsigned int y0 = 0;
  unsigned int x1 = 0;
  unsigned int y1 = 0;
  bool any = false;
  for (unsigned int k = 0; k < in.size(); k++) {
    if (in[k] == 0) {
      continue;
    }
    const unsigned int x = k % wide;
    const unsigned int y = k / wide;
    if (!any) {
      y0 = y;
      any = true;
    }
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    y1 = y;
  }
  if (!any) {
    return out;
  }
  out.w = x1 - x0 + 1;
  out.h = y1 - y0 + 1;
  for (unsigned int k = 0; k < in.size(); k++) {
    if (in[k] != 0) {
      out.cells.push_back({(k % wide) - x0, (k / wide) - y0});
    }
  }
  return out;
}

/// The board in its four turns.
std::array<BoardCells, 4> turnsOf(BoardCells cells) {
  std::array<BoardCells, 4> out;
  out[0] = std::move(cells);
  for (std::size_t k = 1; k < 4; k++) {
    out[k] = out[k - 1].turned();
  }
  return out;
}

/// A board found.
struct FoundBoard {
  /// The least of the four turns' texts: equal for boards that are one
  /// another turned.
  std::string key;
  BoardCells cells;
  double shiftX = 0.0;
  double shiftY = 0.0;
  /// The best placement's board: first among those with as many cells.
  bool best = false;

  [[nodiscard]] std::size_t whole() const { return cells.cells.size(); }
};

FoundBoard foundBoard(BoardCells cells, double sx, double sy) {
  FoundBoard out;
  out.shiftX = sx;
  out.shiftY = sy;
  const std::array<BoardCells, 4> turns = turnsOf(std::move(cells));
  out.key = turns[0].text();
  for (std::size_t k = 1; k < 4; k++) {
    out.key = std::min(out.key, turns[k].text());
  }
  out.cells = turns[0];
  return out;
}

/// True when one of `part` (a board's four turns), moved by whole cells,
/// lies on cells of `all`.
bool partOf(const std::array<BoardCells, 4>& part, const BoardCells& all) {
  std::vector<std::uint8_t> on(static_cast<std::size_t>(all.w) * all.h, 0);
  for (const std::array<unsigned int, 2>& c : all.cells) {
    on[(static_cast<std::size_t>(c[1]) * all.w) + c[0]] = 1;
  }
  for (const BoardCells& a : part) {
    if (a.w > all.w || a.h > all.h) {
      continue;
    }
    for (unsigned int dy = 0; dy + a.h <= all.h; dy++) {
      for (unsigned int dx = 0; dx + a.w <= all.w; dx++) {
        const bool inside = std::all_of(a.cells.begin(), a.cells.end(), [&](const auto& c) {
          return on[(static_cast<std::size_t>(c[1] + dy) * all.w) + c[0] + dx] != 0;
        });
        if (inside) {
          return true;
        }
      }
    }
  }
  return false;
}

/// Two numbers for a cell, the same on every machine (SplitMix64): a set of
/// cells is told from another by the two sums of its cells' numbers.
std::array<std::uint64_t, 2> cellMark(std::size_t cell) {
  const auto mix = [](std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  };
  const std::uint64_t first = mix(cell);
  return {first, mix(first ^ 0xD1B54A32D192ED03ULL)};
}

/// Hashes a pair of marks.
struct MarkHash {
  std::size_t operator()(const std::array<std::uint64_t, 2>& m) const noexcept {
    return static_cast<std::size_t>(m[0] ^ (m[1] * 0x9E3779B97F4A7C15ULL));
  }
};

/// The cells whole at a shift within kWindow of zero, in their own box.
BoardCells boardAt(const Geometry& g, double sx, double sy) {
  const auto [ri, rj] = g.cellRange();
  const auto wide = static_cast<unsigned int>(ri.hi - ri.lo) + 1;
  const auto high = static_cast<unsigned int>(rj.hi - rj.lo) + 1;
  std::vector<std::uint8_t> in(static_cast<std::size_t>(wide) * high, 0);
  std::size_t k = 0;
  for (int j = static_cast<int>(rj.lo); j <= static_cast<int>(rj.hi); j++) {
    for (int i = static_cast<int>(ri.lo); i <= static_cast<int>(ri.hi); i++, k++) {
      in[k] = g.whole(i, j, sx, sy) ? 1 : 0;
    }
  }
  return boxed(in, wide);
}

}  // namespace

bool Tray::valid() const {
  if (width < 1 || width > kMaxSize || height < 1 || height > kMaxSize || over < 1 ||
      over > kMaxSlope || down > over || std::gcd(over, down) != 1) {
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
  return walls.empty() || walls.size() == static_cast<std::size_t>(width) * height;
}

double Tray::degrees() const {
  return std::atan2(static_cast<double>(down), static_cast<double>(over)) * 180.0 /
         std::numbers::pi;
}

Tray withSize(const Tray& tray, unsigned int width, unsigned int height) {
  BT_ASSERT(width >= 1 && width <= Tray::kMaxSize && height >= 1 && height <= Tray::kMaxSize);
  Tray out = tray;
  out.width = width;
  out.height = height;
  out.walls.clear();
  bool any = false;
  std::vector<std::uint8_t> walls(static_cast<std::size_t>(width) * height, 0);
  for (unsigned int y = 0; y < std::min(height, tray.height); y++) {
    for (unsigned int x = 0; x < std::min(width, tray.width); x++) {
      if (tray.isWall(x, y)) {
        walls[(static_cast<std::size_t>(y) * width) + x] = 1;
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
  const double wide = g.width();
  const double high = g.height();
  const double sx = tray.shiftX;
  const double sy = tray.shiftY;

  // the grid cells the tray's rectangle can touch
  const std::array<Point, 4> trayCorners = {
      g.toGrid(0.0, 0.0, sx, sy), g.toGrid(wide, 0.0, sx, sy), g.toGrid(wide, high, sx, sy),
      g.toGrid(0.0, high, sx, sy)};
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
  inside.r = {0.0, 0.0, wide, high};
  inside.u0 = 0.0;
  inside.u1 = (wide * g.c()) + (high * g.s());
  inside.v0 = -wide * g.s();
  inside.v1 = high * g.c();

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
  BT_ASSERT(minI.has_value());  // a tray with sides of 1 or more holds part of a cell

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
  out.corners = {inBox(0.0, 0.0), inBox(wide, 0.0), inBox(wide, high), inBox(0.0, high)};
  for (unsigned int y = 0; y < tray.height; y++) {
    for (unsigned int x = 0; x < tray.width; x++) {
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
  // every line the count can change on is swept
  Placed best;
  bool found = false;
  std::vector<Piece> pieces;
  for (const Line& line : sweepLines(g)) {
    const LineBest lb = bestOnLine(g, line, pieces);
    if (!found || lb.count > best.whole) {
      found = true;
      best.whole = lb.count;
      best.shiftX = line.px + (lb.lambda * line.dx);
      best.shiftY = line.py + (lb.lambda * line.dy);
    }
  }

  // a placement symmetric about the tray's center, when it is as good: the
  // center on a cell's middle, on a grid point, or on the middle of an edge
  const double midX = g.width() / 2.0;
  const double midY = g.height() / 2.0;
  const std::array<Point, 4> centered = {g.turned(0.5, 0.5), g.turned(0.0, 0.0),
                                             g.turned(0.5, 0.0), g.turned(0.0, 0.5)};
  std::array<Point, 4> sym{};
  std::array<unsigned int, 4> symCount{};
  unsigned int top = best.whole;
  for (std::size_t k = 0; k < centered.size(); k++) {
    sym[k] = nearZero(g, midX - centered[k].x, midY - centered[k].y);
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

Placements placements(const Tray& tray, const StopToken& stop, const PlacementLimits& limits) {
  BT_ASSERT(tray.valid());
  const Geometry g(tray);
  const auto [ri, rj] = g.cellRange();
  const auto wide = static_cast<unsigned int>(ri.hi - ri.lo) + 1;
  const auto high = static_cast<unsigned int>(rj.hi - rj.lo) + 1;
  const std::size_t cellCount = static_cast<std::size_t>(wide) * high;

  Placements out;
  std::vector<FoundBoard> found;
  std::unordered_set<std::string> keys;
  std::uint64_t work = 0;
  const auto add = [&](double sx, double sy) {
    work += cellCount;
    FoundBoard b = foundBoard(boardAt(g, sx, sy), sx, sy);
    if (keys.contains(b.key)) {
      return;
    }
    if (found.size() >= limits.maxBoards) {
      out.complete = false;  // one more than may be kept
      return;
    }
    keys.insert(b.key);
    found.push_back(std::move(b));
  };
  // the best placement first, so that its board keeps its shift
  const Placed best = bestShift(tray);
  const Point bestNear = nearZero(g, best.shiftX, best.shiftY);
  add(bestNear.x, bestNear.y);
  found[0].shiftX = best.shiftX;
  found[0].shiftY = best.shiftY;
  found[0].best = true;

  // Each line swept: where pieces have begun and the next thing is a piece's
  // end, the cells whole are more than on either side. The board is taken
  // in the middle of that stretch, where rounding cannot change it.
  std::vector<std::array<std::uint64_t, 2>> marks(cellCount);
  for (std::size_t k = 0; k < cellCount; k++) {
    marks[k] = cellMark(k);
  }
  std::vector<Piece> pieces;
  std::vector<std::size_t> byEnd;
  // the same cells are met on many lines: looked at once
  std::unordered_set<std::array<std::uint64_t, 2>, MarkHash> swept;
  for (const Line& line : sweepLines(g)) {
    if (!out.complete || stop.stopRequested() || work > limits.maxWork) {
      out.complete = false;
      break;
    }
    piecesOnLine(g, line, pieces);
    work += cellCount + pieces.size();
    std::sort(pieces.begin(), pieces.end(),
              [](const Piece& a, const Piece& b) { return a.lo < b.lo; });
    byEnd.resize(pieces.size());
    std::iota(byEnd.begin(), byEnd.end(), std::size_t{0});
    std::sort(byEnd.begin(), byEnd.end(),
              [&](std::size_t a, std::size_t b) { return pieces[a].hi < pieces[b].hi; });
    std::array<std::uint64_t, 2> in{};
    std::size_t begun = 0;
    std::size_t ended = 0;
    bool rising = false;
    double last = 0.0;
    while (ended < pieces.size()) {
      const Piece& end = pieces[byEnd[ended]];
      if (begun < pieces.size() && pieces[begun].lo <= end.hi) {
        // a piece that begins where another ends is whole there too
        const std::array<std::uint64_t, 2>& m = marks[pieces[begun].cell];
        in = {in[0] + m[0], in[1] + m[1]};
        last = pieces[begun].lo;
        rising = true;
        begun++;
        continue;
      }
      if (rising && swept.insert(in).second) {
        const double lambda = (last + end.hi) / 2.0;
        add(line.px + (lambda * line.dx), line.py + (lambda * line.dy));
      }
      rising = false;
      const std::array<std::uint64_t, 2>& m = marks[end.cell];
      in = {in[0] - m[0], in[1] - m[1]};
      ended++;
    }
  }

  // the most cells first; a board that is part of one with more cells goes
  std::sort(found.begin(), found.end(), [](const FoundBoard& a, const FoundBoard& b) {
    if (a.whole() != b.whole()) {
      return a.whole() > b.whole();
    }
    return a.best != b.best ? a.best : a.key < b.key;
  });
  std::vector<const FoundBoard*> kept;
  bool stopped = false;
  for (const FoundBoard& b : found) {
    // stopped, the rest is kept as it is: the answer is not complete anyway
    stopped = stopped || stop.stopRequested();
    bool part = false;
    if (!stopped && !kept.empty() && kept.front()->whole() > b.whole()) {
      const std::array<BoardCells, 4> turns = turnsOf(b.cells);
      part = std::any_of(kept.begin(), kept.end(), [&](const FoundBoard* k) {
        return k->whole() > b.whole() && partOf(turns, k->cells);
      });
    }
    if (!part) {
      kept.push_back(&b);
    }
  }

  out.complete = out.complete && !stopped;

  const Point own = nearZero(g, tray.shiftX, tray.shiftY);
  const std::string ownKey = foundBoard(boardAt(g, own.x, own.y), own.x, own.y).key;
  for (const FoundBoard* b : kept) {
    if (b->key == ownKey) {
      out.inUse = out.boards.size();
    }
    const Point canon = canonicalShift(tray, {b->shiftX, b->shiftY});
    out.boards.push_back({canon.x, canon.y, static_cast<unsigned int>(b->whole())});
  }
  return out;
}

}  // namespace bt::tilted
