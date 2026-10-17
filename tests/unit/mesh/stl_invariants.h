// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What an STL mesh must keep on every platform and compiler: its bounding
/// box, surface area and volume. The exact triangulation does not travel:
/// mergeCoplanarFaces decides which faces are coplanar with thresholds such
/// as 1 - 1e-6, and the last bit of a float (a fused multiply-add on one
/// compiler, a separate multiply and add on another) can flip one decision
/// and give a different but equally valid set of triangles. So the tests
/// compare these measures with a tolerance, not the triangles of the
/// original's recording.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace bt::test {

using StlTriangle = std::array<std::array<double, 3>, 3>;

/// The triangles of a binary STL (80 bytes of header, a count, 50 bytes each).
inline std::vector<StlTriangle> binaryStlTriangles(std::span<const std::uint8_t> b) {
  std::vector<StlTriangle> out;
  if (b.size() < 84) {
    return out;
  }
  std::uint32_t count = 0;
  std::memcpy(&count, b.data() + 80, 4);
  for (std::uint32_t t = 0; t < count && 84 + (50 * (std::size_t{t} + 1)) <= b.size(); t++) {
    const std::uint8_t* p = b.data() + 84 + (50 * std::size_t{t}) + 12;
    StlTriangle tri{};
    for (std::size_t c = 0; c < 3; c++) {
      float v[3];
      std::memcpy(v, p + (12 * c), 12);
      tri[c] = {static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2])};
    }
    out.push_back(tri);
  }
  return out;
}

/// The triangles of a text STL: every three `vertex x y z` lines make one.
inline std::vector<StlTriangle> textStlTriangles(const std::string& text) {
  std::vector<StlTriangle> out;
  std::istringstream in(text);
  std::string word;
  StlTriangle tri{};
  std::size_t corner = 0;
  while (in >> word) {
    if (word == "vertex") {
      in >> tri[corner][0] >> tri[corner][1] >> tri[corner][2];
      if (++corner == 3) {
        out.push_back(tri);
        corner = 0;
      }
    }
  }
  return out;
}

/// The measures of a triangle mesh.
struct StlMeasures {
  std::array<double, 3> lo{1e30, 1e30, 1e30};
  std::array<double, 3> hi{-1e30, -1e30, -1e30};
  double area = 0;
  /// Signed, from the divergence theorem: the enclosed volume of a closed mesh.
  double volume = 0;
};

inline StlMeasures measure(const std::vector<StlTriangle>& mesh) {
  StlMeasures m;
  for (const StlTriangle& t : mesh) {
    for (const auto& v : t) {
      for (std::size_t a = 0; a < 3; a++) {
        m.lo[a] = std::min(m.lo[a], v[a]);
        m.hi[a] = std::max(m.hi[a], v[a]);
      }
    }
    const auto sub = [](const std::array<double, 3>& p, const std::array<double, 3>& q) {
      return std::array<double, 3>{p[0] - q[0], p[1] - q[1], p[2] - q[2]};
    };
    const auto cross = [](const std::array<double, 3>& p, const std::array<double, 3>& q) {
      return std::array<double, 3>{(p[1] * q[2]) - (p[2] * q[1]), (p[2] * q[0]) - (p[0] * q[2]),
                                   (p[0] * q[1]) - (p[1] * q[0])};
    };
    const std::array<double, 3> n = cross(sub(t[1], t[0]), sub(t[2], t[0]));
    m.area += 0.5 * std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
    const std::array<double, 3> c = cross(t[1], t[2]);
    m.volume += ((t[0][0] * c[0]) + (t[0][1] * c[1]) + (t[0][2] * c[2])) / 6;
  }
  return m;
}

/// Whether two numbers agree to `relative` of their size, and 1e-3 absolute
/// (the recordings print six decimals and the meshes are about ten units
/// wide).
inline bool close(double a, double b, double relative) {
  return std::fabs(a - b) <= 1e-3 + (relative * std::max(std::fabs(a), std::fabs(b)));
}

/// Whether two meshes have the same box, area and volume (within a tenth of a
/// percent for the last two); else what differs.
inline bool sameMeasures(const StlMeasures& a, const StlMeasures& b, std::string& why) {
  std::ostringstream out;
  for (std::size_t i = 0; i < 3; i++) {
    if (!close(a.lo[i], b.lo[i], 1e-4) || !close(a.hi[i], b.hi[i], 1e-4)) {
      out << "box axis " << i << ": " << a.lo[i] << ".." << a.hi[i] << " against " << b.lo[i]
          << ".." << b.hi[i] << "; ";
    }
  }
  // a different merge of near-coplanar faces moves the area by a hundredth of a percent
  if (!close(a.area, b.area, 1e-3)) {
    out << "area " << a.area << " against " << b.area << "; ";
  }
  if (!close(a.volume, b.volume, 1e-3)) {
    out << "volume " << a.volume << " against " << b.volume << "; ";
  }
  why = out.str();
  return why.empty();
}

}  // namespace bt::test
