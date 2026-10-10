// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order in which the vector image paints its faces and lines (stage
// 7): back to front for an eye, by a BSP tree, as gl2ps sorted the
// original's vector export (gl2ps.c, GL2PS_BSP_SORT). A painter that sorts
// whole triangles by their centres (three.js's SVGRenderer) draws a large
// face over a small one in front of it, and the edge lines under the
// faces they border; the tree splits what lies across a plane, so every
// piece is painted after everything behind it. A line lying in a face's
// plane is painted just after that face (the flat style's edges lie on
// the faces). Plain numbers, DOM-free.

/** A point [x, y, z]. */
export type P3 = [number, number, number];

/** A convex polygon (three or more corners in order) with what it is painted with. */
export interface Face<T> {
  corners: P3[];
  paint: T;
}

/** A line with what it is painted with. */
export interface Segment<T> {
  a: P3;
  b: P3;
  paint: T;
}

/** One thing to paint, in painting order. */
export type Painted<F, S> = { face: Face<F> } | { segment: Segment<S> };

/** How close to a plane a point counts as on it. */
const EPS = 1e-5;

interface Node<F, S> {
  n: P3;
  d: number;
  faces: Face<F>[];
  segments: Segment<S>[];
  front: Node<F, S> | null;
  back: Node<F, S> | null;
}

const ORIGIN: P3 = [0, 0, 0];
const at = (c: readonly P3[], i: number): P3 => c[i % c.length] ?? ORIGIN;
const dot = (a: P3, b: P3): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const lerp = (a: P3, b: P3, t: number): P3 => [
  a[0] + t * (b[0] - a[0]),
  a[1] + t * (b[1] - a[1]),
  a[2] + t * (b[2] - a[2]),
];

/** The unit normal of a polygon (Newell's), or null when it has no area. */
export function polygonNormal(c: readonly P3[]): P3 | null {
  const n: P3 = [0, 0, 0];
  for (let i = 0; i < c.length; i++) {
    const p = at(c, i);
    const q = at(c, i + 1);
    n[0] += (p[1] - q[1]) * (p[2] + q[2]);
    n[1] += (p[2] - q[2]) * (p[0] + q[0]);
    n[2] += (p[0] - q[0]) * (p[1] + q[1]);
  }
  const l = Math.hypot(n[0], n[1], n[2]);
  return l < 1e-12 ? null : [n[0] / l, n[1] / l, n[2] / l];
}

// a polygon's parts in front of and behind a plane (Sutherland and Hodgman)
function splitPolygon(c: readonly P3[], n: P3, d: number): { front: P3[]; back: P3[] } {
  const front: P3[] = [];
  const back: P3[] = [];
  for (let i = 0; i < c.length; i++) {
    const p = at(c, i);
    const q = at(c, i + 1);
    const dp = dot(n, p) - d;
    const dq = dot(n, q) - d;
    if (dp >= -EPS) {
      front.push(p);
    }
    if (dp <= EPS) {
      back.push(p);
    }
    if ((dp > EPS && dq < -EPS) || (dp < -EPS && dq > EPS)) {
      const x = lerp(p, q, dp / (dp - dq));
      front.push(x);
      back.push(x);
    }
  }
  return { front, back };
}

function build<F, S>(faces: Face<F>[], segments: Segment<S>[]): Node<F, S> | null {
  let first: Face<F> | undefined;
  let n: P3 | null = null;
  while (n === null) {
    first = faces.shift();
    if (first === undefined) {
      break;
    }
    n = polygonNormal(first.corners);
  }
  if (first === undefined || n === null) {
    // no plane left: the lines lie in one convex region, in any order
    return segments.length === 0
      ? null
      : { n: [0, 0, 1], d: 0, faces: [], segments, front: null, back: null };
  }
  const d = dot(n, at(first.corners, 0));
  const node: Node<F, S> = { n, d, faces: [first], segments: [], front: null, back: null };
  const fronts: Face<F>[] = [];
  const backs: Face<F>[] = [];
  for (const f of faces) {
    let pos = false;
    let neg = false;
    for (const p of f.corners) {
      const s = dot(n, p) - d;
      pos ||= s > EPS;
      neg ||= s < -EPS;
    }
    if (pos && neg) {
      const { front, back } = splitPolygon(f.corners, n, d);
      if (front.length >= 3) {
        fronts.push({ corners: front, paint: f.paint });
      }
      if (back.length >= 3) {
        backs.push({ corners: back, paint: f.paint });
      }
    } else if (pos) {
      fronts.push(f);
    } else if (neg) {
      backs.push(f);
    } else {
      node.faces.push(f);
    }
  }
  const frontSegments: Segment<S>[] = [];
  const backSegments: Segment<S>[] = [];
  for (const s of segments) {
    const da = dot(n, s.a) - d;
    const db = dot(n, s.b) - d;
    const aFront = da > EPS;
    const aBack = da < -EPS;
    const bFront = db > EPS;
    const bBack = db < -EPS;
    if ((aFront && bBack) || (aBack && bFront)) {
      const x = lerp(s.a, s.b, da / (da - db));
      (aFront ? frontSegments : backSegments).push({ a: s.a, b: x, paint: s.paint });
      (bFront ? frontSegments : backSegments).push({ a: x, b: s.b, paint: s.paint });
    } else if (aFront || bFront) {
      frontSegments.push(s);
    } else if (aBack || bBack) {
      backSegments.push(s);
    } else {
      node.segments.push(s);
    }
  }
  node.front = build(fronts, frontSegments);
  node.back = build(backs, backSegments);
  return node;
}

/**
 * The faces and lines in the order to paint them for an eye: `eye` a point
 * (a perspective camera's position), or, with `direction`, an eye far away
 * in that direction (an orthographic camera's: towards the eye).
 */
export function paintingOrder<F, S>(
  faces: readonly Face<F>[],
  segments: readonly Segment<S>[],
  eye: P3,
  direction = false,
): Painted<F, S>[] {
  const out: Painted<F, S>[] = [];
  const root = build([...faces], [...segments]);
  // iterative, so a deep tree does not overflow the stack
  const stack: (Node<F, S> | { emit: Node<F, S> })[] = root === null ? [] : [root];
  while (stack.length > 0) {
    const top = stack.pop();
    if (top === undefined) {
      break;
    }
    if ('emit' in top) {
      for (const f of top.emit.faces) {
        out.push({ face: f });
      }
      for (const s of top.emit.segments) {
        out.push({ segment: s });
      }
      continue;
    }
    const side = direction ? dot(top.n, eye) : dot(top.n, eye) - top.d;
    // far side first, then this plane's own, then the near side (pushed in reverse)
    const near = side >= 0 ? top.front : top.back;
    const far = side >= 0 ? top.back : top.front;
    if (near !== null) {
      stack.push(near);
    }
    stack.push({ emit: top });
    if (far !== null) {
      stack.push(far);
    }
  }
  return out;
}
