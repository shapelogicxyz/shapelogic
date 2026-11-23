// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play workspace's DOM-free parts for a stacking puzzle (decision
// 0042): the game (the transfers made, the place in them, a disc put
// straight back), the moves file written, read and matched, and the
// board's measures and drawing.

import { describe, expect, it } from 'vitest';

import { restarted, seek, solvedAt } from '../../../src/renderer/play/game.ts';
import { MOVES_FORMAT, parse } from '../../../src/renderer/play/moves-file.ts';
import {
  discAt,
  fittedLabel,
  levelY,
  LIFT,
  rodAt,
  rodColumn,
  rodX,
  stackLook,
  stackSvg,
} from '../../../src/renderer/play/stack-look.ts';
import type { StackModel } from '../../../src/renderer/play/stack-look.ts';
import {
  discNumbers,
  newStackGame,
  stacksAt,
  transferred,
  withTransfers,
} from '../../../src/renderer/play/stack-game.ts';
import type { Stacks, Transfer } from '../../../src/renderer/play/stack-game.ts';
import {
  parseStack,
  serializeStack,
  STACK_KIND,
  STACK_MOVES_VERSION,
  stackMismatch,
} from '../../../src/renderer/play/stack-moves-file.ts';
import type { StackSetup } from '../../../src/renderer/play/stack-moves-file.ts';

// a three-disc tower on rod 1: disc 0 the largest
const TOWER: Stacks = [
  [
    { disc: 0, z: 0 },
    { disc: 1, z: 1 },
    { disc: 2, z: 2 },
  ],
  [],
  [],
];
const t = (from: number, to: number, z: number): Transfer => ({ from, to, z });

describe('a stacking game played by hand', () => {
  it('starts with no moves, at its start', () => {
    const g = newStackGame('k', TOWER, false);
    expect(g.moves).toEqual([]);
    expect(g.at).toBe(0);
    expect(stacksAt(g)).toEqual(TOWER);
    expect(discNumbers(stacksAt(g))).toEqual([[0, 1, 2], [], []]);
    expect(solvedAt(g)).toBe(false);
    expect(solvedAt(newStackGame('k', TOWER, true))).toBe(true);
    // the start is the game's own
    TOWER[0]?.pop();
    expect(g.start[0]).toHaveLength(3);
    TOWER[0]?.push({ disc: 2, z: 2 });
  });

  it('counts every transfer, and rests each disc where the engine said', () => {
    let g = newStackGame('k', TOWER, false);
    g = transferred(g, t(0, 2, 0), false);
    g = transferred(g, t(0, 1, 0), false);
    // the same disc again, onto a third rod, is another move: the solver counts transfers
    g = transferred(g, t(1, 2, 1), false);
    expect(g.moves).toHaveLength(3);
    expect(g.at).toBe(3);
    expect(stacksAt(g)).toEqual([
      [{ disc: 0, z: 0 }],
      [],
      [
        { disc: 2, z: 0 },
        { disc: 1, z: 1 },
      ],
    ]);
    expect(stacksAt(g, 1)).toEqual([
      [
        { disc: 0, z: 0 },
        { disc: 1, z: 1 },
      ],
      [],
      [{ disc: 2, z: 0 }],
    ]);
    expect(stacksAt(g, 2)[1]).toEqual([{ disc: 1, z: 0 }]);
  });

  it('takes a move off when its disc is put straight back', () => {
    let g = newStackGame('k', TOWER, false);
    g = transferred(g, t(0, 2, 0), false);
    g = transferred(g, t(2, 0, 2), false);
    expect(g.moves).toEqual([]);
    expect(stacksAt(g)).toEqual(TOWER);
    // back to another rod is a move of its own
    g = transferred(g, t(0, 2, 0), false);
    g = transferred(g, t(2, 1, 0), true);
    expect(g.moves).toEqual([t(0, 2, 0), t(2, 1, 0)]);
    expect(solvedAt(g)).toBe(true);
    expect(solvedAt(g, 1)).toBe(false);
    // and the flag of the move taken off goes with it
    g = transferred(g, t(1, 2, 0), false);
    expect(g.moves).toHaveLength(1);
    expect(g.solved).toEqual([false]);
  });

  it('undoes and redoes by its place in the moves; a new move drops those undone', () => {
    let g = newStackGame('k', TOWER, false);
    g = transferred(g, t(0, 2, 0), false);
    g = transferred(g, t(0, 1, 0), false);
    g = seek(g, 1);
    expect(g.moves).toHaveLength(2);
    expect(discNumbers(stacksAt(g))).toEqual([[0, 1], [], [2]]);
    // a move from there: the one undone is gone
    g = transferred(g, t(2, 1, 0), false);
    expect(g.moves).toEqual([t(0, 2, 0), t(2, 1, 0)]);
    expect(g.at).toBe(2);
    // the last move undone and its disc put back: the move before it goes too
    g = transferred(seek(g, 1), t(2, 0, 2), false);
    expect(g.moves).toEqual([]);
    expect(restarted(transferred(g, t(0, 1, 0), false)).moves).toEqual([]);
  });

  it('takes transfers read from a file, shown from the start', () => {
    const moves = [t(0, 2, 0), t(0, 1, 0)];
    const g = withTransfers(newStackGame('k', TOWER, false), moves, [false, true], 0);
    expect(g.at).toBe(0);
    expect(g.moves).toEqual(moves);
    expect(g.moves[0]).not.toBe(moves[0]);
    expect(solvedAt(seek(g, 2))).toBe(true);
    expect(withTransfers(g, moves, [], 9, true).at).toBe(2);
    expect(withTransfers(g, moves, [], 9, true).startSolved).toBe(true);
  });
});

describe('a stacking puzzle’s moves file', () => {
  const setup: StackSetup = { rods: [3, 3, 3], panex: false, start: [[3, 2, 1], [], []] };
  const text = serializeStack({
    puzzle: 'hanoi.xmpuzzle',
    problem: 'P1 Tower',
    ...setup,
    moves: [
      { from: 0, to: 2 },
      { from: 0, to: 1 },
    ],
  });

  it('is read back as it was written', () => {
    const file = parseStack(text);
    expect(typeof file).toBe('object');
    if (typeof file === 'string') {
      return;
    }
    expect(file.format).toBe(MOVES_FORMAT);
    expect(file.kind).toBe(STACK_KIND);
    expect(file.version).toBe(STACK_MOVES_VERSION);
    expect(file.rods).toEqual([3, 3, 3]);
    expect(file.start).toEqual([[3, 2, 1], [], []]);
    expect(file.moves).toEqual([
      { from: 0, to: 2 },
      { from: 0, to: 1 },
    ]);
    expect(file.problem).toBe('P1 Tower');
    expect(stackMismatch(file, setup)).toBeNull();
  });

  it('is refused for another board or another start', () => {
    expect(stackMismatch(setup, { ...setup, rods: [3, 3] })).toBe(
      'These moves are for another board.',
    );
    expect(stackMismatch(setup, { ...setup, rods: [4, 4, 4] })).toBe(
      'These moves are for another board.',
    );
    expect(stackMismatch(setup, { ...setup, panex: true })).toBe(
      'These moves are for another board.',
    );
    expect(stackMismatch(setup, { ...setup, start: [[3, 2], [1], []] })).toBe(
      'These moves start from another position.',
    );
    // 11 on one rod is not 1 and 1 on two
    expect(
      stackMismatch({ ...setup, start: [[11], [], []] }, { ...setup, start: [[1], [1], []] }),
    ).toBe('These moves start from another position.');
  });

  it('says what is wrong with a text that is no stacking moves file', () => {
    const notOne = 'This is not a ShapeLogic moves file.';
    expect(parseStack('nothing')).toBe(notOne);
    expect(parseStack('{"format":"other","version":1}')).toBe(notOne);
    const sliding = JSON.stringify({ format: MOVES_FORMAT, version: 1, board: {}, moves: [] });
    expect(parseStack(sliding)).toBe('These moves are for a sliding puzzle.');
    const whole = JSON.parse(text) as Record<string, unknown>;
    const edited = (change: Record<string, unknown>): string | object =>
      parseStack(JSON.stringify({ ...whole, ...change }));
    expect(edited({ version: 3 })).toBe(
      'These moves were written by a newer version of ShapeLogic.',
    );
    expect(edited({ kind: 'cubes' })).toBe(
      'These moves were written by a newer version of ShapeLogic.',
    );
    const damaged = 'The moves file is damaged.';
    expect(edited({ rods: [] })).toBe(damaged);
    expect(edited({ rods: [3, 3] })).toBe(damaged);
    expect(edited({ start: [[3, 0], [], []] })).toBe(damaged);
    expect(edited({ moves: [{ from: 0, to: 3 }] })).toBe(damaged);
    expect(edited({ moves: [{ from: 0 }] })).toBe(damaged);
    expect(edited({ moves: 'none' })).toBe(damaged);
  });

  it('is told apart from a sliding puzzle’s by the sliding reader', () => {
    expect(parse(text)).toBe('These moves are for a stacking puzzle.');
  });
});

describe('a stacking board’s drawing', () => {
  const color: [number, number, number] = [0.2, 0.4, 0.8];
  const hanoi: StackModel = {
    rods: [0, 1, 2].map((position) => ({
      name: `rod ${String(position + 1)}`,
      pocket: false,
      position,
      capacity: 3,
    })),
    panex: false,
    channel: false,
    discs: [3, 2, 1].map((size) => ({ size, color, label: String(size) })),
  };

  it('gives every rod a column as wide as the largest disc and a gap', () => {
    const look = stackLook(hanoi, 800, 500);
    expect(look.span).toBe(8);
    expect(look.cols).toBe(3);
    expect(look.levels).toBe(3);
    expect(look.width).toBeLessThanOrEqual(800);
    expect(look.height).toBeLessThanOrEqual(500);
    // the rods' middles a column apart, in order
    const xs = [0, 1, 2].map((r) => rodX(hanoi, look, r));
    expect((xs[1] ?? 0) - (xs[0] ?? 0)).toBeCloseTo(look.span * look.u);
    expect((xs[2] ?? 0) - (xs[1] ?? 0)).toBeCloseTo(look.span * look.u);
    // a point belongs to the nearest rod
    expect(rodAt(hanoi, look, (xs[0] ?? 0) + 3)).toBe(0);
    expect(rodAt(hanoi, look, (xs[1] ?? 0) - look.u)).toBe(1);
    expect(rodAt(hanoi, look, look.width)).toBe(2);
    // a level above another, a disc centerd on its rod
    expect(levelY(look, 1)).toBeCloseTo(levelY(look, 0) - look.v);
    const [x, y] = discAt(hanoi, look, 3, 1, 2);
    expect(x + (7 * look.u) / 2).toBeCloseTo(xs[1] ?? 0);
    expect(y).toBeCloseTo(levelY(look, 2));
  });

  it('shortens a label too wide for its disc to its words’ first letters', () => {
    expect(fittedLabel('Right 1', 7)).toBe('Right 1');
    expect(fittedLabel('Right 1', 6)).toBe('R 1');
    expect(fittedLabel('Left 12', 4)).toBe('L 12');
    expect(fittedLabel('Big red disc', 5)).toBe('B r d');
    expect(fittedLabel('Right 1', 2.9)).toBe('R1');
    expect(fittedLabel('Right 1', 1)).toBe('R1');
    expect(fittedLabel('Big red disc', 3)).toBe('Brd');
    // on a narrow board the smallest disc has room for the short form only
    const named: StackModel = {
      ...hanoi,
      discs: hanoi.discs.map((d) => ({ ...d, label: d.size === 1 ? 'Right 1' : 'Left 12' })),
    };
    const svg = stackSvg(named, TOWER, stackLook(named, 400, 300));
    expect(svg).toContain('>R 1</text>');
    expect(svg).toContain('>Left 12</text>');
  });

  it('fits a small room, as the goal’s picture does', () => {
    const look = stackLook(hanoi, 178, 132, true);
    expect(look.width).toBeLessThanOrEqual(178);
    expect(look.height).toBeLessThanOrEqual(132);
    const svg = stackSvg(hanoi, TOWER, look, true);
    expect(svg).toContain('class="sk-svg small"');
    // no strips to take the pointer, no labels, a bar for each disc
    expect(svg).not.toContain('class="sk-col"');
    expect(svg).not.toContain('sk-name');
    expect(svg.match(/class="sk-bar"/g)).toHaveLength(3);
  });

  it('draws the pegs, the base, the discs where they rest and a strip a rod', () => {
    const look = stackLook(hanoi, 800, 500);
    const svg = stackSvg(hanoi, TOWER, look);
    expect(svg.match(/class="sk-peg"/g)).toHaveLength(3);
    expect(svg.match(/class="sk-base"/g)).toHaveLength(1);
    expect(svg.match(/class="sk-col"/g)).toHaveLength(3);
    expect(svg).toContain('data-i="2" data-rod="0" data-z="2"');
    expect(svg).toContain('<title>Rod 2</title>');
    expect(svg).not.toContain('sk-bridge');
    expect(svg.match(/class="sk-name"/g)).toHaveLength(3);
  });

  it('stands the pocket left of rod 1 and draws a Panex board’s steps and bridge', () => {
    const panex: StackModel = {
      rods: [
        { name: 'rod 1', pocket: false, position: 0, capacity: 4 },
        { name: 'rod 2', pocket: false, position: 1, capacity: 4 },
        { name: 'the pocket column', pocket: true, position: -1, capacity: 1 },
      ],
      panex: true,
      channel: false,
      discs: [3, 2, 1].map((size) => ({ size, color, label: '' })),
    };
    expect(rodColumn(panex, 2)).toBe(0);
    expect(rodColumn(panex, 0)).toBe(1);
    expect(rodColumn(panex, 1)).toBe(2);
    const look = stackLook(panex, 800, 500);
    expect(look.levels).toBe(4);
    expect(rodX(panex, look, 2)).toBeLessThan(rodX(panex, look, 0));
    const svg = stackSvg(panex, [[{ disc: 0, z: 0 }], [], []], look);
    // three levels of steps under the bridge for each of the two columns, none for the pocket
    expect(svg.match(/class="sk-step"/g)).toHaveLength(6);
    expect(svg.match(/class="sk-bridge"/g)).toHaveLength(1);
    expect(svg).not.toContain('sk-channel');
    expect(svg).toContain('<title>The pocket column</title>');
    // a disc without a label has no text
    expect(svg).not.toContain('sk-name');
  });

  it('draws the outside channel over the bridge, above where a moving disc crosses', () => {
    const rods = [0, 1, 2].map((position) => ({
      name: `rod ${String(position + 1)}`,
      pocket: false,
      position,
      capacity: 4,
    }));
    const panex: StackModel = {
      rods,
      panex: true,
      channel: true,
      discs: [3, 2, 1].map((size) => ({ size, color, label: '' })),
    };
    const look = stackLook(panex, 800, 500);
    const svg = stackSvg(panex, [[{ disc: 0, z: 0 }], [], []], look);
    expect(svg.match(/class="sk-channel"/g)).toHaveLength(1);
    // its top is higher than a moving disc's (the lowest y the board gives one)
    const d = /class="sk-channel" d="M[\d.]+ [\d.]+V[\d.]+Q[\d.]+ ([\d.]+) /.exec(svg);
    expect(Number(d?.[1])).toBeLessThan(levelY(look, look.levels - 1 + LIFT));
    // the goal's small picture has no room for it
    expect(stackSvg(panex, [[], [], []], stackLook(panex, 178, 132, true), true)).not.toContain(
      'sk-channel',
    );
  });
});
