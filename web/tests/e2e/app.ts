// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app for the e2e tests: the build in out/ launched by Playwright's
// Electron driver with the release engine (or BT_ENGINE), its window at a
// fixed size, and the UI actions the tests share (open a fixture by the
// temporary Open button, a new puzzle by the temporary New button, pick a
// shape, change the display); `engineCall` asks the app's own engine
// through the page's bridge (what the engine holds, not what the UI
// shows), and `launch({ callLog })` has the main process log every call
// of the renderer with how it ended (BT_CALL_LOG).

import { existsSync, readFileSync } from 'node:fs';
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

/** Launches the app and waits for its engine to run; `callLog` is BT_CALL_LOG's file. */
export async function launch(opts: { callLog?: string } = {}): Promise<App> {
  // an editor's terminal may set ELECTRON_RUN_AS_NODE, which turns Electron into Node
  const env = Object.fromEntries(
    Object.entries(process.env).filter(
      (e): e is [string, string] => e[0] !== 'ELECTRON_RUN_AS_NODE' && e[1] !== undefined,
    ),
  );
  env.BT_ENGINE = ENGINE ?? '';
  if (opts.callLog !== undefined) {
    env.BT_CALL_LOG = opts.callLog;
  }
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

/** A call of the renderer as the main process logged it (BT_CALL_LOG). */
export interface LoggedCall {
  id: number;
  method: string;
  errc: string | null;
}

/** The ids engineCall gives its calls, above any the page's proxy reaches. */
const BRIDGE_IDS = 1_000_000_000;

/** The calls of the app a call log holds (the test's own engineCall calls left out). */
export function readCallLog(path: string): LoggedCall[] {
  if (!existsSync(path)) {
    return [];
  }
  return readFileSync(path, 'utf8')
    .split('\n')
    .filter((l) => l !== '')
    .map((l) => JSON.parse(l) as LoggedCall)
    .filter((c) => c.id < BRIDGE_IDS);
}

let bridgeIds = BRIDGE_IDS;

/**
 * Calls the app's own engine through the page's bridge, with an id the
 * page's proxy never uses (its reply is ignored there; the events reach
 * the app as any other). Not for methods whose reply holds a frame.
 */
export async function engineCall<T = unknown>(
  page: Page,
  method: string,
  params?: unknown,
): Promise<T> {
  const id = bridgeIds++;
  return page.evaluate(
    ([id, method, params]) =>
      new Promise<T>((done, fail) => {
        const off = window.bt.onMessage((m) => {
          if ((m.kind === 'reply' || m.kind === 'error') && m.id === id) {
            off();
            if (m.kind === 'reply') {
              done(m.result as T);
            } else {
              fail(new Error(`${m.errc}: ${m.message}`));
            }
          }
        });
        window.bt.send({
          kind: 'call',
          id,
          method,
          ...(params === undefined ? {} : { params }),
        });
      }),
    [id, method, params] as const,
  );
}

/** A new empty puzzle on a grid, by the top bar's temporary New button. */
export async function newDocument(page: Page, grid: string): Promise<void> {
  const before = await page.locator('#stage').getAttribute('data-generation');
  await page.click('#btnNewDoc');
  await page.click(`#pop [data-k="${grid}"]`);
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
  await expect(page.locator('#shapeList .row')).toHaveCount(0);
}
