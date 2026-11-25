// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reference screenshots of three dialogs (cleanup item 13.1, D3): the
// Settings window, File ▸ New and File ▸ Export ▸ STL, each in the light
// theme at the Standard density and in the dark theme at Minimal, so a
// rule that moves between sheets (styles.css, a module's own) and loses to
// another shows here. Recorded with `npx playwright test --update-snapshots
// tests/e2e/dialogs.e2e.ts` before the move, on styles.css as it was.

import { expect, test } from '@playwright/test';
import type { Locator, Page } from '@playwright/test';

import { BUILT, ENGINE, launch, menu, openFixture, selectShape, settle, shapes } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;

test.beforeAll(async () => {
  a = await launch();
  await openFixture(a.page, 'CubeInCage.xmpuzzle');
});

test.afterAll(async () => {
  await a.close();
});

// the looks each dialog is taken in, set as the settings set them (shell/theme.ts)
const LOOKS = [
  { name: 'light', density: 'standard', theme: 'light' },
  { name: 'dark-minimal', density: 'minimal', theme: 'dark' },
] as const;

async function shoot(page: Page, dlg: Locator, name: string, mask: Locator[] = []): Promise<void> {
  for (const look of LOOKS) {
    await page.evaluate((l) => {
      document.documentElement.dataset.density = l.density;
      document.documentElement.dataset.theme = l.theme;
    }, look);
    await page.mouse.move(2, 2);
    await settle(page);
    await expect(dlg).toHaveScreenshot(`dialog-${name}-${look.name}.png`, { mask });
  }
  await page.evaluate(() => {
    document.documentElement.dataset.density = 'standard';
    delete document.documentElement.dataset.theme;
  });
}

test('the Settings window', async () => {
  const { page } = a;
  await menu(page, 'settings');
  const dlg = page.locator('.dlg[aria-label="Settings"]');
  await expect(dlg).toBeVisible();
  await shoot(page, dlg, 'settings');
  await page.keyboard.press('Escape');
  await expect(dlg).toBeHidden();
});

test('File ▸ New', async () => {
  const { page } = a;
  await menu(page, 'new');
  const dlg = page.locator('#newPuzzle');
  await expect(dlg).toBeVisible();
  await shoot(page, dlg.locator('.cfm'), 'new');
  await page.keyboard.press('Escape');
  await expect(dlg).toBeHidden();
});

test('File ▸ Export ▸ STL', async () => {
  const { page } = a;
  const shape = (await shapes(page)).find((s) => s.label === 'S2');
  await selectShape(page, shape?.id ?? '');
  await menu(page, 'export.stl');
  const dlg = page.locator('#exportStl');
  await expect(dlg).toBeVisible();
  await expect(dlg).toHaveAttribute('data-busy', '0');
  // the preview is the 3D view's look, taken by the view's own references
  await shoot(page, dlg.locator('.cfm'), 'stl', [dlg.locator('canvas')]);
  await page.keyboard.press('Escape');
  await expect(dlg).toBeHidden();
});
