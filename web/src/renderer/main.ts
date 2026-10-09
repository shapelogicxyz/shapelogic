// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's entry. Stage 1b ports the mockup's shell here; until then
// it only connects to the engine and says how it stands.

import { EngineProxy } from './engine-proxy.ts';

const engine = new EngineProxy(window.bt);
const line = document.getElementById('engine-status');
engine.onStatus((status) => {
  if (line !== null) {
    line.textContent =
      status.state === 'running' ? `bt-engine ${status.version}` : `bt-engine: ${status.state}`;
  }
});
