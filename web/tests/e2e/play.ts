// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the e2e tests of the Play workspace share (decisions 0039 and
// 0042): opening the tab, dragging a piece over a sliding puzzle's board
// or a disc over a stacking puzzle's, and reading the game shown
// (`#playStage`'s data attributes: the moves made, the place in them,
// solved or not).

import { expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/** The Play workspace's stage. */
export const STAGE = '#playStage';

/**
 * Opens the Play tab and waits for the game and for the board to rest: the
 * cards' widths ease for a moment after a change of tab, and the board is
 * drawn at each size on the way.
 */
export async function toPlay(page: Page, drawing = '.pl-svg', unit = 'data-cell'): Promise<void> {
  await page.click('.rbtn[data-tab=play]');
  await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'play');
  await expect(page.locator(STAGE)).toHaveAttribute('data-game', '1');
  const size = async (): Promise<string> => {
    const box = await page.locator('#plMid').boundingBox();
    const cell = await page.locator(drawing).getAttribute(unit);
    return `${String(box?.width)} ${String(box?.height)} ${String(cell)}`;
  };
  await expect(async () => {
    const before = await size();
    await page.waitForTimeout(150);
    expect(await size()).toBe(before);
  }).toPass();
}

/**
 * Drags the piece at a cell by steps of cells [dx, dy] (y up), one after
 * another; with `shift`, Shift is held from before the press.
 */
export async function drag(
  page: Page,
  at: string,
  steps: [number, number][],
  opts: { shift?: boolean } = {},
): Promise<void> {
  const cell = Number(await page.locator('.pl-svg').getAttribute('data-cell'));
  const box = await page.locator(`.pl-piece[data-at="${at}"]`).boundingBox();
  if (box === null) {
    throw new Error(`no piece at ${at}`);
  }
  // its lowest left cell
  let x = box.x + cell / 2;
  let y = box.y + box.height - cell / 2;
  await page.mouse.move(x, y);
  if (opts.shift === true) {
    await page.keyboard.down('Shift');
  }
  await page.mouse.down();
  for (const [dx, dy] of steps) {
    x += dx * cell;
    y -= dy * cell;
    await page.mouse.move(x, y, { steps: 5 });
  }
  await page.mouse.up();
  if (opts.shift === true) {
    await page.keyboard.up('Shift');
  }
  await expect(page.locator('#playBoard')).not.toHaveAttribute('data-drag');
}

/** The game shown: the moves made, the place in them, and solved or not. */
export async function expectGame(
  page: Page,
  moves: number,
  at: number,
  solved: boolean,
): Promise<void> {
  const stage = page.locator(STAGE);
  await expect(stage).toHaveAttribute('data-moves', String(moves));
  await expect(stage).toHaveAttribute('data-at', String(at));
  await expect(stage).toHaveAttribute('data-solved', String(solved));
  await expect(page.locator('.pl-board[data-anim]')).toHaveCount(0);
}

/** A stacking puzzle's board. */
export const STACK = '#stackBoard';

/** Opens the Play tab on a stacking puzzle and waits for the game and for the board to rest. */
export async function toStackPlay(page: Page): Promise<void> {
  await toPlay(page, `${STACK} .sk-svg`, 'data-u');
}

// the middle of a rod's column on the board
async function rodPoint(page: Page, rod: number): Promise<{ x: number; y: number }> {
  const box = await page.locator(`${STACK} .sk-col[data-rod="${String(rod)}"]`).boundingBox();
  if (box === null) {
    throw new Error(`no rod ${String(rod)}`);
  }
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

/** Drags the top disc of rod `from` over rod `to` and lets it go. */
export async function carry(page: Page, from: number, to: number): Promise<void> {
  const a = await rodPoint(page, from);
  const b = await rodPoint(page, to);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move((a.x + b.x) / 2, a.y - 30, { steps: 4 });
  await page.mouse.move(b.x, b.y, { steps: 4 });
  await page.mouse.up();
  await expect(page.locator(STACK)).not.toHaveAttribute('data-drag');
}

/** Clicks a rod: its top disc lifts, or the disc lifted goes onto it. */
export async function clickRod(page: Page, rod: number): Promise<void> {
  const p = await rodPoint(page, rod);
  await page.mouse.click(p.x, p.y);
  await expect(page.locator(STACK)).not.toHaveAttribute('data-drag');
}

/** The stacking shown: each rod's discs bottom to top by their numbers, rods apart by `|` ("0,1||2"). */
export async function expectStacks(page: Page, stacks: string): Promise<void> {
  await expect(page.locator(STACK)).toHaveAttribute('data-stacks', stacks);
}
