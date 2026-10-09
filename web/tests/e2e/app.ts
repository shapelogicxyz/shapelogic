// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app for the e2e tests: the build in out/ launched by Playwright's
// Electron driver with the release engine (or BT_ENGINE), its window at a
// fixed size, and the UI actions the tests share (open a fixture by the
// temporary Open button, pick a shape, change the display).

import { existsSync } from 'node:fs';
import { resolve } from 'node:path';

import { _electron as electron, expect } from '@playwright/test';
import type { ElectronApplication, Page } from '@playwright/test';

export const WEB = resolve(import.meta.dirname, '../..');
export const ROOT = resolve(WEB, '..');
export const FIXTURES = resolve(ROOT, 'tests/data/puzzles');

/** The engine the app runs, or null (the tests are skipped). */
export const ENGINE: string | null = (() => {
  const fromEnv = process.env.BT_ENGINE;
  if (fromEnv !== undefined && fromEnv !== '') {
    return resolve(fromEnv);
  }
  const path = resolve(ROOT, 'build/release/engine/bt-engine');
  return existsSync(path) ? path : null;
})();

/** Whether the app is built. */
export const BUILT = existsSync(resolve(WEB, 'out/main/index.js'));

/** The window's content size of every test (screenshots depend on it). */
export const WINDOW = { width: 1280, height: 800 } as const;

export interface App {
  app: ElectronApplication;
  page: Page;
  close(): Promise<void>;
}

/** Launches the app and waits for its engine to run. */
export async function launch(): Promise<App> {
  // an editor's terminal may set ELECTRON_RUN_AS_NODE, which turns Electron into Node
  const env = Object.fromEntries(
    Object.entries(process.env).filter(
      (e): e is [string, string] => e[0] !== 'ELECTRON_RUN_AS_NODE' && e[1] !== undefined,
    ),
  );
  env.BT_ENGINE = ENGINE ?? '';
  const app = await electron.launch({ args: [resolve(WEB, 'out/main/index.js')], cwd: WEB, env });
  const page = await app.firstWindow();
  await app.evaluate(({ BrowserWindow }, size) => {
    BrowserWindow.getAllWindows()[0]?.setContentSize(size.width, size.height);
  }, WINDOW);
  await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'running');
  return {
    app,
    page,
    close: () => app.close(),
  };
}

/** Opens a fixture of tests/data/puzzles and waits for its first shape to be drawn. */
export async function openFixture(page: Page, name: string): Promise<void> {
  await page.setInputFiles('#openFile', resolve(FIXTURES, name));
  await expect(page.locator('#fname')).toHaveText(name);
  const first = page.locator('#shapeList .row').first();
  await expect(first).toHaveClass(/sel/);
  const id = await first.getAttribute('data-id');
  await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`^${id ?? ''}:`));
}

/** The shapes of the open document as the picker lists them: id and label. */
export async function shapes(page: Page): Promise<{ id: string; label: string }[]> {
  return page.$$eval('#shapeList .row', (rows) =>
    rows.map((r) => ({
      id: (r as HTMLElement).dataset.id ?? '',
      label: r.querySelector('.chip')?.textContent ?? '',
    })),
  );
}

/** Shows a shape and waits until its mesh is drawn in the current style. */
export async function selectShape(page: Page, id: string): Promise<void> {
  await page.click(`#shapeList .row[data-id="${id}"]`);
  await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`^${id}:`));
  await settle(page);
}

/** Chooses an item of the Display menu, and closes the menu. */
export async function display(page: Page, key: string): Promise<void> {
  await page.click('#bDisp');
  await page.click(`#pop [data-k="${key}"]`);
  await page.keyboard.press('Escape');
  await expect(page.locator('#pop')).toBeHidden();
}

/** The style the view draws now. */
export async function style(page: Page, s: 'drawing' | 'wireframe' | 'flat'): Promise<void> {
  await display(page, s);
  await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`:${s}$`));
  await settle(page);
}

/** Switches a setting of the settings dialog's 3D view page. */
export async function setting3d(page: Page, key: string): Promise<void> {
  await page.click('#btnSettings');
  await page.click('#setNav [data-p="view"]');
  await page.click(`#setPg .switch[data-k="${key}"]`);
  await page.keyboard.press('Escape');
  await expect(page.locator('#scrim')).not.toHaveClass(/show/);
}

/** Two animation frames of the page. */
async function frames(page: Page): Promise<void> {
  await page.evaluate(
    () =>
      new Promise<void>((done) => {
        requestAnimationFrame(() => {
          requestAnimationFrame(() => {
            done();
          });
        });
      }),
  );
}

/**
 * Waits until the camera rests and two frames were drawn after: frames
 * first, so a move just asked for has shown itself as moving.
 */
export async function settle(page: Page): Promise<void> {
  await frames(page);
  await expect(page.locator('#vp')).toHaveAttribute('data-moving', '0');
  await frames(page);
}

/** The camera's state: yaw, pitch, roll, zoom, px, py. */
export async function camera(page: Page): Promise<number[]> {
  const v = (await page.getAttribute('#vp', 'data-camera')) ?? '';
  return v.split(',').map(Number);
}
