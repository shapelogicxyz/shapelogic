// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arithmetic of a sliding piece's picture (decision 0038), apart from
// the page so the unit tests run it without a DOM: the map from the
// picture's pixels to the shape's cells (api ShapeImage's formula, the one
// the engine's shape_image.h keeps up to date as the piece turns) and the
// part of a picture Crop and Stretch show on a box of cells, and the
// rectangle the Manual fit sizes and moves by hand.

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

/**
 * Where the picture lies in the dialog's Manual fit: x, y, width and height
 * in cells, from the top left corner of the shape's box [0, 0, bw, bh], y
 * down. It may be smaller than the box or lie partly off it (decision
 * 0047), but always overlaps it.
 */
export type PictureRect = [number, number, number, number];

/** What a drag in the Manual fit holds: a corner or side of the picture, or its middle. */
export type PictureHandle = 'move' | 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w' | 'nw';

/** How far a Manual drag may go, in cells: the picture's smallest side, and how much of it stays over the box. */
export interface DragLimits {
  minSide: number;
  overlap: number;
}

/** The picture's rectangle for the part `src` of a w x h picture shown on a bw x bh box. */
export function rectFromSource(
  src: readonly number[],
  w: number,
  h: number,
  bw: number,
  bh: number,
): PictureRect {
  const [sx = 0, sy = 0, sw = w, sh = h] = src;
  const rw = (bw * w) / sw;
  const rh = (bh * h) / sh;
  return [(-sx / w) * rw + 0, (-sy / h) * rh + 0, rw, rh]; // + 0: no -0
}

/**
 * The part of a w x h picture lying on the bw x bh box, for the picture's
 * rectangle `r`: whole pixels, at least one wide, reaching past the
 * picture's edges where the picture is smaller than the box or lies off
 * part of it, and overlapping the picture by a pixel at least.
 */
export function sourceFromRect(
  r: PictureRect,
  w: number,
  h: number,
  bw: number,
  bh: number,
): [number, number, number, number] {
  const [rx, ry, rw, rh] = r;
  const within = (v: number, lo: number, hi: number): number => Math.min(Math.max(v, lo), hi);
  const sw = Math.max(1, Math.round((bw / rw) * w));
  const sh = Math.max(1, Math.round((bh / rh) * h));
  return [
    within(Math.round((-rx / rw) * w), 1 - sw, w - 1) + 0,
    within(Math.round((-ry / rh) * h), 1 - sh, h - 1) + 0,
    sw,
    sh,
  ];
}

/**
 * The picture's rectangle `r` after its `handle` is dragged by (dx, dy)
 * cells: the middle moves it, a side moves that side, a corner scales it
 * from the opposite corner keeping its proportions (both sides move freely
 * when `free`). It may grow past the bw x bh box or shrink inside it, down
 * to `limits.minSide`, and move until only `limits.overlap` of it is over
 * the box in x or y; it stays inside `bounds` ([x, y, width, height] in
 * cells: where the handles stay in reach) as far as it can, and keeping
 * its overlap with the box comes first.
 */
export function dragRect(
  r: PictureRect,
  handle: PictureHandle,
  dx: number,
  dy: number,
  bw: number,
  bh: number,
  bounds: readonly [number, number, number, number],
  free = false,
  limits: DragLimits = { minSide: 0.05 * Math.min(bw, bh), overlap: 0.02 * Math.min(bw, bh) },
): PictureRect {
  const [x0, y0, w0, h0] = r;
  const [left, top, bwide, bhigh] = bounds;
  const right = left + bwide;
  const bottom = top + bhigh;
  const { minSide: min, overlap: m } = limits;
  // into [lo, hi]; lo when the range is empty
  const keep = (v: number, lo: number, hi: number): number => Math.max(lo, Math.min(v, hi));
  if (handle === 'move') {
    // inside the bounds, then over the box by `m` at least (which wins)
    const x = keep(Math.min(keep(x0 + dx, left, right - w0), bw - m), m - w0, bw - m);
    const y = keep(Math.min(keep(y0 + dy, top, bottom - h0), bh - m), m - h0, bh - m);
    return [x, y, w0, h0];
  }
  const corner = handle.length === 2;
  if (corner && !free) {
    // scale about the opposite corner by the drag's share along the diagonal
    const sx = handle.includes('e') ? 1 : -1;
    const sy = handle.includes('s') ? 1 : -1;
    const ax = sx > 0 ? x0 : x0 + w0;
    const ay = sy > 0 ? y0 : y0 + h0;
    const px = ax + sx * w0 + dx;
    const py = ay + sy * h0 + dy;
    let s = ((px - ax) * sx * w0 + (py - ay) * sy * h0) / (w0 * w0 + h0 * h0);
    // no smaller than `min`, and still over the box by `m`
    const lo = Math.max(
      min / w0,
      min / h0,
      (sx > 0 ? m - ax : ax - (bw - m)) / w0,
      (sy > 0 ? m - ay : ay - (bh - m)) / h0,
    );
    const hi = Math.min(
      sx > 0 ? (right - ax) / w0 : (ax - left) / w0,
      sy > 0 ? (bottom - ay) / h0 : (ay - top) / h0,
    );
    s = Math.max(Math.min(s, hi), lo);
    const w = s * w0;
    const h = s * h0;
    return [sx > 0 ? ax : ax - w, sy > 0 ? ay : ay - h, w, h];
  }
  let x1 = x0;
  let y1 = y0;
  let x2 = x0 + w0;
  let y2 = y0 + h0;
  if (handle.includes('w')) {
    x1 = keep(Math.min(Math.max(x1 + dx, left), x2 - min), -Infinity, Math.min(x2 - min, bw - m));
  }
  if (handle.includes('e')) {
    x2 = keep(Math.min(x2 + dx, right), Math.max(x1 + min, m), Infinity);
  }
  if (handle.includes('n')) {
    y1 = keep(Math.min(Math.max(y1 + dy, top), y2 - min), -Infinity, Math.min(y2 - min, bh - m));
  }
  if (handle.includes('s')) {
    y2 = keep(Math.min(y2 + dy, bottom), Math.max(y1 + min, m), Infinity);
  }
  return [x1, y1, x2 - x1, y2 - y1];
}

/**
 * The drag (dx, dy) of `handle` on the picture's rectangle `r`, nudged so
 * that the edges it moves land on the grid's lines (whole cells) when they
 * come within `tol` cells of one: what Manual's Snap to grid does before
 * dragRect applies the drag and its limits. The middle snaps whichever of
 * the picture's sides in x, and in y, is nearer a line; a side snaps
 * itself; a corner its two sides (`free`), or, keeping the proportions,
 * the one of them nearer a line, the other following.
 */
export function snapDrag(
  r: PictureRect,
  handle: PictureHandle,
  dx: number,
  dy: number,
  tol: number,
  free = false,
): [number, number] {
  const [x0, y0, w0, h0] = r;
  // the shift that takes the nearest of `edges` onto a line; 0 when none is within tol
  const nudge = (edges: number[]): number => {
    let best = 0;
    let bestOff = Infinity;
    for (const e of edges) {
      const off = Math.round(e) - e;
      if (Math.abs(off) <= tol && Math.abs(off) < Math.abs(bestOff)) {
        best = off;
        bestOff = off;
      }
    }
    return best;
  };
  if (handle === 'move') {
    return [dx + nudge([x0 + dx, x0 + w0 + dx]), dy + nudge([y0 + dy, y0 + h0 + dy])];
  }
  const corner = handle.length === 2;
  if (corner && !free) {
    // dragRect's scale about the opposite corner, and the drag along the
    // diagonal that gives the scale `s` there
    const sx = handle.includes('e') ? 1 : -1;
    const sy = handle.includes('s') ? 1 : -1;
    const s = 1 + (dx * sx * w0 + dy * sy * h0) / (w0 * w0 + h0 * h0);
    const ax = sx > 0 ? x0 : x0 + w0;
    const ay = sy > 0 ? y0 : y0 + h0;
    const px = ax + sx * w0 * s;
    const py = ay + sy * h0 * s;
    const ox = Math.round(px) - px;
    const oy = Math.round(py) - py;
    const useX = Math.abs(ox) <= tol && Math.abs(ox) <= Math.abs(oy);
    const useY = !useX && Math.abs(oy) <= tol;
    if (!useX && !useY) {
      return [dx, dy];
    }
    const t = useX ? s + (sx * ox) / w0 : s + (sy * oy) / h0;
    return [sx * w0 * (t - 1), sy * h0 * (t - 1)];
  }
  const x = handle.includes('w')
    ? nudge([x0 + dx])
    : handle.includes('e')
      ? nudge([x0 + w0 + dx])
      : 0;
  const y = handle.includes('n')
    ? nudge([y0 + dy])
    : handle.includes('s')
      ? nudge([y0 + h0 + dy])
      : 0;
  return [dx + x, dy + y];
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

/** The Apply image dialog's preview size, in CSS pixels. */
export interface PreviewSize {
  w: number;
  h: number;
}

/** The smallest preview. */
export const PREVIEW_MIN: PreviewSize = { w: 300, h: 200 };

/**
 * A first guess at the room the rest of the Apply image dialog takes in
 * the window's height, in CSS pixels; once the dialog is open it measures
 * the room it really takes and passes that.
 */
const DIALOG_CHROME = 420;

/**
 * The preview's size in a window of `win` CSS pixels, for a box of
 * `ratio` (its width over its height): as tall as the window leaves room
 * for beside the rest of the dialog (`chrome` pixels high), and as wide as the box needs (at least half again its height), at
 * most 55% of the window's width; never under PREVIEW_MIN. A 13 × 9
 * tray in a window of 1400 × 900 is drawn about 620 × 440 pixels.
 */
export function previewSize(win: PreviewSize, ratio: number, chrome = DIALOG_CHROME): PreviewSize {
  const clamp = (v: number, lo: number, hi: number): number => Math.max(lo, Math.min(hi, v));
  const maxW = clamp(Math.round(win.w * 0.55), PREVIEW_MIN.w, 1000);
  const h = clamp(Math.round(win.h - chrome), PREVIEW_MIN.h, 700);
  const w = clamp(Math.round(h * Math.max(1.5, ratio * 1.15)), PREVIEW_MIN.w, maxW);
  return { w, h };
}
