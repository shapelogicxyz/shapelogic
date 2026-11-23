// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle in the front end (decision 0045): the words for a
// tray, the map from the board's grid to the tray's frame, the tray's
// picture, File ▸ New's order, the piece colors around the board, and the
// Solver's Pieces may be turned over.

import { describe, expect, it } from 'vitest';

import type { Tray } from '../../../src/engine/api.generated.ts';
import { newOrder } from '../../../src/renderer/shell/files-order.ts';
import { chipColor, setBoardCount, shapeColor } from '../../../src/renderer/shell/piece-colors.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_OPTIONS,
  readOptions,
  solveOptions,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';
import {
  boardToTray,
  cellState,
  isTiltBoard,
  isTilted,
  parseAllowance,
  parseWhole,
  sizeText,
  slopeText,
  squareOf,
  trayFacts,
  trayPicture,
  unfitIds,
  unfitNote,
} from '../../../src/renderer/state/tilted.ts';
import {
  checkerBlocks,
  prismEdges,
  prismTriangles,
  wholeSquares,
} from '../../../src/renderer/view3d/tray3d.ts';

/** A 2 by 2 tray turned 45° (1 down, 1 over), as the engine would send it. */
function diamond(): Tray {
  const r = Math.SQRT1_2;
  // the tray's corner (0, 0) at board point (1, 0.5); its x edge along (r, -r), its y edge along (r, r)
  const c0: [number, number] = [1, 0.5];
  return {
    width: 2,
    height: 2,
    over: 1,
    down: 1,
    allowance: 0,
    placement: 'best',
    shiftX: 0,
    shiftY: 0,
    walls: [[1, 0]],
    degrees: 45,
    whole: 1,
    cut: 2,
    box: [3, 2],
    cells: 'cwcooo',
    corners: [
      c0,
      [c0[0] + 2 * r, c0[1] - 2 * r],
      [c0[0] + 4 * r, c0[1]],
      [c0[0] + 2 * r, c0[1] + 2 * r],
    ],
    wallPolygons: [],
  };
}

/**
 * A straight 3 by 2 tray (3 wide, 2 high) with a wall at (2, 1), as the
 * engine would send it: the board is the tray, cell for cell.
 */
function lying(): Tray {
  return {
    width: 3,
    height: 2,
    over: 1,
    down: 0,
    allowance: 0,
    placement: 'best',
    shiftX: 0,
    shiftY: 0,
    walls: [[2, 1]],
    degrees: 0,
    whole: 5,
    cut: 1,
    box: [3, 2],
    cells: 'wwwwwc',
    corners: [
      [0, 0],
      [3, 0],
      [3, 2],
      [0, 2],
    ],
    wallPolygons: [
      [
        [2, 1],
        [3, 1],
        [3, 2],
        [2, 2],
      ],
    ],
  };
}

describe('a tilted tray in words', () => {
  it('names the slope, the angle and what the tray gives', () => {
    expect(slopeText(1, 2)).toBe('1 down, 2 over');
    expect(slopeText(0, 1)).toBe('straight');
    expect(sizeText(8, 6)).toBe('8 by 6');
    expect(trayFacts({ width: 8, height: 8, whole: 45, degrees: 26.565051, walls: [] })).toEqual([
      'Tray: 8 by 8',
      'Whole cells: 45',
      'Tilt angle: 26.57°',
    ]);
    expect(trayFacts({ width: 8, height: 6, whole: 1, degrees: 45, walls: [[0, 0]] })).toEqual([
      'Tray: 8 by 6',
      'Whole cells: 1',
      'Tilt angle: 45.00°',
      'Walls: 1',
    ]);
  });

  it('reads the fields, refusing what the engine would', () => {
    expect(parseAllowance('0.05')).toBe(0.05);
    expect(parseAllowance('0,025')).toBe(0.025);
    expect(parseAllowance('0')).toBe(0);
    expect(parseAllowance('0.3')).toBeNull();
    expect(parseAllowance('-1')).toBeNull();
    expect(parseAllowance('')).toBeNull();
    expect(parseAllowance('a')).toBeNull();
    expect(parseWhole('8', 1, 64)).toBe(8);
    expect(parseWhole('0', 1, 64)).toBeNull();
    expect(parseWhole('65', 1, 64)).toBeNull();
    expect(parseWhole('2.5', 1, 64)).toBeNull();
    expect(squareOf('3,7')).toEqual([3, 7]);
    expect(squareOf('3')).toBeNull();
    expect(squareOf(undefined)).toBeNull();
  });

  it('knows a tilted puzzle and its board', () => {
    expect(isTilted(null)).toBe(false);
    expect(isTiltBoard({ board: true })).toBe(true);
    expect(isTiltBoard({ board: false })).toBe(false);
    expect(isTiltBoard(undefined)).toBe(false);
  });
});

describe('the tray in its own frame', () => {
  it('maps the board grid onto the tray: its corners onto the tray corners', () => {
    const t = diamond();
    const toTray = boardToTray(t);
    const near = (p: readonly [number, number], q: [number, number]): void => {
      expect(p[0]).toBeCloseTo(q[0], 9);
      expect(p[1]).toBeCloseTo(q[1], 9);
    };
    near(toTray(t.corners[0]), [0, 0]);
    near(toTray(t.corners[1]), [2, 0]);
    near(toTray(t.corners[2]), [2, 2]);
    near(toTray(t.corners[3]), [0, 2]);
  });

  it('maps a rectangular tray by its width and its height', () => {
    // 3 wide and 5 high, turned 1 down 2 over: its x edge along (2, -1) / √5 in the board's grid
    const h = Math.sqrt(5);
    const c0: [number, number] = [0.3, 1.7];
    const ex = [2 / h, -1 / h] as const;
    const ey = [1 / h, 2 / h] as const;
    const at = (u: number, v: number): [number, number] => [
      c0[0] + u * ex[0] + v * ey[0],
      c0[1] + u * ex[1] + v * ey[1],
    ];
    const toTray = boardToTray({
      width: 3,
      height: 5,
      corners: [at(0, 0), at(3, 0), at(3, 5), at(0, 5)],
    });
    for (const [u, v] of [
      [0, 0],
      [3, 0],
      [3, 5],
      [0, 5],
      [1.25, 4.5],
    ] as const) {
      const p = toTray(at(u, v));
      expect(p[0]).toBeCloseTo(u, 9);
      expect(p[1]).toBeCloseTo(v, 9);
    }
  });

  it('reads the board cells row by row from the bottom', () => {
    const t = diamond();
    expect(cellState(t, 0, 0)).toBe('c');
    expect(cellState(t, 1, 0)).toBe('w');
    expect(cellState(t, 0, 1)).toBe('o');
    expect(cellState(t, 3, 0)).toBe('o');
    expect(cellState(t, -1, 0)).toBe('o');
  });

  it('draws both views alike, the turned cells under the squares, and names which stands out', () => {
    const t = diamond();
    for (const view of ['tray', 'cells'] as const) {
      const svg = trayPicture(t, { px: 100, view, id: 'c1', editable: true });
      expect(svg).toContain(`class="tpic tp-${view}"`);
      // the squares to click, the wall among them
      expect(svg.match(/data-sq="/g)?.length).toBe(4);
      expect(svg).toContain('class="tp-sq wall" data-sq="1,0"');
      // the turned cells, clipped to the tray, with the wall over them
      expect(svg.match(/tp-whole/g)?.length).toBe(1);
      expect(svg.match(/tp-cut/g)?.length).toBe(2);
      expect(svg.match(/class="tp-wall wall"/g)?.length).toBe(1);
      expect(svg).toContain('clip-path="url(#c1)"');
      // the squares drawn after the cells, so they take the pointer
      expect(svg.indexOf('tp-sq')).toBeGreaterThan(svg.indexOf('tp-whole'));
    }
    // without hit targets (the New dialog's example)
    expect(trayPicture(t, { px: 100, view: 'cells', id: 'c2' })).not.toContain('data-sq');
    // a square tray's picture is square: px and the margin on each side
    expect(trayPicture(t, { px: 100, view: 'cells', id: 'c3', margin: 5 })).toContain(
      'viewBox="0 0 110 110" width="110" height="110"',
    );
  });

  it('draws a rectangular tray as a rectangle, its longer side px long', () => {
    const t = lying();
    const svg = trayPicture(t, { px: 150, view: 'tray', id: 'r1', margin: 5, editable: true });
    // 3 by 2 squares of 50 pixels, and the margin
    expect(svg).toContain('viewBox="0 0 160 110" width="160" height="110"');
    expect(svg).toContain('data-size="3x2"');
    // the floor, the clip and the outline are the tray's rectangle
    const rect = 'x="5.00" y="5.00" width="150.00" height="100.00"';
    expect(svg).toContain(`<rect class="tp-floor" ${rect}/>`);
    expect(svg).toContain(`<clipPath id="r1"><rect ${rect}/></clipPath>`);
    expect(svg).toContain(`<rect class="tp-edge" ${rect}/>`);
    // a square a tray square, y up: (0, 0) at the bottom left, the wall (2, 1) at the top right
    expect(svg.match(/data-sq="/g)?.length).toBe(6);
    expect(svg).toContain(
      '<rect class="tp-sq" data-sq="0,0" x="5.00" y="55.00" width="50.00" height="50.00"/>',
    );
    expect(svg).toContain(
      '<rect class="tp-sq wall" data-sq="2,1" x="105.00" y="5.00" width="50.00" height="50.00"/>',
    );
    // the cells: five whole, the one under the wall cut
    expect(svg.match(/tp-whole/g)?.length).toBe(5);
    expect(svg.match(/tp-cut/g)?.length).toBe(1);
    expect(svg).toContain(
      '<polygon class="tp-cut" points="105.00,55.00 155.00,55.00 155.00,5.00 105.00,5.00"/>',
    );

    // higher than wide: px is the height
    const upright: Tray = {
      ...t,
      width: 2,
      height: 3,
      walls: [],
      box: [2, 3],
      cells: 'wwwwww',
      corners: [
        [0, 0],
        [2, 0],
        [2, 3],
        [0, 3],
      ],
      wallPolygons: [],
    };
    const tall = trayPicture(upright, { px: 150, view: 'cells', id: 'r2', margin: 5 });
    expect(tall).toContain('viewBox="0 0 110 160" width="110" height="160"');
    expect(tall).toContain('data-size="2x3"');
    expect(tall).toContain(
      '<rect class="tp-edge" x="5.00" y="5.00" width="100.00" height="150.00"/>',
    );
  });
});

describe('File ▸ New', () => {
  it('lists Tilted Square after Stacking', () => {
    const kinds = ['cairo', 'tilted', 'bricks', 'stacking', 'sliding', 'prisms'] as const;
    expect([...kinds].sort((a, b) => newOrder(a) - newOrder(b)).slice(0, 4)).toEqual([
      'bricks',
      'sliding',
      'stacking',
      'tilted',
    ]);
  });
});

describe('the colors around the board', () => {
  it('give the board the board color and the pieces the colors from the first', () => {
    setBoardCount(1);
    try {
      expect(chipColor({ color: null, board: true }, 0)).toEqual(
        chipColor({ color: null, startGoal: 1 }, 0),
      );
      expect(chipColor({ color: null }, 1)).toEqual(shapeColor(null, 1));
      setBoardCount(0);
      const plain = shapeColor(null, 0);
      setBoardCount(1);
      expect(shapeColor(null, 1)).toEqual(plain);
    } finally {
      setBoardCount(0);
    }
  });
});

describe('Pieces may be turned over', () => {
  it('is on unless a problem stored it off', () => {
    expect(DEFAULT_OPTIONS.flipPieces).toBe(true);
    expect(readOptions({}).flipPieces).toBe(true);
    expect(readOptions({ flipPieces: false }).flipPieces).toBe(false);
  });

  it('is stored only for a tilted puzzle’s problem', () => {
    const off = { ...DEFAULT_OPTIONS, flipPieces: false };
    expect(writeOptions(off, {}, 'brick', true).flipPieces).toBe(false);
    expect(writeOptions(off, {}, 'brick').flipPieces).toBeUndefined();
  });

  it('reaches the solve only when off', () => {
    expect(solveOptions(DEFAULT_OPTIONS, DEFAULT_LIMITS).flipPieces).toBeUndefined();
    expect(solveOptions({ ...DEFAULT_OPTIONS, flipPieces: false }, DEFAULT_LIMITS).flipPieces).toBe(
      false,
    );
  });
});

describe('the tray in 3D', () => {
  const square: [number, number][] = [
    [0, 0],
    [1, 0],
    [1, 1],
    [0, 1],
  ];
  it('makes each wall square a block: 12 triangles, 12 edges', () => {
    const tri = prismTriangles([square, square], 0, 1);
    // two blocks, 12 triangles each, 3 corners of 3 numbers
    expect(tri.length).toBe(2 * 12 * 9);
    expect(prismEdges([square], 0, 1).length).toBe(12 * 6);
    // from z0 to z1, moved by the offset
    const moved = prismTriangles([square], 0, 1, [10, 20, -0.5]);
    const zs = [...moved].filter((_, i) => i % 3 === 2);
    expect(Math.min(...zs)).toBe(-0.5);
    expect(Math.max(...zs)).toBe(0.5);
    const xs = [...moved].filter((_, i) => i % 3 === 0);
    expect(Math.min(...xs)).toBe(10);
    expect(Math.max(...xs)).toBe(11);
  });

  it('leaves out what is not four-sided', () => {
    expect(prismTriangles([square.slice(0, 3)], 0, 1).length).toBe(0);
    expect(prismEdges([square.slice(0, 3)], 0, 1).length).toBe(0);
  });

  it('draws the board as its whole cells, blocks in a checker of two colors', () => {
    const t = { box: [3, 2] as [number, number], cells: 'wwcwoo' };
    const sq = wholeSquares(t);
    // the whole cells (0,0), (1,0) and (0,1); cut and outside cells left out
    expect(sq.map((s) => s.quad[0])).toEqual([
      [0, 0],
      [1, 0],
      [0, 1],
    ]);
    expect(sq.map((s) => s.tone)).toEqual([0, 1, 1]);
    const b = checkerBlocks(t, 0, 1);
    expect(b.positions.length).toBe(3 * 12 * 9);
    expect(b.colors.length).toBe(b.positions.length);
    // the top faces (the second six corners of a block) in the two colors, unshaded
    const top = (i: number): number[] => [
      ...b.colors.slice((i * 36 + 6) * 3, (i * 36 + 6) * 3 + 3),
    ];
    expect(top(1)).toEqual(top(2));
    expect(top(0)).not.toEqual(top(1));
  });
});

describe('the pieces a tray change leaves off the board', () => {
  const shapes = [
    { id: 'b', label: 'B', fits: true },
    { id: 'p1', label: 'S1', fits: false },
    { id: 'p2', label: 'S2', fits: false },
    { id: 'p3', label: 'S3', fits: false },
  ];
  it('names those that fitted before', () => {
    expect([...unfitIds(shapes)]).toEqual(['p1', 'p2', 'p3']);
    expect(unfitNote(new Set(['p1', 'p2', 'p3']), shapes)).toBeNull();
    expect(unfitNote(new Set(['p1', 'p2']), shapes)).toBe(
      'S3 no longer fits the board: no place has every cell on a whole cell',
    );
    expect(unfitNote(new Set(), shapes)).toBe(
      'S1, S2 and S3 no longer fit the board: no place has every cell on a whole cell',
    );
    expect(unfitNote(new Set(['p1']), shapes)).toBe(
      'S2 and S3 no longer fit the board: no place has every cell on a whole cell',
    );
  });
});
