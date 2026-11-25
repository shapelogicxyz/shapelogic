// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view: the shape the store selects, drawn from the engine's mesh
// (meshcache.ts, mesh.ts, scene.ts), navigated and picked by its pointer
// (pointer.ts; docs/mockup/index.html: `renderTag`, the empty message). In
// the Puzzle workspace it draws the pieces scene instead (pieces.ts: the
// problem's result and pieces, one mesh per shape; a click selects a piece
// through the puzzle host), in the Solver workspace a solution
// (solution.ts: the pieces the Solver's player gives it, posed, framed on
// the result). Each puzzle kind's part is a layer (layer.ts): a sliding
// puzzle's trays, walls and borders, a stacking puzzle's board, a Tilted
// Square puzzle's tray; a resize, a reload, a theme, the look and the
// Puzzle workspace's drawing walk them. It renders only when something
// changed.

import { Quaternion, Vector3 } from 'three';

import { EngineError } from '../../engine/errors.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { MeshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId, css, esc } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab, toggleFocus3d } from '../shell/layout.ts';
import { chipColor, contrast, hex, viewColor } from '../shell/piece-colors.ts';
import { onPieceImage, piecePicture } from '../shell/piece-images.ts';
import type { PiecePicture } from '../shell/picture-math.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { reportError } from '../shell/status.ts';
import { isDarkTheme, onTheme } from '../shell/theme.ts';
import { baseOf, Camera, EYE_DISTANCE, HOME, pixelsPerUnit } from './camera.ts';
import { gridExtent } from './extent.ts';
import type { BoardModel } from './board.ts';
import type { LayerHost, ViewLayer } from './layer.ts';
import type { BuiltMesh } from './mesh.ts';
import { MeshCache } from './meshcache.ts';
import { MeshSlot, ShapeMeshes } from './shape-meshes.ts';
import type { MeshAsk, MeshLease } from './shape-meshes.ts';
import { layerAxis, layerSlab } from './overlays.ts';
import type { Plane } from './overlays.ts';
import { PiecesScene } from './pieces.ts';
import type { PieceMesh, PiecesModel } from './pieces.ts';
import { labelSprite } from './labels.ts';
import { SolutionScene } from './solution.ts';
import type { ScenePose, SolutionModel } from './solution.ts';
import type { Pick } from './picking.ts';
import { ViewPointer } from './pointer.ts';
import type { EditHost, ViewMods } from './pointer.ts';
import type { Ghost } from './preview.ts';
import { measureMeshBuild, ViewRenderer } from './renderer.ts';
import { ShapeScene } from './scene.ts';
import { SlidingLayer } from './sliding-layer.ts';
import { StackingLayer } from './stacking-layer.ts';
import { vectorImage } from './svg.ts';
import { TiltedLayer } from './tilted-layer.ts';
import { Toolbar } from './toolbar.ts';
import type { TrayModel } from './trays.ts';
import type { Display, EditButton, NavMode } from './toolbar.ts';
import { ViewCube } from './viewcube.ts';

type V3 = [number, number, number];

export type { EditHost, ViewMods } from './pointer.ts';

/** What the Puzzle workspace tells the view and hears from it. */
export interface PuzzleHost {
  /** A piece was clicked in the pieces scene. */
  pick(shape: string): void;
  /** The view's tag (HTML), the mockup's pzTag. */
  tag(): string;
  /** The message over an empty scene ('' for none). */
  empty(): string;
}

/** What the Solver workspace tells the view. */
export interface SolverHost {
  /** The view's tag (HTML), the mockup's svTag. */
  tag(): string;
  /** The message over an empty scene ('' for none). */
  empty(): string;
  /** The Display menu's style changed: the solution's meshes again. */
  styleChanged(): void;
  /** Whether an Alt-drag on a piece moves it now (the movement explorer), not the view. */
  movesPieces(): boolean;
  /** A piece was Alt-dragged by (dx, dy) CSS pixels on the screen. */
  pieceDragged(piece: number, dx: number, dy: number): void;
}

/**
 * Who draws in the Solver's view: the player, an explorer over it while one
 * is open, or the live preview of a run under it (while the others have nothing).
 */
export type SolutionLayer = 'player' | 'explorer' | 'preview';

export class View3D {
  readonly camera = new Camera();
  readonly display: Display = {
    axes: true,
    bounds: true,
    layer: true,
    dim: false,
    persp: true,
    colorView: false,
    style: 'drawing',
    xray: false,
    pieceNames: true,
    gridNames: true,
  };
  mode: NavMode = 'orbit';
  /** The voxel editor's plane and layer (the slab, the dimmed layers). */
  plane: Plane = 'XY';
  layer = 0;
  /** 3D editing, when a workspace does it. */
  edit: EditHost | null = null;
  /** The Puzzle workspace, once it is there. */
  puzzle: PuzzleHost | null = null;
  /** The Solver workspace, once it is there. */
  solver: SolverHost | null = null;

  private readonly store: Store;
  private readonly cache: MeshCache;
  /** The shape meshes built, one copy each for every part of the view that draws them. */
  private readonly meshes: ShapeMeshes;
  /** The shape view's mesh, and the pieces scene's. */
  private readonly shapeSlot: MeshSlot;
  private readonly piecesSlot: MeshSlot;
  private readonly renderer: ViewRenderer;
  private readonly scene: ShapeScene;
  /** Each puzzle kind's part of the view (layer.ts), in the order they are walked. */
  private readonly sliding: SlidingLayer;
  private readonly stackingLayer: StackingLayer;
  private readonly tilted: TiltedLayer;
  private readonly layers3d: readonly ViewLayer[];
  /** The titles over the Puzzle view (a sliding board's START and GOAL). */
  private trayTags = '';
  private readonly cube: ViewCube;
  private readonly toolbar: Toolbar;
  private readonly pointer: ViewPointer;
  /** Whether lookChanged asked for the look, not yet worked out (applyLook). */
  private lookPending = false;
  /** What the pieces' screen positions were worked out for (`data-piece-screen`). */
  private piecesOnScreen = '';
  /** What the Edit button shows now (JSON), so a hover that changes nothing leaves it. */
  private editButtonShown = '';
  private readonly displayListeners = new Set<() => void>();
  private readonly canvas: HTMLCanvasElement;
  private extent: V3 = [1, 1, 1];
  /** What the mesh shown is of: shape, style, generation, revision. */
  private shownKey = '';
  private readonly pieces: PiecesScene;
  private piecesModel: PiecesModel | null = null;
  private readonly solution = new SolutionScene(labelSprite);
  private solutionModel: SolutionModel | null = null;
  /**
   * What each layer would show; the explorer's over the player's while it
   * has a model, the preview's only while neither has one.
   */
  private readonly layers: Record<
    SolutionLayer,
    { model: SolutionModel | null; poses: readonly ScenePose[] }
  > = {
    player: { model: null, poses: [] },
    explorer: { model: null, poses: [] },
    preview: { model: null, poses: [] },
  };
  private shownLayer: SolutionLayer = 'player';
  /** What the pieces scene shows: the model's key and the style. */
  private piecesShown = '';
  /** The revision each shape last changed at (document.changed), per document. */
  private readonly shapeRev = new Map<string, number>();
  private revGeneration = -1;
  private revBase = 0;

  constructor(store: Store) {
    this.store = store;
    // the settings are read before the view is made: the first view is the home of theirs
    this.camera.setBase(this.upright());
    this.cache = new MeshCache(store.engine);
    this.meshes = new ShapeMeshes(this.cache, () => store.generation);
    this.shapeSlot = new MeshSlot(this.meshes);
    this.piecesSlot = new MeshSlot(this.meshes);
    this.canvas = byId<HTMLCanvasElement>('cv');
    this.renderer = new ViewRenderer(this.canvas);
    this.scene = new ShapeScene(this.renderer.model);
    const layerHost: LayerHost = {
      store,
      renderer: this.renderer,
      camera: this.camera,
      meshes: this.meshes,
      display: this.display,
      shapeRevision: (s) => this.shapeRevision(s),
      solutionModel: () => this.solutionModel,
    };
    this.pieces = new PiecesScene(byId('pzScene'));
    this.sliding = new SlidingLayer({
      ...layerHost,
      solution: this.solution,
      pieces: this.pieces,
      shapeMesh: () => this.scene.mesh,
      drawPieces: (top) => {
        this.drawPieces(top);
      },
      pictureOf: (shape) => this.pictureOf(shape),
    });
    this.stackingLayer = new StackingLayer(layerHost);
    this.pieces.group.visible = false;
    this.renderer.scene.add(this.pieces.group);
    // centerd on the result (the poses' frame), turned by the camera as the shape is
    this.solution.group.visible = false;
    this.renderer.root.add(this.solution.group);
    this.tilted = new TiltedLayer(layerHost);
    // the Puzzle view is the first layer's that draws it (drawPuzzle)
    this.layers3d = [this.stackingLayer, this.sliding, this.tilted];
    this.renderer.onDraw(() => {
      this.draw();
    });
    this.cube = new ViewCube(byId<HTMLCanvasElement>('vcube'), {
      camera: this.camera,
      perspective: () => this.display.persp,
      dark: isDarkTheme,
      moved: () => {
        this.renderer.request();
      },
    });
    this.renderer.onRestored(() => {
      this.cube.draw(true);
    });
    this.toolbar = new Toolbar({
      display: this.display,
      mode: () => this.mode,
      setMode: (m) => {
        this.setMode(m);
      },
      sliding: () => this.store.snapshot?.grid === 'sliding',
      changed: () => {
        this.displayChanged();
      },
      toggleFocus: toggleFocus3d,
      toggleEdit: () => {
        this.edit?.toggle();
      },
    });
    this.toolbar.renderMode();

    store.subscribe((c) => {
      switch (c.kind) {
        case 'document':
        case 'selection':
          if (c.kind === 'document' && c.restarted) {
            this.cache.drop(null);
          }
          this.tilted.sync();
          this.reload();
          break;
        case 'status':
          this.renderEmpty();
          break;
        case 'changed': {
          const p = c.change;
          if (p.what.includes('shapes') || p.what.includes('colors')) {
            this.shapeRevision('');
            if (p.shapes.length === 0 || p.what.includes('colors')) {
              this.revBase = p.revision;
              this.shapeRev.clear();
            } else {
              for (const s of p.shapes) {
                this.shapeRev.set(s, p.revision);
              }
            }
          }
          break;
        }
        case 'settings':
          break;
      }
    });
    // a sliding piece's picture (decision 0038), wherever the piece is drawn
    this.solution.pictureOf = (shape) => this.pictureOf(shape);
    onPieceImage(() => {
      this.lookChanged();
    });
    onLayout(() => {
      this.toolbar.renderFocus();
      this.lookChanged();
    });
    onTheme(() => {
      this.themeChanged();
    });
    onPrefs((changed) => {
      if (changed.includes('cube')) {
        this.cube.setVisible(prefs.cube);
        this.cube.draw();
      }
      if (
        changed.includes('lights') ||
        changed.includes('pieceLabels') ||
        changed.includes('pieceNames') ||
        changed.includes('labelCharsPerVoxel') ||
        // the Solver's labels sit on the widest layer, counted up from the bottom
        (changed.includes('zAxis') && tab === 'solver')
      ) {
        this.lookChanged();
      }
      if (changed.includes('density')) {
        this.toolbar.reset();
        // the toolbar's size changed: the cube may be crowded, or no longer
        this.cube.draw(true);
      }
      if (changed.includes('zAxis')) {
        this.camera.setBase(this.upright(), performance.now());
        this.renderer.request();
      }
    });
    onKey('view.orbit', () => {
      this.setMode('orbit');
      return true;
    });
    onKey('view.pan', () => {
      this.setMode('pan');
      return true;
    });
    onKey('home', () => {
      this.camera.home(performance.now());
      this.renderer.request();
      return true;
    });
    onKey('view.fit', () => {
      this.camera.fit(performance.now());
      this.toolbar.flashShow();
      this.renderer.request();
      return true;
    });
    this.pointer = new ViewPointer({
      canvas: this.canvas,
      camera: this.camera,
      renderer: this.renderer,
      store,
      scene: this.scene,
      pieces: this.pieces,
      solution: this.solution,
      mode: () => this.mode,
      extent: () => this.extent,
      solutionExtent: () => this.solutionModel?.extent ?? [1, 1, 1],
      rodSetShown: () => this.rodSetShown,
      plane: () => this.plane,
      layer: () => this.layer,
      edit: () => this.edit,
      pick: (shape) => {
        this.puzzle?.pick(shape);
      },
      movesPieces: () => this.solver?.movesPieces() === true,
      pieceDragged: (piece, dx, dy) => {
        this.solver?.pieceDragged(piece, dx, dy);
      },
      setCursor: (cursor) => {
        this.setCursor(cursor);
      },
    });
    new ResizeObserver(() => {
      this.resized();
    }).observe(byId('vp'));
    this.cube.setVisible(prefs.cube);
    this.themeChanged();
    this.resized();
    this.reload();
  }

  /** The renderer, for the view's measurements. */
  get gl(): ViewRenderer {
    return this.renderer;
  }

  /** The mesh shown, or null. */
  get mesh(): BuiltMesh | null {
    return this.scene.mesh;
  }

  /** The meshes of shapes asked of the engine (the Solver's assembled view shares them). */
  get meshCache(): MeshCache {
    return this.cache;
  }

  /** File ▸ Export ▸ Image: the view as a PNG (a data URL), `scale` pixels to a CSS pixel. */
  image(scale: number): { data: string; width: number; height: number } {
    return this.renderer.capture(scale);
  }

  /** File ▸ Export ▸ Vector Image: the view as an SVG document, over the flat meshes of what it draws. */
  vectorImage(): Promise<string> {
    const store = this.store;
    return vectorImage(
      this.renderer,
      async (source) =>
        meshArrays(
          source.kind === 'shape'
            ? await this.cache.get(source.shape, 'flat', source.revision, source.transformation)
            : await store.engine.call('playback.mesh', {
                handle: source.handle,
                piece: source.piece,
                style: 'flat',
              }),
        ),
      css('--canvas') || '#e4e8ef',
    );
  }

  /** Changes the Display menu's choices (the View menu's display modes and styles). */
  setDisplay(change: Partial<Display>): void {
    Object.assign(this.display, change);
    this.displayChanged();
  }

  /** Listens to changes of the Display choices (the View menu shows them). */
  onDisplay(listener: () => void): () => void {
    this.displayListeners.add(listener);
    return () => {
      this.displayListeners.delete(listener);
    };
  }

  private displayChanged(): void {
    // another style is another mesh
    this.reload();
    this.loadPieces();
    this.solver?.styleChanged();
    this.lookChanged();
    for (const l of [...this.displayListeners]) {
      l();
    }
  }

  /**
   * The Solver's solution (null: none): its pieces with their meshes, framed
   * on the result. The player draws in its layer; an explorer's model, while
   * it has one, is shown over it (the player's comes back when it goes).
   */
  setSolution(model: SolutionModel | null, layer: SolutionLayer = 'player'): void {
    this.layers[layer].model = model;
    if (layer !== 'player' && model === null) {
      this.layers[layer].poses = [];
    }
    const shown: SolutionLayer =
      this.layers.explorer.model !== null
        ? 'explorer'
        : this.layers.player.model === null && this.layers.preview.model !== null
          ? 'preview'
          : 'player';
    if (layer !== shown && shown === this.shownLayer) {
      return;
    }
    this.shownLayer = shown;
    const l = this.layers[shown];
    this.solutionModel = l.model;
    this.solution.set(l.model);
    this.solution.setPoses(l.poses);
    this.sliding.loadWalls(l.model);
    this.stackingLayer.show();
    const vp = byId('vp');
    vp.dataset.solution = l.model?.key ?? '';
    vp.dataset.solutionLayer = shown;
    vp.dataset.poses = this.solution.posesShown();
    vp.dataset.solutionPieces = this.solution.describe();
    this.lookChanged();
  }

  /** The layer the Solver's view shows now. */
  get solutionLayer(): SolutionLayer {
    return this.shownLayer;
  }

  /**
   * Places the solution's pieces (a pose per piece of the layer's model).
   * `marks` false leaves the test attributes (`data-poses`,
   * `data-solution-pieces`) as they were: a frame of play, which writes
   * them when it pauses or ends.
   */
  poseSolution(poses: readonly ScenePose[], layer: SolutionLayer = 'player', marks = true): void {
    this.layers[layer].poses = [...poses];
    if (layer !== this.shownLayer) {
      return;
    }
    this.solution.setPoses(poses);
    if (marks) {
      const vp = byId('vp');
      vp.dataset.poses = this.solution.posesShown();
      vp.dataset.solutionPieces = this.solution.describe();
    }
    this.renderer.request();
  }

  /**
   * Where a piece of the solution shown is on the screen (CSS pixels in the
   * viewport), and where a move by `delta` (mesh units along the scene's
   * axes) would take it: what an Alt-drag is matched against.
   */
  pieceOnScreen(
    piece: number,
    delta: readonly [number, number, number] = [0, 0, 0],
  ): { x: number; y: number; dx: number; dy: number } | null {
    this.renderer.frame(this.camera, this.solutionModel?.extent ?? [1, 1, 1]);
    const c = this.solution.centerOf(piece);
    if (c === null) {
      return null;
    }
    const d = new Vector3(delta[0], delta[1], delta[2]).applyQuaternion(
      this.renderer.root.quaternion,
    );
    const a = this.toScreen(c);
    const b = this.toScreen(c.clone().add(d));
    return { x: a[0], y: a[1], dx: b[0] - a[0], dy: b[1] - a[1] };
  }

  private toScreen(v: Vector3): [number, number] {
    const p = v.clone().project(this.renderer.camera);
    return [((p.x + 1) / 2) * this.canvas.clientWidth, ((1 - p.y) / 2) * this.canvas.clientHeight];
  }

  /** A sliding board's two trays in the Puzzle view (null: the problem shown has none). */
  setTray(model: TrayModel | null): void {
    this.sliding.setTray(model);
    this.lookChanged();
  }

  /**
   * A stacking puzzle's board in a workspace (null: none): the Entities'
   * rod set shown alone instead of the disc, the Puzzle's start or goal.
   */
  setBoard(model: BoardModel | null, where: 'entities' | 'puzzle'): void {
    if (!this.stackingLayer.setBoard(model, where)) {
      return;
    }
    this.renderTag();
    this.lookChanged();
  }

  /** Whether the Entities workspace shows a rod set instead of its disc. */
  get rodSetShown(): boolean {
    return this.stackingLayer.rodSetShown;
  }

  private get stacking(): boolean {
    return this.store.snapshot?.grid === 'stacking';
  }

  // the upright frame: the Z axis direction's, but a stacking puzzle's view
  // is tilted to stand its stack up (renderer.ts) and keeps its own
  private upright(): Quaternion {
    return this.stacking ? new Quaternion() : baseOf(prefs.zAxis);
  }

  /** The pieces scene's model (the Puzzle workspace); its meshes are loaded when shown. */
  setPieces(model: PiecesModel | null): void {
    this.piecesModel = model;
    this.loadPieces();
  }

  /** The tag and the empty message again (the Puzzle workspace's changed). */
  renderLabels(): void {
    this.renderTag();
    this.renderEmpty();
  }

  /** The voxel editor's plane and layer: the slab and the dimming follow. */
  setEditLayer(plane: Plane, layer: number): void {
    if (plane !== this.plane || layer !== this.layer) {
      this.plane = plane;
      this.layer = layer;
      this.lookChanged();
    }
  }

  /** The 3D edit's preview (empty: none). */
  setPreview(ghosts: readonly Ghost[]): void {
    // drawn again only when it changed (cleanup item 10.1)
    if (this.scene.setPreview(ghosts)) {
      byId('vp').dataset.preview = String(this.scene.preview.size);
      this.renderer.request();
    }
  }

  /** Shows the shape as a tool tab button would leave it, over the shape (null: none). */
  setTaskGhost(arrays: MeshArrays | null): void {
    if (this.scene.setTaskGhost(arrays)) {
      byId('vp').dataset.taskGhost = String(this.scene.taskGhost.size);
      this.renderer.request();
    }
  }

  /** The view's mouse cursor (empty: the navigation's). */
  setCursor(cursor: string): void {
    if (this.canvas.style.cursor !== cursor) {
      this.canvas.style.cursor = cursor;
    }
  }

  /** The Edit button. */
  renderEditButton(e: EditButton): void {
    const key = JSON.stringify(e);
    if (key !== this.editButtonShown) {
      this.editButtonShown = key;
      this.toolbar.renderEdit(e);
    }
  }

  /** The modifiers held changed (a key): the hover is told again. */
  modsChanged(m: ViewMods): void {
    this.pointer.modsChanged(m);
  }

  /** The last pick under the mouse, or null. */
  get lastPick(): Pick | null {
    return this.pointer.lastPick;
  }

  private setMode(m: NavMode): void {
    this.mode = m;
    this.toolbar.renderMode();
  }

  private resized(): void {
    const vp = byId('vp');
    this.renderer.resize(vp.clientWidth, vp.clientHeight);
    this.scene.setResolution(vp.clientWidth, vp.clientHeight);
    this.pieces.setResolution(vp.clientWidth, vp.clientHeight);
    this.solution.setResolution(vp.clientWidth, vp.clientHeight);
    for (const l of this.layers3d) {
      l.setResolution(vp.clientWidth, vp.clientHeight);
    }
    this.cube.draw(true);
    this.renderer.request();
  }

  private themeChanged(): void {
    this.renderer.setBackground(css('--canvas') || '#e4e8ef');
    // a sliding tray's walls stand out from the canvas
    this.solution.setDark(isDarkTheme());
    for (const l of this.layers3d) {
      l.themed();
    }
    this.lookChanged();
  }

  // the shape (or its absence) the store selects: fetch its mesh
  private reload(): void {
    const store = this.store;
    const snap = store.snapshot;
    const shape = store.shape;
    this.renderTag();
    for (const l of this.layers3d) {
      l.reload();
    }
    if (snap === null || shape === null) {
      this.shapeSlot.clear(() => {
        this.shownKey = '';
        this.scene.setMesh(null);
      });
      delete byId('vp').dataset.shown;
      this.lookChanged();
      return;
    }
    this.extent = gridExtent(snap.grid, shape.size);
    const style = this.display.style;
    // the mesh is asked for at the revision the shape last changed at, not
    // the document's: a rename, a weight or a solve's end leaves it as it is
    // (cleanup item 10.3; the engine names every shape whose mesh changed)
    const revision = this.shapeRevision(shape.id);
    const key = `${store.generation}:${shape.id}:${style}:${String(revision)}`;
    if (key === this.shownKey) {
      // any mesh still on its way is stale now (wire frame, then back to the
      // shown style: the wire frame's mesh must not land over it)
      this.shapeSlot.stale();
      byId('vp').dataset.revision = String(snap.revision);
      this.lookChanged();
      return;
    }
    const size = shape.size;
    const snapRevision = snap.revision;
    // a Tilted Square board is drawn by its tray (syncTurn), its whole
    // cells as checker blocks: its mesh would show variable cells
    if (snap.tray != null && shape.board) {
      this.shapeSlot.clear(() => {
        this.shownKey = key;
        this.scene.setMesh(null);
      });
      byId('vp').dataset.shown = `${shape.id}:${style}`;
      byId('vp').dataset.revision = String(snapRevision);
      this.lookChanged();
      return;
    }
    const ask: MeshAsk = {
      shape: shape.id,
      style,
      revision,
      size,
      built: (t0, reply) => {
        measureMeshBuild(t0, { cells: shape.fixed + shape.variable, triangles: reply.triangles });
      },
    };
    this.shapeSlot
      .load([ask], ([lease]) => {
        this.shownKey = key;
        this.scene.setMesh(lease?.built ?? null);
        byId('vp').dataset.shown = `${shape.id}:${style}`;
        byId('vp').dataset.revision = String(snapRevision);
        this.lookChanged();
        this.pointer.repick();
      })
      .catch((e: unknown) => {
        reportError(e, 'The shape could not be drawn');
      });
    this.lookChanged();
  }

  // the revision a shape last changed at in this document (its meshes
  // asked at an older one are asked again)
  private shapeRevision(shape: string): number {
    const store = this.store;
    if (store.generation !== this.revGeneration) {
      this.revGeneration = store.generation;
      this.revBase = store.snapshot?.revision ?? 0;
      this.shapeRev.clear();
    }
    return Math.max(this.revBase, this.shapeRev.get(shape) ?? 0);
  }

  private get puzzleMode(): boolean {
    return tab === 'puzzle';
  }

  private get solverMode(): boolean {
    return tab === 'solver';
  }

  // the pieces scene's meshes (one per shape), then the scene; only while it is shown
  private loadPieces(): void {
    const model = this.piecesModel;
    if (!this.puzzleMode) {
      return;
    }
    const style = this.display.style;
    const shown = `${model?.key ?? ''}:${style}`;
    if (shown === this.piecesShown) {
      // a scene still on its way is stale: this one is already shown
      this.piecesSlot.stale();
      return;
    }
    if (model === null) {
      this.piecesSlot.clear(() => {
        this.showPieces(null, new Map(), shown);
      });
      return;
    }
    const store = this.store;
    const shapes = [...new Set(model.items.map((i) => i.shape))];
    const asks = shapes.map((s): MeshAsk => ({
      shape: s,
      style,
      revision: this.shapeRevision(s),
      size: store.snapshot?.shapes.find((x) => x.id === s)?.size ?? [1, 1, 1],
    }));
    // a scene stale by the time its meshes come gives them back (the
    // shared meshes keep what another scene still shows)
    const loading = this.piecesSlot.load(asks, (leases) => {
      this.showPieces(model, new Map(leases.map((l) => [l.shape, pieceMesh(l)])), shown);
    });
    const n = this.piecesSlot.loads;
    loading.catch((e: unknown) => {
      if (e instanceof EngineError && e.errc === 'superseded') {
        // a newer request of a mesh took this one's place: ask again (it is cached now)
        queueMicrotask(() => {
          if (this.piecesSlot.loads === n) {
            this.loadPieces();
          }
        });
        return;
      }
      reportError(e, 'The pieces could not be drawn');
    });
  }

  private showPieces(
    model: PiecesModel | null,
    meshes: Map<string, PieceMesh>,
    shown: string,
  ): void {
    this.pieces.set(
      model ?? { key: '', items: [], hidden: 0, selected: null, names: new Map() },
      meshes,
      this.piecesLook(),
      this.camera.fromAngles(HOME),
    );
    this.piecesShown = shown;
    const vp = byId('vp');
    vp.dataset.scene = this.pieces.describe();
    vp.dataset.sceneMeshes = String(this.pieces.meshCount);
    vp.dataset.sceneKey = model?.key ?? '';
    vp.dataset.sceneStyle = this.display.style;
    this.renderLabels();
    this.renderer.request();
  }

  /** A shape's picture once decoded, on a sliding puzzle (null: none, or not yet). */
  private pictureOf(shape: string): PiecePicture | null {
    const snap = this.store.snapshot;
    if (snap?.grid !== 'sliding') {
      return null;
    }
    return piecePicture(this.store.engine, snap.shapes[this.store.indexOf(shape)]);
  }

  private piecesLook(): Parameters<PiecesScene['setLook']>[0] {
    const snap = this.store.snapshot;
    return {
      style: this.display.style,
      colorView: this.display.colorView,
      lit: prefs.lights,
      palette: snap?.colors.map((c) => c.rgb) ?? [],
      colorOf: (shape) => {
        const i = this.store.indexOf(shape);
        return viewColor(snap?.shapes[i], i);
      },
      // a Tilted Square board (the result) as checker blocks, as in the shape view
      standIn: (shape) => this.tilted.standIn(snap, shape),
      pictureOf: (shape) => this.pictureOf(shape),
    };
  }

  // the look and the overlays changed: worked out again once for everything
  // a task changed (a snapshot reaches the view, its workspaces and their
  // boards and trays in one task: cleanup item 10.2), before the next frame
  private lookChanged(): void {
    this.renderer.request();
    if (this.lookPending) {
      return;
    }
    this.lookPending = true;
    queueMicrotask(() => {
      this.applyLook();
    });
  }

  // the look and the overlays from the options, the store and the settings
  // (when lookChanged asked for them)
  private applyLook(): void {
    if (!this.lookPending) {
      return;
    }
    this.lookPending = false;
    // the pictures may have been decoded or changed since the scenes were set
    this.solution.applyPictures();
    const puzzle = this.puzzleMode;
    const solver = this.solverMode;
    const stacking = this.stacking;
    // each puzzle kind's part: what shows, its look (a stacking puzzle is
    // drawn tilted, a Tilted Square one turned with its home view)
    for (const l of this.layers3d) {
      l.sync();
    }
    this.camera.setBase(this.upright(), performance.now());
    // a sliding board's trays in the Puzzle view, the pieces scene under
    // them (draw() shows each in its own band); a stacking puzzle's
    // Puzzle workspace draws its board, not the pieces scene
    const trays = puzzle && this.sliding.puzzleTrays && !stacking;
    this.renderer.root.visible = !puzzle || stacking || trays;
    this.renderer.model.visible = !solver && !puzzle && !this.rodSetShown;
    this.pieces.group.visible = puzzle && !stacking;
    this.solution.group.visible = solver;
    if (solver) {
      const snap0 = this.store.snapshot;
      this.solution.setLook({
        style: this.display.style,
        colorView: this.display.colorView,
        lit: prefs.lights,
        xray: this.display.xray,
        palette: snap0?.colors.map((c) => c.rgb) ?? [],
      });
      this.solution.setLabels({
        label: prefs.pieceLabels,
        name: prefs.pieceNames,
        chars: prefs.labelCharsPerVoxel,
        // the scene's up in the home view: +Y with Z Toward; +Z with Z Up
        // and on a stacking puzzle (upright)
        up: this.stacking || prefs.zAxis === 'Z Up' ? 2 : 1,
      });
      this.renderTag();
    }
    if (puzzle && !stacking) {
      this.pieces.setLook(this.piecesLook());
      this.loadPieces();
      this.renderTag();
    }
    const snap = this.store.snapshot;
    const shape = this.store.shape;
    const index = shape === null ? 0 : this.store.indexOf(shape.id);
    const ed = editorVisible();
    this.renderer.perspective = this.display.persp;
    this.scene.setLook({
      style: this.display.style,
      colorView: this.display.colorView,
      lit: prefs.lights,
      xray: this.display.xray,
      piece: viewColor(shape ?? undefined, index),
      picture: shape === null ? null : this.pictureOf(shape.id),
      palette: snap?.colors.map((c) => c.rgb) ?? [],
      dim: this.display.dim && ed ? { axis: layerAxis(this.plane), layer: this.layer } : null,
    });
    const showSlab = this.display.layer && ed && snap !== null && shape !== null && !solver;
    this.scene.setOverlays({
      extent: this.extent,
      axes: this.display.axes && shape !== null && tab === 'entities' && !this.rodSetShown,
      bounds: this.display.bounds && shape !== null && tab === 'entities' && !this.rodSetShown,
      slab: showSlab
        ? layerSlab(
            snap.grid,
            this.extent,
            shape.size,
            this.plane,
            this.layer,
            this.scene.mesh?.arrays ?? null,
          )
        : null,
      colors: {
        ax: css('--ax') || '#e5484d',
        ay: css('--ay') || '#2fa85a',
        az: css('--az') || '#2f6df6',
        line: css('--line2') || '#c6ccd7',
        accent: css('--accent') || '#2f6df6',
      },
    });
    this.renderEmpty();
    this.cube.draw();
    this.renderer.request();
  }

  // the mockup's renderTag: the shape's chip and name at the view's top left
  // (pzTag in the Puzzle workspace)
  private renderTag(): void {
    const shape = this.store.shape;
    const tag = byId('vtag');
    if (this.puzzleMode && this.puzzle !== null) {
      tag.innerHTML = this.puzzle.tag();
      return;
    }
    if (this.solverMode && this.solver !== null) {
      tag.innerHTML = this.solver.tag();
      return;
    }
    const rods = this.stackingLayer.rodSetTag();
    if (rods !== null) {
      // the rod set shown alone (showSelectedRods)
      tag.innerHTML = rods;
      return;
    }
    if (shape === null) {
      tag.innerHTML = '';
      return;
    }
    const c = chipColor(shape, this.store.indexOf(shape.id));
    tag.innerHTML = `<span class="chip" style="background:${hex(c)};color:${contrast(c)}">${esc(shape.label)}</span><span>${esc(shape.name)}</span>`;
  }

  // the message over an empty view
  private renderEmpty(): void {
    const el = byId('emptyMsg');
    const s = this.store;
    let text = '';
    if (this.puzzleMode && s.running && s.snapshot !== null) {
      text = this.puzzle?.empty() ?? '';
    } else if (this.solverMode && s.running && s.snapshot !== null) {
      text = this.solver?.empty() ?? '';
    } else if (s.status === null || s.status.state === 'exited') {
      text = 'Starting the engine…';
    } else if (s.status.state === 'failed') {
      text = `The engine could not be started: ${s.status.reason}`;
    } else if (s.snapshot === null) {
      text = '';
    } else if (this.rodSetShown) {
      text = '';
    } else if (s.snapshot.shapes.length === 0) {
      text = this.stacking
        ? 'No discs — add one with + New'
        : s.snapshot.grid === 'sliding'
          ? 'No shapes — add one with + New Piece or + New Board'
          : 'No shapes — add one with + New';
    } else if (s.shape !== null && s.shape.fixed + s.shape.variable === 0) {
      text = 'Empty shape — draw in the Voxel editor';
    }
    el.textContent = text;
    el.classList.toggle('show', text !== '');
  }

  private draw(): void {
    // a look asked for in this task (a capture draws at once)
    this.applyLook();
    const moving = this.camera.step(performance.now());
    // the trays' titles (START, GOAL) over the Puzzle view
    let tags = '';
    if (this.puzzleMode) {
      // the first layer that draws the Puzzle view, else the pieces scene
      let drawn: string | null = null;
      for (const l of this.layers3d) {
        drawn = l.drawPuzzle();
        if (drawn !== null) {
          break;
        }
      }
      if (drawn === null) {
        this.drawPiecesAlone();
      }
      tags = drawn ?? '';
    } else if (this.solverMode) {
      const r = this.renderer;
      const extent = this.solutionModel?.extent ?? [1, 1, 1];
      r.frame(this.camera, extent);
      this.solution.setLabelScale(pixelsPerUnit(r.width, r.height, extent, this.camera.zoom));
      r.render();
      // the labels drawn, readable from outside (the e2e tests)
      byId('vp').dataset.pieceLabels = this.solution.labelsShown();
      // where each piece is on the screen (the e2e tests drag them): worked
      // out again only when the poses or the camera changed (cleanup item 10.1)
      const c = this.camera;
      const at = [
        this.solution.version,
        c.q.x,
        c.q.y,
        c.q.z,
        c.q.w,
        c.zoom,
        c.px,
        c.py,
        r.width,
        r.height,
        r.perspective ? 1 : 0,
        r.turn,
      ].join(',');
      if (this.solver?.movesPieces() === true && at !== this.piecesOnScreen) {
        this.piecesOnScreen = at;
        byId('vp').dataset.pieceScreen = (this.solutionModel?.pieces ?? [])
          .map((p) => {
            const at = this.pieceOnScreen(p.piece);
            return at === null ? '' : `${String(p.piece)}:${at.x.toFixed(1)},${at.y.toFixed(1)}`;
          })
          .filter((t) => t !== '')
          .join(' ');
      }
    } else {
      this.renderer.frame(this.camera, this.stackingLayer.rodSetExtent ?? this.extent);
      this.renderer.render();
    }
    if (tags !== this.trayTags) {
      this.trayTags = tags;
      byId('trayTags').innerHTML = tags;
    }
    this.cube.draw();
    if (moving || this.camera.moving) {
      this.renderer.request();
    }
    // the view's state, readable from outside (the e2e tests, accessibility
    // tools); written only when it changed
    const a = this.camera.angles;
    const vp = byId('vp');
    const mark = (k: 'camera' | 'moving' | 'triangles' | 'projection', v: string): void => {
      if (vp.dataset[k] !== v) {
        vp.dataset[k] = v;
      }
    };
    mark(
      'camera',
      [a.yaw, a.pitch, a.roll, this.camera.zoom, this.camera.px, this.camera.py]
        .map((v) => (Math.abs(v) < 5e-4 ? 0 : v).toFixed(3))
        .join(','),
    );
    mark('moving', this.camera.moving ? '1' : '0');
    mark('triangles', String(this.renderer.gl.info.render.triangles));
    mark('projection', this.display.persp ? 'perspective' : 'orthographic');
  }

  // the pieces scene over the whole view, or over all of it above a
  // sliding board's validity bar (the 2D focus, where the grids show the
  // trays)
  private drawPiecesAlone(): void {
    const r = this.renderer;
    const valid = byId('slideValid');
    const y1 =
      valid.offsetParent !== null && valid.offsetHeight > 0
        ? Math.max(2, valid.offsetTop - 4)
        : r.height;
    if (y1 >= r.height) {
      this.drawPieces(0);
      return;
    }
    r.gl.clear();
    r.band(0, y1, () => {
      this.drawPieces(0);
    });
  }

  // the pieces scene in the renderer's size; in a band `top` pixels down
  // (under a sliding board's trays) it always fits it: the zoom and the pan
  // are the trays'
  private drawPieces(top: number): void {
    const r = this.renderer;
    const band = top > 0;
    this.pieces.setResolution(r.width, r.height);
    const { sc, reach } = this.pieces.scale(r.width, r.height, band ? 1 : this.camera.zoom, band);
    r.frameScene(
      band ? { px: 0, py: 0 } : this.camera,
      sc,
      Math.max(EYE_DISTANCE, 2.5 * reach),
      reach,
    );
    // a tilted puzzle's pieces lie as they lie in the tray (the view's turn)
    this.pieces.turn(
      this.renderer.turn === 0
        ? this.camera.q
        : this.camera.q
            .clone()
            .multiply(new Quaternion().setFromAxisAngle(new Vector3(0, 0, 1), this.renderer.turn)),
    );
    r.render();
    this.pieces.place(r.camera, r.width, r.height, sc, top);
  }
}

// a piece's mesh with its box: the mesh's own (the frame's lo and hi may
// be larger: a grid's cells need not fill it), so the piece is centerd on
// what is drawn
function pieceMesh(l: MeshLease): PieceMesh {
  const built = l.built;
  const bb = built.faces.boundingBox;
  const lo = bb === null || bb.isEmpty() ? l.reply.lo : bb.min.toArray();
  const hi = bb === null || bb.isEmpty() ? l.reply.hi : bb.max.toArray();
  const center = [0, 1, 2].map((i) => ((lo[i] ?? 0) + (hi[i] ?? 0)) / 2) as V3;
  const radius = Math.max(
    0.5,
    Math.hypot(...[0, 1, 2].map((i) => Math.max(0, (hi[i] ?? 0) - (lo[i] ?? 0)))) / 2,
  );
  const half = [0, 1, 2].map((i) => Math.max(0, (hi[i] ?? 0) - (lo[i] ?? 0)) / 2) as V3;
  return { built, center, radius, half };
}
