// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a point of the 3D view shows: a Raycaster hit's faceIndex is a
// triangle drawn (mesh.ts), which names the frame's triangle, whose
// `voxel` and `voxelFace` are the cell and its side (decision 0012,
// MeshFrame), as the original's GL picking names did (glPushName of
// _fb_index and _fb_face in voxelFrame_c::drawShape). Replaces the mockup's
// inPoly over its own projected faces. A pick also says which quarter of
// its side the point is in, as the view shows the side (3D editing does
// different things in different quarters).

import { Raycaster, Vector2, Vector3 } from 'three';
import type { Camera, Object3D } from 'three';

import { cellOf, FLAG } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';

/** A picked face. */
export interface Pick {
  /** The frame's triangle. */
  triangle: number;
  /** The cell index (x + X*(y + Y*z)) and the cell. */
  voxel: number;
  cell: [number, number, number];
  /** The cell's side, -1 for a bevel or a gap. */
  face: number;
  /** The face's flags. */
  flags: number;
  /** The quarter of the side under the point, as the view shows it (pickAt only; none for a bevel). */
  quarter?: Quarter;
}

/** A quarter of a side: top or bottom, left or right of its middle on the screen. */
export type Quarter = 'tl' | 'tr' | 'bl' | 'br';

/** The face a drawn triangle (a hit's faceIndex) belongs to. */
export function pickOf(mesh: BuiltMesh, drawnTriangle: number): Pick | null {
  const t = mesh.triangle[drawnTriangle];
  if (t === undefined) {
    return null;
  }
  const voxel = mesh.arrays.voxel[t] ?? 0;
  return {
    triangle: t,
    voxel,
    cell: cellOf(voxel, mesh.size),
    face: mesh.arrays.voxelFace[t] ?? -1,
    flags: mesh.arrays.flags[t] ?? 0,
  };
}

const raycaster = new Raycaster();

/**
 * The face under a point of the view (normalised device coordinates, -1 to
 * 1, y up), nearest first; inside faces are skipped unless they are shown.
 */
export function pickAt(
  ndc: Vector2,
  camera: Camera,
  target: Object3D,
  mesh: BuiltMesh,
  insideShown: boolean,
): Pick | null {
  raycaster.setFromCamera(ndc, camera);
  for (const hit of raycaster.intersectObject(target, false)) {
    if (hit.faceIndex === undefined || hit.faceIndex === null) {
      continue;
    }
    const p = pickOf(mesh, hit.faceIndex);
    if (p !== null && (insideShown || (p.flags & FLAG.inside) === 0)) {
      const mid = p.face < 0 ? null : sideMiddle(mesh, p.voxel, p.face);
      if (mid !== null) {
        mid.applyMatrix4(target.matrixWorld).project(camera);
        p.quarter = `${ndc.y >= mid.y ? 't' : 'b'}${ndc.x >= mid.x ? 'r' : 'l'}`;
      }
      return p;
    }
  }
  return null;
}

// the middle of a cell's side in mesh units: its triangles' centroids,
// weighted by their areas (a sphere's variable marker and the gaps left out)
function sideMiddle(mesh: BuiltMesh, voxel: number, face: number): Vector3 | null {
  const { positions, indices, voxel: vox, voxelFace, flags } = mesh.arrays;
  const skip = FLAG.variableFace | FLAG.gap;
  const a = new Vector3();
  const b = new Vector3();
  const c = new Vector3();
  const sum = new Vector3();
  let total = 0;
  const at = (v: Vector3, i: number): Vector3 =>
    v.set(positions[3 * i] ?? 0, positions[3 * i + 1] ?? 0, positions[3 * i + 2] ?? 0);
  for (let t = 0; t < vox.length; t++) {
    if (vox[t] !== voxel || voxelFace[t] !== face || ((flags[t] ?? 0) & skip) !== 0) {
      continue;
    }
    at(a, indices[3 * t] ?? 0);
    at(b, indices[3 * t + 1] ?? 0);
    at(c, indices[3 * t + 2] ?? 0);
    const centroid = a.clone().add(b).add(c).divideScalar(3);
    const area = b.sub(a).cross(c.sub(a)).length() / 2;
    sum.addScaledVector(centroid, area);
    total += area;
  }
  return total > 0 ? sum.divideScalar(total) : null;
}

/** The status bar's words for a pick (the mockup's "Cell …" of its non-cube grids). */
export function describePick(p: Pick): string {
  const [x, y, z] = p.cell;
  return `Cell ${x}, ${y}, ${z} · ${p.face < 0 ? 'edge' : `side ${p.face}`}`;
}
