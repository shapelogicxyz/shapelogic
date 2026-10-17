// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view: the shape the store selects, drawn from the engine's mesh
// (meshcache.ts, mesh.ts, scene.ts), navigated as the mockup's view
// (docs/mockup/index.html: the canvas's pointer handlers: a drag past 4 px
// orbits or pans by the mode, Shift the other, the middle button pans; the
// wheel zooms, reversed by the setting; `renderTag`; the empty message),
// with the original's arc-ball when the setting says so. A point under the
// mouse names its cell and side in the status bar (picking.ts); with an
// edit host (the Entities workspace, stage 2) the point under the mouse
// and a click (not a drag) go to it, with the modifiers held, for 3D
// editing and its preview. In the Puzzle workspace it draws the pieces
// scene instead (pieces.ts: the problem's result and pieces, one mesh per
// shape; a click selects a piece through the puzzle host), in the Solver
// workspace a solution (solution.ts: the pieces the Solver's player gives
// it, posed, framed on the result). A stacking puzzle (docs/webui-spec.md,
// section 9) is drawn tilted as the original's (renderer.ts), with its
// board (board.ts): the rod set alone or a disc in the Entities workspace,
// the board with the start's or the goal's discs in the Puzzle workspace
// (instead of the pieces scene), the pegs around the player's discs. It
// renders only when something changed.

import { Box3, Raycaster, Vector2, Vector3 } from 'three';

import { EngineError } from '../../engine/errors.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { MeshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId, css, esc } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab, toggleFocus3d } from '../shell/layout.ts';
import { chipColour, contrast, hex, ROD_CHIP, viewColour } from '../shell/piece-colours.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { reportError, setCursor } from '../shell/status.ts';
import { isDarkTheme, onTheme } from '../shell/theme.ts';
import { Camera, EYE_DISTANCE } from './camera.ts';
import { gridExtent } from './extent.ts';
import { BoardView } from './board.ts';
import type { BoardModel } from './board.ts';
import { buildMesh } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';
import { MeshCache } from './meshcache.ts';
import { layerAxis, layerSlab } from './overlays.ts';
import type { Plane } from './overlays.ts';
import { describePick, pickAt } from './picking.ts';
import { PiecesScene } from './pieces.ts';
import type { PieceMesh, PiecesModel } from './pieces.ts';
import { SolutionScene } from './solution.ts';
import type { ScenePose, SolutionModel } from './solution.ts';
import type { Pick } from './picking.ts';
import type { Ghost } from './preview.ts';
import { ViewRenderer } from './renderer.ts';
import { ShapeScene } from './scene.ts';
import { vectorImage } from './svg.ts';
import { Toolbar } from './toolbar.ts';
import { TrayView } from './trays.ts';
import { wallOutline, wallSegments } from './walls.ts';
import type { TrayModel } from './trays.ts';
import type { Display, EditButton, NavMode } from './toolbar.ts';
import { ViewCube } from './viewcube.ts';

type V3 = [number, number, number];

/** The modifiers of a pointer event or key (Cmd counts as Alt, as the mockup's `mods`). */
export interface ViewMods {
  ctrl: boolean;
  shift: boolean;
  alt: boolean;
}

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

/** Who draws in the Solver's view: the player, or an explorer over it while one is open. */
export type SolutionLayer = 'player' | 'explorer';

/** What 3D editing does with the view's points (the Entities workspace). */
export interface EditHost {
  /**
   * The face under the mouse changed, or the modifiers did (null: none);
   * `over` says whether the mouse is over the shape's space (its grid box,
   * the empty cells too).
   */
  hover(pick: Pick | null, mods: ViewMods, over: boolean): void;
  /** A click (no drag) on a face, or on nothing; true when it was taken. */
  click(pick: Pick | null, mods: ViewMods): boolean;
  /** The Edit button. */
  toggle(): void;
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

export class View3D {
  readonly camera = new Camera();
  readonly display: Display = {
    axes: true,
    bounds: true,
    layer: true,
    dim: false,
    persp: true,
    colourView: false,
    style: 'drawing',
    xray: false,
  };
  mode: NavMode = 'orbit';
  /** The voxel editor's plane and layer (the slab, the dimmed layers). */
  plane: Plane = 'XY';
  layer = 0;
  /** The last pick under the mouse, or null. */
  lastPick: Pick | null = null;
  /** Whether the mouse was last over the shape's space (its grid box). */
  private overSpace = false;
  /** 3D editing, when a workspace does it. */
  edit: EditHost | null = null;
  /** The Puzzle workspace, once it is there. */
  puzzle: PuzzleHost | null = null;
  /** The Solver workspace, once it is there. */
  solver: SolverHost | null = null;
  private mods: ViewMods = { ctrl: false, shift: false, alt: false };

  private readonly store: Store;
  private readonly cache: MeshCache;
  private readonly renderer: ViewRenderer;
  private readonly scene: ShapeScene;
  /** A sliding start/goal shape's GOAL tray and pieces (the Entities view). */
  private readonly trays: TrayView;
  private trayTags = '';
  private wallLoads = 0;
  /** A stacking puzzle's board: the Entities' rod set, the Puzzle's stacking, the player's pegs. */
  private readonly board: BoardView;
  private readonly boards: { entities: BoardModel | null; puzzle: BoardModel | null } = {
    entities: null,
    puzzle: null,
  };
  private readonly cube: ViewCube;
  private readonly toolbar: Toolbar;
  private readonly displayListeners = new Set<() => void>();
  private readonly canvas: HTMLCanvasElement;
  private extent: V3 = [1, 1, 1];
  /** What the mesh shown is of: shape, style, generation, revision. */
  private shownKey = '';
  private loading = 0;
  private drag: Drag | null = null;
  private hoverAt: { x: number; y: number } | null = null;
  private readonly pieces: PiecesScene;
  private piecesModel: PiecesModel | null = null;
  private readonly solution = new SolutionScene();
  private solutionModel: SolutionModel | null = null;
  /** What each layer would show; the explorer's over the player's while it has a model. */
  private readonly layers: Record<
    SolutionLayer,
    { model: SolutionModel | null; poses: readonly ScenePose[] }
  > = { player: { model: null, poses: [] }, explorer: { model: null, poses: [] } };
  private shownLayer: SolutionLayer = 'player';
  /** What the pieces scene shows: the model's key and the style. */
  private piecesShown = '';
  private piecesLoading = 0;
  /** The pieces scene's meshes by shape, with what they were made of. */
  private pieceMeshes = new Map<string, PieceMesh & { key: string }>();
  /** The revision each shape last changed at (document.changed), per document. */
  private readonly shapeRev = new Map<string, number>();
  private revGeneration = -1;
  private revBase = 0;

  constructor(store: Store) {
    this.store = store;
    this.cache = new MeshCache(store.engine);
    this.canvas = byId<HTMLCanvasElement>('cv');
    this.renderer = new ViewRenderer(this.canvas);
    this.scene = new ShapeScene(this.renderer.model);
    this.trays = new TrayView(
      {
        store,
        cache: this.cache,
        shapeRevision: (s) => this.shapeRevision(s),
        style: () => this.display.style,
        changed: () => {
          byId('vp').dataset.tray = this.trays.describe();
          byId('vp').dataset.trayPoses = this.trays.posesShown();
          this.renderer.request();
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
      this.renderer.model,
    );
    this.board = new BoardView({
      store,
      cache: this.cache,
      shapeRevision: (s) => this.shapeRevision(s),
      style: () => this.display.style,
      changed: () => {
        const vp = byId('vp');
        vp.dataset.board = this.board.describe();
        vp.dataset.boardPoses = this.board.posesShown();
        vp.dataset.boardBoxes = String(this.board.boxMeshes);
        this.renderer.request();
      },
      failed: (e) => {
        if (e instanceof EngineError && e.errc === 'superseded') {
          queueMicrotask(() => {
            this.board.load();
          });
          return;
        }
        reportError(e, 'The board could not be drawn');
      },
    });
    this.board.group.visible = false;
    this.renderer.root.add(this.board.group);
    this.pieces = new PiecesScene(byId('pzScene'));
    this.pieces.group.visible = false;
    this.renderer.scene.add(this.pieces.group);
    // centred on the result (the poses' frame), turned by the camera as the shape is
    this.solution.group.visible = false;
    this.renderer.root.add(this.solution.group);
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
    this.toolbar = new Toolbar({
      display: this.display,
      mode: () => this.mode,
      setMode: (m) => {
        this.setMode(m);
      },
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
          this.reload();
          break;
        case 'status':
          this.renderEmpty();
          break;
        case 'changed': {
          const p = c.change;
          if (p.what.includes('shapes') || p.what.includes('colours')) {
            this.shapeRevision('');
            if (p.shapes.length === 0 || p.what.includes('colours')) {
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
      if (changed.includes('lights')) {
        this.lookChanged();
      }
      if (changed.includes('density')) {
        this.toolbar.reset();
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
    this.installPointer();
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

  /**
   * The Solver's solution (null: none): its pieces with their meshes, framed
   * on the result. The player draws in its layer; an explorer's model, while
   * it has one, is shown over it (the player's comes back when it goes).
   */
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

  setSolution(model: SolutionModel | null, layer: SolutionLayer = 'player'): void {
    this.layers[layer].model = model;
    if (layer === 'explorer' && model === null) {
      this.layers.explorer.poses = [];
    }
    const shown: SolutionLayer = this.layers.explorer.model !== null ? 'explorer' : 'player';
    if (layer !== shown && shown === this.shownLayer) {
      return;
    }
    this.shownLayer = shown;
    const l = this.layers[shown];
    this.solutionModel = l.model;
    this.solution.set(l.model);
    this.solution.setPoses(l.poses);
    this.loadWalls(l.model);
    this.showBoard();
    const vp = byId('vp');
    vp.dataset.solution = l.model?.key ?? '';
    vp.dataset.solutionLayer = shown;
    vp.dataset.poses = this.solution.posesShown();
    vp.dataset.solutionPieces = this.solution.describe();
    this.lookChanged();
  }

  /** Places the solution's pieces (a pose per piece of the layer's model). */
  poseSolution(poses: readonly ScenePose[], layer: SolutionLayer = 'player'): void {
    this.layers[layer].poses = [...poses];
    if (layer !== this.shownLayer) {
      return;
    }
    this.solution.setPoses(poses);
    const vp = byId('vp');
    vp.dataset.poses = this.solution.posesShown();
    vp.dataset.solutionPieces = this.solution.describe();
    this.renderer.request();
  }

  // a sliding tray's walls around the solution (buildSlidingWalls), from
  // the result's floor as the engine has it
  private loadWalls(model: SolutionModel | null): void {
    const store = this.store;
    const snap = store.snapshot;
    const result = model?.result ?? null;
    const n = ++this.wallLoads;
    if (model === null || result === null || snap?.grid !== 'sliding') {
      this.solution.setWalls(null);
      byId('vp').dataset.walls = '0';
      return;
    }
    store.engine.call('shape.get', { shape: result }, { coalesce: 'walls' }).then(
      (r) => {
        if (n !== this.wallLoads) {
          return;
        }
        const [sx, sy, sz] = r.shape.size;
        const open = new Set(r.cells.map((c) => `${String(c.at[0])},${String(c.at[1])}`));
        const outline = wallOutline(sx, sy, (x, y) => open.has(`${String(x)},${String(y)}`));
        this.solution.setWalls(wallSegments(outline, sx, sy, sz));
        byId('vp').dataset.walls = String(this.solution.wallCount);
        this.renderer.request();
      },
      (e: unknown) => {
        if (n === this.wallLoads && !(e instanceof EngineError && e.errc === 'superseded')) {
          reportError(e, 'The walls could not be drawn');
        }
      },
    );
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
    const c = this.solution.centreOf(piece);
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

  /** A sliding start/goal shape's two trays (null: the shape shown is no tray). */
  setTray(model: TrayModel | null): void {
    this.trays.set(model);
  }

  /**
   * A stacking puzzle's board in a workspace (null: none): the Entities'
   * rod set shown alone instead of the disc, the Puzzle's start or goal.
   */
  setBoard(model: BoardModel | null, where: 'entities' | 'puzzle'): void {
    this.boards[where] = model;
    this.showBoard();
    this.renderTag();
    this.lookChanged();
  }

  /** Whether the Entities workspace shows a rod set instead of its disc. */
  get rodSetShown(): boolean {
    return tab === 'entities' && this.boards.entities !== null;
  }

  private get stacking(): boolean {
    return this.store.snapshot?.grid === 'stacking';
  }

  // the board of the workspace shown (the player's: its pegs, no discs)
  private showBoard(): void {
    let model: BoardModel | null = null;
    if (this.stacking) {
      if (tab === 'entities') {
        model = this.boards.entities;
      } else if (tab === 'puzzle') {
        model = this.boards.puzzle;
      } else {
        const b = this.solutionModel?.board ?? null;
        model =
          b === null ? null : { key: `solver:${this.solutionModel?.key ?? ''}`, ...b, discs: [] };
      }
    }
    this.board.set(model);
    this.board.group.visible = model !== null;
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
    this.scene.setPreview(ghosts);
    byId('vp').dataset.preview = String(this.scene.preview.size);
    this.renderer.request();
  }

  /** Shows the shape as a tool tab button would leave it, over the shape (null: none). */
  setTaskGhost(arrays: MeshArrays | null): void {
    this.scene.setTaskGhost(arrays);
    byId('vp').dataset.taskGhost = String(this.scene.taskGhost.size);
    this.renderer.request();
  }

  /** The view's mouse cursor (empty: the navigation's). */
  setCursor(cursor: string): void {
    this.canvas.style.cursor = cursor;
  }

  /** The Edit button. */
  renderEditButton(e: EditButton): void {
    this.toolbar.renderEdit(e);
  }

  /** The modifiers held changed (a key): the hover is told again. */
  modsChanged(m: ViewMods): void {
    this.mods = m;
    if (this.hoverAt !== null && this.drag === null) {
      this.edit?.hover(this.lastPick, m, this.overSpace);
    }
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
    this.trays.setResolution(vp.clientWidth, vp.clientHeight);
    this.board.setResolution(vp.clientWidth, vp.clientHeight);
    this.renderer.request();
  }

  private themeChanged(): void {
    this.renderer.setBackground(css('--canvas') || '#e4e8ef');
    this.lookChanged();
  }

  // the shape (or its absence) the store selects: fetch its mesh
  private reload(): void {
    const store = this.store;
    const snap = store.snapshot;
    const shape = store.shape;
    this.renderTag();
    this.trays.load();
    this.board.load();
    if (snap === null || shape === null) {
      this.shownKey = '';
      this.scene.setMesh(null);
      delete byId('vp').dataset.shown;
      this.lookChanged();
      return;
    }
    this.extent = gridExtent(snap.grid, shape.size);
    const style = this.display.style;
    const key = `${store.generation}:${shape.id}:${style}:${snap.revision}`;
    if (key === this.shownKey) {
      this.lookChanged();
      return;
    }
    const n = ++this.loading;
    const size = shape.size;
    const revision = snap.revision;
    this.cache.get(shape.id, style, revision).then(
      (reply) => {
        if (n !== this.loading) {
          return;
        }
        const t0 = performance.now();
        const built = buildMesh(meshArrays(reply), size, style, {
          kind: 'shape',
          shape: shape.id,
          revision,
          transformation: 0,
        });
        performance.measure('view3d:mesh-build', {
          start: t0,
          end: performance.now(),
          detail: { cells: shape.fixed + shape.variable, triangles: reply.triangles },
        });
        this.shownKey = key;
        this.scene.setMesh(built);
        byId('vp').dataset.shown = `${shape.id}:${style}`;
        byId('vp').dataset.revision = String(revision);
        this.lookChanged();
        this.repick();
      },
      (e: unknown) => {
        if (n === this.loading) {
          reportError(e, 'The shape could not be drawn');
        }
      },
    );
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
      return;
    }
    const n = ++this.piecesLoading;
    if (model === null) {
      this.showPieces(n, null, new Map(), shown);
      return;
    }
    const store = this.store;
    const gen = store.generation;
    const shapes = [...new Set(model.items.map((i) => i.shape))];
    Promise.all(
      shapes.map(async (s): Promise<[string, PieceMesh & { key: string }]> => {
        const rev = this.shapeRevision(s);
        const key = `${String(gen)}:${s}:${style}:${String(rev)}`;
        const have = this.pieceMeshes.get(s);
        if (have?.key === key) {
          return [s, have];
        }
        const size = store.snapshot?.shapes.find((x) => x.id === s)?.size ?? [1, 1, 1];
        const reply = await this.cache.get(s, style, rev);
        const built = buildMesh(meshArrays(reply), size, style, {
          kind: 'shape',
          shape: s,
          revision: rev,
          transformation: 0,
        });
        const lo = reply.lo;
        const hi = reply.hi;
        const c = [0, 1, 2].map((i) => ((lo[i] ?? 0) + (hi[i] ?? 0)) / 2) as [
          number,
          number,
          number,
        ];
        const radius = Math.max(
          0.5,
          Math.hypot(...[0, 1, 2].map((i) => Math.max(0, (hi[i] ?? 0) - (lo[i] ?? 0)))) / 2,
        );
        return [s, { built, centre: c, radius, key }];
      }),
    ).then(
      (list) => {
        if (n !== this.piecesLoading) {
          // a newer scene took its place: free what only this one made
          for (const [s, m] of list) {
            if (this.pieceMeshes.get(s) !== m) {
              disposeBuilt(m.built);
            }
          }
          return;
        }
        this.showPieces(n, model, new Map(list), shown);
      },
      (e: unknown) => {
        if (n !== this.piecesLoading) {
          return;
        }
        if (e instanceof EngineError && e.errc === 'superseded') {
          // a newer request of a mesh took this one's place: ask again (it is cached now)
          queueMicrotask(() => {
            if (n === this.piecesLoading) {
              this.loadPieces();
            }
          });
          return;
        }
        reportError(e, 'The pieces could not be drawn');
      },
    );
  }

  private showPieces(
    n: number,
    model: PiecesModel | null,
    meshes: Map<string, PieceMesh & { key: string }>,
    shown: string,
  ): void {
    if (n !== this.piecesLoading) {
      return;
    }
    const old = this.pieceMeshes;
    this.pieceMeshes = meshes;
    this.pieces.set(
      model ?? { key: '', items: [], hidden: 0, selected: null, names: new Map() },
      meshes,
      this.piecesLook(),
    );
    for (const [s, m] of old) {
      if (meshes.get(s) !== m) {
        disposeBuilt(m.built);
      }
    }
    this.piecesShown = shown;
    const vp = byId('vp');
    vp.dataset.scene = this.pieces.describe();
    vp.dataset.sceneMeshes = String(this.pieces.meshCount);
    vp.dataset.sceneKey = model?.key ?? '';
    vp.dataset.sceneStyle = this.display.style;
    this.renderLabels();
    this.renderer.request();
  }

  private piecesLook(): Parameters<PiecesScene['setLook']>[0] {
    const snap = this.store.snapshot;
    return {
      style: this.display.style,
      colourView: this.display.colourView,
      lit: prefs.lights,
      palette: snap?.colours.map((c) => c.rgb) ?? [],
      colourOf: (shape) => {
        const i = this.store.indexOf(shape);
        return viewColour(snap?.shapes[i], i);
      },
    };
  }

  // the look and the overlays from the options, the store and the settings
  private lookChanged(): void {
    const puzzle = this.puzzleMode;
    const solver = this.solverMode;
    const stacking = this.stacking;
    // a stacking puzzle's Puzzle workspace draws its board, not the pieces scene
    this.renderer.tilt = stacking;
    this.renderer.root.visible = !puzzle || stacking;
    this.renderer.model.visible = !solver && !puzzle && !this.rodSetShown;
    this.pieces.group.visible = puzzle && !stacking;
    this.solution.group.visible = solver;
    this.showBoard();
    this.board.setLook({
      style: this.display.style,
      colourView: false,
      lit: prefs.lights,
      xray: false,
      palette: [],
    });
    if (solver) {
      const snap0 = this.store.snapshot;
      this.solution.setLook({
        style: this.display.style,
        colourView: this.display.colourView,
        lit: prefs.lights,
        xray: this.display.xray,
        palette: snap0?.colours.map((c) => c.rgb) ?? [],
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
    this.trays.setLook({
      style: this.display.style,
      colourView: false,
      lit: prefs.lights,
      xray: false,
      palette: [],
    });
    this.scene.setLook({
      style: this.display.style,
      colourView: this.display.colourView,
      lit: prefs.lights,
      xray: this.display.xray,
      piece: viewColour(shape ?? undefined, index),
      palette: snap?.colours.map((c) => c.rgb) ?? [],
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
      colours: {
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
    const rods = this.boards.entities;
    if (this.rodSetShown && rods !== null) {
      // the rod set shown alone (showSelectedRods)
      tag.innerHTML = `<span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(rods.label ?? '')}</span><span>Rod set</span>`;
      return;
    }
    if (shape === null) {
      tag.innerHTML = '';
      return;
    }
    const c = chipColour(shape, this.store.indexOf(shape.id));
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
      text = this.stacking ? 'No discs — add one with + New' : 'No shapes — add one with + New';
    } else if (s.shape !== null && s.shape.fixed + s.shape.variable === 0) {
      text = 'Empty shape — draw in the Voxel editor';
    }
    el.textContent = text;
    el.classList.toggle('show', text !== '');
  }

  private draw(): void {
    const moving = this.camera.step(performance.now());
    if (this.puzzleMode && this.stacking) {
      this.renderer.frame(this.camera, this.board.extent ?? [1, 1, 1]);
      this.renderer.render();
    } else if (this.puzzleMode) {
      const r = this.renderer;
      const { sc, reach } = this.pieces.scale(r.width, r.height, this.camera.zoom);
      r.frameScene(this.camera, sc, Math.max(EYE_DISTANCE, 2.5 * reach), reach);
      this.pieces.turn(this.camera.q);
      r.render();
      this.pieces.place(r.camera, r.width, r.height, sc);
    } else if (this.solverMode) {
      this.renderer.frame(this.camera, this.solutionModel?.extent ?? [1, 1, 1]);
      this.renderer.render();
      if (this.solver?.movesPieces() === true) {
        // where each piece is on the screen (the e2e tests drag them)
        byId('vp').dataset.pieceScreen = (this.solutionModel?.pieces ?? [])
          .map((p) => {
            const at = this.pieceOnScreen(p.piece);
            return at === null ? '' : `${String(p.piece)}:${at.x.toFixed(1)},${at.y.toFixed(1)}`;
          })
          .filter((t) => t !== '')
          .join(' ');
      }
    } else {
      const ex = this.rodSetShown ? this.board.extent : this.trays.extent;
      this.renderer.frame(this.camera, ex ?? this.extent);
      this.renderer.render();
    }
    // the trays' titles (START, GOAL) over the Entities view
    const tags =
      !this.puzzleMode && !this.solverMode
        ? this.trays.tags(this.renderer.model, (v) => this.toScreen(v))
        : '';
    if (tags !== this.trayTags) {
      this.trayTags = tags;
      byId('trayTags').innerHTML = tags;
    }
    this.cube.draw();
    if (moving || this.camera.moving) {
      this.renderer.request();
    }
    // the view's state, readable from outside (the e2e tests, accessibility tools)
    const a = this.camera.angles;
    const vp = byId('vp');
    vp.dataset.camera = [a.yaw, a.pitch, a.roll, this.camera.zoom, this.camera.px, this.camera.py]
      .map((v) => (Math.abs(v) < 5e-4 ? 0 : v).toFixed(3))
      .join(',');
    vp.dataset.moving = this.camera.moving ? '1' : '0';
    vp.dataset.triangles = String(this.renderer.gl.info.render.triangles);
    vp.dataset.projection = this.display.persp ? 'perspective' : 'orthographic';
  }

  // ---- picking ----

  // the piece of the solution shown under a point
  private solutionPieceAt(clientX: number, clientY: number): number | null {
    const r = this.canvas.getBoundingClientRect();
    const ndc = new Vector2(
      ((clientX - r.left) / r.width) * 2 - 1,
      -(((clientY - r.top) / r.height) * 2 - 1),
    );
    this.renderer.frame(this.camera, this.solutionModel?.extent ?? [1, 1, 1]);
    const ray = new Raycaster();
    ray.setFromCamera(ndc, this.renderer.camera);
    return this.solution.pick(ray);
  }

  // the piece under a point in the pieces scene (the mockup's puzPick)
  private pieceAt(clientX: number, clientY: number): string | null {
    const r = this.canvas.getBoundingClientRect();
    return this.pieces.pick(clientX - r.left, clientY - r.top);
  }

  private hoverPiece(shape: string | null): void {
    if (this.pieces.setHover(shape)) {
      this.canvas.style.cursor = shape === null ? '' : 'pointer';
    }
  }

  private pickAtClient(clientX: number, clientY: number): Pick | null {
    if (this.puzzleMode || this.solverMode) {
      return null;
    }
    const target = this.scene.target;
    const mesh = this.scene.mesh;
    if (target === null || mesh === null) {
      return null;
    }
    return pickAt(
      this.ndcAt(clientX, clientY),
      this.renderer.camera,
      target,
      mesh,
      this.scene.insideShown,
    );
  }

  // a point of the canvas in normalised device coordinates, the shape framed
  private ndcAt(clientX: number, clientY: number): Vector2 {
    const r = this.canvas.getBoundingClientRect();
    this.renderer.frame(this.camera, this.extent);
    return new Vector2(
      ((clientX - r.left) / r.width) * 2 - 1,
      -(((clientY - r.top) / r.height) * 2 - 1),
    );
  }

  // whether a point is over the shape's space (its grid box), where an
  // empty cell may be: 3D editing's cursor shows there, not only on faces
  private spaceAtClient(clientX: number, clientY: number): boolean {
    if (this.puzzleMode || this.solverMode || this.rodSetShown || this.store.shape === null) {
      return false;
    }
    const ray = new Raycaster();
    ray.setFromCamera(this.ndcAt(clientX, clientY), this.renderer.camera);
    ray.ray.applyMatrix4(this.renderer.model.matrixWorld.clone().invert());
    return ray.ray.intersectsBox(new Box3(new Vector3(0, 0, 0), new Vector3(...this.extent)));
  }

  private showPick(p: Pick | null, over: boolean): void {
    this.lastPick = p;
    this.overSpace = over;
    setCursor(p === null ? '' : describePick(p));
    this.edit?.hover(p, this.mods, over);
  }

  // the face and the space under a point
  private showPickAt(clientX: number, clientY: number): void {
    this.showPick(this.pickAtClient(clientX, clientY), this.spaceAtClient(clientX, clientY));
  }

  private repick(): void {
    if (this.hoverAt !== null && this.drag === null) {
      this.showPickAt(this.hoverAt.x, this.hoverAt.y);
    }
  }

  // ---- navigation (the mockup's pointer handlers) ----

  private installPointer(): void {
    const cv = this.canvas;
    cv.addEventListener('contextmenu', (e) => {
      e.preventDefault();
    });
    cv.addEventListener('pointerdown', (e) => {
      this.camera.stop();
      if (e.button === 2) {
        return;
      }
      if (e.button === 1) {
        e.preventDefault();
      }
      cv.setPointerCapture(e.pointerId);
      const other = this.mode === 'orbit' ? 'pan' : 'orbit';
      // an Alt-drag on a piece moves it (the movement explorer)
      const piece =
        e.button === 0 && modsOf(e).alt && this.solverMode && this.solver?.movesPieces() === true
          ? this.solutionPieceAt(e.clientX, e.clientY)
          : null;
      this.drag = {
        x: e.clientX,
        y: e.clientY,
        sx: e.clientX,
        sy: e.clientY,
        moved: false,
        btn: e.button,
        m: piece !== null ? 'piece' : e.button === 1 ? 'pan' : e.shiftKey ? other : this.mode,
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
        if (this.solverMode && this.solver?.movesPieces() === true) {
          // the piece an Alt-drag would move
          const hp = this.solutionPieceAt(e.clientX, e.clientY);
          byId('vp').dataset.hoverPiece = hp === null ? '' : String(hp);
          this.canvas.style.cursor = hp !== null && this.mods.alt ? 'grab' : '';
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
        this.edit?.hover(null, this.mods, false);
        d.x = e.clientX;
        d.y = e.clientY;
        if (d.m === 'orbit' && prefs.rot === 'Arc-ball') {
          const r = cv.getBoundingClientRect();
          this.camera.arcStart(d.sx - r.left, d.sy - r.top, r.width, r.height);
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
        if (prefs.rot === 'Arc-ball') {
          const r = cv.getBoundingClientRect();
          this.camera.arcDrag(e.clientX - r.left, e.clientY - r.top);
        } else {
          this.camera.orbit(dx, dy);
        }
      } else {
        this.camera.pan(dx, dy);
      }
      this.renderer.request();
    });
    cv.addEventListener('pointerup', (e) => {
      const was = this.drag;
      this.drag = null;
      this.camera.arcEnd();
      this.mods = modsOf(e);
      if (was?.m === 'piece') {
        if (was.moved) {
          this.solver?.pieceDragged(was.piece, e.clientX - was.sx, e.clientY - was.sy);
        }
      } else if (was !== null && was.btn === 0 && !was.moved && this.puzzleMode) {
        // a click on a piece selects it (the mockup's puzClick)
        const shape = this.pieceAt(e.clientX, e.clientY);
        if (shape !== null) {
          this.puzzle?.pick(shape);
        }
      } else if (was !== null && was.btn === 0 && !was.moved) {
        // a click: the cell and side under it, edited when the workspace edits
        const p = this.pickAtClient(e.clientX, e.clientY);
        this.edit?.click(p, this.mods);
        this.showPick(p, this.spaceAtClient(e.clientX, e.clientY));
      } else if (was?.moved === true) {
        this.repick();
      }
    });
    cv.addEventListener('pointerleave', () => {
      if (this.drag === null) {
        this.hoverAt = null;
        this.hoverPiece(null);
        this.showPick(null, false);
      }
    });
    cv.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        const dy = e.deltaY * (e.deltaMode === 1 ? 33 : 1);
        this.camera.zoomBy(Math.exp(-dy * (prefs.rev ? -1 : 1) * 0.0016), performance.now());
        this.renderer.request();
      },
      { passive: false },
    );
  }
}

function disposeBuilt(b: BuiltMesh): void {
  b.faces.dispose();
  b.marks?.dispose();
}
