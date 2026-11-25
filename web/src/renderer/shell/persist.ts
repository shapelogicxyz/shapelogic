// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings and the layouts kept across sessions (stage 6): read from
// the main process's store before the shell starts (main.ts, then
// startApp: prefs.ts's `validPrefs`, layout.ts's `restoreLayout`), so the
// first picture already has them, and stored again whenever they change
// (here). The engine's settings among them (undo depth, threads, the
// search memory, the searches' disk space) go to
// every engine the window starts (Store.wantedSettings). The settings are
// the app's: a change in another window is taken here too (decision 0036),
// and not stored again; the layouts are each window's own.

import type { BtApp } from '../../engine/app-bridge.ts';
import { layoutState, onLayout } from './layout.ts';
import { onPrefs, prefs, setPrefs, validPrefs } from './prefs.ts';

/**
 * Stores the settings and the layouts whenever they change; `taken` runs
 * after settings another window changed were taken.
 */
export function keepState(app: BtApp, taken: () => void = () => undefined): void {
  let taking = false;
  onPrefs(() => {
    if (!taking) {
      app.saveState('prefs', { ...prefs });
    }
  });
  app.onPrefs((saved) => {
    taking = true;
    try {
      setPrefs(validPrefs(saved));
    } finally {
      taking = false;
    }
    taken();
  });
  onLayout(() => {
    app.saveState('layout', layoutState());
  });
}
