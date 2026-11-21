// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a stacking puzzle's board is drawn in the Play workspace (decision
// 0042), apart from the board itself (stack-board.ts) so the unit tests
// run it without a DOM: what is drawn (the rods, the discs), the
// drawing's measures in a room, where rods, levels and discs are in it,
// and its SVG markup. The pegs stand on a base; a Panex column has the
// steps a disc of each size may rest on behind it and the bridge across
// the top, where a raised disc waits. The same markup, small, is the
// goal's picture beside the board.

import { esc } from '../shell/html.ts';
import { contrast, hex } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import type { Stacks } from './stack-game.ts';

/** A rod as the board draws it (stacking.playStart's). */
export interface StackRod {
  /** "rod 2", "the pocket column". */
  name: string;
  pocket: boolean;
  /** Where it stands along the board: rod r at r, the pocket at -1. */
  position: number;
  /** The discs it holds: a Panex column one more than its height. */
  capacity: number;
}

/** A disc as the board draws it. */
export interface StackDisc {
  size: number;
  colour: Rgb3;
  /** What is written on it; '' for nothing. */
  label: string;
}

/** What the board draws. */
export interface StackModel {
  rods: StackRod[];
  /** The rods are Panex columns (the pocket is a plain rod). */
  panex: boolean;
  discs: StackDisc[];
}

/** The measures of a drawing, in pixels. */
export interface StackLook {
  /** A cell across: a disc of size s is 2s + 1 of them wide. */
  u: number;
  /** A level: the height of a disc. */
  v: number;
  /** The cells a rod's column takes: the widest disc and a gap. */
  span: number;
  /** The columns: the rods, the pocket included. */
  cols: number;
  /** The levels of the tallest rod. */
  levels: number;
  /** The margin round the drawing, and the base's thickness. */
  pad: number;
  base: number;
  width: number;
  height: number;
}

/** How far above the tallest rod a moving disc cruises, in levels. */
export const LIFT = 0.55;
/** The levels kept free above the rods for it. */
const HEADROOM = 1.7;

const fmt = (v: number): string => String(Math.round(v * 100) / 100);

/** The largest disc size of a board (1 without discs). */
function largest(m: StackModel): number {
  return m.discs.reduce((a, d) => Math.max(a, d.size), 1);
}

/** The column a rod stands in, from the left: the pocket stands left of rod 1. */
export function rodColumn(m: StackModel, rod: number): number {
  const shift = m.rods.some((r) => r.pocket) ? 1 : 0;
  return (m.rods[rod]?.position ?? rod) + shift;
}

/**
 * The measures of a board drawn in a room of `w` by `h` pixels. A `small`
 * one (the goal's picture) has thin margins and no room kept above the
 * rods for a moving disc.
 */
export function stackLook(m: StackModel, w: number, h: number, small = false): StackLook {
  const span = 2 * largest(m) + 2;
  const cols = Math.max(1, m.rods.length);
  const levels = Math.max(1, ...m.rods.map((r) => r.capacity));
  const pad = small ? 3 : 12;
  const head = small ? 0.6 : HEADROOM;
  const u = Math.max(small ? 1 : 3, Math.min(small ? 12 : 44, (w - 2 * pad) / (cols * span)));
  // a disc no taller than its smallest size is wide, and no flatter than a sliver
  const tall = Math.min(small ? 16 : 46, 3 * u);
  const v = Math.max(small ? 3 : 9, Math.min(tall, (h - 2 * pad) / (levels + head + 0.45)));
  const base = Math.max(small ? 2 : 5, Math.round(v * 0.42));
  return {
    u,
    v,
    span,
    cols,
    levels,
    pad,
    base,
    width: Math.round(cols * span * u + 2 * pad),
    height: Math.round((levels + head) * v + base + 2 * pad),
  };
}

/** The x of a rod's middle. */
export function rodX(m: StackModel, look: StackLook, rod: number): number {
  return look.pad + (rodColumn(m, rod) + 0.5) * look.span * look.u;
}

/** The y of the top of level `z` (0 the lowest; fractions between). */
export function levelY(look: StackLook, z: number): number {
  return look.height - look.pad - look.base - (z + 1) * look.v;
}

/** The rod whose column a point `x` across the drawing is in. */
export function rodAt(m: StackModel, look: StackLook, x: number): number {
  let best = 0;
  let near = Infinity;
  m.rods.forEach((_, r) => {
    const d = Math.abs(rodX(m, look, r) - x);
    if (d < near) {
      near = d;
      best = r;
    }
  });
  return best;
}

// a disc's bar, its top left at the origin of its group, the group at `at` on `rod` at height `z`
function discSvg(
  d: StackDisc,
  i: number,
  look: StackLook,
  small: boolean,
  rod: number,
  z: number,
  at: readonly [number, number],
): string {
  const w = (2 * d.size + 1) * look.u;
  const gap = small ? 0.5 : 1;
  const h = look.v - 2 * gap;
  const r = Math.min(h / 2, small ? 3 : 9);
  let label = '';
  const size = Math.min(15, Math.floor(h * 0.62));
  if (!small && d.label !== '' && size >= 7 && w >= size * 0.62 * d.label.length + 6) {
    label = `<text class="sk-name" x="${fmt(w / 2)}" y="${fmt(gap + h / 2)}" font-size="${String(size)}" fill="${contrast(d.colour)}">${esc(d.label)}</text>`;
  }
  return `<g class="sk-disc" data-i="${String(i)}" data-rod="${String(rod)}" data-z="${String(z)}" style="transform:translate(${at[0].toFixed(2)}px,${at[1].toFixed(2)}px)"><rect class="sk-bar" x="${fmt(gap)}" y="${fmt(gap)}" width="${fmt(w - 2 * gap)}" height="${fmt(h)}" rx="${fmt(r)}" fill="${hex(d.colour)}"/>${small ? '' : `<rect class="sk-sheen" x="${fmt(gap)}" y="${fmt(gap)}" width="${fmt(w - 2 * gap)}" height="${fmt(h)}" rx="${fmt(r)}"/>`}${label}</g>`;
}

/** Where a disc's group goes: its bar's top left, for a disc of `size` on `rod` at height `z`. */
export function discAt(
  m: StackModel,
  look: StackLook,
  size: number,
  rod: number,
  z: number,
): [number, number] {
  return [rodX(m, look, rod) - ((2 * size + 1) * look.u) / 2, levelY(look, z)];
}

/**
 * The drawing of a board with its discs at `stacks`: the base, the pegs, a
 * Panex board's steps and bridge, the discs, and (not on a `small` one) a
 * strip over each rod's column that takes the pointer.
 */
export function stackSvg(m: StackModel, stacks: Stacks, look: StackLook, small = false): string {
  const { u, v, span } = look;
  const floor = look.height - look.pad - look.base;
  const pegW = Math.max(small ? 1.5 : 4, Math.min(12, u * 0.45));
  const m1 = largest(m);
  let back = '';
  let pegs = '';
  let glows = '';
  let strips = '';
  const xs = m.rods.map((_, r) => rodX(m, look, r));
  const columns = m.rods.filter((r) => !(m.panex && r.pocket));
  const panexXs = m.rods.flatMap((r, i) => (m.panex && !r.pocket ? [xs[i] ?? 0] : []));
  m.rods.forEach((rod, r) => {
    const x = xs[r] ?? 0;
    const column = m.panex && !rod.pocket;
    // a Panex column's top level is the bridge: its peg ends under it
    const high = column ? rod.capacity - 1 : rod.capacity;
    if (column) {
      // the steps: at each level the narrowest disc that may rest there
      for (let z = 0; z < high; z++) {
        const half = Math.min(m1, high - z);
        const w = (2 * half + 1) * u;
        back += `<rect class="sk-step" x="${fmt(x - w / 2)}" y="${fmt(levelY(look, z))}" width="${fmt(w)}" height="${fmt(v)}"/>`;
      }
    }
    const top = levelY(look, high - 1) - (column ? 0 : v * 0.45);
    pegs += `<rect class="sk-peg" x="${fmt(x - pegW / 2)}" y="${fmt(top)}" width="${fmt(pegW)}" height="${fmt(floor - top)}" rx="${fmt(column ? 0 : pegW / 2)}"/>`;
    if (!small) {
      // the glow that marks a rod a disc may go to, and the strip that takes the pointer
      glows += `<rect class="sk-glow" data-rod="${String(r)}" x="${fmt(x - (span * u) / 2 + 3)}" y="2" width="${fmt(span * u - 6)}" height="${fmt(floor + look.base - 2)}" rx="10"/>`;
      strips += `<rect class="sk-col" data-rod="${String(r)}" x="${fmt(x - (span * u) / 2)}" y="0" width="${fmt(span * u)}" height="${String(look.height)}"><title>${esc(rod.name.charAt(0).toUpperCase() + rod.name.slice(1))}</title></rect>`;
    }
  });
  if (panexXs.length > 0) {
    // the bridge: the level over the columns, where one raised disc waits
    const high = Math.max(...columns.map((r) => r.capacity)) - 1;
    const half = ((2 * m1 + 1) * u) / 2 + Math.min(u, 6);
    const x0 = Math.min(...panexXs) - half;
    const x1 = Math.max(...panexXs) + half;
    back =
      `<rect class="sk-bridge" x="${fmt(x0)}" y="${fmt(levelY(look, high))}" width="${fmt(x1 - x0)}" height="${fmt(v)}" rx="${fmt(Math.min(v / 2, 8))}"/>` +
      back;
  }
  const lo = Math.min(...xs) - (span * u) / 2 + u * 0.4;
  const hi = Math.max(...xs) + (span * u) / 2 - u * 0.4;
  const ground = `<rect class="sk-base" x="${fmt(lo)}" y="${fmt(floor)}" width="${fmt(hi - lo)}" height="${String(look.base)}" rx="${fmt(Math.min(look.base / 2, 5))}"/>`;
  let discs = '';
  stacks.forEach((rod, r) => {
    for (const d of rod) {
      const disc = m.discs[d.disc];
      if (disc !== undefined) {
        const at = discAt(m, look, disc.size, r, d.z);
        discs += discSvg(disc, d.disc, look, small, r, d.z, at);
      }
    }
  });
  // the sheen that rounds a disc: light along its top, dark along its foot
  const defs = small
    ? ''
    : '<defs><linearGradient id="sk-sheen" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".38"/><stop offset=".45" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".22"/></linearGradient></defs>';
  return `<svg class="sk-svg${small ? ' small' : ''}" width="${String(look.width)}" height="${String(look.height)}" viewBox="0 0 ${String(look.width)} ${String(look.height)}" data-u="${fmt(u)}" data-v="${fmt(v)}">${defs}<g class="sk-back">${glows}${back}${pegs}${ground}</g><g class="sk-discs">${discs}</g><g class="sk-cols">${strips}</g></svg>`;
}
