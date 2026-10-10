// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sliding front end's DOM-free parts (docs/webui-spec.md, section 8):
// the Solver's sliding options over the stored settings (the original's
// boxes, its solver names, Find Solutions at first), the Activity line's
// counts, the tray's marks and stamps from sliding.tray, the stamp
// outline's colours and the validity bar's words, and the walls around a
// tray's floor (voxelframe.cpp's buildSlidingWalls).

import { describe, expect, it } from 'vitest';

import type { ShapeSummary } from '../../../src/engine/api.generated.ts';
import { newOrder } from '../../../src/renderer/shell/files-order.ts';
import {
  chipColour,
  TRAY_LIST,
  TRAY_VIEW,
  viewColour,
} from '../../../src/renderer/shell/piece-colours.ts';
import {
  DEFAULT_LIMITS,
  normalize,
  readOptions,
  shortCount,
  slidingActivity,
  solveOptions,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';
import {
  marksOf,
  outlineColour,
  pieceText,
  stampPieces,
  stampsOf,
  validity,
} from '../../../src/renderer/entities/tray.ts';
import type { TrayReply } from '../../../src/renderer/entities/tray.ts';
import { WALL_GAP, wallOutline, wallSegments } from '../../../src/renderer/view3d/walls.ts';

const shape = (id: string, label: string, startGoal: number | null = null): ShapeSummary => ({
  id,
  label,
  name: '',
  size: [2, 2, 1],
  fixed: 1,
  variable: 0,
  weight: 1,
  colour: null,
  startGoal,
});

describe('the sliding options', () => {
  it('start with Find Solutions and the full search; read the sliding solver names', () => {
    const o = readOptions({}, undefined, true);
    expect(o).toMatchObject({ disassemble: true, slidingSolver: 'full', nestedSlides: false });
    // a brick puzzle's defaults are as they were
    expect(readOptions({}).disassemble).toBe(false);
    const s = readOptions(
      { solverType: 'deep', nestedSlides: true, partialNested: true, highMemory: true },
      undefined,
      true,
    );
    expect(s).toMatchObject({
      slidingSolver: 'deep',
      nestedSlides: true,
      partialNested: true,
      highMemory: true,
      solver: 'classic',
    });
    // a brick solver's name on a sliding puzzle, and the reverse, keep the default
    expect(readOptions({ solverType: 'bt2' }, undefined, true).slidingSolver).toBe('full');
    expect(readOptions({ solverType: 'fast' }).solver).toBe('classic');
  });

  it('store the sliding boxes and solver with a sliding puzzle only', () => {
    const o = readOptions({}, undefined, true);
    const w = writeOptions({ ...o, slidingSolver: 'fast', highMemory: true }, { other: 'x' }, true);
    expect(w).toMatchObject({
      solverType: 'fast',
      highMemory: true,
      nestedSlides: false,
      partialNested: false,
      other: 'x',
    });
    const b = writeOptions(readOptions({}), { highMemory: true });
    expect(b.solverType).toBe('classic');
    // a brick puzzle keeps what it stored of them
    expect(b.highMemory).toBe(true);
    expect('nestedSlides' in b).toBe(false);
  });

  it('follow updateSolverOptionCheckboxes: no symmetry, levels or colours; partial needs nested', () => {
    const o = {
      ...readOptions({}, undefined, true),
      keepMirrors: true,
      keepRotations: true,
      deepSymmetry: true,
      strictColors: true,
      justLevels: true,
      checkRotations: true,
      partialNested: true,
    };
    const n = normalize(o, null, true);
    expect(n.options).toMatchObject({
      keepMirrors: false,
      keepRotations: false,
      deepSymmetry: false,
      strictColors: false,
      justLevels: false,
      checkRotations: false,
      partialNested: true,
    });
    expect(n.disabled.has('partialNested')).toBe(true);
    expect(normalize({ ...o, nestedSlides: true }, null, true).disabled.has('partialNested')).toBe(
      false,
    );
    const counted = normalize({ ...o, justCount: true }, 'justCount', true);
    expect(counted.options.disassemble).toBe(false);
    expect(counted.disabled.has('disassemble')).toBe(true);
  });

  it('map to SolveOptions.sliding, partially nested only with nested', () => {
    const o = { ...readOptions({}, undefined, true), partialNested: true, highMemory: true };
    expect(solveOptions(o, DEFAULT_LIMITS, true)).toEqual({
      disassemble: true,
      justCount: false,
      keepAtMost: 100,
      keepEach: 1,
      sliding: { solver: 'full', nestedSlides: false, partialNested: false, highMemory: true },
    });
    expect(
      solveOptions({ ...o, nestedSlides: true }, DEFAULT_LIMITS, true).sliding?.partialNested,
    ).toBe(true);
    expect(solveOptions(readOptions({}), DEFAULT_LIMITS).sliding).toBeUndefined();
  });

  it('say the search as the original’s Activity line', () => {
    expect(shortCount(0)).toBe('0');
    expect(shortCount(999_999)).toBe('999,999');
    expect(shortCount(1_234_567)).toBe('1.2 million');
    expect(shortCount(999_960_000)).toBe('1.00 billion');
    expect(shortCount(23_534_000_000)).toBe('23.53 billion');
    expect(slidingActivity(12_345, 7)).toBe(
      'Sliding search, 7 moves deep. Scanned 12,345 arrangements.',
    );
    expect(slidingActivity(5, 0)).toBe('Sliding search. Scanned 5 arrangements.');
  });
});

describe('a start/goal shape', () => {
  // a 3 by 2 tray: S2 starts at (0,0) and (1,0), its goal at (1,1) and (2,1)
  const tray: TrayReply = {
    number: 1,
    name: '',
    size: [3, 2, 1],
    starts: ['s2', 's2', null, null, null, 's3'],
    goals: [null, null, null, null, 's2', 's2'],
    error: null,
    overflow: null,
    floor: 6,
  };
  const shapes = [shape('s1', 'S1', 1), shape('s2', 'S2'), shape('s3', 'S3')];

  it('marks its cells in the pieces’ colours, the starts or the goals', () => {
    const starts = marksOf(tray, false, shapes);
    expect([...starts.keys()]).toEqual(['0,0,0', '1,0,0', '2,1,0']);
    expect(starts.get('0,0,0')).toEqual(viewColour(shapes[1], 1));
    expect([...marksOf(tray, true, shapes).keys()]).toEqual(['1,1,0', '2,1,0']);
    // a mark of a shape the snapshot does not have is left out
    expect(
      marksOf({ ...tray, starts: ['s9', null, null, null, null, null] }, false, shapes).size,
    ).toBe(0);
  });

  it('gives each piece’s cells on START and GOAL', () => {
    expect([...stampsOf(tray, false)]).toEqual([
      [
        's2',
        [
          [0, 0],
          [1, 0],
        ],
      ],
      ['s3', [[2, 1]]],
    ]);
    expect([...stampsOf(tray, true)]).toEqual([
      [
        's2',
        [
          [1, 1],
          [2, 1],
        ],
      ],
    ]);
  });

  it('lists the pieces but the trays; colours, outline and validity as the original', () => {
    expect(stampPieces(shapes).map((s) => s.id)).toEqual(['s2', 's3']);
    expect(pieceText({ ...shape('s2', 'S2'), name: 'block' })).toBe('S2 - block');
    expect(chipColour(shapes[0], 0)).toEqual(TRAY_LIST);
    expect(viewColour(shapes[0], 0)).toEqual(TRAY_VIEW);
    expect(outlineColour({ fits: true, there: true })).toBe('rgb(245,150,30)');
    expect(outlineColour({ fits: true, there: false })).toBe('rgb(60,210,80)');
    expect(outlineColour({ fits: false, there: false })).toBe('rgb(230,40,40)');
    expect(validity(null)).toEqual({
      text: 'Valid Start/Goal positions for pieces in Start and Goal of this shape',
      valid: true,
    });
    expect(
      validity('Place at least one piece at its Start, and the same piece at its Goal.'),
    ).toEqual({
      text: 'Invalid Start/Goal positions - Place at least one piece at its Start, and the same piece at its Goal.',
      valid: false,
    });
  });

  it('is offered second by New, as the original’s grid selector', () => {
    const grids = ['bricks', 'prisms', 'spheres', 'rhombic', 'tetraOcta', 'sliding'] as const;
    expect([...grids].sort((a, b) => newOrder(a) - newOrder(b))).toEqual([
      'bricks',
      'sliding',
      'prisms',
      'spheres',
      'rhombic',
      'tetraOcta',
    ]);
  });
});

describe('the walls', () => {
  it('outline one cell with four sides a gap outside it and four posts', () => {
    const o = wallOutline(1, 1, () => true);
    expect(o.lines).toHaveLength(16);
    const g = WALL_GAP;
    // each corner of the cell (three walls around it) moves out one gap along both axes
    const want = [-g, -g, 1 + g, -g, -g, 1 + g, 1 + g, 1 + g];
    expect(o.posts).toHaveLength(want.length);
    o.posts.forEach((v, i) => {
      expect(v).toBeCloseTo(want[i] ?? NaN, 9);
    });
  });

  it('run straight past a wall cell and turn at an inner corner (an L of three cells)', () => {
    // cells (0,0), (1,0), (0,1) open; (1,1) a wall
    const open = new Set(['0,0', '1,0', '0,1']);
    const o = wallOutline(2, 2, (x, y) => open.has(`${String(x)},${String(y)}`));
    // eight sides: the L's outline cut at each grid corner
    expect(o.lines).toHaveLength(8 * 4);
    // the inner corner (1,1) has three wall cells around it: a post, moved one gap along each axis
    const posts: [number, number][] = [];
    for (let i = 0; i < o.posts.length; i += 2) {
      posts.push([o.posts[i] ?? 0, o.posts[i + 1] ?? 0]);
    }
    expect(posts).toHaveLength(6);
    expect(
      posts.some(
        ([x, y]) => Math.abs(x - (1 + WALL_GAP)) < 1e-9 && Math.abs(y - (1 + WALL_GAP)) < 1e-9,
      ),
    ).toBe(true);
  });

  it('stand at the floor’s bottom and top, the posts between, centred on the tray', () => {
    const o = wallOutline(1, 1, () => true);
    const s = wallSegments(o, 1, 1, 1);
    // 4 sides twice and 4 posts, two points each
    expect(s.length).toBe((4 * 2 + 4) * 6);
    const zs = new Set<number>();
    for (let i = 2; i < s.length; i += 3) {
      zs.add(s[i] ?? NaN);
    }
    expect([...zs].sort()).toEqual([-0.5, 0.5]);
  });
});
