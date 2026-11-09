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
    case 'hexagonal':
      // each row half a cell further right; the rows' lowest and highest corners
      return [x + 0.5 * (y - 1), 2 / Math.sqrt(3) + (y - 1) * (Math.sqrt(3) / 2), z];
    case 'cairo':
      // a cell's pentagon lies within one lattice unit of its coordinate
      return [(x + 1) * Math.SQRT1_2, (y + 1) * Math.SQRT1_2, z];
  }
}

/**
 * Grid coordinates to mesh units (recalcSpaceCoordinates of the original's
 * voxel_0 to voxel_4): a linear map, so a cell minus a hotspot maps as one.
 * A scale per axis on every grid but the hexagonal one, whose rows are
 * sheared half a cell each.
 */
export function cellToMesh(
  grid: Grid,
  v: readonly [number, number, number],
): [number, number, number] {
  const [x, y, z] = v;
  switch (grid) {
    case 'bricks':
    case 'sliding':
    case 'stacking':
      return [x, y, z];
    case 'prisms':
      return [0.5 * x, (Math.sqrt(3) / 2) * y, z];
    case 'spheres':
      return [Math.SQRT1_2 * x, Math.SQRT1_2 * y, Math.SQRT1_2 * z];
    case 'rhombic':
      return [0.4 * x, 0.4 * y, 0.4 * z];
    case 'tetraOcta':
      return [x / 3, y / 3, z / 3];
    case 'hexagonal':
      return [x + 0.5 * y, (Math.sqrt(3) / 2) * y, z];
    case 'cairo':
      return [Math.SQRT1_2 * x, Math.SQRT1_2 * y, z];
  }
}

/**
 * Where a placed piece's mesh goes, in mesh units centred on the result
 * (the poses' frame): an assembly puts the turned shape's hotspot on the
 * placement's cell (decision 0012, "Meshes in an orientation"), so the
 * mesh moves by the cell minus that hotspot, scaled; the assembled view,
 * the explorers' placements and positions alike.
 */
export function placedAt(
  at: readonly [number, number, number],
  hotspot: readonly [number, number, number],
  grid: Grid,
  extent: readonly [number, number, number],
): [number, number, number] {
  const m = cellToMesh(grid, [at[0] - hotspot[0], at[1] - hotspot[1], at[2] - hotspot[2]]);
  return [m[0] - extent[0] / 2, m[1] - extent[1] / 2, m[2] - extent[2] / 2];
}
