// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace's DOM-free parts: the search options over the stored
// settings (both spellings, the original's rules), the poses between the
// engine's samples (straight, slerp, the fade of a piece taken away), the
// solutions pager (pages by version, solutionsAdded at the version paged
// ignored), and the solve records' state machine on events made up here
// (the recorded engine's are in ../solver-replay.test.ts).

import { describe, expect, it } from 'vitest';

import type { SolutionRow, SolverSettings } from '../../../src/engine/api.generated.ts';
import type { EngineApi } from '../../../src/engine/client-types.ts';
import type { PoseArrays } from '../../../src/engine/frames.ts';
import { cellScale } from '../../../src/renderer/view3d/extent.ts';
import { PAGE, SolutionPager } from '../../../src/renderer/solver/pager.ts';
import { CHUNK, PoseTrack, SAMPLES, slerp } from '../../../src/renderer/solver/poses.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_OPTIONS,
  normalize,
  readOptions,
  solveOptions,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';
import { Solves } from '../../../src/renderer/state/solves.ts';

describe('the search options', () => {
  it('read the stored names, the boxes as booleans, the choices by value', () => {
    const o = readOptions({
      disassemble: true,
      deepSymmetry: true,
      strictColors: true,
      justLevels: false,
      autosave: false,
      solverType: 'crowell',
      sortBy: 'level',
    });
    expect(o).toMatchObject({
      disassemble: true,
      deepSymmetry: true,
      strictColors: true,
      autosave: false,
      solver: 'crowell',
      sortBy: 'level',
    });
    // an unknown solver type (a sliding one) or sort keeps the default
    expect(readOptions({ solverType: 'deep', sortBy: 'colour' })).toMatchObject({
      solver: 'classic',
      sortBy: 'moves',
    });
    // a problem that stored nothing: the original's widgets as made
    expect(readOptions({})).toEqual(DEFAULT_OPTIONS);
  });

  it('write the stored names over what was stored, keeping what this version does not show', () => {
    const stored: SolverSettings = {
      highMemory: true,
      nestedSlides: false,
      futureSetting: 'x',
      futureBox: '1',
      sortBy: 'moves',
    };
    const w = writeOptions({ ...DEFAULT_OPTIONS, deepSymmetry: true, solver: 'crowell' }, stored);
    expect(w).toMatchObject({
      highMemory: true,
      nestedSlides: false,
      futureSetting: 'x',
      futureBox: '1',
      deepSymmetry: true,
      solverType: 'crowell',
      autosave: true,
    });
    expect(readOptions(w)).toMatchObject({ deepSymmetry: true, solver: 'crowell' });
  });

  it('map to SolveOptions in the API’s names, the limits not stored', () => {
    const o = solveOptions(
      { ...DEFAULT_OPTIONS, disassemble: true, deepSymmetry: true, strictColors: true },
      { keepAtMost: 7, keepEach: 3 },
    );
    expect(o).toMatchObject({
      completeRotations: true,
      strictColours: true,
      disassemble: true,
      keepAtMost: 7,
      keepEach: 3,
      sortBy: 'moves',
    });
    expect(o).not.toHaveProperty('threads');
    // without the take-apart a sort by moves keeps them unsorted
    expect(solveOptions(DEFAULT_OPTIONS, DEFAULT_LIMITS).sortBy).toBe('unsorted');
    expect(DEFAULT_LIMITS).toEqual({ keepAtMost: 100, keepEach: 1 });
  });

  it('follow the original’s rules (updateSolverOptionCheckboxes)', () => {
    const base = { ...DEFAULT_OPTIONS, disassemble: true, checkRotations: true };
    // Just Count drops the take-apart and Just Levels
    let n = normalize({ ...base, justCount: true, justLevels: true }, 'justCount');
    expect(n.options).toMatchObject({
      disassemble: false,
      justLevels: false,
      checkRotations: false,
    });
    expect([...n.disabled].sort()).toEqual(['checkRotations', 'disassemble', 'justLevels']);
    // Just Levels newly checked turns Check Rotations off, keeps it available, takes apart
    n = normalize({ ...base, disassemble: false, justLevels: true }, 'justLevels');
    expect(n.options).toMatchObject({ disassemble: true, checkRotations: false, justCount: false });
    expect(n.disabled.has('checkRotations')).toBe(false);
    expect(n.disabled.has('disassemble')).toBe(true);
    // Just Levels already on: Check Rotations stays as it is
    expect(normalize({ ...base, justLevels: true }, null).options.checkRotations).toBe(true);
    // no take-apart: no Check Rotations
    n = normalize({ ...base, disassemble: false }, 'disassemble');
    expect(n.options.checkRotations).toBe(false);
    expect(n.disabled.has('checkRotations')).toBe(true);
    // Keep Rotated turns the rotation filter off: the thorough check and Keep Mirror go
    n = normalize({ ...base, keepRotations: true, deepSymmetry: true, keepMirrors: true }, null);
    expect(n.options).toMatchObject({ deepSymmetry: false, keepMirrors: false });
    expect(n.disabled.has('deepSymmetry') && n.disabled.has('keepMirrors')).toBe(true);
    // the thorough check rules Keep Rotated out
    n = normalize({ ...base, deepSymmetry: true }, null);
    expect(n.disabled.has('keepRotations')).toBe(true);
  });

  it('only assemble on a grid without a take-apart (spheres, rhombic, tetra-octa)', () => {
    const base = { ...DEFAULT_OPTIONS, disassemble: true, checkRotations: true, justLevels: true };
    const n = normalize(base, null, 'brick', false);
    expect(n.options).toMatchObject({
      disassemble: false,
      justLevels: false,
      checkRotations: false,
    });
    expect([...n.disabled].sort()).toEqual(['checkRotations', 'disassemble', 'justLevels']);
    // a stored take-apart is read as Assemble Only
    expect(readOptions({ disassemble: true }, undefined, 'brick', false).disassemble).toBe(false);
  });

  it('place cells by the original’s recalcSpaceCoordinates spacing', () => {
    expect(cellScale('bricks')).toEqual([1, 1, 1]);
    expect(cellScale('prisms')).toEqual([0.5, Math.sqrt(3) / 2, 1]);
    expect(cellScale('spheres')).toEqual([Math.SQRT1_2, Math.SQRT1_2, Math.SQRT1_2]);
    expect(cellScale('rhombic')).toEqual([0.4, 0.4, 0.4]);
    expect(cellScale('tetraOcta')[0]).toBeCloseTo(1 / 3);
  });
});

/** Samples of a made-up playback: piece 0 moves along x, piece 1 turns 90° about z in step 1, piece 2 goes at step 2. */
function chunkArrays(track: PoseTrack, chunk: number): PoseArrays {
  const { from, to } = track.range(chunk);
  const samples = (to - from) * SAMPLES + 1;
  const n = track.pieces;
  const at = new Float32Array(samples);
  const translation = new Float32Array(samples * n * 3);
  const rotation = new Float32Array(samples * n * 4);
  const visible = new Uint8Array(samples * n);
  for (let k = 0; k < samples; k++) {
    const step = from + k / SAMPLES;
    at[k] = step;
    for (let i = 0; i < n; i++) {
      const b = k * n + i;
      translation[3 * b] = i === 0 ? step : i;
      const angle = i === 1 ? (Math.min(1, Math.max(0, step - 1)) * Math.PI) / 2 : 0;
      rotation[4 * b + 2] = Math.sin(angle / 2);
      rotation[4 * b + 3] = Math.cos(angle / 2);
      visible[b] = i === 2 && step >= 2 ? 0 : 1;
    }
  }
  return { at, translation, rotation, visible };
}

describe('the poses', () => {
  it('equal the samples at whole steps and lie between them in between', () => {
    const track = new PoseTrack(3, 3);
    expect(track.at(0, false)).toBeNull();
    expect(track.missing(0.5)).toEqual([0]);
    track.add(0, chunkArrays(track, 0));
    const at1 = track.at(1, false);
    expect(at1?.[0]?.t).toEqual([1, 0, 0]);
    expect(at1?.[1]?.q[3]).toBe(1);
    // a quarter of a step between two samples: straight for the move
    const p = track.at(1 + 0.5 / SAMPLES, false);
    expect(p?.[0]?.t[0]).toBeCloseTo(1 + 0.5 / SAMPLES, 6);
    // the turn half way: 45 degrees, slerp, unit
    const q = track.at(1.5, false)?.[1]?.q ?? [0, 0, 0, 1];
    expect(2 * Math.acos(q[3])).toBeCloseTo(Math.PI / 4, 5);
    expect(Math.hypot(...q)).toBeCloseTo(1, 6);
    expect(track.at(3, false)?.[2]?.visible).toBe(false);
  });

  it('fade a piece taken away over its last step, with the setting', () => {
    const track = new PoseTrack(3, 3);
    track.add(0, chunkArrays(track, 0));
    expect(track.at(1.25, true)?.[2]).toMatchObject({ visible: true, opacity: 0.75 });
    expect(track.at(1.25, false)?.[2]).toMatchObject({ visible: true, opacity: 1 });
    expect(track.at(1, true)?.[2]?.opacity).toBe(1);
    expect(track.at(2, true)?.[2]?.visible).toBe(false);
    // the others stay opaque
    expect(track.at(1.25, true)?.[0]?.opacity).toBe(1);
  });

  it('come in chunks, a chunk’s last step the next one’s first', () => {
    const track = new PoseTrack(3, CHUNK * 2 + 5);
    expect(track.chunkCount).toBe(3);
    expect(track.range(1)).toEqual({ from: CHUNK, to: 2 * CHUNK });
    expect(track.range(2)).toEqual({ from: 2 * CHUNK, to: 2 * CHUNK + 5 });
    track.add(0, chunkArrays(track, 0));
    // the boundary step is the first chunk's last sample
    expect(track.at(CHUNK, false)?.[0]?.t[0]).toBe(CHUNK);
    expect(track.at(CHUNK + 0.1, false)).toBeNull();
    expect(track.missing(CHUNK + 0.1)).toEqual([1]);
    track.add(1, chunkArrays(track, 1));
    expect(track.at(CHUNK + 0.5, false)?.[0]?.t[0]).toBeCloseTo(CHUNK + 0.5, 4);
    expect(() => {
      track.add(2, chunkArrays(track, 1));
    }).toThrow(/samples/);
  });

  it('are one pose for a solution without moves (steps 0)', () => {
    const track = new PoseTrack(2, 0);
    expect(track.range(0)).toEqual({ from: 0, to: 0 });
    track.add(0, chunkArrays(track, 0));
    expect(track.at(0, true)?.map((p) => p.visible)).toEqual([true, true]);
    expect(track.at(5, true)?.[0]?.t).toEqual([0, 0, 0]);
  });

  it('slerp the short way and keep unit quaternions', () => {
    const a: [number, number, number, number] = [0, 0, 0, 1];
    const b: [number, number, number, number] = [0, 0, -Math.SQRT1_2, -Math.SQRT1_2];
    const m = slerp(a, b, 0.5);
    // -b is +90° about z: half way is +45°
    expect(2 * Math.atan2(m[2], m[3])).toBeCloseTo(Math.PI / 4, 6);
    expect(slerp(a, b, 0)).toEqual(a);
    // a quarter of the way: 22.5 degrees (a normalised straight line gives 21.6)
    const r = slerp(a, b, 0.25);
    expect(2 * Math.atan2(r[2], r[3])).toBeCloseTo(Math.PI / 8, 6);
    expect(Math.hypot(...slerp(a, a, 0.3))).toBeCloseTo(1, 9);
  });
});

/** A made-up engine for the pager: the list is `total` rows at `version`. */
function pageEngine(): {
  engine: Pick<EngineApi, 'call'>;
  calls: { offset: number }[];
  list: { total: number; version: number };
} {
  const list = { total: 250, version: 7 };
  const calls: { offset: number }[] = [];
  const row = (i: number): SolutionRow => ({
    id: `sol${String(i + 1)}`,
    index: i,
    assembly: i,
    solution: i,
    pieces: 3,
    disassembly: 'tree',
    moves: 2,
    rotations: 0,
    steps: 2,
    movesText: '1.1',
  });
  const engine = {
    call: (method: string, params?: unknown) => {
      if (method !== 'solution.page') {
        throw new Error(method);
      }
      const p = params as { offset: number; limit: number };
      calls.push({ offset: p.offset });
      const rows = Array.from(
        { length: Math.max(0, Math.min(p.limit, list.total - p.offset)) },
        (_, k) => row(p.offset + k),
      );
      return Promise.resolve({
        total: list.total,
        offset: p.offset,
        solutionsVersion: list.version,
        rows,
      });
    },
  } as unknown as Pick<EngineApi, 'call'>;
  return { engine, calls, list };
}

const settle = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

describe('the solutions pager', () => {
  it('asks for the pages in sight once, at their version', async () => {
    const { engine, calls } = pageEngine();
    const pager = new SolutionPager(engine, 'p1', 'moves', true);
    pager.want(0, 30);
    await settle();
    expect(calls).toEqual([{ offset: 0 }]);
    expect(pager.total).toBe(250);
    expect(pager.version).toBe(7);
    expect(pager.row(5)?.id).toBe('sol6');
    pager.want(90, 130);
    await settle();
    expect(calls.map((c) => c.offset)).toEqual([0, 100]);
    expect(pager.current(0, 200)).toBe(true);
    expect(pager.indexOf('sol150')).toBe(149);
    pager.want(10, 20);
    await settle();
    expect(calls).toHaveLength(2);
  });

  it('ignores solutionsAdded at its version; reads again at another', async () => {
    const { engine, calls, list } = pageEngine();
    const pager = new SolutionPager(engine, 'p1', 'list', false);
    pager.want(0, PAGE);
    await settle();
    pager.added(7);
    await settle();
    expect(calls).toHaveLength(1);
    list.version = 9;
    list.total = 120;
    pager.added(9);
    await settle();
    expect(calls).toHaveLength(2);
    expect(pager.version).toBe(9);
    expect(pager.total).toBe(120);
  });

  it('reads the other pages again when one comes at a newer version; keeps their rows meanwhile', async () => {
    const { engine, calls, list } = pageEngine();
    const pager = new SolutionPager(engine, 'p1', 'list', false);
    pager.want(0, 200);
    await settle();
    expect(calls).toHaveLength(2);
    list.version = 8;
    pager.refresh();
    expect(pager.row(150)?.id).toBe('sol151');
    expect(pager.current(0, 200)).toBe(false);
    await settle();
    expect(calls).toHaveLength(4);
    expect(pager.current(0, 200)).toBe(true);
    pager.close();
    pager.refresh();
    await settle();
    expect(calls).toHaveLength(4);
  });
});

/** Events made up for the solve records. */
function eventBus(): {
  on: EngineApi['on'];
  emit: (event: string, params: unknown) => void;
} {
  const handlers = new Map<string, ((p: unknown) => void)[]>();
  return {
    on: ((event: string, h: (p: unknown) => void) => {
      handlers.set(event, [...(handlers.get(event) ?? []), h]);
      return () => undefined;
    }) as EngineApi['on'],
    emit: (event, params) => {
      for (const h of handlers.get(event) ?? []) {
        h(params);
      }
    },
  };
}

describe('the solve records', () => {
  it('end a run at the document.changed after its last state', async () => {
    const bus = eventBus();
    const s = new Solves(bus);
    s.launch('p1', 'classic');
    let end = -2;
    void s.whenEnded('p1').then((r) => (end = r));
    bus.emit('solve.state', { problem: 'p1', state: 'assembling' });
    bus.emit('document.changed', { revision: 4, what: [], shapes: [], problems: ['p1'] });
    expect(s.get('p1').phase).toBe('active');
    bus.emit('solve.state', { problem: 'p1', state: 'finished' });
    expect(s.get('p1').phase).toBe('ending');
    expect(s.running).toBe(true);
    // a change of another problem is not this run's end
    bus.emit('document.changed', { revision: 5, what: [], shapes: [], problems: ['p2'] });
    expect(s.get('p1').phase).toBe('ending');
    bus.emit('document.changed', { revision: 6, what: [], shapes: [], problems: ['p1'] });
    await settle();
    expect(s.get('p1')).toMatchObject({ phase: 'none', endRevision: 6 });
    expect(end).toBe(6);
    expect(s.running).toBe(false);
  });

  it('take a state told again for no run (a replaced document’s last idle)', () => {
    const bus = eventBus();
    const s = new Solves(bus);
    bus.emit('solve.state', { problem: 'p1', state: 'idle' });
    expect(s.get('p1').phase).toBe('none');
    bus.emit('solve.state', { problem: 'p1', state: 'idle' });
    expect(s.get('p1').phase).toBe('none');
    // a state that moved between two ends is a run that started and ended between polls
    bus.emit('solve.state', { problem: 'p1', state: 'finished' });
    expect(s.get('p1').phase).toBe('ending');
    // a solve paused in memory aborted ends no run
    const t = new Solves(bus);
    bus.emit('solve.state', { problem: 'p2', state: 'paused' });
    bus.emit('solve.state', { problem: 'p2', state: 'idle' });
    expect(t.get('p2').phase).toBe('none');
  });

  it('end no run when a finished solve goes back to idle (its problem edited, or cleared)', async () => {
    const bus = eventBus();
    const s = new Solves(bus);
    s.launch('p1', 'classic');
    bus.emit('solve.state', { problem: 'p1', state: 'finished' });
    bus.emit('document.changed', { revision: 3, what: [], shapes: [], problems: ['p1'] });
    await settle();
    expect(s.get('p1')).toMatchObject({ phase: 'none', endRevision: 3 });
    // the edit's document.changed comes first, then the poll's idle
    bus.emit('document.changed', { revision: 4, what: ['problems'], shapes: [], problems: ['p1'] });
    bus.emit('solve.state', { problem: 'p1', state: 'idle' });
    expect(s.get('p1').phase).toBe('none');
    expect(s.running).toBe(false);
    // and an error cleared the same way
    bus.emit('solve.state', { problem: 'p2', state: 'error' });
    bus.emit('solve.state', { problem: 'p2', state: 'idle' });
    expect(s.get('p2').phase).toBe('none');
  });

  it('wait for a launched run before its first state, and keep a failure until the next launch', async () => {
    const bus = eventBus();
    const s = new Solves(bus);
    s.launch('p1', 'classic');
    let ended = false;
    void s.whenEnded('p1').then(() => (ended = true));
    await settle();
    expect(ended).toBe(false);
    bus.emit('solve.state', { problem: 'p1', state: 'error' });
    bus.emit('solve.error', {
      problem: 'p1',
      failure: { assembler: 'tooFewUnits', param: 0, message: 'too few', file: '', line: 0 },
    });
    bus.emit('document.changed', { revision: 9, what: [], shapes: [], problems: ['p1'] });
    await settle();
    expect(ended).toBe(true);
    expect(s.get('p1').failure?.message).toBe('too few');
    s.launch('p1', 'classic');
    expect(s.get('p1').failure).toBeNull();
  });

  it('resolve a pending whenEnded when the document is replaced (clear), so autosave never hangs', async () => {
    const bus = eventBus();
    const s = new Solves(bus);
    s.launch('p1', 'classic');
    bus.emit('solve.state', { problem: 'p1', state: 'assembling' });
    const ended = s.whenEnded('p1');
    // another document arrives while a caller waits for this run's end
    s.clear();
    const outcome = await Promise.race([
      ended.then((r) => `resolved:${String(r)}`),
      new Promise<string>((r) => {
        setTimeout(() => {
          r('hung');
        }, 100);
      }),
    ]);
    expect(outcome).toBe('resolved:-1');
  });
});
