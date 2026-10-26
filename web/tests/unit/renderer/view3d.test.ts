// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's pure parts: a frame made into geometry (per-triangle
// attributes per vertex, the wire-frame style's faces, the variable marks
// as the original's drawShrinkTriangle and drawShrinkQuadrilateral), a
// pick's cell and side, the space's extent per grid, the camera's angles,
// the Top Up orbit (the mockup's Drag) and the Free Spin arc-ball, the Z
// axis directions' upright frames, and its animations.

import { Quaternion, Vector3 } from 'three';
import { describe, expect, it } from 'vitest';

import type { MeshArrays } from '../../../src/engine/frames.ts';
import {
  anglesOf,
  arcballPoint,
  baseOf,
  arcballTurn,
  Camera,
  dirToView,
  HOME,
  pixelsPerUnit,
  quatOf,
  toView,
  toWorld,
} from '../../../src/renderer/view3d/camera.ts';
import { DEFAULTS } from '../../../src/renderer/shell/prefs.ts';
import { gridExtent } from '../../../src/renderer/view3d/extent.ts';
import {
  buildMesh,
  cellOf,
  FLAG,
  markedPolygons,
  shrinkQuadrilateral,
  shrinkTriangle,
} from '../../../src/renderer/view3d/mesh.ts';
import { layerSlab } from '../../../src/renderer/view3d/overlays.ts';
import { describePick, pickOf } from '../../../src/renderer/view3d/picking.ts';

/** A frame of polygons given as corner lists, emitted as the engine's fans. */
function frame(
  polys: { corners: number[][]; voxel: number; face: number; flags: number; colour?: number }[],
): MeshArrays {
  const positions: number[] = [];
  const normals: number[] = [];
  const indices: number[] = [];
  const voxel: number[] = [];
  const voxelFace: number[] = [];
  const flags: number[] = [];
  const colour: number[] = [];
  const polygon: number[] = [];
  polys.forEach((p, pi) => {
    const base = positions.length / 3;
    for (const c of p.corners) {
      positions.push(...c);
      normals.push(0, 0, 1);
    }
    // the fan from the second corner: (c1, c2, c3), (c1, c3, c4) ... (c1, c(n-1), c0)
    const n = p.corners.length;
    for (let i = 2; i < n + 1; i++) {
      indices.push(base + 1, base + (i % n), base + ((i + 1) % n));
      voxel.push(p.voxel);
      voxelFace.push(p.face);
      flags.push(p.flags);
      colour.push(p.colour ?? 0);
      polygon.push(pi);
    }
    // drop the last, degenerate (c1, c0, c1) triangle of the loop above
    indices.splice(-3);
    voxel.pop();
    voxelFace.pop();
    flags.pop();
    colour.pop();
    polygon.pop();
  });
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint32Array(indices),
    voxel: new Uint32Array(voxel),
    voxelFace: new Int32Array(voxelFace),
    flags: new Uint32Array(flags),
    colour: new Uint8Array(colour),
    polygon: new Uint32Array(polygon),
    edgeLines: new Float32Array([0, 0, 0, 1, 0, 0]),
  };
}

const square = (z: number): number[][] => [
  [0, 0, z],
  [1, 0, z],
  [1, 1, z],
  [0, 1, z],
];

describe('mesh.ts', () => {
  it('decodes a cell index', () => {
    expect(cellOf(0, [3, 4, 5])).toEqual([0, 0, 0]);
    expect(cellOf(2 + 3 * (1 + 4 * 3), [3, 4, 5])).toEqual([2, 1, 3]);
  });

  it('expands the per-triangle arrays per vertex', () => {
    const a = frame([
      {
        corners: square(1),
        voxel: 2 + 3 * (1 + 4 * 3),
        face: 4,
        flags: FLAG.light | FLAG.wireframe,
        colour: 2,
      },
      { corners: square(0), voxel: 0, face: -1, flags: FLAG.bevel },
    ]);
    expect(a.flags).toHaveLength(4);
    const m = buildMesh(a, [3, 4, 5], 'drawing');
    expect(m.triangle).toEqual(new Uint32Array([0, 1, 2, 3]));
    const pos = m.faces.getAttribute('position');
    expect(pos.count).toBe(12);
    expect([pos.getX(0), pos.getY(0), pos.getZ(0)]).toEqual([1, 0, 1]);
    const flags = m.faces.getAttribute('aFlags');
    const col = m.faces.getAttribute('aColour');
    const cell = m.faces.getAttribute('aCell');
    for (let v = 0; v < 6; v++) {
      expect(flags.getX(v)).toBe(FLAG.light | FLAG.wireframe);
      expect(col.getX(v)).toBe(2);
      expect([cell.getX(v), cell.getY(v), cell.getZ(v)]).toEqual([2, 1, 3]);
    }
    expect(flags.getX(6)).toBe(FLAG.bevel);
    expect(m.edges).toBeNull();
    expect(m.marks).toBeNull();
    expect(buildMesh(a, [3, 4, 5], 'flat').edges).toEqual(a.edgeLines);
  });

  it('keeps only the wire-frame faces in the wire-frame style, mapping picks back', () => {
    const a = frame([
      { corners: square(1), voxel: 1, face: 4, flags: 0 },
      { corners: square(0), voxel: 5, face: 5, flags: FLAG.wireframe },
    ]);
    const m = buildMesh(a, [2, 3, 1], 'wireframe');
    expect(m.triangle).toEqual(new Uint32Array([2, 3]));
    expect(m.faces.getAttribute('position').count).toBe(6);
    const p = pickOf(m, 1);
    expect(p).toEqual({ triangle: 3, voxel: 5, cell: [1, 2, 0], face: 5, flags: FLAG.wireframe });
    expect(p === null ? '' : describePick(p)).toBe('Cell 1, 2, 0 · side 5');
    expect(pickOf(m, 2)).toBeNull();
    const bevel = buildMesh(
      frame([{ corners: square(0), voxel: 0, face: -1, flags: 0 }]),
      [1, 1, 1],
      'drawing',
    );
    const q = pickOf(bevel, 0);
    expect(q === null ? '' : describePick(q)).toBe('Cell 0, 0, 0 · edge');
  });

  it('finds the marked polygons in their order from the fans', () => {
    const quad = square(0);
    const tri = [
      [0, 0, 2],
      [2, 0, 2],
      [0, 2, 2],
    ];
    const a = frame([
      { corners: quad, voxel: 0, face: 0, flags: FLAG.variableMark },
      { corners: square(5), voxel: 0, face: 1, flags: 0 },
      { corners: tri, voxel: 0, face: 2, flags: FLAG.variableMark },
    ]);
    const polys = markedPolygons(a, new Uint32Array(a.flags.map((_, i) => i)));
    // from the fan's apex c1 on: c1, c2, c3, c0
    expect(polys).toEqual([
      [quad[1], quad[2], quad[3], quad[0]],
      [tri[1], tri[2], tri[0]],
    ]);
    const m = buildMesh(a, [1, 1, 1], 'drawing');
    expect(m.marks?.getAttribute('position').count).toBe(6 + 3);
  });

  it('shrinks the marks as the original: sides SHRINK (0.13) inwards, MY (0.005) up', () => {
    // a right triangle with legs 1: incircle radius (1 + 1 - √2) / 2
    const t = shrinkTriangle([0, 0, 0], [1, 0, 0], [0, 1, 0]);
    const r = (2 - Math.SQRT2) / 2;
    const c = r;
    const k = (r - 0.13) / r;
    expect(t[0]?.[0]).toBeCloseTo(c + k * (0 - r), 6);
    expect(t[1]?.[0]).toBeCloseTo(c + k * (1 - r), 6);
    expect(t[0]?.[2]).toBeCloseTo(0.005, 6);
    // the unit square: each corner moved 0.13 inwards on both axes
    const q = shrinkQuadrilateral([0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]);
    const corners = q.map((p) => p.map((v) => Number(v.toFixed(6))));
    expect(corners).toEqual([
      [0.13, 0.13, 0.005],
      [0.87, 0.13, 0.005],
      [0.87, 0.87, 0.005],
      [0.87, 0.87, 0.005],
      [0.13, 0.87, 0.005],
      [0.13, 0.13, 0.005],
    ]);
  });
});

describe('the extent and the layer slab', () => {
  it('are the original calculateSize per grid', () => {
    expect(gridExtent('bricks', [6, 6, 5])).toEqual([6, 6, 5]);
    const p = gridExtent('prisms', [4, 2, 1]);
    expect(p[0]).toBe(2.5);
    expect(p[1]).toBeCloseTo(1.7320508, 6);
    expect(gridExtent('spheres', [5, 5, 3])[2]).toBeCloseTo(1 + 2 * Math.SQRT1_2, 9);
    expect(gridExtent('rhombic', [25, 25, 25])).toEqual([10, 10, 10]);
    expect(gridExtent('rhombic', [20, 20, 15])).toEqual([8, 8, 6]);
    expect(gridExtent('tetraOcta', [18, 18, 12])).toEqual([6, 6, 4]);
  });

  it('is one unit of the plane on cubes, the layer’s cells on the other grids', () => {
    expect(layerSlab('bricks', [3, 4, 5], [3, 4, 5], 'XZ', 2, null)).toEqual({
      lo: [0, 2, 0],
      hi: [3, 3, 5],
    });
    const a = frame([
      {
        corners: square(0.7).map(([x, y]) => [x ?? 0, y ?? 0, 0.7]),
        voxel: 2 * 4,
        face: 0,
        flags: 0,
      },
    ]);
    // a cell of layer 2 (index 2 * X * Y) at z 0.7
    const f = Math.fround(0.7);
    expect(layerSlab('spheres', [2, 2, 3], [2, 2, 3], 'XY', 2, a)).toEqual({
      lo: [0, 0, f],
      hi: [2, 2, f],
    });
    expect(layerSlab('spheres', [2, 2, 3], [2, 2, 3], 'XY', 1, a)).toEqual({
      lo: [0, 0, 1],
      hi: [2, 2, 2],
    });
  });
});

describe('camera.ts', () => {
  const close = (
    a: { yaw: number; pitch: number; roll: number },
    b: { yaw: number; pitch: number; roll: number },
  ): void => {
    expect(a.yaw).toBeCloseTo(b.yaw, 6);
    expect(a.pitch).toBeCloseTo(b.pitch, 6);
    expect(a.roll).toBeCloseTo(b.roll, 6);
  };

  it('turns the mockup’s angles into the view and back', () => {
    close(anglesOf(quatOf({ yaw: -32, pitch: 26, roll: 15 })), { yaw: -32, pitch: 26, roll: 15 });
    // the mockup's mkRot: yaw about y, then pitch about x, then roll about z
    const q = quatOf({ yaw: 90, pitch: 0, roll: 0 });
    const x = toView(q, [1, 0, 0]);
    expect([x.x, x.y, x.z].map((v) => Math.round(v) + 0)).toEqual([0, 0, -1]);
    // a positive pitch looks from above: the world's +y comes towards the viewer
    const top = toView(quatOf({ yaw: 0, pitch: 90, roll: 0 }), [0, 1, 0]);
    expect([top.x, top.y, top.z].map((v) => Math.round(v) + 0)).toEqual([0, 0, 1]);
    const back = toWorld(q, [0, 0, -1]);
    expect([back.x, back.y, back.z].map((v) => Math.round(v) + 0)).toEqual([1, 0, 0]);
  });

  it('orbits half a degree a pixel, the pitch held inside ±89°', () => {
    const c = new Camera();
    c.orbit(20, -10);
    close(c.angles, { yaw: HOME.yaw + 10, pitch: HOME.pitch - 5, roll: 0 });
    c.orbit(0, 1000);
    expect(c.angles.pitch).toBeCloseTo(89, 6);
    // with a roll of 90°, a drag along x tilts
    const r = new Camera();
    r.q = quatOf({ yaw: 0, pitch: 0, roll: 90 });
    r.orbit(10, 0);
    close(r.angles, { yaw: 0, pitch: 5, roll: 90 });
  });

  it('stands each Z axis direction up, turned and never mirrored', () => {
    const at = (q: Quaternion, v: [number, number, number]): number[] =>
      [...toView(q, v).toArray()].map((n) => Math.round(n) + 0);
    // the upright frame seen head on: X, Y, Z of the world on the screen (x right, y up, z towards you)
    const axes = (z: Parameters<typeof baseOf>[0]): number[][] =>
      [
        [1, 0, 0],
        [0, 1, 0],
        [0, 0, 1],
      ].map((v) => at(baseOf(z), v as [number, number, number]));
    expect(axes('Z Towards')).toEqual([
      [1, 0, 0],
      [0, 1, 0],
      [0, 0, 1],
    ]);
    expect(axes('Z Up')).toEqual([
      [1, 0, 0],
      [0, 0, -1],
      [0, 1, 0],
    ]);
    // the default is the view as it was before the setting: no turn at all
    expect(baseOf(DEFAULTS.zAxis).equals(new Quaternion())).toBe(true);
    const d = new Camera();
    d.setBase(baseOf(DEFAULTS.zAxis));
    expect(d.q.equals(quatOf(HOME))).toBe(true);
    // the home view and the Top Up orbit are about the frame's up axis: Z stays up
    const c = new Camera();
    c.setBase(baseOf('Z Up'));
    close(c.angles, HOME);
    c.orbit(300, 0);
    expect(toView(c.q, [0, 0, 1]).x).toBeCloseTo(0, 9);
    expect(toView(c.q, [0, 0, 1]).y).toBeGreaterThan(0.8);
    c.home(0);
    c.step(1000);
    close(c.angles, HOME);
  });

  it('looks along a direction (dirToView)', () => {
    const front = dirToView([0, 0, 1], 0);
    expect([front.yaw + 0, front.pitch + 0, front.roll + 0]).toEqual([0, 0, 0]);
    expect(dirToView([1, 0, 0], 0).yaw).toBeCloseTo(-90, 9);
    expect(dirToView([0, 1, 0], 130)).toEqual({ yaw: 90, pitch: 89, roll: 0 });
    const v = dirToView([1, 1, 1], 0);
    expect(v.pitch).toBeCloseTo((Math.atan(1 / Math.SQRT2) * 180) / Math.PI, 9);
  });

  it('turns by the arc-ball as arcBall_c: the drag between two points of the ball, applied in front', () => {
    const centre = arcballPoint(50, 50, 101, 101);
    expect([centre.x, centre.y, centre.z]).toEqual([0, 0, 1]);
    const edge = arcballPoint(100, 50, 101, 101);
    expect([edge.x, edge.y, edge.z]).toEqual([1, 0, 0]);
    // outside the ball: on its rim
    const out = arcballPoint(150, 50, 101, 101);
    expect([out.x, out.y, out.z]).toEqual([1, 0, 0]);
    // centre to rim: 2 × 90° about y, as the original's quaternion (cross, dot)
    const t = arcballTurn(centre, edge);
    expect(t.equals(new Quaternion(0, 1, 0, 0))).toBe(true);
    const c = new Camera();
    c.q = new Quaternion();
    c.arcStart(50, 50, 101, 101);
    c.arcDrag(50 + 25, 50);
    const z = new Vector3(0, 0, 1).applyQuaternion(c.q);
    // a drag to the right brings the front towards the right
    expect(z.x).toBeGreaterThan(0.5);
    c.arcEnd();
    const kept = c.q.clone();
    c.arcDrag(0, 0);
    expect(c.q.equals(kept)).toBe(true);
  });

  it('animates home and fits, zooming smoothly within 0.3 to 4', () => {
    const c = new Camera();
    c.q = quatOf({ yaw: 40, pitch: -10, roll: 0 });
    c.zoom = 2;
    c.px = 30;
    c.fit(0);
    expect(c.step(180)).toBe(true);
    expect(c.zoom).toBeGreaterThan(1);
    c.step(400);
    expect([c.zoom, c.px, c.py]).toEqual([1, 0, 0]);
    close(c.angles, { yaw: 40, pitch: -10, roll: 0 });
    expect(c.moving).toBe(false);
    c.home(1000);
    c.step(2000);
    close(c.angles, HOME);
    c.zoomBy(100, 0);
    for (let t = 16; t < 3000; t += 16) {
      c.step(t);
    }
    expect(c.zoom).toBe(4);
    expect(c.moving).toBe(false);
  });

  it('frames the space as the mockup: its largest side with a margin fills the view', () => {
    expect(pixelsPerUnit(800, 600, [3, 3, 3], 1)).toBeCloseTo(600 / (3 * 2.05 + 1.5), 9);
    expect(pixelsPerUnit(800, 600, [3, 10, 3], 2)).toBeCloseTo((600 / 22) * 2, 9);
  });
});
