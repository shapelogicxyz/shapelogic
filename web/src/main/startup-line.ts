// SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Startup" line of each window (cleanup item 2.7): the milliseconds
// from the app's start to the window shown, its page loaded, its engine
// spawned, the engine's hello and the first document shown, so a slow or
// stuck start shows where it stopped. Free of Electron.

/** The moments of one window's start (Date.now() times; null: not reached). */
export interface StartupTimes {
  shown: number | null;
  loaded: number | null;
  spawned: number | null;
  hello: number | null;
  document: number | null;
}

/** The line's text, e.g. "window shown 420 ms, page loaded 610 ms, ...; first document 900 ms". */
export function startupText(appStart: number, t: StartupTimes): string {
  const at = (label: string, time: number | null): string =>
    `${label} ${time === null ? 'not yet' : `${String(time - appStart)} ms`}`;
  return [
    at('window shown', t.shown),
    at('page loaded', t.loaded),
    at('engine spawned', t.spawned),
    at('engine hello', t.hello),
    at('first document shown', t.document),
  ].join(', ');
}

/** How long a window's start may take before its Startup line is written with what it reached. */
export const STARTUP_LINE_WAIT_MS = 30_000;
