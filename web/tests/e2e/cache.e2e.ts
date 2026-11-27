// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings ▸ General ▸ Cache folder (main/cache-folder.ts): Choose Folder…
// makes a ShapeLogic folder in the folder the dialog gave, says what the
// folders in use hold and moves it there; Use Default brings it back; a
// folder that cannot be written is refused and the folders in use stay; a
// solve whose search cannot write the cache folder pauses and asks what to
// do (decision 0028). View ▸ Manage Cache and Autosave Files… lists what
// the folders hold, shows an item's details and deletes it; at the start,
// once a run, a notice names what they hold (not the crash recovery copy),
// with Manage Files…, Delete All… and Ignore.
// The folder dialog is answered in the main process; the default cache
// folder is the test's data directory (SHAPELOGIC_USER_DATA).

import {
  chmodSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  rmSync,
  utimesSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { appOf, ENGINE, BUILT, engineCall, launch, menu, nextWindow, openFixture } from './app.ts';
import type { App } from './app.ts';
import { runButton, runEnded, solverSettled, toSolver } from './solver.ts';

let a: App;
let dir: string;

test.beforeEach(async () => {
  a = await launch();
  // The start's look at the cache folder is over before a test puts files
  // there: the main process lists it once a run, for whoever asks first,
  // and the page asks some time after the engine runs. A test that wrote a
  // saved search before that got the notice at the start over its window
  // (a busy machine), and its click on Settings never landed.
  await a.page.evaluate(() =>
    (
      window as unknown as { btApp: { cacheItemsAtStart(): Promise<unknown> } }
    ).btApp.cacheItemsAtStart(),
  );
  dir = mkdtempSync(join(tmpdir(), 'bt-e2e-cache-'));
});

test.afterEach(async () => {
  await a.close();
  chmodSync(dir, 0o755);
  rmSync(dir, { recursive: true, force: true });
});

/** The folder dialog answers this folder. */
async function dialogGives(page: Page, folder: string): Promise<void> {
  await appOf(page).evaluate(({ dialog }, f) => {
    dialog.showOpenDialog = () => Promise.resolve({ canceled: false, filePaths: [f] });
  }, folder);
}

/** Settings ▸ General, at the cache folder's row. */
async function openRow(page: Page): Promise<void> {
  await page.click('#btnSettings');
  await page.click('#setNav [data-p="general"]');
  await expect(page.locator('#setPg [data-cache="choose"]')).toBeVisible();
}

const KEY = '0123456789abcdef';

test('a chosen folder gets the data moved into it, and Use Default brings it back', async () => {
  const { page, userData } = a;
  // a saved stacking search in the default folder
  const saved = join(userData, 'panex', KEY);
  mkdirSync(saved, { recursive: true });
  writeFileSync(join(saved, 'state'), 'x'.repeat(1200));
  await openRow(page);
  await expect(page.locator('#setPg [data-cache="default"]')).toBeDisabled();
  await dialogGives(page, dir);
  await page.click('#setPg [data-cache="choose"]');
  const ask = page.locator('#cacheData');
  await expect(ask).toContainText('1 saved stacking search (1.2 KB)');
  // the folder is made before the question is answered
  expect(existsSync(join(dir, 'ShapeLogic', 'sliding'))).toBe(true);
  await ask.locator('[data-x="move"]').click();
  await expect(ask).toHaveCount(0);
  await expect(page.locator('#setPg .cachepaths')).toContainText(join(dir, 'ShapeLogic'));
  expect(existsSync(join(dir, 'ShapeLogic', 'panex', KEY, 'state'))).toBe(true);
  expect(existsSync(saved)).toBe(false);
  // the engine was told
  expect(await engineCall(page, 'settings.get')).toMatchObject({
    panexFolder: join(dir, 'ShapeLogic', 'panex'),
  });

  await page.click('#setPg [data-cache="default"]');
  await page.locator('#cacheData [data-x="move"]').click();
  await expect(page.locator('#setPg [data-cache="default"]')).toBeDisabled();
  expect(existsSync(join(userData, 'panex', KEY, 'state'))).toBe(true);
  expect(existsSync(join(dir, 'ShapeLogic'))).toBe(false);
});

test('a folder that cannot be written is refused, and the folders in use stay', async () => {
  test.skip(process.platform === 'win32', 'folder permissions differ on Windows');
  const { page } = a;
  chmodSync(dir, 0o555);
  await openRow(page);
  await dialogGives(page, dir);
  await page.click('#setPg [data-cache="choose"]');
  const notice = page.locator('#notice');
  await expect(notice).toContainText('The new cache destination is invalid');
  await expect(notice).toContainText('permission');
  await notice.locator('[data-x="ok"]').click();
  await expect(page.locator('#setPg [data-cache="default"]')).toBeDisabled();
  expect(existsSync(join(dir, 'ShapeLogic'))).toBe(false);
});

test('a solve whose cache folder cannot be written pauses and asks what to do', async () => {
  test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');
  const { page } = a;
  // a cache folder chosen, then its sliding folder a file: a drive that is
  // gone, say
  await openRow(page);
  await dialogGives(page, dir);
  await page.click('#setPg [data-cache="choose"]');
  await expect(page.locator('#setPg .cachepaths')).toContainText(join(dir, 'ShapeLogic'));
  await page.keyboard.press('Escape');
  const gone = join(dir, 'ShapeLogic', 'sliding');
  rmSync(gone, { recursive: true, force: true });
  writeFileSync(gone, 'x');
  await openFixture(page, 'test_sliding_solver.xmpuzzle');
  await toSolver(page);
  await page.click('#slvMode [data-m="dis"]');
  await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
  await solverSettled(page);
  await runButton(page, 'start');
  const ask = page.locator('#cacheUnwritable');
  await expect(ask).toContainText(gone);
  await runEnded(page, 'paused');
  // an error: View Logs is among its buttons
  await expect(ask.locator('[data-x="viewLogs"]')).toHaveText('View Logs');

  // the drive is back: Try Again carries the solve on to its end
  rmSync(gone);
  await ask.locator('[data-x="retry"]').click();
  await expect(ask).toHaveCount(0);
  await runEnded(page, 'finished');
  expect(Number(await page.getAttribute('#leftS', 'data-solutions'))).toBeGreaterThan(0);

  // gone again: Choose Folder… gives the cache another folder, and the
  // solve goes on there
  rmSync(gone, { recursive: true, force: true });
  writeFileSync(gone, 'x');
  await runButton(page, 'start');
  await expect(ask).toContainText(gone);
  const other = join(dir, 'other');
  mkdirSync(other);
  await dialogGives(page, other);
  await ask.locator('[data-x="choose"]').click();
  await expect(ask).toHaveCount(0);
  await runEnded(page, 'finished');
  expect(await engineCall(page, 'settings.get')).toMatchObject({
    slideFolder: join(other, 'ShapeLogic', 'sliding'),
  });
});

test('Manage Cache and Autosave Files lists what the folders hold, shows an item and deletes it', async () => {
  const { page, userData } = a;
  // a sliding search saved by a version that did not say whose it was, and a stray file
  const saved = join(userData, 'sliding', `slide-${KEY}`);
  mkdirSync(saved, { recursive: true });
  writeFileSync(join(saved, 'state.bin'), 'x'.repeat(2500));
  writeFileSync(join(saved, 'f1.lvl'), 'x'.repeat(500));
  // an hour older than the stray file: the list has the newest first
  const hourAgo = new Date(Date.now() - 3_600_000);
  for (const p of [join(saved, 'state.bin'), join(saved, 'f1.lvl'), saved]) {
    utimesSync(p, hourAgo, hourAgo);
  }
  mkdirSync(join(userData, 'panex'), { recursive: true });
  writeFileSync(join(userData, 'panex', 'state.tmp'), 'half');
  await menu(page, 'cacheFiles');
  const dlg = page.locator('#cacheFiles');
  const rows = dlg.locator('.cf-row');
  await expect(rows).toHaveCount(2);
  await expect(dlg.locator('.cf-total')).toHaveText('2 items, 3.0 KB in all');
  const search = rows.filter({ hasText: 'Paused sliding solve (puzzle not known)' });
  await search.click();
  await expect(search).toHaveAttribute('aria-selected', 'true');
  const detail = dlg.locator('.cf-detail');
  await expect(detail.locator('h4')).toHaveText('Paused sliding solve (puzzle not known)');
  await expect(detail).toContainText('3.0 KB (3,000 bytes)');
  await expect(detail).toContainText('Not known');
  await expect(detail.locator('.cf-files')).toContainText(join(saved, 'state.bin'));
  await expect(detail.locator('.cf-files')).toContainText(join(saved, 'f1.lvl'));
  // the arrow keys move the selection
  await dlg.locator('.cf-list').press('ArrowUp');
  await expect(detail.locator('h4')).toHaveText('Other files in the stacking search folder');
  await dlg.locator('.cf-list').press('ArrowDown');
  await expect(detail.locator('h4')).toHaveText('Paused sliding solve (puzzle not known)');

  // Delete asks first; Cancel keeps it
  await detail.locator('[data-act="delete"]').click();
  const ask = page.locator('#cacheFilesDelete');
  await expect(ask).toContainText('Its 2 files (3.0 KB) will be deleted');
  await ask.locator('[data-x="cancel"]').click();
  expect(existsSync(saved)).toBe(true);
  await detail.locator('[data-act="delete"]').click();
  await ask.locator('[data-x="delete"]').click();
  await expect(rows).toHaveCount(1);
  expect(existsSync(saved)).toBe(false);
  // the other item is selected now; Refresh finds what came meanwhile
  await expect(detail.locator('h4')).toHaveText('Other files in the stacking search folder');
  writeFileSync(join(userData, 'sliding', 'stray'), 'x');
  await dlg.locator('[data-x="refresh"]').click();
  await expect(rows).toHaveCount(2);
  await dlg.locator('[data-x="close"]').click();
  await expect(dlg).toHaveCount(0);
});

test('the start names what the cache folder holds: Manage Files…, Delete All… and Ignore', async () => {
  // a saved stacking search, a sliding search's files never saved, and a
  // crash recovery copy (which its own offer is about: left out)
  const userData = mkdtempSync(join(tmpdir(), 'bt-e2e-cachestart-'));
  const saved = join(userData, 'panex', KEY);
  mkdirSync(saved, { recursive: true });
  writeFileSync(join(saved, 'state'), 'x'.repeat(1200));
  const slide = join(userData, 'sliding', `slide-${KEY}`);
  mkdirSync(slide, { recursive: true });
  writeFileSync(join(slide, 'f1.lvl'), 'x'.repeat(800));
  let b = await launch({ userData });
  try {
    // Ignore leaves them
    let notice = b.page.locator('#cacheAtStart');
    await expect(notice.locator('.cn-list li')).toHaveCount(2);
    await expect(notice).toContainText('These 2 items were left in the cache folder');
    await expect(notice.locator('.cn-tip')).toHaveText(
      'Tip: These files can be managed using View ▸ Manage Cache and Autosave Files.',
    );
    await notice.locator('[data-x="ignore"]').click();
    await expect(notice).toHaveCount(0);
    expect(existsSync(saved)).toBe(true);
    // once a run: a new window does not name them again
    const w = await nextWindow(b.app, () => menu(b.page, 'newWindow'));
    await w.waitForTimeout(500);
    await expect(w.locator('#cacheAtStart')).toHaveCount(0);
    await b.close();

    // Manage Files… opens the manager
    b = await launch({ userData });
    notice = b.page.locator('#cacheAtStart');
    await notice.locator('[data-x="manage"]').click();
    await expect(notice).toHaveCount(0);
    await expect(b.page.locator('#cacheFiles .cf-row')).toHaveCount(2);
    await b.page.locator('#cacheFiles [data-x="close"]').click();
    await b.close();

    // Delete All… asks first; Cancel keeps them, Delete All deletes them
    b = await launch({ userData });
    notice = b.page.locator('#cacheAtStart');
    await notice.locator('[data-x="deleteAll"]').click();
    const ask = b.page.locator('#cacheAtStartDelete');
    await expect(ask).toContainText('All 2 items (2.0 KB) will be deleted');
    await ask.locator('[data-x="cancel"]').click();
    await expect(notice).toBeVisible();
    expect(existsSync(saved)).toBe(true);
    await notice.locator('[data-x="deleteAll"]').click();
    await ask.locator('[data-x="delete"]').click();
    await expect(notice).toHaveCount(0);
    await expect(b.page.locator('#stText')).toContainText('Deleted 2 items');
    expect(existsSync(saved)).toBe(false);
    expect(existsSync(slide)).toBe(false);
    await b.close();

    // nothing left: no notice
    b = await launch({ userData });
    await b.page.waitForTimeout(500);
    await expect(b.page.locator('#cacheAtStart')).toHaveCount(0);
  } finally {
    await b.close();
    rmSync(userData, { recursive: true, force: true });
  }
});
