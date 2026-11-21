// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the goal's window (goal-picture.ts) is told of a problem, free of
// the page: its title, and where a sliding goal's picture stands each
// piece.

import type { Cell } from './game.ts';

const cell = (c: readonly number[]): Cell => [c[0] ?? 0, c[1] ?? 0, c[2] ?? 0];

/** The goal window's title for a problem: "Goal: P1 name". */
export function goalTitle(problem: { label: string; name: string } | undefined): string {
  if (problem === undefined) {
    return 'Goal';
  }
  return `Goal: ${problem.label}${problem.name === '' ? '' : ` ${problem.name}`}`;
}

/**
 * Where each piece stands on its goal (its lowest corner), by its number:
 * a goal is a whole piece's cells, so it takes the first piece of its
 * shape not placed yet whose cells it is. A piece without a goal has none.
 */
export function goalPlaces(start: {
  pieces: readonly { piece: string; cells: readonly (readonly number[])[] }[];
  goals: readonly { piece: string; cells: readonly (readonly number[])[] }[];
}): Map<number, Cell> {
  const key = (cells: readonly Cell[]): string =>
    cells
      .map((c) => c.join(','))
      .sort()
      .join(' ');
  const out = new Map<number, Cell>();
  for (const g of start.goals) {
    const cells = g.cells.map(cell);
    if (cells.length === 0) {
      continue;
    }
    const low: Cell = [0, 1, 2].map((k) => Math.min(...cells.map((c) => c[k] ?? 0))) as Cell;
    const shape = key(cells.map((c): Cell => [c[0] - low[0], c[1] - low[1], c[2] - low[2]]));
    const i = start.pieces.findIndex(
      (p, n) => p.piece === g.piece && !out.has(n) && key(p.cells.map(cell)) === shape,
    );
    if (i >= 0) {
      out.set(i, low);
    }
  }
  return out;
}
