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
// puzzle's board (docs/webui-spec.md, section 8.1) is drawn as any shape;
// its starts and goals are the Puzzle workspace's (puzzle/slide-card.ts).
// A stacking puzzle (section 9.1): the disc list
// in the shapes list's place, the Rods card (rods-card.ts) in the voxel
// editor's, no inspector, and the view showing the disc chosen or the rod
// set chosen alone (stacking.rodSetLayout; the original's
// `showSelectedRods`).

import type { CellTool, ShapeSummary } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { installHistory } from '../shell/history.ts';
import type { GroupState } from '../shell/history.ts';
import { onKey } from '../shell/keyboard.ts';
import { onPrefs, prefs } from '../shell/prefs.ts';
import { editorVisible, layout, onLayout, tab } from '../shell/layout.ts';
import { chipColor, hex, isTray, shapeColor } from '../shell/piece-colors.ts';
import { onPieceImage, pieceImage } from '../shell/piece-images.ts';
import { pictureToCells } from '../shell/picture-math.ts';
import { flash, reportError, setCursor } from '../shell/status.ts';
import { onTheme } from '../shell/theme.ts';
import type { View3D } from '../view3d/view.ts';
import type { Plane } from '../view3d/overlays.ts';
import { Edit3D, editHelp } from './edit3d.ts';
import { EditorCard, nextColor } from './editor-card.ts';
import { axesOf, EditorState, expand, planeMap, TOOLS } from './editor-state.ts';
import type { Cell, PlaneMap } from './editor-state.ts';
import { Grid2D } from './grid2d.ts';
import { openImageDialog } from './image-dialog.ts';
import type { GridInput } from './grid2d.ts';
import { modsOf } from './tools.ts';
import type { Mods } from './tools.ts';
import { Inspector } from './inspector.ts';
import type { TaskInfo } from './inspector.ts';
import { LayerGeometries } from './layer-geometry.ts';
import type { LayerGeometry } from './layer-geometry.ts';
import { ShapeCells } from './shape-cells.ts';
import type { CellValue } from './shape-cells.ts';
import { RodsCard } from './rods-card.ts';
import { rodSetText } from '../state/stacking.ts';
import { ShapesList } from './shapes-list.ts';
import { TiltCard, tiltKey } from './tilt-card.ts';
import { TrayViewCard } from './tray-view.ts';
import { cellState, isTiltBoard, isTilted } from '../state/tilted.ts';
import { boardCenter } from '../view3d/board.ts';
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
  /** A tilted puzzle's Tilt & tolerance section, and its board's views. */
  private readonly tilt: TiltCard;
  private readonly trayView: TrayViewCard;
  private readonly card: EditorCard;
  private readonly grid: Grid2D;
  private readonly edit3d: Edit3D;
  private tasks: TaskInfo[] | null = null;
  private tasksFor = '';
  private generation = -1;
  private geo: LayerGeometry | null = null;
  /** The drawing of the layer hinted at (the Voxel hint layer setting), or null. */
  private hintGeo: LayerGeometry | null = null;
  private geoKey = '';
  private stroke: Stroke | null = null;
  /** A stroke whose group is closing, until the engine answered its edit.end. */
  private closing: Promise<unknown> | null = null;
  private closingStroke: Stroke | null = null;
  private inspectorKey = '';
  /** Edits under way (a stroke open or closing, a call waiting for its revision): `data-busy`. */
  private readonly edits: Edits;
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
    this.tilt = new TiltCard({
      store,
      refuse,
      mutate: (doing, call) => this.mutate(doing, call),
      openPage: () => {
        window.btApp.openHelp('tilted');
      },
    });
    this.trayView = new TrayViewCard({
      store,
      refuse,
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
      addColor: () => void this.addColor(),
      editColor: (id, rgb) =>
        void this.mutate('Edit color', () => store.engine.call('color.edit', { color: id, rgb })),
      setShapeColor: (shape, rgb) =>
        void this.mutate('Color', () => store.engine.call('shape.setColor', { shape, rgb })),
      editImage: () => {
        this.editImage();
      },
      clearImage: (shape) =>
        void this.mutate('Remove image', () => store.engine.call('shape.clearImage', { shape })),
      deleteColor: (id) => void this.deleteColor(id),
    });
    this.grid = new Grid2D(byId('garea'), {
      state: this.state,
      frozen: () => this.refuse(),
      drawingColor: () => this.badge(),
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
      stamping: () => false,
      stampClick: () => undefined,
      stampHover: () => undefined,
      focused: () => layout.focus === '2d',
    });
    this.edit3d = new Edit3D({
      store,
      state: this.state,
      cells: this.cells,
      view,
      refuse,
      canEdit: () => !store.solving && tab === 'entities' && store.snapshot?.grid !== 'stacking',
      drawingHex: () => this.card.drawingHex(),
      colorHex: (c) => this.card.colorHex(c),
      badge: () => this.badge(),
      apply: (tool, cell, color) => this.apply3d(tool, cell, color),
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
    // a piece's picture decoded: the grid draws it
    onPieceImage(() => {
      this.showGrid();
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
      // a board's chequer follows the theme's board color
      this.showGrid();
    });
    // the Grid plane setting: the grid goes to it at once (where the grid has it)
    onPrefs((changed) => {
      if (changed.includes('gridPlane')) {
        this.setPlane(prefs.gridPlane);
      }
      if (changed.includes('hintLayer')) {
        this.showGrid();
      }
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

  /** The drawing color for a badge: null for Default. */
  private badge(): string | null {
    const id = this.state.color;
    return id === null ? null : (this.store.snapshot?.colors.find((c) => c.id === id)?.rgb ?? null);
  }

  private documentChanged(): void {
    const store = this.store;
    const snap = store.snapshot;
    if (store.generation !== this.generation) {
      // another document: its own grid, colors and shapes
      this.generation = store.generation;
      this.state.box = null;
      this.state.layer = 0;
      this.state.color = null;
      // the Grid plane setting's plane, where the grid has more than XY
      this.state.plane = !isCube(snap?.grid) || snap?.grid === 'sliding' ? 'XY' : prefs.gridPlane;
      this.geos.clear();
      this.geo = null;
      this.hintGeo = null;
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
      this.state.color !== null &&
      !(snap?.colors.some((c) => c.id === this.state.color) ?? false)
    ) {
      this.state.color = null;
    }
    this.state.dropPaint();
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
    // a disc chosen: the view shows it again (activateShape)
    if (this.rodsShown) {
      this.rodsShown = false;
      this.showRodSet();
    }
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
    const k = `${String(this.tasks?.length)}:${this.tasksFor}:${s?.id}:${s?.label}:${s?.color}:${(s?.fixed ?? 0) + (s?.variable ?? 0) === 0}:${this.state.applyAll}:${this.store.indexOf(s?.id ?? '')}:${tiltKey(this.store)}`;
    if (k !== this.inspectorKey) {
      this.inspectorKey = k;
      this.inspector.render();
      this.tilt.render();
    }
    // a tilted puzzle: the board has only Tilt & tolerance; a piece has no
    // Fit & scale (every shape has the board's size)
    const tilted = isTilted(this.store.snapshot);
    const board = tilted && isTiltBoard(s);
    byId('secTf').hidden = board;
    byId('secFit').hidden = tilted;
    byId('secTools').hidden = board;
    byId('secTut').hidden = !tilted;
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

  // the Apply image dialog for the piece or board shown (decision 0038)
  private editImage(): void {
    const shape = this.store.shape;
    if (shape === null || this.refuse()) {
      return;
    }
    const cells = [...this.cells.all.values()].map((v) => v.at);
    if (cells.length === 0) {
      flash(
        `Draw the ${isTray(shape) ? 'board' : 'piece'}’s cells first: the image shows only on them`,
      );
      return;
    }
    const picture = pieceImage(this.store.engine, shape.image);
    openImageDialog({
      label: shape.label,
      board: isTray(shape),
      cells,
      color: hex(chipColor(shape, this.store.indexOf(shape.id))),
      current: shape.image !== null && picture !== null ? { image: shape.image, picture } : null,
      apply: (a) =>
        this.mutate('Image', () =>
          this.store.engine.call('shape.setImage', { shape: shape.id, ...a }),
        ).then((r) => r !== null),
      remove: () =>
        void this.mutate('Remove image', () =>
          this.store.engine.call('shape.clearImage', { shape: shape.id }),
        ),
    });
  }

  // a sliding piece's or board's picture for the grid, once decoded (null: none yet)
  private pictureOf(shape: { image: ShapeSummary['image'] }): GridInput['image'] {
    if (this.store.snapshot?.grid !== 'sliding' || shape.image === null) {
      return null;
    }
    const picture = pieceImage(this.store.engine, shape.image);
    return picture === null
      ? null
      : { picture, toCells: pictureToCells(shape.image), src: shape.image.src };
  }

  // the grid of the layer shown: its drawing and the hinted layer's (asked
  // of the engine once per size and layer) and the engine's cells of them
  private showGrid(): void {
    const store = this.store;
    const snap = store.snapshot;
    const shape = store.shape;
    if (snap === null || shape === null) {
      this.geo = null;
      this.hintGeo = null;
      this.grid.show(null);
      this.trayView.show(null);
      byId('stage').classList.remove('tltb', 'tlt');
      return;
    }
    // a tilted puzzle's board: the tray, seen as the board's view says
    // (tray-view.ts), in place of the voxel grid
    const tiltBoard = isTiltBoard(shape) && snap.tray != null;
    byId('stage').classList.toggle('tltb', tiltBoard);
    byId('stage').classList.toggle('tlt', snap.tray != null);
    if (tiltBoard) {
      this.geo = null;
      this.hintGeo = null;
      this.geoKey = '';
      this.grid.show(null);
      this.trayView.show(snap.tray);
      return;
    }
    this.trayView.show(null);
    const st = this.state;
    const plane = planeMap(st.plane, shape.size);
    const next = st.layer + (prefs.hintLayer === 'Above' ? 1 : -1);
    const hintLayer = next >= 0 && next < plane.layers ? next : null;
    const k = `${store.generation}:${snap.grid}:${shape.size.join('x')}:${st.plane}:${st.layer}:${String(hintLayer)}`;
    const layerOf = (layer: number): Map<string, CellValue> =>
      new Map([...this.cells.all].filter(([, v]) => plane.crl(v.at)[2] === layer));
    const input = (geo: LayerGeometry, hintGeo: LayerGeometry | null): GridInput => {
      const cells =
        st.plane === 'XY' && this.cells.layer === st.layer
          ? this.cells.layerCells
          : layerOf(st.layer);
      const own = hex(shapeColor(shape.color, store.indexOf(shape.id)));
      return {
        grid: snap.grid,
        geo,
        plane,
        layer: st.layer,
        cells,
        shapeColor: own,
        // the constraint colors (a sliding puzzle's start and goal marks
        // are drawn apart from them); a board in its tiles
        palette: new Map(snap.colors.map((c) => [c.id, c.rgb])),
        tray:
          snap.grid === 'sliding' && isTray(shape)
            ? {
                marks: new Map(),
                // a board given a color shows its chequer in it, else in the theme's
                own: shape.color == null ? null : shapeColor(shape.color, store.indexOf(shape.id)),
              }
            : null,
        image: this.pictureOf(shape),
        // a tilted puzzle's piece: the board's whole cells (state/tilted.ts)
        whole:
          snap.tray != null && st.plane === 'XY'
            ? (x: number, y: number) => snap.tray != null && cellState(snap.tray, x, y) === 'w'
            : null,
        hint:
          hintGeo === null || hintLayer === null
            ? null
            : { geo: hintGeo, cells: layerOf(hintLayer) },
      };
    };
    if (k === this.geoKey && this.geo !== null) {
      this.grid.show(input(this.geo, this.hintGeo));
      this.card.renderSelbar();
      return;
    }
    this.geoKey = k;
    Promise.all([
      this.geos.get(snap.grid, shape.size, st.plane, st.layer),
      hintLayer === null ? null : this.geos.get(snap.grid, shape.size, st.plane, hintLayer),
    ]).then(
      ([g, h]) => {
        if (this.geoKey === k) {
          this.geo = g;
          this.hintGeo = h;
          this.grid.show(input(g, h));
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
        color: tool === 'erase' ? null : st.color,
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

  private async apply3d(tool: CellTool, cell: Cell, color?: string | null): Promise<void> {
    const shape = this.store.selected;
    if (shape === null) {
      return;
    }
    const st = this.state;
    const mirror = axesOf(st.mirror);
    const span = axesOf(st.span);
    const c = color === undefined ? st.color : color;
    await this.mutate('The voxel could not be changed', () =>
      this.store.engine.call('shape.setCells', {
        shape,
        tool,
        cells: [cell],
        ...(tool !== 'erase' && c !== null ? { color: c } : {}),
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
      // the cell's coordinates, not its column and row (which run backwards on a
      // flipped axis), counted from 1 as the rulers and the layers are
      const p = planeMap(this.state.plane, s.size);
      const at = (a: 'X' | 'Y' | 'Z'): string => `${a}${String((cell['XYZ'.indexOf(a)] ?? 0) + 1)}`;
      setCursor(`Cursor ${at(p.ca)} ${at(p.ra)} · ${at(p.la)}`);
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
        color: this.badge(),
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

  // Fill puts the chosen voxel tool in the box: under Erase it clears, as Clear does
  private async fillBox(clearing: boolean): Promise<void> {
    const s = this.store.shape;
    const box = this.state.box;
    if (s === null || box === null || this.refuse()) {
      return;
    }
    const st = this.state;
    const tool = clearing ? 'erase' : st.voxel;
    const clear = tool === 'erase';
    const p = planeMap(st.plane, s.size);
    const from = p.xyz(Math.min(box.c0, box.c1), Math.min(box.r0, box.r1), st.layer);
    const to = p.xyz(Math.max(box.c0, box.c1), Math.max(box.r0, box.r1), st.layer);
    const mirror = axesOf(st.mirror);
    const span = axesOf(st.span);
    await this.mutate(clear ? 'Clear' : 'Fill', () =>
      this.store.engine.call('shape.setBox', {
        shape: s.id,
        tool,
        from,
        to,
        ...(!clear && st.color !== null ? { color: st.color } : {}),
        ...(mirror.length === 0 ? {} : { mirror }),
        ...(span.length === 0 ? {} : { span }),
      }),
    );
  }

  private async addColor(): Promise<void> {
    if (this.edits.refuseList()) {
      return;
    }
    // unlike the colors there and the shape's own (Default)
    const have = (this.store.snapshot?.colors ?? []).map((c) => c.rgb);
    const s = this.store.shape;
    if (s !== null) {
      have.push(hex(shapeColor(s.color, this.store.indexOf(s.id))));
    }
    const r = await this.mutate('Add color', () =>
      this.store.engine.call('color.add', { rgb: nextColor(have) }),
    );
    if (r !== null) {
      this.state.color = r.color;
      this.state.changed();
    }
  }

  private async deleteColor(id: string): Promise<void> {
    if (this.edits.refuseList()) {
      return;
    }
    const at = this.store.snapshot?.colors.findIndex((c) => c.id === id) ?? -1;
    const r = await this.mutate('Remove color', () =>
      this.store.engine.call('color.delete', { color: id }),
    );
    // the drawing color moves to the one before (Default before the first)
    if (r !== null) {
      this.state.color = at > 0 ? (this.store.snapshot?.colors[at - 1]?.id ?? null) : null;
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
              center: boardCenter(layout),
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
          // Paint only with a restriction color chosen (where its brush shows)
          if ((t.id === 'paint' && st.color === null) || this.store.snapshot?.grid === 'stacking') {
            return true;
          }
          // a voxel tool keeps the box selection (its Fill uses it); the brush draws
          if (t.id === 'paint') {
            st.tool = 'paint';
            st.boxMode = false;
          } else {
            st.setVoxel(t.id);
          }
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
