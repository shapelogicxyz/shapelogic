// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace put together: the shapes list and the inspector
// in the left card, the voxel editor in the right, 3D editing in the view,
// the workspace's keys (1 to 4, E, PgUp and PgDn, the list's, F2, Del,
// Ctrl+D; Esc clears the box selection), undo and redo, the status bar's
// cursor, and the frozen state while a solve or an analysis runs
// (Snapshot.solving: the cards are disabled and say "pause the solve
// first", so the engine is never asked for what it would refuse as
// invalid_state). From docs/mockup/index.html: `renderAll`,
// `renderEditor`, `afterEdit`, `selectShape`, the keydown handler's
// Entities rows, `updateStatus` and the #garea cursor text.

import type { CellTool } from '../../engine/api.generated.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { installHistory } from '../shell/history.ts';
import type { GroupState } from '../shell/history.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab } from '../shell/layout.ts';
import { hex, shapeColour } from '../shell/piece-colours.ts';
import { reportError, setCursor } from '../shell/status.ts';
import { onTheme } from '../shell/theme.ts';
import type { View3D } from '../view3d/view.ts';
import type { Plane } from '../view3d/overlays.ts';
import { Edit3D } from './edit3d.ts';
import { EditorCard, nextColour } from './editor-card.ts';
import { axesOf, EditorState, expand, planeMap, TOOLS } from './editor-state.ts';
import type { Cell, PlaneMap } from './editor-state.ts';
import { Grid2D } from './grid2d.ts';
import type { GridInput } from './grid2d.ts';
import { modsOf } from './tools.ts';
import type { Mods } from './tools.ts';
import { Inspector } from './inspector.ts';
import type { TaskInfo } from './inspector.ts';
import { LayerGeometries } from './layer-geometry.ts';
import type { LayerGeometry } from './layer-geometry.ts';
import { ShapeCells } from './shape-cells.ts';
import { ShapesList } from './shapes-list.ts';
import { Stroke } from './stroke.ts';

const isCube = (g: string | undefined): boolean =>
  g === 'bricks' || g === 'sliding' || g === 'stacking';

export class EntitiesWorkspace {
  readonly state = new EditorState();
  readonly cells: ShapeCells;
  private readonly store: Store;
  private readonly view: View3D;
  private readonly geos: LayerGeometries;
  private readonly list: ShapesList;
  private readonly inspector: Inspector;
  private readonly card: EditorCard;
  private readonly grid: Grid2D;
  private readonly edit3d: Edit3D;
  private tasks: TaskInfo[] | null = null;
  private tasksFor = '';
  private generation = -1;
  private geo: LayerGeometry | null = null;
  private geoKey = '';
  private stroke: Stroke | null = null;
  /** A stroke whose group is closing, until the engine answered its edit.end. */
  private closing: Promise<unknown> | null = null;
  private closingStroke: Stroke | null = null;
  private inspectorKey = '';
  /** Edits under way (a stroke open or closing, a call waiting for its revision): `data-busy`. */
  private readonly edits: Edits;

  constructor(store: Store, view: View3D, edits: Edits) {
    this.store = store;
    this.edits = edits;
    this.view = view;
    this.cells = new ShapeCells(store);
    this.geos = new LayerGeometries(store.engine);
    const refuse = (): boolean => this.refuse();
    this.list = new ShapesList({
      store,
      refuse,
      refuseList: () => this.edits.refuseList(),
      mutate: (doing, call) => this.mutate(doing, call),
    });
    this.inspector = new Inspector({
      store,
      state: this.state,
      refuse,
      tasks: () => this.tasks,
      applyAllChanged: () => {
        this.state.changed();
      },
      track: (p) => this.track(p),
    });
    this.card = new EditorCard({
      store,
      state: this.state,
      changed: () => {
        this.state.changed();
      },
      plane: () => this.planeMap(),
      layersWithCells: () => {
        const p = this.state.plane;
        return this.cells.layersWithCells(p === 'XY' ? 2 : p === 'XZ' ? 1 : 0);
      },
      unit: () => this.unit(),
      allPlanes: () => isCube(store.snapshot?.grid),
      setPlane: (p) => {
        this.setPlane(p);
      },
      setLayer: (l) => {
        this.setLayer(l);
      },
      resize: (axis, delta) => void this.resize(axis, delta),
      fillBox: (clear) => void this.fillBox(clear),
      addColour: () => void this.addColour(),
      editColour: (id, rgb) =>
        void this.mutate('Edit colour', () =>
          store.engine.call('colour.edit', { colour: id, rgb }),
        ),
      deleteColour: (id) => void this.deleteColour(id),
    });
    this.grid = new Grid2D(byId('garea'), {
      state: this.state,
      frozen: () => this.refuse(),
      drawingColour: () => this.badge(),
      strokeStart: (tool, cell) => {
        this.strokeStart(tool, cell);
      },
      strokeAdd: (cells) => {
        this.stroke?.add(cells);
      },
      strokeEnd: () => {
        this.strokeEnd();
      },
      strokeHas: (cell) => (this.stroke ?? this.closingStroke)?.has(cell) ?? false,
      expand: (cell) => {
        const s = store.shape;
        return s === null ? [] : expand(cell, s.size, this.state.mirror, this.state.span);
      },
      hover: (cell, tool, mods) => {
        this.hover2d(cell, tool, mods);
      },
      boxChanged: () => {
        this.card.renderSelbar();
      },
    });
    this.edit3d = new Edit3D({
      store,
      state: this.state,
      cells: this.cells,
      view,
      refuse,
      canEdit: () => !store.solving && tab === 'entities',
      drawingHex: () => this.card.drawingHex(),
      badge: () => this.badge(),
      apply: (tool, cell) => this.apply3d(tool, cell),
      track: (p) => this.track(p),
    });
    view.edit = this.edit3d;

    store.subscribe((c) => {
      if (c.kind === 'document') {
        this.documentChanged();
      } else if (c.kind === 'selection') {
        this.selectionChanged();
      } else if (c.kind === 'settings') {
        this.loadTasks();
      }
    });
    this.cells.subscribe(() => {
      this.showGrid();
      this.card.renderLayers();
      this.edit3d.show();
    });
    this.state.subscribe(() => {
      this.stateChanged();
    });
    onLayout(() => {
      this.view.setEditLayer(this.state.plane, this.state.layer);
      this.grid.render();
      this.edit3d.show();
    });
    onTheme(() => {
      this.edit3d.show();
    });
    this.installKeys();
    installHistory(
      store,
      () => this.groupState(),
      () => this.edits.refuseList(),
    );
    this.documentChanged();
  }

  // ---- what the workspace shows ----

  private planeMap(): PlaneMap | null {
    const s = this.store.shape;
    return s === null ? null : planeMap(this.state.plane, s.size);
  }

  private unit(): number {
    const grid = this.store.snapshot?.grid;
    return this.store.info?.grids.find((g) => g.grid === grid)?.sizeUnit ?? 1;
  }

  /** The drawing colour for a badge: null for Default. */
  private badge(): string | null {
    const id = this.state.colour;
    return id === null
      ? null
      : (this.store.snapshot?.colours.find((c) => c.id === id)?.rgb ?? null);
  }

  private documentChanged(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (store.generation !== this.generation) {
      // another document: its own grid, colours and shapes
      this.generation = store.generation;
      this.state.box = null;
      this.state.layer = 0;
      this.state.colour = null;
      if (!isCube(snap?.grid)) {
        this.state.plane = 'XY';
      }
      this.geos.clear();
      this.geo = null;
      this.geoKey = '';
      this.edit3d.reset();
      this.cells.show(null, 0);
    }
    if (
      this.state.colour !== null &&
      !(snap?.colours.some((c) => c.id === this.state.colour) ?? false)
    ) {
      this.state.colour = null;
    }
    this.loadTasks();
    this.clampLayer();
    byId('stage').classList.toggle('frozen', store.solving);
    byId('stage').dataset.frozen = store.solving ? '1' : '0';
    this.list.render();
    this.renderInspector();
    this.card.render();
    this.cells.show(store.selected, this.xyLayer());
    this.showGrid();
    this.edit3d.show();
  }

  private selectionChanged(): void {
    this.state.box = null;
    this.clampLayer();
    this.list.render();
    this.renderInspector();
    this.card.render();
    this.cells.show(this.store.selected, this.xyLayer());
    this.showGrid();
    this.edit3d.show();
  }

  private stateChanged(): void {
    this.clampLayer();
    this.renderInspector();
    this.card.render();
    this.cells.show(this.store.selected, this.xyLayer());
    this.showGrid();
    this.grid.refreshHover();
    this.edit3d.show();
  }

  // the inspector again only when what it shows changed (not every cell drawn)
  private renderInspector(): void {
    const s = this.store.shape;
    const k = `${String(this.tasks?.length)}:${this.tasksFor}:${s?.id}:${s?.label}:${s?.colour}:${(s?.fixed ?? 0) + (s?.variable ?? 0) === 0}:${this.state.applyAll}:${this.store.indexOf(s?.id ?? '')}`;
    if (k !== this.inspectorKey) {
      this.inspectorKey = k;
      this.inspector.render();
    }
  }

  private xyLayer(): number {
    return this.state.plane === 'XY' ? this.state.layer : this.cells.layer;
  }

  private clampLayer(): void {
    const p = this.planeMap();
    if (p !== null && this.state.layer > p.layers - 1) {
      this.state.layer = p.layers - 1;
      this.state.box = null;
    }
    this.view.setEditLayer(this.state.plane, this.state.layer);
  }

  private loadTasks(): void {
    const store = this.store;
    const grid = store.snapshot?.grid;
    const k = `${store.generation}:${grid}`;
    if (grid === undefined || k === this.tasksFor || !store.running) {
      return;
    }
    this.tasksFor = k;
    this.tasks = null;
    store.engine.call('shape.taskSet').then(
      (r) => {
        if (this.tasksFor === k) {
          this.tasks = r.tasks;
          this.inspectorKey = '';
          this.renderInspector();
        }
      },
      (e: unknown) => {
        this.tasksFor = '';
        reportError(e, 'The shape tools could not be read');
      },
    );
  }

  // the grid of the layer shown: its drawing (asked of the engine once per
  // size and layer) and the engine's cells of it
  private showGrid(): void {
    const store = this.store;
    const snap = store.snapshot;
    const shape = store.shape;
    if (snap === null || shape === null) {
      this.geo = null;
      this.grid.show(null);
      return;
    }
    const st = this.state;
    const plane = planeMap(st.plane, shape.size);
    const k = `${store.generation}:${snap.grid}:${shape.size.join('x')}:${st.plane}:${st.layer}`;
    const input = (geo: LayerGeometry): GridInput => {
      const cells =
        st.plane === 'XY' && this.cells.layer === st.layer
          ? this.cells.layerCells
          : new Map([...this.cells.all].filter(([, v]) => plane.crl(v.at)[2] === st.layer));
      const own = hex(shapeColour(shape.colour, store.indexOf(shape.id)));
      return {
        grid: snap.grid,
        geo,
        plane,
        layer: st.layer,
        cells,
        shapeColour: own,
        palette: new Map(snap.colours.map((c) => [c.id, c.rgb])),
      };
    };
    if (k === this.geoKey && this.geo !== null) {
      this.grid.show(input(this.geo));
      this.card.renderSelbar();
      return;
    }
    this.geoKey = k;
    this.geos.get(snap.grid, shape.size, st.plane, st.layer).then(
      (g) => {
        if (this.geoKey === k) {
          this.geo = g;
          this.grid.show(input(g));
          this.card.renderSelbar();
        }
      },
      (e: unknown) => {
        if (this.geoKey === k) {
          this.geoKey = '';
          reportError(e, 'The layer could not be drawn');
        }
      },
    );
  }

  // ---- editing ----

  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean {
    return this.edits.refuse();
  }

  /** Runs a mutating call, waits for its revision; null when it failed (and says why). */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null> {
    return this.edits.mutate(doing, call);
  }

  /** Counts a call as an edit under way until it settles. */
  track<T>(p: Promise<T>): Promise<T> {
    return this.edits.track(p);
  }

  private setBusy(d: number): void {
    this.edits.add(d);
  }

  private groupState(): GroupState {
    if (this.stroke !== null) {
      return 'drawing';
    }
    return this.closing ?? 'none';
  }

  private strokeStart(tool: CellTool, cell: Cell): void {
    const shape = this.store.selected;
    if (shape === null || this.refuse()) {
      return;
    }
    this.strokeEnd();
    const st = this.state;
    this.setBusy(1);
    this.stroke = new Stroke(
      this.store.engine,
      {
        shape,
        tool,
        colour: tool === 'erase' ? null : st.colour,
        mirror: axesOf(st.mirror),
        span: axesOf(st.span),
      },
      (e) => {
        reportError(e, 'The voxels could not be changed');
      },
      (f) => requestAnimationFrame(f),
    );
    this.stroke.add([cell]);
  }

  private strokeEnd(): void {
    const s = this.stroke;
    if (s === null) {
      return;
    }
    this.stroke = null;
    this.closingStroke = s;
    const done = s.end().then(async (revision) => {
      if (revision !== null) {
        await this.store.whenRevision(revision);
      }
      if (this.closing === done) {
        this.closing = null;
        this.closingStroke = null;
        this.grid.render();
      }
      this.setBusy(-1);
    });
    this.closing = done;
  }

  private async apply3d(tool: CellTool, cell: Cell): Promise<void> {
    const shape = this.store.selected;
    if (shape === null) {
      return;
    }
    const st = this.state;
    const mirror = axesOf(st.mirror);
    const span = axesOf(st.span);
    await this.mutate('The voxel could not be changed', () =>
      this.store.engine.call('shape.setCells', {
        shape,
        tool,
        cells: [cell],
        ...(tool !== 'erase' && st.colour !== null ? { colour: st.colour } : {}),
        ...(mirror.length === 0 ? {} : { mirror }),
        ...(span.length === 0 ? {} : { span }),
      }),
    );
  }

  private hover2d(cell: Cell | null, tool: CellTool | null, mods: Mods): void {
    const s = this.store.shape;
    const grid = this.store.snapshot?.grid;
    if (cell === null || s === null) {
      setCursor('');
    } else if (isCube(grid)) {
      const p = planeMap(this.state.plane, s.size);
      const [c, r, l] = p.crl(cell);
      setCursor(`Cursor ${p.ca}${c} ${p.ra}${r} · ${p.la}${l}`);
    } else {
      setCursor(`Cell ${cell.join(', ')}`);
    }
    this.edit3d.hover2d(cell, tool);
    const tmp = mods.ctrl || mods.alt || mods.shift;
    if (tmp && tool !== null) {
      const t = TOOLS.find((x) => x.id === tool);
      this.view.renderEditButton({
        tool,
        name: t?.n ?? '',
        key: t?.k ?? '',
        colour: this.badge(),
        on: this.state.edit3d,
        tmp: true,
        enabled: true,
      });
    }
  }

  private setPlane(p: Plane): void {
    if (p !== 'XY' && !isCube(this.store.snapshot?.grid)) {
      return;
    }
    this.state.plane = p;
    this.state.box = null;
    this.state.changed();
  }

  private setLayer(l: number): void {
    const p = this.planeMap();
    if (p === null) {
      return;
    }
    const layer = Math.max(0, Math.min(p.layers - 1, l));
    if (layer !== this.state.layer) {
      this.state.layer = layer;
      this.state.box = null;
      this.state.changed();
    }
  }

  private async resize(axis: number, delta: number): Promise<void> {
    const s = this.store.shape;
    if (s === null || this.refuse()) {
      return;
    }
    const u = this.unit();
    const size = s.size.map((v) => Math.ceil(v / u)) as [number, number, number];
    size[axis] = Math.max(1, (size[axis] ?? 1) + delta);
    const toAll = this.state.applyAll;
    await this.mutate('Resize', () =>
      this.store.engine.call('shape.resize', { shape: s.id, size, ...(toAll ? { toAll } : {}) }),
    );
    this.state.box = null;
    this.state.changed();
  }

  private async fillBox(clear: boolean): Promise<void> {
    const s = this.store.shape;
    const box = this.state.box;
    if (s === null || box === null || this.refuse()) {
      return;
    }
    const st = this.state;
    const p = planeMap(st.plane, s.size);
    const from = p.xyz(Math.min(box.c0, box.c1), Math.min(box.r0, box.r1), st.layer);
    const to = p.xyz(Math.max(box.c0, box.c1), Math.max(box.r0, box.r1), st.layer);
    const mirror = axesOf(st.mirror);
    const span = axesOf(st.span);
    await this.mutate(clear ? 'Clear' : 'Fill', () =>
      this.store.engine.call('shape.setBox', {
        shape: s.id,
        tool: clear ? 'erase' : 'fixed',
        from,
        to,
        ...(!clear && st.colour !== null ? { colour: st.colour } : {}),
        ...(mirror.length === 0 ? {} : { mirror }),
        ...(span.length === 0 ? {} : { span }),
      }),
    );
  }

  private async addColour(): Promise<void> {
    if (this.edits.refuseList()) {
      return;
    }
    const n = this.store.snapshot?.colours.length ?? 0;
    const r = await this.mutate('Add colour', () =>
      this.store.engine.call('colour.add', { rgb: nextColour(n) }),
    );
    if (r !== null) {
      this.state.colour = r.colour;
      this.state.changed();
    }
  }

  private async deleteColour(id: string): Promise<void> {
    if (this.edits.refuseList()) {
      return;
    }
    const r = await this.mutate('Remove colour', () =>
      this.store.engine.call('colour.delete', { colour: id }),
    );
    if (r !== null) {
      this.state.colour = null;
      this.state.changed();
    }
  }

  // ---- keys ----

  private installKeys(): void {
    const st = this.state;
    const inEntities = (): boolean => tab === 'entities';
    TOOLS.forEach((t, i) => {
      const action = (['tool.fixed', 'tool.variable', 'tool.erase', 'tool.paint'] as const)[i];
      if (action !== undefined) {
        onKey(action, () => {
          if (!inEntities()) {
            return false;
          }
          st.tool = t.id;
          st.boxMode = false;
          st.changed();
          return true;
        });
      }
    });
    onKey('edit3d', () => {
      if (!inEntities()) {
        return false;
      }
      this.edit3d.toggle();
      return true;
    });
    onKey('page.up', () => {
      if (!inEntities() || !editorVisible()) {
        return false;
      }
      this.setLayer(st.layer + 1);
      return true;
    });
    onKey('page.down', () => {
      if (!inEntities() || !editorVisible()) {
        return false;
      }
      this.setLayer(st.layer - 1);
      return true;
    });
    addEscapeStep(ESC_ORDER.selection, () => {
      if (st.box === null) {
        return false;
      }
      st.box = null;
      st.changed();
      return true;
    });
    // the modifiers held change the quick tool under the mouse
    const mods = (e: KeyboardEvent): void => {
      if (e.key !== 'Control' && e.key !== 'Shift' && e.key !== 'Alt' && e.key !== 'Meta') {
        return;
      }
      const m = modsOf(e);
      this.grid.modsChanged(m);
      this.view.modsChanged(m);
    };
    document.addEventListener('keydown', mods);
    document.addEventListener('keyup', mods);
  }
}

/** The Entities workspace's status line (the mockup's updateStatus), HTML. */
export function entitiesStatus(store: Store): string {
  if (store.solving) {
    return 'Shapes are frozen while a solve runs — <b>pause the solve first</b> to edit them';
  }
  const shape = store.shape;
  if (shape === null) {
    return 'No shapes';
  }
  const n = shape.fixed + shape.variable;
  return `Shape <b>${esc(shape.label)}</b> has <b>${n}</b> voxels (${shape.fixed} fixed, ${shape.variable} variable)`;
}
