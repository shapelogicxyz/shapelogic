// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the pieces scene puts things (from docs/mockup/index.html:
// `puzLayout`): the result at the top left, the selected piece beside it,
// and every copy of every piece in rows of at most five below, the copies
// above a part's minimum marked optional (drawn faded). Positions are the
// mockup's units, in which a piece is about four wide; the scene scales
// them to its largest piece; `label` is what it writes over or under
// each. The copies follow the problem's parts (its
// piece numbering), where the mockup went through the shapes in order.

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

/** The scene's items (the mockup's puzLayout); `hidden` counts the copies left out. */
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
      pos: [7.5, 0],
    });
  }
  const list: Omit<SceneItem, 'role' | 'pos'>[] = [];
  let all = 0;
  for (const p of parts) {
    for (let k = 0; k < p.max; k++) {
      all++;
      if (list.length < MAX_COPIES) {
        list.push({ shape: p.shape, copy: k + 1, total: p.max, optional: k >= p.min });
      }
    }
  }
  const cols = Math.min(5, Math.max(1, list.length));
  list.forEach((q, i) => {
    items.push({
      role: 'piece',
      ...q,
      pos: [3.75 + ((i % cols) - (cols - 1) / 2) * 5.9, -7.2 - Math.floor(i / cols) * 5.6],
    });
  });
  return { items, hidden: all - list.length };
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
