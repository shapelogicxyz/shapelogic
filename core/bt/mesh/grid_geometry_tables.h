// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The per-grid implementations behind grid_geometry.h (internal).

#include <vector>

namespace bt::mesh::detail {

void connectionFaceBricks(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners);
void connectionFacePrisms(int x, int y, int z, int n, double bevel, double offset,
                          std::vector<float>& corners);
void connectionFaceSpheres(int x, int y, int z, int n, std::vector<float>& corners);
void connectionFaceRhombic(int x, int y, int z, int n, double bevel, double offset,
                           std::vector<float>& corners);
void connectionFaceTetraOcta(int x, int y, int z, int n, double bevel, double offset,
                             std::vector<float>& corners);

/// Floor division (intdiv_inf of the original).
constexpr int floorDiv(int x, int y) { return x / y - ((x % y != 0 && ((x % y) ^ y) < 0) ? 1 : 0); }

}  // namespace bt::mesh::detail
