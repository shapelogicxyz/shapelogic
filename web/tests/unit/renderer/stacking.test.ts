// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stacking puzzles in the front end (deferred part 4, docs/webui-spec.md
// section 9): the Solver's stacking options over the stored settings and
// their rules (updateSolverOptionCheckboxes), SolveOptions.stacking with
// the board's one solver, the Details lines of the Panex and the stacking
// search; the board's boxes as the original's voxelframe.cpp built them
// (addPeg, addRodBase, addPanexV), its middle and the discs' places; the
// disc list's sizes, a rod set's text and the fields its rules grey out,
// the rod slider's places and captions, the validity bar's words, the
// export's size; the main process's refusal of a page's folder settings.

import { describe, expect, it } from 'vitest';

import type { BoardLayout, RodSet } from '../../../src/engine/api.generated.ts';
import { settingsRefusal } from '../../../src/main/path-grants.ts';
import {
  DEFAULT_LIMITS,
  familyOf,
  normalize,
  readOptions,
  solveOptions,
  stackingDetails,
  stackingSolverOf,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';
import {
  discSizeOf,
  rodAt,
  rodCaption,
  rodFieldsOff,
  rodPlaces,
  rodSetText,
  sizeText,
  stackIndex,
  stackValidity,
} from '../../../src/renderer/state/stacking.ts';
import {
  boardBoxes,
  boardCentre,
  boardExtent,
  discAt,
  PANEX_GREY,
  PEG_GREY,
} from '../../../src/renderer/view3d/board.ts';

const rodSet = (r: Partial<RodSet> = {}): RodSet => ({
  id: 'r1',
  label: 'R1',
  name: '',
  rodCount: 3,
  rods: 3,
  growHeight: true,
  definedHeight: 8,
  sizeMatters: true,
  distanceMatters: false,
  panexColumns: false,
  pocketColumn: false,
  pocketHeight: 1,
  outsideChannel: false,
  ...r,
});

const layout = (l: Partial<BoardLayout> = {}): BoardLayout => ({
  spacing: 7,
  rodCount: 3,
  rodHeight: 3,
  rodX: [0, 7, 14],
  rodHeights: [3, 3, 3],
  panex: false,
  pocket: false,
  maxSize: 3,
  discs: [],
  ...l,
});

describe('the stacking solver options', () => {
  it('start with Find Solutions; the original stored no solver; a stored High Memory kept', () => {
    expect(familyOf('stacking')).toBe('stacking');
    expect(familyOf('sliding')).toBe('sliding');
    expect(familyOf('prisms')).toBe('brick');
    const o = readOptions({}, undefined, 'stacking');
    expect(o.disassemble).toBe(true);
    expect(readOptions({}).disassemble).toBe(false);
    // a stored solver type is not a stacking one
    expect(readOptions({ solverType: 'crowell' }, undefined, 'stacking').solver).toBe('classic');
    const w = writeOptions(o, { solverType: 'kept', other: 'x', highMemory: true }, 'stacking');
    expect(w.solverType).toBe('kept');
    expect(w.highMemory).toBe(true);
    expect(w.other).toBe('x');
    expect('nestedSlides' in w).toBe(false);
  });

  it('follow the original rules: Just Count or Just Levels, no symmetry or colours', () => {
    const o = {
      ...readOptions({}, undefined, 'stacking'),
      strictColors: true,
      keepMirrors: true,
      deepSymmetry: true,
      checkRotations: true,
    };
    const n = normalize(o, null, 'stacking');
    expect(n.options).toMatchObject({
      strictColors: false,
      keepMirrors: false,
      deepSymmetry: false,
      checkRotations: false,
      disassemble: true,
    });
    const counted = normalize({ ...o, justCount: true }, 'justCount', 'stacking');
    expect(counted.options).toMatchObject({ disassemble: false, justLevels: false });
    expect([...counted.disabled].sort()).toEqual(['disassemble', 'justLevels']);
    const levels = normalize({ ...o, justCount: true, justLevels: true }, 'justLevels', 'stacking');
    expect(levels.options).toMatchObject({ disassemble: true, justCount: false });
    expect([...levels.disabled].sort()).toEqual(['disassemble', 'justCount']);
  });

  it('solve with the board one solver and the search own autosave', () => {
    expect(stackingSolverOf(true)).toBe('panex');
    expect(stackingSolverOf(false)).toBe('stacking');
    const o = { ...readOptions({}, undefined, 'stacking'), autosave: false };
    expect(solveOptions(o, DEFAULT_LIMITS, 'stacking', 'panex')).toEqual({
      disassemble: true,
      justCount: false,
      keepAtMost: 100,
      keepEach: 1,
      stacking: { solver: 'panex', autosave: false },
    });
    expect(solveOptions(o, DEFAULT_LIMITS).stacking).toBeUndefined();
  });

  it('tell the search on the Details line as the original’s Activity line', () => {
    expect(stackingDetails(true, 12_345, 7, 0)).toBe('7 moves deep. Found 12,345 stackings.');
    expect(stackingDetails(true, 1, 881, 40)).toBe('Tracing the path. Traced 40 of 881 moves.');
    expect(stackingDetails(false, 2_500_000, 0, 0)).toBe('Scanned 2.5 million stackings.');
  });
});

describe('the board in the 3D view', () => {
  it('has a peg per rod and the base under them (addPeg, addRodBase)', () => {
    const boxes = boardBoxes(layout());
    expect(boxes).toEqual([
      { min: [0, 0, 0], max: [1, 1, 3], colour: PEG_GREY },
      { min: [7, 0, 0], max: [8, 1, 3], colour: PEG_GREY },
      { min: [14, 0, 0], max: [15, 1, 3], colour: PEG_GREY },
      { min: [0, 0, -1], max: [15, 1, 0], colour: PEG_GREY },
    ]);
  });

  it('has a V behind each Panex column, not the pocket (addPanexV)', () => {
    const lay = layout({
      rodCount: 4,
      rodX: [7, 14, 21, 0],
      rodHeights: [3, 3, 3, 2],
      panex: true,
      pocket: true,
    });
    const boxes = boardBoxes(lay);
    const vs = boxes.filter((b) => b.colour === PANEX_GREY);
    // three columns of three layers; the pocket (the last rod) a plain peg of its height
    expect(vs).toHaveLength(9);
    expect(boxes.filter((b) => b.colour === PEG_GREY)).toHaveLength(5);
    expect(boxes).toContainEqual({ min: [0, 0, 0], max: [1, 1, 2], colour: PEG_GREY });
    // narrowing to the top: half widths 3, 2, 1 at depths 3, 2, 1, one row behind the largest disc
    expect(vs.slice(0, 3)).toEqual([
      { min: [4, 4, 0], max: [11, 5, 1], colour: PANEX_GREY },
      { min: [5, 4, 1], max: [10, 5, 2], colour: PANEX_GREY },
      { min: [6, 4, 2], max: [9, 5, 3], colour: PANEX_GREY },
    ]);
    // the base from the pocket (left of rod 1) to the last rod
    expect(boxes.at(-1)).toEqual({ min: [0, 0, -1], max: [22, 1, 0], colour: PEG_GREY });
  });

  it('is centred as the poses are; a disc stands by its centre', () => {
    const lay = layout();
    // the engine's stackingCentre (stacking.board's centre)
    expect(boardCentre(lay)).toEqual([7.5, 0.5, 1.5]);
    expect(boardCentre(layout({ rodX: [] }))).toEqual([0, 0, 0]);
    expect(discAt([14, 0, 2], 1, [7.5, 0.5, 1.5])).toEqual([5.5, -1.5, 0.5]);
    expect(boardExtent(lay)).toEqual([21, 7, 5]);
    expect(boardExtent(layout({ panex: true }))).toEqual([21, 9, 5]);
  });
});

describe('the stacking cards', () => {
  it('take a disc size from 1 to 100 (DiskSelector::onSize)', () => {
    expect(discSizeOf('3')).toBe(3);
    expect(discSizeOf(' 100 ')).toBe(100);
    expect(discSizeOf('0')).toBeNull();
    expect(discSizeOf('101')).toBeNull();
    expect(discSizeOf('2.5')).toBeNull();
    expect(discSizeOf('')).toBeNull();
    expect(discSizeOf('x')).toBeNull();
  });

  it('name a rod set and grey out its fields as the original (loadRodFields, syncPanexFields)', () => {
    expect(rodSetText(rodSet())).toBe('R1');
    expect(rodSetText(rodSet({ label: 'R2', name: 'Hanoi' }))).toBe('R2 - Hanoi');
    expect([...rodFieldsOff(rodSet())].sort()).toEqual([
      'definedHeight',
      'outsideChannel',
      'pocketColumn',
      'pocketHeight',
    ]);
    expect([...rodFieldsOff(rodSet({ growHeight: false, panexColumns: true }))].sort()).toEqual([
      'pocketHeight',
      'sizeMatters',
    ]);
    expect(
      [...rodFieldsOff(rodSet({ panexColumns: true, pocketColumn: true, rodCount: 2 }))].sort(),
    ).toEqual(['definedHeight', 'outsideChannel', 'sizeMatters']);
  });

  it('put the pocket first on the rod slider (RodIndexBar_c, selectedStackRod)', () => {
    const plain = rodPlaces(3, false);
    expect([0, 1, 2].map((v) => rodAt(plain, v))).toEqual([0, 1, 2]);
    expect([0, 1, 2].map((v) => rodCaption(plain, v))).toEqual(['Rod: 1', 'Rod: 2', 'Rod: 3']);
    const pocket = rodPlaces(4, true);
    // the pocket is the stack map's last rod
    expect([0, 1, 2, 3].map((v) => rodAt(pocket, v))).toEqual([3, 0, 1, 2]);
    expect([0, 1, 2, 3].map((v) => rodCaption(pocket, v))).toEqual([
      'Rod: P',
      'Rod: 1',
      'Rod: 2',
      'Rod: 3',
    ]);
    expect(rodAt(plain, 9)).toBe(2);
    expect(rodPlaces(0, false).count).toBe(1);
  });

  it('word the validity bar and the export question as the original', () => {
    expect(stackValidity(null)).toEqual({ valid: true, text: 'Valid' });
    expect(stackValidity('S1 is missing from the goal')).toEqual({
      valid: false,
      text: 'Invalid - S1 is missing from the goal',
    });
    // the list shows the top first
    expect(stackIndex(3, 0)).toBe(2);
    expect(stackIndex(3, 2)).toBe(0);
    expect(sizeText(1_234_000_000)).toBe('1.2 GB');
    expect(sizeText(850_000_000)).toBe('850 MB');
  });
});

describe('the engine folders', () => {
  it('are the app own: a page settings.set naming one is refused', () => {
    expect(settingsRefusal('settings.set', { panexFolder: '/tmp/x' })).toMatch(/panexFolder/);
    expect(settingsRefusal('settings.set', { slideFolder: '/', threads: 2 })).toMatch(
      /slideFolder/,
    );
    expect(settingsRefusal('settings.set', { threads: 1, undoDepth: 10 })).toBeNull();
    expect(settingsRefusal('settings.get', { panexFolder: 'x' })).toBeNull();
    expect(settingsRefusal('settings.set', undefined)).toBeNull();
  });
});
