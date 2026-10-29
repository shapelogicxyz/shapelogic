// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app for the e2e tests: the build in out/ launched by Playwright's
// Electron driver with the release engine (or BT_ENGINE), its window at a
// fixed size and its data (the stored settings, the rescue copy) in a
// directory of its own, and the UI actions the tests share (open a
// fixture as File ▸ Open Recent does, a new puzzle by File ▸ New, pick a
// shape, change the display, click a menu item, answer the file dialogs);
// `engineCall` asks the app's own engine through the page's bridge (what
// the engine holds, not what the UI shows), and `launch({ callLog })` has
// the main process log every call of the renderer with how it ended
// (BT_CALL_LOG).

import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

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
  const path = resolve(ROOT, 'build/release/engine/shapelogic_engine');
  return existsSync(path) ? path : null;
})();

/** Whether the app is built. */
export const BUILT = existsSync(resolve(WEB, 'out/main/index.js'));

/** The window's content size of every test (screenshots depend on it). */
export const WINDOW = { width: 1280, height: 800 } as const;

export interface App {
  app: ElectronApplication;
  page: Page;
  /** The app's data directory (BT_USER_DATA). */
  userData: string;
  close(): Promise<void>;
}

const apps = new WeakMap<Page, ElectronApplication>();

/** The app a page belongs to. */
export function appOf(page: Page): ElectronApplication {
  const app = apps.get(page);
  if (app === undefined) {
    throw new Error('the page was not launched by launch()');
  }
  return app;
}

/** The stylesheet that hides every scrollbar in the app under test. */
const NO_SCROLLBARS = '::-webkit-scrollbar{display:none}';

/**
 * Launches the app and waits for its engine to run; `callLog` is
 * BT_CALL_LOG's file, `autosave` the Solver's autosave interval in seconds
 * (BT_AUTOSAVE_SECONDS; 20 minutes without), `userData` the app's data
 * directory (a new one, removed on close, unless given: a restart keeps
 * its settings in the one given), `args` the app's command line after its
 * main script (a file to open).
 */
export async function launch(
  opts: { callLog?: string; autosave?: number; userData?: string; args?: string[] } = {},
): Promise<App> {
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
  if (opts.autosave !== undefined) {
    env.BT_AUTOSAVE_SECONDS = String(opts.autosave);
  }
  const userData = opts.userData ?? mkdtempSync(join(tmpdir(), 'bt-e2e-data-'));
  env.BT_USER_DATA = userData;
  // the reference screenshots are taken in sRGB whatever display the window
  // lands on (Playwright's own Chromium does the same)
  const app = await electron.launch({
    args: ['--force-color-profile=srgb', resolve(WEB, 'out/main/index.js'), ...(opts.args ?? [])],
    cwd: WEB,
    env,
  });
  const page = await app.firstWindow();
  apps.set(page, app);
  // an uncaught exception in the main process would show Electron's dialog
  // and hold the app, so a test would end in "the app did not quit" and say
  // nothing of the cause (the stage 6 audit's stall): recorded instead, and
  // reported by closeApp
  await app.evaluate(() => {
    const g = globalThis as unknown as { mainErrors?: string[] };
    g.mainErrors = [];
    process.on('uncaughtException', (e: unknown) => {
      g.mainErrors?.push(e instanceof Error ? (e.stack ?? e.message) : String(e));
    });
  });
  await app.evaluate(({ BrowserWindow }, size) => {
    BrowserWindow.getAllWindows()[0]?.setContentSize(size.width, size.height);
  }, WINDOW);
  // no scrollbars: macOS shows them taking room, or only while scrolling,
  // by its setting and whether a mouse is connected, so a page laid out
  // with them would depend on the host (every window, also those opened later)
  await app.evaluate(({ app, BrowserWindow }, css) => {
    const hide = (wc: Electron.WebContents): void => {
      void wc.insertCSS(css);
    };
    for (const w of BrowserWindow.getAllWindows()) {
      hide(w.webContents);
    }
    app.on('web-contents-created', (_e, wc) => {
      wc.on('dom-ready', () => {
        hide(wc);
      });
    });
  }, NO_SCROLLBARS);
  await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'running');
  // the first document shown
  await expect(page.locator('#stage')).toHaveAttribute('data-revision', /\d/);
  return {
    app,
    page,
    userData,
    close: async () => {
      await closeApp(app);
      if (opts.userData === undefined) {
        rmSync(userData, { recursive: true, force: true });
      }
    },
  };
}

/**
 * Ends the app without the question about unsaved changes (a test's
 * document is thrown away): the windows are destroyed, then the app closes.
 * An exception the main process did not catch (launch records them) fails
 * the test with its stack.
 */
export async function closeApp(app: ElectronApplication): Promise<void> {
  const errors = await app
    .evaluate(() => (globalThis as unknown as { mainErrors?: string[] }).mainErrors ?? [])
    .catch((): string[] => []);
  await app
    .evaluate(({ BrowserWindow }) => {
      for (const w of BrowserWindow.getAllWindows()) {
        w.destroy();
      }
    })
    .catch(() => undefined);
  // an app that does not quit is a failure of the test, said so, not a hang
  let timer: ReturnType<typeof setTimeout> | undefined;
  const quit = await Promise.race([
    app.close().then(() => true),
    new Promise<boolean>((done) => {
      timer = setTimeout(() => {
        done(false);
      }, 20_000);
    }),
  ]);
  clearTimeout(timer);
  if (!quit) {
    const pid = app.process().pid;
    if (pid !== undefined) {
      process.kill(pid, 'SIGKILL');
    }
    throw new Error(
      `the app did not quit within 20 s of its windows going${errors.length === 0 ? '' : `; the main process threw: ${errors.join('\n')}`}`,
    );
  }
  if (errors.length > 0) {
    throw new Error(`the main process threw: ${errors.join('\n')}`);
  }
}

/**
 * Sends the window a menu command (as Electron's menu does). A file to open,
 * `{ cmd: 'openPath', path }`, goes through the main process as a file the
 * system opens with the app does (macOS's open-file), so the window is
 * handed it (main/path-grants.ts) as Open Recent hands it.
 */
export async function command(page: Page, c: Record<string, unknown>): Promise<void> {
  await appOf(page).evaluate(({ app, BrowserWindow }, cmd) => {
    if (cmd.cmd === 'openPath') {
      app.emit('open-file', { preventDefault: () => undefined }, cmd.path);
      return;
    }
    BrowserWindow.getAllWindows()[0]?.webContents.send('bt:command', cmd);
  }, c);
}

/** Clicks a menu item by its id (main/menu.ts), as a user would. */
export async function menu(page: Page, id: string): Promise<void> {
  const found = await appOf(page).evaluate(({ Menu }, itemId) => {
    const item = Menu.getApplicationMenu()?.getMenuItemById(itemId);
    if (!item?.enabled) {
      return false;
    }
    (item.click as () => void)();
    return true;
  }, id);
  if (!found) {
    throw new Error(`no enabled menu item ${id}`);
  }
}

/** Whether a menu item is there and enabled, and its label. */
export async function menuItem(
  page: Page,
  id: string,
): Promise<{ enabled: boolean; label: string; checked: boolean } | null> {
  return appOf(page).evaluate(({ Menu }, itemId) => {
    const item = Menu.getApplicationMenu()?.getMenuItemById(itemId);
    return item === null || item === undefined
      ? null
      : { enabled: item.enabled, label: item.label, checked: item.checked };
  }, id);
}

/**
 * The file dialogs answer with these paths (null: cancelled), as a user
 * would choose them, until they are answered again.
 */
export async function answerDialogs(
  page: Page,
  answers: { open?: string | null; save?: string | null },
): Promise<void> {
  await appOf(page).evaluate(({ dialog }, a) => {
    if (a.open !== undefined) {
      const path = a.open;
      dialog.showOpenDialog = () =>
        Promise.resolve({
          canceled: path === null,
          filePaths: path === null ? [] : [path],
        });
    }
    if (a.save !== undefined) {
      const path = a.save;
      dialog.showSaveDialog = () =>
        Promise.resolve({
          canceled: path === null,
          filePath: path ?? '',
        });
    }
  }, answers);
}

/**
 * The question about unsaved changes, if the document has them: answered
 * Don't Save (a test's edits are thrown away when it opens another file).
 */
async function dropChanges(page: Page): Promise<void> {
  if ((await page.locator('#stage').getAttribute('data-dirty')) === '1') {
    await page.click('#discard [data-x="drop"]');
  }
}

/**
 * Opens a fixture of tests/data/puzzles (as File ▸ Open Recent does, unsaved
 * changes given up) and waits for its first shape to be drawn.
 */
export async function openFixture(page: Page, name: string): Promise<void> {
  const before = await page.locator('#stage').getAttribute('data-generation');
  await command(page, { cmd: 'openPath', path: resolve(FIXTURES, name) });
  await dropChanges(page);
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
  await expect(page.locator('#fname')).toHaveText(name);
  const first = page.locator(SHAPE_ROWS).first();
  await expect(first).toHaveClass(/sel/);
  const id = await first.getAttribute('data-id');
  await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`^${id ?? ''}:`));
}

/** Every shape's row: a sliding puzzle's Boards list, then the Shapes list. */
export const SHAPE_ROWS = '#boardList .row, #shapeList .row';

/** The shapes of the open document as the picker lists them: id and label. */
export async function shapes(page: Page): Promise<{ id: string; label: string }[]> {
  return page.$$eval(SHAPE_ROWS, (rows) =>
    rows.map((r) => ({
      id: (r as HTMLElement).dataset.id ?? '',
      label: r.querySelector('.chip')?.textContent ?? '',
    })),
  );
}

/** Shows a shape and waits until its mesh is drawn in the current style. */
export async function selectShape(page: Page, id: string): Promise<void> {
  await page.click(`:is(#boardList, #shapeList) .row[data-id="${id}"] .chip`);
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
export async function style(
  page: Page,
  s: 'drawing' | 'wireframe' | 'flat' | 'stl',
): Promise<void> {
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

/** A new empty puzzle on a grid, by File ▸ New (unsaved changes given up). */
export async function newDocument(page: Page, grid: string): Promise<void> {
  const before = await page.locator('#stage').getAttribute('data-generation');
  await menu(page, 'new');
  await dropChanges(page);
  await page.click(`#newPuzzle [data-grid="${grid}"]`);
  await page.click('#newPuzzle [data-x="ok"]');
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
  await expect(page.locator(SHAPE_ROWS)).toHaveCount(0);
}

/** The engines an app's main process runs (their pids). */
export function engines(mainPid: number): number[] {
  try {
    return execFileSync('pgrep', ['-P', String(mainPid), 'shapelogic_engine'], { encoding: 'utf8' })
      .split('\n')
      .filter((l) => l.trim() !== '')
      .map(Number);
  } catch {
    return [];
  }
}
