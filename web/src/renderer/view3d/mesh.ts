// SPDX-License-Identifier: GPL-3.0-or-later
//
// A MeshFrame (shape.mesh, playback.mesh) as three.js geometry. The frame's
// arrays come through meshArrays() (frames.ts), never parsed by hand; the
// per-triangle colour, flags, cell and side are expanded per vertex, so the
// view's options (colour view, lighting, x-ray, the dimmed layers) are
// uniforms of material.ts and need no rebuild.
//
// What is drawn follows the face loop of voxelFrame_c::drawShape in the
// original (src/gui/voxelframe.cpp): the wire-frame style keeps only the
// wire-frame faces (flag 8), as its "gridline" mode did; a variable cell's
// faces (flag 2) get the black inner polygon of drawShrinkTriangle and
// drawShrinkQuadrilateral (ported below with SHRINK and MY); the flat
// style's edge lines are the frame's `edgeLines`. Nothing of the mockup's
// hand-projected cubes is left.

import { BufferAttribute, BufferGeometry } from 'three';

import type { MeshStyle, Size } from '../../engine/api.generated.ts';
import type { MeshArrays } from '../../engine/frames.ts';

/** The FaceFlag bits of a frame (decision 0012, MeshFrame). */
export const FLAG = {
  /** The lighter colour of the checker pattern. */
  light: 1,
  /** A variable cell's face: the variable mark is drawn on it. */
  variableMark: 2,
  /** The face is itself the variable marker (drawn black). */
  variableFace: 4,
  /** Kept by the wire-frame style. */
  wireframe: 8,
  /** The bevel of a cell's edge. */
  bevel: 16,
  /** The gap between neighbouring cells. */
  gap: 32,
  /** The inner side of a hollow shape: hidden unless x-ray. */
  inside: 128,
} as const;

/** How far the variable mark lies inside its face, and above it (voxelframe.cpp's SHRINK and MY). */
const SHRINK = 0.13;
const MY = 0.005;

/**
 * Where a mesh came from, so the same can be asked in another style (the
 * vector image asks for the flat mesh of what the view draws): a shape,
 * at a revision, turned by a transformation; or a piece of a playback.
 */
export type MeshSource =
  | { kind: 'shape'; shape: string; revision: number; transformation: number }
  | { kind: 'playback'; handle: string; piece: number };

/** A frame made into geometry. */
export interface BuiltMesh {
  /**
   * The faces, one vertex per triangle corner: `position`, `normal`, and
   * per vertex the triangle's `aFlags`, `aColour` (the constraint colour's
   * number, 0 none) and `aCell` (x, y, z of its cell).
   */
  faces: BufferGeometry;
  /** For each triangle drawn (a raycast's faceIndex), the frame's triangle. */
  triangle: Uint32Array;
  /** The black variable marks (position, normal), or null when the shape has no variable cells. */
  marks: BufferGeometry | null;
  /** The edge lines of the flat style (two points per line), or null. */
  edges: Float32Array | null;
  /** The frame's arrays, for picking. */
  arrays: MeshArrays;
  /** The shape's size: a cell index is x + X*(y + Y*z). */
  size: Size;
  /** Its style, and where it came from (also in `faces.userData`); null when not told. */
  style: MeshStyle;
  source: MeshSource | null;
}

/**
 * Whether a style shows the checker (drawShape's `checker`): the bevelled
 * voxels and the wire frame; the flat style and the STL-like one paint a
 * piece in one colour.
 */
export function hasChecker(style: MeshStyle): boolean {
  return style === 'drawing' || style === 'wireframe';
}

/** The cell [x, y, z] of a cell index of a shape of this size. */
export function cellOf(index: number, size: Size): [number, number, number] {
  const [sx, sy] = size;
  const x = index % sx;
  const rest = Math.floor(index / sx);
  return [x, rest % sy, Math.floor(rest / sy)];
}

/** The geometry of a mesh frame in a style; `source` says where it came from. */
export function buildMesh(
  arrays: MeshArrays,
  size: Size,
  style: MeshStyle,
  source: MeshSource | null = null,
): BuiltMesh {
  const { positions, normals, indices, voxel, flags, colour } = arrays;
  const total = flags.length;

  // the triangles drawn: the wire-frame style keeps only its faces
  let count = 0;
  const keep = new Uint32Array(total);
  for (let t = 0; t < total; t++) {
    if (style !== 'wireframe' || ((flags[t] ?? 0) & FLAG.wireframe) !== 0) {
      keep[count++] = t;
    }
  }
  const triangle = keep.slice(0, count);

  const pos = new Float32Array(count * 9);
  const nor = new Float32Array(count * 9);
  const aFlags = new Float32Array(count * 3);
  const aColour = new Float32Array(count * 3);
  const aCell = new Float32Array(count * 9);
  const sx = size[0];
  const sy = size[1];
  for (let k = 0; k < count; k++) {
    const t = triangle[k] ?? 0;
    const f = flags[t] ?? 0;
    const c = colour[t] ?? 0;
    const cell = voxel[t] ?? 0;
    const cx = cell % sx;
    const rest = Math.floor(cell / sx);
    const cy = rest % sy;
    const cz = Math.floor(rest / sy);
    for (let j = 0; j < 3; j++) {
      const v = indices[3 * t + j] ?? 0;
      const o = 9 * k + 3 * j;
      pos[o] = positions[3 * v] ?? 0;
      pos[o + 1] = positions[3 * v + 1] ?? 0;
      pos[o + 2] = positions[3 * v + 2] ?? 0;
      nor[o] = normals[3 * v] ?? 0;
      nor[o + 1] = normals[3 * v + 1] ?? 0;
      nor[o + 2] = normals[3 * v + 2] ?? 0;
      aFlags[3 * k + j] = f;
      aColour[3 * k + j] = c;
      aCell[o] = cx;
      aCell[o + 1] = cy;
      aCell[o + 2] = cz;
    }
  }
  const faces = new BufferGeometry();
  faces.setAttribute('position', new BufferAttribute(pos, 3));
  faces.setAttribute('normal', new BufferAttribute(nor, 3));
  faces.setAttribute('aFlags', new BufferAttribute(aFlags, 1));
  faces.setAttribute('aColour', new BufferAttribute(aColour, 1));
  faces.setAttribute('aCell', new BufferAttribute(aCell, 3));
  faces.computeBoundingBox();
  faces.computeBoundingSphere();
  faces.userData.style = style;
  faces.userData.source = source;
  faces.userData.size = size;

  return {
    faces,
    triangle,
    marks: variableMarks(arrays, triangle),
    edges: style === 'flat' && arrays.edgeLines.length > 0 ? arrays.edgeLines : null,
    arrays,
    size,
    style,
    source,
  };
}

type V3 = [number, number, number];

/**
 * The corners of each polygon whose faces carry the variable mark, in
 * order. The engine emits a polygon as the view's fan from its second
 * corner (c1, c2, c3), (c1, c3, c4) ... (c1, c(n-1), c0), triangles of a
 * polygon one after another; the fan's apex and its far corners give the
 * polygon back in its cyclic order.
 */
export function markedPolygons(arrays: MeshArrays, drawn: Uint32Array): V3[][] {
  const { positions, indices, flags, polygon } = arrays;
  const corner = (t: number, j: number): V3 => {
    const v = indices[3 * t + j] ?? 0;
    return [positions[3 * v] ?? 0, positions[3 * v + 1] ?? 0, positions[3 * v + 2] ?? 0];
  };
  const out: V3[][] = [];
  let current = -1;
  let corners: V3[] = [];
  for (const t of drawn) {
    if (((flags[t] ?? 0) & FLAG.variableMark) === 0) {
      continue;
    }
    const p = polygon[t] ?? 0;
    if (p !== current) {
      if (corners.length >= 3) {
        out.push(corners);
      }
      current = p;
      corners = [corner(t, 0), corner(t, 1), corner(t, 2)];
    } else {
      corners.push(corner(t, 2));
    }
  }
  if (corners.length >= 3) {
    out.push(corners);
  }
  return out;
}

function sub(a: V3, b: V3): V3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

function dist(a: V3, b: V3): number {
  return Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

function unitNormal(p1: V3, p2: V3, p3: V3): V3 {
  const u = sub(p2, p1);
  const w = sub(p3, p1);
  const n: V3 = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
  const l = Math.hypot(n[0], n[1], n[2]);
  return l === 0 ? [0, 0, 0] : [n[0] / l, n[1] / l, n[2] / l];
}

// corner p2 of the triangle (p1, p2, p3) moved towards the incircle's centre
// so that the triangle's sides move SHRINK inwards, and MY along n
function shrinkCorner(p1: V3, p2: V3, p3: V3, n: V3, which: 0 | 1 | 2): V3 {
  const c = dist(p2, p1);
  const b = dist(p3, p1);
  const a = dist(p2, p3);
  const per = a + b + c;
  const centre: V3 = [0, 1, 2].map(
    (i) => ((p1[i] ?? 0) * a + (p2[i] ?? 0) * b + (p3[i] ?? 0) * c) / per,
  ) as V3;
  const s = per / 2;
  const r = Math.sqrt(((s - a) * (s - b) * (s - c)) / s);
  const q = (r - SHRINK) / r;
  const p = which === 0 ? p1 : which === 1 ? p2 : p3;
  return [0, 1, 2].map(
    (i) => (centre[i] ?? 0) + q * ((p[i] ?? 0) - (centre[i] ?? 0)) + MY * (n[i] ?? 0),
  ) as V3;
}

/** The mark of a triangle (drawShrinkTriangle): the triangle, its sides moved SHRINK inwards. */
export function shrinkTriangle(p1: V3, p2: V3, p3: V3): V3[] {
  const n = unitNormal(p1, p2, p3);
  return [
    shrinkCorner(p1, p2, p3, n, 0),
    shrinkCorner(p1, p2, p3, n, 1),
    shrinkCorner(p1, p2, p3, n, 2),
  ];
}

/**
 * The mark of a quadrilateral (drawShrinkQuadrilateral): each corner moved
 * as in the triangle of it and its two neighbours; two triangles.
 */
export function shrinkQuadrilateral(p1: V3, p2: V3, p3: V3, p4: V3): V3[] {
  const n = unitNormal(p1, p2, p3);
  const q2 = shrinkCorner(p1, p2, p3, n, 1);
  const q3 = shrinkCorner(p2, p3, p4, n, 1);
  const q4 = shrinkCorner(p3, p4, p1, n, 1);
  const q1 = shrinkCorner(p4, p1, p2, n, 1);
  return [q1, q2, q3, q3, q4, q1];
}

// the marks as triangles; the original drew polygons of more than four
// corners by their first four, and so does this
function variableMarks(arrays: MeshArrays, drawn: Uint32Array): BufferGeometry | null {
  const tris: V3[] = [];
  for (const poly of markedPolygons(arrays, drawn)) {
    const [a, b, c, d] = poly;
    if (a === undefined || b === undefined || c === undefined) {
      continue;
    }
    tris.push(...(d === undefined ? shrinkTriangle(a, b, c) : shrinkQuadrilateral(a, b, c, d)));
  }
  if (tris.length === 0) {
    return null;
  }
  const pos = new Float32Array(tris.length * 3);
  const nor = new Float32Array(tris.length * 3);
  for (let i = 0; i < tris.length; i += 3) {
    const corners = tris.slice(i, i + 3);
    const [a, b, c] = corners;
    if (a === undefined || b === undefined || c === undefined) {
      break;
    }
    const n = unitNormal(a, b, c);
    corners.forEach((p, j) => {
      pos.set(p, 3 * (i + j));
      nor.set(n, 3 * (i + j));
    });
  }
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(pos, 3));
  g.setAttribute('normal', new BufferAttribute(nor, 3));
  return g;
}
