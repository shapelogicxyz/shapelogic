// SPDX-License-Identifier: GPL-3.0-or-later
//
// The step-by-step explorers of the Solver workspace (FRONTEND-PLAN.md,
// stage 5): the mockup's drawer at the foot of the 3D view with three
// views, each over an engine handle on a copy of the puzzle:
// - Placements (explore.openAssembly, explore.placements): every placement
//   of each piece in the result, in the assembler's order; the view shows
//   the result as a wire frame and the piece at the placement chosen (the
//   original's placement browser: ↑ ↓ the placement, ← → the piece).
// - Step (explore.openAssembly, explore.step): the assembly search one
//   step (or 10, 100, 1000) at a time; the view shows the pieces placed
//   now in the result, the drawer how far the search is (`finished`).
// - Movements (explore.openMovements and the rest): the positions of the
//   selected solution's take-apart as a tree the user grows: Analyse (every
//   position one move away), Add move (pieces and a direction of the
//   grid's, valid or not), a matching move found by Alt-dragging a piece in
//   the view (the original's Shift/Ctrl-click on a face), Step back, Prune
//   tree; the view shows the position chosen.
// Pieces are drawn as the Solver's assembled view draws them: the shape's
// mesh in the placement's transformation at its cell minus that mesh's
// hotspot (extent.ts's placedAt), in the view's explorer layer over the
// player's solution. Placements and Step need a prepared or paused solve
// (the mockup's rule), Movements a solution with its moves; the handles
// are closed when the drawer closes, the problem shown changes or another
// document comes (whose handles the engine dropped already). From
// docs/mockup/index.html: `renderSolverDrawer` and `#sdrawer`, `svPrepare`
// and `svStep` (the Step button), the Advanced section's buttons
// (`#slvAdv`) and their handlers; the contents from src/gui/
// placementbrowser.cpp and movementbrowser.cpp (the mockup's drawer was
// "layout only").

import type {
  MeshStyle,
  Methods,
  MovementNode,
  Placement,
  ProblemDetail,
  SolutionRow,
  SolveOptions,
} from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, setHtml } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { onKey } from '../shell/keyboard.ts';
import type { KeyAction } from '../shell/keys.ts';
import { tab } from '../shell/layout.ts';
import { pieceSubColour, shapeColour } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import { cellScale, gridExtent, placedAt } from '../view3d/extent.ts';
import { buildMesh } from '../view3d/mesh.ts';
import type { BuiltMesh } from '../view3d/mesh.ts';
import type { PieceMode, ScenePose, SolutionModel } from '../view3d/solution.ts';
import type { View3D } from '../view3d/view.ts';
import { dragDirection, MovementTree } from './tree.ts';

/** Which explorer the drawer shows. */
export type ExplorerMode = 'placements' | 'step' | 'movements';

type V3 = [number, number, number];
type ExplorePlacement = Methods['explore.placements']['result']['placements'][number];

/** A piece of the problem: its number, shape, label (S1, S1.2) and colour. */
export interface ProblemPiece {
  piece: number;
  shape: string;
  label: string;
  colour: Rgb3;
}

/** What the explorer needs of the Solver workspace. */
export interface ExplorerHost {
  readonly store: Store;
  readonly view: View3D;
  /** The problem shown. */
  problem(): string | null;
  detail(): ProblemDetail | null;
  /** The options a run would use now (the assembly explorer's matrix takes them). */
  options(): SolveOptions;
  /** The solution selected. */
  solution(): SolutionRow | null;
  /** Whether the problem's solve is prepared or paused (Placements and Step). */
  assemblyReady(): boolean;
  /** The explorer opened, closed or moved on: the cards, the tag. */
  changed(): void;
}

/** A piece to draw: where, how, in which colour; piece -1 is the result. */
interface DrawItem {
  piece: number;
  shape: string;
  colour: Rgb3;
  mode: PieceMode;
  transformation: number;
  at: V3;
}

/** The pieces of a problem, numbered part by part, labelled and coloured as the original's. */
export function problemPieces(detail: ProblemDetail | null, store: Store): ProblemPiece[] {
  const snap = store.snapshot;
  const out: ProblemPiece[] = [];
  let n = 0;
  for (const part of detail?.parts ?? []) {
    const i = store.indexOf(part.shape);
    const s = snap?.shapes[i];
    const base = shapeColour(s?.colour ?? null, i);
    for (let q = 0; q < part.max; q++) {
      out.push({
        piece: n++,
        shape: part.shape,
        label: (s?.label ?? part.shape) + (part.max > 1 ? `.${String(q + 1)}` : ''),
        colour: pieceSubColour(i, q, base),
      });
    }
  }
  return out;
}

export class Explorer {
  mode: ExplorerMode | null = null;
  private readonly host: ExplorerHost;
  private generation = -1;
  private problem: string | null = null;
  /** Grows with every change of what is shown, so late replies are dropped. */
  private token = 0;
  private pieces: ProblemPiece[] = [];

  // Placements
  private ax: string | null = null;
  private axOpening: Promise<string | null> | null = null;
  private placementsSupported = true;
  private readonly lists = new Map<number, ExplorePlacement[]>();
  private selPiece = 0;
  private selPlacement = 0;

  // Step
  private axStep: string | null = null;
  private stepOpening: Promise<string | null> | null = null;
  private finished = 0;
  private stepPlaced: Placement[] = [];
  private stepsTaken = 0;

  // Movements
  private mx: string | null = null;
  private mxSolution: string | null = null;
  private tree: MovementTree | null = null;
  private sel = 0;
  private directions: string[] = [];
  private vectors: V3[] = [];
  private transformations = new Map<number, number>();
  /** Grows when the movement explorer is closed or opened again (a late opening is dropped). */
  private mxToken = 0;
  private adding: { direction: number; pieces: Set<number> } | null = null;

  /** Calls under way (the drawer's buttons wait). */
  private busy = 0;
  /** The meshes drawn, by shape, transformation and style (made once a drawer session). */
  private readonly built = new Map<string, BuiltMesh>();
  private modelKey = '';

  constructor(host: ExplorerHost) {
    this.host = host;
    const d = byId('sdrawer');
    d.addEventListener('click', (e) => {
      void this.clicked(e);
    });
    d.addEventListener('change', (e) => {
      this.formChanged(e);
    });
    byId('slvAdv').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-x]');
      if (b === null || b.disabled) {
        return;
      }
      const k = b.dataset.x;
      if (k === 'step') {
        void this.stepBy(1);
      } else if (k === 'placements' || k === 'movements') {
        this.toggle(k);
      }
    });
    host.store.subscribe((c) => {
      if (
        c.kind === 'document' &&
        host.store.generation !== this.generation &&
        this.mode !== null
      ) {
        // the engine dropped the handles with the old document
        this.forget();
        this.close();
      }
    });
    this.installKeys();
    addEscapeStep(ESC_ORDER.playback + 5, () => {
      if (tab !== 'solver' || this.mode === null) {
        return false;
      }
      this.close();
      return true;
    });
  }

  // ---- what may be opened ----

  /** Whether an explorer can be opened now (the mockup's rules). */
  allowed(mode: ExplorerMode): boolean {
    if (this.host.problem() === null) {
      return false;
    }
    if (mode === 'movements') {
      // a grid without a take-apart has no movement search (sliding: its
      // moves are the slide search's routes; the engine refuses it)
      const store = this.host.store;
      const grid = store.info?.grids.find((g) => g.grid === store.snapshot?.grid);
      return grid?.disassemble !== false && this.host.solution()?.disassembly === 'tree';
    }
    // a grid without an assembler (stacking) has no placements to browse or step
    const store = this.host.store;
    const grid = store.info?.grids.find((g) => g.grid === store.snapshot?.grid);
    return grid?.assemble !== false && this.host.assemblyReady();
  }

  /** The Advanced section's buttons (the mockup's #slvAdv). */
  renderButtons(): void {
    const b = (k: ExplorerMode, label: string, title: string): string =>
      `<button data-x="${k}" class="${this.mode === k ? 'on' : ''}" ${this.allowed(k) ? '' : 'disabled'} title="${title}">${label}</button>`;
    setHtml(
      byId('slvAdv'),
      b(
        'placements',
        'Placements',
        'Browse the placements of each piece (needs a prepared or paused solve)',
      ) +
        b('movements', 'Movements', 'Browse the disassembly search of the selected solution') +
        b('step', 'Step', 'Advance the assembler by one step (needs a prepared or paused solve)'),
    );
  }

  /**
   * The workspace changed (another problem, solution or state): an explorer
   * no longer allowed closes; Movements follows the solution selected.
   */
  refresh(): void {
    const problem = this.host.problem();
    if (
      this.mode !== null &&
      (problem !== this.problem || this.host.store.generation !== this.generation)
    ) {
      this.close();
    }
    if (this.mode !== null && !this.allowed(this.mode)) {
      this.close();
    }
    if (this.mode === 'movements' && this.host.solution()?.id !== this.mxSolution) {
      // another solution: its own tree
      this.closeMovements();
      void this.openMovements();
    }
    this.renderButtons();
  }

  // ---- opening and closing ----

  /** Opens an explorer, or closes it when it is the one open. */
  toggle(mode: ExplorerMode): void {
    if (this.mode === mode) {
      this.close();
    } else {
      this.open(mode);
    }
  }

  /** Opens an explorer in the drawer. */
  open(mode: ExplorerMode): void {
    if (!this.allowed(mode)) {
      return;
    }
    const problem = this.host.problem();
    if (problem !== this.problem || this.host.store.generation !== this.generation) {
      this.closeHandles();
      this.problem = problem;
      this.generation = this.host.store.generation;
      this.pieces = problemPieces(this.host.detail(), this.host.store);
      this.selPiece = 0;
      this.selPlacement = 0;
    }
    this.mode = mode;
    this.token++;
    this.adding = null;
    byId('sdrawer').classList.add('open');
    this.render();
    this.host.changed();
    if (mode === 'placements') {
      void this.showPlacements();
    } else if (mode === 'step') {
      void this.ensureStep().then(() => {
        this.render();
        void this.draw();
      });
    } else {
      if (this.host.solution()?.id !== this.mxSolution) {
        this.closeMovements();
      }
      if (this.mxSolution === null) {
        void this.openMovements();
      } else {
        this.render();
        void this.draw();
      }
    }
  }

  /** Closes the drawer and every handle. */
  close(): void {
    if (this.mode === null && this.ax === null && this.axStep === null && this.mx === null) {
      return;
    }
    this.mode = null;
    this.token++;
    this.closeHandles();
    this.adding = null;
    const d = byId('sdrawer');
    d.classList.remove('open');
    d.innerHTML = '';
    d.dataset.mode = '';
    d.dataset.handle = '';
    this.host.view.setSolution(null, 'explorer');
    this.disposeBuilt();
    this.modelKey = '';
    this.host.changed();
  }

  // the handles of another document are gone already: nothing to close
  private forget(): void {
    this.ax = null;
    this.axStep = null;
    this.mx = null;
    this.axOpening = null;
    this.stepOpening = null;
  }

  private closeHandles(): void {
    for (const h of [this.ax, this.axStep]) {
      this.closeHandle(h);
    }
    this.ax = null;
    this.axOpening = null;
    this.lists.clear();
    this.axStep = null;
    this.stepOpening = null;
    this.finished = 0;
    this.stepPlaced = [];
    this.stepsTaken = 0;
    this.closeMovements();
  }

  private closeMovements(): void {
    this.mxToken++;
    this.closeHandle(this.mx);
    this.mx = null;
    this.mxSolution = null;
    this.tree = null;
    this.sel = 0;
    this.adding = null;
  }

  private closeHandle(h: string | null): void {
    if (h !== null && this.generation === this.host.store.generation) {
      this.host.store.engine.call('explore.close', { handle: h }).catch(() => {
        // gone already (another document)
      });
    }
  }

  // an assembly explorer of the problem with the options shown (Prepare in the original)
  private async openAssembly(): Promise<string | null> {
    const problem = this.problem;
    if (problem === null) {
      return null;
    }
    const o = this.host.options();
    const gen = this.generation;
    this.busy++;
    try {
      const r = await this.host.store.engine.call('explore.openAssembly', {
        problem,
        ...(o.solver === undefined ? {} : { solver: o.solver }),
        keepMirrors: o.keepMirrors ?? false,
        keepRotations: o.keepRotations ?? false,
        completeRotations: o.completeRotations ?? false,
        strictColours: o.strictColours ?? false,
      });
      if (problem !== this.problem || gen !== this.host.store.generation) {
        this.host.store.engine.call('explore.close', { handle: r.handle }).catch(() => undefined);
        return null;
      }
      this.placementsSupported = r.placementsSupported;
      return r.handle;
    } catch (e) {
      if (!quiet(e)) {
        reportError(e, 'The assembler could not be prepared');
      }
      return null;
    } finally {
      this.busy--;
    }
  }

  // ---- Placements ----

  private async ensurePlacements(): Promise<string | null> {
    if (this.ax !== null) {
      return this.ax;
    }
    this.axOpening ??= this.openAssembly().then((h) => {
      this.ax = h;
      return h;
    });
    return this.axOpening;
  }

  private async showPlacements(): Promise<void> {
    const t = this.token;
    const h = await this.ensurePlacements();
    if (h === null || t !== this.token) {
      return;
    }
    if (!this.placementsSupported) {
      this.render();
      return;
    }
    // every piece's count for the list, the piece chosen's placements first
    const order = [
      this.selPiece,
      ...this.pieces.map((p) => p.piece).filter((p) => p !== this.selPiece),
    ];
    for (const piece of order) {
      if (!this.lists.has(piece)) {
        try {
          const r = await this.host.store.engine.call('explore.placements', { handle: h, piece });
          if (h !== this.ax) {
            return;
          }
          this.lists.set(piece, r.placements);
        } catch (e) {
          if (!quiet(e)) {
            reportError(e, 'The placements could not be read');
          }
          return;
        }
      }
      if (piece === this.selPiece && t === this.token) {
        this.render();
        void this.draw();
      }
    }
    if (this.mode === 'placements') {
      this.render();
    }
  }

  private choosePiece(piece: number): void {
    const n = this.pieces.length;
    if (n === 0) {
      return;
    }
    this.selPiece = Math.max(0, Math.min(n - 1, piece));
    this.selPlacement = 0;
    this.token++;
    this.render();
    void this.showPlacements();
  }

  private choosePlacement(i: number): void {
    const list = this.lists.get(this.selPiece) ?? [];
    if (list.length === 0) {
      return;
    }
    this.selPlacement = Math.max(0, Math.min(list.length - 1, i));
    this.render();
    void this.draw();
  }

  // ---- Step ----

  private async ensureStep(): Promise<string | null> {
    if (this.axStep !== null) {
      return this.axStep;
    }
    this.stepOpening ??= this.openAssembly().then((h) => {
      this.axStep = h;
      return h;
    });
    return this.stepOpening;
  }

  /** The Step button: the assembler `n` steps on (the drawer's Step view opened). */
  async stepBy(n: number): Promise<void> {
    if (this.mode !== 'step') {
      if (!this.allowed('step')) {
        return;
      }
      this.open('step');
    }
    const h = await this.ensureStep();
    if (h === null || this.busy > 0) {
      return;
    }
    this.busy++;
    this.render();
    try {
      const r = await this.host.store.engine.call('explore.step', { handle: h, steps: n });
      if (h !== this.axStep) {
        return;
      }
      this.finished = r.finished;
      this.stepPlaced = r.placements;
      this.stepsTaken += n;
    } catch (e) {
      if (!quiet(e)) {
        reportError(e, 'Step');
      }
    } finally {
      this.busy--;
    }
    if (this.mode === 'step') {
      this.render();
      void this.draw();
    }
  }

  // Reset: the search from the start again (a new assembler)
  private resetStep(): void {
    this.closeHandle(this.axStep);
    this.axStep = null;
    this.stepOpening = null;
    this.finished = 0;
    this.stepPlaced = [];
    this.stepsTaken = 0;
    void this.ensureStep().then(() => {
      this.render();
      void this.draw();
    });
  }

  // ---- Movements ----

  private async openMovements(): Promise<void> {
    const problem = this.problem;
    const row = this.host.solution();
    if (problem === null || row?.disassembly !== 'tree') {
      return;
    }
    const t = ++this.mxToken;
    const gen = this.generation;
    // the solution being opened: a refresh meanwhile does not open it again
    this.mxSolution = row.id;
    this.busy++;
    this.render();
    try {
      const [mx, got] = await Promise.all([
        this.host.store.engine.call('explore.openMovements', { problem, solution: row.id }),
        this.host.store.engine.call('solution.get', { problem, solution: row.id }),
      ]);
      if (t !== this.mxToken || gen !== this.host.store.generation) {
        this.host.store.engine.call('explore.close', { handle: mx.handle }).catch(() => undefined);
        return;
      }
      this.mx = mx.handle;
      this.tree = new MovementTree(mx.root);
      this.sel = mx.root.node;
      this.directions = mx.directions;
      this.vectors = mx.vectors.map((v) => [v[0], v[1], v[2]]);
      this.transformations = new Map(got.placements.map((p) => [p.piece, p.transformation]));
    } catch (e) {
      if (!quiet(e)) {
        reportError(e, 'The movements could not be opened');
      }
    } finally {
      this.busy--;
    }
    if (this.mode === 'movements') {
      this.render();
      void this.draw();
    }
  }

  /** A position of the tree, chosen. */
  private chooseNode(id: number): void {
    if (this.tree?.get(id) === undefined) {
      return;
    }
    this.sel = id;
    this.adding = null;
    this.render();
    void this.draw();
  }

  // a call that adds positions under the one chosen; the one to choose after it
  private async grow(
    doing: string,
    call: (h: string, node: number) => Promise<MovementNode[]>,
    choose: (nodes: MovementNode[]) => number | null,
  ): Promise<void> {
    const h = this.mx;
    const tree = this.tree;
    if (h === null || tree === null || this.busy > 0) {
      return;
    }
    this.busy++;
    this.render();
    try {
      const nodes = await call(h, this.sel);
      if (h !== this.mx) {
        return;
      }
      tree.put(nodes);
      const next = choose(nodes);
      if (next !== null) {
        this.sel = next;
      }
    } catch (e) {
      if (e instanceof EngineError && e.errc === 'not_found' && doing === 'Find a move') {
        flash('No move of that piece that way');
      } else if (!quiet(e)) {
        reportError(e, doing);
      }
    } finally {
      this.busy--;
    }
    this.adding = null;
    this.render();
    void this.draw();
  }

  /** Analyse: every position one move away from the one chosen. */
  async analyse(): Promise<void> {
    await this.grow(
      'Analyse',
      async (handle, node) =>
        (await this.host.store.engine.call('explore.analyse', { handle, node })).nodes,
      () => null,
    );
    const n = this.tree?.get(this.sel)?.children.length ?? 0;
    flash(
      n === 0
        ? 'No moves from this position'
        : `${String(n)} move${n === 1 ? '' : 's'} from this position`,
    );
  }

  private async addMove(pieces: number[], label: number): Promise<void> {
    await this.grow(
      'Add move',
      async (handle, node) => [
        await this.host.store.engine.call('explore.addMove', {
          handle,
          node,
          pieces,
          direction: Math.floor(label / 2),
          reverse: label % 2 === 1,
        }),
      ],
      (nodes) => nodes[0]?.node ?? null,
    );
  }

  /** A piece Alt-dragged in the view: the first valid move that takes it the way it was dragged. */
  pieceDragged(piece: number, dx: number, dy: number): void {
    if (this.mode !== 'movements' || this.mx === null) {
      return;
    }
    const grid = this.host.store.snapshot?.grid;
    if (grid === undefined) {
      return;
    }
    const scale = cellScale(grid);
    const onScreen = this.vectors.map((v): [number, number] => {
      const s = this.host.view.pieceOnScreen(piece, [
        v[0] * scale[0],
        v[1] * scale[1],
        v[2] * scale[2],
      ]);
      return s === null ? [0, 0] : [s.dx, s.dy];
    });
    const d = dragDirection([dx, dy], onScreen);
    const v = d === null ? undefined : this.vectors[d.direction];
    if (d === null || v === undefined) {
      flash('Drag the piece along one of the directions it can move in');
      return;
    }
    const delta: V3 = d.reverse ? [-v[0], -v[1], -v[2]] : [v[0], v[1], v[2]];
    void this.grow(
      'Find a move',
      async (handle, node) => [
        await this.host.store.engine.call('explore.findMatching', { handle, node, piece, delta }),
      ],
      (nodes) => nodes[0]?.node ?? null,
    );
  }

  /** Whether an Alt-drag on a piece moves it now. */
  get movesPieces(): boolean {
    return this.mode === 'movements' && this.mx !== null && tab === 'solver';
  }

  private async prune(): Promise<void> {
    const h = this.mx;
    const tree = this.tree;
    if (h === null || tree === null || this.busy > 0) {
      return;
    }
    this.busy++;
    try {
      await this.host.store.engine.call('explore.prune', { handle: h, node: this.sel });
      if (h === this.mx) {
        tree.prune(this.sel);
      }
    } catch (e) {
      if (!quiet(e)) {
        reportError(e, 'Prune');
      }
    } finally {
      this.busy--;
    }
    this.render();
  }

  // ---- the drawer ----

  private async clicked(e: MouseEvent): Promise<void> {
    const t = e.target as Element;
    const x = t.closest<HTMLElement>('[data-ex]');
    const a = t.closest<HTMLButtonElement>('[data-exa]');
    const r = t.closest<HTMLElement>('[data-exr]');
    if (x !== null) {
      const k = x.dataset.ex;
      if (k === 'close') {
        this.close();
      } else if (k === 'placements' || k === 'step' || k === 'movements') {
        if (k !== this.mode) {
          this.open(k);
        }
      }
      return;
    }
    if (r !== null) {
      byId('sdrawer').querySelector<HTMLElement>('.dl')?.focus({ preventScroll: true });
      const v = Number(r.dataset.exr);
      if (this.mode === 'placements') {
        this.choosePiece(v);
      } else if (this.mode === 'movements') {
        this.chooseNode(v);
      }
      return;
    }
    if (a === null || a.disabled) {
      return;
    }
    switch (a.dataset.exa) {
      case 'prev':
        this.choosePlacement(this.selPlacement - 1);
        return;
      case 'next':
        this.choosePlacement(this.selPlacement + 1);
        return;
      case 'step':
        await this.stepBy(Number(a.dataset.n ?? '1'));
        return;
      case 'reset':
        this.resetStep();
        return;
      case 'analyse':
        await this.analyse();
        return;
      case 'add':
        this.adding = { direction: 0, pieces: new Set() };
        this.render();
        return;
      case 'addDone': {
        const f = this.adding;
        if (f === null) {
          return;
        }
        if (f.pieces.size === 0) {
          flash('Choose the pieces to move');
          return;
        }
        await this.addMove(
          [...f.pieces].sort((p, q) => p - q),
          f.direction,
        );
        return;
      }
      case 'addCancel':
        this.adding = null;
        this.render();
        return;
      case 'back': {
        const n = this.tree?.get(this.sel);
        if (n !== undefined && n.parent !== n.node) {
          this.chooseNode(n.parent);
        }
        return;
      }
      case 'prune':
        await this.prune();
        return;
    }
  }

  private formChanged(e: Event): void {
    const el = e.target as HTMLInputElement | HTMLSelectElement;
    const f = this.adding;
    if (f === null) {
      if (el.dataset.f === 'placement') {
        this.choosePlacement(Number(el.value));
      }
      return;
    }
    if (el.dataset.f === 'dir') {
      f.direction = Number(el.value);
    } else if (el.dataset.f === 'piece' && el instanceof HTMLInputElement) {
      const p = Number(el.value);
      if (el.checked) {
        f.pieces.add(p);
      } else {
        f.pieces.delete(p);
      }
    }
  }

  private labelOf(piece: number): string {
    return this.pieces.find((p) => p.piece === piece)?.label ?? `#${String(piece)}`;
  }

  /** The drawer's contents (the mockup's renderSolverDrawer). */
  render(): void {
    const d = byId('sdrawer');
    d.classList.toggle('open', this.mode !== null);
    if (this.mode === null) {
      return;
    }
    const m = this.mode;
    const seg = (k: ExplorerMode, label: string): string =>
      `<button data-ex="${k}" class="${m === k ? 'on' : ''}" ${this.allowed(k) || m === k ? '' : 'disabled'}>${label}</button>`;
    const sub =
      m === 'placements'
        ? 'Every placement of each piece, in the assembler’s order'
        : m === 'step'
          ? 'The assembly search, one step at a time'
          : 'The disassembly search of the selected solution';
    const head = `<div class="seg">${seg('placements', 'Placements')}${seg('step', 'Step')}${seg('movements', 'Movements')}</div><span class="mute" style="font-size:12px">${sub}</span><span class="sp"></span><button class="ib" data-ex="close" title="Close explorer  (Esc)">${ic('close', 16)}</button>`;
    const [list, right] =
      m === 'placements'
        ? this.placementsHtml()
        : m === 'step'
          ? this.stepHtml()
          : this.movementsHtml();
    // the parts kept, their contents set: the list keeps the keyboard focus
    if (d.querySelector('.dl') === null) {
      d.innerHTML =
        '<div class="dh"></div><div class="db"><div class="dl" tabindex="0" aria-label="Explorer list"></div><div class="dr"></div></div>';
    }
    setHtml(d.querySelector<HTMLElement>('.dh') ?? d, head);
    setHtml(d.querySelector<HTMLElement>('.dl') ?? d, list);
    setHtml(d.querySelector<HTMLElement>('.dr') ?? d, right);
    d.dataset.mode = m;
    d.dataset.handle = (m === 'placements' ? this.ax : m === 'step' ? this.axStep : this.mx) ?? '';
    d.dataset.busy = String(this.busy);
    d.dataset.piece = String(this.selPiece);
    d.dataset.placement = String(this.selPlacement);
    d.dataset.node = String(this.sel);
    d.dataset.finished = String(this.finished);
    d.dataset.steps = String(this.stepsTaken);
    d.dataset.placed = String(this.stepPlaced.filter((p) => p.placed).length);
    // what the engine's step gave, for whoever compares (the e2e tests)
    d.dataset.stepPlacements = JSON.stringify(this.stepPlaced);
    d.querySelector('.dl .on')?.scrollIntoView({ block: 'nearest' });
  }

  private placementsHtml(): [string, string] {
    if (!this.placementsSupported) {
      return ['', '<div class="info">This assembler has no placement browser.</div>'];
    }
    const list = this.pieces
      .map((p) => {
        const n = this.lists.get(p.piece)?.length;
        return `<div class="${p.piece === this.selPiece ? 'on' : ''}" data-exr="${String(p.piece)}" data-count="${n === undefined ? '' : String(n)}">${esc(p.label)} placements<span>${n === undefined ? '…' : String(n)}</span></div>`;
      })
      .join('');
    const pl = this.lists.get(this.selPiece);
    const p = this.pieces.find((x) => x.piece === this.selPiece);
    let right: string;
    if (pl === undefined) {
      right = '<div class="info">Reading the placements…</div>';
    } else if (pl.length === 0) {
      right = `<div class="info">${esc(p?.label ?? '')} has no placement in the result: no assembly can use it.</div>`;
    } else {
      const q = pl[this.selPlacement];
      right =
        `<div class="ctl"><button class="btn sm" data-exa="prev" ${this.selPlacement > 0 ? '' : 'disabled'}>◀ Back</button><button class="btn sm pri" data-exa="next" ${this.selPlacement < pl.length - 1 ? '' : 'disabled'}>Next ▶</button><input type="range" data-f="placement" min="0" max="${String(pl.length - 1)}" value="${String(this.selPlacement)}" aria-label="Placement"></div>` +
        `<div class="info"><b>${esc(p?.label ?? '')}</b> · placement ${String(this.selPlacement + 1)} of ${String(pl.length)} · orientation ${String(q?.transformation ?? 0)} at (${(q?.at ?? []).join(', ')})</div>` +
        '<div class="info">The view shows the result as a wire frame and the piece in this placement. ↑ ↓ the placement, ← → the piece.</div>';
    }
    if (this.finished > 0 && this.finished < 1) {
      right += '<div class="info">The assembler is neither at its start nor at its end.</div>';
    }
    return [list, right];
  }

  private stepHtml(): [string, string] {
    const placed = new Set(this.stepPlaced.filter((p) => p.placed).map((p) => p.piece));
    const list = this.pieces
      .map(
        (p) =>
          `<div data-piece="${String(p.piece)}" class="${placed.has(p.piece) ? 'on' : ''}">${esc(p.label)}<span>${placed.has(p.piece) ? 'placed' : '—'}</span></div>`,
      )
      .join('');
    const busy = this.busy > 0 || this.axStep === null;
    const dis = busy ? 'disabled' : '';
    const right =
      `<div class="ctl"><button class="btn sm pri" data-exa="step" data-n="1" ${dis}>Step ▶</button><button class="btn sm" data-exa="step" data-n="10" ${dis}>10 ▶</button><button class="btn sm" data-exa="step" data-n="100" ${dis}>100 ▶</button><button class="btn sm" data-exa="step" data-n="1000" ${dis}>Run ▶▶</button><button class="btn sm" data-exa="reset" ${dis}>Reset</button></div>` +
      `<div class="info">${String(placed.size)} of ${String(this.pieces.length)} pieces placed · ${String(this.stepsTaken)} step${this.stepsTaken === 1 ? '' : 's'} taken · search ${(this.finished * 100).toFixed(1)} % done${this.finished >= 1 ? ' (through)' : ''}</div>` +
      `<div class="tree">${this.pieces.map((p) => `<i class="${placed.has(p.piece) ? 'on' : ''}">${esc(p.label)}</i>`).join('')}</div>` +
      '<div class="info" style="margin-top:auto">An assembler of its own, prepared with the search options shown: stepping it never moves the solve. Space steps once.</div>';
    return [list, right];
  }

  private movementsHtml(): [string, string] {
    const tree = this.tree;
    if (tree === null) {
      return ['', '<div class="info">Opening the movements…</div>'];
    }
    const list = tree
      .rows()
      .map(
        ({ node, depth }) =>
          `<div class="${node.node === this.sel ? 'on' : ''}" data-exr="${String(node.node)}" data-node="${String(node.node)}" data-label="${esc(node.label)}" style="padding-left:${String(8 + 14 * depth)}px">${node.node === tree.root ? 'Assembled' : esc(node.label)}<span>${node.children.length > 0 ? String(node.children.length) : ''}</span></div>`,
      )
      .join('');
    const n = tree.get(this.sel);
    const dis = this.busy > 0 ? 'disabled' : '';
    let right =
      `<div class="ctl"><button class="btn sm pri" data-exa="analyse" ${dis} title="Analyze this node for possible movement">Analyse</button><button class="btn sm" data-exa="add" ${dis} title="Add a fixed movement to current node and see what happens">Add move…</button><button class="btn sm" data-exa="back" ${n !== undefined && n.parent !== n.node ? '' : 'disabled'} title="Jump back to the parent of the selected node in the tree">Step back</button><button class="btn sm" data-exa="prune" ${dis} title="Remove all nodes except the ones that lead to the selected one">Prune tree</button></div>` +
      `<div class="info">Position ${String(this.sel)} · depth ${String(tree.path(this.sel).length - 1)} · ${String(n?.children.length ?? 0)} move${n?.children.length === 1 ? '' : 's'} from it shown${n !== undefined && n.label !== '' ? ` · reached by ${esc(n.label)}` : ''}</div>`;
    const f = this.adding;
    if (f !== null && n !== undefined) {
      right +=
        `<div class="exadd"><label>Direction <select data-f="dir">${this.directions.map((l, i) => `<option value="${String(i)}" ${i === f.direction ? 'selected' : ''}>${esc(l)}</option>`).join('')}</select></label>` +
        `<div class="pcs">${n.pieces.map((p) => `<label><input type="checkbox" data-f="piece" value="${String(p.piece)}" ${f.pieces.has(p.piece) ? 'checked' : ''}> ${esc(this.labelOf(p.piece))}</label>`).join('')}</div>` +
        `<div class="ctl"><button class="btn sm pri" data-exa="addDone">Done</button><button class="btn sm" data-exa="addCancel">Cancel</button></div></div>`;
    } else {
      right +=
        '<div class="info" style="margin-top:auto"><kbd>Alt</kbd>/<kbd>⌘</kbd>-drag a piece in the view to move it: the first valid move that takes it that way (and as few others as it must). ↑ ↓ a position, ← its parent, → its first move, Enter analyses.</div>';
    }
    return [list, right];
  }

  /** The view's tag while an explorer shows (HTML; '' for none). */
  tag(): string {
    if (this.mode === 'placements') {
      const n = this.lists.get(this.selPiece)?.length ?? 0;
      return `<span class="mute">· Placements: ${esc(this.labelOf(this.selPiece))}${n > 0 ? `, ${String(this.selPlacement + 1)} of ${String(n)}` : ''}</span>`;
    }
    if (this.mode === 'step') {
      const k = this.stepPlaced.filter((p) => p.placed).length;
      return `<span class="mute">· Assembler state — ${String(k)} of ${String(this.pieces.length)} pieces placed</span>`;
    }
    if (this.mode === 'movements') {
      return `<span class="mute">· Movements: position ${String(this.sel)}</span>`;
    }
    return '';
  }

  // ---- the view ----

  private items(): DrawItem[] {
    const detail = this.host.detail();
    const snap = this.host.store.snapshot;
    const result = detail?.result ?? null;
    const out: DrawItem[] = [];
    const resultItem = (): DrawItem | null => {
      if (result === null) {
        return null;
      }
      const i = this.host.store.indexOf(result);
      return {
        piece: -1,
        shape: result,
        colour: shapeColour(snap?.shapes[i]?.colour ?? null, i),
        mode: 'wire',
        transformation: 0,
        at: [0, 0, 0],
      };
    };
    const byNumber = new Map(this.pieces.map((p) => [p.piece, p]));
    if (this.mode === 'placements') {
      const r = resultItem();
      if (r !== null) {
        out.push(r);
      }
      const q = this.lists.get(this.selPiece)?.[this.selPlacement];
      const p = byNumber.get(this.selPiece);
      if (q !== undefined && p !== undefined) {
        out.push({ ...p, mode: 'solid', transformation: q.transformation, at: [...q.at] });
      }
    } else if (this.mode === 'step') {
      const r = resultItem();
      if (r !== null) {
        out.push(r);
      }
      for (const q of this.stepPlaced) {
        const p = byNumber.get(q.piece);
        if (q.placed && p !== undefined) {
          out.push({ ...p, mode: 'solid', transformation: q.transformation, at: [...q.at] });
        }
      }
    } else if (this.mode === 'movements') {
      for (const q of this.tree?.get(this.sel)?.pieces ?? []) {
        const p = byNumber.get(q.piece);
        if (p !== undefined) {
          out.push({
            ...p,
            mode: 'solid',
            transformation: this.transformations.get(q.piece) ?? 0,
            at: [...q.at],
          });
        }
      }
    }
    return out;
  }

  /** Draws what the explorer shows (the view's explorer layer). */
  async draw(): Promise<void> {
    const store = this.host.store;
    const snap = store.snapshot;
    const view = this.host.view;
    if (this.mode === null || snap === null) {
      return;
    }
    const t = this.token;
    const mode = this.mode;
    const items = this.items();
    const style = view.display.style;
    const styleOf = (it: DrawItem): MeshStyle => (it.mode === 'wire' ? 'wireframe' : style);
    const key = (it: DrawItem): string => `${it.shape}:${String(it.transformation)}:${styleOf(it)}`;
    try {
      const replies = await Promise.all(
        items.map((it) =>
          view.meshCache.get(it.shape, styleOf(it), snap.revision, it.transformation),
        ),
      );
      if (t !== this.token || mode !== this.mode) {
        return;
      }
      const resultId = this.host.detail()?.result ?? null;
      const resultSize = snap.shapes.find((s) => s.id === resultId)?.size ?? [1, 1, 1];
      const extent = gridExtent(snap.grid, resultSize);
      const scale = cellScale(snap.grid);
      const built = items.map((it, i) => {
        const k = key(it);
        let b = this.built.get(k);
        const r = replies[i];
        if (b === undefined && r !== undefined) {
          const size = snap.shapes.find((s) => s.id === it.shape)?.size ?? [1, 1, 1];
          b = buildMesh(meshArrays(r), size, styleOf(it), {
            kind: 'shape',
            shape: it.shape,
            revision: snap.revision,
            transformation: it.transformation,
          });
          this.built.set(k, b);
        }
        return b ?? null;
      });
      const modelKey = `${String(this.generation)}:${this.problem ?? ''}:${mode}:${items.map((it) => `${String(it.piece)}/${key(it)}`).join(',')}`;
      if (modelKey !== this.modelKey) {
        const model: SolutionModel = {
          key: `explorer:${modelKey}`,
          extent,
          pieces: items.map((it, i) => ({
            piece: it.piece,
            shape: it.shape,
            colour: it.colour,
            mode: it.mode,
            built: built[i] ?? null,
          })),
        };
        this.modelKey = modelKey;
        view.setSolution(model, 'explorer');
      }
      const poses: ScenePose[] = items.map((it, i) => ({
        // the result fills its own space; a piece's hotspot goes on its cell
        t:
          it.piece < 0
            ? placedAt([0, 0, 0], [0, 0, 0], scale, extent)
            : placedAt(it.at, replies[i]?.hotspot ?? [0, 0, 0], scale, extent),
        q: [0, 0, 0, 1],
        visible: true,
        opacity: 1,
      }));
      view.poseSolution(poses, 'explorer');
      if (mode === 'movements') {
        // each piece's step in each direction as it looks on the screen (the e2e tests drag by them)
        const grid = snap.grid;
        const sc = cellScale(grid);
        const dirs: Record<string, [number, number][]> = {};
        for (const it of items) {
          dirs[String(it.piece)] = this.vectors.map((v) => {
            const o = view.pieceOnScreen(it.piece, [v[0] * sc[0], v[1] * sc[1], v[2] * sc[2]]);
            return o === null ? [0, 0] : [Number(o.dx.toFixed(2)), Number(o.dy.toFixed(2))];
          });
        }
        byId('sdrawer').dataset.screenDirs = JSON.stringify(dirs);
      }
      this.host.changed();
    } catch (e) {
      if (t === this.token && !quiet(e)) {
        reportError(e, 'The pieces could not be drawn');
      }
    }
  }

  /** The view's style changed: the meshes again. */
  restyle(): void {
    if (this.mode === null) {
      return;
    }
    this.modelKey = '';
    void this.draw();
  }

  private disposeBuilt(): void {
    for (const b of this.built.values()) {
      b.faces.dispose();
      b.marks?.dispose();
    }
    this.built.clear();
  }

  // ---- keys: the explorer's meaning of the shared actions, while the drawer has the focus ----

  private installKeys(): void {
    const on = (action: KeyAction, f: () => boolean): void => {
      onKey(
        action,
        (e) => {
          const t = e.target;
          if (
            tab !== 'solver' ||
            this.mode === null ||
            !(t instanceof Element) ||
            t.closest('#sdrawer') === null
          ) {
            return false;
          }
          return f();
        },
        true,
      );
    };
    const rows = (): number[] => (this.tree?.rows() ?? []).map((r) => r.node.node);
    const moveRow = (d: number): boolean => {
      const r = rows();
      const i = r.indexOf(this.sel);
      const next = r[Math.max(0, Math.min(r.length - 1, i + d))];
      if (next !== undefined) {
        this.chooseNode(next);
      }
      return true;
    };
    on('prev', () => {
      if (this.mode === 'placements') {
        this.choosePlacement(this.selPlacement - 1);
        return true;
      }
      return this.mode === 'movements' ? moveRow(-1) : false;
    });
    on('next', () => {
      if (this.mode === 'placements') {
        this.choosePlacement(this.selPlacement + 1);
        return true;
      }
      return this.mode === 'movements' ? moveRow(1) : false;
    });
    on('move.back', () => {
      if (this.mode === 'placements') {
        this.choosePiece(this.selPiece - 1);
        return true;
      }
      if (this.mode === 'movements') {
        const n = this.tree?.get(this.sel);
        if (n !== undefined && n.parent !== n.node) {
          this.chooseNode(n.parent);
        }
        return true;
      }
      return false;
    });
    on('move.forward', () => {
      if (this.mode === 'placements') {
        this.choosePiece(this.selPiece + 1);
        return true;
      }
      if (this.mode === 'movements') {
        const c = this.tree?.get(this.sel)?.children[0];
        if (c !== undefined) {
          this.chooseNode(c);
        }
        return true;
      }
      return false;
    });
    on('home', () => {
      if (this.mode === 'placements') {
        this.choosePlacement(0);
        return true;
      }
      if (this.mode === 'movements' && this.tree !== null) {
        this.chooseNode(this.tree.root);
        return true;
      }
      return false;
    });
    on('end', () => {
      if (this.mode === 'placements') {
        this.choosePlacement((this.lists.get(this.selPiece)?.length ?? 1) - 1);
        return true;
      }
      return this.mode === 'movements' ? moveRow(Number.MAX_SAFE_INTEGER) : false;
    });
    on('activate', () => {
      if (this.mode === 'step') {
        void this.stepBy(1);
        return true;
      }
      if (this.mode === 'movements') {
        void this.analyse();
        return true;
      }
      return false;
    });
  }
}

// a failure nobody needs to hear of: a newer read took its place, the
// handle or the engine went
function quiet(e: unknown): boolean {
  return (
    e instanceof EngineError &&
    (e.errc === 'superseded' || e.errc === 'not_found' || e.errc === 'disconnected')
  );
}
