// SPDX-License-Identifier: GPL-3.0-or-later
//
// The options the solves were started with, kept across a reload of the
// page (View ▸ Reload in development, and the dev server's reload when a
// source file is saved): the engine and its runs go on through a reload,
// but it takes a run's options with each solve.continue and keeps none, so
// the page that carries a run on (the crash recovery journal after its
// save, Continue) must still know them. Carried on with other options, a
// sliding search is another search (its saved search is named by them: the
// nested slides, the optimized paths' grid): it finds nothing saved and
// begins again at the first move. The optimized paths chosen at Solve are
// no stored option of the problem, so the problem's settings do not say.
//
// Kept in the page's session storage: it lasts as long as the window does,
// a reload included, and each window has its own (as it has its own
// engine). Where there is none, nothing is kept and the Solver falls back
// on the problem's options as shown (SolverWorkspace.launchOptions).
// DOM-free but for `sessionStore`, for the unit tests.

import type { SolveOptions } from '../../engine/api.generated.ts';

/** What is used of a Storage. */
export type KeyValue = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

const KEY = 'solver.runOptions';

/** The page's session storage; null where there is none or it is refused. */
export function sessionStore(): KeyValue | null {
  try {
    return (globalThis as { sessionStorage?: KeyValue }).sessionStorage ?? null;
  } catch {
    return null;
  }
}

/** The options kept, by problem; none when nothing (well formed) is kept. */
export function readRunOptions(store: KeyValue | null): Map<string, SolveOptions> {
  const out = new Map<string, SolveOptions>();
  try {
    const kept: unknown = JSON.parse(store?.getItem(KEY) ?? 'null');
    if (typeof kept === 'object' && kept !== null && !Array.isArray(kept)) {
      for (const [problem, options] of Object.entries(kept)) {
        if (typeof options === 'object' && options !== null && !Array.isArray(options)) {
          out.set(problem, options as SolveOptions);
        }
      }
    }
  } catch {
    // not read, or not JSON: nothing kept
  }
  return out;
}

/** Keeps these options, by problem, in the place of what was kept. */
export function writeRunOptions(
  store: KeyValue | null,
  runs: ReadonlyMap<string, SolveOptions>,
): void {
  try {
    if (runs.size === 0) {
      store?.removeItem(KEY);
    } else {
      store?.setItem(KEY, JSON.stringify(Object.fromEntries(runs)));
    }
  } catch {
    // refused or full: the options as shown stand in after a reload
  }
}
