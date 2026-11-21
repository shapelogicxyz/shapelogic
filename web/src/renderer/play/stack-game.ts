// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game of a stacking puzzle played by hand (decision 0042), apart from
// the page so the unit tests run it without a DOM or an engine: the
// stacking it starts from, the transfers made and the place in them the
// board shows (game.ts's `GameOf`). The engine says which transfers a
// position allows, where a disc comes to rest and whether a position is
// the goal (stacking.playMoves, stacking.playReplay); this file only keeps
// what was done. A game is a value: every change gives a new one.
//
// Every transfer is one move, as the solver counts them; a disc put
// straight back on the rod it came from takes its move off, as a sliding
// piece put back where its move began does.

import type { GameOf } from './game.ts';

/** A disc on a rod: its number among the problem's discs, and the height it rests at. */
export interface Resting {
  disc: number;
  z: number;
}

/** A stacking: each rod's discs, bottom to top. */
export type Stacks = Resting[][];

/** One transfer: the top disc of rod `from` onto rod `to`, where it rests at `z` (the engine's answer). */
export interface Transfer {
  from: number;
  to: number;
  z: number;
}

/** A stacking puzzle's game. */
export type StackGame = GameOf<Stacks, Transfer>;

const copy = (stacks: Stacks): Stacks => stacks.map((rod) => rod.map((d) => ({ ...d })));

/** A game at its start. */
export function newStackGame(key: string, start: Stacks, startSolved: boolean): StackGame {
  return { key, start: copy(start), startSolved, moves: [], solved: [], at: 0 };
}

/** The stacking after `at` transfers (the game's place, when left out). */
export function stacksAt(g: StackGame, at: number = g.at): Stacks {
  const stacks = copy(g.start);
  for (const m of g.moves.slice(0, Math.max(0, at))) {
    const disc = stacks[m.from]?.pop();
    if (disc !== undefined) {
      stacks[m.to]?.push({ disc: disc.disc, z: m.z });
    }
  }
  return stacks;
}

/** A stacking as the engine takes a position: each rod's discs by their numbers. */
export function discNumbers(stacks: Stacks): number[][] {
  return stacks.map((rod) => rod.map((d) => d.disc));
}

/**
 * The game after a transfer made from the position shown: the transfers
 * taken back are gone, and a disc put straight back on the rod it came
 * from takes its move off instead of adding one. `solved` says whether the
 * position the transfer gives is the goal.
 */
export function transferred(g: StackGame, move: Transfer, solved: boolean): StackGame {
  const moves = g.moves.slice(0, g.at);
  const flags = g.solved.slice(0, g.at);
  const last = moves.at(-1);
  if (last?.to === move.from && last.from === move.to) {
    moves.pop();
    flags.pop();
  } else {
    moves.push({ ...move });
    flags.push(solved);
  }
  return { ...g, moves, solved: flags, at: moves.length };
}

/** The game with its transfers replaced (read from a file, or checked again), shown from `at`. */
export function withTransfers(
  g: StackGame,
  moves: readonly Transfer[],
  solved: readonly boolean[],
  at: number,
  startSolved: boolean = g.startSolved,
): StackGame {
  const list = moves.map((m): Transfer => ({ ...m }));
  return {
    ...g,
    startSolved,
    moves: list,
    solved: list.map((_, i) => solved[i] ?? false),
    at: Math.max(0, Math.min(list.length, at)),
  };
}
