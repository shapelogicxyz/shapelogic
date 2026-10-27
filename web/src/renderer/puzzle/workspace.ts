// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace put together (FRONTEND-PLAN.md, stage 3): the
// problems list, the result, the pieces and the library, the fit summary
// in the left card (problems.ts, pieces-card.ts); the selected piece, the
// exclusive groups and the colour rules in the right (piece-card.ts); the
// pieces scene in the view (view3d/pieces.ts over layout.ts); the keys
// (+, −, Del), the status bar (the original's line, FitStats.statusLine)
// and the view's tag. The problem shown is read from the engine
// (problem.get, problem.fit, problem.kind) after every change, and the
// page says which revision it shows (`#leftP[data-revision]`); every
// action is a call awaited until that revision is shown. While a solve or
// an analysis runs, decision 0011's frozen parts are disabled and say
// "pause the solve first" before any call: the lists while anything
// solves (Snapshot.solving), a problem while it is solved or analysed
// (ProblemSummary.solve, analysing). From docs/mockup/index.html:
// `renderPuzzle`, `pzStatus`, `pzTag`, `pzAdd`, `pzBump`, `pzChanged`,
// `puzClick`, the keydown handler's Puzzle row, and `PZ`. A stacking
// problem (docs/webui-spec.md, section 9.2): its board and stacks
// (stacks-card.ts over stacking.board) in place of the result, the pieces
// and the selected piece, the board with the discs of the start or the
// goal in the view (view3d/board.ts) instead of the pieces scene. A
// sliding problem (section 8.2): Start and goal (slide-card.ts over
// sliding.tray) in the selected piece's place, and the board's START and
// GOAL trays with the copies placed in the view (view3d/trays.ts).

import type {
  FitStats,
  Methods,
  ProblemDetail,
  ProblemSummary,
} from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, forgetHtml } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { onLayout, tab } from '../shell/layout.ts';
import { chipColour, contrast, hex } from '../shell/piece-colours.ts';
import { flash, reportError, updateStatus } from '../shell/status.ts';
import { copyColour, ROD_CHIP } from '../shell/piece-colours.ts';
import { boardCentre } from '../view3d/board.ts';
import type { View3D } from '../view3d/view.ts';
import { rodSetText } from '../state/stacking.ts';
import { rodAt, stackValidity } from '../state/stacking.ts';
import { StacksCard } from './stacks-card.ts';
import { nextCount, nextGroup, partOf, problemChipStyle, solveActive } from './counts.ts';
import { puzLayout } from './layout.ts';
import { PiecesCard } from './pieces-card.ts';
import { PieceCard } from './piece-card.ts';
import { ProblemsList } from './problems.ts';
import { SlideCard } from './slide-card.ts';

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
  private waiting: { revision: number; done: () => void }[] = [];

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
      if (tab === 'puzzle') {
        this.renderScene();
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

  /** A shape's chip colours. */
  /** A problem's chip style, by its place in the list. */
  private probChip(p: ProblemSummary): string {
    return problemChipStyle(this.store.snapshot?.problems.findIndex((q) => q.id === p.id) ?? 0);
  }

  chip(shape: string): { bg: string; fg: string } {
    const i = this.store.indexOf(shape);
    const c = chipColour(this.store.snapshot?.shapes[i], i);
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
    // a sliding board: its START and GOAL trays instead of the pieces scene
    const trays = this.shown < 0 ? null : this.slide.trayModel();
    this.view3d.setTray(trays);
    if (trays !== null) {
      this.view3d.setPieces(null);
      this.view3d.renderLabels();
      return;
    }
    if (d === null || snap === null || this.shown < 0) {
      this.view3d.setPieces(null);
    } else {
      const { items, hidden } = puzLayout(d.result, this.sel, d.parts);
      this.view3d.setPieces({
        key: `${String(this.store.generation)}:${d.id}:${String(this.shown)}:${this.sel ?? ''}`,
        items,
        hidden,
        selected: this.sel,
        names: new Map(snap.shapes.map((s) => [s.id, s.label])),
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
                  centre: boardCentre(layout),
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
    const centre = b?.centre ?? null;
    if (layout === null || centre === null) {
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
          colour: copyColour(snap.shapes[i], i, x.instance, snap.colours),
        },
      ];
    });
    this.view3d.setBoard(
      {
        // what is drawn, not when: a board read for Start or Goal may come at the same revision
        key: `board:${d.id}:${String(layout.panex)}:${String(layout.pocket)}:${String(layout.maxSize)}:${layout.rodX.join(',')}:${layout.rodHeights.join(',')}:${discs.map((x) => `${x.shape}@${x.at.join(',')}:${hex(x.colour)}`).join(' ')}`,
        layout,
        centre,
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
      return `Problem <b>${esc(p.label)}</b> has no result — choose one with <b>Change…</b>`;
    }
    return esc(f.statusLine);
  }

  // ---- loading ----

  private documentChanged(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (store.generation !== this.generation) {
      this.generation = store.generation;
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
    this.load();
    this.render();
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
    const id = this.current;
    if (id === null) {
      this.loaded(n, revision, null, null, null);
      return;
    }
    const e = store.engine;
    // a stacking problem has no fit (no result): its board instead
    const stacking = snap.grid === 'stacking';
    const goal = this.goal;
    Promise.all([
      e.call('problem.get', { problem: id }, { coalesce: 'problem.get' }),
      stacking
        ? Promise.resolve(null)
        : e.call('problem.fit', { problem: id }, { coalesce: 'problem.fit' }),
      this.kindOf(id),
      stacking
        ? e.call('stacking.board', { problem: id, goal }, { coalesce: 'stacking.board' })
        : Promise.resolve(null),
    ]).then(
      ([d, f, k, b]) => {
        if (id === this.current) {
          this.board = b;
          this.boardGoal = goal;
          this.loaded(n, revision, d, f, k);
        }
      },
      (err: unknown) => {
        // a newer load took its place, or the problem went meanwhile
        if (
          err instanceof EngineError &&
          (err.errc === 'superseded' || err.errc === 'not_found' || err.errc === 'disconnected')
        ) {
          return;
        }
        if (n === this.loads) {
          reportError(err, 'The problem could not be read');
          // the edits waiting for it go on (said), rather than staying busy
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
  private whenShown(revision: number): Promise<void> {
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

  /** Moves a piece one place earlier or later (the pieces renumber). */
  async movePart(shape: string, delta: -1 | 1): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Move piece', () =>
      this.engine.call('problem.movePart', { problem, shape, delta }),
    );
  }

  /** Moves a piece to a place among the pieces: one undo step. */
  async moveTo(shape: string, to: number): Promise<void> {
    const problem = this.current;
    const parts = this.detail?.parts ?? [];
    const from = parts.findIndex((p) => p.shape === shape);
    if (problem === null || from < 0 || to === from || to < 0 || to >= parts.length) {
      return;
    }
    if (this.refuse()) {
      return;
    }
    const delta = to > from ? 1 : -1;
    const e = this.engine;
    await this.mutate('Move piece', async () => {
      // one step: the moves in a group, closed whatever failed inside it
      await e.call('edit.begin', { label: 'Move piece' });
      let failed: Error | null = null;
      try {
        for (let i = from; i !== to; i += delta) {
          await e.call('problem.movePart', { problem, shape, delta });
        }
      } catch (x) {
        failed = x instanceof Error ? x : new Error(String(x));
      }
      const r = await e.call('edit.end');
      if (failed !== null) {
        throw failed;
      }
      return r;
    });
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

  async setColourRule(pieceColour: string, resultColour: string, allowed: boolean): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Color rule', () =>
      this.engine.call('problem.setColourRule', { problem, pieceColour, resultColour, allowed }),
    );
  }

  async setColourRules(preset: 'eachOnItself' | 'allowAll' | 'clear'): Promise<void> {
    const problem = this.current;
    if (problem === null || this.refuse()) {
      return;
    }
    await this.mutate('Color rules', () =>
      this.engine.call('problem.setColourRules', { problem, preset }),
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
