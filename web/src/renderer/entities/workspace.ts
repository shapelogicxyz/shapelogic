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
// Entities rows, `updateStatus` and the #garea cursor text. A sliding
// puzzle's start/goal shape (docs/webui-spec.md, section 8.1): its Tray
// edit mode, the starts and goals placed by a click (`sliding.place`,
// `sliding.clearAt`, the outline from `sliding.preview`), the validity bar
// under the view (the original's mainwindow_rods.cpp `refreshSlideValid`)
// and the view's two trays. A stacking puzzle (section 9.1): the disc list
// in the shapes list's place, the Rods card (rods-card.ts) in the voxel
// editor's, no inspector, and the view showing the disc chosen or the rod
// set chosen alone (stacking.rodSetLayout; the original's
// `showSelectedRods`).

import type { CellTool } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { installHistory } from '../shell/history.ts';
import type { GroupState } from '../shell/history.ts';
import { onKey } from '../shell/keyboard.ts';
import { editorVisible, onLayout, tab } from '../shell/layout.ts';
import { hex, isTray, shapeColour } from '../shell/piece-colours.ts';
import { flash, reportError, setCursor } from '../shell/status.ts';
import { onTheme } from '../shell/theme.ts';
import type { View3D } from '../view3d/view.ts';
import type { Plane } from '../view3d/overlays.ts';
import { Edit3D, editHelp } from './edit3d.ts';
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
import { RodsCard } from './rods-card.ts';
import { rodSetText } from '../state/stacking.ts';
import { ShapesList } from './shapes-list.ts';
import { boardCentre } from '../view3d/board.ts';
import { Stroke } from './stroke.ts';
import { marksOf, outlineColour, stampPieces, stampsOf, TrayState, validity } from './tray.ts';

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
  /** The start/goal shape shown and what the engine says it holds. */
  private readonly trays: TrayState;
  /** The cell under the mouse while placing a start or goal. */
  private stampAt: Cell | null = null;
  private stampLoads = 0;
  /** A stacking puzzle's rod sets (the right card). */
  private readonly rods: RodsCard;
  /** Whether the view shows the rod set chosen (true) or the disc chosen. */
  private rodsShown = false;
  private rodLoads = 0;

  constructor(store: Store, view: View3D, edits: Edits) {
    this.store = store;
    this.edits = edits;
    this.view = view;
    this.cells = new ShapeCells(store);
    this.trays = new TrayState(
      store.engine,
      () => {
        this.trayRead();
      },
      (e) => {
        reportError(e, 'The start/goal shape could not be read');
      },
    );
    this.geos = new LayerGeometries(store.engine);
    this.rods = new RodsCard({
      store,
      refuseList: () => this.edits.refuseList(),
      mutate: (doing, call) => this.mutate(doing, call),
      showRodSet: () => {
        this.rodsShown = true;
        this.showRodSet();
      },
    });
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
      showGhost: (arrays) => {
        this.view.setTaskGhost(arrays);
      },
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
      // a sliding puzzle is edited layer by layer in the XY plane: the
      // start/goal editor places a piece's lowest layer on the layer shown
      allPlanes: () => isCube(store.snapshot?.grid) && store.snapshot?.grid !== 'sliding',
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
        void this.mutate('Edit color', () => store.engine.call('colour.edit', { colour: id, rgb })),
      setShapeColour: (shape, rgb) =>
        void this.mutate('Color', () => store.engine.call('shape.setColour', { shape, rgb })),
      deleteColour: (id) => void this.deleteColour(id),
      tray: () => (this.onTray ? this.trays.reply : null),
      trayChanged: () => {
        this.trayModeChanged();
      },
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
      stamping: () => this.card.stamping,
      stampClick: (cell, right) => void this.stampClick(cell, right),
      stampHover: (cell) => {
        this.stampHover(cell);
      },
    });
    this.edit3d = new Edit3D({
      store,
      state: this.state,
      cells: this.cells,
      view,
      refuse,
      canEdit: () => !store.solving && tab === 'entities' && store.snapshot?.grid !== 'stacking',
      drawingHex: () => this.card.drawingHex(),
      colourHex: (c) => this.card.colourHex(c),
      badge: () => this.badge(),
      apply: (tool, cell, colour) => this.apply3d(tool, cell, colour),
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
      this.state.tray = 'walls';
      this.state.stampPiece = null;
      if (!isCube(snap?.grid) || snap?.grid === 'sliding') {
        this.state.plane = 'XY';
      }
      this.geos.clear();
      this.geo = null;
      this.geoKey = '';
      this.edit3d.reset();
      this.cells.show(null, 0);
      this.rodsShown = false;
    }
    // a stacking puzzle: the disc list and the Rods card (section 9.1)
    const stacking = snap?.grid === 'stacking';
    byId('stage').classList.toggle('stk', stacking);
    byId('rightTitle').textContent = stacking ? 'Rods' : 'Voxel editor';
    this.rods.render();
    this.showRodSet();
    if (
      this.state.colour !== null &&
      !(snap?.colours.some((c) => c.id === this.state.colour) ?? false)
    ) {
      this.state.colour = null;
    }
    this.state.dropPaint();
    this.loadTasks();
    this.clampLayer();
    this.chooseStampPiece();
    this.showTray();
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
    this.stampAt = null;
    // a disc chosen: the view shows it again (activateShape)
    if (this.rodsShown) {
      this.rodsShown = false;
      this.showRodSet();
    }
    this.clampLayer();
    this.showTray();
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
      const reply = this.onTray ? this.trays.reply : null;
      return {
        grid: snap.grid,
        geo,
        plane,
        layer: st.layer,
        cells,
        shapeColour: own,
        palette: new Map(snap.colours.map((c) => [c.id, c.rgb])),
        // a tray's tiles; its marks while a start or goal is placed (grideditor.cpp)
        tray: !this.onTray
          ? null
          : {
              marks:
                reply !== null && this.card.stamping
                  ? marksOf(reply, st.tray === 'goal', snap.shapes)
                  : new Map(),
            },
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

  private async apply3d(tool: CellTool, cell: Cell, colour?: string | null): Promise<void> {
    const shape = this.store.selected;
    if (shape === null) {
      return;
    }
    const st = this.state;
    const mirror = axesOf(st.mirror);
    const span = axesOf(st.span);
    const c = colour === undefined ? st.colour : colour;
    await this.mutate('The voxel could not be changed', () =>
      this.store.engine.call('shape.setCells', {
        shape,
        tool,
        cells: [cell],
        ...(tool !== 'erase' && c !== null ? { colour: c } : {}),
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
        help: editHelp(tool, grid),
      });
    }
  }

  private setPlane(p: Plane): void {
    const grid = this.store.snapshot?.grid;
    if (p !== 'XY' && (!isCube(grid) || grid === 'sliding')) {
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
    // unlike the colours there and the shape's own (Default)
    const have = (this.store.snapshot?.colours ?? []).map((c) => c.rgb);
    const s = this.store.shape;
    if (s !== null) {
      have.push(hex(shapeColour(s.colour, this.store.indexOf(s.id))));
    }
    const r = await this.mutate('Add color', () =>
      this.store.engine.call('colour.add', { rgb: nextColour(have) }),
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
    const at = this.store.snapshot?.colours.findIndex((c) => c.id === id) ?? -1;
    const r = await this.mutate('Remove color', () =>
      this.store.engine.call('colour.delete', { colour: id }),
    );
    // the drawing colour moves to the one before (Default before the first)
    if (r !== null) {
      this.state.colour = at > 0 ? (this.store.snapshot?.colours[at - 1]?.id ?? null) : null;
      this.state.changed();
    }
  }

  // ---- a stacking puzzle's rod set in the view ----

  // the rod set chosen drawn alone (stacking.rodSetLayout), or the disc
  private showRodSet(): void {
    const store = this.store;
    const set = this.rods.rodSet;
    const n = ++this.rodLoads;
    if (!this.rodsShown || set === null || store.snapshot?.grid !== 'stacking') {
      this.view.setBoard(null, 'entities');
      return;
    }
    const gen = store.generation;
    const revision = store.snapshot.revision;
    store.engine
      .call('stacking.rodSetLayout', { rodSet: set.id }, { coalesce: 'stacking.rodSetLayout' })
      .then(
        (layout) => {
          if (n !== this.rodLoads || gen !== store.generation) {
            return;
          }
          this.view.setBoard(
            {
              key: `rods:${set.id}:${String(revision)}`,
              layout,
              centre: boardCentre(layout),
              discs: [],
              label: rodSetText(set),
            },
            'entities',
          );
        },
        (e: unknown) => {
          if (n === this.rodLoads && !(e instanceof EngineError && e.errc === 'superseded')) {
            reportError(e, 'The rod set could not be drawn');
          }
        },
      );
  }

  // ---- a sliding puzzle's start/goal shape ----

  /** Whether the shape shown is a sliding start/goal shape. */
  private get onTray(): boolean {
    return this.store.snapshot?.grid === 'sliding' && isTray(this.store.shape);
  }

  // reads the tray shown at this revision (or forgets it)
  private showTray(): void {
    const store = this.store;
    this.trays.show(
      this.onTray ? store.selected : null,
      store.generation,
      store.snapshot?.revision ?? 0,
    );
    this.renderValidity();
  }

  // the piece to place: the one chosen while it is a piece, else the first
  private chooseStampPiece(): void {
    const pieces = stampPieces(this.store.snapshot?.shapes ?? []);
    if (!pieces.some((p) => p.id === this.state.stampPiece)) {
      this.state.stampPiece = pieces[0]?.id ?? null;
    }
  }

  // the engine answered what the tray holds
  private trayRead(): void {
    this.card.renderTray();
    this.showGrid();
    this.renderValidity();
    this.view.setTray(this.trayModel());
    this.refreshStamp();
  }

  // Walls, Start or Goal chosen, or another piece
  private trayModeChanged(): void {
    this.card.render();
    this.showGrid();
    this.grid.refreshHover();
    this.refreshStamp();
  }

  /** What the view's two trays show (null: the shape shown is no tray, or not read yet). */
  private trayModel(): {
    starts: Map<string, [number, number, number][]>;
    goals: Map<string, [number, number, number][]>;
  } | null {
    const r = this.onTray ? this.trays.reply : null;
    return r === null ? null : { starts: stampsOf(r, false), goals: stampsOf(r, true) };
  }

  // the validity bar under the view (refreshSlideValid)
  private renderValidity(): void {
    const el = byId('slideValid');
    const r = this.onTray ? this.trays.reply : null;
    if (r === null) {
      el.classList.remove('show');
      el.textContent = '';
      delete el.dataset.valid;
      return;
    }
    const v = validity(r.error);
    el.textContent = v.text;
    el.classList.add('show');
    el.classList.toggle('bad', !v.valid);
    el.dataset.valid = v.valid ? '1' : '0';
  }

  // the outline of the piece to place under the mouse (sliding.preview)
  private stampHover(cell: Cell | null): void {
    this.stampAt = cell;
    const tray = this.store.selected;
    const piece = this.state.stampPiece;
    const n = ++this.stampLoads;
    if (cell === null || tray === null || piece === null || !this.card.stamping) {
      this.grid.setStamp(null);
      return;
    }
    this.store.engine
      .call(
        'sliding.preview',
        { tray, piece, at: cell, goal: this.state.tray === 'goal' },
        { coalesce: 'sliding.preview' },
      )
      .then(
        (p) => {
          if (n === this.stampLoads) {
            this.grid.setStamp({ cells: p.cells, colour: outlineColour(p) });
            if (p.why !== null) {
              setCursor(p.why);
            }
          }
        },
        (e: unknown) => {
          if (n === this.stampLoads) {
            this.grid.setStamp(null);
            reportError(e, 'The piece could not be shown');
          }
        },
      );
  }

  private refreshStamp(): void {
    this.stampHover(this.stampAt);
  }

  // a click on the tray: the chosen piece placed (or taken off where it
  // is), a right click takes off the piece under the mouse
  private async stampClick(cell: Cell, right: boolean): Promise<void> {
    const tray = this.store.selected;
    const st = this.state;
    if (tray === null || this.refuse()) {
      return;
    }
    const goal = st.tray === 'goal';
    const at = cell;
    const piece = st.stampPiece;
    if (!right && piece === null) {
      flash('Choose a piece in the list first', 4000);
      return;
    }
    const e = this.store.engine;
    this.setBusy(1);
    try {
      const r =
        right || piece === null
          ? await e.call('sliding.clearAt', { tray, at, goal })
          : await e.call('sliding.place', { tray, piece, at, goal });
      await this.store.whenRevision(r.revision);
    } catch (err) {
      // the original put the reason in the status line
      if (err instanceof EngineError && err.errc === 'invalid_argument') {
        flash(err.detail, 5000);
      } else {
        reportError(err, goal ? 'The goal could not be placed' : 'The start could not be placed');
      }
    } finally {
      this.setBusy(-1);
      this.refreshStamp();
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
          // Paint only with a restriction colour chosen (where its brush shows)
          if (
            (t.id === 'paint' && st.colour === null) ||
            this.store.snapshot?.grid === 'stacking'
          ) {
            return true;
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
      // a disc is not edited by its cells
      if (this.store.snapshot?.grid === 'stacking') {
        return true;
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
  if (store.snapshot?.grid === 'stacking') {
    // a disc: its size (the disc list)
    return shape === null
      ? 'No discs — add one with <b>+ New</b>'
      : `Disc <b>${esc(shape.label)}</b>${shape.name === '' ? '' : ` ${esc(shape.name)}`} has size <b>${String(shape.disc ?? 1)}</b>`;
  }
  if (shape === null) {
    return 'No shapes';
  }
  const n = shape.fixed + shape.variable;
  return `Shape <b>${esc(shape.label)}</b> has <b>${n}</b> voxels (${shape.fixed} fixed, ${shape.variable} variable)`;
}
