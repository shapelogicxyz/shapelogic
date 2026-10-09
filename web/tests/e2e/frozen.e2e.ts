// SPDX-License-Identifier: GPL-3.0-or-later
//
// While a solve runs the shapes are frozen (decision 0011; Snapshot.solving):
// the Entities cards are disabled, the status bar says to pause the solve
// first, and a click, a key or a 3D click that would edit says so instead
// of asking the engine, which would refuse it as invalid_state. A long
// solve (a 2x4x6 box of dominoes on one thread) is started and aborted
// through the bridge; the call log shows that the UI sent no edit and
// met no invalid_state while it ran, and editing works again after.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, readCallLog } from './app.ts';
import { cellPoint, clickCell, settled, shapeGet } from './editor.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const EDITS = new Set([
  'shape.create',
  'shape.duplicate',
  'shape.delete',
  'shape.move',
  'shape.rename',
  'shape.setWeight',
  'shape.setColour',
  'shape.setCells',
  'shape.setBox',
  'shape.resize',
  'shape.task',
  'colour.add',
  'colour.delete',
  'edit.begin',
  'edit.end',
  'edit.undo',
  'edit.redo',
]);

test('the shapes are frozen while a solve runs', async () => {
  test.setTimeout(120_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await newDocument(page, 'bricks');
    // a box of dominoes: many assemblies, a long solve on one thread
    const box = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 4, 6] }))
      .shape;
    await engineCall(page, 'shape.setBox', {
      shape: box,
      tool: 'fixed',
      from: [0, 0, 0],
      to: [1, 3, 5],
    });
    const d = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 1, 1] }))
      .shape;
    await engineCall(page, 'shape.setBox', {
      shape: d,
      tool: 'fixed',
      from: [0, 0, 0],
      to: [1, 0, 0],
    });
    const p = (await engineCall<{ problem: string }>(page, 'problem.create')).problem;
    await engineCall(page, 'problem.setResult', { problem: p, shape: box });
    await engineCall(page, 'problem.setPieceCount', { problem: p, shape: d, min: 24, max: 24 });
    await settled(page);
    await page.click(`#shapeList .row[data-id="${box}"]`);
    const before = await shapeGet(page, box);

    await engineCall(page, 'solve.start', {
      problem: p,
      options: { threads: 1, disassemble: true },
    });
    await expect(page.locator('#stage')).toHaveAttribute('data-frozen', '1');
    await expect(page.locator('#stText')).toContainText('pause the solve first');
    const mark = readCallLog(log).length;

    // a click in the grid, the keys of edits, a 3D click, the cards' buttons
    const pt = await cellPoint(page, [0, 0, 0]);
    // (the disabled card takes no click: the status already says why)
    await page.mouse.click(pt?.x ?? 0, pt?.y ?? 0);
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Control+z');
    await expect(page.locator('#stText')).toHaveText('Pause the solve first');
    await page.keyboard.press('Delete');
    await page.keyboard.press('Control+d');
    await page.keyboard.press('F2');
    await expect(page.locator('#shapeList .lbl input')).toHaveCount(0);
    const vp = await page.locator('#cv').boundingBox();
    await page.keyboard.down('Shift');
    await page.mouse.click(
      (vp?.x ?? 0) + (vp?.width ?? 0) / 2,
      (vp?.y ?? 0) + (vp?.height ?? 0) / 2,
    );
    await page.keyboard.up('Shift');
    await page.locator('#btnNew').dispatchEvent('click');
    await page.locator('#cAdd').dispatchEvent('click');
    await page.locator('#sizeSteppers [data-d="0"][data-s="1"]').dispatchEvent('click');
    await page.locator('#insp [data-task="flip12"]').dispatchEvent('click');
    await expect(page.locator('#stText')).toHaveText('Pause the solve first');
    // the cards are disabled
    await expect(page.locator('#insp')).toHaveCSS('pointer-events', 'none');
    await expect(page.locator('#toolStrip')).toHaveCSS('pointer-events', 'none');
    // looking is still allowed: the tool keys, another shape
    await page.keyboard.press('2');
    await expect(page.locator('#toolStrip [data-t="variable"]')).toHaveClass(/\bon\b/);
    await page.keyboard.press('1');
    await page.click(`#shapeList .row[data-id="${d}"]`);
    await page.click(`#shapeList .row[data-id="${box}"]`);
    await expect(page.locator('#stage')).toHaveAttribute('data-frozen', '1');
    const during = readCallLog(log).slice(mark);
    expect(during.filter((c) => EDITS.has(c.method)).map((c) => c.method)).toEqual([]);
    expect(await shapeGet(page, box)).toEqual(before);

    // the solve ends (aborted): editing works again
    await engineCall(page, 'solve.abort', { problem: p });
    await expect(page.locator('#stage')).toHaveAttribute('data-frozen', '0');
    await expect(page.locator('#stText')).not.toContainText('pause the solve first');
    await clickCell(page, [0, 0, 0], { button: 'right' });
    expect((await shapeGet(page, box)).cells.length).toBe(before.cells.length - 1);
    expect(readCallLog(log).filter((c) => c.errc === 'invalid_state')).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
