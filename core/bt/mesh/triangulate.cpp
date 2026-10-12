// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/triangulate.h (cubeMesh::triangulateLoops).
#include "bt/mesh/triangulate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace bt::mesh {

namespace {

constexpr double kTol = 1e-6;

// The geometric tests, by polygon position (the keyhole polygon repeats
// vertices and whole edges along its bridges).
struct Geo {
  const std::vector<Point2>& pts;

  [[nodiscard]] double X(int v) const { return pts[static_cast<std::size_t>(v)].x; }
  [[nodiscard]] double Y(int v) const { return pts[static_cast<std::size_t>(v)].y; }
  [[nodiscard]] static bool same(double ax, double ay, double bx, double by) {
    return std::fabs(ax - bx) < kTol && std::fabs(ay - by) < kTol;
  }
  // signed distance of (px,py) from the line a->b (positive left)
  [[nodiscard]] static double sdist(double ax, double ay, double bx, double by, double px,
                                    double py) {
    const double sx = bx - ax;
    const double sy = by - ay;
    const double l = std::sqrt((sx * sx) + (sy * sy));
    if (l < 1e-300) {
      return std::sqrt(((px - ax) * (px - ax)) + ((py - ay) * (py - ay)));
    }
    return ((sx * (py - ay)) - (sy * (px - ax))) / l;
  }
  // Does the interior wedge of polygon position q (prev p, next n)
  // contain direction d? Inclusive of the wedge's edges.
  [[nodiscard]] static bool wedge(double qx, double qy, double px, double py, double nx, double ny,
                                  double dx, double dy) {
    double nxx = nx - qx;
    double nxy = ny - qy;
    double prx = px - qx;
    double pry = py - qy;
    const double ln = std::sqrt((nxx * nxx) + (nxy * nxy));
    const double lp = std::sqrt((prx * prx) + (pry * pry));
    const double ld = std::sqrt((dx * dx) + (dy * dy));
    if (ln < 1e-300 || lp < 1e-300 || ld < 1e-300) {
      return false;
    }
    nxx /= ln;
    nxy /= ln;
    prx /= lp;
    pry /= lp;
    dx /= ld;
    dy /= ld;
    const double cnd = (nxx * dy) - (nxy * dx);   // cross(next dir, d)
    const double cdp = (dx * pry) - (dy * prx);   // cross(d, prev dir)
    const double cnp = (nxx * pry) - (nxy * prx); // cross(next dir, prev dir): >0 convex
    const double dnp = (nxx * prx) + (nxy * pry);
    const double a = 1e-7;
    if (cnp > a || (std::fabs(cnp) <= a && dnp < 0)) {  // convex or straight
      return cnd > -a && cdp > -a;
    }
    return !(cnd < a && cdp < a);  // reflex: not in the exterior wedge
  }
};

// position q of a polygon of n positions, wrapped
int wrap(int q, int n) { return ((q % n) + n) % n; }

}  // namespace

bool triangulateLoops(const std::vector<Point2>& pts, const std::vector<std::vector<int>>& loops,
                      std::vector<int>& tris, const char*& why) {
  tris.clear();
  if (loops.empty() || loops[0].size() < 3) {
    return true;
  }
  const auto P = [&](int v) -> const Point2& { return pts[static_cast<std::size_t>(v)]; };
  std::vector<int> poly = loops[0];
  bool ok = true;
  why = "";
  const Geo G{pts};
  const auto at = [&](const std::vector<int>& p, int q) { return p[static_cast<std::size_t>(q)]; };

  // holes sorted by rightmost vertex, rightmost first
  std::vector<std::pair<double, int>> order;
  for (std::size_t h = 1; h < loops.size(); h++) {
    if (loops[h].size() < 3) {
      continue;
    }
    double mx = -1e300;
    for (const int v : loops[h]) {
      mx = std::max(mx, P(v).x);
    }
    order.emplace_back(-mx, static_cast<int>(h));
  }
  std::sort(order.begin(), order.end());
  for (std::size_t oq = 0; oq < order.size() && ok; oq++) {
    const std::vector<int>& H = loops[static_cast<std::size_t>(order[oq].second)];
    const int hs = static_cast<int>(H.size());
    int mi = 0;
    for (int i = 1; i < hs; i++) {
      if (P(at(H, i)).x > P(at(H, mi)).x) {
        mi = i;
      }
    }
    const double Mx = P(at(H, mi)).x;
    const double My = P(at(H, mi)).y;
    int n = static_cast<int>(poly.size());
    int Pi = -1;
    // A hole vertex in the interior of an outline edge (the hole touches
    // the outline there without a vertex): that edge is split there, so
    // the touch is at a polygon vertex. Every hole vertex, not just M: a
    // tiny hole touching the outline at a vertex other than its rightmost
    // one leaves a T-junction the clipper gets stuck on.
    for (int hv = 0; hv < hs; hv++) {
      const double Hx = P(at(H, hv)).x;
      const double Hy = P(at(H, hv)).y;
      for (int e = 0; e < n; e++) {
        const int a = at(poly, e);
        const int b = at(poly, (e + 1) % n);
        const double ax = G.X(a);
        const double ay = G.Y(a);
        const double bx = G.X(b);
        const double by = G.Y(b);
        const double ex = bx - ax;
        const double ey = by - ay;
        const double el = std::sqrt((ex * ex) + (ey * ey));
        if (el < kTol) {
          continue;
        }
        const double t = (((Hx - ax) * ex) + ((Hy - ay) * ey)) / (el * el);
        if (t * el <= kTol || (1 - t) * el <= kTol) {
          continue;
        }
        if (std::fabs(Geo::sdist(ax, ay, bx, by, Hx, Hy)) >= kTol) {
          continue;
        }
        poly.insert(poly.begin() + (e + 1), at(H, hv));
        n++;
        break;
      }
    }
    // The hole touches the polygon at M itself: a zero-length bridge,
    // spliced at the copy whose wedge holds the hole's outgoing edge (the
    // hole lies inside that copy's interior).
    {
      const double hx = P(at(H, (mi + 1) % hs)).x;
      const double hy = P(at(H, (mi + 1) % hs)).y;
      for (int q = 0; q < n && Pi < 0; q++) {
        if (!Geo::same(G.X(at(poly, q)), G.Y(at(poly, q)), Mx, My)) {
          continue;
        }
        const int pr = at(poly, wrap(q - 1, n));
        const int nx = at(poly, (q + 1) % n);
        if (Geo::wedge(Mx, My, G.X(pr), G.Y(pr), G.X(nx), G.Y(nx), hx - Mx, hy - My)) {
          Pi = q;
        }
      }
    }
    int hitE = -1;
    double hitX = 1e300;
    double hitT = 0;
    bool found = false;
    double Rx = Mx;
    double Ry = My;
    if (Pi < 0) {
      // The closest edge hit by the ray +x from M. Only edges crossed
      // upward count: the polygon is CCW, so their interior side faces M
      // (the downward twin of a bridge faces away).
      for (int e = 0; e < n; e++) {
        const int a = at(poly, e);
        const int b = at(poly, (e + 1) % n);
        const double ax = G.X(a);
        const double ay = G.Y(a);
        const double bx = G.X(b);
        const double by = G.Y(b);
        if (!(ay <= My && My < by)) {
          continue;
        }
        const double t = (My - ay) / (by - ay);
        const double x = ax + (t * (bx - ax));
        if (x < Mx - kTol) {
          continue;
        }
        if (x < hitX) {
          hitX = x;
          hitE = e;
          hitT = t;
        }
      }
      if (hitE < 0) {
        ok = false;
        why = "hole outside the outline";
        break;
      }
      const int e0 = hitE;
      const int e1 = (hitE + 1) % n;
      const double el = std::sqrt(
          ((G.X(at(poly, e1)) - G.X(at(poly, e0))) * (G.X(at(poly, e1)) - G.X(at(poly, e0)))) +
          ((G.Y(at(poly, e1)) - G.Y(at(poly, e0))) * (G.Y(at(poly, e1)) - G.Y(at(poly, e0)))));
      if (hitT * el < kTol) {
        Pi = e0;  // ray through a vertex: bridge to it
      } else if ((1 - hitT) * el < kTol) {
        Pi = e1;
      } else {
        Pi = (G.X(at(poly, e0)) > G.X(at(poly, e1))) ? e0 : e1;
      }
      Rx = G.X(at(poly, Pi));
      Ry = G.Y(at(poly, Pi));
      // Eberly: a reflex vertex inside the triangle (M, I, P) may block
      // the bridge to P; the one with the smallest angle to the ray
      // (closest on a tie) is taken instead. Copies of P at its own
      // coordinates are P itself for this purpose.
      double bestAng = 1e300;
      double bestD = 1e300;
      const double x1 = Mx;
      const double y1 = My;
      const double x2 = hitX;
      const double y2 = My;
      const double x3 = Rx;
      const double y3 = Ry;
      for (int q = 0; q < n; q++) {
        const double qx = G.X(at(poly, q));
        const double qy = G.Y(at(poly, q));
        if (qx < Mx - kTol) {
          continue;
        }
        if (Geo::same(qx, qy, Rx, Ry) || Geo::same(qx, qy, Mx, My)) {
          continue;
        }
        const int pr = at(poly, wrap(q - 1, n));
        const int nx = at(poly, (q + 1) % n);
        if (Geo::sdist(G.X(pr), G.Y(pr), qx, qy, G.X(nx), G.Y(nx)) >= -kTol) {
          continue;  // not reflex
        }
        // inside the (often sliver) triangle M, I, P: within kTol of every
        // side's inner half plane, and inside its box
        bool inside = true;
        const double ex[3] = {x1, x2, x3};
        const double ey[3] = {y1, y2, y3};
        const double area = ((x2 - x1) * (y3 - y1)) - ((y2 - y1) * (x3 - x1));
        for (int k = 0; k < 3 && inside; k++) {
          double d = Geo::sdist(ex[k], ey[k], ex[(k + 1) % 3], ey[(k + 1) % 3], qx, qy);
          if (area < 0) {
            d = -d;
          }
          if (d < -kTol) {
            inside = false;
          }
        }
        if (qx > std::max(x2, x3) + kTol || qy < std::min(y1, y3) - kTol ||
            qy > std::max(y1, y3) + kTol) {
          inside = false;
        }
        if (!inside) {
          continue;
        }
        const double ang = std::atan2(std::fabs(qy - My), qx - Mx);
        const double dd = ((qx - Mx) * (qx - Mx)) + ((qy - My) * (qy - My));
        if (ang < bestAng - 1e-9 || (std::fabs(ang - bestAng) <= 1e-9 && dd < bestD)) {
          bestAng = ang;
          bestD = dd;
          found = true;
          Rx = qx;
          Ry = qy;
        }
      }
      // The target coordinates may occur at several polygon positions
      // (bridge twins, touching loops): the copy whose interior wedge
      // contains the direction towards M is taken.
      int Pp = -1;
      for (int q = 0; q < n; q++) {
        if (!Geo::same(G.X(at(poly, q)), G.Y(at(poly, q)), Rx, Ry)) {
          continue;
        }
        if (!found && q == Pi) {
          Pp = q;
          break;
        }
        const int pr = at(poly, wrap(q - 1, n));
        const int nx = at(poly, (q + 1) % n);
        if (Geo::wedge(Rx, Ry, G.X(pr), G.Y(pr), G.X(nx), G.Y(nx), Mx - Rx, My - Ry)) {
          Pp = q;
          break;
        }
      }
      if (Pp < 0) {
        ok = false;
        why = "no bridge target copy faces the hole";
        break;
      }
      Pi = Pp;
    }
    // splice: ... P, M, hole around back to M, P, ...
    std::vector<int> np;
    for (int q = 0; q <= Pi; q++) {
      np.push_back(at(poly, q));
    }
    for (int i = 0; i <= hs; i++) {
      np.push_back(at(H, (mi + i) % hs));
    }
    for (int q = Pi; q < n; q++) {
      np.push_back(at(poly, q));
    }
    poly.swap(np);
  }

  // ear clipping
  std::vector<int> I = poly;
  while (ok && I.size() > 3) {
    const int m = static_cast<int>(I.size());
    int pick = -1;
    int drop = -1;
    for (int i = 0; i < m && pick < 0; i++) {
      const int a = at(I, wrap(i - 1, m));
      const int b = at(I, i);
      const int c = at(I, (i + 1) % m);
      const double ax = G.X(a);
      const double ay = G.Y(a);
      const double bx = G.X(b);
      const double by = G.Y(b);
      const double cx = G.X(c);
      const double cy = G.Y(c);
      // Coincident points: a zero-length edge, or the two twins of a
      // bridge meeting again (a == c): b is dropped, no triangle. A
      // collinear vertex is never dropped: it is a real vertex that
      // neighbouring constructs attach to, so the edges on both sides of
      // it must stay (ears elsewhere consume it).
      if (drop < 0 && (Geo::same(ax, ay, bx, by) || Geo::same(bx, by, cx, cy) ||
                       Geo::same(ax, ay, cx, cy))) {
        drop = i;
        continue;
      }
      // convex: b more than kTol left of a->c
      if (Geo::sdist(ax, ay, cx, cy, bx, by) >= -kTol) {
        continue;  // reflex or straight
      }
      bool clear = true;
      // No vertex on the open diagonal a-c: the diagonal becomes a
      // triangle edge and that vertex would be a T-junction on it (a hole
      // edge collinear with an outline vertex).
      const double acx = cx - ax;
      const double acy = cy - ay;
      const double acl = std::sqrt((acx * acx) + (acy * acy));
      for (int q = 0; q < m && clear; q++) {
        if (q == wrap(i - 1, m) || q == i || q == (i + 1) % m) {
          continue;
        }
        const double wx = G.X(at(I, q));
        const double wy = G.Y(at(I, q));
        const double t = (((wx - ax) * acx) + ((wy - ay) * acy)) / (acl * acl);
        if (t * acl <= kTol || (1 - t) * acl <= kTol) {
          continue;
        }
        if (std::fabs(Geo::sdist(ax, ay, cx, cy, wx, wy)) < kTol) {
          clear = false;
        }
      }
      // No edge of the polygon (other than the two at b) may pass through
      // the open triangle a, b, c: an endpoint strictly inside, or a
      // crossing of the interior (loops running through a sliver ear
      // without a vertex inside it). The triangle a,b,c is CCW (b right of
      // a->c), so inside is left of each side.
      const double ex[3] = {ax, bx, cx};
      const double ey[3] = {ay, by, cy};
      for (int q = 0; q < m && clear; q++) {
        if (q == wrap(i - 1, m) || q == i) {
          continue;  // edges a-b and b-c
        }
        const int u = at(I, q);
        const int v = at(I, (q + 1) % m);
        const double ux = G.X(u);
        const double uy = G.Y(u);
        const double vx = G.X(v);
        const double vy = G.Y(v);
        // parameter interval of u+t(v-u) deeper than kTol inside all three sides
        double lo = 0;
        double hi = 1;
        for (int k = 0; k < 3 && lo < hi; k++) {
          const double du = Geo::sdist(ex[k], ey[k], ex[(k + 1) % 3], ey[(k + 1) % 3], ux, uy);
          const double dv = Geo::sdist(ex[k], ey[k], ex[(k + 1) % 3], ey[(k + 1) % 3], vx, vy);
          if (du <= kTol && dv <= kTol) {  // never inside
            lo = 1;
            hi = 0;
            break;
          }
          if (du > kTol && dv > kTol) {
            continue;
          }
          const double t = (du - kTol) / (du - dv);  // crossing of the kTol-inset side
          if (du > dv) {
            hi = std::min(hi, t);
          } else {
            lo = std::max(lo, t);
          }
        }
        if (lo < hi) {
          clear = false;
        }
      }
      // And the ear must be inside the polygon: once a hole touching the
      // outline has been clipped around, the leftover can be a zero-area
      // slit whose edges all run twice in opposite directions; an ear
      // there passes the edge tests but covers exterior. Winding number
      // of the centroid.
      if (clear) {
        const double gx = (ax + bx + cx) / 3;
        const double gy = (ay + by + cy) / 3;
        int wn = 0;
        for (int q = 0; q < m; q++) {
          const int u = at(I, q);
          const int v = at(I, (q + 1) % m);
          const double ux = G.X(u);
          const double uy = G.Y(u);
          const double vx = G.X(v);
          const double vy = G.Y(v);
          if (uy <= gy) {
            if (vy > gy && ((vx - ux) * (gy - uy)) - ((vy - uy) * (gx - ux)) > 0) {
              wn++;
            }
          } else if (vy <= gy && ((vx - ux) * (gy - uy)) - ((vy - uy) * (gx - ux)) < 0) {
            wn--;
          }
        }
        if (wn != 1) {
          clear = false;
        }
      }
      if (clear) {
        pick = i;
      }
    }
    if (pick < 0) {
      if (drop < 0) {
        // A remainder with no area is finished: a slit whose edges all
        // run along one line (a hole's bridge doubling back on an outline
        // edge) has nothing left to triangulate.
        const double ex0 = G.X(at(I, 0));
        const double ey0 = G.Y(at(I, 0));
        double far = 0;
        int fi = 0;
        for (int q = 1; q < m; q++) {
          const double dd = ((G.X(at(I, q)) - ex0) * (G.X(at(I, q)) - ex0)) +
                            ((G.Y(at(I, q)) - ey0) * (G.Y(at(I, q)) - ey0));
          if (dd > far) {
            far = dd;
            fi = q;
          }
        }
        bool flatRem = true;
        for (int q = 0; q < m && flatRem; q++) {
          if (std::fabs(Geo::sdist(ex0, ey0, G.X(at(I, fi)), G.Y(at(I, fi)), G.X(at(I, q)),
                                   G.Y(at(I, q)))) > kTol) {
            flatRem = false;
          }
        }
        if (flatRem) {
          break;
        }
        ok = false;
        why = "no valid ear";
        break;
      }
      I.erase(I.begin() + drop);
      continue;
    }
    const int a = at(I, wrap(pick - 1, m));
    const int b = at(I, pick);
    const int c = at(I, (pick + 1) % m);
    tris.push_back(a);
    tris.push_back(b);
    tris.push_back(c);
    I.erase(I.begin() + pick);
  }
  if (ok && I.size() == 3) {
    if (Geo::sdist(G.X(I[0]), G.Y(I[0]), G.X(I[2]), G.Y(I[2]), G.X(I[1]), G.Y(I[1])) < -kTol) {
      tris.push_back(I[0]);
      tris.push_back(I[1]);
      tris.push_back(I[2]);
    }
  }
  // invariant: the triangles cover exactly the loops' area
  if (ok) {
    double la = 0;
    double ta = 0;
    for (const std::vector<int>& loop : loops) {
      for (std::size_t i = 0; i < loop.size(); i++) {
        const Point2& a = P(loop[i]);
        const Point2& b = P(loop[(i + 1) % loop.size()]);
        la += ((a.x * b.y) - (b.x * a.y)) / 2;
      }
    }
    for (std::size_t t = 0; t < tris.size(); t += 3) {
      const Point2& a = P(tris[t]);
      const Point2& b = P(tris[t + 1]);
      const Point2& c = P(tris[t + 2]);
      ta += (((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x))) / 2;
    }
    if (std::fabs(la - ta) > 1e-6 * std::max(1.0, std::fabs(la))) {
      ok = false;
      why = "area mismatch";
    }
  }
  return ok;
}

}  // namespace bt::mesh
