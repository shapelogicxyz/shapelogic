// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo page's entry (site/public/demo/, built by demo.vite.config.ts
// through tools/build-demo.ts): the app's renderer as it is, over the
// engine compiled to WebAssembly (engine/wasm_main.cpp, wasm-transport.ts,
// engine-bridge.ts) and the demo's app (demo-app.ts) in place of the
// preload's window.bt and window.btApp. The demo puzzle is opened from the
// site as a dropped file would be, with the Solver set to check
// disassembly; the Solve buttons do nothing (a search would hold the
// page's one thread), and the bridge refuses a search that gets past them.
// Nothing is kept: a reload starts again.

import { EngineClient } from '../engine/client.ts';
import { startApp } from '../renderer/shell/app.ts';
import { flash } from '../renderer/shell/status.ts';
import { demoApp } from './demo-app.ts';
import { EngineBridge } from './engine-bridge.ts';
import createEngine from './engine/shapelogic_engine.js';
import { WasmTransport } from './wasm-transport.ts';

/** The puzzle the demo opens, on the site. */
const PUZZLE_URL = '/examples/PelikanBurr.xmpuzzle';
const PUZZLE_NAME = 'PelikanBurr.xmpuzzle';

const SOLVE_REFUSED =
  'This is a demo: searches run in the app. Download ShapeLogic to solve puzzles.';

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

// the top bar says what this is, beside the file's name, with a way back
// to the solved puzzle (an edited piece drops its solutions, as in the app)
function badge(): void {
  const right = document.querySelector('.top .right');
  if (right === null) {
    return;
  }
  const chip =
    'text-decoration:none;color:var(--accent);background:var(--accentSoft);font-weight:600;padding:2px 10px;margin-right:8px;font-size:11px;cursor:pointer';
  const a = document.createElement('a');
  a.className = 'chip';
  a.id = 'demoBadge';
  a.href = '/docs/install/';
  a.target = '_top';
  a.textContent = 'Demo';
  a.title =
    'The app in your browser: edit shapes, build problems, play the solution. Searches are off and nothing is kept. Click to download ShapeLogic.';
  a.style.cssText = chip;
  const reset = document.createElement('button');
  reset.className = 'chip';
  reset.id = 'demoReset';
  reset.textContent = 'Reset';
  reset.title = 'Start the demo again with the solved puzzle (your changes are dropped)';
  reset.style.cssText = `${chip};border:0;font:inherit;font-size:11px;font-weight:600`;
  reset.addEventListener('click', () => {
    window.location.reload();
  });
  right.prepend(a, reset);
}

// Solve, Prepare and Continue do nothing: taken before the Solver's own
// handler (which would close the player and ask the engine)
function stopSearches(): void {
  document.addEventListener(
    'click',
    (e) => {
      const b = (e.target as Element | null)?.closest<HTMLButtonElement>(
        '#leftS button[data-r="start"], #leftS button[data-r="prepare"], #leftS button[data-r="continue"]',
      );
      if (b !== null && b !== undefined) {
        e.stopPropagation();
        e.preventDefault();
        flash(SOLVE_REFUSED, 5000);
      }
    },
    true,
  );
}

async function main(): Promise<void> {
  const m = await createEngine();
  const client = new EngineClient(new WasmTransport(m));
  const hello = await client.hello();
  const app = demoApp(hello.version);
  Object.defineProperty(window, 'bt', { value: new EngineBridge(client, hello.version) });
  Object.defineProperty(window, 'btApp', { value: app });
  badge();
  stopSearches();
  const { store, solver } = startApp();
  await stageReady();
  const r = await fetch(PUZZLE_URL);
  if (!r.ok) {
    throw new Error(`the demo puzzle: ${String(r.status)} ${r.statusText}`);
  }
  await store.openBytes(PUZZLE_NAME, new Uint8Array(await r.arrayBuffer()));
  // the Solver shows Assemble and Disassemble (the file does not say)
  const problem = store.snapshot?.problems[0]?.id;
  if (problem !== undefined) {
    const settings = (await store.engine.call('problem.get', { problem })).solverSettings;
    const set = await store.engine.call('problem.setSolverSettings', {
      problem,
      settings: { ...settings, disassemble: true },
    });
    await store.whenRevision(set.revision);
    solver.selectProblem(problem);
  }
}

void main();
