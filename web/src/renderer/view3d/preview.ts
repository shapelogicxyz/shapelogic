// SPDX-License-Identifier: GPL-3.0-or-later
//
// The live preview of 3D editing: what a click would do, drawn over the
// shape before it is done (the mockup's render3d "live preview for 3D
// editing", with its mirror and span ghosts): a cube grid's cell as a box
// (the mockup's `ghost`), a cell the shape has as its own faces from the
// engine's mesh (the mockup's `gc` of its non-cube grids), one side of a
// cell (the face a cell would grow from on the other grids: an empty
// cell's shape there is not the client's to know), and a refused target
// (the danger colour on the face clicked). Drawn on top, without depth,
// as the mockup painted its ghosts last.

import {
  BoxGeometry,
  BufferAttribute,
  BufferGeometry,
  Color,
  DoubleSide,
  EdgesGeometry,
  Group,
  LineBasicMaterial,
  LineDashedMaterial,
  LineSegments,
  Mesh,
  MeshBasicMaterial,
} from 'three';

import type { MeshArrays } from '../../engine/frames.ts';
import { FLAG } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';

type V3 = [number, number, number];

/** One thing the preview draws. */
export type Ghost =
  /** A cell of the cube grid (mesh units: the cell's corner). */
  | { kind: 'box'; at: V3; fill: string; edge: string; alpha: number; dashed: boolean }
  /** A cell the mesh has, by its index. */
  | { kind: 'cell'; voxel: number; fill: string; alpha: number }
  /** A whole shape from a frame of its own (what a tool tab button would make of it). */
  | { kind: 'frame'; arrays: MeshArrays; fill: string; alpha: number }
  /** One side of a cell the mesh has (voxelFace), by its index. */
  | {
      kind: 'face';
      voxel: number;
      face: number;
      fill: string;
      alpha: number;
      /** Triangles with these flags are left out (the sphere grid's connections: 1). */
      skipFlags?: number;
    };

/** The triangles of the frame of one cell (and one side), as a geometry. */
export function cellTriangles(
  mesh: BuiltMesh,
  voxel: number,
  face: number | null,
  skipFlags = 0,
): BufferGeometry | null {
  const { positions, indices, voxelFace, flags } = mesh.arrays;
  const vox = mesh.arrays.voxel;
  const out: number[] = [];
  for (let t = 0; t < vox.length; t++) {
    if (
      vox[t] !== voxel ||
      (face !== null && voxelFace[t] !== face) ||
      ((flags[t] ?? 0) & skipFlags) !== 0
    ) {
      continue;
    }
    for (let j = 0; j < 3; j++) {
      const v = indices[3 * t + j] ?? 0;
      out.push(positions[3 * v] ?? 0, positions[3 * v + 1] ?? 0, positions[3 * v + 2] ?? 0);
    }
  }
  if (out.length === 0) {
    return null;
  }
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(new Float32Array(out), 3));
  return g;
}

/**
 * The faces of a frame that show a shape's outline: the gaps between cells,
 * the inner sides of a hollow shape and the variable marks are left out.
 */
export function frameTriangles(arrays: MeshArrays): BufferGeometry | null {
  const { positions, normals, indices, flags } = arrays;
  const skip = FLAG.gap | FLAG.inside | FLAG.variableMark | FLAG.variableFace;
  const kept: number[] = [];
  for (let t = 0; t < flags.length; t++) {
    if (((flags[t] ?? 0) & skip) === 0) {
      kept.push(indices[3 * t] ?? 0, indices[3 * t + 1] ?? 0, indices[3 * t + 2] ?? 0);
    }
  }
  if (kept.length === 0) {
    return null;
  }
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(positions, 3));
  g.setAttribute('normal', new BufferAttribute(normals, 3));
  g.setIndex(kept);
  return g;
}

export class Preview {
  readonly group = new Group();
  private readonly owned: { dispose(): void }[] = [];

  constructor() {
    this.group.renderOrder = 10;
  }

  /** Shows these ghosts instead of the ones before. */
  set(ghosts: readonly Ghost[], mesh: BuiltMesh | null): void {
    this.clear();
    for (const g of ghosts) {
      if (g.kind === 'box') {
        this.box(g);
      } else if (g.kind === 'frame') {
        const geo = frameTriangles(g.arrays);
        if (geo !== null) {
          this.faces(geo, g.fill, g.alpha);
        }
      } else if (mesh !== null) {
        const geo =
          g.kind === 'face'
            ? cellTriangles(mesh, g.voxel, g.face, g.skipFlags ?? 0)
            : cellTriangles(mesh, g.voxel, null);
        if (geo !== null) {
          this.faces(geo, g.fill, g.alpha);
        }
      }
    }
  }

  /** The number of things shown (the view's state for the tests). */
  get size(): number {
    return this.group.children.length;
  }

  private material(fill: string, alpha: number): MeshBasicMaterial {
    const m = new MeshBasicMaterial({
      color: new Color(fill),
      transparent: true,
      opacity: alpha,
      side: DoubleSide,
      depthTest: false,
      depthWrite: false,
    });
    this.owned.push(m);
    return m;
  }

  private box(g: Extract<Ghost, { kind: 'box' }>): void {
    const geo = new BoxGeometry(1, 1, 1);
    this.owned.push(geo);
    const m = new Mesh(geo, this.material(g.fill, g.alpha));
    m.position.set(g.at[0] + 0.5, g.at[1] + 0.5, g.at[2] + 0.5);
    m.renderOrder = 10;
    this.group.add(m);
    const edges = new EdgesGeometry(geo);
    this.owned.push(edges);
    const lm = g.dashed
      ? new LineDashedMaterial({
          color: new Color(g.edge),
          dashSize: 0.12,
          gapSize: 0.09,
          depthTest: false,
        })
      : new LineBasicMaterial({ color: new Color(g.edge), depthTest: false });
    this.owned.push(lm);
    const line = new LineSegments(edges, lm);
    if (g.dashed) {
      line.computeLineDistances();
    }
    line.position.copy(m.position);
    line.renderOrder = 11;
    this.group.add(line);
  }

  private faces(geo: BufferGeometry, fill: string, alpha: number): void {
    this.owned.push(geo);
    const m = new Mesh(geo, this.material(fill, alpha));
    m.renderOrder = 10;
    this.group.add(m);
  }

  /** Removes and frees everything shown. */
  clear(): void {
    this.group.clear();
    for (const d of this.owned) {
      d.dispose();
    }
    this.owned.length = 0;
  }
}
