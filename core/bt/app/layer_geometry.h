// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The grid editor's drawing of one layer of a shape: the outline of each
/// cell of the layer, as a polygon (a circle on the sphere grid), and the
/// cell of the layer across each edge, so that a front end can draw the
/// layer, find the cell under the pointer and outline a selection.
/// Ported from the cell drawing of src/gui/grideditor_0.cpp to
/// grideditor_4.cpp: drawTileFrame (the squares and triangles),
/// getTriangle (rhombic), getPolygon (tetrahedra-octahedra), the circles
/// of grideditor_2's drawNormalTile, the extent of calcParameters, and the
/// edges drawTileCursor finds between neighbouring cells (findCommonEdge).
/// The original drew in pixels with the cell size of the widget; this
/// gives the same figures in units of the grid's own cell (see
/// layerGeometry).

#include <vector>

namespace bt {
class Voxel;
}

namespace bt::app {

/// A point of the layer's drawing, y up (the original drew y = 0 at the
/// bottom).
struct LayerPoint {
  double x = 0;
  double y = 0;
  bool operator==(const LayerPoint&) const = default;
};

/// One cell of a layer.
struct LayerCell {
  /// The cell is (x, y, layer).
  int x = 0;
  int y = 0;
  /// Its outline, counter-clockwise; empty for a circle.
  std::vector<LayerPoint> polygon;
  /// Per edge (corner i to corner i + 1): the index in LayerGeometry::cells
  /// of the cell across it, or -1 when no cell of the layer is.
  std::vector<int> neighbours;
  /// The sphere grid: a circle instead of a polygon.
  bool circle = false;
  LayerPoint centre;
  double radius = 0;
};

/// The drawing of one layer of a voxel space.
struct LayerGeometry {
  /// The box the drawing is laid out in, from (0, 0): the cells fill it
  /// (the circles of the sphere grid reach a little past it).
  double width = 0;
  double height = 0;
  /// The cells of the layer that are cells of the grid
  /// (Voxel::validCoordinate), x by x and y by y within, as the original
  /// drew them.
  std::vector<LayerCell> cells;
};

/// The drawing of layer `layer` (z) of a voxel space of its size and grid.
/// The units: a cube is 1 by 1; a triangle of the prism grid has sides of
/// 1 (rows sqrt(3)/2 apart); spheres are 1 apart; on the rhombic grid a
/// square of 5 by 5 cells is 1 by 1, on the tetrahedra-octahedra grid one
/// of 3 by 3. Sliding and stacking spaces are drawn as cubes. A layer
/// outside the space has no cells.
[[nodiscard]] LayerGeometry layerGeometry(const Voxel& space, unsigned int layer);

}  // namespace bt::app
