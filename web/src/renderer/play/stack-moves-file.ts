// SPDX-License-Identifier: GPL-3.0-or-later
//
// The moves file of a stacking puzzle (.slmoves, decision 0042): the
// transfers of a game played by hand, written so that someone else with
// the puzzle can read them back. JSON with the sliding file's format name
// (moves-file.ts), `kind: "stacking"` and version 2, so a program that
// only knows sliding files says the file is newer than it is. It holds the
// rods (how many discs each takes, Panex columns or not), the start as
// each rod's disc sizes and the transfers by rod; a file is taken only by
// a problem with the same rods and the same start, whatever its discs are
// called. Apart from the page so the unit tests run it without a DOM. The
// engine checks the transfers themselves (stacking.playReplay).

import { isInt, MOVES_FORMAT, record } from './moves-file.ts';

/** The `kind` of a stacking puzzle's moves file. */
export const STACK_KIND = 'stacking';
/** The newest `version` of a stacking moves file this program writes and reads. */
export const STACK_MOVES_VERSION = 2;

/** A transfer as the file keeps it: the top disc of rod `from` onto rod `to`. */
export interface FileTransfer {
  from: number;
  to: number;
}

/** What a file is matched by: the rods and the start. */
export interface StackSetup {
  /** The discs each rod holds (the pocket column last). */
  rods: number[];
  /** The rods are Panex columns. */
  panex: boolean;
  /** The start: each rod's discs bottom to top, by their sizes. */
  start: number[][];
}

/** A stacking puzzle's moves file. */
export interface StackMovesFile extends StackSetup {
  format: typeof MOVES_FORMAT;
  version: number;
  kind: typeof STACK_KIND;
  /** The puzzle's and the problem's names when it was written: for the reader, not checked. */
  puzzle: string;
  problem: string;
  moves: FileTransfer[];
}

/** The text of a stacking moves file. */
export function serializeStack(file: Omit<StackMovesFile, 'format' | 'version' | 'kind'>): string {
  const whole: StackMovesFile = {
    format: MOVES_FORMAT,
    version: STACK_MOVES_VERSION,
    kind: STACK_KIND,
    ...file,
  };
  return `${JSON.stringify(whole)}\n`;
}

const isCount = (v: unknown): v is number => isInt(v) && v >= 0;

/** A stacking moves file from its text, or why the text is none (a sentence for the user). */
export function parseStack(text: string): StackMovesFile | string {
  const notOne = 'This is not a ShapeLogic moves file.';
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return notOne;
  }
  const f = record(raw);
  if (f?.format !== MOVES_FORMAT || !isInt(f.version) || f.version < 1) {
    return notOne;
  }
  if (f.kind !== STACK_KIND) {
    // a file without a kind is a sliding puzzle's
    return f.kind === undefined
      ? 'These moves are for a sliding puzzle.'
      : 'These moves were written by a newer version of ShapeLogic.';
  }
  if (f.version > STACK_MOVES_VERSION) {
    return 'These moves were written by a newer version of ShapeLogic.';
  }
  const damaged = 'The moves file is damaged.';
  if (!Array.isArray(f.rods) || !Array.isArray(f.start) || !Array.isArray(f.moves)) {
    return damaged;
  }
  const rods = f.rods as unknown[];
  const start = f.start as unknown[];
  if (rods.length === 0 || !rods.every(isCount) || start.length !== rods.length) {
    return damaged;
  }
  if (!start.every((rod) => Array.isArray(rod) && rod.every((s) => isInt(s) && s >= 1))) {
    return damaged;
  }
  const moves: FileTransfer[] = [];
  for (const m of f.moves as unknown[]) {
    const r = record(m);
    const from = r?.from;
    const to = r?.to;
    if (!isCount(from) || !isCount(to) || from >= rods.length || to >= rods.length) {
      return damaged;
    }
    moves.push({ from, to });
  }
  return {
    format: MOVES_FORMAT,
    version: f.version,
    kind: STACK_KIND,
    puzzle: typeof f.puzzle === 'string' ? f.puzzle : '',
    problem: typeof f.problem === 'string' ? f.problem : '',
    rods,
    panex: f.panex === true,
    start: start as number[][],
    moves,
  };
}

/**
 * Why a file does not fit a problem's rods and start (a sentence for the
 * user); null when it is of this board and this start.
 */
export function stackMismatch(file: StackSetup, ours: StackSetup): string | null {
  if (file.panex !== ours.panex || file.rods.join(',') !== ours.rods.join(',')) {
    return 'These moves are for another board.';
  }
  const text = (start: readonly (readonly number[])[]): string =>
    start.map((rod) => rod.join(',')).join('|');
  return text(file.start) === text(ours.start) ? null : 'These moves start from another position.';
}
