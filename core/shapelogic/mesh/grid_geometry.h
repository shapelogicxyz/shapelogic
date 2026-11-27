// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The geometry of the cells of every grid: the corners of a cell's faces
/// (the face toward each neighbor, and the bevel faces along its edges
/// and corners), the size of a space and where a cell lies, in the units
/// the meshes use.
/// Ported from getConnectionFace, calculateSize and recalcSpaceCoordinates
/// of src/lib/voxel_0.cpp to voxel_4.cpp and voxel_2_mesh.cpp (they lived
/// in the voxel classes; the grid module here has no geometry).

#include "shapelogic/grid/grid_type.h"

#include <array>
#include <vector>

namespace sl {
class Voxel;
}

namespace sl::mesh {

/// Appends the corners (x, y, z per corner, 3 or 4 corners) of a face of
/// cell (x, y, z) to `corners`: for n >= 0 the face toward neighbor n,
/// for n < 0 bevel face -1-n. Nothing is appended when there is no such
/// face. `bevel` cuts the cell's edges, `offset` moves the faces in to
/// leave a gap between neighboring cells (both in cell units).
void connectionFace(const Voxel& v, int x, int y, int z, int n, double bevel, double offset,
                    std::vector<float>& corners);

/// The size of the whole space in mesh units.
void calculateSize(const Voxel& v, float* x, float* y, float* z);

/// Turns cell coordinates into mesh units (positions of pieces and
/// hotspots).
void recalcSpaceCoordinates(const Voxel& v, float* x, float* y, float* z);

/// A 3x3 matrix by rows: row i times a vector gives the vector's axis i.
using Matrix3 = std::array<std::array<double, 3>, 3>;

/// calculateSize and recalcSpaceCoordinates of a grid as linear rules,
/// for a client that frames and places the engine's meshes itself
/// (engine.info): a cell c lies at `cellToMesh` c, and a space of size s
/// is `extentOffset` + `extentMatrix` u in size, where u is s in whole
/// blocks of `extentUnit` cells per axis, rounded up (wholeUnits). The
/// functions above stay as the original computed them; a test holds
/// these rules to them on every grid.
struct SpaceRule {
  Matrix3 cellToMesh;
  unsigned int extentUnit;
  Matrix3 extentMatrix;
  std::array<double, 3> extentOffset;
};

/// The space rule of a grid kind.
[[nodiscard]] SpaceRule spaceRule(GridKind kind);

}  // namespace sl::mesh
