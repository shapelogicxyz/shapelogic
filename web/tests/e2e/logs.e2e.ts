// SPDX-License-Identifier: GPL-3.0-or-later
//
// View ▸ Log File Manager… (main/app-log.ts, renderer/shell/log-files.ts):
// the app's start, a file opened and one that could not be (the engine's
// lines, its `log` setting) are in the log, newest on top; the errors and
// warnings filter and the search box filter it as they change; a hundred
// lines at a time with Load more messages…; Export… offers the last 100
// lines (a longer log) and all of it, with their sizes, and writes the one
// picked as plain text to the file the save dialog gives; Clear All Log
// Data… deletes it after asking; the line at the bottom left links GitHub
// Issues. The log is in the test's data directory (SHAPELOGIC_USER_DATA).

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
  // Pretty first: a table, the level in its color, the event apart
  await expect(dlg.locator('.lf-view [data-v="pretty"]')).toHaveClass(/\bon\b/);
  await expect(dlg.locator('.lf-table th')).toHaveText([
    'Date',
    'Time',
    'Level',
    'Source',
    'Event',
    'Message',
  ]);
  await expect(dlg.locator('.lf-row.lf-error .lf-lv-error').first()).toHaveText('ERROR');
  await expect(dlg.locator('.lf-row .lf-time').first()).toHaveText(/^\d\d:\d\d$/);
  // every line has its event
  const events = await dlg.locator(`${ROWS} .lf-ev`).allTextContents();
  expect(events.length).toBeGreaterThan(2);
  expect(events.every((e) => /^[A-Z]/.test(e))).toBe(true);
  expect(events).toContain('File Opened');
  expect(events).toContain('App Started');
  // Raw: the lines as the file has them
  await dlg.locator('.lf-view [data-v="raw"]').click();
  await expect(dlg.locator('.lf-table')).toHaveCount(0);
  const lines = await dlg.locator(ROWS).allTextContents();
  // newest on top: the failed open above the open before it, the start last
  const failed = lines.findIndex(
    (l) =>
      l.includes('ERROR') && l.includes('File Open Failed: ') && l.includes('damaged.xmpuzzle'),
  );
  const opened = lines.findIndex(
    (l) =>
      l.includes('INFO') && l.includes(`File Opened: "${resolve(FIXTURES, 'BallRoom.xmpuzzle')}"`),
  );
  const started = lines.findIndex((l) => l.includes('[app] App Started: ShapeLogic'));
  expect(failed).toBeGreaterThanOrEqual(0);
  expect(opened).toBeGreaterThan(failed);
  expect(started).toBeGreaterThan(opened);
  await expect(dlg.locator('.lf-row.lf-error').first()).toContainText('File Open Failed');

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
  // the text marked in each line, in both views
  await expect(dlg.locator(`${ROWS} mark`).first()).toHaveText('BallR');
  await dlg.locator('.lf-view [data-v="pretty"]').click();
  await expect(dlg.locator(`.lf-table ${ROWS} mark`).first()).toHaveText('BallR');
  await dlg.locator('.lf-view [data-v="raw"]').click();
  // the click took the focus: back to the end of the search's text
  await search.focus();
  await search.press('End');
  await search.pressSequentially('zzz');
  await expect(dlg.locator('.lf-empty')).toContainText('No line has “ballrzzz” in it.');
  for (let i = 0; i < 3; i++) {
    await search.press('Backspace');
  }
  await expect(dlg.locator(ROWS).first()).toContainText('BallRoom');
  await search.fill('');
  await expect(dlg.locator(ROWS)).toHaveCount(lines.length);

  // where to report a problem, at the bottom left
  await expect(dlg.locator('.dlg-foot')).toHaveText(
    'Copy or export relevant log lines to open an issue in GitHub Issues or as an email to shapelogic.xyz@gmail.com',
  );
  await expect(dlg.locator('.dlg-foot a')).toHaveText('GitHub Issues');

  // Export: a short log offers all of it alone, with its size; Esc closes
  // the choices, not the dialog
  const out = join(dir, 'exported');
  await answerDialogs(page, { save: out });
  await dlg.locator('[data-x="export"]').click();
  // the popover, above the dialog (a menu of the dialog)
  const choices = page.locator('#pop [role="menuitem"]');
  await expect(choices).toHaveCount(1);
  await expect(choices.first()).toHaveText(/^All \d+ lines \(.+\)$/);
  await page.keyboard.press('Escape');
  await expect(page.locator('#pop')).toBeHidden();
  await expect(dlg).toBeVisible();
  // picked: all of it, as plain text, to the file the save dialog gives
  await dlg.locator('[data-x="export"]').click();
  await choices.first().click();
  await expect(page.locator('#stText')).toContainText('Log exported');
  const text = readFileSync(`${out}.txt`, 'utf8');
  expect(text).toContain('damaged.xmpuzzle');
  expect(text).toContain('App Started');

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
  await openWith(page, 'Debug Logging On');
  const dlg = page.locator('#logFiles');
  await expect(dlg.locator('.lf-sum')).toContainText('debug logging on');
  await dlg.locator('[data-x="close"]').click();
  await switchDebug(page);
  await openWith(page, 'Debug Logging Off');
  await expect(dlg.locator('.lf-sum')).not.toContainText('debug logging on');
  // the start, both changes: a short log is kept whole
  await expect(dlg.locator(ROWS).last()).toContainText('App Started');
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
  await openWith(page, 'App Started');
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

  // Export… offers the last 100 lines and all of them; the last 100 written
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-logexport-'));
  try {
    const out = join(dir, 'last.txt');
    await answerDialogs(page, { save: out });
    await dlg.locator('[data-x="export"]').click();
    const choices = page.locator('#pop [role="menuitem"]');
    await expect(choices).toHaveText([
      /^Last 100 lines \(\d.* KB\)$/,
      new RegExp(`^All ${String(all.length)} lines \\(\\d.* KB\\)$`),
    ]);
    // the menu takes the arrow keys (round at the ends) and Tab before the dialog
    await expect(choices.first()).toBeFocused();
    await page.keyboard.press('ArrowDown');
    await expect(choices.nth(1)).toBeFocused();
    await page.keyboard.press('ArrowDown');
    await expect(choices.first()).toBeFocused();
    await page.keyboard.press('ArrowUp');
    await expect(choices.nth(1)).toBeFocused();
    await page.keyboard.press('Tab');
    await expect(page.locator('#pop')).toBeHidden();
    await expect(dlg.locator('[data-x="export"]')).toBeFocused();
    await expect(dlg).toBeVisible();
    await dlg.locator('[data-x="export"]').click();
    await choices.first().click();
    await expect(page.locator('#stText')).toContainText('Log exported');
    const text = readFileSync(out, 'utf8');
    expect(text.split('\n').filter((l) => l !== '')).toHaveLength(100);
    expect(text).not.toContain('earlier line 0\n');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
