// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's pointer (cleanup item 13.3): navigated as the mockup's view
// (docs/mockup/index.html: the canvas's pointer handlers: a drag past 4 px
// orbits or pans by the mode, Shift the other, the middle button pans; the
// wheel zooms, reversed by the setting), with the original's arc-ball when
// the setting says so; and picking: a point under the mouse names its cell
// and side in the status bar (picking.ts); with an edit host (the Entities
// workspace) the point under the mouse and a click (not a drag) go to it,
// with the modifiers held, for 3D editing and its preview. In the Puzzle
// workspace a click on a piece of the pieces scene selects it; in the
// Solver's, an Alt-drag on a piece moves it while the movement explorer
// takes it. One ray is cast an event (cleanup item 10.1).

import { Box3, Matrix4, Raycaster, Vector2, Vector3 } from 'three';
import type { Ray } from 'three';

import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { tab } from '../shell/layout.ts';
import { prefs } from '../shell/prefs.ts';
import { setCursor } from '../shell/status.ts';
import type { Camera } from './camera.ts';
import { isCubeGrid } from './extent.ts';
import { layerAxis } from './overlays.ts';
import type { Plane } from './overlays.ts';
import { describePick, pickAt } from './picking.ts';
import type { Pick } from './picking.ts';
import type { PiecesScene } from './pieces.ts';
import type { ViewRenderer } from './renderer.ts';
import type { ShapeScene } from './scene.ts';
import type { SolutionScene } from './solution.ts';
import type { NavMode } from './toolbar.ts';

type V3 = [number, number, number];

/** The modifiers of a pointer event or key (Cmd counts as Alt, as the mockup's `mods`). */
export interface ViewMods {
  ctrl: boolean;
  shift: boolean;
  alt: boolean;
}

/** What 3D editing does with the view's points (the Entities workspace). */
export interface EditHost {
  /**
   * The face under the mouse changed, or the modifiers did (null: none);
   * `over` says whether the mouse is over the shape's space (its grid box,
   * the empty cells too), and `layerCell` is the cell of the editor's layer
   * under it (the cube grids; null: none), where a click on no face adds.
   */
  hover(pick: Pick | null, mods: ViewMods, over: boolean, layerCell: V3 | null): void;
  /** A click (no drag) on a face, or on nothing; true when it was taken. */
  click(pick: Pick | null, mods: ViewMods, layerCell: V3 | null): boolean;
  /** The Edit button. */
  toggle(): void;
}

/** What the pointer reads of the view, and whom it tells. */
export interface PointerHost {
  readonly canvas: HTMLCanvasElement;
  readonly camera: Camera;
  readonly renderer: ViewRenderer;
  readonly store: Store;
  /** The shape view, the Puzzle workspace's pieces scene, the Solver's solution. */
  readonly scene: ShapeScene;
  readonly pieces: PiecesScene;
  readonly solution: SolutionScene;
  /** The navigation mode (orbit or pan). */
  mode(): NavMode;
  /** The shape's space in mesh units; the solution's. */
  extent(): V3;
  solutionExtent(): V3;
  /** Whether the Entities workspace shows a rod set instead of its disc. */
  rodSetShown(): boolean;
  /** The voxel editor's plane and layer. */
  plane(): Plane;
  layer(): number;
  /** 3D editing, when a workspace does it. */
  edit(): EditHost | null;
  /** A piece was clicked in the pieces scene (the Puzzle workspace). */
  pick(shape: string): void;
  /** Whether an Alt-drag on a piece moves it now (the movement explorer), not the view. */
  movesPieces(): boolean;
  /** A piece was Alt-dragged by (dx, dy) CSS pixels on the screen. */
  pieceDragged(piece: number, dx: number, dy: number): void;
  /** The view's mouse cursor (empty: the navigation's). */
  setCursor(cursor: string): void;
}

const modsOf = (e: {
  ctrlKey: boolean;
  shiftKey: boolean;
  altKey: boolean;
  metaKey: boolean;
}): ViewMods => ({
  ctrl: e.ctrlKey,
  shift: e.shiftKey,
  alt: e.altKey || e.metaKey,
});

/** The ray under the pointer, cast once an event: its point, and the ray in the model's units. */
interface PointerRay {
  ndc: Vector2;
  local: Ray;
}

interface Drag {
  x: number;
  y: number;
  sx: number;
  sy: number;
  moved: boolean;
  btn: number;
  /** A piece moved by an Alt-drag (the movement explorer) instead of the view. */
  m: 'orbit' | 'pan' | 'piece';
  piece: number;
}

/** The view's pointer handlers and picking. */
export class ViewPointer {
  /** The last pick under the mouse, or null. */
  lastPick: Pick | null = null;
  private readonly host: PointerHost;
  /** Whether the mouse was last over the shape's space (its grid box). */
  private overSpace = false;
  /** The editor's layer's cell last under the mouse (the cube grids). */
  private lastLayerCell: V3 | null = null;
  private mods: ViewMods = { ctrl: false, shift: false, alt: false };
  private drag: Drag | null = null;
  private hoverAt: { x: number; y: number } | null = null;
  /** The pointer's ray (one for the view) and the model's inverse matrix it is taken into. */
  private readonly raycaster = new Raycaster();
  private readonly toModel = new Matrix4();

  constructor(host: PointerHost) {
    this.host = host;
    this.install();
  }

  /** The modifiers held changed (a key): the hover is told again. */
  modsChanged(m: ViewMods): void {
    this.mods = m;
    if (this.hoverAt !== null && this.drag === null && tab === 'entities') {
      this.host.edit()?.hover(this.lastPick, m, this.overSpace, this.lastLayerCell);
    }
  }

  /** The point under a still mouse picked again (the shape's mesh changed under it). */
  repick(): void {
    if (this.hoverAt !== null && this.drag === null) {
      this.showPickAt(this.hoverAt.x, this.hoverAt.y);
    }
  }

  private get puzzleMode(): boolean {
    return tab === 'puzzle';
  }

  private get solverMode(): boolean {
    return tab === 'solver';
  }

  // the piece of the solution shown under a point
  private solutionPieceAt(clientX: number, clientY: number): number | null {
    const ndc = this.ndcOf(clientX, clientY);
    this.host.renderer.frame(this.host.camera, this.host.solutionExtent());
    this.raycaster.setFromCamera(ndc, this.host.renderer.camera);
    return this.host.solution.pick(this.raycaster);
  }

  // the piece under a point in the pieces scene (the mockup's puzPick)
  private pieceAt(clientX: number, clientY: number): string | null {
    const r = this.host.canvas.getBoundingClientRect();
    return this.host.pieces.pick(clientX - r.left, clientY - r.top);
  }

  private hoverPiece(shape: string | null): void {
    if (this.host.pieces.setHover(shape)) {
      this.host.canvas.style.cursor = shape === null ? '' : 'pointer';
    }
  }

  // a point of the canvas in normalized device coordinates
  private ndcOf(clientX: number, clientY: number): Vector2 {
    const r = this.host.canvas.getBoundingClientRect();
    return new Vector2(
      ((clientX - r.left) / r.width) * 2 - 1,
      -(((clientY - r.top) / r.height) * 2 - 1),
    );
  }

  // the ray under a point, the shape framed: in the scene (the raycaster's)
  // and in the model's units (cleanup item 10.1: one ray an event, which
  // the face, the space and the layer's cell share)
  private castAt(clientX: number, clientY: number): PointerRay {
    const renderer = this.host.renderer;
    const ndc = this.ndcOf(clientX, clientY);
    renderer.frame(this.host.camera, this.host.extent());
    this.raycaster.setFromCamera(ndc, renderer.camera);
    const local = this.raycaster.ray
      .clone()
      .applyMatrix4(this.toModel.copy(renderer.model.matrixWorld).invert());
    return { ndc, local };
  }

  private pickOf(c: PointerRay): Pick | null {
    if (this.puzzleMode || this.solverMode) {
      return null;
    }
    const scene = this.host.scene;
    const target = scene.target;
    const mesh = scene.mesh;
    if (target === null || mesh === null) {
      return null;
    }
    return pickAt(
      this.raycaster,
      c.ndc,
      this.host.renderer.camera,
      target,
      mesh,
      scene.insideShown,
    );
  }

  // whether a point is over the shape's space (its grid box), where an
  // empty cell may be: 3D editing's cursor shows there, not only on faces
  private spaceOf(c: PointerRay): boolean {
    if (
      this.puzzleMode ||
      this.solverMode ||
      this.host.rodSetShown() ||
      this.host.store.shape === null
    ) {
      return false;
    }
    return c.local.intersectsBox(
      new Box3(new Vector3(0, 0, 0), new Vector3(...this.host.extent())),
    );
  }

  // the cell of the editor's layer under a point, on the cube grids (whose
  // mesh units are cells): the middle of the ray's run through the layer's
  // slab, so a shape with no cells there yet can be drawn on in 3D
  private layerCellOf(c: PointerRay): V3 | null {
    const grid = this.host.store.snapshot?.grid;
    if (
      this.puzzleMode ||
      this.solverMode ||
      this.host.rodSetShown() ||
      this.host.store.shape === null ||
      !isCubeGrid(grid)
    ) {
      return null;
    }
    const layer = this.host.layer();
    const ai = layerAxis(this.host.plane());
    const lo = new Vector3(0, 0, 0);
    const hi = new Vector3(...this.host.extent());
    if (layer >= hi.getComponent(ai)) {
      return null;
    }
    lo.setComponent(ai, layer);
    hi.setComponent(ai, layer + 1);
    const slab = new Box3(lo, hi);
    const ray = c.local.clone();
    const enter = ray.intersectBox(slab, new Vector3());
    if (enter === null) {
      return null;
    }
    // the way out: the same line from beyond the slab, backwards
    const far = slab.getSize(new Vector3()).length() + enter.distanceTo(ray.origin) + 1;
    ray.origin.addScaledVector(ray.direction, far);
    ray.direction.negate();
    const leave = ray.intersectBox(slab, new Vector3()) ?? enter;
    const mid = enter.add(leave).multiplyScalar(0.5);
    const cell = (i: number): number =>
      Math.min(Math.max(Math.floor(mid.getComponent(i)), 0), hi.getComponent(i) - 1);
    return [cell(0), cell(1), cell(2)];
  }

  private showPick(p: Pick | null, over: boolean, layerCell: V3 | null): void {
    this.lastPick = p;
    this.overSpace = over;
    this.lastLayerCell = layerCell;
    const grid = this.host.store.snapshot?.grid;
    setCursor(p === null ? '' : describePick(p, isCubeGrid(grid)));
    byId('vp').dataset.quarter = p?.quarter ?? '';
    // 3D editing is the Entities workspace's: elsewhere its cursor would
    // replace the view's own (the Solver's Alt-hover grab, cleanup item 10.1)
    if (tab === 'entities') {
      this.host.edit()?.hover(p, this.mods, over, layerCell);
    }
  }

  // the face, the space and the layer's cell under a point
  private showPickAt(clientX: number, clientY: number): void {
    const c = this.castAt(clientX, clientY);
    this.showPick(this.pickOf(c), this.spaceOf(c), this.layerCellOf(c));
  }

  // ---- navigation (the mockup's pointer handlers) ----

  private install(): void {
    const host = this.host;
    const camera = host.camera;
    const cv = host.canvas;
    cv.addEventListener('contextmenu', (e) => {
      e.preventDefault();
    });
    cv.addEventListener('pointerdown', (e) => {
      camera.stop();
      if (e.button === 2) {
        return;
      }
      if (e.button === 1) {
        e.preventDefault();
      }
      cv.setPointerCapture(e.pointerId);
      const mode = host.mode();
      const other = mode === 'orbit' ? 'pan' : 'orbit';
      // an Alt-drag on a piece moves it (the movement explorer)
      const piece =
        e.button === 0 && modsOf(e).alt && this.solverMode && host.movesPieces()
          ? this.solutionPieceAt(e.clientX, e.clientY)
          : null;
      this.drag = {
        x: e.clientX,
        y: e.clientY,
        sx: e.clientX,
        sy: e.clientY,
        moved: false,
        btn: e.button,
        m: piece !== null ? 'piece' : e.button === 1 ? 'pan' : e.shiftKey ? other : mode,
        piece: piece ?? -1,
      };
    });
    cv.addEventListener('pointermove', (e) => {
      const d = this.drag;
      this.mods = modsOf(e);
      if (d === null) {
        this.hoverAt = { x: e.clientX, y: e.clientY };
        if (this.puzzleMode) {
          this.hoverPiece(this.pieceAt(e.clientX, e.clientY));
          return;
        }
        if (this.solverMode && host.movesPieces()) {
          // the piece an Alt-drag would move
          const hp = this.solutionPieceAt(e.clientX, e.clientY);
          const vp = byId('vp');
          const shown = hp === null ? '' : String(hp);
          if (vp.dataset.hoverPiece !== shown) {
            vp.dataset.hoverPiece = shown;
          }
          host.setCursor(hp !== null && this.mods.alt ? 'grab' : '');
        }
        this.showPickAt(e.clientX, e.clientY);
        return;
      }
      if (!d.moved) {
        if (Math.hypot(e.clientX - d.sx, e.clientY - d.sy) <= 4) {
          return;
        }
        d.moved = true;
        // a drag navigates: no edit preview while it lasts
        if (tab === 'entities') {
          host.edit()?.hover(null, this.mods, false, null);
        }
        d.x = e.clientX;
        d.y = e.clientY;
        if (d.m === 'orbit' && prefs.rot === 'Free Spin') {
          const r = cv.getBoundingClientRect();
          camera.arcStart(d.sx - r.left, d.sy - r.top, r.width, r.height);
        }
      }
      const dx = e.clientX - d.x;
      const dy = e.clientY - d.y;
      d.x = e.clientX;
      d.y = e.clientY;
      if (d.m === 'piece') {
        return;
      }
      if (d.m === 'orbit') {
        if (prefs.rot === 'Free Spin') {
          const r = cv.getBoundingClientRect();
          camera.arcDrag(e.clientX - r.left, e.clientY - r.top);
        } else {
          camera.orbit(dx, dy);
        }
      } else {
        camera.pan(dx, dy);
      }
      host.renderer.request();
    });
    cv.addEventListener('pointerup', (e) => {
      const was = this.drag;
      this.drag = null;
      camera.arcEnd();
      this.mods = modsOf(e);
      if (was?.m === 'piece') {
        if (was.moved) {
          host.pieceDragged(was.piece, e.clientX - was.sx, e.clientY - was.sy);
        }
      } else if (was !== null && was.btn === 0 && !was.moved && this.puzzleMode) {
        // a click on a piece selects it (the mockup's puzClick)
        const shape = this.pieceAt(e.clientX, e.clientY);
        if (shape !== null) {
          host.pick(shape);
        }
      } else if (was !== null && was.btn === 0 && !was.moved) {
        // a click: the cell and side under it, edited when the workspace edits
        const c = this.castAt(e.clientX, e.clientY);
        const p = this.pickOf(c);
        const lc = this.layerCellOf(c);
        host.edit()?.click(p, this.mods, lc);
        this.showPick(p, this.spaceOf(c), lc);
      } else if (was?.moved === true) {
        this.repick();
      }
    });
    cv.addEventListener('pointerleave', () => {
      if (this.drag === null) {
        this.hoverAt = null;
        this.hoverPiece(null);
        this.showPick(null, false, null);
      }
    });
    cv.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        const dy = e.deltaY * (e.deltaMode === 1 ? 33 : 1);
        camera.zoomBy(Math.exp(-dy * (prefs.rev ? -1 : 1) * 0.0016), performance.now());
        host.renderer.request();
      },
      { passive: false },
    );
  }
}
