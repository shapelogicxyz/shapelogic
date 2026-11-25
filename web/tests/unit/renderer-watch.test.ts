// SPDX-License-Identifier: GPL-3.0-or-later
//
// A window's page that crashed or hangs (src/main/renderer-watch.ts,
// cleanup item 7.1): reloaded at most three times a minute, then asked
// about; a clean exit left alone; a hung page asked about, the question
// closed when it answers again, and its reload not counted as a crash.

import { describe, expect, it } from 'vitest';

import { RELOADS_MAX, RELOADS_WINDOW_MS, RendererWatch } from '../../src/main/renderer-watch.ts';
import type { GoneDetails, WatchedWindow } from '../../src/main/renderer-watch.ts';

interface Seen {
  calls: string[];
  logs: string[];
  /** The hung question's signal, while one is open. */
  signal: AbortSignal | null;
  answerGone: (a: 'reload' | 'close') => void;
  answerHung: (a: 'wait' | 'reload') => void;
  clock: { t: number };
  closing: { v: boolean };
}

function watched(): { watch: RendererWatch; seen: Seen } {
  const seen: Seen = {
    calls: [],
    logs: [],
    signal: null,
    answerGone: () => undefined,
    answerHung: () => undefined,
    clock: { t: 1_000_000 },
    closing: { v: false },
  };
  let watch: RendererWatch | null = null;
  const w: WatchedWindow = {
    reload: () => seen.calls.push('reload'),
    kill: () => {
      seen.calls.push('kill');
      // Electron tells the end of the process it ended
      watch?.gone({ reason: 'killed', exitCode: 0 });
    },
    close: () => seen.calls.push('close'),
    closing: () => seen.closing.v,
    askGone: () => {
      seen.calls.push('askGone');
      return new Promise((done) => {
        seen.answerGone = done;
      });
    },
    askHung: (signal) => {
      seen.calls.push('askHung');
      seen.signal = signal;
      return new Promise((done) => {
        seen.answerHung = done;
        signal.addEventListener('abort', () => {
          done('wait');
        });
      });
    },
    log: (level, event, text) => seen.logs.push(`${level} ${event}: ${text}`),
    now: () => seen.clock.t,
  };
  watch = new RendererWatch(w);
  return { watch, seen };
}

const crashed: GoneDetails = { reason: 'crashed', exitCode: 11 };
const tick = (): Promise<void> => new Promise((done) => setImmediate(done));

describe('a page that crashed', () => {
  it('is logged and reloaded, at most three times a minute, then the window asks', async () => {
    const { watch, seen } = watched();
    for (let i = 0; i < RELOADS_MAX; i++) {
      watch.gone(crashed);
      seen.clock.t += 1000;
    }
    expect(seen.calls).toEqual(['reload', 'reload', 'reload']);
    expect(seen.logs[0]).toBe(
      "ERROR Renderer Gone: the page's process ended: crashed, exit code 11",
    );
    // the fourth in the minute: asked, Reload or Close
    watch.gone(crashed);
    // and a fifth while the question is open asks no second one
    watch.gone(crashed);
    expect(seen.calls).toEqual(['reload', 'reload', 'reload', 'askGone']);
    seen.answerGone('reload');
    await tick();
    expect(seen.calls.at(-1)).toBe('reload');
    watch.gone(crashed);
    expect(seen.calls.at(-1)).toBe('askGone');
    seen.answerGone('close');
    await tick();
    expect(seen.calls.at(-1)).toBe('close');
    // a minute after the first ones, reloads by itself again
    seen.clock.t += RELOADS_WINDOW_MS;
    watch.gone(crashed);
    expect(seen.calls.at(-1)).toBe('reload');
  });

  it('is left alone after a clean exit, and only logged while the window closes', () => {
    const { watch, seen } = watched();
    watch.gone({ reason: 'clean-exit', exitCode: 0 });
    expect(seen.calls).toEqual([]);
    expect(seen.logs).toEqual([]);
    seen.closing.v = true;
    watch.gone({ reason: 'oom', exitCode: 0 });
    expect(seen.calls).toEqual([]);
    expect(seen.logs).toHaveLength(1);
  });
});

describe('a page that hangs', () => {
  it('is asked about once, and the question closes when it answers again', async () => {
    const { watch, seen } = watched();
    watch.unresponsive();
    watch.unresponsive();
    expect(seen.calls).toEqual(['askHung']);
    expect(seen.logs[0]).toMatch(/^WARN Renderer Unresponsive: /);
    watch.responsive();
    expect(seen.signal?.aborted).toBe(true);
    await tick();
    expect(seen.calls).toEqual(['askHung']);
    // hangs again: a new question
    watch.unresponsive();
    expect(seen.calls).toEqual(['askHung', 'askHung']);
  });

  it('is ended and reloaded on Reload, which counts as no crash', async () => {
    const { watch, seen } = watched();
    watch.unresponsive();
    seen.answerHung('reload');
    await tick();
    expect(seen.calls).toEqual(['askHung', 'kill', 'reload']);
    // its end was logged as the reload's, not as a crash
    expect(seen.logs.some((l) => l.startsWith('ERROR'))).toBe(false);
    // the three reloads of crashes are all still there
    for (let i = 0; i < RELOADS_MAX; i++) {
      watch.gone(crashed);
    }
    expect(seen.calls.filter((c) => c === 'reload')).toHaveLength(1 + RELOADS_MAX);
  });

  it('asks no more once the page crashed meanwhile', async () => {
    const { watch, seen } = watched();
    watch.unresponsive();
    watch.gone(crashed);
    expect(seen.signal?.aborted).toBe(true);
    await tick();
    expect(seen.calls).toEqual(['askHung', 'reload']);
  });
});
