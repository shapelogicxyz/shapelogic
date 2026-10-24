// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes list (FRONTEND-PLAN.md, stage 2): new, duplicate (Ctrl+D,
// added last), delete (Del; never the last), move by Alt+arrows, by the
// right-click menu and by dragging a row (labels renumber), rename (F2,
// Enter, a double-click; Esc cancels; a name the engine refuses shows its
// message), weight, the shape's own colour, selection by the arrows; each
// checked against the engine's shape.list.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, readCallLog } from './app.ts';
import { selected, settled } from './editor.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

interface Summary {
  id: string;
  label: string;
  name: string;
  weight: number;
  colour: string | null;
}

async function list(page: Page): Promise<Summary[]> {
  return (await engineCall<{ shapes: Summary[] }>(page, 'shape.list')).shapes;
}

/** The ids in list order, as the engine has them. */
async function order(page: Page): Promise<string[]> {
  return (await list(page)).map((s) => s.id);
}

/** The rows as the page shows them: id and label. */
async function rows(page: Page): Promise<string[]> {
  return page.$$eval('#shapeList .row', (rs) =>
    rs.map(
      (r) =>
        `${(r as HTMLElement).dataset.id ?? ''}:${r.querySelector('.chip')?.textContent ?? ''}`,
    ),
  );
}

async function sameAsEngine(page: Page): Promise<void> {
  await settled(page);
  await expect
    .poll(async () => rows(page))
    .toEqual((await list(page)).map((s) => `${s.id}:${s.label}`));
}

test('the shapes list', async () => {
  test.setTimeout(120_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await newDocument(page, 'bricks');
    // the only shape can be deleted too
    await page.click('#btnNew');
    await sameAsEngine(page);
    await expect(page.locator('#shapeList .row [data-a="del"]')).toBeEnabled();
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Delete');
    await sameAsEngine(page);
    expect((await order(page)).length).toBe(0);
    // new, duplicate: added last and selected
    await page.click('#btnNew');
    await sameAsEngine(page);
    await page.click('#btnNew');
    await sameAsEngine(page);
    const [s1, s2] = await order(page);
    expect(await selected(page)).toBe(s2);
    await page.keyboard.press('Control+d');
    await sameAsEngine(page);
    const s3 = (await order(page))[2];
    expect(await selected(page)).toBe(s3);
    // the arrows select while the list has the focus
    await page.locator('#shapeList').focus();
    await page.keyboard.press('ArrowUp');
    expect(await selected(page)).toBe(s2);
    await page.keyboard.press('ArrowUp');
    expect(await selected(page)).toBe(s1);
    await page.keyboard.press('ArrowDown');
    expect(await selected(page)).toBe(s2);
    // Alt+arrows move it; labels follow the order
    await page.keyboard.press('Alt+ArrowUp');
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s2, s1, s3]);
    expect(await selected(page)).toBe(s2);
    await expect(page.locator('#stText')).toHaveText('Order changed — shapes renumbered S1…S3');
    await page.keyboard.press('Alt+ArrowDown');
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s1, s2, s3]);
    // the menu: move later
    await page.click(`#shapeList .row[data-id="${s1}"]`, { button: 'right' });
    await expect(page.locator('#pop')).toBeVisible();
    await page.click('#pop [data-k="down"]');
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s2, s1, s3]);
    // dragging a row onto another
    await page.dragAndDrop(
      `#shapeList .row[data-id="${s3}"] .grip`,
      `#shapeList .row[data-id="${s2}"]`,
    );
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s3, s2, s1]);

    // rename: F2, Enter in the list, a double-click; Esc cancels
    const nameOf = async (id: string | undefined): Promise<string> =>
      (await list(page)).find((s) => s.id === id)?.name ?? '';
    await page.click(`#shapeList .row[data-id="${s1}"] .chip`);
    await page.keyboard.press('F2');
    await page.locator('#shapeList .lbl input').fill('Arm');
    await page.keyboard.press('Enter');
    await expect.poll(async () => nameOf(s1)).toBe('Arm');
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Enter');
    await page.locator('#shapeList .lbl input').fill('Leg');
    await page.keyboard.press('Escape');
    await settled(page);
    expect(await nameOf(s1)).toBe('Arm');
    await expect(page.locator(`#shapeList .row[data-id="${s1}"] .lbl`)).toHaveText('Arm');
    await page.dblclick(`#shapeList .row[data-id="${s2}"] .chip`);
    await page.locator('#shapeList .lbl input').fill('Box');
    await page.keyboard.press('Enter');
    await expect.poll(async () => nameOf(s2)).toBe('Box');
    // a name the engine refuses (a control character): its message shows
    await page.click(`#shapeList .row[data-id="${s2}"] [data-a="label"]`);
    await page.locator('#shapeList .lbl input').fill('Bad\u0001name');
    await page.keyboard.press('Enter');
    await expect(page.locator('#stText')).toContainText('Rename:');
    expect(await nameOf(s2)).toBe('Box');

    // weight − and + from the menu (the row has no weight buttons or badge)
    await expect(page.locator(`#shapeList .row[data-id="${s2}"] [data-a="wp"]`)).toHaveCount(0);
    const weightMenu = async (k: 'wp' | 'wm'): Promise<void> => {
      await page.click(`#shapeList .row[data-id="${s2}"]`, { button: 'right' });
      await page.click(`#pop [data-k="${k}"]`);
      await settled(page);
    };
    await weightMenu('wp');
    await expect.poll(async () => (await list(page)).find((s) => s.id === s2)?.weight).toBe(2);
    await weightMenu('wm');
    await weightMenu('wm');
    await expect.poll(async () => (await list(page)).find((s) => s.id === s2)?.weight).toBe(0);
    await page.click(`#shapeList .row[data-id="${s2}"]`, { button: 'right' });
    await expect(page.locator('#pop [data-k="wm"]')).toBeDisabled();
    await page.keyboard.press('Escape');

    // the shape's own colour (the picker's answer), and back to the default
    await page.click(`#shapeList .row[data-id="${s2}"]`, { button: 'right' });
    await expect(page.locator('#pop [data-k="nocolour"]')).toBeDisabled();
    await page.keyboard.press('Escape');
    await page.evaluate((id) => {
      const p = document.getElementById('shapeColourPick');
      if (p !== null) {
        p.dataset.shape = id;
      }
    }, s2 ?? '');
    await page.locator('#shapeColourPick').fill('#336699');
    await expect
      .poll(async () => (await list(page)).find((s) => s.id === s2)?.colour)
      .toBe('#336699');
    await expect(page.locator(`#shapeList .row[data-id="${s2}"] .chip`)).toHaveCSS(
      'background-color',
      'rgb(51, 102, 153)',
    );
    await page.click(`#shapeList .row[data-id="${s2}"]`, { button: 'right' });
    await page.click('#pop [data-k="nocolour"]');
    await expect.poll(async () => (await list(page)).find((s) => s.id === s2)?.colour).toBeNull();

    // delete: Del, and the menu; the next shape takes the place
    await page.click(`#shapeList .row[data-id="${s2}"]`);
    await page.keyboard.press('Delete');
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s3, s1]);
    expect(await selected(page)).toBe(s1);
    await page.click(`#shapeList .row[data-id="${s3}"]`, { button: 'right' });
    await page.click('#pop [data-k="del"]');
    await sameAsEngine(page);
    expect(await order(page)).toEqual([s1]);
    await expect(page.locator('#shapeList .row [data-a="del"]')).toBeEnabled();
    // the collapsed card's chips select and add
    await page.click('#left [data-collapse="L"]');
    await page.click('#lrail #btnLN');
    await sameAsEngine(page);
    expect((await order(page)).length).toBe(2);
    await page.click(`#lrail .chip[data-id="${s1}"]`);
    expect(await selected(page)).toBe(s1);
    await page.click('#lrail #btnLE');
    await expect(page.locator('#shapeList')).toBeVisible();

    // the refused rename is the only refusal
    const refused = readCallLog(log).filter((c) => c.errc !== null && c.errc !== 'superseded');
    expect(refused.map((c) => `${c.method} ${c.errc ?? ''}`)).toEqual([
      'shape.rename invalid_argument',
    ]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
