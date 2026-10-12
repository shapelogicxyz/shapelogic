// SPDX-License-Identifier: GPL-3.0-or-later
//
// The disassembly player of the Solver workspace (FRONTEND-PLAN.md, stage
// 4). A solution chosen in the list is shown assembled at once: its
// placements (solution.get), each piece drawn as its shape's mesh in the
// placement's transformation (shape.mesh {transformation}, the view's mesh
// cache) at its cell minus that mesh's hotspot, in mesh units
// (extent.ts's cellScale), centred on the result as the poses are. Once
// the choice rests a moment, its playback is opened (playback.open: a
// solution without its moves is the single pose, steps 0, one path for
// all): each piece's mesh in its assembly orientation and the chosen style
// (playback.mesh), the poses of the steps shown in chunks (playback.poses,
// SAMPLES a step; poses.ts interpolates, slerp for the turns). Play and
// pause, one move back and forward, the jumps to assembled and apart, the
// seek bar and its speed move a time `t` in steps; the pieces are solid,
// wire frames or hidden (the chips) and fade when taken away (the
// setting). From docs/mockup/index.html: `svSelect`, `svSeek`,
// `svTogglePlay`, `svPlayLoop`, `svPlayerSync` and the player's handlers
// in `svWire`. A stacking solution (docs/webui-spec.md, section 9.3) steps
// in transfers (the engine's steps), its discs placed by their centres
// (the original's ensureHotspot) around the board's middle, as the poses
// are, with the board's pegs drawn around them (view3d/board.ts).

import type {
  BoardLayout,
  Placement,
  ProblemDetail,
  SolutionRow,
} from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { meshArrays, poseArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { pieceSubColour, shapeColour } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { prefs } from '../shell/prefs.ts';
import { flash, reportError } from '../shell/status.ts';
import { boardExtent } from '../view3d/board.ts';
import { cellScale, gridExtent, placedAt } from '../view3d/extent.ts';
import { buildMesh } from '../view3d/mesh.ts';
import type { BuiltMesh } from '../view3d/mesh.ts';
import type { PieceMode, ScenePose, SolutionModel } from '../view3d/solution.ts';
import type { View3D } from '../view3d/view.ts';
import { PoseTrack, SAMPLES } from './poses.ts';

/** A placed piece of the solution shown. */
export interface PlayerPiece {
  /** The problem's piece number. */
  piece: number;
  shape: string;
  /** S1, or S1.2 for the second copy of a part of several. */
  label: string;
  colour: Rgb3;
  transformation: number;
  at: [number, number, number];
}

/** What the player tells its workspace. */
export interface PlayerHost {
  readonly store: Store;
  readonly view: View3D;
  /** The problem shown (its parts number the pieces). */
  detail(): ProblemDetail | null;
  /** The player's state changed (the controls, the chips). */
  changed(): void;
  /** Whether an analysis of this session found the solution not to come apart. */
  inseparable(solution: string): boolean;
  /** Whether the solution is still the one chosen, in a row of the list's newest version. */
  current(solution: string): boolean;
  /** A stacking problem's board and the middle its poses are placed around (null: none). */
  board(): { layout: BoardLayout; centre: [number, number, number] } | null;
}

/** How long a choice rests before its playback is opened (arrow keys run through quickly). */
const OPEN_AFTER_MS = 120;
/** The mockup's pace: 1.2 moves a second at 1×. */
const MOVES_PER_SECOND = 1.2;

export class Player {
  /** The solution shown: its problem, row and placements. */
  problem: string | null = null;
  row: SolutionRow | null = null;
  pieces: PlayerPiece[] = [];
  /** The open playback, its steps (0: the single pose) and level text. */
  handle: string | null = null;
  steps = 0;
  movesText: string | null = null;
  /** The time shown, in steps. */
  t = 0;
  playing = false;
  speed = 1;
  /** How each piece is shown, by piece number (the chips). */
  readonly modes = new Map<number, PieceMode>();
  private readonly host: PlayerHost;
  private track: PoseTrack | null = null;
  /** The playback's pieces in its order: piece numbers. */
  private order: number[] = [];
  /** The playback's meshes by piece number and style. */
  private readonly playMeshes = new Map<string, BuiltMesh>();
  private assembledMeshes: BuiltMesh[] = [];
  private token = 0;
  private generation = -1;
  private openTimer = 0;
  /** The playback's opening: waiting for the choice to rest, or asked for. */
  private opening: 'none' | 'timer' | 'asked' = 'none';
  private raf = 0;
  private loadingChunks = new Set<number>();
  /** The model shown: 'assembled', or 'playback' once the playback is in. */
  private shown: 'none' | 'assembled' | 'playback' = 'none';
  /** A solution to play once its playback is open. */
  private pendingPlay: string | null = null;

  constructor(host: PlayerHost) {
    this.host = host;
  }

  /** Whether the playback is open and its poses can be shown. */
  get ready(): boolean {
    return this.shown === 'playback';
  }

  /** The solution shown, if any. */
  get solution(): string | null {
    return this.row?.id ?? null;
  }

  /** Shows a solution (null: none): assembled at once, its playback after a moment. */
  show(problem: string | null, row: SolutionRow | null): void {
    const store = this.host.store;
    if (
      row !== null &&
      problem === this.problem &&
      row.id === this.row?.id &&
      store.generation === this.generation
    ) {
      // the same solution (a re-read list): its row may say more now
      const disassemblyChanged = row.disassembly !== this.row.disassembly;
      this.row = row;
      if (disassemblyChanged) {
        // its moves came or went (an analysis, a delete): open it again
        this.reopen();
      } else if (problem !== null && this.shown !== 'playback' && this.opening === 'none') {
        // its opening was held (the list was changing): open it now
        void this.loadAssembled(++this.token, problem, row);
      }
      return;
    }
    this.close();
    this.problem = problem;
    this.row = row;
    this.generation = store.generation;
    this.t = 0;
    this.stop();
    if (problem === null || row === null) {
      this.host.view.setSolution(null);
      this.mark();
      this.host.changed();
      return;
    }
    const n = ++this.token;
    void this.loadAssembled(n, problem, row);
    this.host.changed();
  }

  /** Opens the playback again (the moves of the solution changed), keeping the time. */
  reopen(): void {
    const problem = this.problem;
    const row = this.row;
    if (problem === null || row === null) {
      return;
    }
    const t = this.t;
    this.close();
    this.problem = problem;
    this.row = row;
    this.t = t;
    const n = ++this.token;
    void this.loadAssembled(n, problem, row);
  }

  /** Forgets the solution and closes its playback (another document drops it engine-side). */
  close(): void {
    this.token++;
    window.clearTimeout(this.openTimer);
    this.opening = 'none';
    this.stop();
    const h = this.handle;
    if (h !== null && this.generation === this.host.store.generation) {
      this.host.store.engine.call('playback.close', { handle: h }).catch(() => {
        // gone already (another document)
      });
    }
    this.handle = null;
    this.track = null;
    this.steps = 0;
    this.movesText = null;
    this.order = [];
    this.disposeMeshes();
    this.shown = 'none';
    this.problem = null;
    this.row = null;
    this.pieces = [];
    this.loadingChunks.clear();
    this.mark();
  }

  // ---- what is drawn ----

  /** The view's style or a piece's chip changed: the meshes again. */
  restyle(): void {
    const problem = this.problem;
    const row = this.row;
    if (problem === null || row === null) {
      return;
    }
    const n = ++this.token;
    if (this.shown === 'playback') {
      void this.showPlayback(n);
    } else {
      void this.loadAssembled(n, problem, row);
    }
  }

  /** Cycles a piece's chip: solid → wire frame → hidden. */
  cycle(piece: number): void {
    const m = this.modes.get(piece) ?? 'solid';
    this.modes.set(piece, m === 'solid' ? 'wire' : m === 'wire' ? 'hidden' : 'solid');
    this.restyle();
    this.host.changed();
  }

  /** Every piece solid again (Show all). */
  showAll(): void {
    this.modes.clear();
    this.restyle();
    this.host.changed();
  }

  private mode(piece: number): PieceMode {
    return this.modes.get(piece) ?? 'solid';
  }

  private styleOf(piece: number): 'drawing' | 'wireframe' | 'flat' {
    return this.mode(piece) === 'wire' ? 'wireframe' : this.host.view.display.style;
  }

  private async loadAssembled(n: number, problem: string, row: SolutionRow): Promise<void> {
    const store = this.host.store;
    const snap = store.snapshot;
    try {
      const got = await store.engine.call('solution.get', { problem, solution: row.id });
      if (n !== this.token || snap === null) {
        return;
      }
      this.row = got.row;
      this.pieces = piecesOf(got.placements, this.host.detail(), store);
      this.host.changed();
      const { extent, centring } = this.frame();
      const scale = cellScale(snap.grid);
      // a disc is placed by its centre (ensureHotspot), as the engine's poses place it
      const discs = snap.grid === 'stacking';
      const cache = this.host.view.meshCache;
      const replies = await Promise.all(
        this.pieces.map((p) =>
          this.mode(p.piece) === 'hidden'
            ? Promise.resolve(null)
            : cache.get(p.shape, this.styleOf(p.piece), snap.revision, p.transformation),
        ),
      );
      if (n !== this.token) {
        return;
      }
      const built: (BuiltMesh | null)[] = replies.map((r, i) => {
        const p = this.pieces[i];
        if (r === null || p === undefined) {
          return null;
        }
        const size = snap.shapes.find((s) => s.id === p.shape)?.size ?? [1, 1, 1];
        return buildMesh(meshArrays(r), size, this.styleOf(p.piece), {
          kind: 'shape',
          shape: p.shape,
          revision: snap.revision,
          transformation: p.transformation,
        });
      });
      const poses: ScenePose[] = this.pieces.map((p, i) => {
        const size = snap.shapes.find((s) => s.id === p.shape)?.disc ?? 1;
        const hotspot: [number, number, number] = discs
          ? [size, size, 0]
          : [...(replies[i]?.hotspot ?? [0, 0, 0])];
        return {
          t: placedAt(p.at, hotspot, scale, centring),
          q: [0, 0, 0, 1],
          visible: true,
          opacity: 1,
        };
      });
      this.disposeAssembled();
      this.assembledMeshes = built.filter((b): b is BuiltMesh => b !== null);
      this.setModel(n, extent, built, 'assembled');
      this.host.view.poseSolution(poses);
      if (this.shown !== 'playback') {
        this.shown = 'assembled';
      }
      this.mark();
      // the playback once the choice rests
      window.clearTimeout(this.openTimer);
      this.opening = 'timer';
      this.openTimer = window.setTimeout(() => {
        if (n !== this.token) {
          return;
        }
        // not if the list changed meanwhile (the solution may be gone): the
        // list's next reading shows it again
        if (!this.host.current(row.id)) {
          this.opening = 'none';
          return;
        }
        this.opening = 'asked';
        void this.openPlayback(n, problem, row.id);
      }, OPEN_AFTER_MS);
    } catch (e) {
      if (n === this.token && !quiet(e)) {
        reportError(e, 'The solution could not be shown');
      }
    }
  }

  private async openPlayback(n: number, problem: string, solution: string): Promise<void> {
    const store = this.host.store;
    try {
      const pb = await store.engine.call('playback.open', { problem, solution });
      if (n !== this.token) {
        store.engine.call('playback.close', { handle: pb.handle }).catch(() => undefined);
        return;
      }
      this.handle = pb.handle;
      this.steps = pb.steps;
      this.movesText = pb.movesText;
      this.order = pb.pieces.map((p) => p.piece);
      this.track = new PoseTrack(pb.pieces.length, pb.steps);
      this.t = Math.min(this.t, pb.steps);
      await this.showPlayback(n);
    } catch (e) {
      if (n === this.token && !quiet(e)) {
        reportError(e, 'The disassembly could not be opened');
      }
    } finally {
      if (n === this.token) {
        this.opening = 'none';
      }
    }
  }

  /**
   * Holds a playback not asked for yet (a removal or an abort is about to
   * change the list): the solution may be gone by the time it would be
   * asked for; the list's next reading opens it again if it is still there.
   */
  holdOpen(): void {
    if (this.opening === 'timer') {
      window.clearTimeout(this.openTimer);
      this.opening = 'none';
    }
  }

  // the playback's meshes in their styles and the poses at t
  private async showPlayback(n: number): Promise<void> {
    const store = this.host.store;
    const h = this.handle;
    const track = this.track;
    const snap = store.snapshot;
    if (h === null || track === null || snap === null) {
      return;
    }
    const built = await Promise.all(
      this.order.map(async (piece, i): Promise<BuiltMesh | null> => {
        if (this.mode(piece) === 'hidden') {
          return null;
        }
        const style = this.styleOf(piece);
        const key = `${String(piece)}:${style}`;
        const have = this.playMeshes.get(key);
        if (have !== undefined) {
          return have;
        }
        const r = await store.engine.call('playback.mesh', { handle: h, piece: i, style });
        const shape = this.pieces.find((p) => p.piece === piece)?.shape;
        const size = snap.shapes.find((s) => s.id === shape)?.size ?? [1, 1, 1];
        const b = buildMesh(meshArrays(r), size, style, { kind: 'playback', handle: h, piece: i });
        if (n !== this.token || this.handle !== h) {
          b.faces.dispose();
          b.marks?.dispose();
          return null;
        }
        this.playMeshes.set(key, b);
        return b;
      }),
    ).catch((e: unknown) => {
      if (n === this.token && !quiet(e)) {
        reportError(e, 'The pieces could not be drawn');
      }
      return null;
    });
    if (built === null || n !== this.token) {
      return;
    }
    await this.ensureChunks(n, this.t);
    if (n !== this.token) {
      return;
    }
    this.setModel(n, this.frame().extent, built, 'playback');
    this.shown = 'playback';
    this.disposeAssembled();
    this.pose();
    this.mark();
    this.host.changed();
    if (this.pendingPlay === this.row?.id) {
      this.pendingPlay = null;
      if (this.steps > 0 && !this.playing) {
        this.t = 0;
        this.togglePlay();
      }
    }
  }

  /**
   * What the view frames (the result's extent, a stacking board's) and what
   * the pieces are centred on, as an extent (placedAt takes its half): the
   * result's, or twice a stacking board's middle.
   */
  private frame(): { extent: [number, number, number]; centring: [number, number, number] } {
    const snap = this.host.store.snapshot;
    const board = this.host.board();
    if (snap?.grid === 'stacking' && board !== null) {
      const c = board.centre;
      return { extent: boardExtent(board.layout), centring: [2 * c[0], 2 * c[1], 2 * c[2]] };
    }
    const resultId = this.host.detail()?.result ?? null;
    const result = snap?.shapes.find((s) => s.id === resultId);
    const extent = gridExtent(snap?.grid ?? 'bricks', result?.size ?? [1, 1, 1]);
    return { extent, centring: extent };
  }

  // the model of the view: the pieces in the playback's order or the assembled view's
  private setModel(
    n: number,
    extent: [number, number, number],
    built: (BuiltMesh | null)[],
    kind: 'assembled' | 'playback',
  ): void {
    const byPiece = new Map(this.pieces.map((p) => [p.piece, p]));
    const numbers = kind === 'playback' ? this.order : this.pieces.map((p) => p.piece);
    const model: SolutionModel = {
      key: `${String(this.generation)}:${this.problem ?? ''}:${this.row?.id ?? ''}:${kind}:${String(n)}`,
      extent,
      result: this.host.detail()?.result ?? null,
      board: this.host.store.snapshot?.grid === 'stacking' ? this.host.board() : null,
      pieces: numbers.map((piece, i) => {
        const p = byPiece.get(piece);
        return {
          piece,
          shape: p?.shape ?? '',
          colour: p?.colour ?? [1, 1, 1],
          mode: this.mode(piece),
          built: built[i] ?? null,
        };
      }),
    };
    this.host.view.setSolution(model);
  }

  // ---- the poses ----

  /** Loads the chunks the pose at `t` needs (and the next one, ahead of play). */
  private async ensureChunks(n: number, t: number): Promise<void> {
    const track = this.track;
    const h = this.handle;
    if (track === null || h === null) {
      return;
    }
    const want = track.missing(t);
    const ahead = PoseTrack.chunkOf(t) + 1;
    const prefetch =
      ahead < track.chunkCount && !track.has(ahead) && !this.loadingChunks.has(ahead);
    const loads = want
      .filter((c) => !this.loadingChunks.has(c))
      .map((c) => this.loadChunk(n, h, c));
    if (prefetch) {
      void this.loadChunk(n, h, ahead);
    }
    await Promise.all(loads);
  }

  private async loadChunk(n: number, h: string, c: number): Promise<void> {
    const track = this.track;
    if (track === null) {
      return;
    }
    this.loadingChunks.add(c);
    try {
      const { from, to } = track.range(c);
      const r = await this.host.store.engine.call('playback.poses', {
        handle: h,
        from,
        to,
        samplesPerStep: SAMPLES,
      });
      if (n === this.token && this.track === track) {
        track.add(c, poseArrays(r, 'step'));
      }
    } catch (e) {
      if (n === this.token && !quiet(e)) {
        reportError(e, 'The moves could not be read');
      }
    } finally {
      this.loadingChunks.delete(c);
    }
  }

  /** Shows the poses at t (loading what they need first). */
  private pose(): void {
    const track = this.track;
    if (track === null || this.shown !== 'playback') {
      return;
    }
    const poses = track.at(this.t, prefs.fade);
    if (poses === null) {
      const n = this.token;
      void this.ensureChunks(n, this.t).then(() => {
        if (n === this.token) {
          this.pose();
        }
      });
      return;
    }
    this.host.view.poseSolution(poses);
    this.mark();
  }

  // ---- playing and seeking (the mockup's svTogglePlay, svSeek, svPlayLoop) ----

  /** Plays from the start once the playback is open (a double-click on a row). */
  playWhenReady(): void {
    const id = this.row?.id;
    if (id === undefined || this.row?.disassembly !== 'tree') {
      return;
    }
    if (this.ready) {
      this.t = 0;
      this.togglePlay();
      return;
    }
    this.pendingPlay = id;
  }

  /** Play or pause; a solution without moves says why it cannot play. */
  togglePlay(): void {
    const row = this.row;
    if (row === null) {
      return;
    }
    if (!this.ready || this.steps === 0) {
      if (this.ready || row.disassembly !== 'tree') {
        flash(noMovesFlash(row, this.host.inseparable(row.id)));
      }
      return;
    }
    this.playing = !this.playing;
    if (this.playing && this.t >= this.steps) {
      this.t = 0;
    }
    this.loop();
    this.mark();
    this.host.changed();
  }

  /** Seeks: 'start', 'end', or one move back (-1) or forward (+1). */
  seek(to: 'start' | 'end' | -1 | 1): void {
    if (this.row === null) {
      return;
    }
    if (!this.ready || this.steps === 0) {
      flash('No disassembly to step through');
      return;
    }
    this.playing = false;
    const s = this.steps;
    this.t =
      to === 'start'
        ? 0
        : to === 'end'
          ? s
          : to < 0
            ? Math.max(0, Math.ceil(this.t - 1e-9) - 1)
            : Math.min(s, Math.floor(this.t + 1e-9) + 1);
    this.pose();
    this.mark();
    this.host.changed();
  }

  /** The seek bar was dragged to a time. */
  scrub(t: number): void {
    if (!this.ready) {
      return;
    }
    this.playing = false;
    this.t = Math.max(0, Math.min(this.steps, t));
    this.pose();
    this.syncBar();
  }

  /** Pauses playback (Esc); true when it was playing. */
  pause(): boolean {
    if (!this.playing) {
      return false;
    }
    this.stop();
    this.mark();
    this.host.changed();
    return true;
  }

  private stop(): void {
    this.playing = false;
    cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  private loop(): void {
    cancelAnimationFrame(this.raf);
    if (!this.playing) {
      return;
    }
    let last = performance.now();
    const step = (now: number): void => {
      if (!this.playing) {
        return;
      }
      const dt = Math.min(0.1, (now - last) / 1000);
      last = now;
      const track = this.track;
      const next = Math.min(this.steps, this.t + dt * MOVES_PER_SECOND * this.speed);
      // a chunk not come yet holds the time where it is
      if (track !== null && track.at(next, prefs.fade) !== null) {
        this.t = next;
        void this.ensureChunks(this.token, this.t);
      } else {
        void this.ensureChunks(this.token, next);
      }
      this.pose();
      this.syncBar();
      if (this.t >= this.steps) {
        this.playing = false;
        this.mark();
        this.host.changed();
      } else {
        this.raf = requestAnimationFrame(step);
      }
    };
    this.raf = requestAnimationFrame(step);
  }

  // the seek bar and its label while playing or scrubbing (no re-render)
  private syncBar(): void {
    const bar = document.getElementById('slvMove') as HTMLInputElement | null;
    if (bar !== null) {
      bar.value = String(this.t);
    }
    const lbl = document.querySelector('#splayer .lbl');
    if (lbl !== null) {
      lbl.textContent = `Move ${this.t.toFixed(1)} / ${String(this.steps)}`;
    }
    this.mark();
  }

  /** The player's state, readable from outside (the e2e tests). */
  private mark(): void {
    const el = document.getElementById('splayer');
    if (el === null) {
      return;
    }
    el.dataset.solution = this.row?.id ?? '';
    el.dataset.ready = this.ready ? '1' : '0';
    el.dataset.steps = String(this.steps);
    el.dataset.t = this.t.toFixed(3);
    el.dataset.playing = this.playing ? '1' : '0';
    el.dataset.handle = this.handle ?? '';
    byId('vp').dataset.solutionView = this.shown;
  }

  private disposeAssembled(): void {
    for (const b of this.assembledMeshes) {
      b.faces.dispose();
      b.marks?.dispose();
    }
    this.assembledMeshes = [];
  }

  private disposeMeshes(): void {
    this.disposeAssembled();
    for (const b of this.playMeshes.values()) {
      b.faces.dispose();
      b.marks?.dispose();
    }
    this.playMeshes.clear();
  }
}

/** The flash of a solution that cannot play (the mockup's svTogglePlay). */
export function noMovesFlash(row: SolutionRow, inseparable: boolean): string {
  if (inseparable) {
    return 'This assembly cannot be taken apart';
  }
  return row.disassembly === 'summary'
    ? 'Only the level was kept — the moves were not stored'
    : 'No disassembly stored — use Analyse disassembly';
}

// the placed pieces of a solution: their shapes (pieces are numbered part
// by part), labels and colours (the original's per copy)
function piecesOf(
  placements: readonly Placement[],
  detail: ProblemDetail | null,
  store: Store,
): PlayerPiece[] {
  const parts = detail?.parts ?? [];
  const snap = store.snapshot;
  const out: PlayerPiece[] = [];
  let first = 0;
  for (const part of parts) {
    const i = store.indexOf(part.shape);
    const s = snap?.shapes[i];
    const base = shapeColour(s?.colour ?? null, i);
    for (let q = 0; q < part.max; q++) {
      const pl = placements[first + q];
      if (pl?.placed === true) {
        out.push({
          piece: pl.piece,
          shape: part.shape,
          label: (s?.label ?? part.shape) + (part.max > 1 ? `.${String(q + 1)}` : ''),
          colour: pieceSubColour(i, q, base),
          transformation: pl.transformation,
          at: [pl.at[0], pl.at[1], pl.at[2]],
        });
      }
    }
    first += part.max;
  }
  return out;
}

// a failure nobody needs to hear of: a newer read took its place, the
// solution or the engine went
function quiet(e: unknown): boolean {
  return (
    e instanceof EngineError &&
    (e.errc === 'superseded' || e.errc === 'not_found' || e.errc === 'disconnected')
  );
}
