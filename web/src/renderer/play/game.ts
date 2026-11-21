// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game of a puzzle played by hand (decisions 0039 and 0042), apart from
// the page so the unit tests run it without a DOM or an engine: the start,
// the moves made and the place in them the board shows. The engine says
// what a move may be and whether a position is the goal (sliding.playReach,
// sliding.playReplay); this file only keeps what was done. A game is a
// value: every change gives a new one.
//
// What a start and a move are depends on the puzzle (`GameOf`): here are
// what every game shares and a sliding puzzle's game; a stacking puzzle's
// is stack-game.ts.
//
// A sliding puzzle's moves are counted as the solver counts them: one
// piece going anywhere it can reach is one move, so a piece dragged again
// straight after its own move goes on with that move (`canMerge`), and one
// put back where the move began takes the move off.

/** A cell [x, y, z] of the board, or a shift in cells. */
export type Cell = [number, number, number];

/** One move: the pieces that moved as one (by their places in the start's list), and how far. */
export interface Move {
  movers: number[];
  by: Cell;
}

/** A game whose start is an S and whose moves are Ms. */
export interface GameOf<S, M> {
  /** What the start is of (the pieces and where they start): a game of another start is another game. */
  key: string;
  /** The position the game starts from. */
  start: S;
  /** The start is the goal already. */
  startSolved: boolean;
  /** The moves made, those taken back with Undo included (Redo makes them again). */
  moves: M[];
  /** After each move: the position is the goal. */
  solved: boolean[];
  /** How many of the moves the board shows made: 0 is the start. */
  at: number;
}

/** A game of any puzzle: what the Play workspace goes through without knowing its moves. */
export type AnyGame = GameOf<unknown, unknown>;

/** A sliding puzzle's game: its start is where each piece starts, the lowest corner of its cells. */
export type Game = GameOf<Cell[], Move>;

/** A game at its start. */
export function newGame(key: string, start: readonly Cell[], startSolved: boolean): Game {
  return {
    key,
    start: start.map((c): Cell => [c[0], c[1], c[2]]),
    startSolved,
    moves: [],
    solved: [],
    at: 0,
  };
}

const isZero = (c: Cell): boolean => c[0] === 0 && c[1] === 0 && c[2] === 0;

/** Whether two moves move the same pieces (in any order). */
export function sameMovers(a: readonly number[], b: readonly number[]): boolean {
  return a.length === b.length && a.every((m) => b.includes(m));
}

/**
 * Whether a move made straight after `prev` goes on with it instead of
 * counting as another: the same pieces moving again are still one move.
 */
export function canMerge(prev: Move, next: Move): boolean {
  return sameMovers(prev.movers, next.movers);
}

/** Where each piece is after `at` moves (the game's place, when left out). */
export function placesAt(g: Game, at: number = g.at): Cell[] {
  const places = g.start.map((c): Cell => [c[0], c[1], c[2]]);
  for (const m of g.moves.slice(0, Math.max(0, at))) {
    for (const i of m.movers) {
      const p = places[i];
      if (p !== undefined) {
        places[i] = [p[0] + m.by[0], p[1] + m.by[1], p[2] + m.by[2]];
      }
    }
  }
  return places;
}

/** Whether the position after `at` moves is the goal. */
export function solvedAt(g: AnyGame, at: number = g.at): boolean {
  return at <= 0 ? g.startSolved : (g.solved[at - 1] ?? false);
}

/**
 * The game after a move made from the position shown: the moves taken
 * back are gone, and the move goes on with the one before it when they
 * merge. `solved` says whether the position it gives is the goal.
 */
export function made(g: Game, move: Move, solved: boolean): Game {
  const moves = g.moves.slice(0, g.at);
  const flags = g.solved.slice(0, g.at);
  const last = moves.at(-1);
  if (last !== undefined && canMerge(last, move)) {
    const by: Cell = [last.by[0] + move.by[0], last.by[1] + move.by[1], last.by[2] + move.by[2]];
    moves.pop();
    flags.pop();
    if (!isZero(by)) {
      moves.push({ movers: last.movers, by });
      flags.push(solved);
    }
  } else if (!isZero(move.by)) {
    moves.push({ movers: [...move.movers], by: [...move.by] });
    flags.push(solved);
  }
  return { ...g, moves, solved: flags, at: moves.length };
}

/** The game showing the position after `at` moves (held to the moves there are). */
export function seek<G extends AnyGame>(g: G, at: number): G {
  return { ...g, at: Math.max(0, Math.min(g.moves.length, Math.round(at))) };
}

/** The game at its start again, its moves thrown away. */
export function restarted<G extends AnyGame>(g: G): G {
  return { ...g, moves: [], solved: [], at: 0 };
}

/** The game with its moves replaced (moves read from a file, or checked again), shown from `at`. */
export function withMoves(
  g: Game,
  moves: readonly Move[],
  solved: readonly boolean[],
  at: number,
  startSolved: boolean = g.startSolved,
): Game {
  const list = moves.map((m): Move => ({ movers: [...m.movers], by: [...m.by] }));
  return {
    ...g,
    startSolved,
    moves: list,
    solved: list.map((_, i) => solved[i] ?? false),
    at: Math.max(0, Math.min(list.length, at)),
  };
}

/** "1 move", "12 moves". */
export function movesText(n: number): string {
  return `${String(n)} ${n === 1 ? 'move' : 'moves'}`;
}
