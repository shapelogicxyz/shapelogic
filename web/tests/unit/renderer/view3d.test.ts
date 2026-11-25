// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's pure parts: a frame made into geometry (per-triangle
// attributes per vertex, the wire-frame style's faces, the variable marks
// as the original's drawShrinkTriangle and drawShrinkQuadrilateral), a
// pick's cell and side, the space's extent per grid, the camera's angles,
// the Top Up orbit (the mockup's Drag) and the Free Spin arc-ball, the Z
// axis directions' upright frames, its animations, and the text drawn on a
// solution's piece (its label, its name cut short, or both).

import { BufferAttribute, BufferGeometry, Quaternion, Vector3 } from 'three';
import { describe, expect, it } from 'vitest';

import type { MeshArrays } from '../../../src/engine/frames.ts';
import {
  anglesOf,
  arcballPoint,
  arcballStep,
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
  EdgeLines,
  FLAG,
  markedPolygons,
  shrinkQuadrilateral,
  shrinkTriangle,
} from '../../../src/renderer/view3d/mesh.ts';
import { layerSlab, Overlays } from '../../../src/renderer/view3d/overlays.ts';
import type { OverlayOptions } from '../../../src/renderer/view3d/overlays.ts';
import { Preview } from '../../../src/renderer/view3d/preview.ts';
import type { Ghost } from '../../../src/renderer/view3d/preview.ts';
import { describePick, pickOf } from '../../../src/renderer/view3d/picking.ts';
import { labelSpot, pieceText, SolutionScene } from '../../../src/renderer/view3d/solution.ts';

/** A frame of polygons given as corner lists, emitted as the engine's fans. */
function frame(
  polys: { corners: number[][]; voxel: number; face: number; flags: number; color?: number }[],
): MeshArrays {
  const positions: number[] = [];
  const normals: number[] = [];
  const indices: number[] = [];
  const voxel: number[] = [];
  const voxelFace: number[] = [];
  const flags: number[] = [];
  const color: number[] = [];
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
      color.push(p.color ?? 0);
      polygon.push(pi);
    }
    // drop the last, degenerate (c1, c0, c1) triangle of the loop above
    indices.splice(-3);
    voxel.pop();
    voxelFace.pop();
    flags.pop();
    color.pop();
    polygon.pop();
  });
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint32Array(indices),
    voxel: new Uint32Array(voxel),
    voxelFace: new Int32Array(voxelFace),
    flags: new Uint32Array(flags),
    color: new Uint8Array(color),
    polygon: new Uint32Array(polygon),
    edgeLines: new Float32Array([0, 0, 0, 1, 0, 0]),
    imageUv: new Float32Array(0),
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
        color: 2,
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
    const col = m.faces.getAttribute('aColor');
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
    // the cube grids count from 1, as their rulers and layers do
    expect(p === null ? '' : describePick(p, true)).toBe('Cell 2, 3, 1 · side 5');
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
    // a positive pitch looks from above: the world's +y comes toward the viewer
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
    // the upright frame seen head on: X, Y, Z of the world on the screen (x right, y up, z toward you)
    const axes = (z: Parameters<typeof baseOf>[0]): number[][] =>
      [
        [1, 0, 0],
        [0, 1, 0],
        [0, 0, 1],
      ].map((v) => at(baseOf(z), v as [number, number, number]));
    expect(axes('Z Toward')).toEqual([
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
    const center = arcballPoint(50, 50, 101, 101);
    expect([center.x, center.y, center.z]).toEqual([0, 0, 1]);
    // past r² = ½ on Bell's sheet (z = 1 / 2r), a unit vector
    const edge = arcballPoint(100, 50, 101, 101);
    expect(edge.x).toBeCloseTo(2 / Math.sqrt(5), 9);
    expect(edge.z).toBeCloseTo(1 / Math.sqrt(5), 9);
    const out = arcballPoint(150, 50, 101, 101);
    expect(out.length()).toBeCloseTo(1, 9);
    expect(out.z).toBeLessThan(edge.z);
    // a turn between two points of the ball: twice their angle, as the original's quaternion (cross, dot)
    const t = arcballTurn(center, new Vector3(1, 0, 0));
    expect(t.equals(new Quaternion(0, 1, 0, 0))).toBe(true);
    const c = new Camera();
    c.q = new Quaternion();
    c.arcStart(50, 50, 101, 101);
    c.arcDrag(50 + 25, 50);
    const z = new Vector3(0, 0, 1).applyQuaternion(c.q);
    // a drag to the right brings the front toward the right
    expect(z.x).toBeGreaterThan(0.5);
    c.arcEnd();
    const kept = c.q.clone();
    c.arcDrag(0, 0);
    expect(c.q.equals(kept)).toBe(true);
  });

  it('turns step by step: no snap back past the far side, and on outside the ball', () => {
    const angleOf = (q: Quaternion): number => 2 * Math.acos(Math.min(1, Math.abs(q.w)));
    // a slow drag right, across the ball and far past its rim: every step turns, none jumps
    const c = new Camera();
    c.q = new Quaternion();
    c.arcStart(50, 50, 101, 101);
    let prev = c.q.clone();
    for (let x = 52; x <= 1000; x += 2) {
      c.arcDrag(x, 50);
      const step = angleOf(prev.clone().invert().multiply(c.q));
      expect(step).toBeGreaterThan(1e-4);
      expect(step).toBeLessThan(0.15);
      prev = c.q.clone();
    }
    // the front went right round more than once, about y only
    const y = new Vector3(0, 1, 0).applyQuaternion(c.q);
    expect(y.y).toBeCloseTo(1, 9);
    // outside the ball a move about the center rolls about the view's z
    const roll = arcballStep(150, 50, 50, -50, 101, 101);
    const z = new Vector3(0, 0, 1).applyQuaternion(roll);
    expect(z.z).toBeCloseTo(1, 9);
    expect(angleOf(roll)).toBeGreaterThan(1);
    // and a move away from it tilts, at the rim's rate (1.6 radians a radius)
    const tilt = arcballStep(150, 50, 175, 50, 101, 101);
    expect(new Vector3(0, 1, 0).applyQuaternion(tilt).y).toBeCloseTo(1, 9);
    expect(angleOf(tilt)).toBeCloseTo(0.8, 9);
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

describe('the text drawn on a solution piece', () => {
  const p = { label: 'S2', name: 'My long label' };
  const show = (label: boolean, name: boolean, chars = 6) => ({ label, name, chars });
  it('says the label, the name cut to 6 characters per voxel of width, or both', () => {
    expect(DEFAULTS.labelCharsPerVoxel).toBe(6);
    expect(pieceText(p, show(true, false))).toBe('S2');
    expect(pieceText(p, show(false, true))).toBe('My lon');
    expect(pieceText(p, show(false, true), 2)).toBe('My long labe');
    expect(pieceText(p, show(false, true), 3)).toBe('My long label');
    expect(pieceText({ label: 'S2', name: 'Corner' }, show(true, true))).toBe('S2 - Corner');
    expect(pieceText(p, show(false, false))).toBe('');
  });
  it('cuts the name to the characters set for each voxel', () => {
    expect(pieceText(p, show(false, true, 1))).toBe('M');
    expect(pieceText(p, show(false, true, 1), 4)).toBe('My l');
    expect(pieceText(p, show(false, true, 13))).toBe('My long label');
    expect(pieceText({ name: 'abcdefghijklmnopq' }, show(false, true, 14))).toBe('abcdefghijklmn');
    // a width under one voxel counts as one
    expect(pieceText(p, show(false, true), 0)).toBe('My lon');
  });
  it('with no limit draws the whole name, a line every 6 characters per voxel', () => {
    const name = 'abcdefghijklmnopqrstuvwxyz0123456789';
    expect(pieceText({ name }, show(false, true, 0), 2)).toBe(
      'abcdefghijkl\nmnopqrstuvwx\nyz0123456789',
    );
    expect(pieceText({ label: 'S2', name: 'My long label' }, show(true, true, 0), 3)).toBe(
      'S2 - My long label',
    );
    expect(pieceText({ name: 'A very long piece name' }, show(false, true, 0))).toBe(
      'A very\nlong\npiece\nname',
    );
  });
  it('falls back to what there is, and keeps an emoji whole', () => {
    expect(pieceText({ label: 'S3', name: '' }, show(true, true))).toBe('S3');
    expect(pieceText({ label: 'S3' }, show(false, true))).toBe('');
    expect(pieceText({ name: '🧩🧩🧩🧩🧩🧩🧩🧩🧩' }, show(false, true))).toBe('🧩🧩🧩🧩🧩🧩');
  });
});

/** A piece of unit cubes at these cells (x, y, z), as labelSpot reads a mesh: each cube's corners and its aCell. */
function cubes(cells: [number, number, number][], gap = 0): BufferGeometry {
  const pos: number[] = [];
  const cell: number[] = [];
  for (const [x, y, z] of cells) {
    for (let k = 0; k < 8; k++) {
      pos.push(
        x + (k & 1 ? 1 - gap : gap),
        y + (k & 2 ? 1 - gap : gap),
        z + (k & 4 ? 1 - gap : gap),
      );
      cell.push(x, y, z);
    }
  }
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(new Float32Array(pos), 3));
  g.setAttribute('aCell', new BufferAttribute(new Float32Array(cell), 3));
  return g;
}

describe('where a solution piece’s label goes', () => {
  const at = (s: { at: Vector3 }): number[] => [s.at.x, s.at.y, s.at.z];
  it('a single cube: its middle, one voxel wide', () => {
    const s = labelSpot(cubes([[2, 3, 4]]), 1);
    expect(s.width).toBe(1);
    expect(at(s)).toEqual([2.5, 3.5, 4.5]);
  });
  it('the widest layer from the bottom, Y up: an upside-down T has its label on the bar', () => {
    // a bar of 3 at y 0, a stem of 2 above its middle
    const s = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [2, 0, 0],
        [1, 1, 0],
        [1, 2, 0],
      ]),
      1,
    );
    expect(s.width).toBe(3);
    expect(at(s)).toEqual([1.5, 0.5, 0.5]);
  });
  it('the widest layer: a T has its label on the bar at the top', () => {
    const s = labelSpot(
      cubes([
        [1, 0, 0],
        [1, 1, 0],
        [0, 2, 0],
        [1, 2, 0],
        [2, 2, 0],
        [3, 2, 0],
      ]),
      1,
    );
    expect(s.width).toBe(4);
    expect(at(s)).toEqual([2, 2.5, 0.5]);
  });
  it('the lowest of the widest layers', () => {
    // two bars of 2 at y 1 and y 3, single cubes between and below
    const s = labelSpot(
      cubes([
        [0, 0, 0],
        [0, 1, 0],
        [1, 1, 0],
        [0, 2, 0],
        [0, 3, 0],
        [1, 3, 0],
      ]),
      1,
    );
    expect(s.width).toBe(2);
    expect(at(s)).toEqual([1, 1.5, 0.5]);
  });
  it('with Z up, the layers are counted up Z', () => {
    // a bar of 3 along X at z 2 over a column of two
    const g = cubes([
      [1, 0, 0],
      [1, 0, 1],
      [0, 0, 2],
      [1, 0, 2],
      [2, 0, 2],
    ]);
    const z = labelSpot(g, 2);
    expect(z.width).toBe(3);
    expect(at(z)).toEqual([1.5, 0.5, 2.5]);
    // up Y they are all one layer, y 0: three columns, the front cell (z 2) holding it
    const y = labelSpot(g, 1);
    expect(y.width).toBe(3);
    expect(at(y)).toEqual([1.5, 0.5, 2.5]);
  });
  it('a gap breaks a layer: the longest run, the leftmost of two', () => {
    // X X . X X X at y 0; X X . X X at y 1
    const s = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [3, 0, 0],
        [4, 0, 0],
        [5, 0, 0],
        [0, 1, 0],
        [1, 1, 0],
        [3, 1, 0],
        [4, 1, 0],
      ]),
      1,
    );
    expect(s.width).toBe(3);
    expect(at(s)).toEqual([4.5, 0.5, 0.5]);
    const two = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [3, 0, 0],
        [4, 0, 0],
      ]),
      1,
    );
    expect(two.width).toBe(2);
    expect(at(two)).toEqual([1, 0.5, 0.5]);
  });
  it('a layer’s cells at other depths count once per column, and the front cell holds the label', () => {
    // an L in the layer y 0: x 0..2 at z 0, and x 0 again at z 1 and z 2
    const s = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [2, 0, 0],
        [0, 0, 1],
        [0, 0, 2],
      ]),
      1,
    );
    expect(s.width).toBe(3);
    expect(at(s)).toEqual([1.5, 0.5, 0.5]);
    // two rows of 2, one behind the other: the front one (+Z toward the eye)
    const rows = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [0, 0, 1],
        [1, 0, 1],
      ]),
      1,
    );
    expect(rows.width).toBe(2);
    expect(at(rows)).toEqual([1, 0.5, 1.5]);
    // with Z up, -Y is toward the eye
    const zUp = labelSpot(
      cubes([
        [0, 0, 0],
        [1, 0, 0],
        [0, 1, 0],
        [1, 1, 0],
      ]),
      2,
    );
    expect(at(zUp)).toEqual([1, 0.5, 0.5]);
  });
  it('bevelled cubes (a little smaller than a cell) count the same', () => {
    const s = labelSpot(
      cubes(
        [
          [0, 0, 0],
          [1, 0, 0],
          [2, 0, 0],
          [3, 0, 0],
          [4, 0, 0],
        ],
        0.06,
      ),
      1,
    );
    expect(s.width).toBe(5);
    expect(s.at.x).toBeCloseTo(2.5, 6);
  });
});

describe('a sliding tray’s walls on the Solver tab', () => {
  it('are black on the light theme and white on the dark one', () => {
    const scene = new SolutionScene();
    expect(scene.wallColor).toBe('#000000');
    scene.setDark(true);
    expect(scene.wallColor).toBe('#ffffff');
    scene.setDark(false);
    expect(scene.wallColor).toBe('#000000');
    scene.dispose();
  });
});

describe('the pieces scene’s edge lines (cleanup item 6.9)', () => {
  it('are built once a mesh for all the copies of a shape, the old ones disposed for another', () => {
    const mesh = () =>
      buildMesh(frame([{ corners: square(0), voxel: 0, face: 0, flags: 0 }]), [1, 1, 1], 'flat');
    const disposed: unknown[] = [];
    const lines = new EdgeLines();
    const first = mesh();
    // the selected piece and three copies, drawn in one set() and the next
    const drawn = [1, 2, 3, 4, 5, 6, 7, 8].map(() => lines.for(first));
    const one = drawn[0];
    expect(one).not.toBeNull();
    one?.addEventListener('dispose', () => disposed.push(one));
    expect(new Set(drawn).size).toBe(1);
    expect(disposed).toEqual([]);
    // another mesh of the shape: its lines, the old ones disposed once
    const two = lines.for(mesh());
    expect(two).not.toBe(one);
    expect(disposed).toEqual([one]);
    // a mesh without edges (the drawing style) has none
    expect(lines.for(buildMesh(frame([]), [1, 1, 1], 'drawing'))).toBeNull();
    lines.dispose();
  });
});

describe('the edit preview and the overlays drawn again only on a change (cleanup items 10.1, 10.2)', () => {
  it('keeps the preview for the same ghosts, and its materials for others', () => {
    const mesh = () =>
      buildMesh(frame([{ corners: square(0), voxel: 0, face: 0, flags: 0 }]), [1, 1, 1], 'flat');
    const m1 = mesh();
    const p = new Preview();
    const box = (alpha: number): Ghost => ({
      kind: 'box',
      at: [0, 0, 0],
      fill: '#ff0000',
      edge: '#0000ff',
      alpha,
      dashed: true,
    });
    const cell: Ghost = { kind: 'cell', voxel: 0, fill: '#00ff00', alpha: 0.4 };
    expect(p.set([box(0.5), cell], m1)).toBe(true);
    const shown = [...p.group.children];
    expect(shown.length).toBe(3);
    // the same ghosts (another array, equal): nothing made again
    expect(p.set([box(0.5), cell], m1)).toBe(false);
    expect(p.group.children).toEqual(shown);
    // the same ghosts cut from another mesh: made again
    expect(p.set([box(0.5), cell], mesh())).toBe(true);
    const again = p.group.children;
    expect(again[0]).not.toBe(shown[0]);
    // the materials are kept: the same fill and alpha, the same material
    const mat = (o: unknown): unknown => (o as { material: unknown }).material;
    expect(mat(again[0])).toBe(mat(shown[0]));
    expect(mat(again[1])).toBe(mat(shown[1]));
    // another alpha is another material; none shown is none
    expect(p.set([box(0.25)], m1)).toBe(true);
    expect(mat(p.group.children[0])).not.toBe(mat(shown[0]));
    expect(p.set([], m1)).toBe(true);
    expect(p.size).toBe(0);
    expect(p.set([], m1)).toBe(false);
    p.dispose();
  });

  it('keeps the overlays for the same options', () => {
    const o = new Overlays();
    const opts = (layer: number): OverlayOptions => ({
      extent: [3, 3, 3],
      axes: true,
      bounds: true,
      slab: { lo: [0, 0, layer], hi: [3, 3, layer + 1] },
      colors: { ax: '#ff0000', ay: '#00ff00', az: '#0000ff', line: '#888888', accent: '#2f6df6' },
    });
    o.update(opts(0));
    const shown = [...o.group.children];
    expect(shown.length).toBe(6);
    o.update(opts(0));
    expect(o.group.children).toEqual(shown);
    // another layer: new geometry, the same materials
    o.update(opts(1));
    const mat = (x: unknown): unknown => (x as { material: unknown }).material;
    expect(o.group.children[0]).not.toBe(shown[0]);
    expect(o.group.children.map(mat)).toEqual(shown.map(mat));
    o.dispose();
  });
});
