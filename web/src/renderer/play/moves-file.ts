// SPDX-License-Identifier: GPL-3.0-or-later
//
// The moves file (.slmoves, decision 0039): the moves of a game played by
// hand, written so that someone else with the puzzle can read them back.
// JSON: the board, where the pieces start and the moves; a file is taken
// only by a problem with the same board and the same start, whatever its
// pieces are called or in what order they are listed. Apart from the page
// so the unit tests run it without a DOM. The engine checks the moves
// themselves (sliding.playReplay). This is a sliding puzzle's file; a
// stacking puzzle's has the same format name, a `kind` and a version of
// its own (stack-moves-file.ts).

import type { Cell, Move } from './game.ts';

/** The file's `format`. */
export const MOVES_FORMAT = 'shapelogic-moves';
/** The newest `version` this program writes and reads. */
export const MOVES_VERSION = 1;

/** A board as the file keeps it. */
export interface FileBoard {
  size: Cell;
  /** One character a cell, x + X*(y + Y*z): 0 a wall, 1 floor, 2 a corridor. */
  cells: string;
}

/** A piece at its start. */
export interface FilePiece {
  /** The cells it fills, from its lowest corner. */
  cells: Cell[];
  /** Where that corner starts. */
  at: Cell;
}

/** The rules the moves were made by (the Solver's options of the same names). */
export interface FileRules {
  nestedSlides: boolean;
  partialNested: boolean;
  verticalMoves: boolean;
}

/** A moves file. */
export interface MovesFile {
  format: typeof MOVES_FORMAT;
  version: number;
  /** The puzzle's and the problem's names when it was written: for the reader, not checked. */
  puzzle: string;
  problem: string;
  board: FileBoard;
  rules: FileRules;
  pieces: FilePiece[];
  moves: Move[];
}

/** A board as sliding.playStart gives it. */
export interface BoardCells {
  size: readonly number[];
  cells: readonly { at: readonly number[]; state: string }[];
}

/** The board as the file keeps it. */
export function boardOf(board: BoardCells): FileBoard {
  const [X = 0, Y = 0, Z = 0] = board.size;
  const text = new Array<string>(X * Y * Z).fill('0');
  for (const c of board.cells) {
    const [x = 0, y = 0, z = 0] = c.at;
    text[x + X * (y + Y * z)] = c.state === 'variable' ? '2' : '1';
  }
  return { size: [X, Y, Z], cells: text.join('') };
}

/** The text of a moves file. */
export function serialise(file: Omit<MovesFile, 'format' | 'version'>): string {
  const whole: MovesFile = { format: MOVES_FORMAT, version: MOVES_VERSION, ...file };
  return `${JSON.stringify(whole)}\n`;
}

/** Whether a value read from a file is a whole number. */
export const isInt = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v);

function cellOf(v: unknown): Cell | null {
  return Array.isArray(v) && v.length === 3 && v.every(isInt) ? (v as Cell) : null;
}

/** A value read from a file as an object, or null when it is none. */
export function record(v: unknown): Record<string, unknown> | null {
  return typeof v === 'object' && v !== null && !Array.isArray(v)
    ? (v as Record<string, unknown>)
    : null;
}

/** A moves file from its text, or why the text is none (a sentence for the user). */
export function parse(text: string): MovesFile | string {
  const notOne = 'This is not a ShapeLogic moves file.';
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return notOne;
  }
  const f = record(raw);
  if (f?.format !== MOVES_FORMAT) {
    return notOne;
  }
  if (!isInt(f.version) || f.version < 1) {
    return notOne;
  }
  if (f.kind === 'stacking') {
    return 'These moves are for a stacking puzzle.';
  }
  if (f.version > MOVES_VERSION) {
    return 'These moves were written by a newer version of ShapeLogic.';
  }
  const damaged = 'The moves file is damaged.';
  const board = record(f.board);
  const size = cellOf(board?.size);
  if (board === null || size === null || typeof board.cells !== 'string') {
    return damaged;
  }
  if (size.some((n) => n < 1) || board.cells.length !== size[0] * size[1] * size[2]) {
    return damaged;
  }
  if (!Array.isArray(f.pieces) || !Array.isArray(f.moves)) {
    return damaged;
  }
  const pieces: FilePiece[] = [];
  for (const p of f.pieces as unknown[]) {
    const r = record(p);
    const at = cellOf(r?.at);
    const cells = Array.isArray(r?.cells) ? (r.cells as unknown[]).map(cellOf) : null;
    if (at === null || cells === null || cells.length === 0 || cells.includes(null)) {
      return damaged;
    }
    pieces.push({ at, cells: cells as Cell[] });
  }
  const moves: Move[] = [];
  for (const m of f.moves as unknown[]) {
    const r = record(m);
    const by = cellOf(r?.by);
    const movers = Array.isArray(r?.movers) ? (r.movers as unknown[]) : null;
    if (
      by === null ||
      movers === null ||
      movers.length === 0 ||
      !movers.every((i) => isInt(i) && i >= 0 && i < pieces.length)
    ) {
      return damaged;
    }
    moves.push({ movers: movers as number[], by });
  }
  const rules = record(f.rules) ?? {};
  return {
    format: MOVES_FORMAT,
    version: f.version,
    puzzle: typeof f.puzzle === 'string' ? f.puzzle : '',
    problem: typeof f.problem === 'string' ? f.problem : '',
    board: { size, cells: board.cells },
    rules: {
      nestedSlides: rules.nestedSlides === true,
      partialNested: rules.partialNested === true,
      verticalMoves: rules.verticalMoves === true,
    },
    pieces,
    moves,
  };
}

// a piece's cells and place as one text, the cells in a fixed order
function pieceKey(p: { cells: readonly (readonly number[])[]; at: readonly number[] }): string {
  const cells = p.cells.map((c) => c.join(',')).sort();
  return `${p.at.join(',')}|${cells.join(';')}`;
}

/**
 * Which of our pieces each of the file's is, when the file is of this
 * board and this start: the file's piece i is ours [i]. Otherwise why the
 * file does not fit (a sentence for the user).
 */
export function matchStart(
  file: MovesFile,
  board: BoardCells,
  pieces: readonly { cells: readonly (readonly number[])[]; at: readonly number[] }[],
): number[] | string {
  const ours = boardOf(board);
  if (ours.size.join(',') !== file.board.size.join(',') || ours.cells !== file.board.cells) {
    return 'These moves are for another board.';
  }
  const other = 'These moves start from another position.';
  if (pieces.length !== file.pieces.length) {
    return other;
  }
  // no two pieces fill the same cells, so a key names one piece
  const where = new Map(pieces.map((p, i) => [pieceKey(p), i]));
  const map: number[] = [];
  for (const p of file.pieces) {
    const i = where.get(pieceKey(p));
    if (i === undefined || map.includes(i)) {
      return other;
    }
    map.push(i);
  }
  return map;
}

/** The file's moves with its pieces named as ours (matchStart's map). */
export function remap(moves: readonly Move[], map: readonly number[]): Move[] {
  return moves.map((m) => ({
    movers: m.movers.map((i) => map[i] ?? i),
    by: [m.by[0], m.by[1], m.by[2]],
  }));
}
