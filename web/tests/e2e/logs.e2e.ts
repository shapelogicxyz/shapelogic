// SPDX-License-Identifier: GPL-3.0-or-later
//
// View ▸ Log File Manager… (main/app-log.ts, renderer/shell/log-files.ts):
// the app's start, a file opened and one that could not be (the engine's
// lines, its `log` setting) are in the log, newest on top; the errors and
// warnings filter and the search box filter it as they change; a hundred
// lines at a time with Load more messages…; Export… writes all of it as
// plain text to the file the save dialog gives; Clear All Log Data…
// deletes it after asking. The log is in the test's data directory
// (BT_USER_DATA).

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { answerDialogs, FIXTURES, launch, menu, openFixture } from './app.ts';
import type { App } from './app.ts';

let a: App;
let dir: string;
let userData: string;

test.beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), 'bt-e2e-logs-'));
  userData = mkdtempSync(join(tmpdir(), 'bt-e2e-logdata-'));
});

test.afterEach(async () => {
  await a.close();
  rmSync(dir, { recursive: true, force: true });
  rmSync(userData, { recursive: true, force: true });
});

const ROWS = '.lf-row';

/** Opens the manager, and reads the log again until `text` is in it. */
async function openWith(page: Page, text: string): Promise<void> {
  await menu(page, 'logFiles');
  const dlg = page.locator('#logFiles');
  await expect(dlg).toBeVisible();
  await expect(async () => {
    await dlg.locator('[data-x="refresh"]').click();
    await expect(dlg.locator('.lf-list')).toContainText(text, { timeout: 500 });
  }).toPass({ timeout: 10_000 });
}

test('the log tells what was opened and what could not be, newest first, filtered', async () => {
  a = await launch({ userData });
  const page = a.page;
  await openFixture(page, 'BallRoom.xmpuzzle');
  // a file that is not a puzzle: the engine says why it could not open it
  const bad = join(dir, 'damaged.xmpuzzle');
  writeFileSync(bad, 'this is not a puzzle');
  await answerDialogs(page, { open: bad });
  await menu(page, 'open');
  await expect(page.locator('#fname')).toHaveText('BallRoom.xmpuzzle');
  await page.keyboard.press('Escape');

  await openWith(page, 'damaged.xmpuzzle');
  const dlg = page.locator('#logFiles');
  const lines = await dlg.locator(ROWS).allTextContents();
  // newest on top: the failed open above the open before it, the start last
  const failed = lines.findIndex((l) => l.includes('ERROR') && l.includes('damaged.xmpuzzle'));
  const opened = lines.findIndex(
    (l) => l.includes('INFO') && l.includes(`Opened "${resolve(FIXTURES, 'BallRoom.xmpuzzle')}"`),
  );
  const started = lines.findIndex((l) => l.includes('[app] ShapeLogic') && l.includes('started'));
  expect(failed).toBeGreaterThanOrEqual(0);
  expect(opened).toBeGreaterThan(failed);
  expect(started).toBeGreaterThan(opened);
  await expect(dlg.locator('.lf-row.lf-error').first()).toContainText('Could not open');

  // Errors and warnings: those alone
  await dlg.locator('.lf-errors').check();
  await expect(dlg.locator('.lf-row').first()).toContainText('damaged.xmpuzzle');
  const rows: string[] = await dlg
    .locator(ROWS)
    .evaluateAll((els: Element[]) => els.map((e) => e.getAttribute('class') ?? ''));
  expect(rows.length).toBeGreaterThan(0);
  expect(rows.every((c) => /\blf-(error|warn)\b/.test(c))).toBe(true);
  await dlg.locator('.lf-errors').uncheck();

  // the search box filters as each letter is typed and deleted
  const search = dlg.locator('.lf-search');
  await search.pressSequentially('ballr');
  await expect(dlg.locator(ROWS).first()).toContainText('BallRoom');
  expect(
    (await dlg.locator(ROWS).allTextContents()).every((l) => l.toLowerCase().includes('ballr')),
  ).toBe(true);
  await search.pressSequentially('zzz');
  await expect(dlg.locator('.lf-empty')).toContainText('No line has “ballrzzz” in it.');
  for (let i = 0; i < 3; i++) {
    await search.press('Backspace');
  }
  await expect(dlg.locator(ROWS).first()).toContainText('BallRoom');
  await search.fill('');
  await expect(dlg.locator(ROWS)).toHaveCount(lines.length);

  // Export: all of it, as plain text, to the file the save dialog gives
  const out = join(dir, 'exported');
  await answerDialogs(page, { save: out });
  await dlg.locator('[data-x="export"]').click();
  await expect(page.locator('#stText')).toContainText('Log exported');
  const text = readFileSync(`${out}.txt`, 'utf8');
  expect(text).toContain('damaged.xmpuzzle');
  expect(text).toContain('started');

  // Clear: asked first, then the log is empty
  await dlg.locator('[data-x="clear"]').click();
  await page.locator('#logFilesClear [data-x="clear"]').click();
  await expect(dlg.locator('.lf-empty')).toHaveText('The log is empty.');
  await dlg.locator('[data-x="close"]').click();
  await expect(dlg).toBeHidden();
});

/** Switches Settings ▸ General ▸ Debug logging. */
async function switchDebug(page: Page): Promise<void> {
  await page.click('#btnSettings');
  await page.click('#setNav [data-p="general"]');
  await page.click('#setPg .switch[data-k="debugLog"]');
  await page.keyboard.press('Escape');
  await expect(page.locator('#scrim')).not.toHaveClass(/show/);
}

test('Settings ▸ General ▸ Debug logging is told in the log, and the log trimmed after', async () => {
  a = await launch({ userData });
  const page = a.page;
  await switchDebug(page);
  await openWith(page, 'Debug logging turned on');
  const dlg = page.locator('#logFiles');
  await expect(dlg.locator('.lf-sum')).toContainText('debug logging on');
  await dlg.locator('[data-x="close"]').click();
  await switchDebug(page);
  await openWith(page, 'Debug logging turned off');
  await expect(dlg.locator('.lf-sum')).not.toContainText('debug logging on');
  // the start, both changes: a short log is kept whole
  await expect(dlg.locator(ROWS).last()).toContainText('started');
});

test('a long log comes a hundred lines at a time', async () => {
  // a log from earlier runs, 250 lines
  mkdirSync(join(userData, 'logs'), { recursive: true });
  const old = Array.from(
    { length: 250 },
    (_, i) => `2026-11-01 10:00:00.000 INFO  [app] earlier line ${String(i)}\n`,
  ).join('');
  writeFileSync(join(userData, 'logs', 'shapelogic.log'), old);
  a = await launch({ userData });
  const page = a.page;
  await openWith(page, 'started');
  const dlg = page.locator('#logFiles');
  await expect(dlg.locator(ROWS)).toHaveCount(100);
  const more = dlg.locator('.lf-more');
  await expect(more).toBeVisible();
  await more.click();
  await expect(dlg.locator(ROWS)).toHaveCount(200);
  await more.click();
  await expect(more).toBeHidden();
  const all = await dlg.locator(ROWS).allTextContents();
  expect(all.at(-1)).toContain('earlier line 0');
  expect(all.length).toBeGreaterThan(250);
});
