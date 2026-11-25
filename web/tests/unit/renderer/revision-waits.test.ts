// SPDX-License-Identifier: GPL-3.0-or-later
//
// The waits for a revision (cleanup item 6.1): released at or below the
// revision a view shows, and all at once by another document; an edit whose
// reload fails because the engine restarted still settles, so the edits
// under way go back to none (the rescue copy waits for that). And no
// coalesce key is used in two files: a call that reused another view's key
// would supersede its load, and the edits waiting for it would hang.

import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';

import { describe, expect, it } from 'vitest';

import type { EngineStatus } from '../../../src/engine/client-types.ts';
import { CLIENT_ERROR_CODE, EngineError } from '../../../src/engine/errors.ts';
import { RevisionWaits, whenShown } from '../../../src/renderer/state/revision-waits.ts';
import { Store } from '../../../src/renderer/state/store.ts';
import type { EngineLink } from '../../../src/renderer/state/store.ts';

interface Call {
  method: string;
  params: unknown;
  coalesce: string | undefined;
  resolve(v: unknown): void;
  reject(e: unknown): void;
}

/** The engine proxy, scripted: each call waits until the test answers it. */
class ScriptedEngine {
  status: EngineStatus | null = null;
  calls: Call[] = [];
  private statusHandlers: ((s: EngineStatus) => void)[] = [];
  private changed: ((p: unknown) => void)[] = [];
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
      on: (_e, h) => {
        this.changed.push(h as (p: unknown) => void);
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

  /** The engine's document.changed (the only event the store listens to). */
  documentChanged(revision: number): void {
    for (const h of this.changed) {
      h({ revision, what: ['shapes'], shapes: ['s1'], problems: [] });
    }
  }

  take(method: string): Call {
    const i = this.calls.findIndex((c) => c.method === method);
    const c = i < 0 ? undefined : this.calls.splice(i, 1)[0];
    if (c === undefined) {
      throw new Error(`no ${method} waiting`);
    }
    return c;
  }

  answer(method: string, value: unknown): void {
    this.take(method).resolve(value);
  }
}

const tick = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

const snapshot = (revision: number) => ({
  revision,
  dirty: false,
  grid: 'bricks',
  path: null,
  notes: '',
  canUndo: false,
  canRedo: false,
  solving: false,
  shapes: [],
  colors: [],
  problems: [],
});

async function running(f: ScriptedEngine, restarts: number, revision: number): Promise<void> {
  f.setStatus({ state: 'running', version: '1', restarts });
  f.answer('document.get', snapshot(revision));
  f.answer('engine.info', { grids: [] });
  f.answer('settings.get', { undoDepth: 25, threads: 0, solver: 'classic' });
  await tick();
}

describe('the revision waits', () => {
  it('release the waits at or below the revision, and every wait of another document', async () => {
    const w = new RevisionWaits();
    const done: string[] = [];
    void w.wait(3, 0).then(() => done.push('3'));
    void w.wait(5, 0).then(() => done.push('5'));
    void w.wait(9, 0).then(() => done.push('9'));
    w.release(4, 0);
    await tick();
    expect(done).toEqual(['3']);
    expect(w.size).toBe(2);
    // another document: its revision 0 releases every wait of the old one
    void w.wait(1, 1).then(() => done.push('1 of the new'));
    w.release(0, 1);
    await tick();
    expect(done).toEqual(['3', '5', '9']);
    expect(w.size).toBe(1);
    w.release(1, 1);
    await tick();
    expect(done).toEqual(['3', '5', '9', '1 of the new']);
    expect(w.size).toBe(0);
  });
});

describe('an edit whose reload fails because the engine restarted', () => {
  it('settles, and the edits under way go back to none', async () => {
    const f = new ScriptedEngine();
    const store = new Store(f.link);
    await running(f, 0, 1);
    // the edits under way, counted as state/edits.ts's mutate counts them
    let busy = 0;
    // a workspace as Puzzle's and the Solver's are: it reads the problem
    // again for each snapshot, and releases the waits with the load's result
    const waits = new RevisionWaits();
    let shown = 1;
    const loads: { revision: number; generation: number }[] = [];
    store.subscribe((c) => {
      if (c.kind !== 'document' || store.snapshot === null) {
        return;
      }
      const revision = store.snapshot.revision;
      const generation = store.generation;
      if (c.restarted) {
        waits.release(-1, generation);
        shown = -1;
      }
      const call = f.link.call('problem.get', { problem: 'p1' });
      call.then(
        () => {
          shown = revision;
          waits.release(revision, generation);
        },
        () => {
          waits.release(revision, generation);
        },
      );
      loads.push({ revision, generation });
    });

    const edit = (async () => {
      busy++;
      try {
        const r = await f.link.call('shape.rename', { shape: 's1', name: 'x' });
        await whenShown(store, waits, () => shown, r.revision);
        return r;
      } finally {
        busy--;
      }
    })();
    expect(busy).toBe(1);
    f.answer('shape.rename', { revision: 2 });
    await tick();
    // its document.changed reloads the snapshot, and the workspace reads the problem again
    f.documentChanged(2);
    f.answer('document.get', snapshot(2));
    await tick();
    expect(loads.map((l) => l.revision)).toEqual([2]);
    expect(waits.size).toBe(1);
    // the engine dies before the reload's reply; the restarted engine counts from 0
    f.setStatus({ state: 'exited', code: null, signal: 'SIGKILL', stderr: '' });
    f.take('problem.get').reject(
      new EngineError(CLIENT_ERROR_CODE, 'disconnected', 'the engine stopped'),
    );
    await tick();
    expect(await edit).toEqual({ revision: 2 });
    expect(busy).toBe(0);
    expect(waits.size).toBe(0);
    // the new engine's document is the next generation, its revisions from 0
    await running(f, 1, 0);
    expect(store.generation).toBe(1);
    expect(loads.map((l) => [l.revision, l.generation])).toEqual([
      [2, 0],
      [0, 1],
    ]);
  });
});

describe('the coalesce keys', () => {
  it('are each used in one file only', () => {
    const src = join(import.meta.dirname, '../../../src');
    const files: string[] = [];
    const walk = (dir: string): void => {
      for (const name of readdirSync(dir)) {
        const p = join(dir, name);
        if (statSync(p).isDirectory()) {
          walk(p);
        } else if (p.endsWith('.ts') && !p.endsWith('.generated.ts')) {
          files.push(p);
        }
      }
    };
    walk(src);
    // `coalesce: '…'` or `coalesce: \`…\``: a template's key is its text up to the first ${
    const literal = /coalesce:\s*(?:'([^']*)'|`([^`$]*)(\$\{)?[^`]*`)/g;
    const owner = new Map<string, string>();
    const clashes: string[] = [];
    let found = 0;
    for (const file of files) {
      const text = readFileSync(file, 'utf8');
      const keys = new Set<string>();
      for (const m of text.matchAll(literal)) {
        const key = m[1] ?? `${m[2] ?? ''}${m[3] === undefined ? '' : '*'}`;
        keys.add(key);
      }
      for (const key of keys) {
        found++;
        const at = relative(src, file);
        const other = owner.get(key);
        if (other !== undefined && other !== at) {
          clashes.push(`${key}: ${other} and ${at}`);
        }
        owner.set(key, at);
      }
    }
    // a template's prefix must not start another file's key either
    for (const [key, at] of owner) {
      if (!key.endsWith('*')) {
        continue;
      }
      const prefix = key.slice(0, -1);
      for (const [other, there] of owner) {
        if (there !== at && other.startsWith(prefix)) {
          clashes.push(`${other} (${there}) starts with ${prefix} (${at})`);
        }
      }
    }
    // the option is the client's (engine/client.ts), so the scan found its uses
    expect(readFileSync(join(src, 'engine/client.ts'), 'utf8')).toContain('coalesce');
    expect(found).toBeGreaterThan(10);
    expect(clashes).toEqual([]);
  });
});
