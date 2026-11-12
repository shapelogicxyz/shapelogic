// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver view's live preview: while a run of the problem shown
// searches, the arrangements it is looking at (solve.preview, a few times
// a second: a sliding or stacking search's, or a take-apart's), drawn
// under the player and the explorers (the view's 'preview' layer, shown
// while neither has a model). Each piece is its shape's mesh in the
// placement's transformation (the view's mesh cache), placed as the
// player places an assembled solution; a picture that keeps every piece's
// transformation only moves them (poseSolution), one that turns a piece
// fetches that mesh first.

import type { BoardLayout, MeshStyle, ProblemDetail } from '../../engine/api.generated.ts';
import type { EventParams } from '../../engine/client-types.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { placedAt } from '../view3d/extent.ts';
import { buildMesh } from '../view3d/mesh.ts';
import type { BuiltMesh } from '../view3d/mesh.ts';
import type { ScenePose, SolutionModel } from '../view3d/solution.ts';
import type { View3D } from '../view3d/view.ts';
import { problemPieces, solutionFrame } from './player.ts';
import type { ProblemPiece } from './player.ts';

/** A picture of a run's search. */
export type Shot = EventParams<'solve.preview'>;

/** What the preview asks of its workspace. */
export interface LivePreviewHost {
  readonly store: Store;
  readonly view: View3D;
  /** The problem shown (its parts number the pieces). */
  detail(): ProblemDetail | null;
  /** A stacking problem's board and the middle its discs are placed around (null: none). */
  board(): { layout: BoardLayout; centre: [number, number, number] } | null;
  /** The preview was drawn or taken away (the view's message, the tag). */
  changed(): void;
}

/** A piece's mesh in the transformation it was last drawn in. */
interface Drawn {
  transformation: number;
  built: BuiltMesh;
  hotspot: [number, number, number];
}

export class LivePreview {
  private readonly host: LivePreviewHost;
  /** What the meshes are of: the document's generation and revision, the problem, the style. */
  private key = '';
  private pieces: ProblemPiece[] = [];
  private readonly drawn = new Map<number, Drawn>();
  /** The picture to draw next, and whether a drawing is under way. */
  private pending: Shot | null = null;
  private drawing = false;
  private token = 0;
  private models = 0;
  /** Whether the preview layer has a model. */
  private hasModel = false;

  constructor(host: LivePreviewHost) {
    this.host = host;
  }

  /** Whether a picture is drawn (on the preview layer; the player's or an explorer's may cover it). */
  get drawnNow(): boolean {
    return this.hasModel;
  }

  /** Shows a picture of a run of the problem shown; null takes the preview away. */
  show(shot: Shot | null): void {
    if (shot === null) {
      this.clear();
      return;
    }
    this.pending = shot;
    if (!this.drawing) {
      void this.draw();
    }
  }

  /** Takes the preview away and forgets its meshes. */
  clear(): void {
    this.token++;
    this.pending = null;
    this.drawing = false;
    const had = this.hasModel;
    if (had) {
      this.host.view.setSolution(null, 'preview');
      this.hasModel = false;
    }
    this.dispose();
    this.key = '';
    if (had) {
      this.host.changed();
    }
  }

  /** The view's style changed: the meshes again, at the next picture. */
  restyle(): void {
    this.key = '';
  }

  private async draw(): Promise<void> {
    const n = this.token;
    this.drawing = true;
    try {
      for (let shot = this.pending; shot !== null; shot = this.pending) {
        this.pending = null;
        await this.drawShot(n, shot);
        if (n !== this.token) {
          return;
        }
      }
    } finally {
      if (n === this.token) {
        this.drawing = false;
      }
    }
  }

  private async drawShot(n: number, shot: Shot): Promise<void> {
    const host = this.host;
    const store = host.store;
    const snap = store.snapshot;
    if (snap === null) {
      return;
    }
    const style: MeshStyle = host.view.display.style;
    const key = `${String(store.generation)}:${String(snap.revision)}:${shot.problem}:${style}`;
    if (key !== this.key) {
      // another problem, document, revision or style: everything again
      if (this.hasModel) {
        host.view.setSolution(null, 'preview');
        this.hasModel = false;
      }
      this.dispose();
      this.key = key;
      this.pieces = problemPieces(host.detail(), store);
    }
    // the pieces placed in a transformation not drawn yet
    const wanted = shot.placements.filter(
      (pl) =>
        pl.placed &&
        this.pieces[pl.piece] !== undefined &&
        this.drawn.get(pl.piece)?.transformation !== pl.transformation,
    );
    const discs = snap.grid === 'stacking';
    let changed = !this.hasModel;
    if (wanted.length > 0) {
      const cache = host.view.meshCache;
      const replies = await Promise.all(
        wanted.map((pl) =>
          cache.get(this.pieces[pl.piece]?.shape ?? '', style, snap.revision, pl.transformation),
        ),
      ).catch(() => null);
      if (n !== this.token || key !== this.key || replies === null) {
        return;
      }
      const gone: BuiltMesh[] = [];
      wanted.forEach((pl, i) => {
        const r = replies[i];
        const p = this.pieces[pl.piece];
        if (r === undefined || p === undefined) {
          return;
        }
        const shape = snap.shapes.find((s) => s.id === p.shape);
        const size = shape?.size ?? [1, 1, 1];
        const disc = shape?.disc ?? 1;
        const built = buildMesh(meshArrays(r), size, style, {
          kind: 'shape',
          shape: p.shape,
          revision: snap.revision,
          transformation: pl.transformation,
        });
        const was = this.drawn.get(pl.piece);
        if (was !== undefined) {
          gone.push(was.built);
        }
        this.drawn.set(pl.piece, {
          transformation: pl.transformation,
          built,
          // a disc is placed by its centre (ensureHotspot), as the player places it
          hotspot: discs ? [disc, disc, 0] : [...r.hotspot],
        });
      });
      this.setModel();
      for (const b of gone) {
        b.faces.dispose();
        b.marks?.dispose();
      }
      changed = true;
    }
    if (!this.hasModel) {
      return;
    }
    const { centring } = solutionFrame(store, host.detail(), host.board());
    const at = new Map(shot.placements.map((pl) => [pl.piece, pl]));
    const poses: ScenePose[] = [...this.drawn.entries()].map(([piece, d]) => {
      const pl = at.get(piece);
      const placed = pl?.placed === true && pl.transformation === d.transformation;
      return {
        t: placed ? placedAt(pl.at, d.hotspot, snap.grid, centring) : [0, 0, 0],
        q: [0, 0, 0, 1],
        visible: placed,
        opacity: 1,
      };
    });
    host.view.poseSolution(poses, 'preview');
    if (changed) {
      host.changed();
    }
  }

  // the preview layer's model: each piece drawn so far, in the order first
  // drawn (the poses' order)
  private setModel(): void {
    const host = this.host;
    const detail = host.detail();
    const stacking = host.store.snapshot?.grid === 'stacking';
    const board = stacking ? host.board() : null;
    const model: SolutionModel = {
      key: `preview:${this.key}:${String(++this.models)}`,
      extent: solutionFrame(host.store, detail, board).extent,
      result: detail?.result ?? null,
      board,
      pieces: [...this.drawn.entries()].map(([piece, d]) => {
        const p = this.pieces[piece];
        return {
          piece,
          shape: p?.shape ?? '',
          colour: p?.colour ?? [1, 1, 1],
          mode: 'solid' as const,
          built: d.built,
          ...(p === undefined ? {} : { label: p.label }),
        };
      }),
    };
    host.view.setSolution(model, 'preview');
    this.hasModel = true;
  }

  private dispose(): void {
    for (const d of this.drawn.values()) {
      d.built.faces.dispose();
      d.built.marks?.dispose();
    }
    this.drawn.clear();
  }
}
