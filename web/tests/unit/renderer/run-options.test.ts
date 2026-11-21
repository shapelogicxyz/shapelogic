// SPDX-License-Identifier: GPL-3.0-or-later
//
// The solves' options kept across a reload of the page
// (renderer/solver/run-options.ts): what is written is read back, and
// anything else kept there is nothing kept.

import { describe, expect, it } from 'vitest';

import type { SolveOptions } from '../../../src/engine/api.generated.ts';
import { readRunOptions, writeRunOptions } from '../../../src/renderer/solver/run-options.ts';
import type { KeyValue } from '../../../src/renderer/solver/run-options.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_SLIDING,
  solveOptions,
  withOptimizedPaths,
} from '../../../src/renderer/solver/settings.ts';

function memory(): KeyValue & { items: Map<string, string> } {
  const items = new Map<string, string>();
  return {
    items,
    getItem: (k) => items.get(k) ?? null,
    setItem: (k, v) => {
      items.set(k, v);
    },
    removeItem: (k) => {
      items.delete(k);
    },
  };
}

describe('the solves’ options kept across a reload', () => {
  it('are read back as they were written, the optimized paths chosen at Solve included', () => {
    const store = memory();
    // a sliding run started with optimized paths (the question at Solve) and a limit of its own
    const sliding = withOptimizedPaths(
      solveOptions(DEFAULT_SLIDING, { ...DEFAULT_LIMITS, keepAtMost: 7 }, 'sliding'),
      true,
    );
    const brick = solveOptions({ ...DEFAULT_SLIDING, justCount: true }, DEFAULT_LIMITS);
    writeRunOptions(
      store,
      new Map<string, SolveOptions>([
        ['p1', sliding],
        ['p2', brick],
      ]),
    );
    const kept = readRunOptions(store);
    expect([...kept.keys()]).toEqual(['p1', 'p2']);
    expect(kept.get('p1')).toEqual(sliding);
    expect(kept.get('p1')?.sliding?.optimizedPaths).toBe(true);
    expect(kept.get('p2')).toEqual(brick);
    // written again: in the place of what was kept
    writeRunOptions(store, new Map([['p2', brick]]));
    expect([...readRunOptions(store).keys()]).toEqual(['p2']);
    // none: nothing is left there
    writeRunOptions(store, new Map());
    expect(store.items.size).toBe(0);
    expect(readRunOptions(store).size).toBe(0);
  });

  it('are none where nothing well formed is kept, or there is no storage', () => {
    expect(readRunOptions(null).size).toBe(0);
    writeRunOptions(null, new Map([['p1', {}]]));
    const store = memory();
    expect(readRunOptions(store).size).toBe(0);
    for (const bad of ['', 'not json', '[1,2]', '"text"', '7', 'null']) {
      store.items.set('solver.runOptions', bad);
      expect(readRunOptions(store).size).toBe(0);
    }
    // an entry that is no options object is left out, the others read
    store.items.set('solver.runOptions', '{"p1":5,"p2":null,"p3":[1],"p4":{"solver":"classic"}}');
    expect([...readRunOptions(store).entries()]).toEqual([['p4', { solver: 'classic' }]]);
    // a storage that refuses: nothing kept, nothing thrown
    const refusing: KeyValue = {
      getItem: () => {
        throw new Error('refused');
      },
      setItem: () => {
        throw new Error('full');
      },
      removeItem: () => {
        throw new Error('refused');
      },
    };
    expect(readRunOptions(refusing).size).toBe(0);
    writeRunOptions(refusing, new Map([['p1', {}]]));
    writeRunOptions(refusing, new Map());
  });
});
