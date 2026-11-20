// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play workspace's DOM-free parts (decision 0039): the game (the moves
// made, the place in them, the merge of a piece moved again), the drag that
// follows the pointer over the places the engine gave, a route's points
// for the playback, and the moves file written, read and matched.

import { describe, expect, it } from 'vitest';

import {
  aim,
  alongRoute,
  follow,
  groupFor,
  routeLength,
  slide,
  stopsIn,
} from '../../../src/renderer/play/follow.ts';
import {
  canMerge,
  made,
  movesText,
  newGame,
  placesAt,
  seek,
  solvedAt,
  withMoves,
} from '../../../src/renderer/play/game.ts';
import type { Cell, Move } from '../../../src/renderer/play/game.ts';
import { loops, roundedOutline } from '../../../src/renderer/play/outline.ts';
import {
  boardOf,
  matchStart,
  MOVES_FORMAT,
  MOVES_VERSION,
  parse,
  remap,
  serialise,
} from '../../../src/renderer/play/moves-file.ts';

const START: Cell[] = [
  [0, 0, 0],
  [0, 1, 0],
];
const move = (piece: number, x: number, y: number): Move => ({ movers: [piece], by: [x, y, 0] });

describe('a game played by hand', () => {
  it('starts with no moves, at its start', () => {
    const g = newGame('k', START, false);
    expect(g.moves).toEqual([]);
    expect(g.at).toBe(0);
    expect(placesAt(g)).toEqual(START);
    expect(solvedAt(g)).toBe(false);
    expect(solvedAt(newGame('k', START, true))).toBe(true);
    // the start is the game's own
    START[0] = [9, 9, 9];
    expect(g.start[0]).toEqual([0, 0, 0]);
    START[0] = [0, 0, 0];
  });

  it('counts a move for each piece moved, and moves the pieces', () => {
    let g = newGame('k', START, false);
    g = made(g, move(1, 2, 0), false);
    g = made(g, move(0, 1, 0), true);
    expect(g.moves).toHaveLength(2);
    expect(g.at).toBe(2);
    expect(placesAt(g)).toEqual([
      [1, 0, 0],
      [2, 1, 0],
    ]);
    expect(placesAt(g, 1)).toEqual([
      [0, 0, 0],
      [2, 1, 0],
    ]);
    expect([solvedAt(g, 0), solvedAt(g, 1), solvedAt(g, 2)]).toEqual([false, false, true]);
  });

  it('goes on with a piece’s move when the piece is moved again straight away', () => {
    let g = newGame('k', START, false);
    g = made(g, move(1, 2, 0), false);
    // round the corner in a second drag: still one move, as the solver counts
    g = made(g, move(1, 0, -1), true);
    expect(g.moves).toEqual([{ movers: [1], by: [2, -1, 0] }]);
    expect(solvedAt(g)).toBe(true);
    // put back where the move began: the move is gone
    g = made(g, move(1, -2, 1), false);
    expect(g.moves).toEqual([]);
    expect(g.at).toBe(0);
    expect(placesAt(g)).toEqual(START);
    // another piece between is another move
    g = made(made(made(g, move(1, 1, 0), false), move(0, 1, 0), false), move(1, 1, 0), false);
    expect(g.moves).toHaveLength(3);
    // a group merges only with the same group, in any order
    expect(canMerge({ movers: [0, 1], by: [1, 0, 0] }, { movers: [1, 0], by: [1, 0, 0] })).toBe(
      true,
    );
    expect(canMerge({ movers: [0, 1], by: [1, 0, 0] }, move(0, 1, 0))).toBe(false);
  });

  it('undoes and redoes by its place in the moves; a new move drops those undone', () => {
    let g = newGame('k', START, false);
    g = made(made(made(g, move(1, 1, 0), false), move(0, 1, 0), false), move(1, 1, 0), true);
    g = seek(g, 1);
    expect(g.at).toBe(1);
    expect(g.moves).toHaveLength(3);
    expect(placesAt(g)).toEqual([
      [0, 0, 0],
      [1, 1, 0],
    ]);
    expect(seek(g, 99).at).toBe(3);
    expect(seek(g, -4).at).toBe(0);
    // a move from here: the two undone are gone, and it merges with the one kept
    g = made(g, move(1, 1, 0), false);
    expect(g.moves).toEqual([{ movers: [1], by: [2, 0, 0] }]);
    expect(g.solved).toEqual([false]);
    // a move that goes nowhere is none
    expect(made(newGame('k', START, false), move(0, 0, 0), false).moves).toEqual([]);
  });

  it('takes moves read from a file, shown from the start', () => {
    const g = withMoves(
      newGame('k', START, false),
      [move(1, 1, 0), move(0, 1, 0)],
      [false, true],
      0,
    );
    expect(g.at).toBe(0);
    expect(g.moves).toHaveLength(2);
    expect(solvedAt(seek(g, 2))).toBe(true);
    expect(withMoves(g, [], [], 5).at).toBe(0);
    expect(movesText(1)).toBe('1 move');
    expect(movesText(12)).toBe('12 moves');
  });
});

describe('the outline of a piece or a floor', () => {
  it('goes round the cells clockwise, by their corners only', () => {
    // an L: two cells in a row and one below the first (y down)
    expect(
      loops([
        [0, 0],
        [1, 0],
        [0, 1],
      ]),
    ).toEqual([
      [
        [0, 0],
        [2, 0],
        [2, 1],
        [1, 1],
        [1, 2],
        [0, 2],
      ],
    ]);
  });

  it('keeps cells that touch at a corner in loops of their own', () => {
    const two = loops([
      [0, 0],
      [1, 1],
    ]);
    expect(two).toHaveLength(2);
    expect(two.map((l) => l.length)).toEqual([4, 4]);
  });

  it('goes round a hole the other way', () => {
    const ring: [number, number][] = [];
    for (let x = 0; x < 3; x++) {
      for (let y = 0; y < 3; y++) {
        if (x !== 1 || y !== 1) {
          ring.push([x, y]);
        }
      }
    }
    const all = loops(ring);
    expect(all).toHaveLength(2);
    const area = (l: [number, number][]): number =>
      l.reduce((a, p, i) => {
        const q = l[(i + 1) % l.length] ?? p;
        return a + p[0] * q[1] - q[0] * p[1];
      }, 0) / 2;
    // clockwise on the screen is a positive area with y down
    expect(all.map(area).sort((a, b) => a - b)).toEqual([-1, 9]);
  });

  it('draws each side inset by the way it faces, its corners rounded', () => {
    const d = roundedOutline([[0, 0]], {
      scale: 10,
      at: [0, 0],
      inset: (into) => (into[0] < 0 || into[1] < 0 ? 3 : 1),
      radius: 2,
    });
    // the square from (1, 1) to (7, 7), each corner an arc of radius 2
    expect(d).toBe('M1 3A2 2 0 0 1 3 1L5 1A2 2 0 0 1 7 3L7 5A2 2 0 0 1 5 7L3 7A2 2 0 0 1 1 5Z');
  });
});

describe('a dragged piece', () => {
  // an L of places: along the row y = 0 to x = 2, then up the column x = 2
  const stops = stopsIn([
    { by: [1, 0, 0] },
    { by: [2, 0, 0] },
    { by: [2, 1, 0] },
    { by: [2, 2, 0] },
    // another layer's stop plays no part in a drag
    { by: [0, 1, 1] },
  ]);

  it('follows the pointer a step at a time over the places it can be', () => {
    expect(follow([0, 0], [2, 0], stops)).toEqual([2, 0]);
    // past the end: as far as it goes
    expect(follow([0, 0], [5, 0], stops)).toEqual([2, 0]);
    // straight up is no place: it stays
    expect(follow([0, 0], [0, 2], stops)).toEqual([0, 0]);
    // the pointer cut the corner: the piece goes along the row, then up
    expect(follow([0, 0], [2, 2], stops)).toEqual([2, 2]);
    expect(follow([0, 0], [1, 2], stops)).toEqual([1, 0]);
    // and back the way it came
    expect(follow([2, 2], [0, 0], stops)).toEqual([0, 0]);
    expect(follow([2, 1], [2, 1], stops)).toEqual([2, 1]);
  });

  it('never jumps what is in its way', () => {
    // two places with a gap between: the far one is not reached in a straight drag
    const gap = stopsIn([{ by: [1, 0, 0] }, { by: [3, 0, 0] }]);
    expect(follow([0, 0], [3, 0], gap)).toEqual([1, 0]);
  });

  it('moves alone when it can step that way, else with what it carries', () => {
    const alone = stopsIn([{ by: [1, 0, 0] }]);
    const carrying = stopsIn([{ by: [0, 1, 0] }, { by: [1, 0, 0] }]);
    expect(groupFor([alone, carrying], [0, 1], [1, 0])).toBe(0);
    expect(groupFor([alone, carrying], [0, 1], [0, 1])).toBe(1);
    // Shift: the carrying group first
    expect(groupFor([alone, carrying], [1, 0], [1, 0])).toBe(1);
    expect(groupFor([alone, carrying], [0, 1], [-1, 0])).toBe(-1);
    expect(groupFor([alone, carrying], [0, 1], [0, 0])).toBe(-1);
  });

  it('moves with the pointer between stops, towards the next one only', () => {
    // a little way along the row: drawn that far
    expect(slide([0, 0], [0.1, 0], stops)).toEqual([0.1, 0]);
    // never more than half a cell: past that the next stop is the nearest
    expect(slide([0, 0], [0.8, 0], stops)).toEqual([0.5, 0]);
    // nothing to the left or above the start: it stays
    expect(slide([0, 0], [-0.3, 0], stops)).toEqual([0, 0]);
    expect(slide([0, 0], [0.05, 0.3], stops)).toEqual([0.05, 0]);
    // one axis at a time: the one the pointer went further along
    expect(slide([2, 1], [2.125, 1.25], stops)).toEqual([0, 0.25]);
    expect(slide([2, 0], [1.625, 0.25], stops)).toEqual([-0.375, 0]);
  });

  it('chooses which way it goes from the pointer’s first movement', () => {
    expect(aim([0.004, 0])).toBeNull();
    expect(aim([0.1, -0.03])).toEqual([1, 0]);
    expect(aim([-0.02, -0.2])).toEqual([0, -1]);
    expect(aim([1.4, 0.6])).toEqual([1, 1]);
  });

  it('is drawn along a move’s route at an even pace', () => {
    const route = [
      [0, 0, 0],
      [2, 0, 0],
      [2, 2, 0],
    ];
    expect(routeLength(route)).toBe(4);
    expect(alongRoute(route, 0)).toEqual([0, 0, 0]);
    expect(alongRoute(route, 0.25)).toEqual([1, 0, 0]);
    expect(alongRoute(route, 0.5)).toEqual([2, 0, 0]);
    expect(alongRoute(route, 0.75)).toEqual([2, 1, 0]);
    expect(alongRoute(route, 1)).toEqual([2, 2, 0]);
    expect(alongRoute(route, 7)).toEqual([2, 2, 0]);
    expect(alongRoute([[0, 0, 0]], 0.5)).toEqual([0, 0, 0]);
    expect(routeLength([])).toBe(0);
  });
});

describe('the moves file', () => {
  const board = {
    size: [3, 2, 1],
    cells: [
      { at: [0, 0, 0], state: 'fixed' },
      { at: [1, 0, 0], state: 'fixed' },
      { at: [2, 0, 0], state: 'variable' },
      { at: [0, 1, 0], state: 'fixed' },
      { at: [2, 1, 0], state: 'fixed' },
    ],
  };
  const pieces: { cells: Cell[]; at: Cell }[] = [
    {
      cells: [
        [0, 0, 0],
        [1, 0, 0],
      ],
      at: [0, 0, 0],
    },
    { cells: [[0, 0, 0]], at: [0, 1, 0] },
  ];
  const moves = [move(1, 0, 0), move(0, 1, 0)];
  const text = serialise({
    puzzle: 'box.xmpuzzle',
    problem: 'P1 Corner',
    board: boardOf(board),
    rules: { nestedSlides: true, partialNested: false, verticalMoves: false },
    pieces,
    moves,
  });

  it('keeps the board a character a cell: a wall, floor, a corridor', () => {
    expect(boardOf(board)).toEqual({ size: [3, 2, 1], cells: '112101' });
  });

  it('is read back as it was written', () => {
    const file = parse(text);
    expect(typeof file).toBe('object');
    if (typeof file === 'string') {
      return;
    }
    expect(file.format).toBe(MOVES_FORMAT);
    expect(file.version).toBe(MOVES_VERSION);
    expect(file.problem).toBe('P1 Corner');
    expect(file.rules.nestedSlides).toBe(true);
    expect(file.pieces).toEqual(pieces);
    expect(file.moves).toEqual(moves);
    expect(matchStart(file, board, pieces)).toEqual([0, 1]);
  });

  it('names its pieces as the problem lists them now', () => {
    const file = parse(text);
    if (typeof file === 'string') {
      throw new Error(file);
    }
    // the same start with its pieces listed the other way round
    const turned = [pieces[1], pieces[0]].filter((p) => p !== undefined);
    const map = matchStart(file, board, turned);
    expect(map).toEqual([1, 0]);
    expect(remap(file.moves, map as number[])).toEqual([move(0, 0, 0), move(1, 1, 0)]);
  });

  it('is refused for another board or another start', () => {
    const file = parse(text);
    if (typeof file === 'string') {
      throw new Error(file);
    }
    const walled = { ...board, cells: board.cells.slice(1) };
    expect(matchStart(file, walled, pieces)).toBe('These moves are for another board.');
    expect(matchStart(file, { ...board, size: [2, 3, 1] }, pieces)).toBe(
      'These moves are for another board.',
    );
    const moved = [pieces[0], { cells: [[0, 0, 0]] as Cell[], at: [2, 1, 0] as Cell }].filter(
      (p) => p !== undefined,
    );
    expect(matchStart(file, board, moved)).toBe('These moves start from another position.');
    expect(matchStart(file, board, pieces.slice(0, 1))).toBe(
      'These moves start from another position.',
    );
  });

  it('says what is wrong with a text that is no moves file', () => {
    const notOne = 'This is not a ShapeLogic moves file.';
    expect(parse('not json')).toBe(notOne);
    expect(parse('[]')).toBe(notOne);
    expect(parse('{"format":"other","version":1}')).toBe(notOne);
    const whole = JSON.parse(text) as Record<string, unknown>;
    expect(parse(JSON.stringify({ ...whole, version: MOVES_VERSION + 1 }))).toBe(
      'These moves were written by a newer version of ShapeLogic.',
    );
    const damaged = 'The moves file is damaged.';
    expect(parse(JSON.stringify({ ...whole, board: { size: [3, 2, 1], cells: '11' } }))).toBe(
      damaged,
    );
    expect(parse(JSON.stringify({ ...whole, pieces: [{ at: [0, 0], cells: [[0, 0, 0]] }] }))).toBe(
      damaged,
    );
    expect(parse(JSON.stringify({ ...whole, moves: [{ movers: [5], by: [1, 0, 0] }] }))).toBe(
      damaged,
    );
    expect(parse(JSON.stringify({ ...whole, moves: [{ movers: [], by: [1, 0, 0] }] }))).toBe(
      damaged,
    );
    expect(parse(JSON.stringify({ ...whole, moves: [{ movers: [0], by: [1.5, 0, 0] }] }))).toBe(
      damaged,
    );
    // names and rules left out are no damage
    const bare = { ...whole };
    delete bare.puzzle;
    delete bare.problem;
    delete bare.rules;
    const read = parse(JSON.stringify(bare));
    expect(typeof read).toBe('object');
    if (typeof read !== 'string') {
      expect(read.puzzle).toBe('');
      expect(read.rules).toEqual({
        nestedSlides: false,
        partialNested: false,
        verticalMoves: false,
      });
    }
  });
});
