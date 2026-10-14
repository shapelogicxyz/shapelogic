// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The geometry of the cells of every grid: the corners of a cell's faces
/// (the face towards each neighbour, and the bevel faces along its edges
/// and corners), the size of a space and where a cell lies, in the units
/// the meshes use.
/// Ported from getConnectionFace, calculateSize and recalcSpaceCoordinates
/// of src/lib/voxel_0.cpp to voxel_4.cpp and voxel_2_mesh.cpp (they lived
/// in the voxel classes; the grid module here has no geometry).

#include <vector>

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// Appends the corners (x, y, z per corner, 3 or 4 corners) of a face of
/// cell (x, y, z) to `corners`: for n >= 0 the face towards neighbour n,
/// for n < 0 bevel face -1-n. Nothing is appended when there is no such
/// face. `bevel` cuts the cell's edges, `offset` moves the faces in to
/// leave a gap between neighbouring cells (both in cell units).
void connectionFace(const Voxel& v, int x, int y, int z, int n, double bevel, double offset,
                    std::vector<float>& corners);

/// The size of the whole space in mesh units.
void calculateSize(const Voxel& v, float* x, float* y, float* z);

/// Turns cell coordinates into mesh units (positions of pieces and
/// hotspots).
void recalcSpaceCoordinates(const Voxel& v, float* x, float* y, float* z);

}  // namespace bt::mesh
