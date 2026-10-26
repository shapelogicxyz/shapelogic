// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace put together (FRONTEND-PLAN.md, stage 4): the
// problem, what to check, the search options and the run in the left card
// (run-card.ts), the solutions as a list or a slider, the player, the
// sort, the actions and the piece chips in the right (solutions-card.ts),
// the solution in the view (player.ts over view3d/solution.ts), the keys,
// the status bar and the view's tag. The problem shown is read from the
// engine (problem.get; problem.kind once) after every change; its solve
// is the solve record's (state/solves.ts: the events', a run's end its
// revision); its solutions are paged by the engine (pager.ts) and read
// again when its solutionsVersion moves. Every action is a call: a run's
// start waits for its reply (then the run is the record's), a removal or
// a stored option for its revision. What the engine would refuse as
// invalid_state is refused first: the options of a problem being solved,
// a pause with no run under way, an abort once the run has ended (the
// button greys out, as the original's); the frozen document says "pause
// the solve first". A stacking puzzle (docs/webui-spec.md, section 9.3):
// its options and its one solver by the board, Solve and Continue off
// without a board or with invalid stacks, Solve asking about a saved
// search first (Continue, Start Over: mainwindow_solve.cpp's cb_BtnStart),
// Continue and Abort for a saved search (solve.status.savedSearch, the
// original's abortable), the Panex or stacking search on the Activity
// line, the board (stacking.board) for the player. From docs/mockup/index.html:
// `renderSolver`, `svSelect`, `svManage`, `svTag`, `svStatus`, `svKey`
// and the Solver keydown handlers, and `SV`.

import type {
  Methods,
  ProblemDetail,
  ProblemSummary,
  SolutionRow,
  SolveState,
  Solver,
} from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import { isActive } from '../state/solves.ts';
import type { SolveRecord, Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';
import { askConfirm } from '../shell/confirm.ts';
import { askChoice, tell } from '../shell/dialog.ts';
import { byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { onKey } from '../shell/keyboard.ts';
import { onLayout, tab } from '../shell/layout.ts';
import { isTray } from '../shell/piece-colours.ts';
import { onPrefs } from '../shell/prefs.ts';
import { flash, reportError, updateStatus } from '../shell/status.ts';
import type { View3D } from '../view3d/view.ts';
import { problemChipStyle } from '../puzzle/counts.ts';
import { Explorer } from '../explore/explorer.ts';
import { Autosave } from './autosave.ts';
import { SolutionPager } from './pager.ts';
import type { SortKey } from './pager.ts';
import { Player } from './player.ts';
import { RunCard } from './run-card.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_OPTIONS,
  defaultsOf,
  familyOf,
  normalize,
  readOptions,
  slidingActivity,
  sameOptions,
  solveOptions,
  stackingActivity,
  stackingSolverOf,
  writeOptions,
} from './settings.ts';
import type { BoxName, Family, Limits, SearchOptions } from './settings.ts';
import { SolutionsCard } from './solutions-card.ts';
import { StatsPanel } from './stats-panel.ts';

export type ProblemKind = Methods['problem.kind']['result'];

/** A stacking problem's board as the engine draws it (stacking.board). */
export type Board = Methods['stacking.board']['result'];

/** A stacking problem's saved search (solve.status.savedSearch). */
export type SavedSearch = NonNullable<Methods['solve.status']['result']['savedSearch']>;

/** What the Remove menu removes (solution.delete's `which`). */
export type RemoveWhich = 'one' | 'before' | 'after' | 'withoutDisassembly' | 'all';

/** An analysis this client started: what it takes apart, for the ones that did not come apart. */
interface AnalysisRun {
  problem: string;
  /** The solutions it takes apart, in the stored order; null while being read. */
  targets: string[] | null;
}

/** How a sort key sorts first (the mockup's svDefDir: the number ascending, the others hardest first). */
export function firstDescending(k: SortKey): boolean {
  return k !== 'assembly' && k !== 'list';
}

export class SolverWorkspace {
  readonly store: Store;
  readonly edits: Edits;
  readonly solves: Solves;
  readonly player: Player;
  /** The problem shown, or null when the puzzle has none. */
  current: string | null = null;
  detail: ProblemDetail | null = null;
  kind: ProblemKind | null = null;
  /** The options shown (the problem's stored ones until changed). */
  options: SearchOptions = { ...DEFAULT_OPTIONS };
  /** The original's Display Limit and Keep Each: not stored with a problem. */
  limits: Limits = { ...DEFAULT_LIMITS };
  /** Whether "Search options" is open. */
  optOpen = false;
  /** The solutions: the stored order or a sort, as a list or a slider. */
  sortBy: SortKey = 'list';
  descending = false;
  view: 'list' | 'slider' = 'list';
  pager: SolutionPager | null = null;
  /** The solution selected: its place in the sorted list and its id. */
  sel = -1;
  selId: string | null = null;
  /** A run call under way (Solve, Prepare, Continue, Pause, Clear). */
  calling = false;
  /** The snapshot revision the cards show, or -1 while loading. */
  shown = -1;
  readonly autosave: Autosave;
  /** The step-by-step explorers (the drawer over the view). */
  readonly explorer: Explorer;
  /** The debug statistics panel. */
  readonly stats: StatsPanel;
  /** Solutions ▸ Export to STL: the exports' dialog for a solution (set by the app). */
  exportStl: ((problem: string, row: SolutionRow) => void) | null = null;
  private readonly view3d: View3D;
  private readonly runCard: RunCard;
  private readonly solCard: SolutionsCard;
  private readonly kinds = new Map<string, Promise<ProblemKind>>();
  /** Solutions found not to come apart by an analysis, per problem (the client remembers). */
  private readonly inseparables = new Map<string, Set<string>>();
  private analysisRun: AnalysisRun | null = null;
  /** The options of the runs this client started, by problem (autosave carries them on). */
  private readonly launched = new Map<string, ReturnType<typeof solveOptions>>();
  /** Waiting for a document.changed of a problem's solutions. */
  private changeWaiters: { problem: string; done: () => void }[] = [];
  private generation = -1;
  private loads = 0;
  private waiting: { revision: number; done: () => void }[] = [];

  constructor(store: Store, view: View3D, edits: Edits, solves: Solves) {
    this.store = store;
    this.view3d = view;
    this.edits = edits;
    this.solves = solves;
    this.player = new Player({
      store,
      view,
      detail: () => this.detail,
      changed: () => {
        this.solCard.renderPlayer();
        this.solCard.renderChips();
        this.renderTag();
      },
      inseparable: (id) => this.inseparable(id),
      current: (id) => id === this.selId && this.pager?.current(this.sel, this.sel + 1) === true,
      board: () => {
        const layout = this.board?.layout ?? null;
        const centre = this.board?.centre ?? null;
        return layout === null || centre === null ? null : { layout, centre };
      },
    });
    this.runCard = new RunCard(this);
    this.solCard = new SolutionsCard(this);
    this.autosave = new Autosave(
      store,
      solves,
      (p) => this.launchOptions(p),
      (p) => {
        // a sliding run carried on after an autosave tells its note at its end too
        if (this.family !== 'brick') {
          void this.tellNote(p);
        }
      },
    );
    this.explorer = new Explorer({
      store,
      view,
      problem: () => this.current,
      detail: () => this.detail,
      options: () => this.optionsNow(),
      solution: () =>
        this.selId !== null && this.player.row?.id === this.selId ? this.player.row : null,
      assemblyReady: () => this.current !== null && this.stateOf(this.current) === 'paused',
      changed: () => {
        this.explorer.renderButtons();
        this.renderTag();
      },
    });
    this.stats = new StatsPanel(store, solves, () => this.current);
    byId('slvStats').addEventListener('click', () => {
      this.stats.open();
    });
    view.solver = {
      tag: () => this.tagHtml(),
      empty: () => this.emptyText(),
      styleChanged: () => {
        this.player.restyle();
        this.explorer.restyle();
      },
      movesPieces: () => this.explorer.movesPieces,
      pieceDragged: (piece, dx, dy) => {
        this.explorer.pieceDragged(piece, dx, dy);
      },
    };
    store.subscribe((c) => {
      if (c.kind === 'document') {
        this.documentChanged();
      } else if (c.kind === 'changed') {
        const p = c.change;
        const named = (id: string): boolean => p.problems.length === 0 || p.problems.includes(id);
        const ready = this.changeWaiters.filter(
          (w) => named(w.problem) && p.what.includes('solutions'),
        );
        this.changeWaiters = this.changeWaiters.filter((w) => !ready.includes(w));
        for (const w of ready) {
          w.done();
        }
        if (
          this.current !== null &&
          (p.problems.length === 0 || p.problems.includes(this.current)) &&
          (p.what.includes('solutions') || p.what.includes('problems'))
        ) {
          // the list may have changed (a removal, a run's end, an analysis)
          this.pager?.refresh();
        }
      } else if (c.kind === 'status' && !store.running) {
        this.shown = -1;
      }
    });
    solves.subscribe((c) => {
      if (c.kind === 'added' && c.problem !== null && c.problem === this.current) {
        const a = solves.peek(c.problem)?.added;
        if (a !== null && a !== undefined) {
          this.pager?.added(a.solutionsVersion);
        }
      }
      if (c.kind === 'analysis' && c.problem !== null) {
        this.analysisChanged(c.problem);
      }
      if (c.kind === 'error' && c.problem !== null && c.problem === this.current) {
        const f = solves.peek(c.problem)?.failure;
        if (f !== null && f !== undefined) {
          flash(`The solve failed: ${f.message}`, 6000);
        }
      }
      if (c.kind === 'ended' && c.problem === this.current && this.sel < 0) {
        this.pageChanged();
      }
      if (c.problem === null || c.problem === this.current) {
        this.runCard.render();
        this.solCard.renderActions();
        this.explorer.refresh();
        if (tab === 'solver') {
          updateStatus();
          this.renderTag();
        }
      }
    });
    onLayout(() => {
      if (tab === 'solver') {
        this.render();
        this.showSelected();
      } else {
        // out of sight: the playback stops
        this.player.pause();
      }
    });
    onPrefs((changed) => {
      if (changed.includes('fade')) {
        this.player.restyle();
      }
      // the Default solver type: shown at once for a problem that stored none
      const d = this.detail;
      if (
        changed.includes('solverType') &&
        d !== null &&
        this.family === 'brick' &&
        d.solverSettings.solverType === undefined
      ) {
        this.options = readOptions(d.solverSettings, undefined, this.family, this.canDisassemble);
        this.render();
      }
    });
    this.installKeys();
    this.documentChanged();
  }

  // ---- what is shown ----

  /** The summary of the problem shown, as the snapshot has it. */
  get problem(): ProblemSummary | null {
    return this.store.snapshot?.problems.find((p) => p.id === this.current) ?? null;
  }

  /** The solve record of the problem shown. */
  get record(): SolveRecord | null {
    return this.current === null ? null : this.solves.get(this.current);
  }

  /** The state of a problem's solve: the events', else the snapshot's. */
  stateOf(problem: string): SolveState {
    const r = this.solves.peek(problem);
    if (r?.told === true) {
      return r.state;
    }
    return this.store.snapshot?.problems.find((p) => p.id === problem)?.solve ?? 'idle';
  }

  /** Whether the problem shown is analysed now. */
  get analysing(): boolean {
    const r = this.record;
    return (this.problem?.analysing ?? false) || r?.analysis?.active === true;
  }

  /** Whether a run of the problem shown is under way or ending. */
  get running(): boolean {
    const r = this.record;
    return r !== null && r.phase !== 'none';
  }

  /** Whether a run of the problem shown changes its list now (started here and not over). */
  private get runUnderWay(): boolean {
    const r = this.record;
    return r !== null && (r.phase !== 'none' || r.launched);
  }

  /** Why a sliding problem's start/goal shape cannot be solved yet (null: it can, or no sliding problem). */
  slideError: string | null = null;

  /** Whether the puzzle is a sliding one (its own options, decision 0014). */
  get sliding(): boolean {
    return this.store.snapshot?.grid === 'sliding';
  }

  /** Whether the puzzle is a stacking one (its own options, decision 0015). */
  get stacking(): boolean {
    return this.store.snapshot?.grid === 'stacking';
  }

  /** The family of the puzzle's options. */
  get family(): Family {
    return familyOf(this.store.snapshot?.grid);
  }

  /** Whether the puzzle's grid has a take-apart (spheres, rhombic and tetra-octa have none). */
  get canDisassemble(): boolean {
    const grid = this.store.snapshot?.grid;
    return this.store.info?.grids.find((g) => g.grid === grid)?.disassemble !== false;
  }

  /** A stacking problem's board (null: none read, or another grid). */
  board: Board | null = null;

  /** A stacking problem's saved search, as the last read found it (null: none). */
  savedSearch: SavedSearch | null = null;

  /** The one solver of a stacking problem's board (syncSolverTypeMenu). */
  get stackingSolver(): 'stacking' | 'panex' {
    const id = this.detail?.rodSet ?? null;
    const set = this.store.snapshot?.rodSets.find((r) => r.id === id);
    return stackingSolverOf(set?.panexColumns ?? false);
  }

  /**
   * Why a stacking problem cannot be solved as it stands (no board, a rule
   * broken: Solve's and Continue's tooltip in the original); null when it
   * can, or on other grids.
   */
  get stackError(): string | null {
    if (!this.stacking || this.detail === null) {
      return null;
    }
    if (this.detail.rodSet === null) {
      return 'Choose a rod set with Set Start/Goal Rods';
    }
    return this.board?.error ?? null;
  }

  /** The SolveOptions of the options shown. */
  private optionsNow(o: SearchOptions = this.options): ReturnType<typeof solveOptions> {
    return solveOptions(o, this.limits, this.family, this.stackingSolver);
  }

  /** The label of a shape (S1...), or its id. */
  labelOf(shape: string): string {
    return this.store.snapshot?.shapes.find((s) => s.id === shape)?.label ?? shape;
  }

  /** Whether an analysis this session found a solution not to come apart. */
  inseparable(id: string): boolean {
    return this.current !== null && (this.inseparables.get(this.current)?.has(id) ?? false);
  }

  /** The row selected, if its page came. */
  get selected(): SolutionRow | null {
    return this.sel < 0 ? null : (this.pager?.row(this.sel) ?? null);
  }

  /** Shows another problem. */
  selectProblem(id: string): void {
    if (id === this.current) {
      return;
    }
    this.current = id;
    this.detail = null;
    this.kind = null;
    this.board = null;
    this.savedSearch = null;
    byId('leftS').dataset.revision = '';
    this.newPager(true);
    this.load();
    this.render();
  }

  /** Draws the cards, the tag and the status. */
  render(): void {
    this.runCard.render();
    this.solCard.render();
    this.explorer.refresh();
    this.renderTag();
    if (tab === 'solver') {
      updateStatus();
    }
  }

  private renderTag(): void {
    this.view3d.renderLabels();
  }

  /** The shown problem's chip style, by its place in the list. */
  probChip(id: string | undefined = this.problem?.id): string {
    return problemChipStyle(this.store.snapshot?.problems.findIndex((q) => q.id === id) ?? 0);
  }

  // the mockup's svTag: the problem's chip and name, and the solution shown
  private tagHtml(): string {
    const p = this.problem;
    if (p === null) {
      return '';
    }
    const n = this.pager?.total ?? 0;
    const cur = this.player.row !== null && this.sel >= 0 && n > 0;
    const ex = this.explorer.tag();
    return `<span class="chip" style="${this.probChip()}">${esc(p.label)}</span><span class="nm">${esc(p.name) || 'Problem'}</span>${ex !== '' ? ex : cur ? `<span class="mute">· Solution ${String(this.sel + 1)} of ${String(n)}</span>` : ''}`;
  }

  // the mockup's renderSolverScene's message over an empty scene
  private emptyText(): string {
    const snap = this.store.snapshot;
    if (snap === null) {
      return '';
    }
    if (snap.problems.length === 0) {
      return 'No problems — make one in the Puzzle workspace';
    }
    if (this.player.row !== null || this.explorer.mode !== null) {
      return '';
    }
    if ((this.pager?.total ?? 0) > 0) {
      return 'Choose a solution in the list to see it here';
    }
    const state = this.current === null ? 'idle' : this.stateOf(this.current);
    if (isActive(state)) {
      return 'Searching…';
    }
    const p = this.problem;
    if (p !== null && (p.assemblies ?? 0) > 0 && p.saved === 0 && this.options.disassemble) {
      return 'No assembly can be taken apart — nothing to show';
    }
    return 'Run the solver to see solutions here';
  }

  /** The status bar in the Solver workspace (HTML): the mockup's svStatus. */
  statusLine(): string {
    const snap = this.store.snapshot;
    if (snap !== null && snap.problems.length === 0) {
      return 'No problems — make one in the <b>Puzzle</b> workspace';
    }
    const p = this.problem;
    if (p === null) {
      return '';
    }
    const c = this.counts();
    const dis = this.options.disassemble;
    return `Problem <b>${esc(p.label)}</b> — ${String(c.assemblies ?? 0)} assemblies found${dis ? `, ${String(c.solutions ?? 0)} can be disassembled` : ''} · Activity: ${esc(this.activity())}`;
  }

  /** The counters: a run's from its progress, else the problem's. */
  counts(): {
    assemblies: number | null;
    solutions: number | null;
    timeMs: number | null;
    secondsLeft: number | null;
    overall: number;
  } {
    const p = this.problem;
    const r = this.record;
    const g = r?.progress ?? null;
    // a run's counters until the snapshot shows its end
    const shownRev = this.store.snapshot?.revision ?? -1;
    if (g !== null && r !== null && (r.phase !== 'none' || shownRev < r.endRevision)) {
      return {
        assemblies: g.assemblies,
        solutions: g.solutions,
        timeMs: g.elapsedMs,
        secondsLeft: g.secondsLeft,
        overall: g.overall,
      };
    }
    const done = p?.state === 'solved';
    return {
      assemblies: p?.assemblies ?? null,
      solutions: p?.solutions ?? null,
      timeMs: p?.timeMs ?? null,
      secondsLeft: null,
      overall: done ? 1 : (g?.overall ?? 0),
    };
  }

  /** The activity text: the run's (the original's status text), else the problem's state. */
  activity(): string {
    const r = this.record;
    const p = this.problem;
    if (this.analysing) {
      return 'analysing disassemblies';
    }
    if (r !== null && r.phase !== 'none' && r.progress !== null) {
      const g = r.progress;
      if (g.stage === 'search') {
        return this.stacking
          ? stackingActivity(g.panexSearch, g.searched, g.searchDepth, g.searchTraced)
          : slidingActivity(g.searched, g.searchDepth);
      }
      return g.activity;
    }
    // a stacking problem's search saved on disk (updateInterface)
    if (this.savedSearch !== null) {
      return 'Paused. The search is saved.';
    }
    if (r?.failure !== null && r?.failure !== undefined && this.stateOf(r.problem) === 'error') {
      return 'error';
    }
    switch (p?.state) {
      case 'solved':
        return 'Finished.';
      case 'solving':
        return 'Paused.';
      case 'unknown':
        return 'Partly solved.';
      default:
        return 'Not solved yet.';
    }
  }

  // ---- loading ----

  private documentChanged(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (store.generation !== this.generation) {
      this.generation = store.generation;
      this.current = null;
      this.detail = null;
      this.kind = null;
      this.board = null;
      this.savedSearch = null;
      this.kinds.clear();
      this.inseparables.clear();
      this.analysisRun = null;
      this.launched.clear();
      this.changeWaiters = [];
      this.player.close();
      this.pager?.close();
      this.pager = null;
    }
    if (snap !== null) {
      const ids = snap.problems.map((p) => p.id);
      if (this.current === null || !ids.includes(this.current)) {
        this.current = snap.problems[0]?.id ?? null;
        this.detail = null;
        this.kind = null;
        this.board = null;
        this.savedSearch = null;
        this.newPager(true);
      }
    }
    this.load();
    this.render();
  }

  /** Reads the problem shown again (problem.get; its kind once); the newest load wins. */
  private load(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (snap === null || !store.running) {
      return;
    }
    const n = ++this.loads;
    const revision = snap.revision;
    const id = this.current;
    if (id === null) {
      this.loaded(n, revision, null, null);
      return;
    }
    // a sliding problem's start/goal shape: whether Solve can start (its validity bar)
    const result = snap.problems.find((q) => q.id === id)?.result ?? null;
    const tray =
      snap.grid === 'sliding' && result !== null && isTray(snap.shapes.find((x) => x.id === result))
        ? result
        : null;
    // a stacking problem's board (its validity, its centre for the player)
    // and its saved search
    const stacking = snap.grid === 'stacking';
    Promise.all([
      store.engine.call('problem.get', { problem: id }, { coalesce: 'solver:problem.get' }),
      this.kindOf(id),
      tray === null
        ? Promise.resolve(null)
        : store.engine.call('sliding.tray', { tray }, { coalesce: 'solver:sliding.tray' }),
      stacking
        ? store.engine.call(
            'stacking.board',
            { problem: id, goal: false },
            { coalesce: 'solver:stacking.board' },
          )
        : Promise.resolve(null),
      stacking
        ? store.engine.call('solve.status', { problem: id }, { coalesce: 'solver:solve.status' })
        : Promise.resolve(null),
    ]).then(
      ([d, k, t, b, st]) => {
        if (id === this.current) {
          this.slideError = t?.error ?? null;
          // a solution shown before the board came is placed again around its middle
          const first = this.board === null && b !== null;
          this.board = b;
          if (first) {
            this.player.restyle();
          }
          this.savedSearch = st?.savedSearch ?? null;
          this.loaded(n, revision, d, k);
        }
      },
      (err: unknown) => {
        if (
          err instanceof EngineError &&
          (err.errc === 'superseded' || err.errc === 'not_found' || err.errc === 'disconnected')
        ) {
          return;
        }
        if (n === this.loads) {
          reportError(err, 'The problem could not be read');
          this.release(revision);
        }
      },
    );
  }

  private kindOf(id: string): Promise<ProblemKind> {
    const k = `${String(this.store.generation)}:${id}`;
    let p = this.kinds.get(k);
    if (p === undefined) {
      p = this.store.engine.call('problem.kind', { problem: id });
      p.catch(() => {
        this.kinds.delete(k);
      });
      this.kinds.set(k, p);
    }
    return p;
  }

  private loaded(
    n: number,
    revision: number,
    d: ProblemDetail | null,
    k: ProblemKind | null,
  ): void {
    if (n !== this.loads) {
      return;
    }
    const before = this.detail;
    this.detail = d;
    this.kind = k;
    this.shown = revision;
    // the stored options, unless the problem's are being changed here
    if (d !== null && (before?.id !== d.id || !this.editingOptions)) {
      this.options = readOptions(d.solverSettings, undefined, this.family, this.canDisassemble);
    }
    if (d !== null) {
      // a stacking search saves itself (its own 20 minutes): no save point
      this.autosave.want(d.id, this.options.autosave && !this.stacking);
    }
    const left = byId('leftS');
    left.dataset.problem = this.current ?? '';
    left.dataset.revision = String(revision);
    if (this.pager === null && this.current !== null) {
      this.newPager(false);
    }
    this.render();
    this.release(revision);
  }

  private release(revision: number): void {
    const ready = this.waiting.filter((w) => w.revision <= revision);
    this.waiting = this.waiting.filter((w) => w.revision > revision);
    for (const w of ready) {
      w.done();
    }
  }

  /** Resolves when the cards show a revision at least this one (or another document came). */
  whenShown(revision: number): Promise<void> {
    const gen = this.store.generation;
    return this.store.whenRevision(revision).then(() => {
      if (this.shown >= revision || this.store.generation !== gen) {
        return;
      }
      return new Promise<void>((done) => {
        this.waiting.push({ revision, done });
      });
    });
  }

  // ---- the solutions list ----

  /** A new pager for the problem shown and the sort (`reset`: the selection too). */
  private newPager(reset: boolean): void {
    this.pager?.close();
    this.pager = null;
    if (reset) {
      this.sel = -1;
      this.selId = null;
      this.player.show(null, null);
    }
    const id = this.current;
    if (id === null || !this.store.running) {
      this.solCard.render();
      return;
    }
    const pager = new SolutionPager(this.store.engine, id, this.sortBy, this.descending);
    pager.onError((e) => {
      reportError(e, 'The solutions could not be read');
    });
    pager.onChange(() => {
      if (pager === this.pager) {
        this.pageChanged();
      }
    });
    this.pager = pager;
    this.solCard.render();
    pager.want(0, 100);
  }

  // new rows: keep the solution selected where it went, or select the first
  private pageChanged(): void {
    const pager = this.pager;
    if (pager === null) {
      return;
    }
    const total = pager.total ?? 0;
    if (total === 0) {
      this.sel = -1;
      this.selId = null;
    } else if (this.selId !== null) {
      const at = pager.indexOf(this.selId);
      if (at >= 0) {
        this.sel = at;
      } else {
        this.sel = Math.min(Math.max(this.sel, 0), total - 1);
        this.selId = pager.row(this.sel)?.id ?? null;
      }
    } else if (this.sel >= 0 || !this.runUnderWay) {
      // the first solution, unless a run is changing the list (it is chosen once the run is over)
      this.sel = Math.min(Math.max(this.sel, 0), total - 1);
      this.selId = pager.row(this.sel)?.id ?? null;
    }
    this.solCard.render();
    this.showSelected();
    this.renderTag();
    if (tab === 'solver') {
      updateStatus();
    }
  }

  // the player shows the solution selected: only while the Solver is shown,
  // and only a row of the list's newest version (an older one may be gone)
  private showSelected(): void {
    const row = this.selected;
    if (this.sel < 0) {
      this.player.show(null, null);
      this.explorer.refresh();
      return;
    }
    if (row === null || tab !== 'solver' || this.pager?.current(this.sel, this.sel + 1) !== true) {
      return;
    }
    this.player.show(this.current, row);
    this.explorer.refresh();
  }

  /** Selects the solution at a place of the sorted list (clamped). */
  select(i: number): void {
    const total = this.pager?.total ?? 0;
    if (total === 0) {
      return;
    }
    this.sel = Math.max(0, Math.min(total - 1, i));
    const row = this.pager?.row(this.sel);
    this.selId = row?.id ?? null;
    this.pager?.want(this.sel - 50, this.sel + 50);
    this.solCard.selectionChanged();
    this.showSelected();
    this.renderTag();
  }

  /**
   * Sorts by a key: a header's first click its first way, the second the
   * other way, the third the saved order (`list`) again.
   */
  sort(k: SortKey): void {
    if (this.sortBy !== k) {
      this.sortBy = k;
      this.descending = firstDescending(k);
    } else if (k !== 'list' && this.descending === firstDescending(k)) {
      this.descending = !this.descending;
    } else {
      this.sortBy = 'list';
      this.descending = false;
    }
    this.sel = 0;
    this.selId = null;
    this.newPager(false);
  }

  // ---- options (stored with the problem: problem.setSolverSettings) ----

  /** Options being stored now (the problem's are not read over them meanwhile). */
  private editingOptions = false;

  /** Whether the options can be changed now (not while the problem is solved or analysed). */
  get optionsFrozen(): boolean {
    return (
      this.running ||
      this.analysing ||
      isActive(this.current === null ? 'idle' : this.stateOf(this.current))
    );
  }

  /** A check box changed (normalised by the original's rules), or the solver or the sort. */
  async setOption(change: Partial<SearchOptions>, box: BoxName | null = null): Promise<void> {
    const problem = this.current;
    const d = this.detail;
    if (problem === null || d === null) {
      return;
    }
    if (this.edits.refuse(this.optionsFrozen)) {
      this.render();
      return;
    }
    const next = normalize(
      { ...this.options, ...change },
      box,
      this.family,
      this.canDisassemble,
    ).options;
    if (sameOptions(next, this.options)) {
      this.render();
      return;
    }
    this.options = next;
    this.render();
    this.editingOptions = true;
    try {
      await this.edits.mutate(
        'Solver options',
        () =>
          this.store.engine.call('problem.setSolverSettings', {
            problem,
            settings: writeOptions(next, d.solverSettings, this.family),
          }),
        (r) => this.whenShown(r),
      );
    } finally {
      this.editingOptions = false;
    }
    if (this.detail !== null) {
      this.options = readOptions(
        this.detail.solverSettings,
        undefined,
        this.family,
        this.canDisassemble,
      );
    }
    this.render();
  }

  /**
   * A sliding or stacking run's note when it ends (the original's
   * bt_message of the solver note): whether a missing solution is proven,
   * the search gave up at a limit, or (a stacking search paused) where it
   * was saved. Not for a pause to autosave, which carries on.
   */
  private async tellNote(problem: string): Promise<void> {
    const gen = this.store.generation;
    await this.solves.whenEnded(problem);
    if (gen !== this.store.generation || this.autosave.busy) {
      return;
    }
    try {
      const st = await this.store.engine.call('solve.status', { problem });
      if (st.note !== null && gen === this.store.generation) {
        tell(st.note);
      }
    } catch (e) {
      if (!(e instanceof EngineError) || e.errc !== 'not_found') {
        reportError(e, 'The solver’s note could not be read');
      }
    }
  }

  /** Keep at most / keep every n-th (not stored, as the original's). */
  setLimits(change: Partial<Limits>): void {
    this.limits = {
      keepAtMost: Math.max(0, Math.round(change.keepAtMost ?? this.limits.keepAtMost)),
      keepEach: Math.max(1, Math.round(change.keepEach ?? this.limits.keepEach)),
    };
    this.render();
  }

  /** The options a run of a problem was started with (the problem shown's as shown otherwise). */
  launchOptions(problem: string): ReturnType<typeof solveOptions> {
    return (
      this.launched.get(problem) ??
      this.optionsNow(problem === this.current ? this.options : defaultsOf(this.family))
    );
  }

  // ---- the run ----

  /** Solve, Prepare or Continue: the run is the record's once the call is answered. */
  async run(kind: 'start' | 'prepare' | 'continue'): Promise<void> {
    const problem = this.current;
    if (problem === null || this.calling || this.running || this.analysing) {
      return;
    }
    // Solve starts afresh: a saved stacking search would be lost, so ask (cb_BtnStart)
    const saved = this.savedSearch;
    if (kind === 'start' && this.stacking && saved !== null) {
      const k = await askChoice(
        'savedSearch',
        'A saved search',
        `This problem has ${saved.description}.\n\nContinue it, or start over and discard it?`,
        [
          { key: 'cancel', label: 'Cancel' },
          {
            key: 'continue',
            label: 'Continue',
            kind: 'pri',
            default: true,
            tip: 'Carry on the saved search',
          },
          {
            key: 'over',
            label: 'Start Over',
            kind: 'danger',
            tip: 'Throw the saved search away and solve from the start',
          },
        ],
      );
      if (k === 'continue') {
        await this.run('continue');
        return;
      }
      if (k !== 'over' || problem !== this.current || this.busyNow()) {
        return;
      }
    }
    const options = this.optionsNow();
    const solver: Solver = options.solver ?? 'classic';
    const method =
      kind === 'start' ? 'solve.start' : kind === 'prepare' ? 'solve.prepare' : 'solve.continue';
    this.calling = true;
    if (kind !== 'continue') {
      // the run drops the solutions: nothing of them is read (or played) from now on
      this.sel = -1;
      this.selId = null;
      this.player.show(null, null);
    }
    this.runCard.render();
    try {
      await this.edits.track(
        this.store.engine.call(method, { problem, options }).then((r) => {
          // before the run's events (decision 0012: the reply first)
          this.solves.launch(problem, solver);
          this.launched.set(problem, options);
          if (this.family !== 'brick') {
            void this.tellNote(problem);
          }
          return r;
        }),
      );
    } catch (e) {
      reportError(e, kind === 'start' ? 'Solve' : kind === 'prepare' ? 'Prepare' : 'Continue');
    } finally {
      this.calling = false;
      this.render();
    }
  }

  // read through a call: the checker keeps the flags narrowed across an await
  private busyNow(): boolean {
    return this.calling || this.running || this.analysing;
  }

  /** Pauses the run (refused with no run under way: the button is off then). */
  async pause(): Promise<void> {
    const problem = this.current;
    const r = this.record;
    if (problem === null || r?.phase !== 'active' || this.calling) {
      return;
    }
    this.calling = true;
    this.runCard.render();
    try {
      await this.edits.track(this.store.engine.call('solve.pause', { problem }));
    } catch (e) {
      reportError(e, 'Pause');
    } finally {
      this.calling = false;
      this.render();
    }
  }

  /**
   * Whether the solve shown can be aborted: a run under way, paused in
   * memory, or a stacking problem's search saved on disk (abortable()).
   */
  get abortable(): boolean {
    const r = this.record;
    if (this.current === null || this.calling) {
      return false;
    }
    const s = this.stateOf(this.current);
    if (this.savedSearch !== null && (r === null || r.phase === 'none') && !isActive(s)) {
      return true;
    }
    if (r === null) {
      return false;
    }
    return (r.phase === 'active' && isActive(s)) || (r.phase === 'none' && s === 'paused');
  }

  /** Clear (the original's Abort): the hard stop, asked first (it cannot be undone, as in the original). */
  abort(): void {
    const problem = this.current;
    if (problem === null || !this.abortable) {
      return;
    }
    askConfirm(
      'Clear the solve?',
      'Stop the solve of this problem if it is running and throw away all of it: its solutions, the paused state and any saved search. This cannot be undone.',
      'Clear',
      () => {
        void this.doAbort(problem);
      },
    );
  }

  private async doAbort(problem: string): Promise<void> {
    if (!this.abortable || problem !== this.current) {
      return;
    }
    this.calling = true;
    this.player.holdOpen();
    this.runCard.render();
    try {
      const r = await this.edits.track(this.store.engine.call('solve.abort', { problem }));
      await this.whenShown(r.revision);
      flash('Solve cleared');
    } catch (e) {
      reportError(e, 'Clear');
    } finally {
      this.calling = false;
      this.render();
    }
  }

  // ---- removing (each asks; undoable) ----

  /** The Remove menu and Del: solution.delete after asking. */
  remove(which: RemoveWhich): void {
    const problem = this.current;
    const row = this.selected;
    const total = this.pager?.total ?? 0;
    if (problem === null || total === 0) {
      return;
    }
    if (this.edits.refuse(this.running || this.analysing)) {
      return;
    }
    const needsRow = which === 'one' || which === 'before' || which === 'after';
    if (needsRow && row === null) {
      return;
    }
    const n =
      which === 'one'
        ? 1
        : which === 'before'
          ? (row?.index ?? 0)
          : which === 'after'
            ? total - (row?.index ?? 0) - 1
            : which === 'all'
              ? total
              : null;
    if (n === 0) {
      flash('Nothing to delete');
      return;
    }
    const text = {
      one: 'This removes the selected solution (and its disassembly).',
      before: 'This removes every solution saved before the selected one.',
      after: 'This removes every solution saved after the selected one.',
      withoutDisassembly: 'This removes every assembly that has no stored disassembly.',
      all: 'This removes every solution of this problem.',
    }[which];
    const title =
      n === null
        ? 'Delete the solutions without a disassembly?'
        : `Delete ${String(n)} solution${n === 1 ? '' : 's'}?`;
    askConfirm(title, `${text} Undo (Ctrl+Z) brings them back.`, 'Delete', () => {
      void this.doRemove(problem, which, row?.id ?? null);
    });
  }

  private async doRemove(
    problem: string,
    which: RemoveWhich,
    solution: string | null,
  ): Promise<void> {
    const before = this.problem?.saved ?? 0;
    this.player.holdOpen();
    const r = await this.edits.mutate(
      'Delete',
      () =>
        this.store.engine.call('solution.delete', {
          problem,
          which,
          ...(solution !== null && (which === 'one' || which === 'before' || which === 'after')
            ? { solution }
            : {}),
        }),
      (rev) => this.whenShown(rev),
    );
    if (r !== null) {
      const removed = before - (this.problem?.saved ?? 0);
      flash(removed > 0 ? `${String(removed)} removed` : 'Nothing was removed');
    }
  }

  /** Drops the moves of the selected solution (`one`) or of all, keeping their levels. */
  async deleteDisassembly(one: boolean): Promise<void> {
    const problem = this.current;
    const row = this.selected;
    if (problem === null || (one && row === null)) {
      return;
    }
    if (this.edits.refuse(this.running || this.analysing)) {
      return;
    }
    const r = await this.edits.mutate(
      'Delete disassembly',
      () =>
        this.store.engine.call('solution.deleteDisassembly', {
          problem,
          ...(one && row !== null ? { solution: row.id } : {}),
        }),
      (rev) => this.whenShown(rev),
    );
    if (r !== null) {
      flash(
        one && row !== null
          ? `Disassembly of solution ${String(row.assembly + 1)} deleted (assembly kept)`
          : 'All disassemblies deleted (assemblies kept)',
      );
    }
  }

  // ---- analyse (solution.analyse; its results on analyseEnded and its document.changed) ----

  /** Takes saved assemblies apart: the selected one, all, or those without their moves. */
  async analyse(which: 'one' | 'all' | 'missing'): Promise<void> {
    const problem = this.current;
    const row = this.selected;
    if (problem === null || (which === 'one' && row === null) || this.analysing) {
      return;
    }
    if (this.edits.refuse(this.running)) {
      return;
    }
    const run: AnalysisRun = {
      problem,
      targets: which === 'one' && row !== null ? [row.id] : null,
    };
    try {
      // the assemblies it takes apart, in the stored order (before it starts: the list is frozen then)
      run.targets ??= await this.collectTargets(problem, which === 'missing');
      if (run.targets.length === 0) {
        flash('Nothing to analyse');
        return;
      }
      const r = await this.edits.track(
        this.store.engine.call('solution.analyse', {
          problem,
          which,
          ...(which === 'one' && row !== null ? { solution: row.id } : {}),
          solver: this.options.solver,
          rotations: this.options.checkRotations,
        }),
      );
      this.analysisRun = run;
      this.solves.analysing(problem, r.total);
      flash(`Analysing ${String(r.total)} assembl${r.total === 1 ? 'y' : 'ies'}…`);
    } catch (e) {
      reportError(e, 'Analyse');
    }
  }

  /** Stops the analysis; what it finished is kept. */
  async stopAnalysis(): Promise<void> {
    const problem = this.current;
    if (problem === null || !this.analysing) {
      return;
    }
    try {
      await this.store.engine.call('solution.analyseStop', { problem });
    } catch (e) {
      reportError(e, 'Stop');
    }
  }

  // the ids of the saved solutions in the stored order (without their moves: `missing`)
  private async collectTargets(problem: string, missing: boolean): Promise<string[]> {
    const out: string[] = [];
    for (let offset = 0; ; offset += 10000) {
      const page = await this.store.engine.call('solution.page', {
        problem,
        sortBy: 'list',
        offset,
        limit: 10000,
      });
      for (const r of page.rows) {
        if (!missing || r.disassembly !== 'tree') {
          out.push(r.id);
        }
      }
      if (offset + page.rows.length >= page.total || page.rows.length === 0) {
        return out;
      }
    }
  }

  // an analysis ended: the ones it took and that did not come apart cannot be
  private analysisChanged(problem: string): void {
    const a = this.solves.peek(problem)?.analysis;
    const run = this.analysisRun;
    if (a === null || a === undefined || a.active || run?.problem !== problem) {
      return;
    }
    this.analysisRun = null;
    const done = run.targets?.slice(0, a.done) ?? [];
    flash(
      a.failure !== null
        ? `The analysis failed: ${a.failure}`
        : `${a.stopped ? 'Analysis stopped' : 'Analysis done'}: ${String(a.separated)} of ${String(a.done)} taken apart`,
      5000,
    );
    if (done.length === 0) {
      return;
    }
    // its results are in once its document.changed came: read which have their moves now
    void new Promise<void>((done) => {
      this.changeWaiters.push({ problem, done });
    })
      .then(async () => {
        const have = new Set<string>();
        for (let offset = 0; ; offset += 10000) {
          const page = await this.store.engine.call('solution.page', {
            problem,
            sortBy: 'list',
            offset,
            limit: 10000,
          });
          for (const r of page.rows) {
            if (r.disassembly !== 'none') {
              have.add(r.id);
            }
          }
          if (offset + page.rows.length >= page.total || page.rows.length === 0) {
            break;
          }
        }
        const set = this.inseparables.get(problem) ?? new Set<string>();
        for (const id of done) {
          if (have.has(id)) {
            set.delete(id);
          } else {
            set.add(id);
          }
        }
        this.inseparables.set(problem, set);
        this.solCard.render();
        this.player.restyle();
      })
      .catch((e: unknown) => {
        if (!(e instanceof EngineError && e.errc === 'not_found')) {
          reportError(e, 'The solutions could not be read');
        }
      });
  }

  // ---- keys: the Solver rows of the one table (shell/keys.ts) ----

  /** Where the keyboard focus is: the list, the slider, the seek bar, a piece chip, or elsewhere. */
  private focusOf(target: EventTarget | null): 'list' | 'slider' | 'seek' | 'chip' | null {
    if (tab !== 'solver' || !(target instanceof HTMLElement)) {
      return null;
    }
    if (target.closest('#slvList') !== null) {
      return 'list';
    }
    if (target.id === 'slvSlider') {
      return 'slider';
    }
    if (target.id === 'slvMove') {
      return 'seek';
    }
    if (target.classList.contains('pvc')) {
      return 'chip';
    }
    return null;
  }

  private installKeys(): void {
    const on = (
      action: Parameters<typeof onKey>[0],
      f: (ctx: 'list' | 'slider' | 'seek' | 'chip', e: KeyboardEvent) => boolean,
    ): void => {
      onKey(
        action,
        (e) => {
          const ctx = this.focusOf(e.target);
          return ctx === null ? false : f(ctx, e);
        },
        true,
      );
    };
    on('activate', (ctx, e) => {
      if (ctx === 'chip') {
        const p = Number((e.target as HTMLElement).dataset.p);
        this.player.cycle(p);
        this.solCard.focusChip(p);
        return true;
      }
      this.player.togglePlay();
      return true;
    });
    on('move.back', (ctx, e) => {
      if (ctx === 'chip') {
        this.solCard.moveChipFocus(e.target as HTMLElement, -1);
        return true;
      }
      this.player.seek(-1);
      return true;
    });
    on('move.forward', (ctx, e) => {
      if (ctx === 'chip') {
        this.solCard.moveChipFocus(e.target as HTMLElement, 1);
        return true;
      }
      this.player.seek(1);
      return true;
    });
    on('move.start', (ctx) => {
      if (ctx === 'chip') {
        return false;
      }
      this.player.seek('start');
      return true;
    });
    on('move.end', (ctx) => {
      if (ctx === 'chip') {
        return false;
      }
      this.player.seek('end');
      return true;
    });
    const step =
      (d: number) =>
      (ctx: 'list' | 'slider' | 'seek' | 'chip'): boolean => {
        if (ctx === 'chip') {
          return false;
        }
        this.select(this.sel + d);
        return true;
      };
    on('prev', step(-1));
    on('next', step(1));
    on('page.up', step(-8));
    on('page.down', step(8));
    on('home', (ctx) => {
      if (ctx === 'chip') {
        return false;
      }
      if (ctx === 'seek') {
        this.player.seek('start');
      } else {
        this.select(0);
      }
      return true;
    });
    on('end', (ctx) => {
      if (ctx === 'chip') {
        return false;
      }
      if (ctx === 'seek') {
        this.player.seek('end');
      } else {
        this.select((this.pager?.total ?? 1) - 1);
      }
      return true;
    });
    on('delete', (ctx) => {
      if (ctx !== 'list') {
        return false;
      }
      this.remove('one');
      return true;
    });
    addEscapeStep(ESC_ORDER.playback, () => tab === 'solver' && this.player.pause());
  }
}
