// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order File ▸ New lists the grids in: Brick, Sliding, Stacking
// first, then Tilted Square (a brick puzzle with a tray), then the others,
// as the original's grid selector (gridtypegui.cpp: "Sliding stays
// immediately after Brick"). DOM-free.

import type { Grid } from '../../engine/api.generated.ts';

/** Where New lists a grid, or Tilted Square. */
export function newOrder(grid: Grid | 'tilted'): number {
  const first: readonly (Grid | 'tilted')[] = ['bricks', 'sliding', 'stacking', 'tilted'];
  const i = first.indexOf(grid);
  return i < 0 ? first.length : i;
}
