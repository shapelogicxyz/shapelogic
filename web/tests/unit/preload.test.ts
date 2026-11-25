// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preload script with Electron stood in for (stage 8): it exposes
// window.bt and window.btApp only; commands the main process sends before
// the page listens (a file given on the command line, sent when the page
// has loaded, while it still reads its stored settings) are kept and handed
// to the first handler in order, a task later, with later ones behind
// them; and a dropped file's path is reported to the main process (the
// files its engine may be given), a file without one is not.

import { describe, expect, it, vi } from 'vitest';

import { APP_CHANNELS } from '../../src/engine/app-bridge.ts';
import type { AppChannel } from '../../src/engine/app-bridge.ts';

type Listener = (event: unknown, ...args: unknown[]) => void;

const electron = vi.hoisted(() => ({
  listeners: new Map<string, Listener[]>(),
  exposed: new Map<string, unknown>(),
  sent: [] as unknown[][],
  invoked: [] as string[],
  synced: [] as string[],
  paths: new Map<object, string>(),
}));

vi.mock('electron', () => ({
  contextBridge: {
    exposeInMainWorld: (name: string, api: unknown) => electron.exposed.set(name, api),
  },
  ipcRenderer: {
    on: (channel: string, l: Listener) => {
      electron.listeners.set(channel, [...(electron.listeners.get(channel) ?? []), l]);
    },
    removeListener: () => undefined,
    send: (...args: unknown[]) => electron.sent.push(args),
    sendSync: (channel: string) => {
      electron.synced.push(channel);
      return { platform: 'test', version: '9.9.9' };
    },
    invoke: (channel: string) => {
      electron.invoked.push(channel);
      return Promise.resolve(null);
    },
  },
  webUtils: { getPathForFile: (f: object) => electron.paths.get(f) ?? '' },
}));

/** The main process sends a command to the page. */
function command(c: unknown): void {
  for (const l of electron.listeners.get('bt:command') ?? []) {
    l({}, c);
  }
}

const tick = (): Promise<void> => new Promise((done) => setTimeout(done, 5));

describe('the preload', () => {
  it('exposes window.bt and window.btApp, and keeps the first commands for the first handler', async () => {
    await import('../../src/preload/index.ts');
    expect([...electron.exposed.keys()]).toEqual(['bt', 'btApp']);
    const btApp = electron.exposed.get('btApp') as {
      version: string;
      onCommand(h: (c: unknown) => void): () => void;
      pathOf(f: object): string;
    };
    expect(btApp.version).toBe('9.9.9');

    // sent at did-finish-load, before the page listens
    command({ cmd: 'openPath', path: '/a.xmpuzzle' });
    command({ cmd: 'nonsense' });
    command({ cmd: 'save' });
    const got: unknown[] = [];
    btApp.onCommand((c) => got.push(c));
    // a task later, not inside the subscribing code; one meanwhile goes behind
    expect(got).toEqual([]);
    command({ cmd: 'undo' });
    await tick();
    expect(got).toEqual([
      { cmd: 'openPath', path: '/a.xmpuzzle' },
      { cmd: 'save' },
      { cmd: 'undo' },
    ]);
    // then straight through
    command({ cmd: 'redo' });
    expect(got.at(-1)).toEqual({ cmd: 'redo' });

    // a dropped file's path goes to the main process; a file without one does not
    const dropped = {};
    electron.paths.set(dropped, '/d/x.xmpuzzle');
    expect(btApp.pathOf(dropped)).toBe('/d/x.xmpuzzle');
    expect(btApp.pathOf({})).toBe('');
    expect(electron.sent.filter((s) => s[0] === 'bt:dropped')).toEqual([
      ['bt:dropped', '/d/x.xmpuzzle'],
    ]);
  });

  it('uses every channel of the table by its kind, and no other (cleanup item 12.2)', async () => {
    await import('../../src/preload/index.ts');
    const bt = electron.exposed.get('bt') as Record<string, (...a: unknown[]) => unknown>;
    const btApp = electron.exposed.get('btApp') as Record<string, unknown>;
    // every method once, with arguments of no meaning: main checks them (a
    // "file" with a path, so pathOf sends it)
    const arg = (): void => undefined;
    electron.paths.set(arg, '/d/y.xmpuzzle');
    const calls: unknown[] = [];
    for (const api of [bt, btApp]) {
      for (const v of Object.values(api)) {
        if (typeof v === 'function') {
          // a method that throws at once has used its channel already
          calls.push(Promise.resolve().then(() => (v as (...a: unknown[]) => unknown)(arg, true)));
        }
      }
    }
    await Promise.allSettled(calls);
    const want = (kind: string): string[] =>
      (Object.keys(APP_CHANNELS) as AppChannel[]).filter((c) => APP_CHANNELS[c] === kind).sort();
    const uniq = (xs: string[]): string[] => [...new Set(xs)].sort();
    expect(uniq(electron.sent.map((s) => String(s[0])))).toEqual(want('send'));
    expect(uniq(electron.invoked)).toEqual(want('invoke'));
    expect(uniq(electron.synced)).toEqual(want('sync'));
    expect(uniq([...electron.listeners.keys()])).toEqual(want('event'));
  });
});
