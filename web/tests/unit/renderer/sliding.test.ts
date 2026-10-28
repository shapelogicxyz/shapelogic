// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sliding front end's DOM-free parts (docs/webui-spec.md, section 8):
// the Solver's sliding options over the stored settings (the original's
// boxes, its solver names, Find Solutions at first), the Details line's
// counts and the Activity line's solver, the tray's marks and stamps from sliding.tray, the stamp
// outline's colours and the validity bar's words, and the walls around a
// tray's floor (voxelframe.cpp's buildSlidingWalls).

import { describe, expect, it } from 'vitest';

import type { ShapeSummary } from '../../../src/engine/api.generated.ts';
import { newOrder } from '../../../src/renderer/shell/files-order.ts';
import {
  chipColour,
  fromHex,
  TRAY_LIST,
  TRAY_VIEW,
  viewColour,
} from '../../../src/renderer/shell/piece-colours.ts';
import {
  DEFAULT_LIMITS,
  normalize,
  readOptions,
  savedDetails,
  searchActive,
  shortCount,
  slidingDetails,
  solveOptions,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';
import { marksOf, outlineColour, copiesOf, validity } from '../../../src/renderer/entities/tray.ts';
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
  disc: null,
  discColour: null,
});

describe('the sliding options', () => {
  it('start with Find Solutions; read the sliding boxes, not a solver', () => {
    const o = readOptions({}, undefined, 'sliding');
    expect(o).toMatchObject({
      disassemble: true,
      nestedSlides: false,
      verticalMoves: false,
    });
    // a brick puzzle's defaults are as they were
    expect(readOptions({}).disassemble).toBe(false);
    // High Memory is the Search memory setting now, not an option
    expect('highMemory' in readOptions({ highMemory: true }, undefined, 'sliding')).toBe(false);
    const s = readOptions(
      {
        solverType: 'deep',
        nestedSlides: true,
        partialNested: true,
        verticalMoves: true,
        highMemory: true,
      },
      undefined,
      'sliding',
    );
    expect(s).toMatchObject({
      nestedSlides: true,
      partialNested: true,
      verticalMoves: true,
      solver: 'classic',
    });
    // the one sliding solver: a stored solver type plays no part
    expect('slidingSolver' in s).toBe(false);
    expect(readOptions({ solverType: 'crowell' }, undefined, 'sliding').solver).toBe('classic');
    // the original's sliding solver names on a brick puzzle keep the default
    expect(readOptions({ solverType: 'fast' }).solver).toBe('classic');
  });

  it('store the sliding boxes with a sliding puzzle only, and no solver', () => {
    const o = readOptions({}, undefined, 'sliding');
    const w = writeOptions(
      { ...o, verticalMoves: true },
      { other: 'x', highMemory: false, solverType: 'deep' },
      'sliding',
    );
    expect(w).toMatchObject({
      // as the problem stored them
      solverType: 'deep',
      highMemory: false,
      nestedSlides: false,
      partialNested: false,
      verticalMoves: true,
      other: 'x',
    });
    const b = writeOptions(readOptions({}), { highMemory: true });
    expect(b.solverType).toBe('classic');
    // a brick puzzle keeps what it stored of them
    expect(b.highMemory).toBe(true);
    expect('nestedSlides' in b).toBe(false);
    expect('verticalMoves' in b).toBe(false);
  });

  it('follow updateSolverOptionCheckboxes: no symmetry, levels or colours; partial needs nested', () => {
    const o = {
      ...readOptions({}, undefined, 'sliding'),
      keepMirrors: true,
      keepRotations: true,
      deepSymmetry: true,
      strictColors: true,
      justLevels: true,
      checkRotations: true,
      partialNested: true,
    };
    const n = normalize(o, null, 'sliding');
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
    expect(
      normalize({ ...o, nestedSlides: true }, null, 'sliding').disabled.has('partialNested'),
    ).toBe(false);
    const counted = normalize({ ...o, justCount: true }, 'justCount', 'sliding');
    expect(counted.options.disassemble).toBe(false);
    expect(counted.disabled.has('disassemble')).toBe(true);
  });

  it('map to SolveOptions.sliding, partially nested only with nested', () => {
    const o = { ...readOptions({}, undefined, 'sliding'), partialNested: true };
    expect(solveOptions(o, DEFAULT_LIMITS, 'sliding')).toEqual({
      disassemble: true,
      justCount: false,
      keepAtMost: 100,
      keepEach: 1,
      sliding: {
        nestedSlides: false,
        partialNested: false,
        verticalMoves: false,
      },
    });
    expect(
      solveOptions({ ...o, verticalMoves: true }, DEFAULT_LIMITS, 'sliding').sliding?.verticalMoves,
    ).toBe(true);
    expect(
      solveOptions({ ...o, nestedSlides: true }, DEFAULT_LIMITS, 'sliding').sliding?.partialNested,
    ).toBe(true);
    expect(solveOptions(readOptions({}), DEFAULT_LIMITS).sliding).toBeUndefined();
  });

  it('say the search as the original’s Activity line, on the Details line', () => {
    expect(shortCount(0)).toBe('0');
    expect(shortCount(999_999)).toBe('999,999');
    expect(shortCount(1_234_567)).toBe('1.2 million');
    expect(shortCount(999_960_000)).toBe('1.00 billion');
    expect(shortCount(23_534_000_000)).toBe('23.53 billion');
    expect(slidingDetails(12_345, 7)).toBe('7 moves deep. Scanned 12,345 arrangements.');
    expect(slidingDetails(5, 0)).toBe('Scanned 5 arrangements.');
  });

  it('say what the solves went through as the original’s finished Activity line', () => {
    expect(savedDetails('sliding', 181_440, 31, 0)).toBe(
      'Scanned 181,440 arrangements. Searched 31 moves deep.',
    );
    expect(savedDetails('brick', 2_500_000, 0, 928)).toBe(
      'Scanned 2.5 million search steps. Tried to take apart 928 assemblies.',
    );
    expect(savedDetails('stacking', 12, 0, 0)).toBe('Scanned 12 stackings.');
    expect(savedDetails('brick', 0, 0, 0)).toBe('');
  });

  it('name the solver of a search under way', () => {
    expect(searchActive('Complete (ShapeLogic)')).toBe(
      'Search active with the Complete (ShapeLogic) solver',
    );
    expect(searchActive('Sliding Puzzle Solver')).toBe(
      'Search active with the Sliding Puzzle Solver',
    );
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
    placed: [
      {
        piece: 's2',
        goal: false,
        cells: [
          [0, 0, 0],
          [1, 0, 0],
        ],
      },
      { piece: 's3', goal: false, cells: [[2, 1, 0]] },
      {
        piece: 's2',
        goal: true,
        cells: [
          [1, 1, 0],
          [2, 1, 0],
        ],
      },
    ],
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

  it('gives each copy’s cells on START and GOAL, as the engine split them', () => {
    expect(copiesOf(tray, false)).toEqual([
      {
        piece: 's2',
        cells: [
          [0, 0, 0],
          [1, 0, 0],
        ],
      },
      { piece: 's3', cells: [[2, 1, 0]] },
    ]);
    expect(copiesOf(tray, true)).toEqual([
      {
        piece: 's2',
        cells: [
          [1, 1, 0],
          [2, 1, 0],
        ],
      },
    ]);
    // two copies of one piece side by side are two entries
    const two: TrayReply = {
      ...tray,
      placed: [
        { piece: 's3', goal: false, cells: [[0, 0, 0]] },
        { piece: 's3', goal: false, cells: [[1, 0, 0]] },
      ],
    };
    expect(copiesOf(two, false).map((c) => c.piece)).toEqual(['s3', 's3']);
    // a tray of layers: the marks of every layer
    const deep = {
      ...tray,
      size: [1, 1, 3] as [number, number, number],
      starts: [null, 's2', 's2'],
      goals: [null, null, null],
    };
    expect([...marksOf(deep, false, shapes).keys()]).toEqual(['0,0,1', '0,0,2']);
  });

  it('has the colours, outline and validity as the original', () => {
    expect(chipColour(shapes[0], 0)).toEqual(TRAY_LIST);
    expect(viewColour(shapes[0], 0)).toEqual(TRAY_VIEW);
    // a board the user gave a colour shows it
    const painted = { ...shapes[0], colour: '#336699' };
    expect(chipColour(painted, 0)).toEqual(fromHex('#336699'));
    expect(viewColour(painted, 0)).toEqual(fromHex('#336699'));
    expect(outlineColour({ fits: true, there: true })).toBe('rgb(245,150,30)');
    expect(outlineColour({ fits: true, there: false })).toBe('rgb(60,210,80)');
    expect(outlineColour({ fits: false, there: false })).toBe('rgb(230,40,40)');
    expect(validity(null)).toEqual({
      text: 'Valid Start/Goal positions for pieces in Start and Goal of this board',
      valid: true,
    });
    expect(validity('S2 has 3 starts but the puzzle has 2 copies of it.')).toEqual({
      text: 'Invalid Start/Goal positions - S2 has 3 starts but the puzzle has 2 copies of it.',
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
