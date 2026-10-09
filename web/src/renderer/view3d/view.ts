// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view: the shape the store selects, drawn from the engine's mesh
// (meshcache.ts, mesh.ts, scene.ts), navigated as the mockup's view
// (docs/mockup/index.html: the canvas's pointer handlers: a drag past 4 px
// orbits or pans by the mode, Shift the other, the middle button pans; the
// wheel zooms, reversed by the setting; `renderTag`; the empty message),
// with the original's arc-ball when the setting says so. A point under the
// mouse names its cell and side in the status bar (picking.ts). It renders
// only when something changed.

import { Vector2 } from 'three';

import { meshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { byId, css, esc } from '../shell/dom.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab, toggleFocus3d } from '../shell/layout.ts';
import { contrast, hex, shapeColour } from '../shell/piece-colours.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { reportError, setCursor } from '../shell/status.ts';
import { isDarkTheme, onTheme } from '../shell/theme.ts';
import { Camera } from './camera.ts';
import { gridExtent } from './extent.ts';
import { buildMesh } from './mesh.ts';
import { MeshCache } from './meshcache.ts';
import { layerAxis, layerSlab } from './overlays.ts';
import type { Plane } from './overlays.ts';
import { describePick, pickAt } from './picking.ts';
import type { Pick } from './picking.ts';
import { ViewRenderer } from './renderer.ts';
import { ShapeScene } from './scene.ts';
import { Toolbar } from './toolbar.ts';
import type { Display, NavMode } from './toolbar.ts';
import { ViewCube } from './viewcube.ts';

type V3 = [number, number, number];

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
  /** The voxel editor's plane and layer (stage 2 moves them). */
  plane: Plane = 'XY';
  layer = 0;
  /** The last pick under the mouse, or null. */
  lastPick: Pick | null = null;

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

  constructor(store: Store) {
    this.store = store;
    this.cache = new MeshCache(store.engine);
    this.canvas = byId<HTMLCanvasElement>('cv');
    this.renderer = new ViewRenderer(this.canvas);
    this.scene = new ShapeScene(this.renderer.model);
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
      },
      toggleFocus: toggleFocus3d,
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
        case 'changed':
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
    onKey('view.home', () => {
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

  private setMode(m: NavMode): void {
    this.mode = m;
    this.toolbar.renderMode();
  }

  private resized(): void {
    const vp = byId('vp');
    this.renderer.resize(vp.clientWidth, vp.clientHeight);
    this.scene.setResolution(vp.clientWidth, vp.clientHeight);
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
    this.cache.get(shape.id, style, snap.revision).then(
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

  // the look and the overlays from the options, the store and the settings
  private lookChanged(): void {
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
    const showSlab = this.display.layer && ed && snap !== null && shape !== null;
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
  private renderTag(): void {
    const shape = this.store.shape;
    const tag = byId('vtag');
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
    if (s.status === null || s.status.state === 'exited') {
      text = 'Starting the engine…';
    } else if (s.status.state === 'failed') {
      text = `The engine could not be started: ${s.status.reason}`;
    } else if (s.snapshot === null) {
      text = '';
    } else if (s.snapshot.shapes.length === 0) {
      text = 'No shapes — open a puzzle';
    } else if (s.shape !== null && s.shape.fixed + s.shape.variable === 0) {
      text = 'Empty shape — draw in the Voxel editor';
    }
    el.textContent = text;
    el.classList.toggle('show', text !== '');
  }

  private draw(): void {
    const moving = this.camera.step(performance.now());
    this.renderer.frame(this.camera, this.extent);
    this.renderer.render();
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

  private pickAtClient(clientX: number, clientY: number): Pick | null {
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
  }

  private repick(): void {
    if (this.hoverAt !== null) {
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
      if (d === null) {
        this.hoverAt = { x: e.clientX, y: e.clientY };
        this.showPick(this.pickAtClient(e.clientX, e.clientY));
        return;
      }
      if (!d.moved) {
        if (Math.hypot(e.clientX - d.sx, e.clientY - d.sy) <= 4) {
          return;
        }
        d.moved = true;
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
      if (was !== null && was.btn === 0 && !was.moved) {
        // a click: the cell and side under it (stage 2 edits them)
        this.showPick(this.pickAtClient(e.clientX, e.clientY));
      }
    });
    cv.addEventListener('pointerleave', () => {
      if (this.drag === null) {
        this.hoverAt = null;
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
