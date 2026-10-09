// SPDX-License-Identifier: GPL-3.0-or-later
//
// The size of a shape's space in mesh units, per grid: what the original's
// view centred a shape on and framed (calculateSize of src/lib/voxel_0.cpp
// to voxel_4.cpp, the core's bt/mesh/grid_geometry.cpp). The engine's
// meshes lie inside [0, extent]; the view needs the space's size, not the
// cells', so a shape does not jump as cells are drawn. And the spacing of
// the cells in mesh units (recalcSpaceCoordinates), which places a piece
// of an assembly at its cell minus its hotspot (stage 4). Client tables,
// as the plan allows the cube grid's neighbours: one-line formulas,
// checked against the engine's playback poses by an engine test.

import type { Grid, Size } from '../../engine/api.generated.ts';

/** The space's size in mesh units. */
export function gridExtent(grid: Grid, size: Size): [number, number, number] {
  const [x, y, z] = size;
  switch (grid) {
    case 'bricks':
    case 'sliding':
    case 'stacking':
      return [x, y, z];
    case 'prisms':
      return [1 + (x - 1) * 0.5, y * (Math.sqrt(3) / 2), z];
    case 'spheres':
      return [1 + (x - 1) * Math.SQRT1_2, 1 + (y - 1) * Math.SQRT1_2, 1 + (z - 1) * Math.SQRT1_2];
    case 'rhombic':
      // integer division, as the original's
      return [
        2 * Math.trunc((x + 4) / 5),
        2 * Math.trunc((y + 4) / 5),
        2 * Math.trunc((z + 4) / 5),
      ];
    case 'tetraOcta':
      return [Math.trunc((x + 2) / 3), Math.trunc((y + 2) / 3), Math.trunc((z + 2) / 3)];
  }
}

/**
 * Grid coordinates to mesh units (recalcSpaceCoordinates of the original's
 * voxel_0 to voxel_4): a linear scale per axis, so a cell minus a hotspot
 * is scaled as one.
 */
export function cellScale(grid: Grid): [number, number, number] {
  switch (grid) {
    case 'bricks':
    case 'sliding':
    case 'stacking':
      return [1, 1, 1];
    case 'prisms':
      return [0.5, Math.sqrt(3) / 2, 1];
    case 'spheres':
      return [Math.SQRT1_2, Math.SQRT1_2, Math.SQRT1_2];
    case 'rhombic':
      return [0.4, 0.4, 0.4];
    case 'tetraOcta':
      return [1 / 3, 1 / 3, 1 / 3];
  }
}
