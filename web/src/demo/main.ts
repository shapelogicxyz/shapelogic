// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo page's entry (site/public/demo/, built by demo.vite.config.ts
// through tools/build-demo.ts): the app's renderer as it is, over the
// engine compiled to WebAssembly (engine/wasm_main.cpp, wasm-transport.ts,
// engine-bridge.ts) and the demo's app (demo-app.ts) in place of the
// preload's window.bt and window.btApp. The demo puzzle is opened from the
// site as a dropped file would be, with its problem set to check
// disassembly. Nothing is kept: a reload starts again.
//
// Solve plays a run without a search (a search would hold the page's one
// thread): the run's events go to the renderer for about three seconds,
// then the saved solution is back. No method puts solutions into a
// document, so the puzzle is opened again from its file with the pieces
// and the problem as the file has them, the rest of the document as the
// visitor left it (`restore`): a changed piece dropped the solutions, as
// in the app, and comes back as it was; new shapes, problems and colours
// stay. The demo problem is the file's first; another problem's Solve is
// refused with the other searches (engine-bridge.ts).

import type { SolveProgress } from '../engine/api.generated.ts';
import { EngineClient } from '../engine/client.ts';
import { fromBase64 } from '../engine/recorder.ts';
import { startApp } from '../renderer/shell/app.ts';
import type { Store } from '../renderer/state/store.ts';
import { demoApp } from './demo-app.ts';
import { EngineBridge } from './engine-bridge.ts';
import type { RunEvents } from './engine-bridge.ts';
import createEngine from './engine/shapelogic_engine.js';
import { WasmTransport } from './wasm-transport.ts';

/** The puzzle the demo opens, on the site. */
const PUZZLE_URL = '/examples/PelikanBurr.xmpuzzle';
const PUZZLE_NAME = 'PelikanBurr.xmpuzzle';
/** How long a played run takes, and how often it reports. */
const RUN_MS = 3000;
const TICK_MS = 100;

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

/** Resolves when `test` holds (looked at every frame), or after `ms`. */
function until(test: () => boolean, ms: number): Promise<void> {
  return new Promise((done) => {
    const end = performance.now() + ms;
    const look = (): void => {
      if (test() || performance.now() > end) {
        done();
      } else {
        requestAnimationFrame(look);
      }
    };
    look();
  });
}

const sleep = (ms: number): Promise<void> => new Promise((done) => setTimeout(done, ms));

// the top bar's way back to the start, and out to the site's front page
function badge(): void {
  const right = document.querySelector('.top .right');
  if (right === null) {
    return;
  }
  const chip =
    'text-decoration:none;color:var(--accent);background:var(--accentSoft);font-weight:600;padding:2px 10px;margin-right:8px;font-size:11px;cursor:pointer';
  const reset = document.createElement('button');
  reset.className = 'chip';
  reset.id = 'demoReset';
  reset.textContent = 'Reset';
  reset.title = 'Start the demo again (your changes are dropped)';
  reset.style.cssText = `${chip};border:0;font:inherit;font-size:11px;font-weight:600`;
  reset.addEventListener('click', () => {
    window.location.reload();
  });
  const exit = document.createElement('a');
  exit.className = 'chip';
  exit.id = 'demoExit';
  exit.href = '/';
  exit.target = '_top';
  exit.textContent = 'Exit';
  exit.title = 'Leave the demo for the ShapeLogic website (nothing is kept)';
  exit.style.cssText = chip;
  right.prepend(reset, exit);
  // the brand at the top left does the same as Exit
  const brand = document.querySelector<HTMLElement>('.top .brand');
  if (brand !== null) {
    brand.style.cursor = 'pointer';
    brand.title = exit.title;
    brand.addEventListener('click', () => {
      exit.click();
    });
  }
}

// ---- the puzzle's file -----------------------------------------------------------------

const GZIP = [0x1f, 0x8b];

async function gunzip(bytes: Uint8Array): Promise<string> {
  if (bytes[0] !== GZIP[0] || bytes[1] !== GZIP[1]) {
    return new TextDecoder().decode(bytes);
  }
  const stream = new Blob([bytes as BlobPart])
    .stream()
    .pipeThrough(new DecompressionStream('gzip'));
  return new Response(stream).text();
}

async function gzip(text: string): Promise<Uint8Array> {
  const stream = new Blob([text]).stream().pipeThrough(new CompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

function parseXml(text: string): Document {
  const doc = new DOMParser().parseFromString(text, 'application/xml');
  if (doc.querySelector('parsererror') !== null) {
    throw new Error('the puzzle file could not be read');
  }
  return doc;
}

/** The demo puzzle as its file has it. */
interface Original {
  bytes: Uint8Array;
  xml: Document;
  /** How many shapes the file has (the problem's pieces and result are among them). */
  shapes: number;
  /** The first problem's counts, for the played run. */
  assemblies: number;
  solutions: number;
  pieces: number;
}

function readOriginal(bytes: Uint8Array, xml: Document): Original {
  const problem = xml.querySelector('puzzle > problems > problem');
  const n = (name: string): number => Number(problem?.getAttribute(name) ?? '0');
  return {
    bytes,
    xml,
    shapes: xml.querySelectorAll('puzzle > shapes > voxel').length,
    assemblies: n('assemblies'),
    solutions: n('solutions'),
    pieces: problem?.querySelectorAll('shapes > shape').length ?? 0,
  };
}

/**
 * The document as the visitor left it, with the file's shapes in place of
 * its first `shapes` ones and the file's first problem (with its solutions)
 * in place of the document's first.
 */
function splice(current: Document, original: Original): string {
  const shapes = current.querySelector('puzzle > shapes');
  const problems = current.querySelector('puzzle > problems');
  const fileShapes = original.xml.querySelectorAll('puzzle > shapes > voxel');
  const fileProblem = original.xml.querySelector('puzzle > problems > problem');
  if (shapes === null || problems === null || fileProblem === null) {
    throw new Error('the puzzle file has no shapes or problems');
  }
  const voxels = [...shapes.children];
  fileShapes.forEach((v, i) => {
    const mine = voxels[i];
    if (mine !== undefined) {
      shapes.replaceChild(current.importNode(v, true), mine);
    }
  });
  const first = problems.firstElementChild;
  if (first !== null) {
    problems.replaceChild(current.importNode(fileProblem, true), first);
  }
  const text = new XMLSerializer().serializeToString(current);
  return text.startsWith('<?xml') ? text : `<?xml version="1.0"?>\n${text}`;
}

// ---- the played run --------------------------------------------------------------------

function progressAt(
  t: number,
  o: Original,
): { state: 'preparing' | 'assembling'; p: SolveProgress } {
  const prepare = 0.15;
  const stage = t < prepare ? 'prepare' : t < 0.85 ? 'assemble' : 'disassemble';
  const done = Math.min(o.assemblies, Math.floor(((t - prepare) / 0.7) * o.assemblies));
  const found = Math.max(0, done);
  const piece = Math.min(o.pieces, 1 + Math.floor((t / prepare) * o.pieces));
  const row = `, level 2: ${String(Math.floor(t * 37) % 9)}/9, split 1/${String(o.pieces - 1)}`;
  const activity =
    stage === 'prepare'
      ? `prepare piece ${String(piece)} of ${String(o.pieces)}`
      : stage === 'assemble'
        ? `assemble, disassemble ${String(Math.max(0, found - 1))} done 1 waiting${row}`
        : `disassemble ${String(o.assemblies)} of ${String(o.assemblies)}${row}`;
  return {
    state: stage === 'prepare' ? 'preparing' : 'assembling',
    p: {
      stage,
      elapsedMs: Math.round(t * RUN_MS),
      usedMs: Math.round(t * RUN_MS),
      piece,
      pieces: o.pieces,
      assemblyFraction: stage === 'prepare' ? 0 : Math.min(1, (t - prepare) / 0.7),
      iterations: Math.round(t * 48_211),
      assemblerThreads: 1,
      assemblies: found,
      solutions: t > 0.6 ? o.solutions : 0,
      disassembly: true,
      disasmCompleted: Math.max(0, found - (stage === 'assemble' ? 1 : 0)),
      disasmPending: stage === 'assemble' && found > 0 ? 1 : 0,
      disasmWorkers: 1,
      overall: Math.min(0.99, t),
      overallKnown: true,
      levelFraction: (t * 7) % 1,
      secondsLeft: Math.max(0, (1 - t) * RUN_MS) / 1000,
      activity,
      searched: 0,
      searchDepth: 0,
      searchTraced: 0,
      panexSearch: false,
    },
  };
}

class Demo {
  private readonly store: Store;
  private readonly solver: ReturnType<typeof startApp>['solver'];
  private readonly original: Original;
  /** The ids of the file's shapes and its problem in the document now (another after each open). */
  private shapeIds: string[] = [];
  private problem: string | null = null;
  private playing = false;

  constructor(app: ReturnType<typeof startApp>, original: Original) {
    this.store = app.store;
    this.solver = app.solver;
    this.original = original;
  }

  /** Opens the puzzle from `bytes` and shows its problem set to check disassembly. */
  async open(bytes: Uint8Array): Promise<void> {
    await this.store.openBytes(PUZZLE_NAME, bytes);
    const snap = this.store.snapshot;
    this.shapeIds = (snap?.shapes ?? []).slice(0, this.original.shapes).map((s) => s.id);
    const problem = snap?.problems[0]?.id ?? null;
    this.problem = problem;
    if (problem === null) {
      return;
    }
    // the Solver shows Assemble and Disassemble (the file does not say)
    const settings = (await this.store.engine.call('problem.get', { problem })).solverSettings;
    const set = await this.store.engine.call('problem.setSolverSettings', {
      problem,
      settings: { ...settings, disassemble: true },
    });
    await this.store.whenRevision(set.revision);
    this.solver.selectProblem(problem);
  }

  /** The bridge's solve.start: the demo problem's run is played. */
  solve(problem: string, events: RunEvents): boolean {
    if (problem !== this.problem || this.playing) {
      return false;
    }
    this.playing = true;
    void this.play(problem, events).finally(() => {
      this.playing = false;
    });
    return true;
  }

  private async play(problem: string, events: RunEvents): Promise<void> {
    const o = this.original;
    // the document as it is now, before the run is told (its edits stay)
    const restored = this.restored();
    events.state(problem, 'preparing');
    const start = performance.now();
    for (let t = 0; t < 1; t = (performance.now() - start) / RUN_MS) {
      const { state, p } = progressAt(t, o);
      events.progress(problem, state, p);
      await sleep(TICK_MS);
    }
    events.state(problem, 'finished');
    events.progress(problem, 'finished', {
      ...progressAt(0.999, o).p,
      stage: 'done',
      overall: 1,
      secondsLeft: 0,
      activity: 'finished',
      assemblies: o.assemblies,
      solutions: o.solutions,
      disasmCompleted: o.assemblies,
      disasmPending: 0,
    });
    events.solutionsAdded(problem, { assemblies: o.assemblies, solutions: o.solutions });
    // the run ends with the document opened again (its document.changed
    // ends the run for the renderer): the file alone when the spliced
    // document is refused
    try {
      await this.open(await restored);
    } catch (e) {
      console.warn('demo: the document could not be kept, the file is opened again', e);
      await this.open(o.bytes);
    }
    // the solution shown, as after a click on it
    await until(() => document.querySelector('#slvList .lrow[data-i="0"]') !== null, 3000);
    this.solver.select(0);
  }

  // the file's pieces and problem among the visitor's document; the file
  // alone when the pieces or the problem are not where the file has them
  private async restored(): Promise<Uint8Array> {
    const snap = this.store.snapshot;
    const same =
      snap !== null &&
      snap.problems[0]?.id === this.problem &&
      this.shapeIds.every((id, i) => snap.shapes[i]?.id === id);
    if (!same) {
      return this.original.bytes;
    }
    try {
      const saved = await this.store.engine.call('document.saveBytes');
      const current = parseXml(await gunzip(fromBase64(saved.data)));
      return await gzip(splice(current, this.original));
    } catch (e) {
      console.warn('demo: the document could not be kept, the file is opened again', e);
      return this.original.bytes;
    }
  }
}

async function main(): Promise<void> {
  const m = await createEngine();
  const client = new EngineClient(new WasmTransport(m));
  const hello = await client.hello();
  const app = demoApp(hello.version);
  const bridge = new EngineBridge(client, hello.version);
  Object.defineProperty(window, 'bt', { value: bridge });
  Object.defineProperty(window, 'btApp', { value: app });
  badge();
  const started = startApp();
  await stageReady();
  const r = await fetch(PUZZLE_URL);
  if (!r.ok) {
    throw new Error(`the demo puzzle: ${String(r.status)} ${r.statusText}`);
  }
  const bytes = new Uint8Array(await r.arrayBuffer());
  const demo = new Demo(started, readOriginal(bytes, parseXml(await gunzip(bytes))));
  bridge.revisionOf = () => started.store.snapshot?.revision ?? 0;
  bridge.solveHook = (problem, events) => demo.solve(problem, events);
  await demo.open(bytes);
}

void main();
