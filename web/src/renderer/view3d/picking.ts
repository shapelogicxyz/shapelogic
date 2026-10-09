// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a point of the 3D view shows: a Raycaster hit's faceIndex is a
// triangle drawn (mesh.ts), which names the frame's triangle, whose
// `voxel` and `voxelFace` are the cell and its side (decision 0012,
// MeshFrame), as the original's GL picking names did (glPushName of
// _fb_index and _fb_face in voxelFrame_c::drawShape). Replaces the mockup's
// inPoly over its own projected faces.

import { Raycaster, Vector2 } from 'three';
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
}

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
      return p;
    }
  }
  return null;
}

/** The status bar's words for a pick (the mockup's "Cell …" of its non-cube grids). */
export function describePick(p: Pick): string {
  const [x, y, z] = p.cell;
  return `Cell ${x}, ${y}, ${z} · ${p.face < 0 ? 'edge' : `side ${p.face}`}`;
}
