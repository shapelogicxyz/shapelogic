// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo page's entry (site/public/demo/, built by demo.vite.config.ts):
// the app's renderer as it is, over the recorded engine (replay-bridge.ts)
// and the demo's app (demo-app.ts) in place of the preload's window.bt
// and window.btApp, with the demo puzzle opened as File ▸ Open Recent
// would. While tools/record-demo.ts records, the bridge is its real
// engine instead (live-bridge.ts).

import type { BtBridge } from '../engine/bridge.ts';
import { startApp } from '../renderer/shell/app.ts';
import { flash } from '../renderer/shell/status.ts';
import { demoApp } from './demo-app.ts';
import { LiveBridge, liveRecording } from './live-bridge.ts';
import { ReplayBridge } from './replay-bridge.ts';
import type { DemoSession } from './session.ts';

declare const __BT_DEMO_BASE__: string;

/** What the status bar says when the demo refuses a change. */
const REFUSED = 'This is a demo: changes are off. Download ShapeLogic to edit and solve puzzles.';

interface DemoWindow {
  /** Set by the recorder: the puzzle to open and the engine's version. */
  __btDemo?: { puzzle: string; version: string };
  /** The replay, for a look from the console at what the recording misses. */
  __btDemoReplay?: ReplayBridge;
}

function stageReady(): Promise<void> {
  const stage = document.getElementById('stage');
  const ready = (): boolean =>
    stage?.dataset.engine === 'running' && (stage.dataset.revision ?? '') !== '';
  return new Promise((done) => {
    if (stage === null || ready()) {
      done();
      return;
    }
    const watch = new MutationObserver(() => {
      if (ready()) {
        watch.disconnect();
        done();
      }
    });
    watch.observe(stage, { attributes: true });
  });
}

// the top bar says what this is, beside the file's name
function badge(): void {
  const right = document.querySelector('.top .right');
  if (right === null) {
    return;
  }
  const a = document.createElement('a');
  a.className = 'chip';
  a.id = 'demoBadge';
  a.href = '/docs/install/';
  a.target = '_top';
  a.textContent = 'Demo';
  a.title =
    'A demo with a recorded engine: look around, play the solution. Changes are off and nothing is kept (reload to start again). Click to download ShapeLogic.';
  a.style.cssText =
    'text-decoration:none;color:var(--accent);background:var(--accentSoft);font-weight:600;padding:2px 10px;margin-right:8px;font-size:11px';
  right.prepend(a);
}

async function main(): Promise<void> {
  const w = window as DemoWindow;
  let bridge: BtBridge;
  let puzzle: string;
  let version: string;
  if (liveRecording() && w.__btDemo !== undefined) {
    bridge = new LiveBridge();
    ({ puzzle, version } = w.__btDemo);
  } else {
    const r = await fetch(`${__BT_DEMO_BASE__}session.json`);
    if (!r.ok) {
      throw new Error(`the demo's recording: ${String(r.status)} ${r.statusText}`);
    }
    const session = (await r.json()) as DemoSession;
    const replay = new ReplayBridge(session, () => {
      flash(REFUSED, 5000);
    });
    w.__btDemoReplay = replay;
    bridge = replay;
    puzzle = session.puzzle;
    version = session.status.state === 'running' ? session.status.version : '';
  }
  const app = demoApp(version);
  Object.defineProperty(window, 'bt', { value: bridge });
  Object.defineProperty(window, 'btApp', { value: app });
  badge();
  startApp();
  await stageReady();
  app.command({ cmd: 'openPath', path: puzzle });
}

void main();
