// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace put together (FRONTEND-PLAN.md, stage 3): the
// problems list, the result, the pieces and the library, the fit summary
// in the left card (problems.ts, pieces-card.ts); the selected piece, the
// exclusive groups and the color rules in the right (piece-card.ts); the
// pieces scene in the view (view3d/pieces.ts over layout.ts); the keys
// (+, −, Del), the status bar (the original's line, FitStats.statusLine)
// and the view's tag. The problem shown is read from the engine
// (problem.get, problem.fit, problem.kind) after every change, and the
// page says which revision it shows (`#leftP[data-revision]`); every
// action is a call awaited until that revision is shown. While a solve or
// an analysis runs, decision 0011's frozen parts are disabled and say
// "pause the solve first" before any call: the lists while anything
// solves (Snapshot.solving; a new problem or a copy is still added), a
// problem while it is solved or analysed (ProblemSummary.solve,
// analysing). From docs/mockup/index.html:
// `renderPuzzle`, `pzStatus`, `pzTag`, `pzAdd`, `pzBump`, `pzChanged`,
// `puzClick`, the keydown handler's Puzzle row, and `PZ`. A stacking
// problem (docs/webui-spec.md, section 9.2): its board and stacks
// (stacks-card.ts over stacking.board) in place of the result, the pieces
// and the selected piece, the board with the discs of the start or the
// goal in the view (view3d/board.ts) instead of the pieces scene. A
// sliding problem (section 8.2): Start and goal (slide-card.ts over
// sliding.tray) in the selected piece's place, and the board's START and
// GOAL trays with the copies placed at the top of the view
// (view3d/trays.ts), the pieces scene's copies under them; in the 2D
// focus, where the grids are large, the pieces scene alone. The Display
// menu's Labels add the shapes' names to the pieces scene's labels and to
// the Start and Goal grids.

import type {
  FitStats,
  Methods,
  ProblemDetail,
  ProblemSummary,
} from '../../engine/api.generated.ts';
import { EngineError, isQuietError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import { RevisionWaits, whenShown } from '../state/revision-waits.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, forgetHtml } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { onLayout, tab } from '../shell/layout.ts';
import { onTheme } from '../shell/theme.ts';
import { chipColor, contrast, hex } from '../shell/piece-colors.ts';
import { flash, reportError, updateStatus } from '../shell/status.ts';
import { copyColor, ROD_CHIP } from '../shell/piece-colors.ts';
import { boardCenter } from '../view3d/board.ts';
import type { View3D } from '../view3d/view.ts';
import { rodSetText } from '../state/stacking.ts';
import { rodAt, stackValidity } from '../state/stacking.ts';
import { StacksCard } from './stacks-card.ts';
import {
  inShapeOrder,
  nextCount,
  nextGroup,
  partOf,
  problemChipStyle,
  solveActive,
} from './counts.ts';
import { puzLayout } from './layout.ts';
import { PiecesCard } from './pieces-card.ts';
import { PieceCard } from './piece-card.ts';
import { ProblemsList } from './problems.ts';
import { SlideCard } from './slide-card.ts';

/** How much larger the pieces are drawn in their places than the mockup's. */
const PIECE_GROW = 1.15;

export type ProblemKind = Methods['problem.kind']['result'];

/** A stacking problem's board (stacking.board). */
export type Board = Methods['stacking.board']['result'];

export class PuzzleWorkspace {
  readonly store: Store;
  readonly edits: Edits;
  /** The problem shown, or null when the puzzle has none. */
  current: string | null = null;
  /** The problem shown as the engine last gave it, and its fit and kind. */
  detail: ProblemDetail | null = null;
  fit: FitStats | null = null;
  kind: ProblemKind | null = null;
  /** The piece selected (a shape), or null. */
  sel: string | null = null;
  /** Whether the Groups section is open (collapsed at first). */
  groupsOpen = false;
  /** Whether "All pieces" is open. */
  libOpen = true;
  /** The snapshot revision the cards show, or -1 while loading. */
  shown = -1;
  /** A stacking problem: the stacking edited (Start or Goal), the board as the engine has it. */
  goal = false;
  board: Board | null = null;
  /** The stacking the board was read for (Start or Goal): until it is the one chosen, it is not shown. */
  boardGoal = false;
  /** The rod slider's place, and the row chosen in Stack Order (the top first). */
  rodPlace = 0;
  stackRow = 0;
  /** The rod set chosen in the board's list (the problem's own at first). */
  boardSel: string | null = null;
  private readonly stacks: StacksCard;
  private readonly slide: SlideCard;
  private rodLoads = 0;
  private readonly view3d: View3D;
  private readonly problems: ProblemsList;
  private readonly pieces: PiecesCard;
  private readonly card: PieceCard;
  /** Pieces whose count is shown as a range though both ends are equal: problem:shape. */
  private readonly ranges = new Set<string>();
  private readonly kinds = new Map<string, Promise<ProblemKind>>();
  private generation = -1;
  private loads = 0;
  private readonly waits = new RevisionWaits();
  /**
   * The document changed while the tab was out of sight: nothing was read
   * or drawn for it (cleanup item 10.5); the tab reads it when shown.
   */
  private stale = false;
  /** The grid the cards were last drawn for: another one is drawn at once, shown or not. */
  private drawnGrid: string | null = null;

  constructor(store: Store, view: View3D, edits: Edits) {
    this.store = store;
    this.view3d = view;
    this.edits = edits;
    this.problems = new ProblemsList(this);
    this.pieces = new PiecesCard(this);
    this.card = new PieceCard(this);
    this.stacks = new StacksCard(this);
    this.slide = new SlideCard(this);
    view.puzzle = {
      pick: (shape) => {
        this.select(shape);
        byId('pzUsed').querySelector(`[data-u="${shape}"]`)?.scrollIntoView({ block: 'nearest' });
      },
      tag: () => this.tag(),
      empty: () => this.emptyText(),
    };
    store.subscribe((c) => {
      if (c.kind === 'document') {
        this.documentChanged();
      } else if (c.kind === 'status' && !store.running) {
        this.shown = -1;
      }
    });
    onLayout(() => {
      if (tab !== 'puzzle') {
        return;
      }
      if (this.stale) {
        // what the cards show is older than the document: read it now
        this.stale = false;
        byId('leftP').dataset.revision = '';
        this.load();
        this.render();
      } else {
        this.renderScene();
      }
    });
    // the Display menu's Labels: the pieces scene's names, the grids'
    let labels = this.labelsKey();
    view.onDisplay(() => {
      const k = this.labelsKey();
      if (k !== labels) {
        labels = k;
        this.render();
      }
    });
    // a board's chip follows the theme's board color
    onTheme(() => {
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

  /** Whether the problem shown cannot be edited now (decision 0011): solved or analysed. */
  get problemFrozen(): boolean {
    const p = this.problem;
    return p !== null && (solveActive(p.solve) || p.analysing);
  }

  /** Whether the puzzle is a stacking one. */
  get stacking(): boolean {
    return this.store.snapshot?.grid === 'stacking';
  }

  /** The stack map's rod the slider chooses. */
  get rod(): number {
    return rodAt(this.stacks.places, this.rodPlace);
  }

  /** Start or Goal (cb_StackMode): the board read again for it. */
  setGoal(goal: boolean): void {
    this.goal = goal;
    this.stackRow = 0;
    this.load();
    this.render();
  }

  /** Another rod on the slider (cb_StackRod). */
  setRodPlace(place: number): void {
    this.rodPlace = Math.max(0, Math.round(place));
    this.stackRow = 0;
    this.render();
  }

  /** Runs a mutating call until the cards show its revision; a failure is thrown, not said. */
  mutateQuiet<T extends { revision: number }>(call: () => Promise<T>): Promise<T> {
    return this.edits.track(
      call().then(async (r) => {
        await this.whenShown(r.revision);
        return r;
      }),
    );
  }

  /** Whether a piece's count is shown as a range. */
  isRange(shape: string): boolean {
    const part = partOf(this.detail, shape);
    return (
      (part !== undefined && part.min !== part.max) || this.ranges.has(`${this.current}:${shape}`)
    );
  }

  /** The label of a shape (S1...), or its id. */
  labelOf(shape: string): string {
    return this.store.snapshot?.shapes.find((s) => s.id === shape)?.label ?? shape;
  }

  /** A problem's chip style, by its place in the list. */
  private probChip(p: ProblemSummary): string {
    return problemChipStyle(this.store.snapshot?.problems.findIndex((q) => q.id === p.id) ?? 0);
  }

  /** A shape's chip colors. */
  chip(shape: string): { bg: string; fg: string } {
    const i = this.store.indexOf(shape);
    const c = chipColor(this.store.snapshot?.shapes[i], i);
    return { bg: hex(c), fg: contrast(c) };
  }

  /** Shows another problem. */
  selectProblem(id: string): void {
    if (id === this.current) {
      return;
    }
    this.current = id;
    this.detail = null;
    this.fit = null;
    this.kind = null;
    this.board = null;
    this.boardSel = null;
    this.stackRow = 0;
    // nothing of the new problem is shown until it is read
    byId('leftP').dataset.revision = '';
    this.load();
    this.render();
  }

  /** Selects a piece (null: none). */
  select(shape: string | null): void {
    this.sel = shape;
    this.render();
  }

  /** Draws the cards, the scene, the tag and the status. */
  render(): void {
    this.drawnGrid = this.store.snapshot?.grid ?? null;
    const stage = byId('stage');
    const frozen = this.problemFrozen;
    stage.classList.toggle('pzfrozen', frozen);
    stage.dataset.pzfrozen = frozen ? '1' : '0';
    byId('pzRightTitle').textContent = this.stacking
      ? 'Stacks'
      : this.store.snapshot?.grid === 'sliding'
        ? 'Start and goal'
        : 'Selected piece';
    this.problems.render();
    this.pieces.render();
    this.card.render();
    this.stacks.render();
    this.slide.render();
    this.renderScene();
    updateStatus();
  }

  /** The Display menu's Labels: the shapes' names in the pieces scene, on the Start and Goal grids. */
  get labels(): { pieces: boolean; grids: boolean } {
    const d = this.view3d.display;
    return { pieces: d.pieceNames, grids: d.gridNames };
  }

  private labelsKey(): string {
    const l = this.labels;
    return `${String(l.pieces)}:${String(l.grids)}`;
  }

  /** The board of a sliding problem was read: the view's trays and the status follow. */
  trayRead(): void {
    this.renderScene();
    updateStatus();
  }

  private renderScene(): void {
    const d = this.detail;
    const snap = this.store.snapshot;
    if (snap?.grid === 'stacking') {
      this.view3d.setPieces(null);
      this.renderBoard();
      this.view3d.renderLabels();
      return;
    }
    // a sliding board: its START and GOAL trays at the top of the view, the
    // pieces scene's copies under them (the board is the result, drawn as
    // the trays; a selected piece is boxed among its copies); in the 2D
    // focus the grids show the trays, and the view the pieces alone
    const trays = this.shown < 0 ? null : this.slide.trayModel();
    const focus = byId('stage').classList.contains('f2d');
    this.view3d.setTray(focus ? null : trays);
    if (d === null || snap === null || this.shown < 0) {
      this.view3d.setPieces(null);
    } else {
      const { items, hidden } =
        trays === null
          ? puzLayout(d.result, this.sel, inShapeOrder(d.parts, snap.shapes))
          : puzLayout(
              null,
              null,
              inShapeOrder(d.parts, snap.shapes).filter((p) => p.shape !== trays.tray),
            );
      const named = this.labels.pieces;
      this.view3d.setPieces({
        key: `${String(this.store.generation)}:${d.id}:${String(this.shown)}:${this.sel ?? ''}${trays === null ? '' : focus ? ':focus' : ':trays'}${named ? ':named' : ''}`,
        items,
        hidden,
        selected: this.sel,
        names: new Map(
          snap.shapes.map((s) => [
            s.id,
            named && s.name !== '' ? `${s.label} – ${s.name}` : s.label,
          ]),
        ),
        // the pieces drawn larger, the gaps between them smaller
        grow: PIECE_GROW,
      });
    }
    this.view3d.renderLabels();
  }

  // a stacking problem's board in the view: its start's or goal's discs
  // (showStacking), or another rod set chosen in the list alone (cb_ProblemRod)
  private renderBoard(): void {
    const d = this.detail;
    // the board read for the other stacking is not drawn as this one
    const b = this.boardGoal === this.goal ? this.board : null;
    const store = this.store;
    const snap = store.snapshot;
    const n = ++this.rodLoads;
    if (d === null || snap === null || this.shown < 0) {
      this.view3d.setBoard(null, 'puzzle');
      return;
    }
    const other = this.boardSel !== null && this.boardSel !== d.rodSet ? this.boardSel : null;
    if (other !== null) {
      const gen = store.generation;
      const set = snap.rodSets.find((r) => r.id === other);
      store.engine
        .call('stacking.rodSetLayout', { rodSet: other }, { coalesce: 'puzzle:rodSetLayout' })
        .then(
          (layout) => {
            if (n === this.rodLoads && gen === store.generation) {
              this.view3d.setBoard(
                {
                  key: `rods:${other}:${String(this.shown)}`,
                  layout,
                  center: boardCenter(layout),
                  discs: [],
                  ...(set === undefined ? {} : { label: rodSetText(set) }),
                },
                'puzzle',
              );
            }
          },
          (e: unknown) => {
            if (n === this.rodLoads && !(e instanceof EngineError && e.errc === 'superseded')) {
              reportError(e, 'The rod set could not be drawn');
            }
          },
        );
      return;
    }
    const layout = b?.layout ?? null;
    const center = b?.center ?? null;
    if (layout === null || center === null) {
      this.view3d.setBoard(null, 'puzzle');
      return;
    }
    const discs = layout.discs.flatMap((x) => {
      if (x.at === null) {
        return [];
      }
      const i = store.indexOf(x.shape);
      return [
        {
          shape: x.shape,
          size: x.size,
          at: x.at,
          color: copyColor(snap.shapes[i], i, x.instance, snap.colors),
        },
      ];
    });
    this.view3d.setBoard(
      {
        // what is drawn, not when: a board read for Start or Goal may come at the same revision
        key: `board:${d.id}:${String(layout.panex)}:${String(layout.pocket)}:${String(layout.maxSize)}:${layout.rodX.join(',')}:${layout.rodHeights.join(',')}:${discs.map((x) => `${x.shape}@${x.at.join(',')}:${hex(x.color)}`).join(' ')}`,
        layout,
        center,
        discs,
      },
      'puzzle',
    );
  }

  // the mockup's pzTag: the problem's chip and name, and its result
  private tag(): string {
    const p = this.problem;
    if (p === null) {
      return '';
    }
    if (this.stacking) {
      // the board (resultviewer.cpp) and the stacking edited
      const set = this.store.snapshot?.rodSets.find((r) => r.id === p.rodSet);
      const res =
        set === undefined
          ? '<span class="mute">· No Start/Goal Rods</span>'
          : `<span class="mute">· Result</span><span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(set.label)}</span><span class="mute">· ${this.goal ? 'Goal' : 'Start'}</span>`;
      return `<span class="chip" style="${this.probChip(p)}">${esc(p.label)}</span><span class="nm">${esc(p.name) || 'Problem'}</span>${res}`;
    }
    const r = p.result;
    const res =
      r === null
        ? '<span class="mute">· No result</span>'
        : `<span class="mute">· Result</span><span class="chip" style="background:${this.chip(r).bg};color:${this.chip(r).fg}">${esc(this.labelOf(r))}</span>`;
    return `<span class="chip" style="${this.probChip(p)}">${esc(p.label)}</span><span class="nm">${esc(p.name) || 'Problem'}</span>${res}`;
  }

  private emptyText(): string {
    const snap = this.store.snapshot;
    if (snap === null) {
      return '';
    }
    if (snap.problems.length === 0) {
      return 'No problems — add one with + New';
    }
    const d = this.detail;
    if (this.stacking) {
      return d !== null && d.rodSet === null ? 'Choose a rod set with Set Start/Goal Rods' : '';
    }
    if (d !== null && d.result === null && d.parts.length === 0) {
      return 'Choose a result and add pieces';
    }
    return '';
  }

  /** The status bar in the Puzzle workspace (HTML): the original's line, FitStats.statusLine. */
  statusLine(): string {
    const p = this.problem;
    if (this.store.snapshot?.problems.length === 0) {
      return 'No problems — add one with <b>+ New</b>';
    }
    if (p === null) {
      return '';
    }
    if (this.problemFrozen) {
      return `Problem <b>${esc(p.label)}</b> is being solved — <b>pause the solve first</b> to edit it`;
    }
    if (this.stacking) {
      // the board and the validity bar's words (refreshStackValid)
      const set = this.store.snapshot?.rodSets.find((r) => r.id === p.rodSet);
      if (set === undefined) {
        return `Problem <b>${esc(p.label)}</b> has no board — choose a rod set with <b>Set Start/Goal Rods</b>`;
      }
      const v = stackValidity(this.board?.error ?? null);
      return `Problem <b>${esc(p.label)}</b> on ${esc(rodSetText(set))} · ${esc(v.text)}`;
    }
    const f = this.fit;
    if (f === null) {
      return '';
    }
    if (f.verdict === 'noResult') {
      return this.store.snapshot?.grid === 'sliding'
        ? `Problem <b>${esc(p.label)}</b> has no board — choose one with <b>Change…</b>`
        : `Problem <b>${esc(p.label)}</b> has no result — choose one with <b>Change…</b>`;
    }
    return esc(f.statusLine);
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
      this.sel = null;
      this.ranges.clear();
      this.kinds.clear();
      this.detail = null;
      this.fit = null;
      this.kind = null;
      this.board = null;
      this.boardSel = null;
      this.goal = false;
      this.rodPlace = 0;
      this.stackRow = 0;
    }
    if (snap !== null) {
      const ids = snap.problems.map((p) => p.id);
      if (this.current === null || !ids.includes(this.current)) {
        // the problem went (deleted, or another document): the one now in its place
        const at = this.problems.lastIndex;
        this.current = snap.problems[Math.min(at, ids.length - 1)]?.id ?? null;
        this.detail = null;
        this.fit = null;
        this.kind = null;
        this.board = null;
        this.boardSel = null;
      }
      if (this.sel !== null && !snap.shapes.some((s) => s.id === this.sel)) {
        this.sel = null;
      }
    }
    // out of sight, an edit of the same document is read when the tab is
    // shown; another document, or another grid, is read and drawn at once
    if (
      tab !== 'puzzle' &&
      snap !== null &&
      store.running &&
      this.shown >= 0 &&
      snap.grid === this.drawnGrid
    ) {
      this.stale = true;
      this.keepRevision();
      return;
    }
    this.stale = false;
    this.load();
    this.render();
  }

  /**
   * Out of sight: the page says the cards are at the snapshot's revision
   * (`#leftP[data-revision]`, which the e2e tests wait on), and the edits
   * waiting for it go on: nothing is drawn for them until the tab is shown.
   */
  private keepRevision(): void {
    const snap = this.store.snapshot;
    if (snap === null) {
      return;
    }
    byId('leftP').dataset.problem = this.current ?? '';
    byId('leftP').dataset.revision = String(snap.revision);
    this.waits.release(snap.revision, this.store.generation);
  }

  /**
   * Reads the problem shown again (problem.get, problem.fit; its kind
   * once), for the snapshot's revision; the newest load wins.
   */
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
      this.loaded(n, revision, generation, null, null, null);
      return;
    }
    const e = store.engine;
    // a stacking problem has no fit (no result): its board instead
    const stacking = snap.grid === 'stacking';
    const goal = this.goal;
    Promise.all([
      e.call('problem.get', { problem: id }, { coalesce: 'puzzle:problem.get' }),
      stacking
        ? Promise.resolve(null)
        : e.call('problem.fit', { problem: id }, { coalesce: 'puzzle:problem.fit' }),
      this.kindOf(id),
      stacking
        ? e.call('stacking.board', { problem: id, goal }, { coalesce: 'puzzle:stacking.board' })
        : Promise.resolve(null),
    ]).then(
      ([d, f, k, b]) => {
        if (id === this.current) {
          this.board = b;
          this.boardGoal = goal;
          this.loaded(n, revision, generation, d, f, k);
        }
      },
      (err: unknown) => {
        // a newer load answers for the edits waiting (it was asked for a later revision)
        if (n !== this.loads) {
          return;
        }
        // the problem went meanwhile, or the engine stopped: no news; any
        // other failure is said; either way the edits waiting for this
        // revision go on, rather than staying busy (a restarted engine
        // never comes back to it)
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
    f: FitStats | null,
    k: ProblemKind | null,
  ): void {
    if (n !== this.loads) {
      return;
    }
    this.detail = d;
    this.fit = f;
    this.kind = k;
    this.shown = revision;
    // the board's list starts on the problem's own rod set (selectProblemRod)
    const sets = this.store.snapshot?.rodSets ?? [];
    if (this.boardSel === null || !sets.some((r) => r.id === this.boardSel)) {
      this.boardSel = d?.rodSet ?? sets[0]?.id ?? null;
    }
    byId('leftP').dataset.problem = this.current ?? '';
    byId('leftP').dataset.revision = String(revision);
    this.render();
    this.waits.release(revision, generation);
    // a load that was under way when the tab was left: the page stays at the snapshot's revision
    if (this.stale) {
      this.keepRevision();
    }
  }

  /** Resolves when the cards show a revision at least this one (or another document came). */
  private whenShown(revision: number): Promise<void> {
    // out of sight, nothing is drawn for an edit: it waits for the snapshot only
    return whenShown(
      this.store,
      this.waits,
      () => (this.stale ? Number.POSITIVE_INFINITY : this.shown),
      revision,
    );
  }

  // ---- actions (each a call awaited until the cards show its revision) ----

  /** Runs a mutating call; null when it failed (said). */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null> {
    return this.edits.mutate(doing, call, (r) => this.whenShown(r));
  }

  /** Refuses an edit of the problem shown while it is solved; says so. */
  refuse(): boolean {
    return this.edits.refuse(this.problemFrozen);
  }

  /** Refuses a change of the problems list while anything is solved; says so. */
  refuseList(): boolean {
    return this.edits.refuseList();
  }

  private get engine(): Store['engine'] {
    return this.store.engine;
  }

  /** One more copy (the original's: minimum and maximum), and the piece selected. */
  async add(shape: string): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    this.sel = shape;
    this.render();
    if (this.stacking) {
      // a disc goes on top of the rod chosen (the Problems tab's +1)
      await this.stacks.add(shape);
      return;
    }
    await this.mutate('Add piece', () => this.engine.call('problem.addPiece', { problem, shape }));
  }

  /** One fewer copy (at 0 the piece leaves the puzzle). */
  async fewer(shape: string): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse() || partOf(this.detail, shape) === undefined) {
      return;
    }
    this.sel = shape;
    await this.mutate('Remove piece', () =>
      this.engine.call('problem.removePiece', { problem, shape }),
    );
  }

  /** Takes a piece out of the puzzle. */
  async remove(shape: string, say = false): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse() || partOf(this.detail, shape) === undefined) {
      return;
    }
    const r = await this.mutate('Remove piece', () =>
      this.engine.call('problem.setPieceCount', { problem, shape, min: 0, max: 0 }),
    );
    if (r !== null && say) {
      flash('Removed from the puzzle');
    }
  }

  /** A count field or stepper: `value` for one end (puzzle/counts.ts's nextCount). */
  async setCount(shape: string, field: 'min' | 'max', value: number): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    const part = partOf(this.detail, shape);
    const { min, max } = nextCount(part, field, value, this.isRange(shape));
    if (min === (part?.min ?? 0) && max === (part?.max ?? 0)) {
      // nothing to change: the fields show the counts again
      forgetHtml(byId('pzUsed'));
      this.render();
      return;
    }
    this.sel = shape;
    await this.mutate('Piece count', () =>
      this.engine.call('problem.setPieceCount', { problem, shape, min, max }),
    );
  }

  /** Shows a piece's count as a range or a fixed count (a fixed count takes the maximum). */
  async setRange(shape: string, on: boolean): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    const k = `${problem}:${shape}`;
    const part = partOf(this.detail, shape);
    if (on) {
      this.ranges.add(k);
      this.render();
      return;
    }
    this.ranges.delete(k);
    if (part !== undefined && part.min !== part.max) {
      await this.mutate('Piece count', () =>
        this.engine.call('problem.setPieceCount', {
          problem,
          shape,
          min: part.max,
          max: part.max,
        }),
      );
    } else {
      this.render();
    }
  }

  /** Makes a shape the result. */
  async setResult(shape: string): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    if (this.sel === shape) {
      this.sel = null;
    }
    await this.mutate('Set result', () =>
      this.engine.call('problem.setResult', { problem, shape }),
    );
  }

  /** Leaves the problem without a result (the Use as result menu's Clear Result). */
  async clearResult(): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Clear result', () => this.engine.call('problem.clearResult', { problem }));
  }

  /** Puts all copies of a piece in a group (0: none; 'new': a new group). */
  async setGroup(shape: string, group: number | 'new'): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse() || partOf(this.detail, shape) === undefined) {
      return;
    }
    const g = group === 'new' ? nextGroup(this.detail) : group;
    this.sel = shape;
    await this.mutate('Group', () =>
      this.engine.call('problem.setGroup', { problem, shape, group: g }),
    );
  }

  /** A shape's weight one step up or down (never below 0): in the disassembly the heavier group stays in place. */
  async stepWeight(shape: string, delta: number): Promise<void> {
    const s = this.store.snapshot?.shapes.find((x) => x.id === shape);
    if (s === undefined || this.refuse()) {
      return;
    }
    const weight = Math.max(0, s.weight + delta);
    if (weight === s.weight) {
      return;
    }
    this.sel = shape;
    await this.mutate('Weight', () => this.engine.call('shape.setWeight', { shape, weight }));
  }

  /** Puts `count` copies of a piece in a group (the original's groups editor). */
  async setGroupCount(shape: string, group: number, count: number): Promise<void> {
    const problem = this.current;
    const part = partOf(this.detail, shape);
    if (problem === null || part === undefined || group <= 0 || this.refuse()) {
      return;
    }
    const n = Math.max(0, Math.min(part.max, Math.round(count) || 0));
    await this.mutate('Group', () =>
      this.engine.call('problem.setGroupCount', { problem, shape, group, count: n }),
    );
  }

  async deleteGroup(group: number): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Delete group', () =>
      this.engine.call('problem.deleteGroup', { problem, group }),
    );
  }

  async setColorRule(pieceColor: string, resultColor: string, allowed: boolean): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Color rule', () =>
      this.engine.call('problem.setColorRule', { problem, pieceColor, resultColor, allowed }),
    );
  }

  async setColorRules(preset: 'eachOnItself' | 'allowAll' | 'clear'): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Color rules', () =>
      this.engine.call('problem.setColorRules', { problem, preset }),
    );
  }

  async addOneOfEach(): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Add one of each', () =>
      this.engine.call('problem.addOneOfEach', { problem }),
    );
  }

  async setRangeAll(min: number, max: number): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Set range for all pieces', () =>
      this.engine.call('problem.setRangeAll', { problem, min, max }),
    );
  }

  async removeAllPieces(): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Remove all pieces', () =>
      this.engine.call('problem.removeAllPieces', { problem }),
    );
  }

  // ---- keys: + and − (one more, one fewer copy), Del (out of the puzzle) ----

  private installKeys(): void {
    const piece = (): string | null => {
      const s = this.sel;
      return tab === 'puzzle' && s !== null && s !== this.detail?.result ? s : null;
    };
    onKey('piece.more', () => {
      const s = piece();
      if (s === null) {
        return false;
      }
      void this.add(s);
      return true;
    });
    // a stacking problem: − and Del take the disc chosen in Stack Order off
    const offStack = (): boolean => {
      if (tab !== 'puzzle' || !this.stacking) {
        return false;
      }
      if (!this.refuse()) {
        void this.stacks.remove();
      }
      return true;
    };
    onKey('piece.fewer', () => {
      if (offStack()) {
        return true;
      }
      const s = piece();
      if (s === null) {
        return false;
      }
      void this.fewer(s);
      return true;
    });
    onKey('delete', () => {
      if (offStack()) {
        return true;
      }
      const s = piece();
      if (s === null) {
        return false;
      }
      void this.remove(s, true);
      return true;
    });
  }
}
