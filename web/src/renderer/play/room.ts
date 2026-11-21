// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a board of the Play workspace stands in its frame (decisions 0039
// and 0042): the room it may fill, and the margins that put its drawing
// there. A sliding puzzle's board (board.ts) and a stacking puzzle's
// (stack-board.ts) share the frame, one of them shown, with the buttons
// beside it.

/** The room a board has, in pixels, and the left margin that puts it there. */
export interface BoardRoom {
  w: number;
  h: number;
  left: number;
}

/**
 * The room `root` has in `frame`. Its distance to the card's outer edge
 * below (what the playback bar takes) is kept, and the board is as far
 * from the card's edge above and on the left, and the buttons beside it as
 * far from its edge on the right; `left` is the margin that puts it there.
 */
export function boardRoom(frame: HTMLElement, root: HTMLElement): BoardRoom {
  const card = frame.closest<HTMLElement>('.card') ?? frame;
  const style = getComputedStyle(frame);
  const px = (v: string): number => Number.parseFloat(v) || 0;
  const f = frame.getBoundingClientRect();
  const c = card.getBoundingClientRect();
  const floor = f.bottom - px(style.paddingBottom);
  const d = Math.max(0, c.bottom - floor);
  const top = Math.max(f.top + px(style.paddingTop), c.top + d);
  const left = Math.max(f.left + px(style.paddingLeft), c.left + d);
  let right = Math.min(f.right - px(style.paddingRight), c.right - d);
  for (const e of frame.children) {
    // the other board, out of sight, takes no room
    if (e !== root && e instanceof HTMLElement && !e.hidden) {
      right -= e.offsetWidth + px(style.columnGap);
    }
  }
  return { w: right - left, h: floor - top, left: left - f.left - px(style.paddingLeft) };
}

/**
 * Puts `root`, which hugs its drawing, at its distance from the card's
 * left and at the foot of its room (in the middle of it when the width is
 * what limits the board).
 */
export function insetBoard(frame: HTMLElement, root: HTMLElement): void {
  const svg = root.firstElementChild;
  if (!(svg instanceof SVGSVGElement)) {
    root.style.marginLeft = '';
    root.style.marginBottom = '';
    return;
  }
  const r = boardRoom(frame, root);
  const bh = svg.height.baseVal.value;
  root.style.marginLeft = `${String(Math.floor(Math.max(0, r.left)))}px`;
  root.style.marginBottom = `${String(Math.floor(Math.max(0, (r.h - bh) / 2)))}px`;
}
