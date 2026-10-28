// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the pieces scene puts things (from docs/mockup/index.html:
// `puzLayout`): the result at the top, the selected piece beside it, and
// every copy of every piece in rows below (`arrange`: five a row, or as
// many as the view's shape is best shown with), centred under them; the
// copies above a part's minimum marked optional (drawn faded). Positions
// are the mockup's units, in which a piece is about four wide (the result
// RESULT_SIZE times that); the scene scales them to its largest piece;
// `label` is what it writes over or under each. The copies follow the
// problem's parts (its piece numbering), where the mockup went through
// the shapes in order.

import type { Part } from '../../engine/api.generated.ts';

/** What an item of the scene is. */
export type SceneRole = 'result' | 'selected' | 'piece';

/** One drawn shape: the result, the selected piece, or a copy of a piece. */
export interface SceneItem {
  role: SceneRole;
  shape: string;
  /** The copy's number, from 1 (1 for the result and the selected piece). */
  copy: number;
  /** The copies of its part (1 for the result and the selected piece). */
  total: number;
  /** A copy above its part's minimum. */
  optional: boolean;
  /** The mockup's position: x right, y up. */
  pos: [number, number];
}

/** At most this many copies are drawn (a part may ask for 65535). */
export const MAX_COPIES = 100;

/** How much larger than a piece's place the result's is. */
export const RESULT_SIZE = 1.6;

/** The distance between the copies in a row, and between the rows. */
const PITCH_X = 5.2;
const PITCH_Y = 6.6;
/**
 * The first row of copies, under the result: its half height (2.2 times
 * RESULT_SIZE), a copy's (2.2) and a gap of 2.
 */
const FIRST_ROW = -7.7;

/** The scene's items (the mockup's puzLayout), in rows of five; `hidden` counts the copies left out. */
export function puzLayout(
  result: string | null,
  selected: string | null,
  parts: readonly Part[],
): { items: SceneItem[]; hidden: number } {
  const items: SceneItem[] = [];
  if (result !== null) {
    items.push({ role: 'result', shape: result, copy: 1, total: 1, optional: false, pos: [0, 0] });
  }
  if (selected !== null && selected !== result) {
    items.push({
      role: 'selected',
      shape: selected,
      copy: 1,
      total: 1,
      optional: false,
      pos: [0, 0],
    });
  }
  let all = 0;
  let drawn = 0;
  for (const p of parts) {
    for (let k = 0; k < p.max; k++) {
      all++;
      if (drawn < MAX_COPIES) {
        drawn++;
        items.push({
          role: 'piece',
          shape: p.shape,
          copy: k + 1,
          total: p.max,
          optional: k >= p.min,
          pos: [0, 0],
        });
      }
    }
  }
  arrange(items, 5);
  return { items, hidden: all - drawn };
}

/**
 * Places the items for `cols` copies a row: the result and the selected
 * piece side by side at the top (the result alone in the middle), the
 * copies in rows under them, both centred on x = 0.
 */
export function arrange(items: SceneItem[], cols: number): void {
  const top = items.filter((i) => i.role !== 'piece');
  const both = top.length > 1;
  for (const it of top) {
    it.pos = [both ? (it.role === 'result' ? -3.75 : 3.75) : 0, 0];
  }
  const copies = items.filter((i) => i.role === 'piece');
  const c = Math.min(Math.max(1, cols), Math.max(1, copies.length));
  const y0 = top.length === 0 ? 0 : FIRST_ROW;
  copies.forEach((it, i) => {
    // a short last row is centred too
    const inRow = Math.min(c, copies.length - Math.floor(i / c) * c);
    it.pos = [((i % c) - (inRow - 1) / 2) * PITCH_X, y0 - Math.floor(i / c) * PITCH_Y];
  });
}

/** An item's label, as the mockup wrote it over or under its box; `name` is the shape's label. */
export function label(item: SceneItem, name: string): string {
  if (item.role === 'result') {
    return `RESULT · ${name}`;
  }
  if (item.role === 'selected') {
    return `SELECTED · ${name}`;
  }
  // (the mockup's "(optional)" on a line of its own: the labels of a row of copies would overlap)
  return `${name}${item.total > 1 ? `  ·  ${String(item.copy)}/${String(item.total)}` : ''}${item.optional ? '\n(optional)' : ''}`;
}
