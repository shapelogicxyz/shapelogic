// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3D editing (E, or a modifier held): a click on a face applies the tool
// to the cell (erase, paint) or to the cell across the face (fixed,
// variable), with mirror and span, as one undo step; the point under the
// mouse previews what a click would do, and the cursor shows the tool.
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
  /** The drawing colour for a cursor's badge (null: Default). */
  badge(): string | null;
  /** Applies a tool to a cell with mirror and span: one undo step. */
  apply(tool: CellTool, cell: Cell): Promise<void>;
  /** Counts work as an edit under way until it settles. */
  track<T>(p: Promise<T>): Promise<T>;
}

interface Target {
  tool: Tool;
  /** The cell the tool goes to, or null when refused. */
  cell: Cell | null;
  /** The face picked (none from the 2D grid). */
  pick: Pick | null;
}

export class Edit3D implements EditHost {
  private readonly host: Edit3DHost;
  private readonly neighbours = new Map<string, Promise<Cell | null>>();
  private token = 0;
  private lastPick: Pick | null = null;
  private lastMods: ViewMods = { ctrl: false, shift: false, alt: false };
  /** The 2D grid's hover, previewed while the mouse is not over the view. */
  private from2d: { cell: Cell; tool: Tool } | null = null;

  constructor(host: Edit3DHost) {
    this.host = host;
  }

  /** Forgets what the grid's neighbours were (another document: another grid). */
  reset(): void {
    this.neighbours.clear();
    this.lastPick = null;
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

  hover(pick: Pick | null, mods: ViewMods): void {
    this.lastPick = pick;
    this.lastMods = mods;
    this.show();
  }

  /** The 2D grid's cell under the mouse and its tool (null: none), previewed in 3D. */
  hover2d(cell: Cell | null, tool: Tool | null): void {
    this.from2d = cell === null || tool === null ? null : { cell, tool };
    this.show();
  }

  click(pick: Pick | null, mods: ViewMods): boolean {
    const host = this.host;
    const act = actFor(mods, host.state.edit3d, host.state.tool);
    if (act === null || tab !== 'entities' || pick === null) {
      return false;
    }
    if (host.refuse()) {
      return true;
    }
    void host.track(
      this.target(act, pick).then((t) => {
        if (t !== null && t.cell !== null) {
          return host.apply(t.tool, t.cell);
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
    });
    const canEdit = act !== null && host.canEdit();
    host.view.setCursor(canEdit && this.lastPick !== null ? cursorFor(act, host.badge()) : '');
    const n = ++this.token;
    if (canEdit && this.lastPick !== null) {
      void this.target(act, this.lastPick).then((t) => {
        if (n === this.token) {
          host.view.setPreview(t === null ? [] : this.ghosts(t));
        }
      });
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
    const cc = host.drawingHex();
    const size = shape.size;
    const voxelOf = (c: Cell): number => c[0] + size[0] * (c[1] + size[1] * c[2]);
    if (t.cell === null) {
      // refused: the face clicked in the danger colour
      return t.pick === null || t.pick.face < 0
        ? t.pick === null
          ? []
          : [{ kind: 'cell', voxel: t.pick.voxel, fill: dng, alpha: 0.25 }]
        : [{ kind: 'face', voxel: t.pick.voxel, face: t.pick.face, fill: dng, alpha: 0.45 }];
    }
    const st = host.state;
    const cube = isCube(grid);
    const out: Ghost[] = [];
    const want = t.tool === 'fixed' ? 'fixed' : 'variable';
    for (const c of expand(t.cell, size, st.mirror, st.span)) {
      const occ = host.cells.at(c);
      if (t.tool === 'fixed' || t.tool === 'variable') {
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
