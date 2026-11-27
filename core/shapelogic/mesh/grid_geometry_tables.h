// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The per-grid implementations behind grid_geometry.h (internal).

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace sl::mesh::detail {

void connectionFaceBricks(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners);
void connectionFacePrisms(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners);
void connectionFaceSpheres(int x, int y, int z, int n, std::vector<float>& corners);
void connectionFaceRhombic(int x, int y, int z, int n, double bevel, double offset,
                           std::vector<float>& corners);
void connectionFaceTetraOcta(int x, int y, int z, int n, double bevel, double offset,
                             std::vector<float>& corners);
// the new grids have no bevel faces (the uniform mesher draws them)
void connectionFaceHex(int x, int y, int z, int n, std::vector<float>& corners);
void connectionFaceCairo(int x, int y, int z, int n, std::vector<float>& corners);

/// The side faces, bottom and top of a prism cell over `polygon` (its
/// corners in mesh units, counter clockwise seen from above), from z to
/// z + 1: face n < polygon.size() is the side between corners n and n + 1,
/// counter clockwise seen from outside; then the bottom (clockwise seen
/// from above) and the top. Nothing for any other n. The hexagonal and
/// cairo grids' cells.
inline void prismFace(std::span<const std::array<double, 2>> polygon, int z, int n,
                      std::vector<float>& corners) {
  const auto push = [&](const std::array<double, 2>& p, double pz) {
    corners.push_back(static_cast<float>(p[0]));
    corners.push_back(static_cast<float>(p[1]));
    corners.push_back(static_cast<float>(pz));
  };
  const auto k = static_cast<int>(polygon.size());
  const double z0 = z;
  const double z1 = z + 1;
  if (n < 0 || n > k + 1) {
    return;
  }
  if (n < k) {
    const auto a = static_cast<std::size_t>(n);
    const std::size_t b = (a + 1) % polygon.size();
    push(polygon[a], z0);
    push(polygon[b], z0);
    push(polygon[b], z1);
    push(polygon[a], z1);
  } else if (n == k) {
    for (std::size_t i = polygon.size(); i-- > 0;) {
      push(polygon[i], z0);
    }
  } else {
    for (const std::array<double, 2>& p : polygon) {
      push(p, z1);
    }
  }
}

}  // namespace sl::mesh::detail
