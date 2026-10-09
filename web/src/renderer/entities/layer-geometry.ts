// SPDX-License-Identifier: GPL-3.0-or-later
//
// The drawing of one layer of a space in the 2D editor: each cell's
// outline (a circle on the sphere grid) and the cell across each edge. The
// XY plane of every grid is the engine's (`grid.geometry`, the original's
// grideditor_0 to _4), kept by grid, size and layer; the cube grid's XZ and
// YZ planes are the client's squares, numbered the same way (the API leaves
// them to the client: docs/API.md, shape.get). Replaces the mockup's mock
// geometry (docs/mockup/index.html: `getSpace`).

import type { Grid, Size } from '../../engine/api.generated.ts';
import type { EngineApi } from '../../engine/client-types.ts';
import { key, planeMap } from './editor-state.ts';
import type { Cell } from './editor-state.ts';
import type { Plane } from '../view3d/overlays.ts';

type P2 = [number, number];

/** A cell of a layer drawing. */
export interface LayerCell {
  at: Cell;
  /** Counter-clockwise, y up; absent on the sphere grid. */
  polygon?: P2[];
  /** Per edge (corner i to i + 1): the index of the cell across it, -1 for none. */
  neighbours?: number[];
  circle?: { centre: P2; radius: number };
}

/** A layer drawing: its size in drawing units, its cells and their index by cell. */
export interface LayerGeometry {
  width: number;
  height: number;
  cells: LayerCell[];
  index: Map<string, number>;
}

function indexed(width: number, height: number, cells: LayerCell[]): LayerGeometry {
  return { width, height, cells, index: new Map(cells.map((c, i) => [key(c.at), i])) };
}

/** A plane of the cube grid as squares (the XZ and YZ planes; any plane in the tests). */
export function squareLayer(plane: Plane, size: Size, layer: number): LayerGeometry {
  const p = planeMap(plane, size);
  const cells: LayerCell[] = [];
  const at = (c: number, r: number): number =>
    c < 0 || r < 0 || c >= p.cols || r >= p.rows ? -1 : r * p.cols + c;
  for (let r = 0; r < p.rows; r++) {
    for (let c = 0; c < p.cols; c++) {
      cells.push({
        at: p.xyz(c, r, layer),
        polygon: [
          [c, r],
          [c + 1, r],
          [c + 1, r + 1],
          [c, r + 1],
        ],
        neighbours: [at(c, r - 1), at(c + 1, r), at(c, r + 1), at(c - 1, r)],
      });
    }
  }
  return indexed(p.cols, p.rows, cells);
}

const LIMIT = 64;

/** The layer drawings of the open document's grid. */
export class LayerGeometries {
  private readonly engine: EngineApi;
  private readonly cache = new Map<string, Promise<LayerGeometry>>();

  constructor(engine: EngineApi) {
    this.engine = engine;
  }

  /** The drawing of a layer of a plane of a space of a grid. */
  get(grid: Grid, size: Size, plane: Plane, layer: number): Promise<LayerGeometry> {
    if (plane !== 'XY') {
      return Promise.resolve(squareLayer(plane, size, layer));
    }
    const k = `${grid}:${size.join('x')}:${layer}`;
    const hit = this.cache.get(k);
    if (hit !== undefined) {
      this.cache.delete(k);
      this.cache.set(k, hit);
      return hit;
    }
    const p = this.engine.call('grid.geometry', { size, layer }).then((g) =>
      indexed(
        g.width,
        g.height,
        g.cells.map((c) => ({
          at: [c.at[0], c.at[1], c.at[2]],
          ...(c.polygon === undefined ? {} : { polygon: c.polygon.map((q): P2 => [q[0], q[1]]) }),
          ...(c.neighbours === undefined ? {} : { neighbours: c.neighbours }),
          ...(c.circle === undefined
            ? {}
            : {
                circle: {
                  centre: [c.circle.centre[0], c.circle.centre[1]] as P2,
                  radius: c.circle.radius,
                },
              }),
        })),
      ),
    );
    this.cache.set(k, p);
    p.catch(() => {
      this.cache.delete(k);
    });
    while (this.cache.size > LIMIT) {
      const oldest = this.cache.keys().next().value;
      if (oldest === undefined) {
        break;
      }
      this.cache.delete(oldest);
    }
    return p;
  }

  /** Forgets every drawing (another document: another grid). */
  clear(): void {
    this.cache.clear();
  }
}
