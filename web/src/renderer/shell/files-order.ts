// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order File ▸ New lists the grids in: Brick, Sliding, Stacking
// first, then the others, as the original's grid selector
// (gridtypegui.cpp: "Sliding stays immediately after Brick"). DOM-free.

import type { Grid } from '../../engine/api.generated.ts';

/** Where New lists a grid. */
export function newOrder(grid: Grid): number {
  const first: readonly Grid[] = ['bricks', 'sliding', 'stacking'];
  const i = first.indexOf(grid);
  return i < 0 ? first.length : i;
}
