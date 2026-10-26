// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the voxel editor holds of its own (the engine holds the shapes):
// the tool, the box mode, the plane and layer, the drawing colour, mirror
// and span, "Apply to all shapes", the box selection and the 3D edit mode.
// From docs/mockup/index.html: `st` (tool, plane, layer, color, mirror,
// span, selCells, applyAll, edit3d) and `TOOLS`, `P()` (a plane's columns,
// rows and layers in x, y, z).

import type { CellTool as Tool, Size } from '../../engine/api.generated.ts';
import type { Plane } from '../view3d/overlays.ts';

/** An axis of mirror and span. */
export type Axis = 'x' | 'y' | 'z';

/** A cell of the grid's own coordinates. */
export type Cell = [number, number, number];

/** The tools of the strip (the mockup's TOOLS). */
export const TOOLS: readonly { id: Tool; n: string; short: string; k: string; tip: string }[] = [
  { id: 'fixed', n: 'Fixed voxel', short: 'Fixed', k: '1', tip: 'Place a fixed (solid) voxel' },
  {
    id: 'variable',
    n: 'Variable voxel',
    short: 'Variable',
    k: '2',
    tip: 'Place a variable (optional) voxel',
  },
  { id: 'erase', n: 'Erase voxel', short: 'Erase', k: '3', tip: 'Erase the voxel' },
  {
    id: 'paint',
    n: 'Paint color',
    short: 'Paint',
    k: '4',
    tip: 'Apply the drawing color to an existing voxel',
  },
];

/**
 * What a click on a sliding puzzle's start/goal shape does (the "Tray
 * edit" switch): the ordinary tools (Walls: fixed floor, variable
 * corridors, erased walls), or a piece's start or goal placed whole (the
 * original's Start/Goal tab).
 */
export type TrayMode = 'walls' | 'start' | 'goal';

/** A box selection: two corners of the layer's columns and rows. */
export interface Box {
  c0: number;
  r0: number;
  c1: number;
  r1: number;
}

/** The voxel tools: one of them is always chosen. */
export type VoxelTool = 'fixed' | 'variable' | 'erase';

/** The editor's state; the views render it and say when they change it. */
export class EditorState {
  /** The tool a press uses: the voxel tool, or the restriction colour's brush ('paint'). */
  tool: Tool = 'fixed';
  /** The voxel tool chosen: lit while the brush or the box selection is on, and what Fill puts in a box. */
  voxel: VoxelTool = 'fixed';
  /**
   * A drag in the 2D grid selects a box instead of drawing (the Select/Paint
   * toggle); the voxel tool stays chosen through it.
   */
  boxMode = false;
  plane: Plane = 'XY';
  layer = 0;
  /** The drawing colour: a constraint colour's id, or null for Default (the shape's own). */
  colour: string | null = null;
  readonly mirror = new Set<Axis>();
  readonly span = new Set<Axis>();
  /** "Apply to all shapes": resizes and the tasks that allow it go to every shape. */
  applyAll = false;
  /** The box selected in the current layer, or null. */
  box: Box | null = null;
  /** 3D edit mode: a plain click in the 3D view uses the tool. */
  edit3d = false;
  /** A start/goal shape's edit mode (only while one is shown). */
  tray: TrayMode = 'walls';
  /** The piece whose start or goal a click places (the Start/Goal tab's list), or null. */
  stampPiece: string | null = null;
  private readonly listeners = new Set<() => void>();

  /** Listens to changes; returns the unsubscribe. */
  subscribe(l: () => void): () => void {
    this.listeners.add(l);
    return () => {
      this.listeners.delete(l);
    };
  }

  /** Tells the views the state changed. */
  changed(): void {
    this.dropPaint();
    // a box lives in the box mode: back to drawing clears it
    if (!this.boxMode) {
      this.box = null;
    }
    for (const l of [...this.listeners]) {
      l();
    }
  }

  /** Chooses a voxel tool; the brush is put down. */
  setVoxel(v: VoxelTool): void {
    this.voxel = v;
    this.tool = v;
  }

  /** Paint goes with a restriction colour (its brush shows by one): back to the voxel tool under Default. */
  dropPaint(): void {
    if (this.tool === 'paint' && this.colour === null) {
      this.tool = this.voxel;
    }
  }
}

/**
 * A plane of a space (the mockup's P()): its columns, rows and layers, the
 * axes they run along, and the cell of a column, row and layer. Each plane
 * is seen from the positive side of its layer axis, as the 3D view shows it
 * from there: XY's columns run along +X and its rows up +Y; XZ's columns
 * along +X and its rows down +Z (`rowsFlipped`), so X 0, Z 0 is the top
 * left; YZ's columns along -Z (`flipped`) and its rows up +Y.
 */
export interface PlaneMap {
  cols: number;
  rows: number;
  layers: number;
  ca: 'X' | 'Y' | 'Z';
  ra: 'X' | 'Y' | 'Z';
  la: 'X' | 'Y' | 'Z';
  /** The columns run from the axis's highest coordinate down (column 0 is the last). */
  flipped: boolean;
  /** The rows run from the axis's highest coordinate down (row 0, the bottom one, is the last). */
  rowsFlipped: boolean;
  xyz(c: number, r: number, l: number): Cell;
  /** The column, row and layer of a cell. */
  crl(cell: readonly number[]): [number, number, number];
}

/** The plane map of a size. */
export function planeMap(plane: Plane, d: Size): PlaneMap {
  const [x, y, z] = d;
  if (plane === 'XY') {
    return {
      cols: x,
      rows: y,
      layers: z,
      ca: 'X',
      ra: 'Y',
      la: 'Z',
      flipped: false,
      rowsFlipped: false,
      xyz: (c, r, l) => [c, r, l],
      crl: (p) => [p[0] ?? 0, p[1] ?? 0, p[2] ?? 0],
    };
  }
  if (plane === 'XZ') {
    return {
      cols: x,
      rows: z,
      layers: y,
      ca: 'X',
      ra: 'Z',
      la: 'Y',
      flipped: false,
      rowsFlipped: true,
      xyz: (c, r, l) => [c, l, z - 1 - r],
      crl: (p) => [p[0] ?? 0, z - 1 - (p[2] ?? 0), p[1] ?? 0],
    };
  }
  return {
    cols: z,
    rows: y,
    layers: x,
    ca: 'Z',
    ra: 'Y',
    la: 'X',
    flipped: true,
    rowsFlipped: false,
    xyz: (c, r, l) => [l, r, z - 1 - c],
    crl: (p) => [z - 1 - (p[2] ?? 0), p[1] ?? 0, p[0] ?? 0],
  };
}

/** A cell as a map key. */
export function key(c: readonly number[]): string {
  return `${c[0] ?? 0},${c[1] ?? 0},${c[2] ?? 0}`;
}

/**
 * The cells a tool touches for one cell with mirror and span, as the core
 * does it (grideditor.cpp's setRecursive, core app/shape_tools.cpp's
 * applyCellTool: each mirror both cells, then the spans along their rows),
 * for the preview only: the engine applies the tool. Cells outside the
 * space are left out; the caller leaves out those the grid does not have.
 */
export function expand(
  cell: Cell,
  size: Size,
  mirror: ReadonlySet<Axis>,
  span: ReadonlySet<Axis>,
): Cell[] {
  const d: readonly number[] = size;
  const out = new Map<string, Cell>();
  const add = (c: Cell): void => {
    if (c.every((v, i) => v >= 0 && v < (d[i] ?? 1))) {
      out.set(key(c), c);
    }
  };
  const rec = (c: Cell, axes: Axis[]): void => {
    const [a, ...rest] = axes;
    if (a !== undefined) {
      const i = a === 'x' ? 0 : a === 'y' ? 1 : 2;
      const m: Cell = [c[0], c[1], c[2]];
      m[i] = (d[i] ?? 1) - c[i] - 1;
      rec(c, rest);
      rec(m, rest);
      return;
    }
    if (span.size === 0) {
      add(c);
      return;
    }
    for (const [s, i] of [
      ['x', 0],
      ['y', 1],
      ['z', 2],
    ] as const) {
      if (span.has(s)) {
        for (let t = 0; t < (d[i] ?? 1); t++) {
          const q: Cell = [c[0], c[1], c[2]];
          q[i] = t;
          add(q);
        }
      }
    }
  };
  rec(
    cell,
    (['x', 'y', 'z'] as const).filter((a) => mirror.has(a)),
  );
  return [...out.values()];
}

/** The engine's axes of a set, in order. */
export function axesOf(set: ReadonlySet<Axis>): Axis[] {
  return (['x', 'y', 'z'] as const).filter((a) => set.has(a));
}
