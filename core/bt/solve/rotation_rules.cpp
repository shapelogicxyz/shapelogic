// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationrules.cpp. The floating point work makes the
// same calls in the same order as the original, so the answers are the
// same bit for bit (the tests pin them with a hash).
#include "bt/solve/rotation_rules.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>

namespace bt {

namespace {

using Cell = RotationRules::Cell;
using Pivot = RotationRules::Pivot;

struct CellLess {
  bool operator()(const Cell& a, const Cell& b) const {
    if (a.x != b.x) {
      return a.x < b.x;
    }
    if (a.y != b.y) {
      return a.y < b.y;
    }
    return a.z < b.z;
  }
};

using CellSet = std::set<Cell, CellLess>;

// A set of cells for the rules: a dense grid over their bounding box for
// lookups, and the cells, each once, for scans. The rules only ask yes/no
// questions, so the scan order does not matter.
class Occupancy {
 public:
  explicit Occupancy(const std::vector<Cell>& in) {
    if (in.empty()) {
      return;
    }
    int x1 = in[0].x;
    int y1 = in[0].y;
    int z1 = in[0].z;
    x0_ = x1;
    y0_ = y1;
    z0_ = z1;
    for (const Cell& c : in) {
      x0_ = std::min(x0_, c.x);
      x1 = std::max(x1, c.x);
      y0_ = std::min(y0_, c.y);
      y1 = std::max(y1, c.y);
      z0_ = std::min(z0_, c.z);
      z1 = std::max(z1, c.z);
    }
    w_ = x1 - x0_ + 1;
    h_ = y1 - y0_ + 1;
    d_ = z1 - z0_ + 1;
    grid_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) *
                     static_cast<std::size_t>(d_),
                 0);
    cells.reserve(in.size());
    for (const Cell& c : in) {
      unsigned char& g = grid_[index(c.x, c.y, c.z)];
      if (g == 0) {
        g = 1;
        cells.push_back(c);
      }
    }
  }

  [[nodiscard]] bool has(int x, int y, int z) const {
    if (x < x0_ || y < y0_ || z < z0_ || x >= x0_ + w_ || y >= y0_ + h_ || z >= z0_ + d_) {
      return false;
    }
    return grid_[index(x, y, z)] != 0;
  }
  [[nodiscard]] bool has(const Cell& c) const { return has(c.x, c.y, c.z); }
  [[nodiscard]] std::size_t size() const { return cells.size(); }

  std::vector<Cell> cells;

 private:
  [[nodiscard]] std::size_t index(int x, int y, int z) const {
    return (static_cast<std::size_t>(z - z0_) * static_cast<std::size_t>(h_) +
            static_cast<std::size_t>(y - y0_)) *
               static_cast<std::size_t>(w_) +
           static_cast<std::size_t>(x - x0_);
  }

  int x0_ = 0, y0_ = 0, z0_ = 0;
  int w_ = 0, h_ = 0, d_ = 0;
  std::vector<unsigned char> grid_;
};

// (u, v, a) as a cell, for a turn about `axis`: u and v in the plane, a
// along the axis.
Cell cellAt(unsigned int axis, int u, int v, int a) {
  if (axis == 0) {
    return Cell(a, u, v);
  }
  if (axis == 1) {
    return Cell(u, a, v);
  }
  return Cell(u, v, a);
}

// Steps along a 90 degree turn (the Fortran solver's NrotSteps).
constexpr int kArcSteps = 24;
constexpr double kBevelRemove = 0.0405;
constexpr double kHalfThick = 0.5 - kBevelRemove;

double pivotWorld(int doubled) { return doubled * 0.5 + 0.5; }

// Cell (i, j, k) is [i, i+1] x [j, j+1] x [k, k+1]. A unit cube centred at
// C overlaps the cells from floor(C - 0.5) to floor(C + 0.5 - eps).
void addOverlappedCells(double cx, double cy, double cz, CellSet& out) {
  const double eps = 1e-9;
  const auto x0 = static_cast<int>(std::floor(cx - 0.5 + eps));
  const auto x1 = static_cast<int>(std::floor(cx + 0.5 - eps));
  const auto y0 = static_cast<int>(std::floor(cy - 0.5 + eps));
  const auto y1 = static_cast<int>(std::floor(cy + 0.5 - eps));
  const auto z0 = static_cast<int>(std::floor(cz - 0.5 + eps));
  const auto z1 = static_cast<int>(std::floor(cz + 0.5 - eps));

  for (int x = x0; x <= x1; x++) {
    for (int y = y0; y <= y1; y++) {
      for (int z = z0; z <= z1; z++) {
        out.insert(Cell(x, y, z));
      }
    }
  }
}

// (u, v) turned by theta towards a +90 (sense 0) or -90 (sense 1) turn.
void rotateInPlane(double u, double v, double theta, unsigned int sense, double* uOut,
                   double* vOut) {
  const double c = std::cos(theta);
  const double s = std::sin(theta);
  if (sense == 0) {
    *uOut = u * c - v * s;
    *vOut = u * s + v * c;
  } else {
    *uOut = u * c + v * s;
    *vOut = -u * s + v * c;
  }
}

// A cell's centre, relative to the pivot, turned by theta.
void rotatedCenter(double dx, double dy, double dz, unsigned int axis, unsigned int sense,
                   double theta, double* ox, double* oy, double* oz) {
  if (axis == 0) {
    double ny = 0;
    double nz = 0;
    rotateInPlane(dy, dz, theta, sense, &ny, &nz);
    *ox = dx;
    *oy = ny;
    *oz = nz;
  } else if (axis == 1) {
    if (sense == 0) {
      *ox = dx * std::cos(theta) + dz * std::sin(theta);
      *oy = dy;
      *oz = -dx * std::sin(theta) + dz * std::cos(theta);
    } else {
      *ox = dx * std::cos(theta) - dz * std::sin(theta);
      *oy = dy;
      *oz = dx * std::sin(theta) + dz * std::cos(theta);
    }
  } else {
    double nx = 0;
    double ny = 0;
    rotateInPlane(dx, dy, theta, sense, &nx, &ny);
    *ox = nx;
    *oy = ny;
    *oz = dz;
  }
}

// The four normals of the separating-axis test at one angle: both squares'
// edges (along the axes, and turned by the angle).
struct SatAxes {
  double c[4];
  double s[4];
  explicit SatAxes(double theta) : c{}, s{} {
    const double nrm[4] = {0.0, 1.5707963267948966, theta, theta + 1.5707963267948966};
    for (int r = 0; r < 4; r++) {
      c[r] = std::cos(nrm[r]);
      s[r] = std::sin(nrm[r]);
    }
  }
};

// Separating axes in 2D: true when the moving bevelled square overlaps the
// static one.
bool bevelSquaresOverlap(const double mc[4][2], const double sc[4][2], const SatAxes& axes) {
  for (int r = 0; r < 4; r++) {
    const double c = axes.c[r];
    const double s = axes.s[r];
    double min1 = 1e100;
    double max1 = -1e100;
    double min2 = 1e100;
    double max2 = -1e100;
    for (int i = 0; i < 4; i++) {
      const double d1 = mc[i][0] * c + mc[i][1] * s;
      const double d2 = sc[i][0] * c + sc[i][1] * s;
      min1 = d1 < min1 ? d1 : min1;
      max1 = d1 > max1 ? d1 : max1;
      min2 = d2 < min2 ? d2 : min2;
      max2 = d2 > max2 ? d2 : max2;
    }
    if (max1 < min2 || max2 < min1) {
      return false;
    }
  }
  return true;
}

void inPlaneFromWorld(double x, double y, double z, unsigned int axis, double* u, double* v) {
  if (axis == 0) {
    *u = y;
    *v = z;
  } else if (axis == 1) {
    *u = x;
    *v = z;
  } else {
    *u = x;
    *v = y;
  }
}

void bevelCornersWorld(const Cell& cell, unsigned int axis, double ht, double out[4][3]) {
  const double cx = cell.x + 0.5;
  const double cy = cell.y + 0.5;
  const double cz = cell.z + 0.5;
  const double su[4] = {-ht, -ht, ht, ht};
  const double sv[4] = {-ht, ht, -ht, ht};
  for (int i = 0; i < 4; i++) {
    if (axis == 0) {
      out[i][0] = cx;
      out[i][1] = cy + su[i];
      out[i][2] = cz + sv[i];
    } else if (axis == 1) {
      out[i][0] = cx + su[i];
      out[i][1] = cy;
      out[i][2] = cz + sv[i];
    } else {
      out[i][0] = cx + su[i];
      out[i][1] = cy + sv[i];
      out[i][2] = cz;
    }
  }
}

// One static cell as the arc sweep sees it: its layer along the axis, and
// its bevelled square in the plane of the turn, centre and corners.
struct StaticSquare {
  int layer = 0;
  double cu = 0;
  double cv = 0;
  double sc[4][2] = {};
};

// Every static cell's square, sorted by layer, for one axis.
std::vector<StaticSquare> staticSquares(const Occupancy& occ, unsigned int axis) {
  std::vector<StaticSquare> out(occ.size());
  for (std::size_t i = 0; i < occ.size(); i++) {
    const Cell& o = occ.cells[i];
    StaticSquare& q = out[i];
    q.layer = (axis == 0) ? o.x : ((axis == 1) ? o.y : o.z);
    double scorners[4][3];
    bevelCornersWorld(o, axis, kHalfThick, scorners);
    for (int k = 0; k < 4; k++) {
      inPlaneFromWorld(scorners[k][0], scorners[k][1], scorners[k][2], axis, &q.sc[k][0],
                       &q.sc[k][1]);
    }
    inPlaneFromWorld(o.x + 0.5, o.y + 0.5, o.z + 0.5, axis, &q.cu, &q.cv);
  }
  std::sort(out.begin(), out.end(),
            [](const StaticSquare& a, const StaticSquare& b) { return a.layer < b.layer; });
  return out;
}

// What the arc sweep needs of each angle it samples, worked out once.
struct ArcStep {
  double c;
  double s;
  double sin2;
  SatAxes axes[2];  // by sense
  explicit ArcStep(double theta)
      : c(std::cos(theta)),
        s(std::sin(theta)),
        sin2(std::sin(2.0 * theta)),
        axes{SatAxes(theta), SatAxes(-theta)} {}
};

// The angles, once (constant data, the same for every sweep).
const std::vector<ArcStep>& arcSteps() {
  static const std::vector<ArcStep> steps = [] {
    const double halfPi = 1.5707963267948966;
    std::vector<ArcStep> v;
    // entry 0 is not used: the sweep samples steps 1 .. kArcSteps - 1
    for (int s = 0; s < kArcSteps; s++) {
      v.emplace_back(halfPi * static_cast<double>(s) / static_cast<double>(kArcSteps));
    }
    return v;
  }();
  return steps;
}

// rotatedCenter with the angle's cosine and sine at hand.
void rotatedCenterCS(double dx, double dy, double dz, unsigned int axis, unsigned int sense,
                     double c, double s, double* ox, double* oy, double* oz) {
  if (axis == 0) {
    *ox = dx;
    if (sense == 0) {
      *oy = dy * c - dz * s;
      *oz = dy * s + dz * c;
    } else {
      *oy = dy * c + dz * s;
      *oz = -dy * s + dz * c;
    }
  } else if (axis == 1) {
    *oy = dy;
    if (sense == 0) {
      *ox = dx * c + dz * s;
      *oz = -dx * s + dz * c;
    } else {
      *ox = dx * c - dz * s;
      *oz = dx * s + dz * c;
    }
  } else {
    *oz = dz;
    if (sense == 0) {
      *ox = dx * c - dy * s;
      *oy = dx * s + dy * c;
    } else {
      *ox = dx * c + dy * s;
      *oy = -dx * s + dy * c;
    }
  }
}

// The largest wiggle half way: the third amount along a diagonal.
const double kWiggleMax = 0.378680 * 1.4142135623730951 + 0.001;
// Squares further apart than this, centre to centre, cannot touch (each
// reaches kHalfThick * sqrt(2), about 0.65, from its centre); with room to
// spare, so leaving them out never changes an answer.
constexpr double kNear = 1.36;

// One moving cell of a sweep: its bevelled square's corners and centre
// from the pivot, and the static squares it can come near at all.
struct ArcMover {
  double corner[4][3];
  double centre[3];
  std::size_t firstNear;
  std::size_t endNear;  // into ArcSweep::nearSquares
};

}  // namespace

// The moving cells of one sweep, laid out once for the plain turn and every
// wiggled one.
struct RotationRules::ArcSweep {
  std::vector<ArcMover> movers;
  std::vector<const StaticSquare*> nearSquares;
  // which of an angle's two sets of normals fits this turn (see arcClear)
  int axesSet = 0;
  // where the last turn tried was stopped: the next wiggle most often stops
  // there too, so it looks there first
  int hitStep = 0;
  std::size_t hitMover = 0;
  const StaticSquare* hitSquare = nullptr;
};

namespace {

using ArcSweep = RotationRules::ArcSweep;

// The 90 degree path with the Fortran solver's bevelled squares, with an
// optional wiggle in the plane (dshift * sin(2 theta) along one of 8
// directions), as its SimpleRot.
bool bevelArcClearOne(ArcSweep& sweep, const Pivot& pivot, unsigned int axis, unsigned int sense,
                      double dshift, int shifti, int shiftj) {
  const double pcx = pivotWorld(pivot.hx);
  const double pcy = pivotWorld(pivot.hy);
  const double pcz = pivotWorld(pivot.hz);
  const std::vector<ArcStep>& steps = arcSteps();

  // a moving cell's corners at one angle of this turn
  auto cornersAt = [&](const ArcMover& m, const ArcStep& step, double mc[4][2]) {
    const double w = dshift * step.sin2;
    const double wu = w * static_cast<double>(shifti);
    const double wv = w * static_cast<double>(shiftj);
    double wx = 0;
    double wy = 0;
    double wz = 0;
    if (axis == 0) {
      wy = wu;
      wz = wv;
    } else if (axis == 1) {
      wx = wu;
      wz = wv;
    } else {
      wx = wu;
      wy = wv;
    }
    for (int k = 0; k < 4; k++) {
      double ox = 0;
      double oy = 0;
      double oz = 0;
      rotatedCenterCS(m.corner[k][0], m.corner[k][1], m.corner[k][2], axis, sense, step.c, step.s,
                      &ox, &oy, &oz);
      inPlaneFromWorld(pcx + ox + wx, pcy + oy + wy, pcz + oz + wz, axis, &mc[k][0], &mc[k][1]);
    }
  };

  // any overlap at any angle stops the turn, so the order does not matter:
  // start where the last turn was stopped
  if (sweep.hitSquare != nullptr) {
    const ArcStep& step = steps[static_cast<std::size_t>(sweep.hitStep)];
    double mc[4][2];
    cornersAt(sweep.movers[sweep.hitMover], step, mc);
    if (bevelSquaresOverlap(mc, sweep.hitSquare->sc, step.axes[sweep.axesSet])) {
      return false;
    }
  }

  for (int s = 1; s < kArcSteps; s++) {
    const ArcStep& step = steps[static_cast<std::size_t>(s)];
    const SatAxes& axes = step.axes[sweep.axesSet];
    const double w = dshift * step.sin2;
    const double wu = w * static_cast<double>(shifti);
    const double wv = w * static_cast<double>(shiftj);

    for (std::size_t mi = 0; mi < sweep.movers.size(); mi++) {
      const ArcMover& m = sweep.movers[mi];
      double ox = 0;
      double oy = 0;
      double oz = 0;
      double mu = 0;
      double mv = 0;
      rotatedCenterCS(m.centre[0], m.centre[1], m.centre[2], axis, sense, step.c, step.s, &ox, &oy,
                      &oz);
      inPlaneFromWorld(pcx + ox, pcy + oy, pcz + oz, axis, &mu, &mv);
      mu += wu;
      mv += wv;

      // the corners at this angle, worked out for the first square near
      double mc[4][2];
      bool haveCorners = false;

      for (std::size_t qi = m.firstNear; qi < m.endNear; qi++) {
        const StaticSquare* q = sweep.nearSquares[qi];
        const double du = q->cu - mu;
        const double dv = q->cv - mv;
        if (du * du + dv * dv > kNear * kNear) {
          continue;
        }
        if (!haveCorners) {
          cornersAt(m, step, mc);
          haveCorners = true;
        }
        if (bevelSquaresOverlap(mc, q->sc, axes)) {
          sweep.hitStep = s;
          sweep.hitMover = mi;
          sweep.hitSquare = q;
          return false;
        }
      }
    }
  }

  return true;
}

bool arcClear(ArcSweep& sweep, const std::vector<StaticSquare>& squares,
              const std::vector<Cell>& startCells, const Pivot& pivot, unsigned int axis,
              unsigned int sense) {
  sweep.movers.clear();
  sweep.nearSquares.clear();
  sweep.hitSquare = nullptr;
  // The overlap test wants the moving square's edge normals: the angle
  // turned so far, signed by the sense. Seen in the plane's (u, v), a turn
  // about y runs the other way round from one about x or z (its plane is
  // (x, z), and +90 degrees takes z to x), so its sign is the other one.
  const bool plus = (sense == 0) != (axis == 1);
  sweep.axesSet = plus ? 0 : 1;

  const double pcx = pivotWorld(pivot.hx);
  const double pcy = pivotWorld(pivot.hy);
  const double pcz = pivotWorld(pivot.hz);
  double pu = 0;
  double pv = 0;
  inPlaneFromWorld(pcx, pcy, pcz, axis, &pu, &pv);

  for (const Cell& cell : startCells) {
    const int ck = (axis == 0) ? cell.x : ((axis == 1) ? cell.y : cell.z);
    auto lo = std::lower_bound(squares.begin(), squares.end(), ck,
                               [](const StaticSquare& q, int l) { return q.layer < l; });
    if (lo == squares.end() || lo->layer != ck) {
      continue;
    }

    // the cell's centre turns on a circle about the pivot, wiggle aside: a
    // static square can only come near when it lies about as far out
    double cu = 0;
    double cv = 0;
    inPlaneFromWorld(cell.x + 0.5, cell.y + 0.5, cell.z + 0.5, axis, &cu, &cv);
    const double radius = std::sqrt((cu - pu) * (cu - pu) + (cv - pv) * (cv - pv));
    const double reach = kNear + kWiggleMax;
    const double outerSq = (radius + reach) * (radius + reach);
    const double innerSq = radius > reach ? (radius - reach) * (radius - reach) : 0.0;

    ArcMover m{};
    m.firstNear = sweep.nearSquares.size();
    for (auto q = lo; q != squares.end() && q->layer == ck; ++q) {
      const double outSq = (q->cu - pu) * (q->cu - pu) + (q->cv - pv) * (q->cv - pv);
      if (outSq <= outerSq && outSq >= innerSq) {
        sweep.nearSquares.push_back(&*q);
      }
    }
    m.endNear = sweep.nearSquares.size();
    if (m.endNear == m.firstNear) {
      continue;
    }

    double corners[4][3];
    bevelCornersWorld(cell, axis, kHalfThick, corners);
    for (int k = 0; k < 4; k++) {
      m.corner[k][0] = corners[k][0] - pcx;
      m.corner[k][1] = corners[k][1] - pcy;
      m.corner[k][2] = corners[k][2] - pcz;
    }
    m.centre[0] = cell.x + 0.5 - pcx;
    m.centre[1] = cell.y + 0.5 - pcy;
    m.centre[2] = cell.z + 0.5 - pcz;
    sweep.movers.push_back(m);
  }

  if (bevelArcClearOne(sweep, pivot, axis, sense, 0.0, 0, 0)) {
    return true;
  }

  const double amounts[3] = {std::sqrt(2.0 * kHalfThick * kHalfThick) - 0.5,
                             (std::sqrt(2.0) - 1.0) * 0.5 + 0.025, 0.378680};
  static constexpr int kWiggleDirs[8][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                                            {1, 1},  {-1, 1}, {1, -1}, {-1, -1}};

  for (const double amount : amounts) {
    for (const auto& d : kWiggleDirs) {
      if (bevelArcClearOne(sweep, pivot, axis, sense, amount, d[0], d[1])) {
        return true;
      }
    }
  }

  return false;
}

int axialCoord(const Cell& c, unsigned int axis) {
  if (axis == 0) {
    return c.x;
  }
  if (axis == 1) {
    return c.y;
  }
  return c.z;
}

// The directions in the plane of a turn: x to (y, z), y to (x, z), z to
// (x, y).
void inPlaneUV(unsigned int axis, int* ux, int* uy, int* uz, int* vx, int* vy, int* vz) {
  if (axis == 0) {
    *ux = 0;
    *uy = 1;
    *uz = 0;
    *vx = 0;
    *vy = 0;
    *vz = 1;
  } else if (axis == 1) {
    *ux = 1;
    *uy = 0;
    *uz = 0;
    *vx = 0;
    *vy = 0;
    *vz = 1;
  } else {
    *ux = 1;
    *uy = 0;
    *uz = 0;
    *vx = 0;
    *vy = 1;
    *vz = 0;
  }
}

int inPlaneU(const Cell& c, unsigned int axis) { return axis == 0 ? c.y : c.x; }

int inPlaneV(const Cell& c, unsigned int axis) { return axis == 2 ? c.y : c.z; }

// The moving cell lies on the pivot's column (the same coordinates in the
// plane).
bool onPivotColumn(const Cell& c, const Pivot& pivot, unsigned int axis) {
  if (axis == 0) {
    return c.y * 2 == pivot.hy && c.z * 2 == pivot.hz;
  }
  if (axis == 1) {
    return c.x * 2 == pivot.hx && c.z * 2 == pivot.hz;
  }
  return c.x * 2 == pivot.hx && c.y * 2 == pivot.hy;
}

// Some moving cell on layer `a` has another piece's cell face to face.
bool layerTouchesOther(const Occupancy& occ, const std::vector<Cell>& startCells,
                       unsigned int axis, int a) {
  static constexpr int kFaces[6][3] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
                                       {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};

  for (const Cell& c : startCells) {
    if (axialCoord(c, axis) != a) {
      continue;
    }
    for (const auto& f : kFaces) {
      if (occ.has(c.x + f[0], c.y + f[1], c.z + f[2])) {
        return true;
      }
    }
  }
  return false;
}

// The other pieces' cells beside one moving cell, in the plane (-U, +U,
// -V, +V), for the axis-cross rule.
void axisCrossScanCell(const Cell& c, unsigned int axis, const Occupancy& occ,
                       const Occupancy& start, bool* negU, bool* posU, bool* negV, bool* posV,
                       CellSet* slotOccupied, CellSet* restricted) {
  Cell nu;
  Cell pu;
  Cell nv;
  Cell pv;

  if (axis == 0) {
    nu = Cell(c.x, c.y - 1, c.z);
    pu = Cell(c.x, c.y + 1, c.z);
    nv = Cell(c.x, c.y, c.z - 1);
    pv = Cell(c.x, c.y, c.z + 1);
  } else if (axis == 1) {
    nu = Cell(c.x - 1, c.y, c.z);
    pu = Cell(c.x + 1, c.y, c.z);
    nv = Cell(c.x, c.y, c.z - 1);
    pv = Cell(c.x, c.y, c.z + 1);
  } else {
    nu = Cell(c.x - 1, c.y, c.z);
    pu = Cell(c.x + 1, c.y, c.z);
    nv = Cell(c.x, c.y - 1, c.z);
    pv = Cell(c.x, c.y + 1, c.z);
  }

  const Cell slots[4] = {nu, pu, nv, pv};
  bool* flags[4] = {negU, posU, negV, posV};

  for (int i = 0; i < 4; i++) {
    if (start.has(slots[i])) {
      continue;
    }
    if (occ.has(slots[i])) {
      *flags[i] = true;
      if (slotOccupied != nullptr) {
        slotOccupied->insert(slots[i]);
      }
    } else if (restricted != nullptr) {
      restricted->insert(slots[i]);
    }
  }
}

// Same-layer sandwich: a moving cell must not have other pieces' cells on
// both opposite sides in the plane (the Fortran SimpleRot test). The moving
// body's own cells are not in `occ`, so they do not pinch.
bool inPlanePinchClear(const Occupancy& occ, const std::vector<Cell>& cells, unsigned int axis,
                       CellSet* blocking) {
  int ux = 0;
  int uy = 0;
  int uz = 0;
  int vx = 0;
  int vy = 0;
  int vz = 0;
  inPlaneUV(axis, &ux, &uy, &uz, &vx, &vy, &vz);

  bool pinched = false;

  for (const Cell& c : cells) {
    const Cell nu(c.x - ux, c.y - uy, c.z - uz);
    const Cell pu(c.x + ux, c.y + uy, c.z + uz);
    const Cell nv(c.x - vx, c.y - vy, c.z - vz);
    const Cell pv(c.x + vx, c.y + vy, c.z + vz);

    const bool negU = occ.has(nu);
    const bool posU = occ.has(pu);
    const bool negV = occ.has(nv);
    const bool posV = occ.has(pv);

    if (negU && posU) {
      pinched = true;
      if (blocking == nullptr) {
        return false;
      }
      blocking->insert(nu);
      blocking->insert(pu);
    }
    if (negV && posV) {
      pinched = true;
      if (blocking == nullptr) {
        return false;
      }
      blocking->insert(nv);
      blocking->insert(pv);
    }
  }

  return !pinched;
}

// Perpendicular-plane capture (Fortran PerPplane 1 and 2): on a slice of
// constant U (or V) with at least two moving and two other cells, other
// pieces' cells on opposite sides in V (or U), at the same coordinate along
// the axis, refuse the axis.
bool perpPlaneClear(const Occupancy& occ, const std::vector<Cell>& startCells, unsigned int axis,
                    CellSet* blocking) {
  if (startCells.size() < 2 || occ.size() < 2) {
    return true;
  }

  int uMin = inPlaneU(startCells[0], axis);
  int uMax = uMin;
  int vMin = inPlaneV(startCells[0], axis);
  int vMax = vMin;
  for (std::size_t i = 1; i < startCells.size(); i++) {
    const int u = inPlaneU(startCells[i], axis);
    const int v = inPlaneV(startCells[i], axis);
    uMin = std::min(uMin, u);
    uMax = std::max(uMax, u);
    vMin = std::min(vMin, v);
    vMax = std::max(vMax, v);
  }

  auto at = [](int k, int lo) { return static_cast<std::size_t>(k - lo); };

  // moving and other cells on each slice, counted once
  std::vector<unsigned int> moveU(at(uMax, uMin) + 1, 0);
  std::vector<unsigned int> statU(at(uMax, uMin) + 1, 0);
  std::vector<unsigned int> moveV(at(vMax, vMin) + 1, 0);
  std::vector<unsigned int> statV(at(vMax, vMin) + 1, 0);
  for (const Cell& c : startCells) {
    moveU[at(inPlaneU(c, axis), uMin)]++;
    moveV[at(inPlaneV(c, axis), vMin)]++;
  }
  for (const Cell& o : occ.cells) {
    const int u = inPlaneU(o, axis);
    const int v = inPlaneV(o, axis);
    if (u >= uMin && u <= uMax) {
      statU[at(u, uMin)]++;
    }
    if (v >= vMin && v <= vMax) {
      statV[at(v, vMin)]++;
    }
  }

  bool captured = false;

  // PerPplane 1: slices of constant U, walls opposite in V; PerPplane 2:
  // slices of constant V, walls opposite in U. A wall is another piece's
  // cell on a moving cell's slice and layer, next to it across the slice.
  for (int plane = 0; plane < 2; plane++) {
    const bool alongU = plane == 0;
    const int lo = alongU ? uMin : vMin;
    const int hi = alongU ? uMax : vMax;
    for (int k = lo; k <= hi; k++) {
      const unsigned int nMove = alongU ? moveU[at(k, uMin)] : moveV[at(k, vMin)];
      const unsigned int nStat = alongU ? statU[at(k, uMin)] : statV[at(k, vMin)];
      if (nMove < 2 || nStat < 2) {
        continue;
      }

      bool neg = false;
      bool pos = false;
      CellSet hits;
      for (const Cell& c : startCells) {
        if ((alongU ? inPlaneU(c, axis) : inPlaneV(c, axis)) != k) {
          continue;
        }
        const int ck = axialCoord(c, axis);
        const int across = alongU ? inPlaneV(c, axis) : inPlaneU(c, axis);
        for (int side = -1; side <= 1; side += 2) {
          const Cell wall =
              alongU ? cellAt(axis, k, across + side, ck) : cellAt(axis, across + side, k, ck);
          if (!occ.has(wall)) {
            continue;
          }
          (side < 0 ? neg : pos) = true;
          if (blocking != nullptr) {
            hits.insert(wall);
          }
        }
      }
      if (neg && pos) {
        captured = true;
        if (blocking == nullptr) {
          return false;
        }
        blocking->insert(hits.begin(), hits.end());
      }
    }
  }

  return !captured;
}

// Other pieces' cells beside the moving cells on the pivot's column, in the
// plane, gathered over the layers along the axis that touch another piece:
// opposites on two different such layers refuse the turn (opposites on one
// layer are the sandwich rule's).
bool axisCrossClear(const Occupancy& occ, const std::vector<Cell>& startCells,
                    const Occupancy& start, const Pivot& pivot, unsigned int axis) {
  if (startCells.empty()) {
    return true;
  }

  int aMin = axialCoord(startCells[0], axis);
  int aMax = aMin;
  for (std::size_t i = 1; i < startCells.size(); i++) {
    const int a = axialCoord(startCells[i], axis);
    aMin = std::min(aMin, a);
    aMax = std::max(aMax, a);
  }

  bool seenNegU = false;
  bool seenPosU = false;
  bool seenNegV = false;
  bool seenPosV = false;

  for (int a = aMin; a <= aMax; a++) {
    if (!layerTouchesOther(occ, startCells, axis, a)) {
      continue;
    }

    bool layerNegU = false;
    bool layerPosU = false;
    bool layerNegV = false;
    bool layerPosV = false;

    for (const Cell& c : startCells) {
      if (axialCoord(c, axis) != a) {
        continue;
      }
      if (!onPivotColumn(c, pivot, axis)) {
        continue;
      }
      axisCrossScanCell(c, axis, occ, start, &layerNegU, &layerPosU, &layerNegV, &layerPosV,
                        nullptr, nullptr);
    }

    if ((layerPosU && seenNegU) || (layerNegU && seenPosU) || (layerPosV && seenNegV) ||
        (layerNegV && seenPosV)) {
      return false;
    }

    seenNegU |= layerNegU;
    seenPosU |= layerPosU;
    seenNegV |= layerNegV;
    seenPosV |= layerPosV;
  }

  return true;
}

}  // namespace

struct RotationRules::OccupancyCache {
  std::vector<Cell> input;
  Occupancy occ{std::vector<Cell>()};
};

struct RotationRules::Prepared {
  std::vector<Cell> occupied;
  std::vector<Cell> start;
  Occupancy occ{std::vector<Cell>()};
  Occupancy startOcc{std::vector<Cell>()};
  // the others' squares for the arc sweep, per axis, made when first needed
  std::array<std::vector<StaticSquare>, 3> squares;
  std::array<bool, 3> haveSquares = {false, false, false};
  std::vector<Cell> end;
};

RotationRules::RotationRules() : sweep_(std::make_unique<ArcSweep>()) {}

RotationRules::~RotationRules() = default;

namespace {

// The occupancy of `cells`, from the cache when they are the ones it holds.
const Occupancy& cachedOccupancy(std::unique_ptr<RotationRules::OccupancyCache>& cache,
                                 const std::vector<Cell>& cells) {
  auto same = [](const Cell& a, const Cell& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
  if (!cache) {
    cache = std::make_unique<RotationRules::OccupancyCache>();
  }
  if (cache->input.size() != cells.size() ||
      !std::equal(cells.begin(), cells.end(), cache->input.begin(), same)) {
    cache->input = cells;
    cache->occ = Occupancy(cells);
  }
  return cache->occ;
}

}  // namespace

bool RotationRules::allowRotation(const std::vector<Cell>& occupied,
                                  const std::vector<Cell>& startCells,
                                  const std::vector<Cell>& endCells, const Pivot& pivot,
                                  unsigned int axis, unsigned int sense) const {
  const Occupancy& occ = cachedOccupancy(occCache_, occupied);

  // the turned cells must not overlap other pieces
  for (const Cell& e : endCells) {
    if (occ.has(e)) {
      return false;
    }
  }

  // sandwiched before the turn, or after it
  if (!inPlanePinchClear(occ, startCells, axis, nullptr)) {
    return false;
  }
  if (!inPlanePinchClear(occ, endCells, axis, nullptr)) {
    return false;
  }

  // walls opposite in planes across the plane of the turn
  if (!perpPlaneClear(occ, startCells, axis, nullptr)) {
    return false;
  }

  // opposite neighbours across layers that touch another piece
  if (!axisCrossClear(occ, startCells, Occupancy(startCells), pivot, axis)) {
    return false;
  }

  // the path must not clip another piece
  return arcClear(*sweep_, staticSquares(occ, axis), startCells, pivot, axis, sense);
}

bool RotationRules::axisBlocked(const std::vector<Cell>& occupied,
                                const std::vector<Cell>& startCells, unsigned int axis) const {
  const Occupancy& occ = cachedOccupancy(occCache_, occupied);

  return !inPlanePinchClear(occ, startCells, axis, nullptr) ||
         !perpPlaneClear(occ, startCells, axis, nullptr);
}

bool RotationRules::rotateCell(const Cell& cell, const Pivot& pivot, unsigned int axis,
                               unsigned int sense, Cell& out) {
  const int dx = cell.x * 2 - pivot.hx;
  const int dy = cell.y * 2 - pivot.hy;
  const int dz = cell.z * 2 - pivot.hz;
  int rx = dx;
  int ry = dy;
  int rz = dz;
  if (axis == 0) {
    if (sense == 0) {
      ry = -dz;
      rz = dy;
    } else {
      ry = dz;
      rz = -dy;
    }
  } else if (axis == 1) {
    if (sense == 0) {
      rx = dz;
      rz = -dx;
    } else {
      rx = -dz;
      rz = dx;
    }
  } else {
    if (sense == 0) {
      rx = -dy;
      ry = dx;
    } else {
      rx = dy;
      ry = -dx;
    }
  }
  const int nx = pivot.hx + rx;
  const int ny = pivot.hy + ry;
  const int nz = pivot.hz + rz;
  if (((nx | ny | nz) & 1) != 0) {
    return false;
  }
  out = Cell(nx / 2, ny / 2, nz / 2);
  return true;
}

void RotationRules::setBodies(const std::vector<Cell>& occupied,
                              const std::vector<Cell>& startCells) {
  if (!prep_) {
    prep_ = std::make_unique<Prepared>();
  }
  prep_->occupied = occupied;
  prep_->start = startCells;
  prep_->occ = Occupancy(occupied);
  prep_->startOcc = Occupancy(startCells);
  prep_->haveSquares = {false, false, false};
}

bool RotationRules::preparedAxisBlocked(unsigned int axis) const {
  return !inPlanePinchClear(prep_->occ, prep_->start, axis, nullptr) ||
         !perpPlaneClear(prep_->occ, prep_->start, axis, nullptr);
}

bool RotationRules::allowPrepared(const Pivot& pivot, unsigned int axis, unsigned int sense,
                                  bool axisClear) const {
  Prepared& p = *prep_;

  // the turned cells, stopping at the first that lands on another piece
  p.end.clear();
  for (const Cell& c : p.start) {
    Cell e;
    if (!rotateCell(c, pivot, axis, sense, e)) {
      return false;
    }
    if (p.occ.has(e)) {
      return false;
    }
    p.end.push_back(e);
  }

  if (!axisClear && preparedAxisBlocked(axis)) {
    return false;
  }
  if (!inPlanePinchClear(p.occ, p.end, axis, nullptr)) {
    return false;
  }
  if (!axisCrossClear(p.occ, p.start, p.startOcc, pivot, axis)) {
    return false;
  }
  if (!p.haveSquares[axis]) {
    p.squares[axis] = staticSquares(p.occ, axis);
    p.haveSquares[axis] = true;
  }
  return arcClear(*sweep_, p.squares[axis], p.start, pivot, axis, sense);
}

void RotationRules::collectDebugConflictCells(const std::vector<Cell>& occupied,
                                              const std::vector<Cell>& startCells,
                                              const Pivot& pivot, unsigned int axis,
                                              unsigned int sense, std::vector<Cell>& outBlocking,
                                              std::vector<Cell>& outClearance,
                                              std::vector<Cell>& outRestricted) const {
  outBlocking.clear();
  outClearance.clear();
  outRestricted.clear();

  const Occupancy occ(occupied);
  const Occupancy start(startCells);

  CellSet blocking;
  CellSet clearance;
  CellSet restricted;

  // the arc: every cell the turning cubes pass through that is not a start
  // cell is clearance; those another piece holds block
  {
    const double pcx = pivotWorld(pivot.hx);
    const double pcy = pivotWorld(pivot.hy);
    const double pcz = pivotWorld(pivot.hz);
    const double halfPi = 1.5707963267948966;

    for (const Cell& cell : startCells) {
      const double dx = (cell.x + 0.5) - pcx;
      const double dy = (cell.y + 0.5) - pcy;
      const double dz = (cell.z + 0.5) - pcz;

      // a cell on the axis turns in place
      if (axis == 0 && dy == 0 && dz == 0) {
        continue;
      }
      if (axis == 1 && dx == 0 && dz == 0) {
        continue;
      }
      if (axis == 2 && dx == 0 && dy == 0) {
        continue;
      }

      for (int s = 1; s < kArcSteps; s++) {
        const double theta = halfPi * static_cast<double>(s) / static_cast<double>(kArcSteps);
        double ox = 0;
        double oy = 0;
        double oz = 0;
        rotatedCenter(dx, dy, dz, axis, sense, theta, &ox, &oy, &oz);

        CellSet hit;
        addOverlappedCells(pcx + ox, pcy + oy, pcz + oz, hit);
        for (const Cell& h : hit) {
          if (start.has(h)) {
            continue;
          }
          clearance.insert(h);
          if (occ.has(h)) {
            blocking.insert(h);
          }
        }
      }
    }
  }

  // sandwiching and capturing cells
  inPlanePinchClear(occ, startCells, axis, &blocking);
  perpPlaneClear(occ, startCells, axis, &blocking);

  // axis-cross: the slots beside the pivot column's moving cells on
  // touching layers; opposition across layers marks blockers
  if (!startCells.empty()) {
    int aMin = axialCoord(startCells[0], axis);
    int aMax = aMin;
    for (std::size_t i = 1; i < startCells.size(); i++) {
      const int a = axialCoord(startCells[i], axis);
      aMin = std::min(aMin, a);
      aMax = std::max(aMax, a);
    }

    bool seenNegU = false;
    bool seenPosU = false;
    bool seenNegV = false;
    bool seenPosV = false;
    CellSet slotOccupied;

    for (int a = aMin; a <= aMax; a++) {
      if (!layerTouchesOther(occ, startCells, axis, a)) {
        continue;
      }

      bool layerNegU = false;
      bool layerPosU = false;
      bool layerNegV = false;
      bool layerPosV = false;

      for (const Cell& c : startCells) {
        if (axialCoord(c, axis) != a) {
          continue;
        }
        if (!onPivotColumn(c, pivot, axis)) {
          continue;
        }
        axisCrossScanCell(c, axis, occ, start, &layerNegU, &layerPosU, &layerNegV, &layerPosV,
                          &slotOccupied, &restricted);
      }

      const bool cross = (layerPosU && seenNegU) || (layerNegU && seenPosU) ||
                         (layerPosV && seenNegV) || (layerNegV && seenPosV);
      if (cross) {
        blocking.insert(slotOccupied.begin(), slotOccupied.end());
      }

      seenNegU |= layerNegU;
      seenPosU |= layerPosU;
      seenNegV |= layerNegV;
      seenPosV |= layerPosV;
    }

    clearance.insert(slotOccupied.begin(), slotOccupied.end());
  }

  outBlocking.assign(blocking.begin(), blocking.end());
  for (const Cell& c : clearance) {
    if (!blocking.contains(c)) {
      outClearance.push_back(c);
    }
  }
  for (const Cell& c : restricted) {
    if (!blocking.contains(c) && !clearance.contains(c)) {
      outRestricted.push_back(c);
    }
  }
}

}  // namespace bt
