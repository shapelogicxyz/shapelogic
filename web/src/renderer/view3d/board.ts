// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle's board in the 3D view (docs/webui-spec.md, section
// 9): the gray pegs, the base under them and, on Panex columns, the V
// behind each column, from the engine's layout (stacking.board,
// stacking.rodSetLayout); the discs placed on it where the layout says
// their centers stand. From the original's voxelframe.cpp: `addPeg` (a
// column of cells at the rod's x, 0.55/0.55/0.58), `addRodBase` (a row of
// cells one below the pegs, from the first rod to the last),
// `addPanexV` (2·maxSize+1 wide, narrowing to the bottom, just behind the
// largest disc, 0.42/0.42/0.46), `addStackBoard` and `showStacking` (a
// disc's hotspot, its center, on the layout's cell: `ensureHotspot`). The
// board is drawn in cells (the stacking grid's mesh units) centerd on the
// board's middle, as the playback's poses are (the engine's
// stackingCenter), and the view tilts it as the original's `tiltPlates`
// (renderer.ts: a disc lies flat, the rods stand up).

import { BufferAttribute, BufferGeometry, Group, Mesh, Vector3 } from 'three';

import type { BoardLayout, MeshStyle } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import type { Rgb3 } from '../shell/piece-colors.ts';
import { faceMaterial, faceUniforms, setPiece } from './material.ts';
import type { BuiltMesh } from './mesh.ts';
import { MeshSlot } from './shape-meshes.ts';
import type { MeshAsk, ShapeMeshes } from './shape-meshes.ts';
import { SolutionScene } from './solution.ts';
import type { ScenePiece, ScenePose, SolutionLook } from './solution.ts';

type V3 = [number, number, number];

/** A box of the board, in cells: from `min` to `max`, in a color. */
export interface BoardBox {
  min: V3;
  max: V3;
  color: Rgb3;
}

/** The pegs' and the base's gray (addPeg, addRodBase). */
export const PEG_GRAY: Rgb3 = [0.55, 0.55, 0.58];
/** The Panex columns' V (addPanexV). */
export const PANEX_GRAY: Rgb3 = [0.42, 0.42, 0.46];

/**
 * The boxes of a board (addStackBoard): a peg per rod, the base under them
 * from the first rod to the last, and a V behind each Panex column (not
 * the pocket), its cells merged into one box per layer.
 */
export function boardBoxes(lay: BoardLayout): BoardBox[] {
  const boxes: BoardBox[] = [];
  const n = Math.min(lay.rodCount, lay.rodX.length);
  for (let r = 0; r < n; r++) {
    const x = lay.rodX[r] ?? 0;
    const h = Math.max(1, lay.rodHeights[r] ?? lay.rodHeight);
    boxes.push({ min: [x, 0, 0], max: [x + 1, 1, h], color: PEG_GRAY });
    if (lay.panex && !(lay.pocket && r + 1 === lay.rodCount)) {
      // a disc of size s may sit s places below the top: about d wide at depth d
      const m = Math.max(1, lay.maxSize);
      for (let z = 0; z < h; z++) {
        const depth = h - z;
        const half = Math.min(m, Math.trunc((depth * m + Math.trunc(h / 2)) / h));
        // the V's cell dx stands at x - m + dx, one row behind the largest disc
        boxes.push({
          min: [x - half, m + 1, z],
          max: [x + half + 1, m + 2, z + 1],
          color: PANEX_GRAY,
        });
      }
    }
  }
  if (lay.rodX.length > 0) {
    const lo = Math.min(...lay.rodX);
    const hi = Math.max(...lay.rodX);
    boxes.push({ min: [lo, 0, -1], max: [hi + 1, 1, 0], color: PEG_GRAY });
  }
  return boxes;
}

/**
 * The middle of a board, in cells: the playback's frame (the engine's
 * stackingCenter; stacking.board's center), half the rods' height up.
 */
export function boardCenter(lay: BoardLayout): V3 {
  if (lay.rodX.length === 0) {
    return [0, 0, 0];
  }
  const lo = Math.min(...lay.rodX);
  const hi = Math.max(...lay.rodX);
  return [(lo + hi) / 2 + 0.5, 0.5, lay.rodHeight / 2];
}

/** The size the view frames a board on, in cells (the discs reach maxSize beside a rod). */
export function boardExtent(lay: BoardLayout): V3 {
  const span = lay.rodX.length === 0 ? 1 : Math.max(...lay.rodX) - Math.min(...lay.rodX) + 1;
  const m = Math.max(1, lay.maxSize);
  return [span + 2 * m, 2 * m + 1 + (lay.panex ? 2 : 0), lay.rodHeight + 2];
}

/**
 * Where a disc's mesh goes, in the board's frame: its center (the disc's
 * hotspot, ensureHotspot: size, size, 0) on the layout's cell, minus the
 * board's middle.
 */
export function discAt(at: readonly [number, number, number], size: number, center: V3): V3 {
  return [at[0] - size - center[0], at[1] - size - center[1], at[2] - center[2]];
}

// the boxes as one geometry with the face material's attributes
function boxesGeometry(boxes: readonly BoardBox[]): BufferGeometry {
  const pos: number[] = [];
  const nrm: number[] = [];
  const quad = (a: V3, b: V3, c: V3, d: V3, n: V3): void => {
    for (const p of [a, b, c, a, c, d]) {
      pos.push(...p);
      nrm.push(...n);
    }
  };
  for (const {
    min: [x0, y0, z0],
    max: [x1, y1, z1],
  } of boxes) {
    quad([x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0], [0, 0, -1]);
    quad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1]);
    quad([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0]);
    quad([x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [0, 1, 0]);
    quad([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0]);
    quad([x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1], [1, 0, 0]);
  }
  const g = new BufferGeometry();
  const count = pos.length / 3;
  g.setAttribute('position', new BufferAttribute(new Float32Array(pos), 3));
  g.setAttribute('normal', new BufferAttribute(new Float32Array(nrm), 3));
  g.setAttribute('aFlags', new BufferAttribute(new Float32Array(count), 1));
  g.setAttribute('aColor', new BufferAttribute(new Float32Array(count), 1));
  g.setAttribute('aCell', new BufferAttribute(new Float32Array(count * 3), 3));
  return g;
}

/** The pegs, the base and the Vs of a board, lit as the pieces are. */
export class BoardScene {
  readonly group = new Group();
  private meshes: Mesh[] = [];
  private lit = true;

  /** Shows a board's boxes (null: none), centerd on `center`. */
  set(lay: BoardLayout | null, center: V3): void {
    this.clear();
    if (lay === null) {
      return;
    }
    const byColor = new Map<string, BoardBox[]>();
    for (const b of boardBoxes(lay)) {
      const k = b.color.join(',');
      byColor.set(k, [...(byColor.get(k) ?? []), b]);
    }
    for (const boxes of byColor.values()) {
      const u = faceUniforms();
      // the material shows 0.9 of the piece color on a face without the checker
      const c = boxes[0]?.color ?? PEG_GRAY;
      setPiece(u, [c[0] / 0.9, c[1] / 0.9, c[2] / 0.9]);
      u.uChecker.value = 0;
      u.uLit.value = this.lit ? 1 : 0;
      const m = new Mesh(boxesGeometry(boxes), faceMaterial(u, 'normal'));
      this.meshes.push(m);
      this.group.add(m);
    }
    this.group.position.set(-center[0], -center[1], -center[2]);
  }

  /** Lit or not, as the view's pieces. */
  setLit(lit: boolean): void {
    this.lit = lit;
    for (const m of this.meshes) {
      const mat = m.material as { uniforms?: { uLit?: { value: number } } };
      if (mat.uniforms?.uLit !== undefined) {
        mat.uniforms.uLit.value = lit ? 1 : 0;
      }
    }
  }

  /** How many boxes' meshes are drawn (one per color). */
  get meshCount(): number {
    return this.meshes.length;
  }

  private clear(): void {
    for (const m of this.meshes) {
      this.group.remove(m);
      m.geometry.dispose();
      // each color's material has a palette texture of its own (faceUniforms): it goes too
      const mat = m.material as {
        dispose(): void;
        uniforms?: { uPalette?: { value: { dispose(): void } | null } };
      };
      mat.uniforms?.uPalette?.value?.dispose();
      mat.dispose();
    }
    this.meshes = [];
  }
}

/** A board and the discs on it, as the view shows them. */
export interface BoardModel {
  /** Names what is shown (with the shapes' revisions it is drawn again when they change). */
  key: string;
  layout: BoardLayout;
  /** The board's middle, in cells (stacking.board's center, or boardCenter for a rod set alone). */
  center: V3;
  /** What the view's tag names (a rod set alone: "R1 - name"). */
  label?: string;
  /** The discs to place (a rod set alone and the player's board: none). */
  discs: readonly {
    shape: string;
    size: number;
    at: readonly [number, number, number];
    color: Rgb3;
  }[];
}

/** What the board view asks of the view. */
export interface BoardHost {
  store: Store;
  meshes: ShapeMeshes;
  /** The revision a shape last changed at (its mesh is asked at it). */
  shapeRevision(shape: string): number;
  style(): MeshStyle;
  /** Something changed: draw again. */
  changed(): void;
  failed(e: unknown): void;
}

/**
 * A board with its discs (the Puzzle's start or goal, the Entities' rod set
 * alone, the player's pegs): the boxes, and the discs' meshes from the
 * engine placed at their cells. Its group sits at the root's origin.
 */
export class BoardView {
  readonly group = new Group();
  private readonly host: BoardHost;
  private readonly board = new BoardScene();
  private readonly discs = new SolutionScene();
  private model: BoardModel | null = null;
  /** The discs' meshes shown (given back when replaced). */
  private readonly slot: MeshSlot;
  private shownKey = '';

  constructor(host: BoardHost) {
    this.host = host;
    this.slot = new MeshSlot(host.meshes);
    this.group.add(this.board.group);
    this.group.add(this.discs.group);
  }

  /** The size the view frames while a board is shown (null: none). */
  get extent(): V3 | null {
    return this.model === null ? null : boardExtent(this.model.layout);
  }

  /** Whether a board is shown. */
  get shown(): boolean {
    return this.model !== null;
  }

  /** Shows a board (null: none). */
  set(model: BoardModel | null): void {
    this.model = model;
    this.load();
  }

  /** The look of the discs (the view's) and the pegs' light. */
  setLook(look: SolutionLook): void {
    this.discs.setLook(look);
    this.board.setLit(look.lit);
  }

  setResolution(w: number, h: number): void {
    this.discs.setResolution(w, h);
  }

  /** The style or a disc's mesh may have changed: load again what differs. */
  load(): void {
    const model = this.model;
    const store = this.host.store;
    if (model === null) {
      this.slot.clear(() => {
        this.show(null, new Map(), '');
      });
      return;
    }
    const style = this.host.style();
    const shapes = [...new Set(model.discs.map((d) => d.shape))];
    const key = `${String(store.generation)}:${style}:${model.key}:${shapes.map((s) => `${s}@${String(this.host.shapeRevision(s))}`).join(',')}`;
    if (key === this.shownKey) {
      // a load in flight for another key is stale now
      this.slot.stale();
      return;
    }
    const asks = shapes.map((s): MeshAsk => ({
      shape: s,
      style,
      revision: this.host.shapeRevision(s),
      size: store.snapshot?.shapes.find((x) => x.id === s)?.size ?? [1, 1, 1],
    }));
    this.slot
      .load(asks, (leases) => {
        this.show(model, new Map(leases.map((l) => [l.shape, l.built])), key);
      })
      .catch((e: unknown) => {
        this.host.failed(e);
      });
  }

  private show(
    model: BoardModel | null,
    meshOf: ReadonlyMap<string, BuiltMesh>,
    key: string,
  ): void {
    this.shownKey = key;
    if (model === null) {
      this.board.set(null, [0, 0, 0]);
      this.discs.set(null);
    } else {
      this.board.set(model.layout, model.center);
      const pieces: ScenePiece[] = [];
      const poses: ScenePose[] = [];
      for (const d of model.discs) {
        const b = meshOf.get(d.shape);
        if (b === undefined) {
          continue;
        }
        pieces.push({
          piece: pieces.length,
          shape: d.shape,
          color: d.color,
          mode: 'solid',
          built: b,
        });
        poses.push({
          t: discAt(d.at, d.size, model.center),
          q: [0, 0, 0, 1],
          visible: true,
          opacity: 1,
        });
      }
      this.discs.set({ key, extent: boardExtent(model.layout), pieces });
      this.discs.setPoses(poses);
    }
    this.host.changed();
  }

  /** What is drawn (the e2e tests read it): the discs as piece:shape:mode:on, and the boxes' meshes. */
  describe(): string {
    return this.discs.describe();
  }

  /** Where the discs stand (the e2e tests read it). */
  posesShown(): string {
    return this.discs.posesShown();
  }

  /** The board's boxes' meshes (the e2e tests read it). */
  get boxMeshes(): number {
    return this.board.meshCount;
  }

  /** A point of the board's frame (cells) in the view's root. */
  toRoot(p: V3): Vector3 {
    const c = this.model?.center ?? [0, 0, 0];
    return new Vector3(p[0] - c[0], p[1] - c[1], p[2] - c[2]);
  }
}
