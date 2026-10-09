// SPDX-License-Identifier: GPL-3.0-or-later
//
// The size of a shape's space in mesh units, per grid: what the original's
// view centred a shape on and framed (calculateSize of src/lib/voxel_0.cpp
// to voxel_4.cpp, the core's bt/mesh/grid_geometry.cpp). The engine's
// meshes lie inside [0, extent]; the view needs the space's size, not the
// cells', so a shape does not jump as cells are drawn. A client table, as
// the plan allows the cube grid's neighbours: five one-line formulas.

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
