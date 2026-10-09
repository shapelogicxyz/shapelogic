// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the 3D view draws besides the shape: the axes, the space's bounds
// and the current layer's slab. From docs/mockup/index.html: render3d's
// "bounds" (dashed, --line2), "axes" (from the origin to the size plus 0.7,
// --ax, --ay, --az, 2.5 px) and "active layer slab" (the one-cell-thick
// layer of the editor's plane, filled with --accent at 0.11 and outlined at
// 1.8 px). The slab comes from the voxel index and the shape's size: on the
// cube grid a layer is one unit; on the others it spans the layer's cells
// as the mesh has them, and an empty layer its share of the space. The
// dimming of the other layers is the face shader's (material.ts).

import { BoxGeometry, Color, Group, Mesh, MeshBasicMaterial, Vector2 } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

import type { Grid, Size } from '../../engine/api.generated.ts';
import type { MeshArrays } from '../../engine/frames.ts';

/** The editor's plane: XY is a z layer, XZ a y layer, YZ an x layer. */
export type Plane = 'XY' | 'XZ' | 'YZ';

/** The axis a plane's layers count along. */
export function layerAxis(plane: Plane): 0 | 1 | 2 {
  return plane === 'XY' ? 2 : plane === 'XZ' ? 1 : 0;
}

type V3 = [number, number, number];

/** The box of a layer of the editor's plane, in mesh units. */
export function layerSlab(
  grid: Grid,
  extent: V3,
  size: Size,
  plane: Plane,
  layer: number,
  arrays: MeshArrays | null,
): { lo: V3; hi: V3 } {
  const ai = layerAxis(plane);
  const lo: V3 = [0, 0, 0];
  const hi: V3 = [...extent];
  if (grid === 'bricks' || grid === 'sliding' || grid === 'stacking') {
    lo[ai] = layer;
    hi[ai] = layer + 1;
    return { lo, hi };
  }
  // the layer's cells as the mesh has them
  let a = Infinity;
  let b = -Infinity;
  if (arrays !== null) {
    const per = size[0] * size[1];
    const { voxel, indices, positions } = arrays;
    for (let t = 0; t < voxel.length; t++) {
      if (Math.floor((voxel[t] ?? 0) / per) !== layer) {
        continue;
      }
      for (let j = 0; j < 3; j++) {
        const z = positions[3 * (indices[3 * t + j] ?? 0) + 2] ?? 0;
        a = Math.min(a, z);
        b = Math.max(b, z);
      }
    }
  }
  if (a > b) {
    const step = extent[2] / Math.max(1, size[2]);
    a = layer * step;
    b = (layer + 1) * step;
  }
  lo[2] = a;
  hi[2] = b;
  return { lo, hi };
}

/** The colours of the overlays, from the theme. */
export interface OverlayColours {
  ax: string;
  ay: string;
  az: string;
  line: string;
  accent: string;
}

/** What to draw. */
export interface OverlayOptions {
  extent: V3;
  axes: boolean;
  bounds: boolean;
  slab: { lo: V3; hi: V3 } | null;
  colours: OverlayColours;
}

const BOX_EDGES = [
  [0, 1],
  [2, 3],
  [4, 5],
  [6, 7],
  [0, 2],
  [1, 3],
  [4, 6],
  [5, 7],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7],
] as const;

function boxLines(lo: V3, hi: V3): number[] {
  const corner = (i: number): V3 => [
    i & 1 ? hi[0] : lo[0],
    i & 2 ? hi[1] : lo[1],
    i & 4 ? hi[2] : lo[2],
  ];
  return BOX_EDGES.flatMap(([a, b]) => [...corner(a), ...corner(b)]);
}

/** The overlays of the view, rebuilt when their options change. */
export class Overlays {
  readonly group = new Group();
  private readonly resolution = new Vector2(1, 1);
  private readonly materials: LineMaterial[] = [];
  /** The geometries and materials made, freed by clear(). */
  private readonly owned: { dispose(): void }[] = [];

  /** The viewport's size in pixels (the lines' widths are pixels). */
  setResolution(w: number, h: number): void {
    this.resolution.set(w, h);
    for (const m of this.materials) {
      m.resolution.copy(this.resolution);
    }
  }

  update(o: OverlayOptions): void {
    this.clear();
    const [dx, dy, dz] = o.extent;
    if (o.bounds) {
      this.lines(boxLines([0, 0, 0], o.extent), o.colours.line, 1, true);
    }
    if (o.axes) {
      const l = 0.7;
      this.lines([0, 0, 0, dx + l, 0, 0], o.colours.ax, 2.5);
      this.lines([0, 0, 0, 0, dy + l, 0], o.colours.ay, 2.5);
      this.lines([0, 0, 0, 0, 0, dz + l], o.colours.az, 2.5);
    }
    if (o.slab !== null) {
      const { lo, hi } = o.slab;
      const geometry = new BoxGeometry(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
      const material = new MeshBasicMaterial({
        color: new Color(o.colours.accent),
        transparent: true,
        opacity: 0.11,
        depthWrite: false,
      });
      this.owned.push(geometry, material);
      const box = new Mesh(geometry, material);
      box.position.set((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2);
      box.renderOrder = 2;
      this.group.add(box);
      this.lines(boxLines(lo, hi), o.colours.accent, 1.8);
    }
  }

  private lines(points: number[], colour: string, width: number, dashed = false): void {
    const g = new LineSegmentsGeometry().setPositions(points);
    const m = new LineMaterial({
      color: new Color(colour).getHex(),
      linewidth: width,
      dashed,
      dashSize: 0.12,
      gapSize: 0.12,
      resolution: this.resolution,
    });
    const line = new LineSegments2(g, m);
    if (dashed) {
      line.computeLineDistances();
    }
    line.renderOrder = 1;
    this.materials.push(m);
    this.owned.push(g, m);
    this.group.add(line);
  }

  /** Removes and frees everything drawn. */
  clear(): void {
    this.group.clear();
    for (const d of this.owned) {
      d.dispose();
    }
    this.owned.length = 0;
    this.materials.length = 0;
  }
}
