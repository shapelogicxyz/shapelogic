// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace put together (FRONTEND-PLAN.md, stage 4): the
// problem, what to check, the search options and the run in the left card
// (run-card.ts), the solutions as a list or a slider, the player, the
// sort, the actions and the piece chips in the right (solutions-card.ts),
// the solution in the view (player.ts over view3d/solution.ts), the keys,
// the status bar and the view's tag (their words are run-text.ts', the
// questions around a run run-questions.ts'). The problem shown is read from the
// engine (problem.get; problem.kind once) after every change; its solve
// is the solve record's (state/solves.ts: the events', a run's end its
// revision); its solutions are paged by the engine (pager.ts) and read
// again when its solutionsVersion moves. Every action is a call: a run's
// start waits for its reply (then the run is the record's), a removal or
// a stored option for its revision. What the engine would refuse as
// invalid_state is refused first: the options of a problem being solved,
// a pause with no run under way, an abort once the run has ended (the
// button grays out, as the original's); the frozen document says "pause
// the solve first". A stacking puzzle (docs/webui-spec.md, section 9.3):
// its options and its one solver by the board, Solve and Continue off
// without a board or with invalid stacks, Continue in Solve's place and
// Abort for a saved search (not the original's cb_BtnStart question,
// Continue or Start Over: starting over is Abort, then Solve) (solve.status.savedSearch, the
// original's abortable), the Panex or stacking search on the Activity
// line, the board (stacking.board) for the player. From docs/mockup/index.html:
// `renderSolver`, `svSelect`, `svManage`, `svTag`, `svStatus`, `svKey`
// and the Solver keydown handlers, and `SV`.

import { LIMITS } from '../../engine/api.generated.ts';
import type {
  Methods,
  ProblemDetail,
  ProblemSummary,
  SolutionRow,
  SolveProgress,
  SolveState,
  Solver,
} from '../../engine/api.generated.ts';
import { EngineError, isQuietError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import { RevisionWaits, whenShown } from '../state/revision-waits.ts';
import { isActive } from '../state/solves.ts';
import type { SolveRecord, Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';
import { CACHE_DELETED } from '../shell/cache-events.ts';
import { askConfirm } from '../shell/confirm.ts';
import { tell } from '../shell/dialog.ts';
import { byId } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { onKey } from '../shell/keyboard.ts';
import { onLayout, tab } from '../shell/layout.ts';
import { isTray } from '../shell/piece-colors.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { flash, reportError, updateStatus } from '../shell/status.ts';
import type { View3D } from '../view3d/view.ts';
import { problemChipStyle } from '../puzzle/counts.ts';
import { Explorer } from '../explore/explorer.ts';
import { Autosave } from './autosave.ts';
import { LivePreview } from './live-preview.ts';
import { SolutionPager } from './pager.ts';
import type { SortKey } from './pager.ts';
import { Player } from './player.ts';
import type { SlidePhase } from './performance.ts';
import { RunCard } from './run-card.ts';
import { RunQuestions } from './run-questions.ts';
import { emptyText, statusLine, tagHtml } from './run-text.ts';
import { openPlacements } from '../entities/placements-dialog.ts';
import {
  defaultLimits,
  DEFAULT_OPTIONS,
  defaultsOf,
  familyOf,
  normalize,
  readOptions,
  sameOptions,
  solveOptions,
  stackingSolverOf,
  withOptimizedPaths,
  writeOptions,
} from './settings.ts';
import type { BoxName, Family, Limits, SearchOptions } from './settings.ts';
import type { FileExport } from '../export/exports.ts';
import { SolutionsCard } from './solutions-card.ts';
import { StatsPanel } from './stats-panel.ts';
import { ic } from '../shell/icons.ts';

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
  /** The live preview of a run of the problem shown (under the player and the explorers). */
  readonly preview: LivePreview;
  /** The problem shown, or null when the puzzle has none. */
  current: string | null = null;
  detail: ProblemDetail | null = null;
  kind: ProblemKind | null = null;
  /** The options shown (the problem's stored ones until changed). */
  options: SearchOptions = { ...DEFAULT_OPTIONS };
  /** The original's Display Limit and Keep Each: not stored with a problem. */
  limits: Limits = defaultLimits();
  /** The limits were changed here: the settings' defaults no longer move them. */
  private limitsSet = false;
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
  /** A run call under way (Solve, Continue, Pause, Abort). */
  calling = false;
  /**
   * The problem whose Abort is under way, from the user's yes until the
   * snapshot shows it: the engine answers nothing else until the run has
   * stopped, so its last progress (a save waiting, a search going on) is
   * not what it does.
   */
  private aborting: string | null = null;
  /** The snapshot revision the cards show, or -1 while loading. */
  shown = -1;
  readonly autosave: Autosave;
  /** The step-by-step explorers (the drawer over the view). */
  readonly explorer: Explorer;
  /** The debug statistics panel. */
  readonly stats: StatsPanel;
  /** Solutions ▸ Export to STL: the exports' dialog for a solution (set by the app). */
  exportStl: ((problem: string, row: SolutionRow) => void) | null = null;
  /** Solutions ▸ Export ▸ File ▸ Export's items (set by the app). */
  fileExport: ((k: FileExport) => void) | null = null;
  private readonly view3d: View3D;
  private readonly runCard: RunCard;
  private readonly solCard: SolutionsCard;
  private readonly questions: RunQuestions;
  private readonly kinds = new Map<string, Promise<ProblemKind>>();
  /** Solutions found not to come apart by an analysis, per problem (the client remembers). */
  private readonly inseparables = new Map<string, Set<string>>();
  private analysisRun: AnalysisRun | null = null;
  /** The options of the runs this client started, by problem (autosave carries them on). */
  private readonly launched = new Map<string, ReturnType<typeof solveOptions>>();
  /** Waiting for a document.changed of a problem's solutions. */
  private changeWaiters: { problem: string; done: () => void }[] = [];
  private generation = -1;
  /** The progress that last told the saved search is taken up, and when it came (`pickingUpMs`). */
  private pickSeen: { progress: SolveProgress; at: number } | null = null;
  private pickTimer: number | null = null;
  private loads = 0;
  private readonly waits = new RevisionWaits();
  /**
   * The document changed while the tab was out of sight: nothing was read
   * or drawn for it (cleanup item 10.5); the tab reads it when shown.
   */
  private stale = false;
  /** The problem the engine sends live preview pictures of (solve.preview), in a document's generation. */
  private previewFor: { generation: number; problem: string | null } = {
    generation: -1,
    problem: null,
  };

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
        const center = this.board?.center ?? null;
        return layout === null || center === null ? null : { layout, center };
      },
    });
    this.preview = new LivePreview({
      store,
      view,
      detail: () => this.detail,
      board: () => {
        const layout = this.board?.layout ?? null;
        const center = this.board?.center ?? null;
        return layout === null || center === null ? null : { layout, center };
      },
      changed: () => {
        this.renderTag();
      },
    });
    this.runCard = new RunCard(this);
    this.solCard = new SolutionsCard(this);
    this.questions = new RunQuestions({
      store,
      whenEnded: (p) => solves.whenEnded(p),
      carriesOn: (p) => this.autosave.carriesOn(p),
      family: () => this.family,
      calling: () => this.calling,
      searchFully: (p) => this.searchFully(p),
      carryOn: (p) => this.launchRun(p, 'continue', this.launchOptions(p)),
      abortSolve: (p) => this.abortSolve(p),
    });
    this.autosave = new Autosave(store, solves, (p) => {
      // a sliding run carried on after an autosave tells its note at its end too
      if (this.family !== 'brick') {
        void this.questions.tellNote(p);
      }
    });
    this.explorer = new Explorer({
      store,
      view,
      problem: () => this.current,
      detail: () => this.detail,
      options: () => this.optionsNow(),
      solution: () =>
        this.selId !== null && this.player.row?.id === this.selId ? this.player.row : null,
      assemblable: () =>
        this.detail !== null && this.detail.result !== null && this.detail.pieces > 0,
      changed: () => {
        this.explorer.renderButtons();
        this.renderTag();
      },
    });
    this.stats = new StatsPanel(store, solves, () => this.current);
    // a paused sliding solve's saved search deleted, in this window or in
    // another (read again as this one comes to the front): Continue may
    // have become Solve
    window.addEventListener(CACHE_DELETED, () => {
      this.readSavedState();
    });
    window.addEventListener('focus', () => {
      this.readSavedState();
    });
    for (const c of document.querySelectorAll('.slv-fold .chev')) {
      c.innerHTML = ic('chevD', 14);
    }
    byId('slvAdvHead').addEventListener('click', () => {
      byId('slvAdvSec').classList.toggle('closed');
    });
    byId('slvStats').addEventListener('click', () => {
      this.stats.open();
    });
    view.solver = {
      tag: () => tagHtml(this),
      empty: () => emptyText(this),
      styleChanged: () => {
        this.player.restyle();
        this.explorer.restyle();
        this.preview.restyle();
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
      } else if (c.kind === 'status') {
        if (!store.running) {
          this.shown = -1;
        }
        // an engine started again knows nothing of the preview asked of the last
        this.previewFor = { generation: -1, problem: null };
        this.syncPreview();
      }
    });
    solves.subscribe((c) => {
      if (c.kind === 'cleared') {
        this.preview.clear();
      } else if (c.problem !== null && c.problem === this.current) {
        if (c.kind === 'preview') {
          // a picture only: nothing else to draw
          this.showPreview();
          return;
        }
        if (c.kind === 'state' || c.kind === 'launch' || c.kind === 'ended') {
          this.showPreview();
        }
      }
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
      if (c.kind === 'ended' && c.problem !== null && this.family !== 'brick') {
        void this.questions.askFolder(c.problem);
      }
      if (c.problem === null || c.problem === this.current) {
        this.runCard.render();
        this.solCard.renderActions();
        this.solCard.renderProgress();
        this.explorer.refresh();
        if (tab === 'solver') {
          updateStatus();
          this.renderTag();
        }
        this.followPickingUp();
      } else {
        // another problem's run: its note in the problems list
        this.runCard.renderProblems();
      }
    });
    onLayout(() => {
      if (tab === 'solver') {
        if (this.stale) {
          // what the cards show is older than the document: read it now
          this.stale = false;
          byId('leftS').dataset.revision = '';
          this.load();
        }
        this.render();
        this.showPreview();
        this.showSelected();
      } else {
        // out of sight: the playback stops, and so do the live preview's pictures
        this.player.pause();
        this.syncPreview();
      }
    });
    onPrefs((changed) => {
      if (changed.includes('fade')) {
        this.player.restyle();
      }
      if (changed.includes('livePreview')) {
        this.syncPreview();
        this.showPreview();
        this.runCard.render();
      }
      // the Search Options' defaults (the settings dialog): shown at once for
      // a problem that stored none of its own, and for the limits (which no
      // problem stores) until they are changed here
      const defaults = changed.filter((k) => k === 'solverType' || k.startsWith('opt'));
      if (defaults.length === 0) {
        return;
      }
      if (!this.limitsSet) {
        this.limits = defaultLimits();
      }
      const d = this.detail;
      if (d !== null && !this.editingOptions) {
        this.options = readOptions(d.solverSettings, undefined, this.family, this.canDisassemble);
      }
      this.render();
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
    return this.solves.stateOf(problem, this.store.snapshot);
  }

  /**
   * For how long the run of the problem shown has been taking its saved
   * search up (SolveProgress.slidePickingUp: after a Continue, the
   * journal's too), searching no move yet, so that no move's clock runs
   * and no picture comes; null when it is not. The engine's count, with
   * the time since its progress came: it is drawn again each second
   * (`pickTimer`), so that a pause of minutes is seen to be at work.
   */
  get pickingUpMs(): number | null {
    const id = this.current;
    const p = id === null ? null : (this.solves.peek(id)?.progress ?? null);
    if (id === null || p === null || !p.slidePickingUp || !isActive(this.stateOf(id))) {
      this.pickSeen = null;
      return null;
    }
    if (this.pickSeen?.progress !== p) {
      this.pickSeen = { progress: p, at: performance.now() };
    }
    return p.slidePickingUpMs + (performance.now() - this.pickSeen.at);
  }

  /**
   * The part of a move the run of the problem shown is in while its slide
   * search visits no arrangement (SolveProgress.slidePhase: the merges
   * between moves, an autosave), so that neither the counts nor the live
   * preview move; null while it expands a move, or runs no slide search.
   */
  get slidePhase(): SlidePhase | null {
    const id = this.current;
    if (id === null || !isActive(this.stateOf(id))) {
      return null;
    }
    return this.solves.peek(id)?.progress?.slidePhase ?? null;
  }

  // Counts the taking up of the saved search on, each second, in the view,
  // the empty solutions card and the Performance Analysis.
  private followPickingUp(): void {
    if (this.pickingUpMs === null) {
      if (this.pickTimer !== null) {
        window.clearInterval(this.pickTimer);
        this.pickTimer = null;
      }
      return;
    }
    this.pickTimer ??= window.setInterval(() => {
      this.solCard.renderProgress();
      if (tab === 'solver') {
        this.renderTag();
      }
      this.followPickingUp();
    }, 1000);
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

  /**
   * Whether the puzzle's grid has a take-apart (spheres have none). A
   * Tilted Square puzzle's pieces lie flat in a tray: they are not taken
   * apart either.
   */
  get canDisassemble(): boolean {
    const grid = this.store.snapshot?.grid;
    return (
      !this.tilted && this.store.info?.grids.find((g) => g.grid === grid)?.disassemble !== false
    );
  }

  /** Whether the puzzle is a Tilted Square puzzle (decision 0045). */
  get tilted(): boolean {
    return this.store.snapshot?.tray != null;
  }

  /** A stacking problem's board (null: none read, or another grid). */
  board: Board | null = null;

  /** A stacking problem's saved search, as the last read found it (null: none). */
  savedSearch: SavedSearch | null = null;

  /**
   * A paused sliding solve that Continue would search all over again, as
   * Solve would (solve.status.continueStartsOver: its saved search was
   * deleted from the cache, say), as the last read found it: the run card
   * offers Solve.
   */
  continueStartsOver = false;

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

  /**
   * The moves of a sliding or stacking problem's solution (it has one at
   * most: the fewest moves); null without one, or before its row came.
   */
  moves(): number | null {
    return this.pager?.row(0)?.moves ?? null;
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
    this.continueStartsOver = false;
    this.preview.clear();
    byId('leftS').dataset.revision = '';
    this.newPager(true);
    this.load();
    this.render();
  }

  /** Draws the cards, the tag and the status. */
  render(): void {
    this.syncPreview();
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

  /** Whether the view shows the live preview (nothing chosen, no explorer open, a picture drawn). */
  get previewShown(): boolean {
    return this.view3d.solutionLayer === 'preview';
  }

  // the newest picture of the problem shown's run, while the preview is on
  private showPreview(): void {
    if (tab !== 'solver') {
      return;
    }
    const shot = this.current === null ? null : (this.solves.peek(this.current)?.preview ?? null);
    this.preview.show(prefs.livePreview && shot?.problem === this.current ? shot : null);
  }

  // the engine sends pictures of the problem shown's runs, while the preview is on
  private syncPreview(): void {
    const store = this.store;
    if (!store.running) {
      return;
    }
    // only while the Solver is shown: elsewhere no picture is drawn
    const want = prefs.livePreview && tab === 'solver' ? this.current : null;
    const generation = store.generation;
    const was = this.previewFor.generation === generation ? this.previewFor.problem : null;
    if (this.previewFor.generation === generation && was === want) {
      return;
    }
    this.previewFor = { generation, problem: want };
    // a problem gone, or an engine without the method (an older demo): nothing to tell
    const quiet = (): void => undefined;
    if (was !== null && store.snapshot?.problems.some((q) => q.id === was) === true) {
      store.engine.call('solve.preview', { problem: was, on: false }).catch(quiet);
    }
    if (want !== null) {
      store.engine.call('solve.preview', { problem: want, on: true }).catch(quiet);
    }
  }

  /** The shown problem's chip style, by its place in the list. */
  probChip(id: string | undefined = this.problem?.id): string {
    return problemChipStyle(this.store.snapshot?.problems.findIndex((q) => q.id === id) ?? 0);
  }

  /** The status bar in the Solver workspace (HTML): run-text.ts'. */
  statusLine(): string {
    return statusLine(this);
  }

  /** Whether this problem's Abort is under way (the user said yes, the snapshot does not show it yet). */
  isAborting(problem: string): boolean {
    return this.aborting === problem;
  }

  /**
   * Whether the problem shown can be solved: it has its result (a stacking
   * one its board), and a sliding puzzle's start and goal or a stacking
   * one's stacks are valid (Solve is grayed otherwise, its tooltip saying why).
   */
  get readyToSolve(): boolean {
    const p = this.problem;
    const board = this.stacking ? p?.rodSet != null : p?.result != null;
    return p !== null && board && (this.slideError ?? this.stackError) === null;
  }

  // ---- loading ----

  private documentChanged(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (store.generation !== this.generation) {
      this.generation = store.generation;
      // the old document's edits wait no more; its revisions are not this one's
      this.waits.release(-1, store.generation);
      this.shown = -1;
      this.current = null;
      this.detail = null;
      this.kind = null;
      this.board = null;
      this.savedSearch = null;
      this.continueStartsOver = false;
      this.kinds.clear();
      this.inseparables.clear();
      this.analysisRun = null;
      this.launched.clear();
      this.changeWaiters = [];
      this.player.close();
      this.preview.clear();
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
        this.continueStartsOver = false;
        this.newPager(true);
      }
    }
    // out of sight, an edit of the same document is read when the tab is
    // shown (another document is read at once)
    if (tab !== 'solver' && snap !== null && store.running && this.shown >= 0) {
      this.stale = true;
      this.keepRevision();
      // the menus ask whether a stacking problem's search is saved (Export ▸ Paused Solver State)
      if (snap.grid === 'stacking') {
        this.readSavedState();
      }
      return;
    }
    this.stale = false;
    this.load();
    this.render();
  }

  /**
   * Out of sight: the page says the cards are at the snapshot's revision
   * (`#leftS[data-revision]`, which the e2e tests wait on), and the edits
   * waiting for it go on: nothing is drawn for them until the tab is shown.
   */
  private keepRevision(): void {
    const snap = this.store.snapshot;
    if (snap === null) {
      return;
    }
    const left = byId('leftS');
    left.dataset.problem = this.current ?? '';
    left.dataset.revision = String(snap.revision);
    this.waits.release(snap.revision, this.store.generation);
  }

  /**
   * Reads again only what the cache folder holds of the solve shown
   * (solve.status: savedSearch, continueStartsOver), when its files may
   * have been deleted; the run card is drawn again when that changed.
   */
  private readSavedState(): void {
    const id = this.current;
    const grid = this.store.snapshot?.grid;
    if (id === null || !this.store.running || (grid !== 'sliding' && grid !== 'stacking')) {
      return;
    }
    this.store.engine
      .call('solve.status', { problem: id }, { coalesce: 'solver:saved-state' })
      .then(
        (st) => {
          if (id !== this.current) {
            return;
          }
          const changed =
            st.continueStartsOver !== this.continueStartsOver ||
            JSON.stringify(st.savedSearch) !== JSON.stringify(this.savedSearch);
          this.savedSearch = st.savedSearch;
          this.continueStartsOver = st.continueStartsOver;
          if (changed) {
            this.render();
          }
        },
        (err: unknown) => {
          if (!isQuietError(err)) {
            reportError(err, 'The solve’s state could not be read');
          }
        },
      );
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
    const generation = store.generation;
    const id = this.current;
    if (id === null) {
      this.loaded(n, revision, generation, null, null);
      return;
    }
    // a sliding problem's start/goal shape: whether Solve can start (its validity bar)
    const result = snap.problems.find((q) => q.id === id)?.result ?? null;
    const tray =
      snap.grid === 'sliding' && result !== null && isTray(snap.shapes.find((x) => x.id === result))
        ? result
        : null;
    // a stacking problem's board (its validity, its center for the player)
    // and its saved search; whether a sliding problem's Continue would
    // start over
    const stacking = snap.grid === 'stacking';
    const status = stacking || snap.grid === 'sliding';
    Promise.all([
      store.engine.call('problem.get', { problem: id }, { coalesce: 'solver:problem.get' }),
      this.kindOf(id),
      tray === null
        ? Promise.resolve(null)
        : store.engine.call(
            'sliding.tray',
            { tray, problem: id },
            { coalesce: 'solver:sliding.tray' },
          ),
      stacking
        ? store.engine.call(
            'stacking.board',
            { problem: id, goal: false },
            { coalesce: 'solver:stacking.board' },
          )
        : Promise.resolve(null),
      status
        ? store.engine.call('solve.status', { problem: id }, { coalesce: 'solver:solve.status' })
        : Promise.resolve(null),
    ]).then(
      ([d, k, t, b, st]) => {
        // only the newest load of this document writes what it read (cleanup item 6.7)
        if (n !== this.loads || generation !== store.generation) {
          return;
        }
        if (id === this.current) {
          this.slideError = t?.error ?? null;
          // a solution shown before the board came is placed again around its middle
          const first = this.board === null && b !== null;
          this.board = b;
          if (first) {
            this.player.restyle();
          }
          this.savedSearch = st?.savedSearch ?? null;
          this.continueStartsOver = st?.continueStartsOver ?? false;
          this.loaded(n, revision, generation, d, k);
        }
      },
      (err: unknown) => {
        // a newer load answers for the edits waiting (it was asked for a later revision)
        if (n !== this.loads) {
          return;
        }
        // the problem went meanwhile, or the engine stopped: no news; any
        // other failure is said; either way the edits waiting for this
        // revision go on (a restarted engine never comes back to it)
        if (!isQuietError(err)) {
          reportError(err, 'The problem could not be read');
        }
        this.waits.release(revision, generation);
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
    generation: number,
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
    const left = byId('leftS');
    left.dataset.problem = this.current ?? '';
    left.dataset.revision = String(revision);
    if (this.pager === null && this.current !== null) {
      this.newPager(false);
    }
    this.render();
    this.waits.release(revision, generation);
    // a load that was under way when the tab was left: the page stays at the snapshot's revision
    if (this.stale) {
      this.keepRevision();
    }
  }

  /** Resolves when the cards show a revision at least this one (or another document came). */
  whenShown(revision: number): Promise<void> {
    // out of sight, nothing is drawn for an edit: it waits for the snapshot only
    return whenShown(
      this.store,
      this.waits,
      () => (this.stale ? Number.POSITIVE_INFINITY : this.shown),
      revision,
    );
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
    if (this.sliding || this.stacking) {
      // the Moves line reads the solution's row
      this.runCard.render();
    }
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

  /** A check box changed (normalized by the original's rules), or the solver or the sort. */
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
            settings: writeOptions(next, d.solverSettings, this.family, this.tilted),
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

  /** Keep at most / keep every n-th (not stored, as the original's). */
  setLimits(change: Partial<Limits>): void {
    this.limitsSet = true;
    this.limits = {
      keepAtMost: Math.max(0, Math.round(change.keepAtMost ?? this.limits.keepAtMost)),
      keepEach: Math.max(1, Math.round(change.keepEach ?? this.limits.keepEach)),
    };
    this.render();
  }

  /**
   * The options a run of a problem was started with by this page; of a run
   * it did not start (the page was reloaded since), the problem shown's as
   * shown, with optimized paths when the problem keeps a grid: the run was
   * started with them (the question at Solve stores no option). A guess
   * that names another search than a paused solve's is refused by the
   * engine (solve.continue), not searched from the first move.
   */
  launchOptions(problem: string): ReturnType<typeof solveOptions> {
    const started = this.launched.get(problem);
    if (started !== undefined) {
      return started;
    }
    const now = this.optionsNow(problem === this.current ? this.options : defaultsOf(this.family));
    const kept = this.store.snapshot?.problems.find((q) => q.id === problem)?.slideGrid ?? 0;
    return this.sliding && kept > 0 ? withOptimizedPaths(now, true) : now;
  }

  /**
   * A tilted problem whose solve found no assembly: the Placements dialog,
   * counting the problem's assemblies on every board the grid can give at
   * once, with the options shown (Try every placement).
   */
  tryPlacements(): void {
    const problem = this.current;
    if (problem === null || !this.tilted || this.calling || this.running || this.analysing) {
      return;
    }
    void openPlacements(
      { store: this.store, mutate: (doing, call) => this.edits.mutate(doing, call) },
      { problem, options: this.optionsNow(), start: true },
    );
  }

  // ---- the run ----

  /** Solve or Continue: the run is the record's once the call is answered. */
  async run(kind: 'start' | 'continue'): Promise<void> {
    const problem = this.current;
    if (problem === null || this.calling || this.running || this.analysing) {
      return;
    }
    let options = this.optionsNow();
    if (this.sliding && options.sliding?.optimizedPaths !== true) {
      if (kind === 'continue') {
        // a run that used optimized paths goes on with them: as this session
        // started it, or (a file opened with the solve paused) by the grid
        // the problem keeps
        const kept = this.store.snapshot?.problems.find((q) => q.id === problem)?.slideGrid ?? 0;
        if (this.launched.get(problem)?.sliding?.optimizedPaths ?? kept > 0) {
          options = withOptimizedPaths(options, true);
        }
      } else {
        const use = await this.questions.askOptimizedPaths(problem);
        if (use === null || problem !== this.current || this.busyNow()) {
          return;
        }
        if (use) {
          options = withOptimizedPaths(options, true);
        }
      }
    }
    if (this.sliding && kind === 'continue') {
      const go = await this.questions.askFromStartAlone(problem, options);
      if (!go || problem !== this.current || this.busyNow()) {
        return;
      }
    }
    if (kind !== 'continue') {
      // the run drops the solutions: nothing of them is read (or played) from now on
      this.sel = -1;
      this.selId = null;
      this.player.show(null, null);
    }
    await this.launchRun(problem, kind, options);
  }

  /** Full Search after optimized paths found no solution: a new run of every stop. */
  private async searchFully(problem: string): Promise<void> {
    if (problem === this.current) {
      this.sel = -1;
      this.selId = null;
      this.player.show(null, null);
    }
    await this.launchRun(problem, 'start', withOptimizedPaths(this.launchOptions(problem), false));
  }

  /** The call of Solve or Continue for a problem, shown or not. */
  private async launchRun(
    problem: string,
    kind: 'start' | 'continue',
    options: ReturnType<typeof solveOptions>,
  ): Promise<void> {
    const solver: Solver = options.solver ?? 'classic';
    const method = kind === 'start' ? 'solve.start' : 'solve.continue';
    this.calling = true;
    this.runCard.render();
    try {
      await this.edits.track(
        this.store.engine.call(method, { problem, options }).then((r) => {
          // before the run's events (decision 0012: the reply first)
          this.solves.launch(problem, solver);
          this.launched.set(problem, options);
          if (this.family !== 'brick') {
            void this.questions.tellNote(problem);
          }
          return r;
        }),
      );
    } catch (e) {
      if (kind === 'continue' && e instanceof EngineError && e.errc === 'invalid_argument') {
        // why the solve is not carried on (the options name another search
        // than the paused one, say): to be read, not flashed for a moment
        tell(`${e.detail.charAt(0).toUpperCase()}${e.detail.slice(1)}.`);
      } else {
        reportError(e, kind === 'start' ? 'Solve' : 'Continue');
      }
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
    // a crash recovery save waiting for its save point must not carry it on
    this.autosave.hold(problem);
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
   * Whether the run card's last button is Abort rather than Reset: the
   * solve shown is running or paused (in memory, left solving by a file,
   * or a stacking search saved), as the original's setAbortLabel judged it.
   */
  get abortMode(): boolean {
    if (this.current === null) {
      return false;
    }
    const r = this.record;
    const s = this.stateOf(this.current);
    return (
      (r !== null && r.phase !== 'none') ||
      isActive(s) ||
      s === 'paused' ||
      this.savedSearch !== null ||
      this.problem?.state === 'solving'
    );
  }

  /**
   * Whether the solve shown can be aborted: a run under way, paused in
   * memory, a problem a file left solving, or a stacking problem's search
   * saved on disk (abortable()).
   */
  get abortable(): boolean {
    const r = this.record;
    if (this.current === null || this.calling) {
      return false;
    }
    const s = this.stateOf(this.current);
    const noRun = (r === null || r.phase === 'none') && !isActive(s);
    if (noRun && (this.savedSearch !== null || this.problem?.state === 'solving')) {
      return true;
    }
    if (r === null) {
      return false;
    }
    return (r.phase === 'active' && isActive(s)) || (r.phase === 'none' && s === 'paused');
  }

  /** Whether the solve shown is over and has results to reset (resettable()). */
  get resettable(): boolean {
    const p = this.problem;
    if (p === null || this.calling || this.running || this.analysing || this.abortMode) {
      return false;
    }
    return p.state !== 'unsolved' || p.saved > 0;
  }

  /** Abort (the original's): the hard stop, asked first (it cannot be undone, as in the original). */
  abort(): void {
    const problem = this.current;
    if (problem === null || !this.abortable) {
      return;
    }
    askConfirm(
      'Abort the solve?',
      'Stop the solve of this problem if it is running and throw away all of it: its solutions, the paused state and any saved search. This cannot be undone. Solve then starts it again from the beginning.',
      'Abort',
      () => {
        void this.doAbort(problem);
      },
    );
  }

  private async doAbort(problem: string): Promise<void> {
    if (!this.abortable || problem !== this.current) {
      return;
    }
    await this.abortSolve(problem);
  }

  /** Reset (the original's, for a solve that is over): its results thrown away as a step, asked first. */
  reset(): void {
    const problem = this.current;
    if (problem === null || !this.resettable) {
      return;
    }
    askConfirm(
      'Reset the problem?',
      'Throw away the solve of this problem: its solutions and what its search went through, so it is unsolved again. Undo (Ctrl+Z) brings them back.',
      'Reset',
      () => {
        void this.doReset(problem);
      },
    );
  }

  private async doReset(problem: string): Promise<void> {
    if (!this.resettable || problem !== this.current) {
      return;
    }
    this.player.holdOpen();
    const r = await this.edits.mutate(
      'Reset',
      () => this.store.engine.call('solve.reset', { problem }),
      (rev) => this.whenShown(rev),
    );
    if (r !== null) {
      flash('Problem reset');
    }
  }

  /** solve.abort for a problem, shown or not. */
  private async abortSolve(problem: string): Promise<void> {
    this.calling = true;
    this.aborting = problem;
    // a crash recovery save waiting for this run must not carry it on or keep it
    this.autosave.abort(problem);
    if (problem === this.current) {
      this.player.holdOpen();
    }
    this.render();
    try {
      const r = await this.edits.track(this.store.engine.call('solve.abort', { problem }));
      await this.whenShown(r.revision);
      flash('Solve aborted');
    } catch (e) {
      reportError(e, 'Abort');
    } finally {
      this.calling = false;
      this.aborting = null;
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
    for (let offset = 0; ; offset += LIMITS.solutions.page) {
      const page = await this.store.engine.call('solution.page', {
        problem,
        sortBy: 'list',
        offset,
        limit: LIMITS.solutions.page,
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
        for (let offset = 0; ; offset += LIMITS.solutions.page) {
          const page = await this.store.engine.call('solution.page', {
            problem,
            sortBy: 'list',
            offset,
            limit: LIMITS.solutions.page,
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
    // Space and Enter alike
    for (const action of ['play', 'activate'] as const) {
      on(action, (ctx, e) => {
        if (ctx === 'chip') {
          const p = Number((e.target as HTMLElement).dataset.p);
          this.player.cycle(p);
          this.solCard.focusChip(p);
          return true;
        }
        this.player.togglePlay();
        return true;
      });
    }
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
