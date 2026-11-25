// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view's grid tables (view3d/extent.ts: gridExtent, cellToMesh,
// isCubeGrid) against the rules engine.info sends for every grid it knows
// (the core's mesh::spaceRule, which a C++ test holds to calculateSize and
// recalcSpaceCoordinates, and GridTraits' cubeLike; cleanup item 12.8). The
// client keeps its copy because the view and the tests without an engine
// need it at once; this test is what ties it to the engine.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import type { Matrix3 } from '../../src/engine/api.generated.ts';
import { cellToMesh, gridExtent, isCubeGrid } from '../../src/renderer/view3d/extent.ts';
import { ENGINE, Session } from '../helpers/engine.ts';

type V3 = [number, number, number];

const times = (m: Matrix3, v: V3): V3 =>
  m.map((row) => row[0] * v[0] + row[1] * v[1] + row[2] * v[2]) as V3;

const close = (got: V3, want: V3): void => {
  for (let i = 0; i < 3; i++) {
    expect(got[i]).toBeCloseTo(want[i] ?? NaN, 9);
  }
};

describe.skipIf(ENGINE === null)("the view's grid tables and engine.info", () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('place, frame and tell the cube grids as the engine does, on every grid', async () => {
    const info = await s.client.call('engine.info');
    expect(info.grids.length).toBeGreaterThan(8);
    for (const g of info.grids) {
      expect({ grid: g.grid, cube: isCubeGrid(g.grid) }).toEqual({
        grid: g.grid,
        cube: g.cubeLike,
      });
      const cells: V3[] = [
        [1, 0, 0],
        [0, 1, 0],
        [0, 0, 1],
        [3, -2, 5],
        [-4, 7, -1],
      ];
      for (const c of cells) {
        close(cellToMesh(g.grid, c), times(g.cellToMesh, c));
      }
      const { unit, matrix, offset } = g.extent;
      for (const size of [
        [1, 1, 1],
        [2, 3, 4],
        [5, 5, 5],
        [6, 11, 3],
        [13, 7, 10],
        [20, 20, 15],
      ] as V3[]) {
        const units = size.map((n) => Math.ceil(n / unit)) as V3;
        const want = times(matrix, units).map((x, i) => x + (offset[i] ?? NaN)) as V3;
        close(gridExtent(g.grid, size), want);
      }
    }
  });
});
