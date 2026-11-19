// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arithmetic of a sliding piece's picture (decision 0038), apart from
// the page so the unit tests run it without a DOM: the map from the
// picture's pixels to the shape's cells (api ShapeImage's formula, the one
// the engine's shape_image.h keeps up to date as the piece turns) and the
// part of a picture Crop and Stretch show on a box of cells.

import type { ImageFit, ShapeImage } from '../../engine/api.generated.ts';

/** A decoded picture as the 3D views use it (piece-images.ts's PieceImage is one). */
export interface PictureSource {
  hash: string;
  width: number;
  height: number;
  /** An ImageBitmap (typed loosely: this file needs no DOM). */
  bitmap: object;
}

/** A shape's picture with its placement, decoded: what a 3D view draws. */
export interface PiecePicture {
  image: ShapeImage;
  picture: PictureSource;
}

/** An affine map [a, b, c, d, e, f]: (x, y) to (a x + c y + e, b x + d y + f), as SVG's matrix(). */
export type Affine = [number, number, number, number, number, number];

/**
 * The map from the picture's pixels to the shape's grid, in cell units, for
 * the picture `image` (its part `src` over its cell rectangle `box`,
 * turned): api ShapeImage's formula. The y of the grid points up.
 */
export function pictureToCells(image: ShapeImage): Affine {
  const [sx, sy, sw, sh] = image.src;
  const [bx, by, bw, bh] = image.box;
  const m = turnMatrix(image.turn);
  // (px, py) -> p = ((px - sx) / sw - 1/2, 1/2 - (py - sy) / sh) -> q = M p
  // -> (bx + (q.x + 1/2) bw, by + (q.y + 1/2) bh)
  const at = (px: number, py: number): [number, number] => {
    const ux = (px - sx) / sw - 0.5;
    const uy = 0.5 - (py - sy) / sh;
    const qx = m[0] * ux + m[1] * uy;
    const qy = m[2] * ux + m[3] * uy;
    return [bx + (qx + 0.5) * bw, by + (qy + 0.5) * bh];
  };
  const o = at(0, 0);
  const x = at(1, 0);
  const y = at(0, 1);
  return [x[0] - o[0], x[1] - o[1], y[0] - o[0], y[1] - o[1], o[0], o[1]];
}

/** The 2x2 matrix of a picture's turn, row major (shape_image.h's). */
export function turnMatrix(turn: number): [number, number, number, number] {
  let m: [number, number, number, number] = turn >= 4 ? [-1, 0, 0, 1] : [1, 0, 0, 1];
  for (let k = 0; k < (turn & 3); k++) {
    // a quarter turn counter-clockwise: [0 -1; 1 0] m
    m = [0 - m[2] + 0, 0 - m[3] + 0, m[0], m[1]]; // + 0: no -0
  }
  return m;
}

/** a after b: (a b)(p) = a(b(p)). */
export function compose(a: Affine, b: Affine): Affine {
  return [
    a[0] * b[0] + a[2] * b[1],
    a[1] * b[0] + a[3] * b[1],
    a[0] * b[2] + a[2] * b[3],
    a[1] * b[2] + a[3] * b[3],
    a[0] * b[4] + a[2] * b[5] + a[4],
    a[1] * b[4] + a[3] * b[5] + a[5],
  ];
}

/** The inverse of an affine map. */
export function invert(t: Affine): Affine {
  const det = t[0] * t[3] - t[1] * t[2];
  const a = t[3] / det;
  const b = -t[1] / det;
  const c = -t[2] / det;
  const d = t[0] / det;
  return [a, b, c, d, -(a * t[4] + c * t[5]), -(b * t[4] + d * t[5])];
}

/** The part of a w x h picture shown on a box of bw x bh cells. */
export function sourceRect(
  fit: ImageFit,
  w: number,
  h: number,
  bw: number,
  bh: number,
): [number, number, number, number] {
  if (fit === 'stretch') {
    return [0, 0, w, h];
  }
  const want = bw / bh;
  if (w / h > want) {
    const sw = Math.max(1, Math.min(w, Math.round(h * want)));
    return [Math.floor((w - sw) / 2), 0, sw, h];
  }
  const sh = Math.max(1, Math.min(h, Math.round(w / want)));
  return [0, Math.floor((h - sh) / 2), w, sh];
}

/** The cells' bounding box in x and y: [x, y, width, height]; null for none. */
export function cellBox(
  cells: readonly (readonly number[])[],
): [number, number, number, number] | null {
  if (cells.length === 0) {
    return null;
  }
  const xs = cells.map((c) => c[0] ?? 0);
  const ys = cells.map((c) => c[1] ?? 0);
  const x = Math.min(...xs);
  const y = Math.min(...ys);
  return [x, y, Math.max(...xs) - x + 1, Math.max(...ys) - y + 1];
}
