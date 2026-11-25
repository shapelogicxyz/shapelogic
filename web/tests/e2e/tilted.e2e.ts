// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle (decision 0045, stage C of
// docs/TILTED-SQUARE-PLAN.md): made with File ▸ New, its board in the
// Board section, the Tilt & tolerance section, walls drawn in the board's
// views, a piece on the board's grid, and the Solver's options. A tray of
// another width than height (stage E) is set with Keep square off. The
// Triple Play fixture (stage F) is opened and solved, with its pieces turned
// over and one face up; a piece a wall leaves without a place is marked.
// Every placement (stage G): the Placements dialog lists the boards the
// grid can give, the Solver's Try every placement counts the assemblies on
// each, and Use puts the grid at the one that takes the pieces. The
// dialog's other ways: the problem chosen with Pieces of, a count stopped,
// a board used while a count runs, and Show more on a tray of many boards.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import type { Snapshot } from '../../src/engine/api.generated.ts';
import { BUILT, docOf, ENGINE, engineCall, launch, menu, openFixture, SHAPE_ROWS } from './app.ts';
import { settled } from './editor.ts';
import { confirm, runButton, runEnded, solverSettled, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

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
    expect(snap.tray?.width).toBe(8);
    expect(snap.tray?.height).toBe(8);
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
    // what the tray gives, under the board's view buttons
    await expect(page.locator('#boardViews .tsize')).toHaveText('Tray: 8 by 8');
    await expect(page.locator('#boardViews .tread')).toHaveText('Whole cells: 45');
    await expect(page.locator('#boardViews .tangle')).toHaveText('Tilt angle: 26.57°');
    await expect(page.locator('#boardViews .thint')).toContainText('draw walls');
    await expect(page.locator('#tiltBody .tread')).toHaveCount(0);
    // the allowance switched on: 52 (the research page's figure)
    await page.click('#tiltBody [data-tallow]');
    await settled(page);
    await expect(page.locator('#boardViews .tread')).toHaveText('Whole cells: 52');
    await expect(page.locator('#tiltBody [data-tf="allowance"]')).toHaveValue('0.05');
    // typed: a 6 by 6 tray at 1 down 3 over (Keep square is on: the width sets both sides)
    await expect(page.locator('#tiltBody [data-tlink]')).toHaveAttribute('aria-checked', 'true');
    await page.fill('#tiltBody [data-tf="width"]', '6');
    await page.press('#tiltBody [data-tf="width"]', 'Enter');
    await settled(page);
    await expect(page.locator('#tiltBody [data-tf="height"]')).toHaveValue('6');
    await page.fill('#tiltBody [data-tf="over"]', '3');
    await page.press('#tiltBody [data-tf="over"]', 'Enter');
    await settled(page);
    snap = await docOf(page);
    expect(snap.tray?.width).toBe(6);
    expect(snap.tray?.height).toBe(6);
    expect(snap.tray?.over).toBe(3);
    expect(snap.tray?.allowance).toBe(0.05);
    await expect(page.locator('#boardViews .tread')).toHaveText('Whole cells: 26');
    await expect(page.locator('#boardViews .tangle')).toHaveText('Tilt angle: 18.43°');

    // a rectangular tray (stage E): Keep square off, the height alone, 6 by 8
    await page.click('#tiltBody [data-tlink]');
    await expect(page.locator('#tiltBody [data-tlink]')).toHaveAttribute('aria-checked', 'false');
    await page.fill('#tiltBody [data-tf="height"]', '8');
    await page.press('#tiltBody [data-tf="height"]', 'Enter');
    await settled(page);
    snap = await docOf(page);
    expect(snap.tray?.width).toBe(6);
    expect(snap.tray?.height).toBe(8);
    await expect(page.locator('#tiltBody [data-tf="width"]')).toHaveValue('6');
    await expect(page.locator('#boardViews .tsize')).toHaveText('Tray: 6 by 8');
    await expect(page.locator('#tiltBody .tline')).toContainText('6 by 8');
    // the board's picture is the rectangle: 6 by 8 squares, higher than wide
    await expect(page.locator('#trayArea svg')).toHaveAttribute('data-size', '6x8');
    await expect(page.locator('#trayArea svg [data-sq]')).toHaveCount(48);
    await expect(page.locator('#trayArea svg .tp-whole')).toHaveCount(snap.tray?.whole ?? -1);
    const tall = await page.locator('#trayArea svg').boundingBox();
    expect(tall !== null && tall.height > tall.width * 1.2).toBe(true);
    // the width's stepper: 7 by 8, the height stays; one step back
    await page.click('#tiltBody [data-tk="width"][data-d="1"]');
    await settled(page);
    snap = await docOf(page);
    expect([snap.tray?.width, snap.tray?.height]).toEqual([7, 8]);
    await page.keyboard.press('Control+z');
    await settled(page);
    snap = await docOf(page);
    expect([snap.tray?.width, snap.tray?.height]).toEqual([6, 8]);
    await expect(page.locator('#tiltBody [data-tlink]')).toHaveAttribute('aria-checked', 'false');
    // Keep square on again: the height takes the width, 6 by 6 as before
    await page.click('#tiltBody [data-tlink]');
    await settled(page);
    snap = await docOf(page);
    expect([snap.tray?.width, snap.tray?.height]).toEqual([6, 6]);
    await expect(page.locator('#tiltBody [data-tlink]')).toHaveAttribute('aria-checked', 'true');
    await expect(page.locator('#boardViews .tsize')).toHaveText('Tray: 6 by 6');
    await expect(page.locator('#boardViews .tread')).toHaveText('Whole cells: 26');
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
    await expect(page.locator('#boardViews .tangle')).toHaveText('Tilt angle: 14.04°');
    expect((await docOf(page)).tray?.over).toBe(4);
    await page.keyboard.press('Control+z');
    await settled(page);
    expect((await docOf(page)).tray?.over).toBe(3);
    await expect(page.locator('#boardViews .tangle')).toHaveText('Tilt angle: 18.43°');

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
    // the 3D view draws the board as its whole cells, checker blocks in the tray
    await expect(page.locator('#vp')).toHaveAttribute('data-tray-cells', '24');

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
    // its grid turned so the tray stands straight (stage D), the tray's walls drawn in it
    await expect(page.locator('#garea svg')).toHaveAttribute('data-tilt', '18.43');
    await expect(page.locator('#garea svg .twalls polygon')).toHaveCount(2);
    await expect(page.locator('#garea svg .tedge')).toHaveCount(1);
    // the 3D view: the tray's two walls as blocks
    await expect(page.locator('#vp')).toHaveAttribute('data-tray-walls', '2');
    await expect(page.locator('#vp')).toHaveAttribute('data-tray-cells', '0');
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

test('Triple Play opened and solved: turned over, and one face up', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // tests/data/puzzles/tilted: a 6 by 6 tray, two corner walls, 1 down 3 over, allowance 0.05
    await openFixture(page, 'tilted/TriplePlay.xmpuzzle');
    await expect(page.locator('#vtChip')).toHaveText('Type: Tilted Square');
    const snap = await docOf(page);
    expect(snap.tray?.whole).toBe(24);
    expect(snap.tray?.walls).toEqual([
      [5, 0],
      [0, 5],
    ]);
    expect(snap.shapes.map((s) => s.label)).toEqual(['B', 'S1', 'S2', 'S3', 'S4', 'S5', 'S6']);
    await page.click('#boardList .row');
    await settled(page);
    await expect(page.locator('#boardViews .tread')).toHaveText('Whole cells: 24');
    await expect(page.locator('#tiltBody [data-tallow]')).toHaveAttribute('aria-checked', 'true');

    // each problem as its file holds it, then solved again from nothing
    await toSolver(page);
    const [first, second] = snap.problems.map((p) => p.id);
    for (const [problem, flip, found] of [
      [first, true, 1],
      [second, false, 2],
    ] as const) {
      await page.click(`#slvProbs .row[data-id="${problem ?? ''}"]`);
      await solverSettled(page);
      await expect(page.locator('#leftS')).toHaveAttribute('data-problem', problem ?? '');
      await expect(page.locator('#slvAsm')).toHaveText(String(found));
      // the stored setting: the pieces turned over, or one face up
      await page.click('#slvOptHead');
      const sw = page.locator('#slvOpts [data-o="flipPieces"]');
      await expect(sw).toHaveAttribute('aria-checked', String(flip));
      await page.click('#slvOptHead');
      await runButton(page, 'reset');
      await confirm(page, /^Reset the problem\?$/, true);
      await solverSettled(page);
      await runButton(page, 'start');
      await runEnded(page, 'finished');
      await expect(page.locator('#slvAsm')).toHaveText(String(found));
      await expect(page.locator(`#slvProbs .row[data-id="${problem ?? ''}"] .st`)).toHaveText(
        found === 1 ? '1 assembly' : `${String(found)} assemblies`,
      );
    }
    // the player: nothing to take apart, no analysis offered
    await page.click('#slvList .lrow[data-i="0"]');
    await expect(page.locator('#splayer')).toContainText('there is nothing to take apart');
    await expect(page.locator('#splayer [data-a="analyse"]')).toHaveCount(0);
    // nor in the Manage menu (cleanup item 6.5; the engine refuses it too)
    await page.click('#slvAct [data-a="manage"]');
    await expect(page.locator('#pop [data-k="at"]')).toBeVisible();
    await expect(page.locator('#pop [data-k^="ad"]')).toHaveCount(0);
    await page.keyboard.press('Escape');
  } finally {
    await a.close();
  }
});

test('a piece a tray change leaves without a place is marked, and the change says so', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await menu(page, 'new');
    await page.click('#newPuzzle [data-grid="tilted"]');
    const gen = await page.getAttribute('#stage', 'data-generation');
    await page.click('#newPuzzle [data-x="ok"]');
    await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', gen ?? '');
    await settled(page);
    // a piece of the board's first twelve whole cells (8 by 8, 1 down 2 over)
    const tray = (await docOf(page)).tray;
    const [bx = 0] = tray?.box ?? [];
    const whole = (tray?.cells ?? '')
      .split('')
      .map((c, k) => (c === 'w' ? [k % bx, Math.floor(k / bx), 0] : null))
      .filter((c) => c !== null)
      .slice(0, 12);
    const { shape } = await engineCall<{ shape: string }>(page, 'shape.create');
    await engineCall(page, 'shape.setCells', { shape, tool: 'fixed', cells: whole });
    await settled(page);
    await expect(page.locator('#shapeList .row .tnofit')).toHaveCount(0);
    // a wall at square (2, 2): no place has all twelve cells whole
    await page.click('#boardList .row');
    await settled(page);
    await page.click('#boardViews [data-bv="tray"]');
    await page.locator('#trayArea svg [data-sq="2,2"]').click();
    await settled(page);
    expect((await docOf(page)).shapes[1]?.fits).toBe(false);
    await expect(page.locator('#stText')).toContainText('S1 no longer fits the board');
    await expect(page.locator(`#shapeList .row[data-id="${shape}"] .tnofit`)).toHaveText(
      'Doesn’t fit',
    );
    // undone: it fits again
    await page.keyboard.press('Control+z');
    await settled(page);
    await expect(page.locator('#shapeList .row .tnofit')).toHaveCount(0);
  } finally {
    await a.close();
  }
});

test('every placement: the list, the count on each board, and the one that takes the pieces used', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // Triple Play's pieces, the walls on the other diagonal: the best
    // placement takes no pieces (the file holds the solve: no assembly)
    await openFixture(page, 'tilted/OtherDiagonal.xmpuzzle');
    const snap = await docOf(page);
    expect(snap.tray?.placement).toBe('best');
    expect(snap.tray?.whole).toBe(24);

    // Tilt & tolerance ▸ Placements…: six boards, the one in use first,
    // each with its picture; nothing counted yet
    await page.click('#boardList .row');
    await settled(page);
    await page.click('#tiltBody [data-tplaces]');
    const dlg = page.locator('#placementsDlg');
    await expect(dlg.locator('.plc-row')).toHaveCount(6);
    await expect(dlg.locator('.plc-row .plc-t b')).toHaveText([
      '24 whole cells',
      '24 whole cells',
      '24 whole cells',
      '24 whole cells',
      '24 whole cells',
      '23 whole cells',
    ]);
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-t small')).toHaveText(
      'Most whole cells · In use',
    );
    await expect(dlg.locator('.plc-row[data-k="0"] [data-use]')).toBeDisabled();
    await expect(dlg.locator('.plc-row .plc-pic svg')).toHaveCount(6);
    await expect(dlg.locator('.plc-row[data-k="5"] .plc-pic svg .tp-whole')).toHaveCount(23);
    await expect(dlg.locator('.plc-row[data-fit="none"]')).toHaveCount(6);
    // one problem: no choice of problem is shown
    await expect(dlg.locator('.plc-prob')).toBeHidden();
    await expect(dlg.locator('[data-plc="run"]')).toHaveText('Count assemblies');
    await page.click('#placementsDlg [data-x="close"]');
    await expect(dlg).toHaveCount(0);

    // the Solver: no assembly, and the offer to try every placement, which
    // opens the list and counts at once
    await toSolver(page);
    await solverSettled(page);
    await expect(page.locator('#slvSel .slv-empty')).toContainText('No assemblies found');
    await page.click('#slvSel [data-tryplaces]');
    await expect(dlg.locator('.plc-row')).toHaveCount(6);
    await expect(dlg.locator('[data-plc="sum"]')).toHaveText('1 of 6 placements takes the pieces');
    await expect(dlg.locator('[data-plc="run"]')).toHaveText('Count assemblies');
    await expect(dlg.locator('.plc-row .plc-fit')).toHaveText([
      'No assembly',
      'No assembly',
      'No assembly',
      'No assembly',
      '1 assembly',
      'No assembly',
    ]);
    // the document is as it was: nothing counted changed it
    const during = await docOf(page);
    expect(during.tray).toEqual(snap.tray);
    expect(during.dirty).toBe(false);

    // Use on the board that takes the pieces: the grid lies there (a custom
    // placement), the dialog closes, and the solve finds the assembly
    await dlg.locator('.plc-row[data-fit="fits"] [data-use]').click();
    await expect(dlg).toHaveCount(0);
    await solverSettled(page);
    await expect(page.locator('#stText')).toContainText('The grid lies at the placement chosen');
    const used = await docOf(page);
    expect(used.tray?.placement).toBe('custom');
    expect(used.tray?.whole).toBe(24);
    expect(used.shapes.slice(1).every((x) => x.fits)).toBe(true);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await expect(page.locator('#slvAsm')).toHaveText('1');

    // one step back: the best placement again
    await page.keyboard.press('Control+z');
    await solverSettled(page);
    expect((await docOf(page)).tray).toEqual(snap.tray);
  } finally {
    await a.close();
  }
});

/** A new Tilted Square puzzle (8 by 8, 1 down 2 over, strict), as File ▸ New makes it. */
async function newTilted(page: Page): Promise<Snapshot> {
  await menu(page, 'new');
  await page.click('#newPuzzle [data-grid="tilted"]');
  const gen = await page.getAttribute('#stage', 'data-generation');
  await page.click('#newPuzzle [data-x="ok"]');
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', gen ?? '');
  await settled(page);
  return docOf(page);
}

/** Opens the Placements dialog from Tilt & tolerance, the board selected. */
async function openPlacementsDialog(page: Page): Promise<void> {
  await page.click('#boardList .row');
  await settled(page);
  await page.click('#tiltBody [data-tplaces]');
  await expect(page.locator('#placementsDlg .plc-row').first()).toBeVisible();
}

test('the Placements dialog counts for the problem chosen, with its stored options', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // Triple Play: P1's pieces may be turned over (1 assembly on the board
    // in use), P2 "One face up" keeps them face up (2)
    await openFixture(page, 'tilted/TriplePlay.xmpuzzle');
    const snap = await docOf(page);
    const [first, second] = snap.problems.map((p) => p.id);
    await openPlacementsDialog(page);
    const dlg = page.locator('#placementsDlg');
    await expect(dlg.locator('.plc-row')).toHaveCount(6);
    // two problems: Pieces of chooses, the first at first
    const pick = dlg.locator('[data-plc="problem"]');
    await expect(dlg.locator('.plc-prob')).toBeVisible();
    await expect(pick.locator('option')).toHaveCount(2);
    await expect(pick).toHaveValue(first ?? '');
    await page.click('#placementsDlg [data-plc="run"]');
    await expect(dlg.locator('[data-plc="sum"]')).toHaveText(/ of 6 placements takes? the pieces$/);
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-fit')).toHaveText('1 assembly');
    await expect(
      dlg.locator('.plc-row[data-fit="waiting"], .plc-row[data-fit="trying"]'),
    ).toHaveCount(0);

    // the other problem: the counts are wiped, then counted with its own
    // stored setting (the pieces face up: two ways on the board in use)
    await pick.selectOption(second ?? '');
    await expect(dlg.locator('.plc-row[data-fit="none"]')).toHaveCount(6);
    await expect(dlg.locator('[data-plc="sum"]')).toHaveText('');
    await page.click('#placementsDlg [data-plc="run"]');
    await expect(dlg.locator('[data-plc="sum"]')).toHaveText(/ of 6 placements takes? the pieces$/);
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-fit')).toHaveText('2 assemblies');
    await page.click('#placementsDlg [data-x="close"]');
    await expect(dlg).toHaveCount(0);
    // counting changed nothing
    const after = await docOf(page);
    expect(after.dirty).toBe(false);
    expect(after.solving).toBe(false);
    expect(after.tray).toEqual(snap.tray);
  } finally {
    await a.close();
  }
});

test('the Placements dialog: a count stopped, and a board used while a count runs', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // twenty dominoes on the 45 cells of a new tray: a count that does not end
    const snap = await newTilted(page);
    const [bx = 0] = snap.tray?.box ?? [];
    const at = (snap.tray?.cells ?? '').indexOf('ww');
    expect(at).toBeGreaterThanOrEqual(0);
    const [x, y] = [at % bx, Math.floor(at / bx)];
    const { shape } = await engineCall<{ shape: string }>(page, 'shape.create');
    await engineCall(page, 'shape.setCells', {
      shape,
      tool: 'fixed',
      cells: [
        [x, y, 0],
        [x + 1, y, 0],
      ],
    });
    await engineCall(page, 'problem.setPieceCount', {
      problem: snap.problems[0]?.id,
      shape,
      min: 20,
      max: 20,
    });
    await settled(page);

    await openPlacementsDialog(page);
    const dlg = page.locator('#placementsDlg');
    const run = dlg.locator('[data-plc="run"]');
    const sum = dlg.locator('[data-plc="sum"]');
    const rows = await dlg.locator('.plc-row').count();
    expect(rows).toBeGreaterThanOrEqual(2);
    // one problem: no choice shown
    await expect(dlg.locator('.plc-prob')).toBeHidden();

    // counting: the first board under way with what it has found, the rest
    // waiting, the button now Stop
    await run.click();
    await expect(run).toHaveText('Stop');
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-fit')).toHaveText(/^[\d,]+ so far…$/);
    await expect(dlg.locator('.plc-row[data-k="0"]')).toHaveAttribute('data-fit', 'trying');
    await expect(dlg.locator('.plc-row[data-k="1"] .plc-fit')).toHaveText('Waiting');
    await expect(sum).toHaveText(`Trying placement 1 of ${String(rows)}…`);
    expect((await docOf(page)).solving).toBe(true);

    // Stop: what was counted stays, the board stopped on says "or more"
    await run.click();
    await expect(sum).toHaveText(
      `Stopped after 0 of ${String(rows)} placements: 1 takes the pieces so far`,
    );
    await expect(run).toHaveText('Count assemblies');
    await expect(dlg.locator('.plc-row[data-k="0"]')).toHaveAttribute('data-fit', 'partial');
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-fit')).toHaveText(/^[\d,]+ or more$/);
    await expect(dlg.locator('.plc-row[data-k="1"] .plc-fit')).toHaveText('Not tried');
    await expect.poll(async () => (await docOf(page)).solving).toBe(false);

    // counting again, a board is used while it runs: the count is stopped
    // first, then the grid lies there, in one step
    await run.click();
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-fit')).toHaveText(/^[\d,]+ so far…$/);
    await dlg.locator('.plc-row[data-k="1"] [data-use]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    await expect(page.locator('#stText')).toContainText('The grid lies at the placement chosen');
    const used = await docOf(page);
    expect(used.solving).toBe(false);
    expect(used.tray?.placement).toBe('custom');
    expect(used.shapes[1]?.fits).toBe(true);
    const listed = await engineCall<{ inUse: number | null }>(page, 'tray.placements');
    expect(listed.inUse).toBe(1);
    await page.keyboard.press('Control+z');
    await settled(page);
    expect((await docOf(page)).tray).toEqual(snap.tray);

    // closed while counting: the count is stopped
    await openPlacementsDialog(page);
    await run.click();
    await expect(run).toHaveText('Stop');
    await page.click('#placementsDlg [data-x="close"]');
    await expect(dlg).toHaveCount(0);
    await expect.poll(async () => (await docOf(page)).solving).toBe(false);
  } finally {
    await a.close();
  }
});

test('the Placements dialog shows a tray of many boards forty at a time', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // a 16 by 16 tray at 3 down 7 over with scattered walls gives about a hundred boards
    await newTilted(page);
    await engineCall(page, 'tray.set', { tray: { size: 16, over: 7, down: 3, allowance: 0.02 } });
    await engineCall(page, 'tray.setWalls', {
      squares: [
        [0, 0],
        [1, 1],
        [8, 2],
        [4, 6],
        [8, 12],
        [15, 15],
      ],
      wall: true,
    });
    await settled(page);
    const listed = await engineCall<{ boards: { whole: number }[]; complete: boolean }>(
      page,
      'tray.placements',
    );
    const n = listed.boards.length;
    expect(listed.complete).toBe(true);
    expect(n).toBeGreaterThan(80);

    await openPlacementsDialog(page);
    const dlg = page.locator('#placementsDlg');
    const more = dlg.locator('[data-more]');
    await expect(dlg.locator('.cb p').first()).toContainText(`The grid gives ${String(n)} boards`);
    await expect(dlg.locator('.plc-row')).toHaveCount(40);
    await expect(more).toHaveText(`Show 40 more of ${String(n - 40)}`);
    await expect(dlg.locator('.plc-row[data-k="0"] .plc-t b')).toHaveText(
      `${String(listed.boards[0]?.whole ?? 0)} whole cells`,
    );
    await more.click();
    await expect(dlg.locator('.plc-row')).toHaveCount(80);
    await expect(more).toHaveText(`Show ${String(Math.min(40, n - 80))} more of ${String(n - 80)}`);
    // the rows added get their pictures too
    await expect(dlg.locator('.plc-row[data-k="45"] .plc-pic svg')).toHaveCount(1);
    await more.click();
    await expect(dlg.locator('.plc-row')).toHaveCount(n);
    await expect(more).toHaveCount(0);
    await expect(dlg.locator(`.plc-row[data-k="${String(n - 1)}"] .plc-t b`)).toHaveText(
      `${String(listed.boards[n - 1]?.whole ?? 0)} whole cells`,
    );
  } finally {
    await a.close();
  }
});
