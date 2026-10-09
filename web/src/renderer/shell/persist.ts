// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings and the layouts kept across sessions (stage 6): read from
// the main process's store before the shell starts (main.ts, then
// startApp: prefs.ts's `validPrefs`, layout.ts's `restoreLayout`), so the
// first picture already has them, and stored again whenever they change
// (here). The engine's settings among them (undo depth, threads) go to
// every engine the window starts (Store.wantedSettings).

import type { BtApp } from '../../engine/app-bridge.ts';
import { layoutState, onLayout } from './layout.ts';
import { onPrefs, prefs } from './prefs.ts';

/** Stores the settings and the layouts whenever they change. */
export function keepState(app: BtApp): void {
  onPrefs(() => {
    app.saveState('prefs', { ...prefs });
  });
  onLayout(() => {
    app.saveState('layout', layoutState());
  });
}
