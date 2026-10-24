// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3D editing (E, or a modifier held): a click on a face applies the tool
// to the cell (erase, paint) or to the cell across the face (fixed,
// variable), with mirror and span, as one undo step; the point under the
// mouse previews what a click would do, and the cursor shows the tool.
// With fixed or variable, the right half of a face works on its own cell:
// the top right quarter makes it of the tool's state (its colour kept),
// the bottom right one erases it; and a click on no face, on a cube grid,
// adds the cell of the editor's layer under the mouse (an empty shape is
// begun so).
// The cell across a face is the client's table on the cube grid (the
// brick grid's getNeighbor, faces 0 to 5: -x, -y, -z, +x, +y, +z) and
// `grid.neighbour` on the others; a face of a bevel, or a cell across that
// is outside the space, refuses (the original grew the space there:
// resizeInclude in mainwindow_solve.cpp; the mockup refused). From
// docs/mockup/index.html: `actFor`, `updateHover`, the canvas's pointerup
// edit, render3d's live preview (its `ghost` and `gc`, mirror and span
// through `expand`), `renderEditBtn`, `cursorFor` and the keydown/keyup
// handler of the modifiers.

import type { CellTool, Grid, Size } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { cursorFor } from '../shell/cursors.ts';
import type { Tool } from '../shell/cursors.ts';
import { css } from '../shell/dom.ts';
import { contrast, fromHex } from '../shell/piece-colours.ts';
import { tab } from '../shell/layout.ts';
import { FLAG } from '../view3d/mesh.ts';
import type { Pick } from '../view3d/picking.ts';
import type { Ghost } from '../view3d/preview.ts';
import type { EditHost, View3D, ViewMods } from '../view3d/view.ts';
import { expand, key, TOOLS } from './editor-state.ts';
import type { Cell, EditorState } from './editor-state.ts';
import type { ShapeCells } from './shape-cells.ts';
import { actFor, brickNeighbour } from './tools.ts';

/** The sphere grid's faces that are no cap of a neighbour (light, or a variable ball's black). */
const SPHERE_FILL = FLAG.light | FLAG.variableFace;

const isCube = (g: Grid): boolean => g === 'bricks' || g === 'sliding' || g === 'stacking';

/** The view's hint lines for 3D edit mode with a tool (HTML), on a grid. */
export function editHelp(tool: Tool, grid: Grid | undefined): string[] {
  if (tool === 'erase') {
    return ['<b>Edit</b>: click a voxel to erase it', 'Choose Fixed or Variable to add voxels'];
  }
  if (tool === 'paint') {
    return [
      '<b>Edit</b>: click a voxel to paint it the drawing color',
      'Choose Fixed or Variable to add voxels',
    ];
  }
  const name = tool === 'fixed' ? 'fixed' : 'variable';
  return [
    `<b>Edit</b>: click a face's left half to add a <b>${name}</b> voxel beside it`,
    `Its top-right quarter makes the voxel ${name} · bottom-right (red) deletes it`,
    ...(grid !== undefined && isCube(grid)
      ? ['Click empty space to add a voxel on the active layer']
      : []),
  ];
}

/** What 3D editing asks of the workspace. */
export interface Edit3DHost {
  store: Store;
  state: EditorState;
  cells: ShapeCells;
  view: View3D;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Whether a click edits now (no solve; the Entities workspace). */
  canEdit(): boolean;
  /** The drawing colour, #rrggbb (Default: the shape's own). */
  drawingHex(): string;
  /** A constraint colour's #rrggbb (null: the shape's own). */
  colourHex(colour: string | null): string;
  /** The drawing colour for a cursor's badge (null: Default). */
  badge(): string | null;
  /**
   * Applies a tool to a cell with mirror and span: one undo step; with the
   * drawing colour, or `colour` when it is given (null: none).
   */
  apply(tool: CellTool, cell: Cell, colour?: string | null): Promise<void>;
  /** Counts work as an edit under way until it settles. */
  track<T>(p: Promise<T>): Promise<T>;
}

interface Target {
  tool: Tool;
  /** The cell the tool goes to, or null when refused. */
  cell: Cell | null;
  /** The face picked (none from the 2D grid, or a layer's empty cell). */
  pick: Pick | null;
  /** What a quarter of a face does instead of the tool: its cell's state changed, or erased. */
  quarter?: 'state' | 'erase';
  /** The colour put on (absent: the drawing colour; null: none). */
  colour?: string | null;
}

export class Edit3D implements EditHost {
  private readonly host: Edit3DHost;
  private readonly neighbours = new Map<string, Promise<Cell | null>>();
  private token = 0;
  private lastPick: Pick | null = null;
  /** Whether the mouse is over the shape's space (its grid box). */
  private lastOver = false;
  private lastMods: ViewMods = { ctrl: false, shift: false, alt: false };
  /** The editor's layer's cell under the mouse (the cube grids). */
  private lastLayerCell: Cell | null = null;
  /** The 2D grid's hover, previewed while the mouse is not over the view. */
  private from2d: { cell: Cell; tool: Tool } | null = null;

  constructor(host: Edit3DHost) {
    this.host = host;
  }

  /** Forgets what the grid's neighbours were (another document: another grid). */
  reset(): void {
    this.neighbours.clear();
    this.lastPick = null;
    this.lastOver = false;
    this.lastLayerCell = null;
    this.from2d = null;
    this.show();
  }

  toggle(): void {
    if (tab !== 'entities') {
      return;
    }
    this.host.state.edit3d = !this.host.state.edit3d;
    this.host.state.changed();
  }

  hover(pick: Pick | null, mods: ViewMods, over: boolean, layerCell: Cell | null): void {
    this.lastPick = pick;
    this.lastMods = mods;
    this.lastOver = over;
    this.lastLayerCell = layerCell;
    this.show();
  }

  /** The 2D grid's cell under the mouse and its tool (null: none), previewed in 3D. */
  hover2d(cell: Cell | null, tool: Tool | null): void {
    this.from2d = cell === null || tool === null ? null : { cell, tool };
    this.show();
  }

  click(pick: Pick | null, mods: ViewMods, layerCell: Cell | null): boolean {
    const host = this.host;
    const act = actFor(mods, host.state.edit3d, host.state.tool);
    const empty = pick === null ? this.layerTarget(act, layerCell) : null;
    if (act === null || tab !== 'entities' || (pick === null && empty === null)) {
      return false;
    }
    if (host.refuse()) {
      return true;
    }
    void host.track(
      (pick === null ? Promise.resolve(empty) : this.target(act, pick)).then((t) => {
        if (t !== null && t.cell !== null) {
          return host.apply(t.tool, t.cell, t.colour);
        }
        return undefined;
      }),
    );
    return true;
  }

  /** Draws the preview, the cursor and the Edit button for what is under the mouse now. */
  show(): void {
    const host = this.host;
    const st = host.state;
    const m = this.lastMods;
    const tmp = m.ctrl || m.alt || m.shift;
    const act =
      tab === 'entities' && host.store.shape !== null ? actFor(m, st.edit3d, st.tool) : null;
    const shown = act ?? st.tool;
    const info = TOOLS.find((t) => t.id === shown);
    host.view.renderEditButton({
      tool: shown,
      name: info?.n ?? '',
      key: info?.k ?? '',
      colour: host.badge(),
      on: st.edit3d,
      tmp: tmp && act !== null,
      enabled: tab === 'entities' && host.store.shape !== null,
      help: editHelp(shown, host.store.snapshot?.grid),
    });
    const canEdit = act !== null && host.canEdit();
    // the tool's cursor over the shape's space, its empty cells too (the
    // mockup's showed it all over the view): a drag there still navigates
    host.view.setCursor(
      canEdit && (this.lastPick !== null || this.lastOver) ? cursorFor(act, host.badge()) : '',
    );
    const n = ++this.token;
    const empty =
      canEdit && this.lastPick === null ? this.layerTarget(act, this.lastLayerCell) : null;
    if (canEdit && this.lastPick !== null) {
      void this.target(act, this.lastPick).then((t) => {
        if (n === this.token) {
          host.view.setPreview(t === null ? [] : this.ghosts(t));
        }
      });
    } else if (empty !== null) {
      host.view.setPreview(this.ghosts(empty));
    } else if (this.from2d !== null && host.canEdit() && tab === 'entities') {
      host.view.setPreview(
        this.ghosts({ tool: this.from2d.tool, cell: this.from2d.cell, pick: null }),
      );
    } else {
      host.view.setPreview([]);
    }
  }

  // where a tool goes for a picked face
  private async target(tool: Tool, pick: Pick): Promise<Target | null> {
    const store = this.host.store;
    const shape = store.shape;
    const grid = store.snapshot?.grid;
    if (shape === null || grid === undefined) {
      return null;
    }
    if (tool === 'erase' || tool === 'paint') {
      return { tool, cell: pick.cell, pick };
    }
    // the right half of a side works on its own cell: the top quarter
    // gives it the tool's state (nothing when it has it), the bottom erases
    const own = this.host.cells.at(pick.cell);
    if (own !== undefined && (pick.quarter === 'tr' || pick.quarter === 'br')) {
      if (pick.quarter === 'br') {
        return { tool: 'erase', cell: pick.cell, pick, quarter: 'erase' };
      }
      return own.state === tool
        ? null
        : { tool, cell: pick.cell, pick, quarter: 'state', colour: own.colour };
    }
    // a bevel or gap has no side; nor have the faces of a ball of the sphere
    // grid that are not its clickable caps (the original drew them lighter,
    // or black on a variable ball: sphere_mesh.cpp), whose number is the
    // cap's drawn before them
    if (pick.face < 0 || (grid === 'spheres' && (pick.flags & SPHERE_FILL) !== 0)) {
      return { tool, cell: null, pick };
    }
    if (isCube(grid)) {
      return { tool, cell: brickNeighbour(pick.cell, pick.face, shape.size), pick };
    }
    return { tool, cell: await this.neighbour(shape.size, pick.cell, pick.face), pick };
  }

  // where fixed or variable goes for a click on no face: the layer's empty cell
  private layerTarget(act: Tool | null, cell: Cell | null): Target | null {
    if (cell === null || (act !== 'fixed' && act !== 'variable')) {
      return null;
    }
    return this.host.cells.at(cell) === undefined ? { tool: act, cell, pick: null } : null;
  }

  private neighbour(size: Size, cell: Cell, face: number): Promise<Cell | null> {
    const k = `${size.join('x')}:${key(cell)}:${face}`;
    let p = this.neighbours.get(k);
    if (p === undefined) {
      p = this.host.store.engine.call('grid.neighbour', { size, cell, face }).then(
        (r) => (r.cell === null ? null : [r.cell[0], r.cell[1], r.cell[2]]),
        () => {
          // a pick of a mesh just replaced (another size): no target
          this.neighbours.delete(k);
          return null;
        },
      );
      this.neighbours.set(k, p);
    }
    return p;
  }

  // the mockup's ghosts: what a click would change, with mirror and span
  private ghosts(t: Target): Ghost[] {
    const host = this.host;
    const store = host.store;
    const shape = store.shape;
    const grid = store.snapshot?.grid;
    if (shape === null || grid === undefined) {
      return [];
    }
    const acc = css('--accent') || '#2f6df6';
    const dng = css('--danger') || '#e5484d';
    const cc = t.colour === undefined ? host.drawingHex() : host.colourHex(t.colour);
    const size = shape.size;
    const voxelOf = (c: Cell): number => c[0] + size[0] * (c[1] + size[1] * c[2]);
    if (t.cell === null) {
      // refused: the face clicked in the muted colour (the danger colour
      // is an erase's)
      const off = css('--muted') || '#667085';
      return t.pick === null || t.pick.face < 0
        ? t.pick === null
          ? []
          : [{ kind: 'cell', voxel: t.pick.voxel, fill: off, alpha: 0.25 }]
        : [{ kind: 'face', voxel: t.pick.voxel, face: t.pick.face, fill: off, alpha: 0.45 }];
    }
    const st = host.state;
    const cube = isCube(grid);
    const out: Ghost[] = [];
    const want = t.tool === 'fixed' ? 'fixed' : 'variable';
    for (const c of expand(t.cell, size, st.mirror, st.span)) {
      const occ = host.cells.at(c);
      if (t.quarter === 'erase') {
        // the bottom right quarter: the cells to erase, faintly in the danger colour
        if (occ !== undefined) {
          out.push({ kind: 'cell', voxel: voxelOf(c), fill: dng, alpha: 0.25 });
        }
      } else if (t.quarter === 'state') {
        // the top right quarter: the cells that change, as the state would draw them
        if (occ !== undefined && occ.state !== want) {
          out.push(
            cube
              ? {
                  kind: 'box',
                  at: c,
                  fill: cc,
                  edge: acc,
                  alpha: 0.55,
                  dashed: want === 'variable',
                }
              : { kind: 'cell', voxel: voxelOf(c), fill: cc, alpha: 0.4 },
          );
        }
      } else if (t.tool === 'fixed' || t.tool === 'variable') {
        if (occ === undefined) {
          if (cube) {
            out.push({
              kind: 'box',
              at: c,
              fill: cc,
              edge: acc,
              alpha: 0.55,
              dashed: t.tool === 'variable',
            });
          }
        } else if (occ.state !== want || occ.colour !== st.colour) {
          out.push(
            cube
              ? { kind: 'box', at: c, fill: cc, edge: acc, alpha: 0.22, dashed: true }
              : { kind: 'cell', voxel: voxelOf(c), fill: cc, alpha: 0.22 },
          );
        }
      } else if (occ !== undefined) {
        const fill = t.tool === 'erase' ? dng : cc;
        out.push(
          cube
            ? {
                kind: 'box',
                at: c,
                fill,
                edge: t.tool === 'erase' ? dng : acc,
                alpha: 0.5,
                dashed: false,
              }
            : { kind: 'cell', voxel: voxelOf(c), fill, alpha: 0.5 },
        );
      }
    }
    // the other grids: an empty cell's shape is not known here, so the
    // face it grows from shows instead, in the drawing colour's contrast
    // (the face is of a cell of that colour, often)
    if (
      !cube &&
      (t.tool === 'fixed' || t.tool === 'variable') &&
      t.pick !== null &&
      t.pick.face >= 0 &&
      host.cells.at(t.cell) === undefined
    ) {
      out.push({
        kind: 'face',
        voxel: t.pick.voxel,
        face: t.pick.face,
        fill: contrast(fromHex(cc)),
        alpha: 0.5,
        skipFlags: grid === 'spheres' ? SPHERE_FILL : 0,
      });
    }
    return out;
  }
}
