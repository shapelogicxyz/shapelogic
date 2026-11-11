// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings ▸ General ▸ Cache folder (main/cache-folder.ts): Choose Folder…
// makes a ShapeLogic folder in the folder the dialog gave, says what the
// folders in use hold and moves it there; Use Default brings it back; a
// folder that cannot be written is refused and the folders in use stay.
// The folder dialog is answered in the main process; the default cache
// folder is the test's data directory (BT_USER_DATA).

import { chmodSync, existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { appOf, engineCall, launch } from './app.ts';
import type { App } from './app.ts';

let a: App;
let dir: string;

test.beforeEach(async () => {
  a = await launch();
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
