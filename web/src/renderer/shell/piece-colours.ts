// SPDX-License-Identifier: GPL-3.0-or-later
//
// The default colours of the shapes, by shape number, and the varied
// colours of a piece's copies. Ported from src/gui/piececolor.cpp of the
// original (pieceColorR/G/B, pieceColorRi/Gi/Bi, the jitter of the
// multi-pieces, darkPieceColor, lightPieceColor, contrastPieceColor). A
// shape with a colour of its own (ShapeSummary.colour) uses that instead.

/** A colour as three numbers from 0 to 1. */
export type Rgb3 = readonly [number, number, number];

// the table of the first 18 colours
const R = [0, 0, 1, 0, 1, 1, 0, 0, 0.6, 0, 0.6, 0.6, 0, 0.6, 0, 0.6, 1, 1];
const G = [0, 1, 0, 1, 1, 0, 0, 0.6, 0, 0.6, 0.6, 0, 1, 1, 0.6, 0, 0.6, 0];
const B = [1, 0, 0, 1, 0, 1, 0.6, 0, 0, 0.6, 0, 0.6, 0.6, 0, 1, 1, 0, 0.6];

// the changes for the copies of a piece (multi-pieces)
const JR = [
  0, -0.3, 0.3, -0.3, 0.3, -0.3, 0.3, -0.3, 0.3, 0.3, -0.3, -0.3, 0.3, 0.3, -0.3, -0.3, 0.3, 0, 0,
  0, 0, 0, 0, 0, 0, 0.3, -0.3, -0.4, 0.4, -0.4, 0.4, -0.4, 0.4, -0.4, 0.4, 0.4, -0.4, -0.4, 0.4,
  0.4, -0.4, -0.4, 0.4, 0, 0, 0, 0, 0, 0, 0, 0, 0.4, -0.4,
];
const JG = [
  0, -0.3, 0.3, 0.3, -0.3, -0.3, 0.3, 0.3, -0.3, 0.3, -0.3, 0.3, -0.3, 0, 0, 0, 0, 0.3, -0.3, -0.3,
  0.3, 0, 0, 0.3, -0.3, 0, 0, -0.4, 0.4, 0.4, -0.4, -0.4, 0.4, 0.4, -0.4, 0.4, -0.4, 0.4, -0.4, 0,
  0, 0, 0, 0.4, -0.4, -0.4, 0.4, 0, 0, 0.4, -0.4, 0, 0,
];
const JB = [
  0, -0.3, 0.3, 0.3, -0.3, 0.3, -0.3, -0.3, 0.3, 0, 0, 0, 0, 0.3, -0.3, 0.3, -0.3, 0.3, -0.3, 0.3,
  -0.3, 0.3, -0.3, 0, 0, 0, 0, -0.4, 0.4, 0.4, -0.4, 0.4, -0.4, -0.4, 0.4, 0, 0, 0, 0, 0.4, -0.4,
  0.4, -0.4, 0.4, -0.4, 0.4, -0.4, 0.4, -0.4, 0, 0, 0, 0,
];

/** The original computed in single precision; so does this. */
const f32 = Math.fround;

/** The default colour of shape number `x` (from 0), as the original's pieceColorR/G/B. */
export function pieceColour(x: number): Rgb3 {
  const r = R[x];
  const g = G[x];
  const b = B[x];
  if (r !== undefined && g !== undefined && b !== undefined) {
    return [r, g, b];
  }
  return [
    f32((1 + Math.sin(0.7 * x)) / 2),
    f32((1 + Math.sin(1.3 * x + 1.5)) / 2),
    f32((1 + Math.sin(3.5 * x + 2.3)) / 2),
  ];
}

function at(a: readonly number[], i: number): number {
  return a[i] ?? 0;
}

// the `sub`-th jitter that keeps every channel inside 0..1 (the table
// exhausted: none), as getJitter
function jitter(c: Rgb3, sub: number): number {
  let j = 0;
  let left = sub;
  while (j < JR.length) {
    const inside = (v: number, d: number): boolean => {
      const s = f32(f32(v) + f32(d));
      return s >= 0 && s <= 1;
    };
    if (!inside(c[0], at(JR, j)) || !inside(c[1], at(JG, j)) || !inside(c[2], at(JB, j))) {
      j++;
      continue;
    }
    if (left === 0) {
      break;
    }
    left--;
    j++;
  }
  return j === JR.length ? 0 : j;
}

const ramp = (v: number): number => f32(0.5 + 0.5 * Math.abs(1 - 2 * v));

// val + jitter * k * ramp(val), in the original's float and double steps
const vary = (v: number, d: number, k: number): number => f32(v) + f32(d) * k * ramp(f32(v));

/** The colour of copy `sub` of shape `x` (copy 0 is the shape's own), as pieceColorR/G/B(x, sub). */
export function pieceSubColour(x: number, sub: number, base: Rgb3 = pieceColour(x)): Rgb3 {
  const j = jitter(base, sub);
  return [
    f32(vary(base[0], at(JR, j), 0.5)),
    f32(vary(base[1], at(JG, j), 0.4)),
    f32(vary(base[2], at(JB, j), 0.7)),
  ];
}

/**
 * The byte channels of copy `sub` of shape `x`, as pieceColorRi/Gi/Bi(x,
 * sub): truncated from the unrounded value (the chips' colours).
 */
export function pieceSubBytes(
  x: number,
  sub: number,
  base: Rgb3 = pieceColour(x),
): [number, number, number] {
  const j = jitter(base, sub);
  return [
    Math.trunc(vary(base[0], at(JR, j), 0.5) * 255),
    Math.trunc(vary(base[1], at(JG, j), 0.4) * 255),
    Math.trunc(vary(base[2], at(JB, j), 0.7) * 255),
  ];
}

/** The darker colour faces are drawn in (darkPieceColor). */
export function dark(c: Rgb3): Rgb3 {
  return [c[0] * 0.9, c[1] * 0.9, c[2] * 0.9];
}

/** The lighter colour of the checker pattern (lightPieceColor). */
export function light(c: Rgb3): Rgb3 {
  return [1 - 0.9 * (1 - c[0]), 1 - 0.9 * (1 - c[1]), 1 - 0.9 * (1 - c[2])];
}

/** Byte channels, as pieceColorRi/Gi/Bi (truncated, not rounded). */
export function bytes(c: Rgb3): [number, number, number] {
  return [Math.trunc(c[0] * 255), Math.trunc(c[1] * 255), Math.trunc(c[2] * 255)];
}

/** `#rrggbb` of a colour. */
export function hex(c: Rgb3): string {
  return (
    '#' +
    bytes(c)
      .map((v) => v.toString(16).padStart(2, '0'))
      .join('')
  );
}

/** A colour from `#rrggbb`. */
export function fromHex(h: string): Rgb3 {
  const n = Number.parseInt(h.slice(1), 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

/** White or black, whichever reads on the colour (contrastPieceColor). */
export function contrast(c: Rgb3): '#ffffff' | '#000000' {
  const [r, g, b] = bytes(c);
  return 3 * r + 6 * g + b < 1275 ? '#ffffff' : '#000000';
}

/** A shape's colour: its own (`#rrggbb`), else the default of its number. */
export function shapeColour(own: string | null, index: number): Rgb3 {
  return own === null ? pieceColour(index) : fromHex(own);
}

// A sliding puzzle's start/goal shape (a tray) has no colour of its own:
// its cells show the pieces' colours. Its own look, from the original's
// slidingcolors.h: black in the shapes list, a dark grey in the 3D view,
// a dark chequer in the 2D editor.

/** The tray's swatch in the shapes list (slidingColors::LIST). */
export const TRAY_LIST: Rgb3 = [0, 0, 0];
/** The tray's grey in the 3D view (slidingColors::VIEW_GREY). */
export const TRAY_VIEW: Rgb3 = [0.12, 0.12, 0.12];
/** The 2D editor's tray tiles (slidingColors::EDIT_LIGHT and EDIT_DARK). */
export const TRAY_EDIT = { light: '#323232', dark: '#1e1e1e' } as const;

/** What a shape's colour depends on (a ShapeSummary). */
export interface ShapeLike {
  colour: string | null;
  /** A sliding start/goal shape's number; null (or absent) for any other shape. */
  startGoal?: number | null;
}

/** A shape's chip colour: a tray's black, else its own or its number's. */
export function chipColour(s: ShapeLike | undefined, index: number): Rgb3 {
  return isTray(s) ? TRAY_LIST : shapeColour(s?.colour ?? null, index);
}

/** A shape's colour in the 3D view: a tray's grey, else its own or its number's. */
export function viewColour(s: ShapeLike | undefined, index: number): Rgb3 {
  return isTray(s) ? TRAY_VIEW : shapeColour(s?.colour ?? null, index);
}

/** Whether a shape is a sliding start/goal shape. */
export function isTray(s: ShapeLike | null | undefined): boolean {
  return s?.startGoal !== undefined && s.startGoal !== null;
}
