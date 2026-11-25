// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle (decision 0045): a brick puzzle with a tray
// (Snapshot.tray), whose first shape is the board the tray gives. What the
// front end says and draws of it: the words for a tray, and the picture of
// the tray in its own frame (the straight squares, the walls, the turned
// cells whole or cut), drawn from what the engine sends (the board's cells
// and the tray's corners in the board's grid). DOM-free, for the unit
// tests. A tray is width by height squares: a square, or any rectangle.

import { LIMITS } from '../../engine/api.generated.ts';
import type { ShapeSummary, Snapshot, Tray, TraySettings } from '../../engine/api.generated.ts';

/** Whether the puzzle is a Tilted Square puzzle. */
export function isTilted(snap: Snapshot | null | undefined): boolean {
  return snap?.tray != null;
}

/** Whether a shape is a Tilted Square puzzle's board. */
export function isTiltBoard(s: Pick<ShapeSummary, 'board'> | null | undefined): boolean {
  return s?.board === true;
}

/** The settings a new tilted puzzle starts with in File ▸ New: 8 by 8, 1 down 2 over, strict. */
export const NEW_TRAY: Readonly<
  Required<Pick<TraySettings, 'size' | 'over' | 'down' | 'allowance'>>
> = { size: 8, over: 2, down: 1, allowance: 0 };

/** The allowance a switch turned on gives (the research page's 0.05 of a cell). */
export const ALLOWANCE_ON = 0.05;
/** The largest allowance (the engine's: types.json TraySettings). */
export const ALLOWANCE_MAX = LIMITS.tray.allowance;
/** The largest tray side. */
export const TRAY_MAX = LIMITS.tray.side;
/** The largest slope number (over, and so down). */
export const SLOPE_MAX = LIMITS.tray.slope;
/** The farthest a custom placement shifts the tray, in cells, either way. */
export const SHIFT_MAX = LIMITS.tray.shift;

/** The ids of a tilted puzzle's pieces that do not fit its board (ShapeSummary.fits). */
export function unfitIds(shapes: readonly Pick<ShapeSummary, 'id' | 'fits'>[]): Set<string> {
  return new Set(shapes.filter((s) => !s.fits).map((s) => s.id));
}

/**
 * What a tray change says of the pieces it left off the board's whole
 * cells (those that fitted before it): null when there are none.
 */
export function unfitNote(
  before: ReadonlySet<string>,
  shapes: readonly Pick<ShapeSummary, 'id' | 'label' | 'fits'>[],
): string | null {
  const now = shapes.filter((s) => !s.fits && !before.has(s.id)).map((s) => s.label);
  if (now.length === 0) {
    return null;
  }
  const names =
    now.length === 1 ? (now[0] ?? '') : `${now.slice(0, -1).join(', ')} and ${now.at(-1) ?? ''}`;
  return `${names} no longer ${now.length === 1 ? 'fits' : 'fit'} the board: no place has every cell on a whole cell`;
}

/** A tray's size in words, its width first: "8 by 6". */
export function sizeText(width: number, height: number): string {
  return `${String(width)} by ${String(height)}`;
}

/** The slope in words: "1 down, 2 over", or "straight". */
export function slopeText(down: number, over: number): string {
  return down === 0 ? 'straight' : `${String(down)} down, ${String(over)} over`;
}

/** An angle: "26.57°". */
export function degreesText(degrees: number): string {
  return `${degrees.toFixed(2)}°`;
}

/**
 * What the tray is and gives, a line each: "Tray: 8 by 6", "Whole cells:
 * 45", "Tilt angle: 26.57°", and "Walls: 4" when it has walls.
 */
export function trayFacts(
  t: Pick<Tray, 'width' | 'height' | 'whole' | 'degrees' | 'walls'>,
): string[] {
  const lines = [
    `Tray: ${sizeText(t.width, t.height)}`,
    `Whole cells: ${String(t.whole)}`,
    `Tilt angle: ${degreesText(t.degrees)}`,
  ];
  if (t.walls.length > 0) {
    lines.push(`Walls: ${String(t.walls.length)}`);
  }
  return lines;
}

/**
 * An allowance typed in a field: a number from 0 to the largest; null for
 * anything else (the field shows the tray's again).
 */
export function parseAllowance(text: string): number | null {
  const v = Number(text.trim().replace(',', '.'));
  return text.trim() === '' || !Number.isFinite(v) || v < 0 || v > ALLOWANCE_MAX ? null : v;
}

/** A point [x, y]. */
export type Pt = readonly [number, number];

/**
 * The map from the board's grid (where the engine draws the board's cells,
 * y up) to the tray's own frame (its straight squares, y up): the tray's
 * corners say where the tray lies in the board's grid, and the grid is
 * turned in it.
 */
export function boardToTray(t: Pick<Tray, 'width' | 'height' | 'corners'>): (p: Pt) => Pt {
  const [c0, c1, , c3] = t.corners;
  // the tray's sides are width and height long in the grid, so these are unit vectors
  const ex = [(c1[0] - c0[0]) / t.width, (c1[1] - c0[1]) / t.width] as const;
  const ey = [(c3[0] - c0[0]) / t.height, (c3[1] - c0[1]) / t.height] as const;
  return (p) => {
    const dx = p[0] - c0[0];
    const dy = p[1] - c0[1];
    return [dx * ex[0] + dy * ex[1], dx * ey[0] + dy * ey[1]];
  };
}

/** What a board cell is: w whole, c cut, o outside the tray. */
export type CellState = 'w' | 'c' | 'o';

/** The state of board cell (x, y) (y up from the box's bottom row). */
export function cellState(t: Pick<Tray, 'box' | 'cells'>, x: number, y: number): CellState {
  const [w, h] = t.box;
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return 'o';
  }
  const c = t.cells[y * w + x];
  return c === 'w' || c === 'c' ? c : 'o';
}

/**
 * How the tray picture shows the board (the board's views). Both draw the
 * turned cells with the tray's straight squares over them: Tray puts the
 * squares first (bright lines, the cells muted), Cells the turned cells
 * (vivid, the squares faint).
 */
export type TrayView = 'tray' | 'cells';

/** What trayPicture draws. */
export interface PictureOptions {
  /** The tray's longer side in pixels (without the margin). */
  px: number;
  view: TrayView;
  /** Room around the tray, in pixels. */
  margin?: number;
  /** An id for the clip path, unique in the page. */
  id: string;
  /** The squares are hit targets (data-sq="x,y"), as in the board's views. */
  editable?: boolean;
}

/**
 * The tray in its own frame as SVG: the turned cells (whole cells in a
 * checker of two classes as the drawings that started the mode, cut cells
 * gray), the walls, the straight squares over them, the outline. The view
 * says which of the two grids stands out (the svg's class, tp-tray or
 * tp-cells); the classes are the stylesheet's (tp-*).
 */
export function trayPicture(t: Tray, o: PictureOptions): string {
  const m = o.margin ?? 2;
  // a square of the tray, in pixels
  const k = o.px / Math.max(t.width, t.height);
  const X = (u: number): string => (m + u * k).toFixed(2);
  const Y = (v: number): string => (m + (t.height - v) * k).toFixed(2);
  const wide = (m * 2 + t.width * k).toFixed(0);
  const high = (m * 2 + t.height * k).toFixed(0);
  // the tray's rectangle: the floor, the clip and the outline
  const rect = `x="${X(0)}" y="${Y(t.height)}" width="${(t.width * k).toFixed(2)}" height="${(t.height * k).toFixed(2)}"`;
  const walls = new Set(t.walls.map(([x, y]) => `${String(x)},${String(y)}`));
  const out: string[] = [
    `<svg class="tpic tp-${o.view}" viewBox="0 0 ${wide} ${high}" width="${wide}" height="${high}" xmlns="http://www.w3.org/2000/svg" data-size="${String(t.width)}x${String(t.height)}">`,
    `<defs><clipPath id="${o.id}"><rect ${rect}/></clipPath></defs>`,
    `<rect class="tp-floor" ${rect}/>`,
  ];
  const squares = (cls: string, all: boolean): void => {
    for (let y = 0; y < t.height; y++) {
      for (let x = 0; x < t.width; x++) {
        const key = `${String(x)},${String(y)}`;
        const wall = walls.has(key);
        if (!all && !wall) {
          continue;
        }
        // only the squares over everything take the pointer
        const hit = o.editable === true && all ? ` data-sq="${key}"` : '';
        out.push(
          `<rect class="${cls}${wall ? ' wall' : ''}"${hit} x="${X(x)}" y="${Y(y + 1)}" width="${k.toFixed(2)}" height="${k.toFixed(2)}"/>`,
        );
      }
    }
  };
  {
    // the turned cells, clipped to the tray
    const toTray = boardToTray(t);
    const [w, h] = t.box;
    out.push(`<g clip-path="url(#${o.id})">`);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const s = cellState(t, x, y);
        if (s === 'o') {
          continue;
        }
        const pts = (
          [
            [x, y],
            [x + 1, y],
            [x + 1, y + 1],
            [x, y + 1],
          ] as const
        )
          .map((p) => {
            const [u, v] = toTray(p);
            return `${X(u)},${Y(v)}`;
          })
          .join(' ');
        const cls = s === 'w' ? `tp-whole ${(x + y) % 2 === 0 ? 'a' : 'b'}` : 'tp-cut';
        out.push(`<polygon class="${cls}" points="${pts}"/>`);
      }
    }
    out.push('</g>');
    // the walls over the cells, then the straight squares over both
    squares('tp-wall', false);
    squares('tp-sq', true);
  }
  out.push(`<rect class="tp-edge" ${rect}/>`, '</svg>');
  return out.join('');
}

/** The tray square a data-sq value names, or null. */
export function squareOf(value: string | undefined): [number, number] | null {
  const m = /^(\d+),(\d+)$/.exec(value ?? '');
  return m === null ? null : [Number(m[1]), Number(m[2])];
}
