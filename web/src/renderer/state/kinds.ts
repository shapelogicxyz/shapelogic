// SPDX-License-Identifier: GPL-3.0-or-later
//
// The puzzles this front end edits and solves. The engine solves and edits
// stacking puzzles since part 3b of docs/DEFERRED-PLAN.md (engine.info says
// every grid is supported); their editor (rod sets, discs, start and goal
// rods), the stacking solver options and the player in transfers come
// with part 4. Until then this front end shows them as it did: opened,
// shown and saved, with the Puzzle's and the Solver's notices, the Solver
// disabled, and New not offering the stacking grid. DOM-free.

/** Whether this front end edits and solves puzzles of a grid (or a problem kind). */
export function frontEndSolves(kind: string | undefined): boolean {
  return kind !== 'stacking';
}
