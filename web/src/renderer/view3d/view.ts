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
// it, posed, framed on the result). It renders only when something
// changed.

import { Vector2 } from 'three';

import { EngineError } from '../../engine/errors.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId, css, esc } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab, toggleFocus3d } from '../shell/layout.ts';
import { contrast, hex, shapeColour } from '../shell/piece-colours.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { reportError, setCursor } from '../shell/status.ts';
import { isDarkTheme, onTheme } from '../shell/theme.ts';
import { Camera, EYE_DISTANCE } from './camera.ts';
import { gridExtent } from './extent.ts';
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
import { Toolbar } from './toolbar.ts';
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
}

/** What 3D editing does with the view's points (the Entities workspace). */
export interface EditHost {
  /** The face under the mouse changed, or the modifiers did (null: none). */
  hover(pick: Pick | null, mods: ViewMods): void;
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
  m: 'orbit' | 'pan';
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
  private readonly cube: ViewCube;
  private readonly toolbar: Toolbar;
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
        // another style is another mesh
        this.reload();
        this.loadPieces();
        this.solver?.styleChanged();
        this.lookChanged();
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

  /** The Solver's solution (null: none): its pieces with their meshes, framed on the result. */
  setSolution(model: SolutionModel | null): void {
    this.solutionModel = model;
    this.solution.set(model);
    const vp = byId('vp');
    vp.dataset.solution = model?.key ?? '';
    vp.dataset.solutionPieces = this.solution.describe();
    this.lookChanged();
  }

  /** Places the solution's pieces (a pose per piece of the model). */
  poseSolution(poses: readonly ScenePose[]): void {
    this.solution.setPoses(poses);
    const vp = byId('vp');
    vp.dataset.poses = this.solution.posesShown();
    vp.dataset.solutionPieces = this.solution.describe();
    this.renderer.request();
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
      this.edit?.hover(this.lastPick, m);
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
        const built = buildMesh(meshArrays(reply), size, style);
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
        const built = buildMesh(meshArrays(reply), size, style);
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
        return shapeColour(snap?.shapes[i]?.colour ?? null, i);
      },
    };
  }

  // the look and the overlays from the options, the store and the settings
  private lookChanged(): void {
    const puzzle = this.puzzleMode;
    const solver = this.solverMode;
    this.renderer.root.visible = !puzzle;
    this.renderer.model.visible = !solver;
    this.pieces.group.visible = puzzle;
    this.solution.group.visible = solver;
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
    if (puzzle) {
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
      colourView: this.display.colourView,
      lit: prefs.lights,
      xray: this.display.xray,
      piece: shapeColour(shape?.colour ?? null, index),
      palette: snap?.colours.map((c) => c.rgb) ?? [],
      dim: this.display.dim && ed ? { axis: layerAxis(this.plane), layer: this.layer } : null,
    });
    const showSlab = this.display.layer && ed && snap !== null && shape !== null && !solver;
    this.scene.setOverlays({
      extent: this.extent,
      axes: this.display.axes && shape !== null && tab === 'entities',
      bounds: this.display.bounds && shape !== null && tab === 'entities',
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
    if (shape === null) {
      tag.innerHTML = '';
      return;
    }
    const c = shapeColour(shape.colour, this.store.indexOf(shape.id));
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
    } else if (s.snapshot.shapes.length === 0) {
      text = 'No shapes — add one with + New';
    } else if (s.shape !== null && s.shape.fixed + s.shape.variable === 0) {
      text = 'Empty shape — draw in the Voxel editor';
    }
    el.textContent = text;
    el.classList.toggle('show', text !== '');
  }

  private draw(): void {
    const moving = this.camera.step(performance.now());
    if (this.puzzleMode) {
      const r = this.renderer;
      const { sc, reach } = this.pieces.scale(r.width, r.height, this.camera.zoom);
      r.frameScene(this.camera, sc, Math.max(EYE_DISTANCE, 2.5 * reach), reach);
      this.pieces.turn(this.camera.q);
      r.render();
      this.pieces.place(r.camera, r.width, r.height, sc);
    } else if (this.solverMode) {
      this.renderer.frame(this.camera, this.solutionModel?.extent ?? [1, 1, 1]);
      this.renderer.render();
    } else {
      this.renderer.frame(this.camera, this.extent);
      this.renderer.render();
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
    const r = this.canvas.getBoundingClientRect();
    const ndc = new Vector2(
      ((clientX - r.left) / r.width) * 2 - 1,
      -(((clientY - r.top) / r.height) * 2 - 1),
    );
    this.renderer.frame(this.camera, this.extent);
    return pickAt(ndc, this.renderer.camera, target, mesh, this.scene.insideShown);
  }

  private showPick(p: Pick | null): void {
    this.lastPick = p;
    setCursor(p === null ? '' : describePick(p));
    this.edit?.hover(p, this.mods);
  }

  private repick(): void {
    if (this.hoverAt !== null && this.drag === null) {
      this.showPick(this.pickAtClient(this.hoverAt.x, this.hoverAt.y));
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
      this.drag = {
        x: e.clientX,
        y: e.clientY,
        sx: e.clientX,
        sy: e.clientY,
        moved: false,
        btn: e.button,
        m: e.button === 1 ? 'pan' : e.shiftKey ? other : this.mode,
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
        this.showPick(this.pickAtClient(e.clientX, e.clientY));
        return;
      }
      if (!d.moved) {
        if (Math.hypot(e.clientX - d.sx, e.clientY - d.sy) <= 4) {
          return;
        }
        d.moved = true;
        // a drag navigates: no edit preview while it lasts
        this.edit?.hover(null, this.mods);
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
      if (was !== null && was.btn === 0 && !was.moved && this.puzzleMode) {
        // a click on a piece selects it (the mockup's puzClick)
        const shape = this.pieceAt(e.clientX, e.clientY);
        if (shape !== null) {
          this.puzzle?.pick(shape);
        }
      } else if (was !== null && was.btn === 0 && !was.moved) {
        // a click: the cell and side under it, edited when the workspace edits
        const p = this.pickAtClient(e.clientX, e.clientY);
        this.edit?.click(p, this.mods);
        this.showPick(p);
      } else if (was?.moved === true) {
        this.repick();
      }
    });
    cv.addEventListener('pointerleave', () => {
      if (this.drag === null) {
        this.hoverAt = null;
        this.hoverPiece(null);
        this.showPick(null);
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
