// SPDX-License-Identifier: GPL-3.0-or-later
//
// The border of a sliding puzzle's tray (decision 0046): what the views
// share (which border shows, its color, where its picture lies) and the
// 3D views' geometry (the rim round the board and the blocks on the cells
// that are no floor).

import { describe, expect, it } from 'vitest';

import type { ShapeImage, TrayBorder } from '../../../src/engine/api.generated.ts';
import { pictureToCells, previewSize } from '../../../src/renderer/shell/picture-math.ts';
import { fromHex } from '../../../src/renderer/shell/piece-colors.ts';
import {
  BORDER_RIM,
  borderColor,
  borderImage,
  borderRim,
  floorTest,
  shownBorder,
  trayBox,
  WOOD_COLOR,
} from '../../../src/renderer/shell/tray-border.ts';
import { borderArrays, floorOfMesh } from '../../../src/renderer/view3d/tray-border.ts';

const border = (over: Partial<TrayBorder> = {}): TrayBorder => ({
  enabled: true,
  corners: 'round',
  material: 'smooth',
  color: null,
  thickness: 0,
  images: [],
  ...over,
});

// the area of the triangles looking up, and the extent of all of them
function measure(a: { positions: Float32Array; normals: Float32Array }): {
  top: number;
  lo: number[];
  hi: number[];
} {
  const p = a.positions;
  const n = a.normals;
  let top = 0;
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < p.length; i += 9) {
    const at = (k: number): number => p[i + k] ?? 0;
    for (let v = 0; v < 3; v++) {
      for (let c = 0; c < 3; c++) {
        lo[c] = Math.min(lo[c] ?? 0, at(3 * v + c));
        hi[c] = Math.max(hi[c] ?? 0, at(3 * v + c));
      }
    }
    if ((n[i + 2] ?? 0) > 0.5) {
      top += Math.abs((at(3) - at(0)) * (at(7) - at(1)) - (at(6) - at(0)) * (at(4) - at(1))) / 2;
    }
  }
  return { top, lo, hi };
}

describe('tray border', () => {
  it('a thin border’s rim is BORDER_RIM, a thick one’s its voxels', () => {
    expect(borderRim(null)).toBe(BORDER_RIM);
    expect(borderRim(border())).toBe(BORDER_RIM);
    expect(borderRim(border({ thickness: 1 }))).toBe(1);
    expect(borderRim(border({ thickness: 3 }))).toBe(3);
    expect(trayBox([4, 5, 1], borderRim(border({ thickness: 2 })))).toEqual([-2, -2, 8, 9]);
    // the 3D border reaches as far out as the rim
    const thick = measure(
      borderArrays({ size: [2, 1], floor: () => true, z0: 0, z1: 1, round: true, rim: 2 }),
    );
    expect(thick.lo).toEqual([-2, -2, 0]);
    expect(thick.hi).toEqual([4, 3, 1]);
  });

  it('the Apply image dialog’s preview grows with the window', () => {
    // a 13 x 9 tray with its thin rim, in a window of 1400 x 900: room for 500 x 300 and more
    const ratio = (13 + 2 * BORDER_RIM) / (9 + 2 * BORDER_RIM);
    const big = previewSize({ w: 1400, h: 900 }, ratio);
    const cs = Math.min((big.w - 8) / (13 + 2 * BORDER_RIM), (big.h - 8) / (9 + 2 * BORDER_RIM));
    expect(Math.floor(cs) * (13 + 2 * BORDER_RIM)).toBeGreaterThanOrEqual(500);
    expect(Math.floor(cs) * (9 + 2 * BORDER_RIM)).toBeGreaterThanOrEqual(300);
    expect(big.w).toBeLessThanOrEqual(1400 * 0.55);
    // never under 300 x 200, however small the window
    expect(previewSize({ w: 500, h: 400 }, ratio)).toEqual({ w: 300, h: 200 });
    // a tall piece: no wider than it needs, at least half again its height
    const tall = previewSize({ w: 1400, h: 900 }, 2 / 6);
    expect(tall.w).toBe(Math.round(tall.h * 1.5));
  });

  it('shows only a board’s border that is turned on', () => {
    expect(shownBorder(null)).toBeNull();
    expect(shownBorder({ border: null })).toBeNull();
    expect(shownBorder({ border: border({ enabled: false }) })).toBeNull();
    expect(shownBorder({ border: border() })).toEqual(border());
  });

  it('its color: its own, the wood’s for wood, else the board’s', () => {
    const board = fromHex('#383838');
    expect(borderColor(border(), board)).toBe(board);
    expect(borderColor(border({ material: 'textured' }), board)).toBe(board);
    expect(borderColor(border({ material: 'wood' }), board)).toBe(WOOD_COLOR);
    expect(borderColor(border({ material: 'wood', color: '#336699' }), board)).toEqual(
      fromHex('#336699'),
    );
    // a color as numbers (the Play board's) is taken as it is
    expect(borderColor({ material: 'smooth', color: [0.1, 0.2, 0.3] }, board)).toEqual([
      0.1, 0.2, 0.3,
    ]);
  });

  it('its picture lies over the whole tray, the rim included', () => {
    expect(trayBox([4, 5, 1], 0.5)).toEqual([-0.5, -0.5, 5, 6]);
    expect(trayBox([4, 5, 1])).toEqual([
      -BORDER_RIM,
      -BORDER_RIM,
      4 + 2 * BORDER_RIM,
      5 + 2 * BORDER_RIM,
    ]);
    const image: ShapeImage = {
      image: 0,
      hash: 'h',
      width: 60,
      height: 40,
      fit: 'stretch',
      src: [0, 0, 60, 40],
      box: [0, 0, 9, 9], // the board's when it was applied: not used
      turn: 0,
    };
    const t = pictureToCells(borderImage(image, [4, 5, 1], 0.5));
    // rounded to a millionth of a cell
    const at = (x: number, y: number): [number, number] => [
      Math.round((t[0] * x + t[2] * y + t[4]) * 1e6) / 1e6,
      Math.round((t[1] * x + t[3] * y + t[5]) * 1e6) / 1e6,
    ];
    // the picture's top left at the tray's top left corner (y up), its bottom right at the other
    expect(at(0, 0)).toEqual([-0.5, 5.5]);
    expect(at(60, 40)).toEqual([4.5, -0.5]);
  });

  it('the floor from cells, and from a mesh’s triangles', () => {
    const floor = floorTest([
      [0, 0, 0],
      [2, 1, 3],
    ]);
    expect([floor(0, 0), floor(2, 1), floor(1, 0)]).toEqual([true, true, false]);
    // a 3 x 2 x 2 board: cell (1, 0) on the lower layer, cell (2, 1) on the upper one
    const mesh = floorOfMesh(new Uint32Array([1, 1, 6 + 5]), [3, 2, 2]);
    expect([mesh(1, 0), mesh(2, 1), mesh(0, 0)]).toEqual([true, true, false]);
  });

  it('the 3D border: a rim round the board and a block on each cell that is no floor', () => {
    // a 2 x 1 board whose left cell is floor
    const shape = {
      size: [2, 1] as const,
      floor: (x: number, y: number) => x === 0 && y === 0,
      z0: 0,
      z1: 2,
      rim: 0.5,
    };
    const sharp = borderArrays({ ...shape, round: false });
    expect(sharp.positions.length % 9).toBe(0);
    expect(sharp.normals.length).toBe(sharp.positions.length);
    const s = measure(sharp);
    // the top: the tray's whole rectangle less the floor
    expect(s.top).toBeCloseTo(3 * 2 - 1, 6);
    expect(s.lo).toEqual([-0.5, -0.5, 0]);
    expect(s.hi).toEqual([2.5, 1.5, 2]);
    // every normal has length 1
    for (let i = 0; i < sharp.normals.length; i += 3) {
      const [x = 0, y = 0, z = 0] = sharp.normals.subarray(i, i + 3);
      expect(Math.hypot(x, y, z)).toBeCloseTo(1, 6);
    }
    // round corners: less by the corners cut off (a little more than the circle's, as chords)
    const r = measure(borderArrays({ ...shape, round: true }));
    const cut = (4 - Math.PI) * 0.5 * 0.5;
    expect(s.top - r.top).toBeGreaterThan(cut);
    expect(s.top - r.top).toBeLessThan(cut * 1.1);
    expect(r.lo).toEqual(s.lo);
    expect(r.hi).toEqual(s.hi);
    // no floor at all: the whole tray is solid
    const solid = measure(borderArrays({ ...shape, floor: () => false, round: false }));
    expect(solid.top).toBeCloseTo(3 * 2, 6);
    // walls face the floor: more triangles than the solid tray has
    expect(sharp.positions.length).toBeGreaterThan(
      borderArrays({ ...shape, floor: () => false, round: false }).positions.length,
    );
  });
});
