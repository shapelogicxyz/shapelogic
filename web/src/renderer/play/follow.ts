// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a dragged piece follows the pointer (decision 0039), apart from the
// page so the unit tests run it without a DOM. The engine gave every place
// one move can take the piece to (sliding.playReach: shifts joined by
// single steps to where it is); the drag walks them a step at a time
// toward the pointer, so the piece slides along what is in its way and
// never jumps over it, however fast the pointer went; between stops it is
// drawn part of the way to the next, so it moves with the pointer from the
// first pixel and settles on the nearest stop when let go. And the points
// of a move's route for the playback.

/** Whether the piece can be at a shift (x, y) from where the drag began. */
export type Has = (x: number, y: number) => boolean;

/**
 * Where the piece gets to from the shift `from` toward `target`, both in
 * cells from where the drag began: single steps along the axis with the
 * further to go, else the other, while there is a stop that way. It never
 * steps away from the target, so a piece is led round a corner by the
 * pointer going round it.
 */
export function follow(
  from: readonly [number, number],
  target: readonly [number, number],
  has: Has,
): [number, number] {
  let [x, y] = from;
  // every step is one cell nearer the target: the distance bounds the walk
  for (;;) {
    const dx = target[0] - x;
    const dy = target[1] - y;
    if (dx === 0 && dy === 0) {
      break;
    }
    const sx = Math.sign(dx);
    const sy = Math.sign(dy);
    const xOk = sx !== 0 && has(x + sx, y);
    const yOk = sy !== 0 && has(x, y + sy);
    if (xOk && (Math.abs(dx) >= Math.abs(dy) || !yOk)) {
      x += sx;
    } else if (yOk) {
      y += sy;
    } else {
      break;
    }
  }
  return [x, y];
}

/**
 * How far past the stop `at` the pieces are drawn toward the pointer at
 * `pointer` (both in cells from where the drag began, the pointer not in
 * whole ones): along one axis, up to half a cell, and only toward a stop
 * next to it, so a piece moves with the pointer however little it goes
 * and never into what is in its way. Let go, the piece settles on `at`,
 * the stop nearest the pointer.
 */
export function slide(
  at: readonly [number, number],
  pointer: readonly [number, number],
  has: Has,
): [number, number] {
  const half = (v: number): number => Math.max(-0.5, Math.min(0.5, v));
  const rx = half(pointer[0] - at[0]);
  const ry = half(pointer[1] - at[1]);
  const xOk = rx !== 0 && has(at[0] + Math.sign(rx), at[1]);
  const yOk = ry !== 0 && has(at[0], at[1] + Math.sign(ry));
  if (xOk && (!yOk || Math.abs(rx) >= Math.abs(ry))) {
    return [rx, 0];
  }
  return yOk ? [0, ry] : [0, 0];
}

/** Below this part of a cell the pointer has not gone anywhere yet. */
const STILL = 0.02;

/**
 * Which way the pointer at `pointer` (in cells, not whole ones) has gone
 * from where the drag began, as a target for groupFor: the nearest cell,
 * or one step along the axis it went further on while that is still the
 * cell it began in; null while it has hardly moved.
 */
export function aim(pointer: readonly [number, number]): [number, number] | null {
  const x = Math.round(pointer[0]);
  const y = Math.round(pointer[1]);
  if (x !== 0 || y !== 0) {
    return [x, y];
  }
  const [px, py] = pointer;
  if (Math.max(Math.abs(px), Math.abs(py)) < STILL) {
    return null;
  }
  return Math.abs(px) >= Math.abs(py) ? [Math.sign(px), 0] : [0, Math.sign(py)];
}

/** The stops of a group as a test: the place the drag began at, and each shift in its layer. */
export function stopsIn(stops: readonly { by: readonly number[] }[]): Has {
  const set = new Set<string>(['0,0']);
  for (const s of stops) {
    if ((s.by[2] ?? 0) === 0) {
      set.add(`${String(s.by[0] ?? 0)},${String(s.by[1] ?? 0)}`);
    }
  }
  return (x, y) => set.has(`${String(x)},${String(y)}`);
}

/**
 * Which group a drag moves, by its first step toward `target`: the first
 * in `order` that can take that step; -1 when none can.
 */
export function groupFor(
  groups: readonly Has[],
  order: readonly number[],
  target: readonly [number, number],
): number {
  for (const i of order) {
    const has = groups[i];
    if (has === undefined) {
      continue;
    }
    const to = follow([0, 0], target, has);
    if (to[0] !== 0 || to[1] !== 0) {
      return i;
    }
  }
  return -1;
}

/** The length of a route (its corner points, as shifts), in cells. */
export function routeLength(route: readonly (readonly number[])[]): number {
  let total = 0;
  for (let i = 1; i < route.length; i++) {
    const a = route[i - 1] ?? [];
    const b = route[i] ?? [];
    total +=
      Math.abs((b[0] ?? 0) - (a[0] ?? 0)) +
      Math.abs((b[1] ?? 0) - (a[1] ?? 0)) +
      Math.abs((b[2] ?? 0) - (a[2] ?? 0));
  }
  return total;
}

/** The shift a part `t` (0 to 1) of the way along a route, at an even pace. */
export function alongRoute(
  route: readonly (readonly number[])[],
  t: number,
): [number, number, number] {
  const first = route[0] ?? [0, 0, 0];
  const last = route.at(-1) ?? first;
  const at = (p: readonly number[]): [number, number, number] => [p[0] ?? 0, p[1] ?? 0, p[2] ?? 0];
  const total = routeLength(route);
  if (total === 0 || t <= 0) {
    return at(t <= 0 ? first : last);
  }
  let left = Math.min(1, t) * total;
  for (let i = 1; i < route.length; i++) {
    const a = at(route[i - 1] ?? first);
    const b = at(route[i] ?? last);
    const len = Math.abs(b[0] - a[0]) + Math.abs(b[1] - a[1]) + Math.abs(b[2] - a[2]);
    if (left <= len && len > 0) {
      const f = left / len;
      return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
    }
    left -= len;
  }
  return at(last);
}
