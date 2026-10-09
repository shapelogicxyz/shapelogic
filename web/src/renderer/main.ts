// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's entry: the shell (shell/app.ts) over the engine proxy,
// started with the settings and layouts the last session kept.

import { startApp } from './shell/app.ts';

window.btApp.loadState().then(startApp, () => {
  startApp();
});
