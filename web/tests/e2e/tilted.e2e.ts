// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle (decision 0045, stage C of
// docs/TILTED-SQUARE-PLAN.md): made with File ▸ New, its board in the
// Board section, the Tilt & tolerance section, walls drawn in the board's
// views, a piece on the board's grid, and the Solver's options.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import type { Snapshot } from '../../src/engine/api.generated.ts';
import { BUILT, ENGINE, engineCall, launch, menu, SHAPE_ROWS } from './app.ts';
import { settled } from './editor.ts';
import { toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const docOf = (page: Page): Promise<Snapshot> => engineCall<Snapshot>(page, 'document.get');

test('a Tilted Square puzzle: New, the board, Tilt & tolerance, walls, a piece, the Solver', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // File ▸ New lists Tilted Square after Stacking, with its fields and a picture of the tray
    // a fresh app's puzzle is unchanged: New asks nothing first
    await menu(page, 'new');
    expect(
      await page
        .locator('#newPuzzle [data-grid]')
        .evaluateAll((els) => els.map((e) => (e as HTMLElement).dataset.grid)),
    ).toEqual([
      'bricks',
      'sliding',
      'stacking',
      'tilted',
      'prisms',
      'spheres',
      'rhombic',
      'tetraOcta',
      'hexagonal',
      'cairo',
    ]);
    await page.click('#newPuzzle [data-grid="tilted"]');
    await expect(page.locator('#newPuzzle [data-grid="tilted"]')).toHaveText('Tilted Square');
    // as simple as the other kinds: a description and an example, the tray a
    // new puzzle starts with (8 by 8, 1 down 2 over, strict: 45 whole cells)
    await expect(page.locator('#newInfo')).toContainText('Description:');
    await expect(page.locator('#newInfo')).toContainText('Example:');
    await expect(page.locator('#newInfo input')).toHaveCount(0);
    await expect(page.locator('#ngtPic svg .tp-whole')).toHaveCount(45);
    const gen = await page.getAttribute('#stage', 'data-generation');
    await page.click('#newPuzzle [data-x="ok"]');
    await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', gen ?? '');
    await settled(page);

    let snap = await docOf(page);
    expect(snap.grid).toBe('bricks');
    expect(snap.tray?.size).toBe(8);
    expect(snap.tray?.over).toBe(2);
    expect(snap.tray?.down).toBe(1);
    expect(snap.tray?.allowance).toBe(0);
    expect(snap.tray?.whole).toBe(45);
    expect(snap.shapes.map((s) => s.label)).toEqual(['B']);
    expect(snap.problems.length).toBe(1);
    await expect(page.locator('#vtChip')).toHaveText('Type: Tilted Square');

    // the board alone in the Board section: renamed, never copied or deleted
    await expect(page.locator('#boardsSec')).toBeVisible();
    await expect(page.locator('#boardsTitle')).toHaveText('Board');
    await expect(page.locator('#btnNewBoard')).toBeHidden();
    await expect(page.locator('#boardList .row')).toHaveCount(1);
    await expect(page.locator('#boardList .row [data-a="copy"]')).toHaveCount(0);
    await expect(page.locator('#boardList .row [data-a="del"]')).toHaveCount(0);
    await expect(page.locator('#btnNewWhat')).toBeVisible();

    // selected, it has Tilt & tolerance and the board's views, not Transform or the grid
    await page.click('#boardList .row');
    await settled(page);
    await expect(page.locator('#secTilt')).toBeVisible();
    // the Tilted Square Tutorial right below it, a closed section that does not fold open
    await expect(page.locator('#secTut')).toBeVisible();
    await expect(page.locator('#secTut .sh')).toHaveText('Tilted Square Tutorial');
    const tilt = await page.locator('#secTilt').boundingBox();
    const tut = await page.locator('#secTut').boundingBox();
    expect(tut !== null && tilt !== null && Math.abs(tut.y - (tilt.y + tilt.height)) < 2).toBe(
      true,
    );
    await expect(page.locator('#secTf')).toBeHidden();
    await expect(page.locator('#secFit')).toBeHidden();
    await expect(page.locator('#stage')).toHaveClass(/tltb/);
    await expect(page.locator('#garea')).toBeHidden();
    await expect(page.locator('#trayArea svg')).toBeVisible();
    await expect(page.locator('#tiltBody .tread')).toHaveText('45 whole cells · 26.57°');
    // the allowance switched on: 52 (the research page's figure)
    await page.click('#tiltBody [data-tallow]');
    await settled(page);
    await expect(page.locator('#tiltBody .tread')).toHaveText('52 whole cells · 26.57°');
    await expect(page.locator('#tiltBody [data-tf="allowance"]')).toHaveValue('0.05');
    // typed: a 6 by 6 tray at 1 down 3 over
    await page.fill('#tiltBody [data-tf="size"]', '6');
    await page.press('#tiltBody [data-tf="size"]', 'Enter');
    await settled(page);
    await page.fill('#tiltBody [data-tf="over"]', '3');
    await page.press('#tiltBody [data-tf="over"]', 'Enter');
    await settled(page);
    snap = await docOf(page);
    expect(snap.tray?.size).toBe(6);
    expect(snap.tray?.over).toBe(3);
    expect(snap.tray?.allowance).toBe(0.05);
    await expect(page.locator('#tiltBody .tread')).toHaveText('26 whole cells · 18.43°');
    // down above over is refused, the field put back
    await page.fill('#tiltBody [data-tf="down"]', '4');
    await page.press('#tiltBody [data-tf="down"]', 'Enter');
    await expect(page.locator('#stText')).toContainText('Down: a whole number from 0 to 3');
    await expect(page.locator('#tiltBody [data-tf="down"]')).toHaveValue('1');
    // Delete on the board says why it stays
    await page.keyboard.press('Delete');
    await expect(page.locator('#stText')).toContainText('not copied, deleted or moved');
    expect((await docOf(page)).shapes.length).toBe(1);

    // Over +1: 1 down 4 over, one undo step
    await page.click('#tiltBody [data-tk="over"][data-d="1"]');
    await settled(page);
    await expect(page.locator('#tiltBody .tread')).toContainText('14.04°');
    expect((await docOf(page)).tray?.over).toBe(4);
    await page.keyboard.press('Control+z');
    await settled(page);
    expect((await docOf(page)).tray?.over).toBe(3);
    await expect(page.locator('#tiltBody .tread')).toHaveText('26 whole cells · 18.43°');

    // walls: in the Tray view a drag over two squares is one step
    await page.click('#boardViews [data-bv="tray"]');
    await expect(page.locator('#trayArea svg [data-sq]')).toHaveCount(36);
    const sq = (key: string) => page.locator(`#trayArea svg [data-sq="${key}"]`);
    const from = await sq('5,0').boundingBox();
    const to = await sq('5,1').boundingBox();
    if (from === null || to === null) {
      throw new Error('the squares are not drawn');
    }
    await page.mouse.move(from.x + from.width / 2, from.y + from.height / 2);
    await page.mouse.down();
    await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 4 });
    await page.mouse.up();
    await settled(page);
    snap = await docOf(page);
    expect(snap.tray?.walls).toEqual([
      [5, 0],
      [5, 1],
    ]);
    await expect(sq('5,0')).toHaveClass(/wall/);
    await page.keyboard.press('Control+z');
    await settled(page);
    expect((await docOf(page)).tray?.walls).toEqual([]);
    // two views, no Both
    await expect(page.locator('#boardViews [data-bv]')).toHaveCount(2);
    await expect(page.locator('#boardViews [data-bv="both"]')).toHaveCount(0);
    // Triple Play's corners, by clicks in the Cells view
    await page.click('#boardViews [data-bv="cells"]');
    await expect(page.locator('#trayArea svg')).toHaveClass(/tp-cells/);
    await sq('5,0').click();
    await settled(page);
    await sq('0,5').click();
    await settled(page);
    snap = await docOf(page);
    expect(snap.tray?.walls).toEqual([
      [5, 0],
      [0, 5],
    ]);
    expect(snap.tray?.whole).toBe(24);
    // both views draw the same: the squares over the turned cells
    await expect(page.locator('#trayArea svg [data-sq]')).toHaveCount(36);
    await page.click('#boardViews [data-bv="tray"]');
    await expect(page.locator('#trayArea svg')).toHaveClass(/tp-tray/);
    await expect(page.locator('#trayArea svg [data-sq]')).toHaveCount(36);
    await expect(page.locator('#trayArea svg .tp-whole')).toHaveCount(24);

    // a new piece has the board's size; the grid editor draws it, without size steppers or layers
    await page.click('#btnNew');
    await settled(page);
    snap = await docOf(page);
    expect(snap.shapes.map((s) => s.label)).toEqual(['B', 'S1']);
    expect(snap.shapes[1]?.size).toEqual([...(snap.tray?.box ?? []), 1]);
    await expect(page.locator('#stage')).not.toHaveClass(/tltb/);
    await expect(page.locator('#stage')).toHaveClass(/tlt/);
    await expect(page.locator('#garea svg')).toBeVisible();
    await expect(page.locator('#gridSize')).toBeHidden();
    await expect(page.locator('#secTilt')).toBeHidden();
    await expect(page.locator('#secTf')).toBeVisible();
    // for a piece the tutorial is below Repair & surface
    const tools = await page.locator('#secTools').boundingBox();
    const tut2 = await page.locator('#secTut').boundingBox();
    expect(tut2 !== null && tools !== null && Math.abs(tut2.y - (tools.y + tools.height)) < 2).toBe(
      true,
    );
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(2);
    // its grid: the cells the tray cuts are gray and take nothing; a whole cell takes the tool
    const [bx = 0, by = 0] = snap.tray?.box ?? [];
    await expect(page.locator('#garea .cell')).toHaveCount(bx * by);
    await expect(page.locator('#garea .cell.tcut')).toHaveCount(bx * by - 24);
    const k = snap.tray?.cells.indexOf('w') ?? -1;
    await page
      .locator(`#garea .cell[data-at="${String(k % bx)},${String(Math.floor(k / bx))},0"]`)
      .click();
    await settled(page);
    expect((await docOf(page)).shapes[1]?.fixed).toBe(1);

    // the Solver: assemble only, and Pieces may be turned over
    await toSolver(page);
    await expect(page.locator('#slvMode [data-m="dis"]')).toBeDisabled();
    await page.click('#slvOptHead');
    await expect(page.locator('#slvOpts [data-o="flipPieces"]')).toBeVisible();
    await expect(page.locator('#slvOpts [data-o="keepAssemblies"]')).toHaveCount(0);
  } finally {
    await a.close();
  }
});
