// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's store and mesh cache over a stand-in for the engine proxy:
// no call before the engine runs; everything loaded when it runs and again
// when it was restarted (a new document, ids not taken for the old ones);
// document.changed reloads the snapshot, and only the newest load counts;
// meshes coalesced by their key and dropped by what document.changed names.

import { describe, expect, it } from 'vitest';

import type { EngineStatus } from '../../../src/engine/client-types.ts';
import { EngineError } from '../../../src/engine/errors.ts';
import { MeshCache } from '../../../src/renderer/view3d/meshcache.ts';
import { Store, toBase64 } from '../../../src/renderer/state/store.ts';
import type { EngineLink } from '../../../src/renderer/state/store.ts';

interface Call {
  method: string;
  params: unknown;
  coalesce: string | undefined;
  resolve(v: unknown): void;
  reject(e: unknown): void;
}

class FakeEngine {
  status: EngineStatus | null = null;
  calls: Call[] = [];
  private statusHandlers: ((s: EngineStatus) => void)[] = [];
  private eventHandlers = new Map<string, ((p: unknown) => void)[]>();
  readonly link: EngineLink;

  constructor() {
    // eslint-disable-next-line @typescript-eslint/no-this-alias
    const self = this;
    this.link = {
      get status() {
        return self.status;
      },
      onStatus: (h) => {
        this.statusHandlers.push(h);
        return () => undefined;
      },
      on: (e, h) => {
        const list = this.eventHandlers.get(e) ?? [];
        list.push(h as (p: unknown) => void);
        this.eventHandlers.set(e, list);
        return () => undefined;
      },
      onEvent: () => () => undefined,
      call: (method: string, params?: unknown, options?: { coalesce?: string }) =>
        new Promise((resolve, reject) => {
          this.calls.push({ method, params, coalesce: options?.coalesce, resolve, reject });
        }),
    };
  }

  setStatus(s: EngineStatus): void {
    this.status = s;
    for (const h of this.statusHandlers) {
      h(s);
    }
  }

  emit(event: string, params: unknown): void {
    for (const h of this.eventHandlers.get(event) ?? []) {
      h(params);
    }
  }

  /** Answers the first waiting call of a method. */
  answer(method: string, value: unknown): void {
    const i = this.calls.findIndex((c) => c.method === method);
    if (i < 0) {
      throw new Error(`no ${method} waiting`);
    }
    const [c] = this.calls.splice(i, 1);
    c?.resolve(value);
  }
}

const tick = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

const snapshot = (revision: number, shapes: string[]) => ({
  revision,
  dirty: false,
  grid: 'bricks',
  path: null,
  notes: '',
  canUndo: false,
  canRedo: false,
  solving: false,
  shapes: shapes.map((id, i) => ({
    id,
    label: `S${i + 1}`,
    name: '',
    size: [1, 1, 1],
    fixed: 1,
    variable: 0,
    weight: 1,
    colour: null,
  })),
  colours: [],
  problems: [],
});

async function running(f: FakeEngine, restarts = 0, shapes = ['s1', 's2']): Promise<void> {
  f.setStatus({ state: 'running', version: '1', restarts });
  f.answer('document.get', snapshot(restarts * 10 + 1, shapes));
  f.answer('engine.info', { grids: [] });
  f.answer('settings.get', { undoDepth: 25, threads: 0, solver: 'classic' });
  await tick();
}

describe('the store', () => {
  it('calls nothing before the engine runs, then loads everything', async () => {
    const f = new FakeEngine();
    const store = new Store(f.link);
    const kinds: string[] = [];
    store.subscribe((c) => kinds.push(c.kind));
    expect(f.calls).toEqual([]);
    f.setStatus({ state: 'exited', code: null, signal: 'SIGKILL', stderr: '' });
    expect(f.calls).toEqual([]);
    await running(f);
    expect(store.snapshot?.revision).toBe(1);
    expect(store.selected).toBe('s1');
    expect(store.settings?.undoDepth).toBe(25);
    expect(kinds).toEqual(['status', 'status', 'document', 'settings']);
    expect(store.generation).toBe(0);
  });

  it('loads again when the engine was restarted: a new document, the selection gone', async () => {
    const f = new FakeEngine();
    const store = new Store(f.link);
    await running(f);
    store.select('s2');
    let restarted = false;
    store.subscribe((c) => {
      if (c.kind === 'document') {
        restarted = c.restarted;
      }
    });
    f.setStatus({ state: 'exited', code: null, signal: 'SIGKILL', stderr: '' });
    await running(f, 1, ['s1']);
    expect(restarted).toBe(true);
    expect(store.generation).toBe(1);
    expect(store.selected).toBe('s1');
    // the same restart count told again (a status asked for) loads nothing
    f.setStatus({ state: 'running', version: '1', restarts: 1 });
    expect(f.calls).toEqual([]);
  });

  it('reloads the snapshot on document.changed; the newest load counts', async () => {
    const f = new FakeEngine();
    const store = new Store(f.link);
    await running(f);
    f.emit('document.changed', { revision: 2, what: ['shapes'], shapes: ['s1'], problems: [] });
    f.emit('document.changed', { revision: 3, what: ['shapes'], shapes: [], problems: [] });
    expect(f.calls.map((c) => c.method)).toEqual(['document.get', 'document.get']);
    const [older, newer] = f.calls.splice(0);
    newer?.resolve(snapshot(3, ['s1']));
    older?.resolve(snapshot(2, ['s1', 's2']));
    await tick();
    expect(store.snapshot?.revision).toBe(3);
    expect(store.snapshot?.shapes).toHaveLength(1);
  });

  it('opens bytes: a new document, its name shown', async () => {
    const f = new FakeEngine();
    const store = new Store(f.link);
    await running(f);
    store.select('s2');
    const opened = store.openBytes('x.xmpuzzle', new Uint8Array([104, 105]));
    expect(f.calls[0]?.params).toEqual({ data: 'aGk=' });
    f.answer('document.openBytes', snapshot(5, ['s1', 's2']));
    await opened;
    expect(store.displayName).toBe('x.xmpuzzle');
    expect(store.generation).toBe(1);
    expect(store.selected).toBe('s1');
  });

  it('reports failed calls, except while the engine restarts', async () => {
    const f = new FakeEngine();
    const store = new Store(f.link);
    const errors: unknown[] = [];
    store.onError((e) => errors.push(e));
    await running(f);
    f.emit('document.changed', { revision: 2, what: ['shapes'], shapes: [], problems: [] });
    f.calls.splice(0)[0]?.reject(new EngineError(0, 'disconnected', 'gone', 'document.get'));
    await tick();
    expect(errors).toEqual([]);
    f.emit('document.changed', { revision: 2, what: ['shapes'], shapes: [], problems: [] });
    f.calls.splice(0)[0]?.reject(new EngineError(-32603, 'internal', 'broken', 'document.get'));
    await tick();
    expect(errors).toHaveLength(1);
  });

  it('writes base64 of any length', () => {
    expect(toBase64(new Uint8Array([]))).toBe('');
    const big = new Uint8Array(100_000).map((_, i) => i % 251);
    expect(atob(toBase64(big)).length).toBe(100_000);
    expect(atob(toBase64(big)).charCodeAt(99_999)).toBe(99_999 % 251);
  });
});

describe('the mesh cache', () => {
  it('coalesces by shape, style and transformation, and keeps what was asked at this revision', () => {
    const f = new FakeEngine();
    const cache = new MeshCache(f.link);
    const p1 = cache.get('s1', 'drawing', 4);
    expect(cache.get('s1', 'drawing', 4)).toBe(p1);
    expect(cache.get('s1', 'drawing', 3)).toBe(p1);
    expect(f.calls).toHaveLength(1);
    expect(f.calls[0]?.coalesce).toBe('mesh:s1:drawing:0');
    expect(f.calls[0]?.params).toEqual({ shape: 's1', style: 'drawing' });
    // a later revision asks again
    expect(cache.get('s1', 'drawing', 5)).not.toBe(p1);
    void cache.get('s1', 'flat', 5, 7);
    expect(f.calls[2]?.coalesce).toBe('mesh:s1:flat:7');
    expect(f.calls[2]?.params).toEqual({ shape: 's1', style: 'flat', transformation: 7 });
  });

  it('drops what document.changed names: its shapes, or every shape', () => {
    const f = new FakeEngine();
    const cache = new MeshCache(f.link);
    const a = cache.get('s1', 'drawing', 1);
    const b = cache.get('s2', 'drawing', 1);
    f.emit('document.changed', { revision: 2, what: ['shapes'], shapes: ['s1'], problems: [] });
    expect(cache.get('s1', 'drawing', 1)).not.toBe(a);
    expect(cache.get('s2', 'drawing', 1)).toBe(b);
    f.emit('document.changed', { revision: 3, what: ['problems'], shapes: [], problems: ['p1'] });
    expect(cache.get('s2', 'drawing', 1)).toBe(b);
    f.emit('document.changed', { revision: 4, what: ['colours'], shapes: [], problems: [] });
    expect(cache.get('s2', 'drawing', 1)).not.toBe(b);
  });

  it('forgets a failed request', async () => {
    const f = new FakeEngine();
    const cache = new MeshCache(f.link);
    const p = cache.get('s1', 'drawing', 1);
    f.calls[0]?.reject(new EngineError(0, 'superseded', 'newer', 'shape.mesh'));
    await expect(p).rejects.toThrow();
    expect(cache.get('s1', 'drawing', 1)).not.toBe(p);
  });
});
