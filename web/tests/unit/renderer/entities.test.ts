// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace's logic without a page: a stroke's protocol (one
// edit.begin, batches of one frame with one at the engine at a time, the
// group closed whatever failed), the planes' maps, the client's squares
// of the cube planes, the quick tools of the modifiers, mirror and span's
// cells, the cube grid's faces, and the store waiting for a revision.

import { describe, expect, it } from 'vitest';

import type { EngineApi } from '../../../src/engine/client-types.ts';
import { EngineError } from '../../../src/engine/errors.ts';
import {
  actFor,
  BRICK_FACES,
  brickNeighbour,
  effectiveTool,
} from '../../../src/renderer/entities/tools.ts';
import { expand, planeMap } from '../../../src/renderer/entities/editor-state.ts';
import type { Cell } from '../../../src/renderer/entities/editor-state.ts';
import { squareLayer } from '../../../src/renderer/entities/layer-geometry.ts';
import { Stroke, strokeLabel } from '../../../src/renderer/entities/stroke.ts';
import { Store } from '../../../src/renderer/state/store.ts';
import type { EngineLink } from '../../../src/renderer/state/store.ts';

interface Call {
  method: string;
  params: Record<string, unknown> | undefined;
  coalesce: string | undefined;
  resolve(v: unknown): void;
  reject(e: unknown): void;
}

/** An engine whose calls wait until the test answers them. */
function scripted(): {
  engine: EngineApi & EngineLink;
  calls: Call[];
  emit: (e: string, p: unknown) => void;
} {
  const calls: Call[] = [];
  const handlers = new Map<string, ((p: unknown) => void)[]>();
  const engine = {
    status: null,
    onStatus: () => () => undefined,
    on: (e: string, h: (p: unknown) => void) => {
      handlers.set(e, [...(handlers.get(e) ?? []), h]);
      return () => undefined;
    },
    onEvent: () => () => undefined,
    call: (method: string, params?: Record<string, unknown>, options?: { coalesce?: string }) =>
      new Promise((resolve, reject) => {
        calls.push({ method, params, coalesce: options?.coalesce, resolve, reject });
      }),
  } as unknown as EngineApi & EngineLink;
  return {
    engine,
    calls,
    emit: (e, p) => {
      for (const h of handlers.get(e) ?? []) {
        h(p);
      }
    },
  };
}

const tick = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

/** Frames the test runs by hand. */
function frames(): { wait: (f: () => void) => void; run(): void; pending(): number } {
  let queue: (() => void)[] = [];
  return {
    wait: (f) => queue.push(f),
    run: () => {
      const q = queue;
      queue = [];
      for (const f of q) {
        f();
      }
    },
    pending: () => queue.length,
  };
}

const tool = { shape: 's1', tool: 'fixed' as const, colour: null, mirror: [], span: [] };

describe('a stroke', () => {
  it('opens a group, sends one batch a frame with one at the engine, and closes after the last', async () => {
    const { engine, calls } = scripted();
    const f = frames();
    const errors: unknown[] = [];
    const s = new Stroke(engine, tool, (e) => errors.push(e), f.wait);
    expect(calls.map((c) => c.method)).toEqual(['edit.begin']);
    expect(calls[0]?.params).toEqual({ label: 'Draw fixed voxels' });
    s.add([[0, 0, 0]]);
    s.add([
      [1, 0, 0],
      [0, 0, 0],
    ]);
    f.run();
    // nothing goes before edit.begin is answered
    expect(calls).toHaveLength(1);
    calls.shift()?.resolve({});
    await tick();
    f.run();
    expect(calls.map((c) => c.method)).toEqual(['shape.setCells']);
    expect(calls[0]?.params).toEqual({
      shape: 's1',
      tool: 'fixed',
      cells: [
        [0, 0, 0],
        [1, 0, 0],
      ],
    });
    expect(s.has([1, 0, 0])).toBe(true);
    // cells entered while a batch is at the engine wait for its reply
    s.add([[2, 0, 0]]);
    s.add([[3, 0, 0]]);
    f.run();
    expect(calls).toHaveLength(1);
    calls.shift()?.resolve({ revision: 5, changed: 2 });
    await tick();
    f.run();
    expect(calls[0]?.params?.cells).toEqual([
      [2, 0, 0],
      [3, 0, 0],
    ]);
    s.add([[4, 0, 0]]);
    const ended = s.end();
    // the end waits for the batch at the engine, then sends the rest and edit.end
    await tick();
    expect(calls).toHaveLength(1);
    calls.shift()?.resolve({ revision: 6, changed: 2 });
    await tick();
    await tick();
    expect(calls.map((c) => c.method)).toEqual(['shape.setCells', 'edit.end']);
    expect(calls[0]?.params?.cells).toEqual([[4, 0, 0]]);
    calls.shift()?.resolve({ revision: 7, changed: 1 });
    calls.shift()?.resolve({ revision: 7 });
    expect(await ended).toBe(7);
    expect(s.batches).toBe(3);
    expect(errors).toEqual([]);
    // nothing after the end
    s.add([[5, 0, 0]]);
    f.run();
    expect(calls).toEqual([]);
  });

  it('closes the group whatever failed inside it', async () => {
    const { engine, calls } = scripted();
    const f = frames();
    const errors: unknown[] = [];
    const s = new Stroke(
      engine,
      { ...tool, tool: 'erase', mirror: ['x'], span: ['z'] },
      (e) => errors.push(e),
      f.wait,
    );
    calls.shift()?.resolve({});
    await tick();
    s.add([[0, 0, 0]]);
    f.run();
    expect(calls[0]?.params).toEqual({
      shape: 's1',
      tool: 'erase',
      cells: [[0, 0, 0]],
      mirror: ['x'],
      span: ['z'],
    });
    calls
      .shift()
      ?.reject(new EngineError(-32002, 'not_found', 'there is no such shape', 'shape.setCells'));
    await tick();
    expect(errors).toHaveLength(1);
    s.add([[1, 0, 0]]);
    const ended = s.end();
    await tick();
    expect(calls.map((c) => c.method)).toEqual(['edit.end']);
    calls.shift()?.resolve({ revision: 3 });
    expect(await ended).toBeNull();
  });

  it('sends nothing more when edit.begin is refused', async () => {
    const { engine, calls } = scripted();
    const f = frames();
    const errors: unknown[] = [];
    const s = new Stroke(engine, tool, (e) => errors.push(e), f.wait);
    calls.shift()?.reject(new EngineError(-32006, 'invalid_state', 'pause first', 'edit.begin'));
    await tick();
    s.add([[0, 0, 0]]);
    f.run();
    expect(await s.end()).toBeNull();
    expect(calls).toEqual([]);
    expect(errors).toHaveLength(1);
  });

  it('names its step by the tool', () => {
    expect(['fixed', 'variable', 'erase', 'paint'].map((t) => strokeLabel(t as 'fixed'))).toEqual([
      'Draw fixed voxels',
      'Draw variable voxels',
      'Erase voxels',
      'Paint voxels',
    ]);
  });
});

describe('the editor', () => {
  it('maps the planes: a column, row and layer to a cell and back', () => {
    for (const plane of ['XY', 'XZ', 'YZ'] as const) {
      const p = planeMap(plane, [2, 3, 4]);
      expect([p.cols * p.rows * p.layers]).toEqual([24]);
      for (let l = 0; l < p.layers; l++) {
        for (let r = 0; r < p.rows; r++) {
          for (let c = 0; c < p.cols; c++) {
            const cell = p.xyz(c, r, l);
            expect(cell.every((v, i) => v >= 0 && v < ([2, 3, 4][i] ?? 0))).toBe(true);
            expect(p.crl(cell)).toEqual([c, r, l]);
          }
        }
      }
    }
    expect(planeMap('XZ', [2, 3, 4]).la).toBe('Y');
    expect(planeMap('YZ', [2, 3, 4]).ca).toBe('Z');
  });

  it('draws the cube planes as squares whose neighbours agree', () => {
    const g = squareLayer('XZ', [3, 4, 2], 1);
    expect(g.width).toBe(3);
    expect(g.height).toBe(2);
    expect(g.cells).toHaveLength(6);
    expect(g.cells.every((c) => c.at[1] === 1)).toBe(true);
    g.cells.forEach((c, i) => {
      for (const n of c.neighbours ?? []) {
        if (n >= 0) {
          expect(g.cells[n]?.neighbours).toContain(i);
        }
      }
    });
    // seen from +Y: the columns run along +X, the rows down +Z (X 0, Z 0 at the top left)
    expect(g.index.get('2,1,1')).toBe(2);
    expect(g.cells[0]?.at).toEqual([0, 1, 1]);
    expect(g.cells[5]?.at).toEqual([2, 1, 0]);
    // and YZ from +X: its columns run along -Z
    expect(squareLayer('YZ', [3, 4, 2], 0).cells[0]?.at).toEqual([0, 0, 1]);
  });

  it('takes the quick tools as the mockup: Ctrl erase, Alt variable, Shift fixed', () => {
    const m = (ctrl: boolean, alt: boolean, shift: boolean) => ({ ctrl, alt, shift });
    expect(effectiveTool(m(false, false, false), 'paint')).toBe('paint');
    expect(effectiveTool(m(true, true, true), 'paint')).toBe('erase');
    expect(effectiveTool(m(false, true, true), 'paint')).toBe('variable');
    expect(effectiveTool(m(false, false, true), 'paint')).toBe('fixed');
    // in 3D a plain click edits only in edit mode
    expect(actFor(m(false, false, false), false, 'paint')).toBeNull();
    expect(actFor(m(false, false, false), true, 'paint')).toBe('paint');
    expect(actFor(m(false, false, true), false, 'erase')).toBe('fixed');
  });

  it('gives the cells of mirror and span as the core does', () => {
    const k = (cells: Cell[]): string[] => cells.map((c) => c.join(',')).sort();
    const size: Cell = [4, 3, 2];
    expect(k(expand([0, 1, 0], size, new Set(), new Set()))).toEqual(['0,1,0']);
    expect(k(expand([0, 0, 0], size, new Set(['x']), new Set()))).toEqual(['0,0,0', '3,0,0']);
    expect(k(expand([0, 0, 0], size, new Set(['x', 'y']), new Set()))).toEqual([
      '0,0,0',
      '0,2,0',
      '3,0,0',
      '3,2,0',
    ]);
    // the middle row of an odd size is its own mirror
    expect(k(expand([1, 1, 0], size, new Set(['y']), new Set()))).toEqual(['1,1,0']);
    expect(k(expand([1, 1, 1], size, new Set(), new Set(['x'])))).toEqual([
      '0,1,1',
      '1,1,1',
      '2,1,1',
      '3,1,1',
    ]);
    // spans run along their own rows, not across each other
    expect(k(expand([0, 0, 0], size, new Set(), new Set(['x', 'z'])))).toEqual([
      '0,0,0',
      '0,0,1',
      '1,0,0',
      '2,0,0',
      '3,0,0',
    ]);
    expect(expand([0, 0, 0], size, new Set(['x']), new Set(['y']))).toHaveLength(6);
  });

  it('knows the cube grid’s faces: -x, -y, -z, +x, +y, +z', () => {
    expect(BRICK_FACES).toHaveLength(6);
    expect(brickNeighbour([1, 1, 1], 0, [3, 3, 3])).toEqual([0, 1, 1]);
    expect(brickNeighbour([1, 1, 1], 5, [3, 3, 3])).toEqual([1, 1, 2]);
    expect(brickNeighbour([0, 1, 1], 0, [3, 3, 3])).toBeNull();
    expect(brickNeighbour([1, 1, 1], 6, [3, 3, 3])).toBeNull();
  });
});

describe('the store and revisions', () => {
  it('reloads the snapshot coalesced, and a call waits for its revision', async () => {
    const { engine, calls, emit } = scripted();
    const store = new Store(engine);
    (engine as unknown as { status: unknown }).status = null;
    const s = (revision: number) => ({
      revision,
      dirty: false,
      grid: 'bricks',
      path: null,
      notes: '',
      canUndo: false,
      canRedo: false,
      solving: false,
      shapes: [],
      colours: [],
      problems: [],
    });
    // as if running: the first load
    (store as unknown as { onStatus(x: unknown): void }).onStatus({
      state: 'running',
      version: '1',
      restarts: 0,
    });
    calls
      .splice(0)
      .find((c) => c.method === 'document.get')
      ?.resolve(s(1));
    await tick();
    expect(store.snapshot?.revision).toBe(1);
    let done = false;
    void store.whenRevision(3).then(() => {
      done = true;
    });
    emit('document.changed', { revision: 2, what: ['shapes'], shapes: [], problems: [] });
    expect(calls[0]?.coalesce).toBe('document.get');
    calls.shift()?.resolve(s(2));
    await tick();
    expect(done).toBe(false);
    emit('document.changed', { revision: 3, what: ['shapes'], shapes: [], problems: [] });
    calls.shift()?.resolve(s(3));
    await tick();
    expect(done).toBe(true);
    // a revision already shown resolves at once
    await store.whenRevision(2);
    // a new document resolves what waited for the old one
    let other = false;
    void store.whenRevision(99).then(() => {
      other = true;
    });
    const made = store.newDocument('spheres');
    expect(calls[0]?.params).toEqual({ grid: 'spheres' });
    calls.shift()?.resolve({ ...s(4), grid: 'spheres' });
    await made;
    await tick();
    expect(other).toBe(true);
    expect(store.generation).toBe(1);
  });
});
