// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo page's entry (site/public/demo/, built by demo.vite.config.ts
// through tools/build-demo.ts): the app's renderer as it is, over the
// engine compiled to WebAssembly (engine/wasm_main.cpp, wasm-transport.ts,
// engine-bridge.ts) and the demo's app (demo-app.ts) in place of the
// preload's window.bt and window.btApp. One example puzzle of each puzzle
// type is on the site (site/public/examples/demo/<type>.xmpuzzle: a solved
// first problem and only its shapes); the top bar's list chooses one, and
// it is opened as a dropped file would be, its problem set to take apart
// where the grid can (Spheres and Tilted Square only assemble; Sliding and
// Stacking search for their own paths). The page's URL keeps the choice and the theme
// (`?type=sliding&theme=dark`), so Reset (a reload) starts that example
// again. Nothing else is kept.
//
// Solve plays a run without a search (a search would hold the page's one
// thread): the run's events go to the renderer for about three seconds,
// shaped as the puzzle type's solver sends them, then the saved solution
// is back. No method puts solutions into a document, so the puzzle is
// opened again from its file with the pieces and the problem as the file
// has them, the rest of the document as the visitor left it (`restore`):
// a changed piece dropped the solutions, as in the app, and comes back as
// it was; new shapes, problems and colors stay. The demo problem is the
// file's first; another problem's Solve is refused with the other
// searches (engine-bridge.ts).

import type { SolveProgress, SolveState } from '../engine/api.generated.ts';
import { EngineClient } from '../engine/client.ts';
import { fromBase64 } from '../engine/recorder.ts';
import { startApp } from '../renderer/shell/app.ts';
import { kindName } from '../renderer/shell/new-grids.ts';
import type { NewKind } from '../renderer/shell/new-grids.ts';
import { onPrefs, prefs, setPrefs } from '../renderer/shell/prefs.ts';
import type { Store } from '../renderer/state/store.ts';
import { demoApp } from './demo-app.ts';
import { EngineBridge } from './engine-bridge.ts';
import type { RunEvents } from './engine-bridge.ts';
import createEngine from './engine/shapelogic_engine.js';
import { WasmTransport } from './wasm-transport.ts';

/**
 * One puzzle type's example: its key in the URL and the file's name, its
 * kind in File ▸ New (a grid, or Tilted Square), the name it is opened under.
 */
interface Example {
  key: string;
  grid: NewKind;
  name: string;
}

/** The example opened when the URL names none. */
const DEFAULT_EXAMPLE: Example = { key: 'brick', grid: 'bricks', name: 'PelikanBurr.xmpuzzle' };

/** The examples, in File ▸ New's order. */
const EXAMPLES: readonly Example[] = [
  DEFAULT_EXAMPLE,
  { key: 'sliding', grid: 'sliding', name: 'Abe 1 to 6 Heart.xmpuzzle' },
  { key: 'stacking', grid: 'stacking', name: 'Panex Swap 3.xmpuzzle' },
  // tests/data/puzzles/tilted/TriplePlay.xmpuzzle's first problem and its shapes
  { key: 'tilted', grid: 'tilted', name: 'Triple Play.xmpuzzle' },
  { key: 'prism', grid: 'prisms', name: 'Prisgon.xmpuzzle' },
  { key: 'spheres', grid: 'spheres', name: 'BallRoom.xmpuzzle' },
  { key: 'rhombic', grid: 'rhombic', name: 'DiagonalCube.xmpuzzle' },
  { key: 'tetraocta', grid: 'tetraOcta', name: 'FourPieceTetrahedron.xmpuzzle' },
  { key: 'hexagonal', grid: 'hexagonal', name: 'HoneycombSeven.xmpuzzle' },
  { key: 'cairo', grid: 'cairo', name: 'PentagonSlab.xmpuzzle' },
];

/** Where an example's file is on the site. */
const exampleUrl = (e: Example): string => `/examples/demo/${e.key}.xmpuzzle`;

/** How long a played run takes, and how often it reports. */
const RUN_MS = 3000;
const TICK_MS = 100;

/**
 * What a run of the grid's solver does: assemble and take apart (the
 * brick family on a grid with a take-apart), only assemble (Spheres, and
 * a Tilted Square tray, whose pieces lie flat), the sliding search, the
 * stacking search.
 */
type RunKind = 'apart' | 'assemble' | 'sliding' | 'stacking';

function runKind(grid: NewKind): RunKind {
  return grid === 'sliding'
    ? 'sliding'
    : grid === 'stacking'
      ? 'stacking'
      : grid === 'spheres' || grid === 'tilted'
        ? 'assemble'
        : 'apart';
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

// ---- the page's URL --------------------------------------------------------------------

/** The example the URL names (`?type=`), else the first. */
function exampleFromUrl(): Example {
  const key = new URLSearchParams(window.location.search).get('type');
  return EXAMPLES.find((e) => e.key === key) ?? DEFAULT_EXAMPLE;
}

/** The theme the URL names (`?theme=light|dark`), else none (the setting's default). */
function themeFromUrl(): 'Light' | 'Dark' | null {
  const t = new URLSearchParams(window.location.search).get('theme');
  return t === 'light' ? 'Light' : t === 'dark' ? 'Dark' : null;
}

/** Puts the example and the theme chosen in the URL (in place: no step in the history). */
function writeUrl(example: Example): void {
  const q = new URLSearchParams(window.location.search);
  q.set('type', example.key);
  if (prefs.theme === 'Light' || prefs.theme === 'Dark') {
    q.set('theme', prefs.theme.toLowerCase());
  } else {
    q.delete('theme');
  }
  const url = `${window.location.pathname}?${q.toString()}${window.location.hash}`;
  if (url !== `${window.location.pathname}${window.location.search}${window.location.hash}`) {
    window.history.replaceState(window.history.state, '', url);
  }
}

// ---- the top bar -----------------------------------------------------------------------

const CHIP =
  'text-decoration:none;color:var(--accent);background:var(--accentSoft);font-weight:600;padding:2px 10px;font-size:calc(11px + var(--fs));cursor:pointer';
const SELECT =
  'height:calc(26px + var(--fh));border:1px solid var(--line2);border-radius:7px;background:var(--panel);color:var(--text);font:inherit;font-size:calc(12px + var(--fs));padding:0 6px;cursor:pointer';

/** The top bar's controls: the theme, Reset, Exit and the puzzle type (where the file's name was). */
interface Bar {
  type: HTMLSelectElement;
  theme: HTMLSelectElement;
}

function topBar(current: Example): Bar | null {
  const right = document.querySelector('.top .right');
  const fname = document.getElementById('fname');
  if (right === null) {
    return null;
  }
  const theme = document.createElement('select');
  theme.id = 'demoTheme';
  theme.setAttribute('aria-label', 'Theme');
  theme.title = 'Light or dark colors';
  theme.style.cssText = SELECT;
  theme.innerHTML =
    '<option value="Light">Light Mode</option><option value="Dark">Dark Mode</option>';
  const reset = document.createElement('button');
  reset.className = 'chip';
  reset.id = 'demoReset';
  reset.textContent = 'Reset';
  reset.title = 'Start this example again (your changes are dropped)';
  reset.style.cssText = `${CHIP};border:0;font:inherit;font-size:calc(11px + var(--fs));font-weight:600`;
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
  exit.style.cssText = CHIP;
  const type = document.createElement('select');
  type.id = 'demoType';
  type.setAttribute('aria-label', 'Puzzle type');
  type.title = 'Open an example of another puzzle type (your changes are dropped)';
  type.style.cssText = SELECT;
  for (const e of EXAMPLES) {
    const o = document.createElement('option');
    o.value = e.key;
    o.textContent = kindName(e.grid);
    type.append(o);
  }
  type.value = current.key;
  right.prepend(theme, reset, exit, type);
  // the file's name and its unsaved-changes dot are not shown: the list says what is open
  if (fname !== null) {
    fname.hidden = true;
  }
  // the brand at the top left does the same as Exit
  const brand = document.querySelector<HTMLElement>('.top .brand');
  if (brand !== null) {
    brand.style.cursor = 'pointer';
    brand.title = exit.title;
    brand.addEventListener('click', () => {
      exit.click();
    });
  }
  // the list gives way to the tabs as the file's name did (shell/topbar.ts's fitName)
  const fit = (): void => {
    fitType(type, theme, [reset, exit]);
  };
  const watch = new ResizeObserver(fit);
  for (const id of ['stage', 'appRail', 'brand', 'btnSettings']) {
    const el = document.getElementById(id);
    if (el !== null) {
      watch.observe(el);
    }
  }
  return { type, theme };
}

/** The narrowest the type list gets (its text is cut short below its own width). */
const TYPE_MIN_PX = 64;
/** Below this room for the type list, the theme list says Light and Dark alone. */
const TYPE_SHORT_PX = 130;

/** The theme list's labels: in full, or short where the bar is narrow. */
function themeLabels(theme: HTMLSelectElement, short: boolean): void {
  for (const o of theme.options) {
    const text = short ? o.value : `${o.value} Mode`;
    if (o.textContent !== text) {
      o.textContent = text;
    }
  }
}

/**
 * Lets the type list use the bar's room left of its right end: up to the
 * tabs when they are in the top bar, else the brand, less what is between
 * (the theme list, `others`: Reset and Exit, and the gaps). Where that
 * leaves the list short of its names, the theme list's labels are
 * shortened first.
 */
function fitType(
  type: HTMLSelectElement,
  theme: HTMLSelectElement,
  others: readonly HTMLElement[],
): void {
  const rail = document.getElementById('appRail');
  const brand = document.getElementById('brand');
  const atTop = document.documentElement.dataset.tabs === 'top';
  const edge = atTop
    ? (rail?.getBoundingClientRect().right ?? 0) + 16
    : (brand?.getBoundingClientRect().right ?? 0) + 24;
  const gap = 8;
  const room = (): number =>
    Math.round(
      type.getBoundingClientRect().right -
        edge -
        [theme, ...others].reduce((w, el) => w + el.getBoundingClientRect().width + gap, 0),
    );
  themeLabels(theme, false);
  if (room() < TYPE_SHORT_PX) {
    themeLabels(theme, true);
  }
  type.style.maxWidth = `${String(Math.max(TYPE_MIN_PX, room()))}px`;
}

/** The colors in use (the System setting's are the operating system's). */
function shownTheme(): 'Light' | 'Dark' {
  if (prefs.theme === 'Light' || prefs.theme === 'Dark') {
    return prefs.theme;
  }
  return matchMedia('(prefers-color-scheme: dark)').matches ? 'Dark' : 'Light';
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

/** An example's puzzle as its file has it. */
interface Original {
  example: Example;
  kind: RunKind;
  bytes: Uint8Array;
  xml: Document;
  /** How many shapes the file has (the problem's pieces and result are among them). */
  shapes: number;
  /** The first problem's counts, for the played run. */
  assemblies: number;
  solutions: number;
  /** The solutions the file keeps (an assemble-only run keeps assemblies). */
  saved: number;
  pieces: number;
  /** A sliding or stacking search's positions and depth. */
  searched: number;
  searchDepth: number;
}

function readOriginal(example: Example, bytes: Uint8Array, xml: Document): Original {
  const problem = xml.querySelector('puzzle > problems > problem');
  const n = (name: string): number => Number(problem?.getAttribute(name) ?? '0');
  return {
    example,
    kind: runKind(example.grid),
    bytes,
    xml,
    shapes: xml.querySelectorAll('puzzle > shapes > voxel').length,
    assemblies: n('assemblies'),
    solutions: n('solutions'),
    saved: problem?.querySelectorAll(':scope > solutions > solution').length ?? 0,
    pieces: problem?.querySelectorAll('shapes > shape').length ?? 0,
    searched: n('searched'),
    searchDepth: n('searchDepth'),
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

/** A progress report with nothing done yet, at `t` of the run. */
function idleProgress(t: number, o: Original): SolveProgress {
  return {
    stage: 'prepare',
    elapsedMs: Math.round(t * RUN_MS),
    usedMs: Math.round(t * RUN_MS),
    piece: 0,
    pieces: 0,
    assemblyFraction: 0,
    iterations: 0,
    assemblerThreads: 1,
    assemblies: 0,
    solutions: 0,
    disassembly: false,
    disasmCompleted: 0,
    disasmPending: 0,
    disasmWorkers: 0,
    overall: Math.min(0.99, t),
    overallKnown: true,
    levelFraction: 0,
    secondsLeft: Math.max(0, (1 - t) * RUN_MS) / 1000,
    activity: '',
    searched: 0,
    searchDepth: 0,
    searchTraced: 0,
    panexSearch: o.kind === 'stacking',
    stopKind: null,
    slideStop: null,
    atSavePoint: false,
    slideForecast: null,
    slidePickingUp: false,
    slidePickingUpMs: 0,
    slidePhase: null,
  };
}

/** The run's report at `t` (0 to 1), as the grid's solver sends it (core/shapelogic/solve/solve_progress.cpp's activity). */
function progressAt(t: number, o: Original): { state: SolveState; p: SolveProgress } {
  const prepare = 0.15;
  const base = idleProgress(t, o);
  const pieces = Math.max(1, o.pieces);
  if (t < prepare) {
    const piece = Math.min(pieces, 1 + Math.floor((t / prepare) * pieces));
    return {
      state: 'preparing',
      p: {
        ...base,
        stage: 'prepare',
        piece,
        pieces,
        assemblerThreads: o.kind === 'stacking' ? 0 : 1,
        activity: `prepare piece ${String(piece)} of ${String(pieces)}`,
      },
    };
  }
  const f = Math.min(1, (t - prepare) / (1 - prepare));
  if (o.kind === 'sliding' || o.kind === 'stacking') {
    // one search from the start to the goal: positions and depth grow, the path is found at the end
    return {
      state: 'assembling',
      p: {
        ...base,
        stage: 'search',
        iterations: o.kind === 'sliding' ? 11 : 0,
        assemblies: o.kind === 'sliding' ? o.assemblies : 0,
        searched: Math.round(o.searched * f * f),
        searchDepth: Math.round(o.searchDepth * f),
      },
    };
  }
  const assembling = Math.min(1, (t - prepare) / 0.7);
  const found = Math.max(0, Math.min(o.assemblies, Math.floor(assembling * o.assemblies)));
  const iterations = Math.round(t * 48_211);
  if (o.kind === 'assemble') {
    return {
      state: 'assembling',
      p: {
        ...base,
        stage: 'assemble',
        assemblyFraction: assembling,
        iterations,
        assemblies: found,
        activity: 'assemble',
      },
    };
  }
  const stage = t < 0.85 ? 'assemble' : 'disassemble';
  const row = `, level 2: ${String(Math.floor(t * 37) % 9)}/9, split 1/${String(pieces - 1)}`;
  return {
    state: 'assembling',
    p: {
      ...base,
      stage,
      assemblyFraction: assembling,
      iterations,
      assemblies: found,
      solutions: t > 0.6 ? o.solutions : 0,
      disassembly: true,
      disasmCompleted: Math.max(0, found - (stage === 'assemble' ? 1 : 0)),
      disasmPending: stage === 'assemble' && found > 0 ? 1 : 0,
      disasmWorkers: 1,
      levelFraction: (t * 7) % 1,
      activity:
        stage === 'assemble'
          ? `assemble, disassemble ${String(Math.max(0, found - 1))} done 1 waiting${row}`
          : `disassemble ${String(o.assemblies)} of ${String(o.assemblies)}${row}`,
    },
  };
}

class Demo {
  private readonly store: Store;
  private readonly solver: ReturnType<typeof startApp>['solver'];
  private original: Original | null = null;
  /** The ids of the file's shapes and its problem in the document now (another after each open). */
  private shapeIds: string[] = [];
  private problem: string | null = null;
  private playing = false;
  /** Told when a played run starts and ends (the top bar's list waits for its end). */
  onPlaying: (playing: boolean) => void = () => undefined;

  constructor(app: ReturnType<typeof startApp>) {
    this.store = app.store;
    this.solver = app.solver;
  }

  get busy(): boolean {
    return this.playing;
  }

  /** Opens an example from its file on the site (the document there before is dropped). */
  async load(example: Example): Promise<void> {
    const r = await fetch(exampleUrl(example));
    if (!r.ok) {
      throw new Error(`the ${example.key} example: ${String(r.status)} ${r.statusText}`);
    }
    const bytes = new Uint8Array(await r.arrayBuffer());
    this.original = readOriginal(example, bytes, parseXml(await gunzip(bytes)));
    await this.open(bytes);
  }

  /** Opens the example from `bytes` and shows its problem set as its grid's solver runs it. */
  private async open(bytes: Uint8Array): Promise<void> {
    const o = this.original;
    if (o === null) {
      return;
    }
    await this.store.openBytes(o.example.name, bytes);
    const snap = this.store.snapshot;
    this.shapeIds = (snap?.shapes ?? []).slice(0, o.shapes).map((s) => s.id);
    const problem = snap?.problems[0]?.id ?? null;
    this.problem = problem;
    if (problem === null) {
      return;
    }
    // the Solver shows Assemble and Disassemble where the grid takes apart,
    // Find Solutions for a sliding or stacking search, Assemble alone on
    // Spheres and a tilted tray (the file does not say)
    const settings = (await this.store.engine.call('problem.get', { problem })).solverSettings;
    const set = await this.store.engine.call('problem.setSolverSettings', {
      problem,
      settings: { ...settings, disassemble: o.kind !== 'assemble' },
    });
    await this.store.whenRevision(set.revision);
    this.solver.selectProblem(problem);
  }

  /** The bridge's solve.start: the demo problem's run is played. */
  solve(problem: string, events: RunEvents): boolean {
    if (problem !== this.problem || this.playing || this.original === null) {
      return false;
    }
    const o = this.original;
    this.playing = true;
    this.onPlaying(true);
    void this.play(problem, o, events).finally(() => {
      this.playing = false;
      this.onPlaying(false);
    });
    return true;
  }

  private async play(problem: string, o: Original, events: RunEvents): Promise<void> {
    // the document as it is now, before the run is told (its edits stay)
    const restored = this.restored(o);
    events.state(problem, 'preparing');
    const start = performance.now();
    for (let t = 0; t < 1; t = (performance.now() - start) / RUN_MS) {
      const { state, p } = progressAt(t, o);
      events.progress(problem, state, p);
      await sleep(TICK_MS);
    }
    const last = progressAt(0.999, o).p;
    events.state(problem, 'finished');
    events.progress(problem, 'finished', {
      ...last,
      stage: 'done',
      overall: 1,
      secondsLeft: 0,
      activity: 'finished',
      assemblies: o.assemblies,
      solutions: o.solutions,
      disasmCompleted: last.disassembly ? o.assemblies : 0,
      disasmPending: 0,
      searched: o.searched,
      searchDepth: o.searchDepth,
    });
    events.solutionsAdded(problem, {
      assemblies: o.assemblies,
      solutions: o.solutions,
      saved: o.saved,
    });
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
  private async restored(o: Original): Promise<Uint8Array> {
    const snap = this.store.snapshot;
    const same =
      snap !== null &&
      snap.problems[0]?.id === this.problem &&
      this.shapeIds.every((id, i) => snap.shapes[i]?.id === id);
    if (!same) {
      return o.bytes;
    }
    try {
      const saved = await this.store.engine.call('document.saveBytes');
      const current = parseXml(await gunzip(fromBase64(saved.data)));
      return await gzip(splice(current, o));
    } catch (e) {
      console.warn('demo: the document could not be kept, the file is opened again', e);
      return o.bytes;
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
  let example = exampleFromUrl();
  const bar = topBar(example);
  const theme = themeFromUrl();
  const started = startApp(theme === null ? {} : { prefs: { theme } });
  await stageReady();
  const demo = new Demo(started);
  bridge.revisionOf = () => started.store.snapshot?.revision ?? 0;
  bridge.solveHook = (problem, events) => demo.solve(problem, events);
  if (bar !== null) {
    // the theme: shown as it is, the setting changed here or in Settings
    const showTheme = (): void => {
      bar.theme.value = shownTheme();
    };
    showTheme();
    bar.theme.addEventListener('change', () => {
      setPrefs({ theme: bar.theme.value === 'Dark' ? 'Dark' : 'Light' });
    });
    onPrefs((changed) => {
      if (changed.includes('theme')) {
        showTheme();
        writeUrl(example);
      }
    });
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', showTheme);
    // the puzzle type: another example opened, one at a time, not while a run plays
    let loading = false;
    demo.onPlaying = (playing) => {
      bar.type.disabled = playing || loading;
    };
    bar.type.addEventListener('change', () => {
      const next = EXAMPLES.find((e) => e.key === bar.type.value);
      if (next === undefined || next === example || demo.busy) {
        bar.type.value = example.key;
        return;
      }
      loading = true;
      bar.type.disabled = true;
      void demo
        .load(next)
        .then(() => {
          example = next;
          writeUrl(example);
        })
        .catch((e: unknown) => {
          console.error('demo: the example could not be opened', e);
          bar.type.value = example.key;
        })
        .finally(() => {
          loading = false;
          bar.type.disabled = demo.busy;
        });
    });
  }
  writeUrl(example);
  // the list waits for the first example
  if (bar !== null) {
    bar.type.disabled = true;
  }
  try {
    await demo.load(example);
  } finally {
    if (bar !== null) {
      bar.type.disabled = demo.busy;
    }
  }
}

void main();
