// SPDX-License-Identifier: GPL-3.0-or-later
//
// The border of a sliding puzzle's tray (decision 0046), as the views
// share it: the tray's body round its floor (a rim round the board's
// rectangle and the cells that are no floor), in the corners, the
// material, the color and the picture the board's Tray border section
// chose. What is shown, in which color, and where the border's picture
// lies; each view draws it in its own way (the Play board's drawing,
// view3d/tray-border.ts for the 3D views). DOM-free.

import type { ShapeImage, TrayBorder } from '../../engine/api.generated.ts';
import { fromHex } from './piece-colors.ts';
import type { Rgb3 } from './piece-colors.ts';

/** A thin rim's width round the board's rectangle, in cells (the Play board's). */
export const BORDER_RIM = 0.28;

/** The thickest border the section offers, in voxels (the engine's kMaxBorderThickness). */
export const MAX_BORDER_THICKNESS = 10;

/**
 * The rim's width in cells: BORDER_RIM for a thin border (thickness 0, or
 * none), else its thickness in voxels.
 */
export function borderRim(b: { thickness: number } | null | undefined): number {
  const t = b?.thickness ?? 0;
  return t > 0 ? t : BORDER_RIM;
}

/** The wood material's color while the border has none of its own: a light brown. */
export const WOOD_COLOR: Rgb3 = fromHex('#c9a26b');

/** The names of the materials, as the section lists them. */
export const BORDER_MATERIALS: readonly [TrayBorder['material'], string][] = [
  ['smooth', 'Smooth'],
  ['textured', 'Textured'],
  ['wood', 'Wood grain'],
];

/** The border a shape shows: a board's, while it is turned on; null for none. */
export function shownBorder(
  shape: { border: TrayBorder | null } | null | undefined,
): TrayBorder | null {
  const b = shape?.border ?? null;
  return b?.enabled === true ? b : null;
}

/**
 * The border's color: its own, else the wood's for the wood material,
 * else the board's (`board`: the color the view draws the board in).
 */
export function borderColor(
  b: { color: string | Rgb3 | null; material: TrayBorder['material'] },
  board: Rgb3,
): Rgb3 {
  if (b.color !== null) {
    return typeof b.color === 'string' ? fromHex(b.color) : b.color;
  }
  return b.material === 'wood' ? WOOD_COLOR : board;
}

/** The tray's outer rectangle in the board's cells: [x, y, width, height], the rim included. */
export function trayBox(
  size: readonly number[],
  rim = BORDER_RIM,
): [number, number, number, number] {
  return [-rim, -rim, (size[0] ?? 1) + 2 * rim, (size[1] ?? 1) + 2 * rim];
}

/**
 * The border's picture as it lies now: over the tray's outer rectangle of
 * a board of `size` (the file's box is the board's when it was applied;
 * only its part shown and its turn count).
 */
export function borderImage(
  image: ShapeImage,
  size: readonly number[],
  rim = BORDER_RIM,
): ShapeImage {
  return { ...image, box: trayBox(size, rim) };
}

/** The cells in x and y that are floor on any layer, as a test (`cells`: [x, y, z] each). */
export function floorTest(cells: Iterable<readonly number[]>): (x: number, y: number) => boolean {
  const open = new Set<string>();
  for (const c of cells) {
    open.add(`${String(c[0])},${String(c[1])}`);
  }
  return (x, y) => open.has(`${String(x)},${String(y)}`);
}
