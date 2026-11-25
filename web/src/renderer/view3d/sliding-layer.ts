// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding puzzle's layer of the 3D view (layer.ts): the board's START and
// GOAL trays in the Puzzle view (trays.ts), drawn with the pieces scene in a
// band under them; the tray's walls around the Solver's solution
// (walls.ts, the original's buildSlidingWalls); and the tray's border
// (decision 0046) round the board the shape view shows and round the
// Solver's pieces.

import { Quaternion, Vector3 } from 'three';

import type { ShapeSummary, TrayBorder } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { byId } from '../shell/dom.ts';
import { tab } from '../shell/layout.ts';
import { viewColor } from '../shell/piece-colors.ts';
import { borderPicture } from '../shell/piece-images.ts';
import type { PiecePicture } from '../shell/picture-math.ts';
import { prefs } from '../shell/prefs.ts';
import { reportError } from '../shell/status.ts';
import { borderColor, borderRim, floorTest, shownBorder } from '../shell/tray-border.ts';
import { HOME } from './camera.ts';
import type { LayerHost, ViewLayer } from './layer.ts';
import type { BuiltMesh } from './mesh.ts';
import type { PiecesScene } from './pieces.ts';
import type { SolutionModel, SolutionScene } from './solution.ts';
import { floorOfMesh, TrayBorderMesh } from './tray-border.ts';
import type { BorderLook } from './tray-border.ts';
import { TrayView } from './trays.ts';
import type { TrayModel } from './trays.ts';
import { wallOutline, wallSegments } from './walls.ts';

/** The most of the Puzzle view's free height a sliding board's pieces take under its trays. */
const PIECES_SHARE = 0.55;
/** The share of their space the trays fill at the home view (the rest a margin). */
const TRAY_FILL = 0.94;

/** What the sliding layer reads of the view beyond every layer's. */
export interface SlidingHost extends LayerHost {
  /** The Solver's solution scene (its walls). */
  readonly solution: SolutionScene;
  /** The Puzzle workspace's pieces scene, drawn in a band under the trays. */
  readonly pieces: PiecesScene;
  /** The shape view's mesh (a board's cells are the border's floor). */
  shapeMesh(): BuiltMesh | null;
  /** Draws the pieces scene in a band `top` pixels down. */
  drawPieces(top: number): void;
  pictureOf(shape: string): PiecePicture | null;
}

export class SlidingLayer implements ViewLayer {
  private readonly host: SlidingHost;
  /** A sliding board's START and GOAL trays and pieces (the Puzzle view). */
  private readonly trays: TrayView;
  /** Whether the Puzzle view shows a board's trays instead of the pieces scene. */
  private trayShown = false;
  private wallLoads = 0;
  /** A sliding tray's border (decision 0046): round the board shown, and round the Solver's pieces. */
  private readonly shapeBorder = new TrayBorderMesh();
  private readonly solutionBorder = new TrayBorderMesh();
  /** The Solver's result as its walls were read: its size and which cells are floor. */
  private solutionFloor: {
    shape: string;
    size: readonly [number, number, number];
    floor: (x: number, y: number) => boolean;
  } | null = null;

  constructor(host: SlidingHost) {
    this.host = host;
    const renderer = host.renderer;
    this.trays = new TrayView(
      {
        store: host.store,
        meshes: host.meshes,
        shapeRevision: (s) => host.shapeRevision(s),
        style: () => host.display.style,
        changed: () => {
          byId('vp').dataset.tray = this.trays.describe();
          byId('vp').dataset.trayBorder = this.trays.describeBorder();
          byId('vp').dataset.trayPoses = this.trays.posesShown();
          renderer.request();
        },
        failed: (e) => {
          if (e instanceof EngineError && e.errc === 'superseded') {
            queueMicrotask(() => {
              this.trays.load();
            });
            return;
          }
          reportError(e, 'The start/goal shape could not be drawn');
        },
      },
      renderer.root,
    );
    this.trays.group.visible = false;
    this.trays.setPictureOf((shape) => host.pictureOf(shape));
    renderer.model.add(this.shapeBorder.group);
    this.solutionBorder.group.visible = false;
    renderer.root.add(this.solutionBorder.group);
  }

  /** Whether the Puzzle view shows a sliding board's trays. */
  get puzzleTrays(): boolean {
    return this.trayShown;
  }

  /** A sliding board's two trays in the Puzzle view (null: the problem shown has none). */
  setTray(model: TrayModel | null): void {
    this.trayShown = model !== null;
    this.trays.set(model);
  }

  reload(): void {
    this.trays.load();
  }

  themed(): void {
    // a board with none of its own takes the theme's color
    this.trays.load();
  }

  setResolution(width: number, height: number): void {
    this.trays.setResolution(width, height);
  }

  sync(): void {
    // the pictures may have been decoded or changed since the trays were set
    this.trays.applyPictures();
    const stacking = this.host.store.snapshot?.grid === 'stacking';
    this.trays.group.visible = tab === 'puzzle' && this.trayShown && !stacking;
    this.trays.setLook({
      style: this.host.display.style,
      colorView: false,
      lit: prefs.lights,
      xray: false,
      palette: [],
    });
    this.syncBorders();
  }

  drawPuzzle(): string | null {
    return this.trayShown ? this.drawTrays() : null;
  }

  /** The tray's walls around the Solver's solution (buildSlidingWalls), from the result's floor as the engine has it. */
  loadWalls(model: SolutionModel | null): void {
    const store = this.host.store;
    const solution = this.host.solution;
    const snap = store.snapshot;
    const result = model?.result ?? null;
    const n = ++this.wallLoads;
    if (model === null || result === null || snap?.grid !== 'sliding') {
      solution.setWalls(null);
      this.solutionFloor = null;
      byId('vp').dataset.walls = '0';
      return;
    }
    store.engine.call('shape.get', { shape: result }, { coalesce: 'walls' }).then(
      (r) => {
        if (n !== this.wallLoads) {
          return;
        }
        const [sx, sy, sz] = r.shape.size;
        const floor = floorTest(r.cells.map((c) => c.at));
        const outline = wallOutline(sx, sy, floor);
        solution.setWalls(wallSegments(outline, sx, sy, sz));
        this.solutionFloor = { shape: result, size: r.shape.size, floor };
        byId('vp').dataset.walls = String(solution.wallCount);
        this.syncBorders();
        this.host.renderer.request();
      },
      (e: unknown) => {
        if (n === this.wallLoads && !(e instanceof EngineError && e.errc === 'superseded')) {
          reportError(e, 'The walls could not be drawn');
        }
      },
    );
  }

  // a sliding tray's border (decision 0046): round the board the shape view
  // shows, once its mesh is here (its cells are the floor), a layer higher
  // than the floor; and round the Solver's pieces, as high as the result,
  // in place of the walls' lines
  private syncBorders(): void {
    const store = this.host.store;
    const snap = store.snapshot;
    const sliding = snap?.grid === 'sliding';
    const lookOf = (s: ShapeSummary, b: TrayBorder): BorderLook => ({
      color: borderColor(b, viewColor(s, store.indexOf(s.id))),
      material: b.material,
      lit: prefs.lights,
      picture: borderPicture(store.engine, s),
    });
    const shape = store.shape;
    const b = sliding ? shownBorder(shape) : null;
    const mesh = this.host.shapeMesh();
    const src = mesh?.source ?? null;
    if (
      shape !== null &&
      b !== null &&
      mesh !== null &&
      src?.kind === 'shape' &&
      src.shape === shape.id
    ) {
      this.shapeBorder.set({
        size: [shape.size[0], shape.size[1]],
        floor: floorOfMesh(mesh.arrays.voxel, mesh.size),
        z0: 0,
        z1: shape.size[2] + 1,
        round: b.corners === 'round',
        rim: borderRim(b),
        at: [[0, 0, 0]],
      });
      this.shapeBorder.setLook(lookOf(shape, b));
    } else {
      this.shapeBorder.set(null);
    }
    const f = this.solutionFloor;
    const result = snap?.shapes.find((s) => s.id === this.host.solutionModel()?.result) ?? null;
    const rb = sliding ? shownBorder(result) : null;
    if (result !== null && rb !== null && f?.shape === result.id) {
      const [sx, sy, sz] = f.size;
      this.solutionBorder.set({
        size: [sx, sy],
        floor: f.floor,
        z0: 0,
        z1: sz,
        round: rb.corners === 'round',
        rim: borderRim(rb),
        at: [[-sx / 2, -sy / 2, -sz / 2]],
      });
      this.solutionBorder.setLook(lookOf(result, rb));
    } else {
      this.solutionBorder.set(null);
    }
    this.host.solution.showWalls(this.solutionBorder.count === 0);
    this.solutionBorder.group.visible = tab === 'solver';
    const vp = byId('vp');
    vp.dataset.border = this.shapeBorder.describe();
    vp.dataset.solutionBorder = this.solutionBorder.describe();
  }

  // a sliding board's two trays in the Puzzle view, and their titles: the
  // whole view, or with pieces to show, the pieces scene in a band over the
  // validity bar as tall as it needs (at most PIECES_SHARE of the free
  // height; as many copies a row as show them largest), and the trays fitted at the home view into the space between
  // the view's tag and that band. The trays are drawn last over the whole
  // view, so a zoom or a turn that takes them past their space is not cut
  // off.
  private drawTrays(): string {
    const r = this.host.renderer;
    const camera = this.host.camera;
    const pieces = this.host.pieces;
    const extent = this.trays.extent ?? [1, 1, 1];
    const tray = (sc?: number, dy = 0): string => {
      const w = r.width;
      const h = r.height;
      this.trays.setResolution(w, h);
      // the trays move as the model would: centerd on what they fill
      const cam = camera;
      r.frame({ q: cam.q, zoom: cam.zoom, px: cam.px, py: cam.py + dy }, extent, sc);
      this.trays.group.position.copy(r.model.position);
      r.render();
      return this.trays.tags(this.trays.group, (v) => {
        const p = v.clone().project(r.camera);
        return [((p.x + 1) / 2) * w, ((1 - p.y) / 2) * h];
      });
    };
    if (pieces.empty) {
      return tray();
    }
    const full = r.height;
    const tag = byId('vtag');
    const valid = byId('slideValid');
    const y0 = Math.min(Math.round(full / 4), tag.offsetTop + tag.offsetHeight + 8);
    const y1 =
      valid.offsetParent !== null && valid.offsetHeight > 0
        ? Math.max(y0 + 2, valid.offsetTop - 4)
        : full;
    const free = y1 - y0;
    const top = y1 - Math.round(pieces.bandHeight(r.width, free * PIECES_SHARE));
    // the view's background, then the pieces in their band
    r.gl.clear();
    this.trays.group.visible = false;
    r.band(top, y1 - top, () => {
      this.host.drawPieces(top);
    });
    this.trays.group.visible = true;
    // the trays over the whole view: their size at the home view fills their
    // space, their center at its center
    const [fw, fh] = homeFootprint(extent, camera.fromAngles(HOME));
    const sc = Math.min((r.width * TRAY_FILL) / fw, ((top - y0) * TRAY_FILL) / fh) * camera.zoom;
    pieces.group.visible = false;
    const auto = r.gl.autoClear;
    r.gl.autoClear = false;
    r.gl.clearDepth();
    try {
      return tray(Math.max(sc, 0.01), (y0 + top) / 2 - full / 2);
    } finally {
      r.gl.autoClear = auto;
      pieces.group.visible = true;
    }
  }
}

// the width and height, in units, a box of `extent` takes on the screen
// turned by `q` (orthographic; the perspective adds a little, which the
// margin takes)
function homeFootprint(extent: readonly number[], q: Quaternion): [number, number] {
  let x0 = Infinity;
  let x1 = -Infinity;
  let y0 = Infinity;
  let y1 = -Infinity;
  const v = new Vector3();
  for (let i = 0; i < 8; i++) {
    v.set(
      ((i & 1) - 0.5) * (extent[0] ?? 1),
      (((i >> 1) & 1) - 0.5) * (extent[1] ?? 1),
      (((i >> 2) & 1) - 0.5) * (extent[2] ?? 1),
    ).applyQuaternion(q);
    x0 = Math.min(x0, v.x);
    x1 = Math.max(x1, v.x);
    y0 = Math.min(y0, v.y);
    y1 = Math.max(y1, v.y);
  }
  return [Math.max(x1 - x0, 1), Math.max(y1 - y0, 1)];
}
