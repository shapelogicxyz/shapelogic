// SPDX-License-Identifier: GPL-3.0-or-later
//
// The outline of a set of cells as one SVG path with rounded corners, for
// the Play board's pieces and its floor, apart from the page so the unit
// tests run it without a DOM. Each edge of the outline can be drawn a
// little inside the cells, by an amount that depends on the way it faces:
// a piece's top face sits further from its right and lower edges than
// from its left and upper ones, which leaves room for its sides.

/** A cell as the drawing has it: column x, row y, y down. */
export type Square = readonly [number, number];

/** A point of the drawing. */
export type Point = [number, number];

/**
 * The loops round a set of cells, in cell units: each loop's corners only
 * (no point midway along a side), clockwise on the screen (y down), so the
 * cells are on the right of each side. Two cells that touch only at a
 * corner are in two loops.
 */
export function loops(cells: readonly Square[]): Point[][] {
  const key = (x: number, y: number): string => `${String(x)},${String(y)}`;
  const has = new Set(cells.map((c) => key(c[0], c[1])));
  // every side with no cell beyond it, from its start, heading clockwise
  const out = new Map<string, { from: Point; d: Point; used: boolean }[]>();
  const add = (x: number, y: number, dx: number, dy: number): void => {
    const k = key(x, y);
    const list = out.get(k) ?? [];
    list.push({ from: [x, y], d: [dx, dy], used: false });
    out.set(k, list);
  };
  for (const [x, y] of cells) {
    if (!has.has(key(x, y - 1))) {
      add(x, y, 1, 0);
    }
    if (!has.has(key(x + 1, y))) {
      add(x + 1, y, 0, 1);
    }
    if (!has.has(key(x, y + 1))) {
      add(x + 1, y + 1, -1, 0);
    }
    if (!has.has(key(x - 1, y))) {
      add(x, y + 1, 0, -1);
    }
  }
  const result: Point[][] = [];
  for (const list of out.values()) {
    for (const first of list) {
      if (first.used) {
        continue;
      }
      const corners: Point[] = [];
      let side = first;
      let d: Point = [0, 0];
      for (;;) {
        side.used = true;
        if (side.d[0] !== d[0] || side.d[1] !== d[1]) {
          corners.push(side.from);
        }
        d = side.d;
        const end = key(side.from[0] + d[0], side.from[1] + d[1]);
        if (end === key(first.from[0], first.from[1])) {
          break;
        }
        // where two cells touch at a corner the sharpest right turn is
        // taken, which keeps to the cells this loop goes round
        const right: Point = [-d[1], d[0]];
        const next = (out.get(end) ?? [])
          .filter((s) => !s.used)
          .sort((a, b) => turn(a.d, right, d) - turn(b.d, right, d))[0];
        if (next === undefined) {
          break;
        }
        side = next;
      }
      // the first side may continue the last: no corner where the loop began
      const last = corners.at(-1);
      const head = corners[0];
      if (corners.length > 2 && last !== undefined && head !== undefined) {
        const lastD = sign(head[0] - last[0], head[1] - last[1]);
        if (lastD[0] === first.d[0] && lastD[1] === first.d[1]) {
          corners.shift();
        }
      }
      result.push(corners);
    }
  }
  return result;
}

// 0 for a right turn, 1 straight on, 2 a left turn
function turn(d: Point, right: Point, ahead: Point): number {
  if (d[0] === right[0] && d[1] === right[1]) {
    return 0;
  }
  return d[0] === ahead[0] && d[1] === ahead[1] ? 1 : 2;
}

function sign(x: number, y: number): Point {
  return [Math.sign(x), Math.sign(y)];
}

/** How the outline is drawn. */
export interface OutlineStyle {
  /** Pixels a cell. */
  scale: number;
  /** Where cell (0, 0)'s top left corner is drawn. */
  at: Point;
  /**
   * How far inside the cells a side is drawn, from the way the cells lie
   * from it ((1, 0): they are to its right, so it is a left side).
   */
  inset: (into: Point) => number;
  /** The corners' radius, in pixels (less where a side is too short for it). */
  radius: number;
}

/** The SVG path of the outline of a set of cells, each loop closed. */
export function roundedOutline(cells: readonly Square[], style: OutlineStyle): string {
  let path = '';
  for (const loop of loops(cells)) {
    path += roundedLoop(insetLoop(loop, style), style.radius);
  }
  return path;
}

// a loop's corners in pixels, each side moved in by its inset
function insetLoop(loop: readonly Point[], style: OutlineStyle): Point[] {
  const n = loop.length;
  return loop.map((v, i) => {
    const p = loop[(i + n - 1) % n] ?? v;
    const q = loop[(i + 1) % n] ?? v;
    // the cells lie to the right of each side: (-dy, dx)
    const a = sign(v[0] - p[0], v[1] - p[1]);
    const b = sign(q[0] - v[0], q[1] - v[1]);
    const na: Point = [-a[1], a[0]];
    const nb: Point = [-b[1], b[0]];
    const ia = style.inset(na);
    const ib = style.inset(nb);
    return [
      style.at[0] + v[0] * style.scale + ia * na[0] + ib * nb[0],
      style.at[1] + v[1] * style.scale + ia * na[1] + ib * nb[1],
    ];
  });
}

const fmt = (v: number): string => String(Math.round(v * 100) / 100);

/** A loop's corner rounded: where its arc begins and ends. */
interface Corner {
  a: Point;
  b: Point;
  r: number;
  /** It turns clockwise on the screen. */
  cw: boolean;
}

// each corner of a loop rounded by an arc of `radius` (less where a side is too short)
function corners(pts: readonly Point[], radius: number): Corner[] {
  const n = pts.length;
  return pts.map((v, i) => {
    const p = pts[(i + n - 1) % n] ?? v;
    const q = pts[(i + 1) % n] ?? v;
    const inLen = Math.hypot(v[0] - p[0], v[1] - p[1]);
    const outLen = Math.hypot(q[0] - v[0], q[1] - v[1]);
    const r = Math.max(0, Math.min(radius, inLen / 2, outLen / 2));
    const u: Point = [(v[0] - p[0]) / inLen, (v[1] - p[1]) / inLen];
    const w: Point = [(q[0] - v[0]) / outLen, (q[1] - v[1]) / outLen];
    return {
      a: [v[0] - u[0] * r, v[1] - u[1] * r],
      b: [v[0] + w[0] * r, v[1] + w[1] * r],
      r,
      // a clockwise turn on the screen is a positive cross product (y down)
      cw: u[0] * w[1] - u[1] * w[0] > 0,
    };
  });
}

const arc = (c: Corner, to: Point): string =>
  c.r > 0 ? `A${fmt(c.r)} ${fmt(c.r)} 0 0 ${c.cw ? '1' : '0'} ${fmt(to[0])} ${fmt(to[1])}` : '';

// a loop with each corner rounded by an arc
function roundedLoop(pts: readonly Point[], radius: number): string {
  if (pts.length < 3) {
    return '';
  }
  let d = '';
  corners(pts, radius).forEach((c, i) => {
    d += `${i === 0 ? 'M' : 'L'}${fmt(c.a[0])} ${fmt(c.a[1])}${arc(c, c.b)}`;
  });
  return `${d}Z`;
}
