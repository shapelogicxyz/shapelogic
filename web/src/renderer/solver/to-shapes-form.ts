// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fields of Convert Assemblies to Pieces (to-shapes.ts) as the call
// they ask for: the original's filters (assmimportwindow.cpp) and their
// defaults, the ranges checked before any call. DOM-free (unit-tested).

import type { Methods } from '../../engine/api.generated.ts';

/** The call's parameters. */
export type ToShapes = Methods['solution.toShapes']['params'];

/** The filters of the dialog: key, label, on at first, bricks only. */
export const FILTERS: readonly [keyof ToShapes & `drop${string}`, string, boolean, boolean][] = [
  ['dropDisconnected', 'Drop disconnected shapes', true, false],
  ['dropMirror', 'Drop shapes with mirror symmetry', false, false],
  ['dropSymmetric', 'Drop all shapes with a symmetry', false, false],
  ['dropNonMillable', 'Drop non-millable shapes', false, true],
  ['dropNonNotchable', 'Drop non-notchable shapes', false, true],
  ['dropIdentical', 'Remove identical shapes', true, false],
];

/** The call the dialog's fields ask for, or a message saying what is wrong. */
export function toShapesParams(
  f: {
    problem: string;
    addTo: 'nothing' | 'newProblem' | 'problem';
    target: string;
    min: string;
    max: string;
    minCells: string;
    maxCells: string;
    drops: Partial<Record<(typeof FILTERS)[number][0], boolean>>;
  },
  bricks: boolean,
): ToShapes | string {
  const int = (s: string): number | null => (/^\s*\d+\s*$/.test(s) ? Number(s) : null);
  const min = int(f.min);
  const max = int(f.max);
  const minCells = int(f.minCells);
  const maxCells = int(f.maxCells);
  if (min === null || max === null || max > 0xffff || min > max) {
    return 'The range: whole numbers, the minimum at most the maximum';
  }
  if (minCells === null || maxCells === null || maxCells > 0xffffffff || minCells > maxCells) {
    return 'The shape size: whole numbers, the minimum at most the maximum';
  }
  const p: ToShapes = { problem: f.problem, minCells, maxCells, addTo: f.addTo, min, max };
  for (const [k, , , bricksOnly] of FILTERS) {
    if (!bricksOnly || bricks) {
      p[k] = f.drops[k] === true;
    }
  }
  if (f.addTo === 'problem') {
    p.target = f.target;
  }
  return p;
}
