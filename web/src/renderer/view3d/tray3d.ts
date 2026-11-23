// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle's tray in the 3D view (decision 0045, stage D):
// its outline, and each wall square a dark block one layer high, straight
// in the tray's frame (so turned against the grid). Drawn in the frame of
// the board's grid, the frame every shape of the puzzle has (the tray's
// corners and wall squares as the engine sends them, Snapshot.tray), and
// moved by an offset into the frame of the scene that holds it: the shape
// view's model, or the Solver's scene, centered on the result. The view's
// turn (ViewRenderer.turn) then stands the tray straight. Built here, not
// by the engine: the shapes are four-sided prisms and lines, from numbers
// the engine has already given. The geometry is DOM-free, for the tests.
//
// The board itself (its whole cells) is drawn here too, not from its mesh:
// its cells are variable cells, whose black marks would hide the board and
// flicker as the view turns. Each is a block in the colors of the board's
// Cells view (styles.css .tp-whole.a, .b), in a checker as there.

import type { Tray } from '../../engine/api.generated.ts';
import {
  BufferAttribute,
  BufferGeometry,
  Color,
  Group,
  Mesh,
  MeshBasicMaterial,
  Vector2,
} from 'three';
import type { Material } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

type P2 = readonly [number, number];
type V3 = readonly [number, number, number];

/** The tray as the 3D view draws it, in the board's grid. */
export interface Tray3D {
  /** The tray's four corners. */
  corners: readonly P2[];
  /** Each wall square's four corners. */
  walls: readonly (readonly P2[])[];
}

/**
 * The triangles of four-sided prisms from z0 to z1 (x,y,z per corner, three
 * corners a triangle): the top, the bottom and the four sides of each.
 */
export function prismTriangles(
  quads: readonly (readonly P2[])[],
  z0: number,
  z1: number,
  at: V3 = [0, 0, 0],
): Float32Array {
  const out: number[] = [];
  const v = (p: P2, z: number): void => {
    out.push(p[0] + at[0], p[1] + at[1], z + at[2]);
  };
  for (const q of quads) {
    const [a, b, c, d] = q;
    if (a === undefined || b === undefined || c === undefined || d === undefined) {
      continue;
    }
    for (const z of [z0, z1]) {
      v(a, z);
      v(b, z);
      v(c, z);
      v(a, z);
      v(c, z);
      v(d, z);
    }
    const ring = [a, b, c, d];
    for (let i = 0; i < 4; i++) {
      const p = ring[i] ?? a;
      const n = ring[(i + 1) % 4] ?? a;
      v(p, z0);
      v(n, z0);
      v(n, z1);
      v(p, z0);
      v(n, z1);
      v(p, z1);
    }
  }
  return new Float32Array(out);
}

/** The edges of four-sided prisms from z0 to z1 as line segments (x,y,z pairs): both rings and the four posts. */
export function prismEdges(
  quads: readonly (readonly P2[])[],
  z0: number,
  z1: number,
  at: V3 = [0, 0, 0],
): number[] {
  const out: number[] = [];
  const v = (p: P2, z: number): void => {
    out.push(p[0] + at[0], p[1] + at[1], z + at[2]);
  };
  for (const q of quads) {
    if (q.length !== 4) {
      continue;
    }
    for (let i = 0; i < 4; i++) {
      const p = q[i] ?? [0, 0];
      const n = q[(i + 1) % 4] ?? [0, 0];
      for (const z of [z0, z1]) {
        v(p, z);
        v(n, z);
      }
      v(p, z0);
      v(p, z1);
    }
  }
  return out;
}

/** The whole-cell colors of the board's Cells view (styles.css .tp-whole.a, .b). */
export const CELL_RGB = [0x1919f3, 0x62e03e] as const;

/** The board's cells: the box's size and each cell's state, row by row from the bottom (Snapshot.tray). */
export type TrayCells = Pick<Tray, 'box' | 'cells'>;

/** The board's whole cells as unit squares, each with its place in the checker (0 or 1). */
export function wholeSquares(t: TrayCells): { quad: P2[]; tone: 0 | 1 }[] {
  const [w, h] = t.box;
  const out: { quad: P2[]; tone: 0 | 1 }[] = [];
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      if (t.cells[y * w + x] === 'w') {
        out.push({
          quad: [
            [x, y],
            [x + 1, y],
            [x + 1, y + 1],
            [x, y + 1],
          ],
          tone: (x + y) % 2 === 0 ? 0 : 1,
        });
      }
    }
  }
  return out;
}

// the shade of each face of a block, in prismTriangles' order: the bottom,
// the top, then the sides facing -y, +x, +y, -x (a light from above, a
// little to the right and behind, so neighboring sides differ)
const SHADES = [0.5, 1, 0.72, 0.86, 0.92, 0.64] as const;

/** The board's whole cells as blocks from z0 to z1: positions and colors (r,g,b per corner). */
export function checkerBlocks(
  t: TrayCells,
  z0: number,
  z1: number,
): { positions: Float32Array; colors: Float32Array } {
  const squares = wholeSquares(t);
  const positions = prismTriangles(
    squares.map((s) => s.quad),
    z0,
    z1,
  );
  const colors = new Float32Array(positions.length);
  const base = CELL_RGB.map((hex) => new Color(hex));
  const c = new Color();
  let i = 0;
  for (const s of squares) {
    for (const shade of SHADES) {
      c.copy(base[s.tone] ?? c).multiplyScalar(shade);
      for (let k = 0; k < 6; k++) {
        colors[i++] = c.r;
        colors[i++] = c.g;
        colors[i++] = c.b;
      }
    }
  }
  return { positions, colors };
}

/**
 * The board's whole cells as geometry, built again only when its cells
 * change; any number of objects may draw it.
 */
export class CheckerCells {
  /** The blocks' material: their own colors, a little behind their edges. */
  readonly material: Material = new MeshBasicMaterial({
    vertexColors: true,
    polygonOffset: true,
    polygonOffsetFactor: 2,
    polygonOffsetUnits: 4,
  });
  private key = '';
  private built: { faces: BufferGeometry; edges: LineSegmentsGeometry } | null = null;

  /** The blocks (one layer, z from 0 to 1) and their edges for a board's cells. */
  geometry(t: TrayCells): { faces: BufferGeometry; edges: LineSegmentsGeometry } {
    const key = `${t.box.join(',')}:${t.cells}`;
    if (this.built !== null && key === this.key) {
      return this.built;
    }
    this.dispose();
    const b = checkerBlocks(t, 0, 1);
    const faces = new BufferGeometry();
    faces.setAttribute('position', new BufferAttribute(b.positions, 3));
    faces.setAttribute('color', new BufferAttribute(b.colors, 3));
    faces.computeBoundingBox();
    faces.computeBoundingSphere();
    const edges = new LineSegmentsGeometry().setPositions(
      prismEdges(
        wholeSquares(t).map((s) => s.quad),
        0,
        1,
      ),
    );
    this.key = key;
    this.built = { faces, edges };
    return this.built;
  }

  /** Frees the geometry. */
  dispose(): void {
    this.built?.faces.dispose();
    this.built?.edges.dispose();
    this.built = null;
    this.key = '';
  }
}

/** The tray in a scene: its walls as blocks, its outline as lines, and the board's cells when shown. */
export class TrayScene {
  readonly group = new Group();
  // pushed back in depth: where the allowance lets a cell reach into a
  // wall, their tops lie in one plane, and the cell is drawn
  private readonly wallMaterial = new MeshBasicMaterial({
    color: 0x2b2f37,
    polygonOffset: true,
    polygonOffsetFactor: 4,
    polygonOffsetUnits: 8,
  });
  private readonly checker = new CheckerCells();
  private readonly cellEdgeMaterial = new LineMaterial({ color: 0x000000, linewidth: 2 });
  private readonly edgeMaterial = new LineMaterial({ color: 0x8a93a3, linewidth: 1.5 });
  private readonly rimMaterial = new LineMaterial({ color: 0x000000, linewidth: 2.5 });
  private shownFor = '';
  private wallCount = 0;
  private cellCount = 0;

  constructor() {
    this.edgeMaterial.resolution = new Vector2(1, 1);
    this.rimMaterial.resolution = new Vector2(1, 1);
    this.cellEdgeMaterial.resolution = new Vector2(1, 1);
  }

  /** The viewport's size in pixels, for the lines' widths. */
  setResolution(w: number, h: number): void {
    this.edgeMaterial.resolution.set(w, h);
    this.rimMaterial.resolution.set(w, h);
    this.cellEdgeMaterial.resolution.set(w, h);
  }

  /** The wall blocks' color and their edges' (the theme's --twall, --twallEdge), #rrggbb. */
  setWallColor(wall: string, edge: string): void {
    this.wallMaterial.color = new Color(wall);
    this.edgeMaterial.color = new Color(edge);
  }

  /** The outline's color (the theme's text color), #rrggbb. */
  setRimColor(rgb: string): void {
    this.rimMaterial.color = new Color(rgb);
  }

  /** How many wall blocks are drawn (the tests read it). */
  get walls(): number {
    return this.wallCount;
  }

  /** How many of the board's cells are drawn (the tests read it). */
  get cells(): number {
    return this.cellCount;
  }

  /**
   * Shows a tray (null: none), its blocks `height` high from z = 0, moved
   * by `at` into the scene's frame; with `board`, the board's whole cells
   * in it (one layer, in the board's grid: `at` is not applied).
   */
  set(
    tray: Tray3D | null,
    height: number,
    at: V3 = [0, 0, 0],
    board: TrayCells | null = null,
  ): void {
    const key =
      tray === null
        ? ''
        : JSON.stringify([tray, height, at, board === null ? null : [board.box, board.cells]]);
    if (key === this.shownFor) {
      return;
    }
    this.shownFor = key;
    this.clear();
    if (tray === null) {
      return;
    }
    if (tray.walls.length > 0) {
      const g = new BufferGeometry();
      g.setAttribute('position', new BufferAttribute(prismTriangles(tray.walls, 0, height, at), 3));
      const blocks = new Mesh(g, this.wallMaterial);
      blocks.userData.trayWalls = true;
      this.group.add(blocks);
      const edges = new LineSegments2(
        new LineSegmentsGeometry().setPositions(prismEdges(tray.walls, 0, height, at)),
        this.edgeMaterial,
      );
      this.group.add(edges);
    }
    const rim = new LineSegments2(
      new LineSegmentsGeometry().setPositions(prismEdges([tray.corners], 0, height, at)),
      this.rimMaterial,
    );
    rim.renderOrder = 1;
    this.group.add(rim);
    this.wallCount = tray.walls.length;
    if (board !== null) {
      const g = this.checker.geometry(board);
      const blocks = new Mesh(g.faces, this.checker.material);
      blocks.userData.trayCells = true;
      this.group.add(blocks);
      const edges = new LineSegments2(g.edges, this.cellEdgeMaterial);
      edges.userData.trayCells = true;
      this.group.add(edges);
      this.cellCount = wholeSquares(board).length;
    }
  }

  private clear(): void {
    for (const o of [...this.group.children]) {
      this.group.remove(o);
      // the board's cells are the checker's, kept while they stay the same
      if (o.userData.trayCells === true) {
        continue;
      }
      if (o instanceof Mesh) {
        (o.geometry as BufferGeometry).dispose();
      } else if (o instanceof LineSegments2) {
        o.geometry.dispose();
      }
    }
    this.wallCount = 0;
    this.cellCount = 0;
  }
}
