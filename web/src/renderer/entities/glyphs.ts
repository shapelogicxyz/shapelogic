// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's pictures: the axis frame with its pink, green and cyan bars
// and gold cubes, the pencils and brush of the legacy icons. From
// docs/mockup/index.html: the glyph library `G` (flip, nudge, rotate,
// mirror, span, plane, tool, surf, fill, prune, center, origin, mul, div)
// with `gsv`, `frame`, `gbox`, `mline`, `arcArrow`, `PENCIL`, `BRUSH`,
// `CUBEC`, `SL`, taken over as they are.

import type { Tool } from '../shell/cursors.ts';
import type { Axis } from './editor-state.ts';
import type { Plane } from '../view3d/overlays.ts';

const CUBEC: Record<Axis, string> = { x: '#e0245e', y: '#2fb463', z: '#1aa7d6' };
const SL: Record<Axis, [[number, number], [number, number]]> = {
  x: [
    [6, 20],
    [17, 20],
  ],
  y: [
    [8, 21],
    [8, 9],
  ],
  z: [
    [6, 21],
    [13, 14],
  ],
};

const gsv = (inner: string, sz: number): string =>
  `<svg class="gx" width="${sz}" height="${sz}" viewBox="0 0 32 32" fill="none" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${inner}</svg>`;

const FRAME =
  '<path d="M4 28V3" stroke="var(--ay)" stroke-width="1.7"/><path d="M4 28H29" stroke="var(--ax)" stroke-width="1.7"/><path d="M4 28L15 17" stroke="var(--az)" stroke-width="1.7"/>';

function gbox(x: number, y: number, w: number, h: number, e: number, c: string | null): string {
  const F = `M${x} ${y}h${w}v${h}h-${w}z`;
  const T = `M${x} ${y}l${e} -${e}h${w}l-${e} ${e}z`;
  const S = `M${x + w} ${y}l${e} -${e}v${h}l-${e} ${e}z`;
  if (c === null) {
    return `<g stroke="currentColor" stroke-opacity=".6" stroke-width="1.1" stroke-dasharray="2 2"><path d="${F}"/><path d="${T}"/><path d="${S}"/></g>`;
  }
  return `<path d="${F}" fill="${c}"/><path d="${T}" fill="${c}"/><path d="${T}" fill="#fff" fill-opacity=".38"/><path d="${S}" fill="${c}"/><path d="${S}" fill="#000" fill-opacity=".28"/><path d="${F}" stroke="#000" stroke-opacity=".35" stroke-width=".8"/>`;
}

const mline = (a: Axis): string =>
  `<path d="${a === 'x' ? 'M16.5 9V27' : a === 'y' ? 'M5 18.5H28' : 'M8 16L18 26'}" stroke="currentColor" stroke-opacity=".7" stroke-width="1.3" stroke-dasharray="2 2"/>`;

// The part of a Transform glyph that travels to where the button sends it:
// styles.css moves it on hover, so the button previews its own movement.
const moving = (from: [number, number], to: [number, number], inner: string): string =>
  `<g class="mv" style="--dx:${to[0] - from[0]}px;--dy:${to[1] - from[1]}px">${inner}</g>`;

const PENCIL =
  '<path d="M3 29l2-8L23 3l6 6L11 27z" fill="#f5b942" stroke="#7a4a00" stroke-width="1.6"/><path d="M3 29l2-8 6 6z" fill="#f6dfb2" stroke="#7a4a00" stroke-width="1.2"/>';
const BRUSH =
  '<path d="M27 3l2 2-11 11-2-2z" fill="#8791a1" stroke="#404a5c" stroke-width="1.2"/><path d="M16 14l2 2c-1 5-5 9-13 9 3-2 3-5 4-8z" fill="#3b6cff" stroke="#1b3fa6" stroke-width="1.2"/>';

// an arc with its head: sg > 0 turns clockwise on the screen
function arcArrow(cx: number, cy: number, r: number, sg: number, col: string): string {
  const R = (d: number): number => (d * Math.PI) / 180;
  const pt = (t: number): [number, number] => [cx + r * Math.cos(R(t)), cy - r * Math.sin(R(t))];
  const a0 = sg > 0 ? 200 : -20;
  const a1 = sg > 0 ? -20 : 200;
  const p0 = pt(a0);
  const p1 = pt(a1);
  const t = sg > 0 ? [Math.sin(R(a1)), Math.cos(R(a1))] : [-Math.sin(R(a1)), -Math.cos(R(a1))];
  const [tx, ty] = [t[0] ?? 0, t[1] ?? 0];
  const n = [-ty, tx];
  const [nx, ny] = [n[0] ?? 0, n[1] ?? 0];
  const f = (v: number): string => v.toFixed(1);
  const tip = [p1[0] + tx * 3, p1[1] + ty * 3];
  const b1 = [p1[0] - tx * 2 + nx * 3, p1[1] - ty * 2 + ny * 3];
  const b2 = [p1[0] - tx * 2 - nx * 3, p1[1] - ty * 2 - ny * 3];
  return `<path d="M${f(p0[0])} ${f(p0[1])}A${r} ${r} 0 1 ${sg > 0 ? 1 : 0} ${f(p1[0])} ${f(p1[1])}" stroke="${col}" stroke-width="1.9"/><path d="M${f(tip[0] ?? 0)} ${f(tip[1] ?? 0)}L${f(b1[0] ?? 0)} ${f(b1[1] ?? 0)}L${f(b2[0] ?? 0)} ${f(b2[1] ?? 0)}z" fill="${col}" stroke="none"/>`;
}

const TOOL: Record<Tool, string> = {
  fixed: `<rect x="3" y="3" width="17" height="17" rx="2" fill="#ff3b3b"/><g transform="translate(9 9) scale(.7)">${PENCIL}</g>`,
  variable: `<rect x="4.5" y="4.5" width="14" height="14" rx="2" stroke="#8be000" stroke-width="3"/><g transform="translate(9 9) scale(.7)">${PENCIL}</g>`,
  erase:
    '<path d="M5 21l10-12 10 9-9 9h-7z" fill="#ff9ec4" stroke="#b04f7c" stroke-width="1.4"/><path d="M12 13l9 8" stroke="#b04f7c" stroke-width="1.2"/>',
  paint: `<rect x="3" y="3" width="17" height="17" rx="2" fill="#ffc400"/><g transform="translate(9 9) scale(.7)">${BRUSH}</g>`,
};

/** A surface operation of the Repair section. */
export type SurfOp = 'fixed' | 'variable' | 'clear';

/** The glyphs (the mockup's G). */
export const G = {
  flip(a: Axis, v: number, sz = 30): string {
    const s = SL[a];
    const [so, gh] = v !== 0 ? [s[1], s[0]] : [s[0], s[1]];
    return gsv(
      FRAME +
        mline(a) +
        gbox(gh[0], gh[1], 7, 7, 3, null) +
        moving(so, gh, gbox(so[0], so[1], 7, 7, 3, CUBEC[a])),
      sz,
    );
  },
  nudge(a: Axis, sg: number, sz = 30): string {
    const s = SL[a];
    const [so, gh] = sg > 0 ? [s[0], s[1]] : [s[1], s[0]];
    return gsv(
      FRAME +
        gbox(gh[0], gh[1], 7, 7, 3, null) +
        moving(so, gh, gbox(so[0], so[1], 7, 7, 3, CUBEC[a])),
      sz,
    );
  },
  rotate(a: Axis, sg: number, sz = 30): string {
    const c = CUBEC[a];
    const bar =
      a === 'x'
        ? gbox(6, 19, 16, 6, 3, c)
        : a === 'y'
          ? gbox(12, 9, 6, 17, 3, c)
          : gbox(8, 22, 6, 6, 10, c);
    return gsv(
      FRAME +
        bar +
        `<g class="spin" style="--rot:${sg > 0 ? 75 : -75}deg">${arcArrow(17, 17, 10, sg, 'currentColor')}</g>`,
      sz,
    );
  },
  mirror(a: Axis, sz = 30): string {
    const s = SL[a];
    return gsv(
      FRAME +
        mline(a) +
        gbox(s[0][0], s[0][1], 7, 7, 3, CUBEC[a]) +
        gbox(s[1][0], s[1][1], 7, 7, 3, CUBEC[a]),
      sz,
    );
  },
  span(a: Axis, sz = 30): string {
    return gsv(
      FRAME +
        (a === 'x'
          ? gbox(6, 20, 21, 7, 3, CUBEC[a])
          : a === 'y'
            ? gbox(9, 6, 7, 21, 3, CUBEC[a])
            : gbox(6, 23, 7, 5, 11, CUBEC[a])),
      sz,
    );
  },
  plane(p: Plane, sz = 30): string {
    const d = { XY: 'M4 28H22V8H4Z', XZ: 'M4 28H22L29 21H11Z', YZ: 'M4 28L12 20V4L4 12Z' }[p];
    return gsv(
      `<path d="${d}" fill="var(--accent)" fill-opacity=".32" stroke="var(--accent)" stroke-width="1.3"/>` +
        FRAME,
      sz,
    );
  },
  tool(id: Tool, sz = 30): string {
    return gsv(TOOL[id], sz);
  },
  /** The box selection (added: the mockup had the selection bar, not its tool). */
  box(sz = 30): string {
    return gsv(
      '<rect x="4" y="6" width="22" height="20" rx="1.5" stroke="currentColor" stroke-width="1.8" stroke-dasharray="3 2.5"/><rect x="9" y="11" width="6" height="6" fill="#ffc400"/><rect x="15" y="11" width="6" height="6" fill="#ff3b3b"/><rect x="9" y="17" width="6" height="6" fill="#ff3b3b"/>',
      sz,
    );
  },
  /** The tool strip's Select: a dotted square, the pointer at its bottom right. */
  select(sz = 30): string {
    return gsv(
      '<rect x="3" y="3" width="20" height="20" rx="1.5" stroke="currentColor" stroke-width="1.8" stroke-dasharray="3 2.5"/><path d="M17 16l11 4.6-4.6 1.6-1.6 4.6z" fill="currentColor" stroke="var(--panel2)" stroke-width="1.2"/>',
      sz,
    );
  },
  surf(op: SurfOp, part: 'inner' | 'outer', sz = 30): string {
    let c = '';
    const ring = (i: number, j: number): boolean => i === 0 || j === 0 || i === 3 || j === 3;
    for (let j = 0; j < 4; j++) {
      for (let i = 0; i < 4; i++) {
        const on = part === 'outer' ? ring(i, j) : !ring(i, j);
        const x = 2 + i * 7;
        const y = 2 + j * 7;
        if (on) {
          c +=
            op === 'variable'
              ? `<rect x="${x + 0.8}" y="${y + 0.8}" width="4.6" height="4.6" stroke="#8be000" stroke-width="1.6"/>`
              : `<rect x="${x}" y="${y}" width="6" height="6" fill="${op === 'fixed' ? '#ff3b3b' : '#ffc400'}"/>`;
        } else {
          c += `<rect x="${x}" y="${y}" width="6" height="6" stroke="currentColor" stroke-opacity=".3" stroke-dasharray="1.5 1.5" stroke-width=".8"/>`;
        }
      }
    }
    return gsv(
      c + `<g transform="translate(14 14) scale(.56)">${op === 'clear' ? BRUSH : PENCIL}</g>`,
      sz,
    );
  },
  fill(sz = 30): string {
    let c = '';
    for (let j = 0; j < 3; j++) {
      for (let i = 0; i < 3; i++) {
        const x = 3 + i * 9;
        const y = 3 + j * 9;
        c +=
          i === 1 && j === 1
            ? `<rect x="${x + 1}" y="${y + 1}" width="7" height="7" fill="#ffc400" stroke="currentColor" stroke-width="1" stroke-dasharray="2 1.5"/>`
            : `<rect x="${x}" y="${y}" width="8" height="8" fill="#ff3b3b"/>`;
      }
    }
    return gsv(c, sz);
  },
  prune(sz = 30): string {
    return gsv(
      '<rect x="3" y="3" width="26" height="26" rx="2" stroke="currentColor" stroke-opacity=".5" stroke-dasharray="3 2"/><rect x="11" y="11" width="10" height="10" fill="#ffc400"/><path d="M13 6l3 3 3-3M13 26l3-3 3 3M6 13l3 3-3 3M26 13l-3 3 3 3" stroke="#2f6df6" stroke-width="1.8"/>',
      sz,
    );
  },
  center(sz = 30): string {
    return gsv(
      '<rect x="3" y="3" width="26" height="26" rx="2" stroke="currentColor" stroke-opacity=".5"/><path d="M16 3V29M3 16H29" stroke="#2f6df6" stroke-width="1.2" stroke-dasharray="2 2"/><rect x="11" y="11" width="10" height="10" fill="#ffc400"/>',
      sz,
    );
  },
  origin(sz = 30): string {
    return gsv(
      FRAME +
        gbox(6, 21, 7, 7, 3, '#ffc400') +
        '<circle cx="4" cy="28" r="2.6" fill="var(--accent)" stroke="none"/>',
      sz,
    );
  },
  mul(n: number, sz = 30): string {
    const cs = n === 2 ? 5 : 3;
    const tot = n * cs + (n - 1);
    const x0 = 30 - tot;
    const y0 = 16 - tot / 2;
    let c =
      '<rect x="2" y="12" width="8" height="8" fill="#ffc400"/><path d="M11 16h6M14.5 13l3 3-3 3" stroke="currentColor" stroke-width="1.5"/>';
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        c += `<rect x="${x0 + i * (cs + 1)}" y="${y0 + j * (cs + 1)}" width="${cs}" height="${cs}" fill="#ffc400"/>`;
      }
    }
    return gsv(c, sz);
  },
  div(n: number, sz = 30): string {
    const cs = n === 2 ? 5 : 3;
    const tot = n * cs + (n - 1);
    const y0 = 16 - tot / 2;
    let c =
      '<rect x="23" y="13" width="7" height="7" fill="#ffc400"/><path d="M13 16h7M17.5 13l3 3-3 3" stroke="currentColor" stroke-width="1.5"/>';
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        c += `<rect x="${2 + i * (cs + 1)}" y="${y0 + j * (cs + 1)}" width="${cs}" height="${cs}" fill="#ffc400"/>`;
      }
    }
    return gsv(c, sz);
  },
  /** A grid of cells drawn over by a few (the original's grid scale; added). */
  gridScale(sz = 30): string {
    let c = '';
    for (let j = 0; j < 4; j++) {
      for (let i = 0; i < 4; i++) {
        const edge = i === 0 || j === 0 || i === 3 || j === 3;
        c += `<rect x="${3 + i * 6.5}" y="${3 + j * 6.5}" width="5.5" height="5.5" ${edge ? 'fill="#ffc400"' : 'stroke="currentColor" stroke-opacity=".35" stroke-width=".8"'}/>`;
      }
    }
    return gsv(c, sz);
  },
};
