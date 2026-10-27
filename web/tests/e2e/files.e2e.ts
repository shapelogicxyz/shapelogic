// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 6's files, recovery and persistence (FRONTEND-PLAN.md, its gate):
// File ▸ New on every grid; every fixture opened by File ▸ Open and saved
// as a copy by File ▸ Save As, with no document.changed on open and the
// copy's bytes the engine's own save of the fixture; a document edited,
// its engine killed, and the rescue copy offered and reopened with the
// edits (its file and its unsaved changes); a copy left by a session that
// died offered at the start; the window's close asking about unsaved
// changes (Cancel keeps the window, Save saves, Don't Save closes); the
// settings and layouts kept across a restart; a save while solving
// offered at a save point; the paused solver state exported and imported;
// the notes, Convert Assemblies to Pieces, About, the help links, Open
// Recent, a file dropped without a path. The file dialogs are answered
// in the main process (answerDialogs), as a user would answer them. The
// audit of the stage added: the rescue copy kept until its offer is
// answered (another death, the window closed), a close while the engine
// cannot run, a quit and a close of the window's own, a close whose save
// does not end, a window destroyed while the menus are built, and a second
// start while the app runs. Stage 8: a file the window was not handed is
// refused before the engine sees it; a file given on the command line
// opens at the start.

import { spawn, spawnSync } from 'node:child_process';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { basename, join, resolve } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  answerDialogs,
  appOf,
  BUILT,
  ENGINE,
  engineCall,
  engines,
  FIXTURES,
  launch,
  menu,
  menuItem,
  openFixture,
  readCallLog,
  WEB,
} from './app.ts';
import { dominoBox, runButton, runEnded, solverSettled, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

interface Snap {
  revision: number;
  dirty: boolean;
  grid: string;
  path: string | null;
  notes: string;
  shapes: { id: string; name: string }[];
  problems: { id: string; saved: number; state: string; solve: string }[];
}

const docOf = (page: Page): Promise<Snap> => engineCall<Snap>(page, 'document.get');

/** A directory of the test's own. */
function tmp(): string {
  return mkdtempSync(join(tmpdir(), 'bt-e2e-files-'));
}

/** The engine's own save of a puzzle file: a shapelogic_engine of the test's own opens it and saves it. */
function engineSave(file: string, out: string): void {
  const line = (id: number, method: string, params: unknown): string =>
    JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n';
  const r = spawnSync(ENGINE ?? '', [], {
    input: line(1, 'document.open', { path: file }) + line(2, 'document.save', { path: out }),
    encoding: 'utf8',
  });
  const replies = r.stdout
    .split('\n')
    .filter((l) => l !== '')
    .map((l) => JSON.parse(l) as { id?: number; error?: unknown });
  expect(replies.filter((x) => x.error !== undefined)).toEqual([]);
}

/** Starts collecting the document.changed events the page's bridge receives. */
async function watchChanges(page: Page): Promise<void> {
  await page.evaluate(() => {
    const w = window as unknown as { changes: unknown[]; offChanges?: () => void };
    w.offChanges?.();
    w.changes = [];
    w.offChanges = window.bt.onMessage((m) => {
      if (m.kind === 'event' && m.event === 'document.changed') {
        w.changes.push(m.params);
      }
    });
  });
}

async function changes(page: Page): Promise<unknown[]> {
  return page.evaluate(() => (window as unknown as { changes: unknown[] }).changes);
}

/** Waits until nothing is under way and the page shows the engine's revision. */
async function settled(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
  await expect
    .poll(async () => {
      const snap = await docOf(page);
      return Number(await page.getAttribute('#stage', 'data-revision')) >= snap.revision;
    })
    .toBe(true);
}

/** The app's windows. */
function windowCount(page: Page): Promise<number> {
  return appOf(page).evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length);
}

/** Closes the window as its close button does. */
async function closeTheWindow(page: Page): Promise<void> {
  await appOf(page).evaluate(({ BrowserWindow }) => {
    BrowserWindow.getAllWindows()[0]?.close();
  });
}

/** Kills the app's engine and waits for the next one its host starts. */
async function killEngine(page: Page): Promise<void> {
  const main = appOf(page).process().pid ?? 0;
  const [pid] = engines(main);
  expect(pid).toBeDefined();
  process.kill(pid ?? 0, 'SIGKILL');
  await expect.poll(() => engines(main).some((p) => p !== pid), { timeout: 15_000 }).toBe(true);
}

/** Kills the engine and waits for the rescue copy's offer after its restart (newer than any shown). */
async function killForOffer(page: Page): Promise<void> {
  await page.evaluate(() => {
    const shown = document.getElementById('rescueAsk');
    if (shown !== null) {
      shown.dataset.older = '1';
    }
  });
  await killEngine(page);
  await expect(page.locator('#rescueAsk:not([data-older])')).toBeVisible({ timeout: 15_000 });
}

/** Opens a puzzle file by File ▸ Open, changes its notes, and waits for the rescue copy. */
async function editFile(page: Page, file: string, notes: string): Promise<void> {
  await answerDialogs(page, { open: file });
  await menu(page, 'open');
  await expect(page.locator('#fname')).toHaveText(basename(file));
  await settled(page);
  await engineCall(page, 'document.setNotes', { text: notes });
  await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');
  await settled(page);
  await rescued(page);
}

/** Waits until the rescue copy holds the document's revision. */
async function rescued(page: Page): Promise<number> {
  const rev = (await docOf(page)).revision;
  await expect(page.locator('#stage')).toHaveAttribute('data-rescued', String(rev), {
    timeout: 10_000,
  });
  return rev;
}

/** The names New lists the grids by (the original's grid selector). */
const NEW_NAMES: Record<string, string> = {
  bricks: 'Brick',
  sliding: 'Sliding',
  stacking: 'Stacking',
  prisms: 'Triangular Prism',
  spheres: 'Spheres',
  rhombic: 'Rhombic',
  tetraOcta: 'Tetrahedra-Octahedra',
};

test('File ▸ New on every grid: the grid chosen once, an empty clean puzzle', async () => {
  const a = await launch();
  try {
    const { page } = a;
    const info = await engineCall<{ grids: { grid: string; label: string; supported: boolean }[] }>(
      page,
      'engine.info',
    );
    // every grid this version solves, sliding and stacking second and third (the original's order)
    const grids = info.grids.filter((g) => g.supported);
    expect(grids.length).toBe(7);
    // Cancel keeps the puzzle
    await openFixture(page, 'Bermuda.xmpuzzle');
    await menu(page, 'new');
    expect(
      await page
        .locator('#newPuzzle [data-grid]')
        .evaluateAll((els) => els.map((e) => (e as HTMLElement).dataset.grid)),
    ).toEqual(['bricks', 'sliding', 'stacking', 'prisms', 'spheres', 'rhombic', 'tetraOcta']);
    for (const g of grids) {
      await expect(page.locator(`#newPuzzle [data-grid="${g.grid}"]`)).toHaveText(
        NEW_NAMES[g.grid] ?? '',
      );
    }
    // the first grid is selected, with its description and picture; OK and Cancel, nothing else
    await expect(page.locator('#newPuzzle [data-grid="bricks"]')).toHaveAttribute(
      'aria-selected',
      'true',
    );
    await expect(page.locator('#newInfo')).toContainText('Pieces are made of cubes.');
    await expect(page.locator('#newInfo img')).toBeVisible();
    await page.click('#newPuzzle [data-grid="spheres"]');
    await expect(page.locator('#newInfo')).toContainText('Pieces are made of balls.');
    await expect(page.locator('#newInfo')).toContainText('Warning:');
    expect(await page.locator('#newPuzzle .bt button').allTextContents()).toEqual(['Cancel', 'OK']);
    await page.keyboard.press('Escape');
    await expect(page.locator('#newPuzzle')).toHaveCount(0);
    expect((await docOf(page)).grid).toBe('prisms');
    for (const g of grids) {
      const gen = await page.getAttribute('#stage', 'data-generation');
      await page.keyboard.press('Control+n');
      await page.click(`#newPuzzle [data-grid="${g.grid}"]`);
      await page.click('#newPuzzle [data-x="ok"]');
      await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', gen ?? '');
      const snap = await docOf(page);
      expect(snap.grid).toBe(g.grid);
      expect(snap.shapes).toEqual([]);
      expect(snap.dirty).toBe(false);
      expect(snap.path).toBeNull();
      await expect(page.locator('#fname')).toHaveText('Untitled');
      await expect(page.locator('#vtChip')).toHaveText(g.label);
    }
  } finally {
    await a.close();
  }
});

test('every fixture opened and saved as a copy: no change on open, the engine’s bytes', async () => {
  test.setTimeout(240_000);
  const dir = tmp();
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  try {
    const { page } = a;
    const files = readdirSync(FIXTURES)
      .filter((f) => f.endsWith('.xmpuzzle'))
      .sort();
    expect(files.length).toBeGreaterThan(15);
    for (const f of files) {
      const fixture = join(FIXTURES, f);
      await watchChanges(page);
      await answerDialogs(page, { open: fixture });
      const gen = await page.getAttribute('#stage', 'data-generation');
      await menu(page, 'open');
      await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', gen ?? '');
      await expect(page.locator('#fname')).toHaveText(f);
      await settled(page);
      // the workspace shown has read what it shows; nothing was changed by opening
      await page.waitForTimeout(300);
      expect(await changes(page), f).toEqual([]);
      const opened = await docOf(page);
      expect(opened.dirty, f).toBe(false);
      expect(opened.path).toBe(fixture);
      await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');

      // Save As: the copy, then the document is the copy's, clean
      const copy = join(dir, `copy-${f}`);
      await answerDialogs(page, { save: copy });
      await page.keyboard.press('Control+Shift+s');
      await expect(page.locator('#fname')).toHaveText(`copy-${f}`);
      await expect(page.locator('#stage')).toHaveAttribute('data-path', copy);
      await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');
      const own = join(dir, `own-${f}`);
      engineSave(fixture, own);
      expect(readFileSync(copy).equals(readFileSync(own)), f).toBe(true);
      expect(existsSync(`${copy}.tmp`)).toBe(false);
    }
    // the last copy is first in Open Recent
    const recent = await menuItem(page, 'recent.0');
    expect(recent?.label).toContain(`copy-${files.at(-1) ?? ''}`);
    // a name without the extension gets it
    await answerDialogs(page, { save: join(dir, 'plain') });
    await menu(page, 'saveAs');
    await expect(page.locator('#fname')).toHaveText('plain.xmpuzzle');
    expect(existsSync(join(dir, 'plain.xmpuzzle'))).toBe(true);
    // a cancelled dialog changes nothing
    await answerDialogs(page, { save: null, open: null });
    await menu(page, 'saveAs');
    await menu(page, 'open');
    await expect(page.locator('#fname')).toHaveText('plain.xmpuzzle');
    const refused = readCallLog(log).filter((c) => c.errc !== null && c.errc !== 'superseded');
    expect(refused).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('an engine killed after edits: restarted, the rescue copy offered and reopened', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  try {
    const { app, page } = a;
    await answerDialogs(page, { open: file });
    await menu(page, 'open');
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle');
    await settled(page);
    const before = await docOf(page);
    // edits: a shape duplicated by its key, the notes by their dialog
    await page.click('#shapeList .row >> nth=0');
    await page.keyboard.press('Control+d');
    await expect(page.locator('#shapeList .row')).toHaveCount(before.shapes.length + 1);
    await menu(page, 'notes');
    await page.locator('#notes textarea').fill('rescued notes');
    await page.click('#notes [data-x="save"]');
    await expect(page.locator('#notes')).toHaveCount(0);
    await settled(page);
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');
    await rescued(page);
    const edited = await docOf(page);
    expect(existsSync(join(a.userData, '__rescue.xmpuzzle'))).toBe(true);

    const main = app.process().pid ?? 0;
    const [pid] = engines(main);
    process.kill(pid ?? 0, 'SIGKILL');
    const ask = page.locator('#rescueAsk');
    await expect(ask).toBeVisible({ timeout: 15_000 });
    await expect(ask).toContainText('The engine stopped');
    await expect(ask).toContainText('Bermuda.xmpuzzle');
    // a question that must be answered: Esc does not take it away
    await page.keyboard.press('Escape');
    await expect(ask).toBeVisible();
    await ask.locator('[data-x="open"]').click();
    // its name, and the mark of unsaved changes
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle●');
    await expect(page.locator('#shapeList .row')).toHaveCount(before.shapes.length + 1);
    const back = await docOf(page);
    expect(back.path).toBe(file);
    expect(back.dirty).toBe(true);
    expect(back.notes).toBe('rescued notes');
    expect(back.shapes.map((s) => s.name)).toEqual(edited.shapes.map((s) => s.name));
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');

    // Save writes the puzzle's own file; clean, the copy goes
    await page.keyboard.press('Control+s');
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');
    await expect.poll(() => existsSync(join(a.userData, '__rescue.xmpuzzle'))).toBe(false);
    const saved = join(dir, 'check.xmpuzzle');
    engineSave(file, saved);
    const reread = spawnSync(ENGINE ?? '', [], {
      input:
        JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'document.open', params: { path: file } }) +
        '\n',
      encoding: 'utf8',
    });
    const snap = (JSON.parse(reread.stdout.split('\n')[0] ?? '{}') as { result: Snap }).result;
    expect(snap.notes).toBe('rescued notes');
    expect(snap.shapes.length).toBe(before.shapes.length + 1);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a rescue copy left by a session that died is offered at the start', async () => {
  const dir = tmp();
  const userData = join(dir, 'data');
  mkdirSync(userData);
  // the session before: its copy written, then it died without closing
  const file = join(dir, 'CubeInCage.xmpuzzle');
  copyFileSync(join(FIXTURES, 'CubeInCage.xmpuzzle'), file);
  const first = await test.step('the session that dies', () => launch({ userData }));
  try {
    await answerDialogs(first.page, { open: file });
    await menu(first.page, 'open');
    await expect(first.page.locator('#fname')).toHaveText('CubeInCage.xmpuzzle');
    await settled(first.page);
    await engineCall(first.page, 'document.setNotes', { text: 'from the session that died' });
    await expect(first.page.locator('#stage')).toHaveAttribute('data-dirty', '1');
    await rescued(first.page);
  } finally {
    // the app dies: no close, no question
    process.kill(first.app.process().pid ?? 0, 'SIGKILL');
  }
  const a = await test.step('the next start', () => launch({ userData }));
  try {
    const { page } = a;
    const ask = page.locator('#rescueAsk');
    await expect(ask).toBeVisible();
    await expect(ask).toContainText('did not close properly');
    await expect(ask).toContainText('CubeInCage.xmpuzzle');
    await ask.locator('[data-x="open"]').click();
    await expect(page.locator('#fname')).toHaveText('CubeInCage.xmpuzzle●');
    const snap = await docOf(page);
    expect(snap.notes).toBe('from the session that died');
    expect(snap.path).toBe(file);
    expect(snap.dirty).toBe(true);
  } finally {
    await a.close();
  }
  // Discard removes it
  const b = await test.step('the start after it', () => launch({ userData }));
  try {
    await expect(b.page.locator('#rescueAsk')).toBeVisible();
    await b.page.locator('#rescueAsk [data-x="drop"]').click();
    await expect.poll(() => existsSync(join(userData, '__rescue.xmpuzzle'))).toBe(false);
  } finally {
    await b.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the window’s close asks about unsaved changes: Cancel keeps it, Save saves, Don’t Save closes', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  const { app, page } = a;
  const windows = (): Promise<number> =>
    app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length);
  const closeWindow = (): Promise<void> =>
    app.evaluate(({ BrowserWindow }) => {
      BrowserWindow.getAllWindows()[0]?.close();
    });
  try {
    await answerDialogs(page, { open: file });
    await menu(page, 'open');
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle');
    await settled(page);
    await engineCall(page, 'document.setNotes', { text: 'not saved yet' });
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');

    // the native question is for a window that does not answer: counted, never wanted here
    await app.evaluate(({ dialog }) => {
      const g = globalThis as unknown as { boxes: number };
      g.boxes = 0;
      dialog.showMessageBox = () => {
        g.boxes++;
        return Promise.resolve({ response: 0, checkboxChecked: false });
      };
    });
    const boxes = (): Promise<number> =>
      app.evaluate(() => (globalThis as unknown as { boxes: number }).boxes);
    // Cancel keeps the window and the changes
    await closeWindow();
    const ask = page.locator('#closeAsk');
    await expect(ask).toBeVisible();
    await expect(ask).toContainText('Bermuda.xmpuzzle');
    await ask.locator('[data-x="cancel"]').click();
    await expect(ask).toHaveCount(0);
    await page.waitForTimeout(4500); // longer than the main process waits for a close to be taken
    expect(await windows()).toBe(1);
    expect(await boxes()).toBe(0);
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');
    // Esc is Cancel too; and Ctrl+W asks the same
    await page.keyboard.press('Control+w');
    await expect(ask).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(ask).toHaveCount(0);
    expect(await windows()).toBe(1);

    // Save: the file written, then the window closes
    await rescued(page);
    await closeWindow();
    await ask.locator('[data-x="save"]').click();
    await expect.poll(windows).toBe(0);
    const out = spawnSync(ENGINE ?? '', [], {
      input:
        JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'document.open', params: { path: file } }) +
        '\n',
      encoding: 'utf8',
    });
    expect(out.stdout).toContain('not saved yet');
    // a window closed with its changes saved leaves no rescue copy
    expect(existsSync(join(a.userData, '__rescue.xmpuzzle'))).toBe(false);
  } finally {
    await a.close();
  }

  // Don't Save closes without saving; a clean document closes without a question
  const b = await launch();
  try {
    await answerDialogs(b.page, { open: file });
    await menu(b.page, 'open');
    await expect(b.page.locator('#fname')).toHaveText('Bermuda.xmpuzzle');
    await settled(b.page);
    await engineCall(b.page, 'document.setNotes', { text: 'given up' });
    await expect(b.page.locator('#stage')).toHaveAttribute('data-dirty', '1');
    await rescued(b.page);
    await b.app.evaluate(({ BrowserWindow }) => {
      BrowserWindow.getAllWindows()[0]?.close();
    });
    await b.page.locator('#closeAsk [data-x="drop"]').click();
    await expect
      .poll(() => b.app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length))
      .toBe(0);
    expect(readFileSync(file).includes(Buffer.from('given up'))).toBe(false);
    expect(existsSync(join(b.userData, '__rescue.xmpuzzle'))).toBe(false);
  } finally {
    await b.close();
  }
  const c = await launch();
  try {
    await openFixture(c.page, 'Bermuda.xmpuzzle');
    await c.app.evaluate(({ BrowserWindow }) => {
      BrowserWindow.getAllWindows()[0]?.close();
    });
    await expect
      .poll(() => c.app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length))
      .toBe(0);
  } finally {
    await c.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('settings and layouts survive a restart', async () => {
  const dir = tmp();
  const userData = join(dir, 'data');
  mkdirSync(userData);
  const a = await launch({ userData });
  try {
    const { page } = a;
    await openFixture(page, 'Bermuda.xmpuzzle');
    await page.keyboard.press('Control+,');
    await page.click('#setPg [data-k="theme"][data-o="Dark"]');
    await page.click('#setPg [data-k="density"][data-o="Minimal"]');
    await page.selectOption('#setPg select[data-k="undo"]', '100');
    await page.click('#setNav [data-p="view"]');
    // the defaults of a new data folder
    await expect(page.locator('#setPg [data-k="rot"][data-o="Free Spin"]')).toHaveClass(/\bon\b/);
    await expect(page.locator('#setPg [data-k="zAxis"][data-o="Z Towards"]')).toHaveClass(/\bon\b/);
    await page.click('#setPg [data-k="rot"][data-o="Top Up"]');
    await page.click('#setPg [data-k="zAxis"][data-o="Z Up"]');
    await page.click('#setPg .switch[data-k="cube"]');
    await page.click('#setNav [data-p="grid"]');
    await expect(page.locator('#setPg [data-k="gridPlane"][data-o="XY"]')).toHaveClass(/\bon\b/);
    for (const k of ['newX', 'newY', 'newZ']) {
      await expect(page.locator(`#setPg input[data-n="${k}"]`)).toHaveValue('4');
    }
    for (const [k, v] of [
      ['slideX', '6'],
      ['slideY', '6'],
      ['slideZ', '1'],
    ] as const) {
      await expect(page.locator(`#setPg input[data-n="${k}"]`)).toHaveValue(v);
    }
    await page.click('#setPg button[data-n="newZ"][data-s="-1"]');
    await expect(page.locator('#setPg input[data-n="newZ"]')).toHaveValue('3');
    await page.click('#setNav [data-p="perf"]');
    await page.locator('#setPg input[data-k="threads"]').fill('1');
    await page.keyboard.press('Escape');
    // layouts: Puzzle with its left card collapsed, Solver with its right one, Solver shown
    await page.keyboard.press('Control+2');
    await page.keyboard.press('Control+[');
    await page.keyboard.press('Control+3');
    await page.keyboard.press('Control+]');
    await expect(page.locator('#stage')).toHaveClass(/\brc\b/);
    await expect
      .poll(() => engineCall<{ undoDepth: number; threads: number }>(page, 'settings.get'))
      .toMatchObject({ undoDepth: 100, threads: 1 });
    // the store writes a moment after a change
    await expect
      .poll(() => {
        try {
          const s = JSON.parse(readFileSync(join(userData, 'state.json'), 'utf8')) as {
            layout?: { tab?: string };
          };
          return s.layout?.tab;
        } catch {
          return undefined;
        }
      })
      .toBe('solver');
  } finally {
    await a.close();
  }
  const b = await launch({ userData });
  try {
    const { page } = b;
    const html = page.locator('html');
    await expect(html).toHaveAttribute('data-theme', 'dark');
    await expect(html).toHaveAttribute('data-density', 'minimal');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'solver');
    await expect(page.locator('#stage')).toHaveClass(/\brc\b/);
    await expect(page.locator('#stage')).not.toHaveClass(/\blc\b/);
    await page.keyboard.press('Control+2');
    await expect(page.locator('#stage')).toHaveClass(/\blc\b/);
    await expect(page.locator('#stage')).not.toHaveClass(/\brc\b/);
    await page.keyboard.press('Control+1');
    await expect(page.locator('#stage')).not.toHaveClass(/\blc\b|\brc\b/);
    // the engine's settings went to the new engine
    expect(await engineCall(page, 'settings.get')).toMatchObject({ undoDepth: 100, threads: 1 });
    await page.keyboard.press('Control+,');
    await page.click('#setNav [data-p="view"]');
    await expect(page.locator('#setPg [data-k="rot"][data-o="Top Up"]')).toHaveClass(/\bon\b/);
    await expect(page.locator('#setPg [data-k="zAxis"][data-o="Z Up"]')).toHaveClass(/\bon\b/);
    await expect(page.locator('#setPg .switch[data-k="cube"]')).not.toHaveClass(/\bon\b/);
    await page.click('#setNav [data-p="grid"]');
    await expect(page.locator('#setPg input[data-n="newZ"]')).toHaveValue('3');
    await page.keyboard.press('Escape');
  } finally {
    await b.close();
  }
  // a store that is not JSON is an empty one: the defaults
  writeFileSync(join(userData, 'state.json'), '{not json');
  const c = await launch({ userData });
  try {
    await expect(c.page.locator('html')).not.toHaveAttribute('data-theme', /./);
    await expect(c.page.locator('#stage')).toHaveAttribute('data-tab', 'entities');
  } finally {
    await c.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('Sidebar width: Static widths as chosen; Variable grows past a 1600 pixel window', async () => {
  const a = await launch();
  try {
    const { page } = a;
    await openFixture(page, 'Bermuda.xmpuzzle');
    // the side cards' widths and the window's, as laid out (after the grid's transition)
    const columns = (): Promise<string> =>
      page.evaluate(() => {
        const body = document.querySelector<HTMLElement>('#stage .body');
        const [l = '', , r = ''] = getComputedStyle(
          body ?? document.body,
        ).gridTemplateColumns.split(' ');
        return `${l} ${r}`;
      });
    const resize = (width: number): Promise<void> =>
      a.app.evaluate(({ BrowserWindow }, w) => {
        BrowserWindow.getAllWindows()[0]?.setContentSize(w, 900);
      }, width);
    await page.keyboard.press('Control+,');
    await expect(page.locator('#setPg [data-k="sidebar"][data-o="Static"]')).toHaveClass(/\bon\b/);
    await expect(page.locator('#setPg input[data-w="sideL"]')).toHaveValue('320');
    await expect(page.locator('#setPg input[data-w="sideR"]')).toHaveValue('340');
    await expect.poll(columns).toBe('320px 340px');
    await page.fill('#setPg input[data-w="sideL"]', '450');
    await page.keyboard.press('Tab');
    await page.click('#setPg button[data-w="sideR"][data-s="1"]');
    await expect(page.locator('#setPg input[data-w="sideR"]')).toHaveValue('350');
    await expect.poll(columns).toBe('450px 350px');
    // out of range: held to 200 to 600
    await page.fill('#setPg input[data-w="sideL"]', '9000');
    await page.keyboard.press('Tab');
    await expect(page.locator('#setPg input[data-w="sideL"]')).toHaveValue('600');
    // a wide window leaves Static as it is
    await resize(1900);
    await expect.poll(columns).toBe('600px 350px');
    await page.click('#setPg [data-k="sidebar"][data-o="Variable"]');
    await expect(page.locator('#setPg input[data-w="sideL"]')).toHaveCount(0);
    // 300 pixels past 1600: a third each
    await expect.poll(columns).toBe('420px 440px');
    await resize(1500);
    await expect.poll(columns).toBe('320px 340px');
    // the right card reaches 600 at 2380, the left at 2440
    await resize(2410);
    await expect.poll(columns).toBe('590px 600px');
    await resize(3000);
    await expect.poll(columns).toBe('600px 600px');
    await page.keyboard.press('Escape');
  } finally {
    await a.close();
  }
});

test('a save while solving at a save point; the paused solver state exported and imported', async () => {
  test.setTimeout(180_000);
  const dir = tmp();
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  try {
    const { page } = a;
    await page.keyboard.press('Control+n');
    await page.click('#newPuzzle [data-x="ok"]');
    await expect(page.locator('#fname')).toHaveText('Untitled');
    // one thread (the setting): the 2x4x6 box of dominoes runs for minutes
    await page.keyboard.press('Control+,');
    await page.click('#setNav [data-p="perf"]');
    await page.locator('#setPg input[data-k="threads"]').fill('1');
    await page.keyboard.press('Escape');
    await expect
      .poll(() => engineCall<{ threads: number }>(page, 'settings.get'))
      .toMatchObject({ threads: 1 });
    const { problem } = await dominoBox(page, 2, 4, 6);
    await toSolver(page);
    await expect(page.locator('#leftS')).toHaveAttribute('data-problem', problem);
    await runButton(page, 'start');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling', {
      timeout: 30_000,
    });
    // Save (no file yet: Save As), refused while solving: offered at a save point
    const file = join(dir, 'dominoes.xmpuzzle');
    await answerDialogs(page, { save: file });
    await page.keyboard.press('Control+s');
    const ask = page.locator('#savePoint');
    await expect(ask).toBeVisible();
    await ask.locator('[data-x="save"]').click();
    await expect.poll(() => existsSync(file), { timeout: 60_000 }).toBe(true);
    // its file now (the solve carried on makes it changed again)
    await expect(page.locator('#fname')).toHaveText(/^dominoes\.xmpuzzle/);
    await expect(page.locator('#stage')).toHaveAttribute('data-path', file);
    // the solve carries on
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling', {
      timeout: 30_000,
    });
    // the copy saved holds the paused position
    const out = spawnSync(ENGINE ?? '', [], {
      input:
        JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'document.open', params: { path: file } }) +
        '\n',
      encoding: 'utf8',
    });
    const saved = (JSON.parse(out.stdout.split('\n')[0] ?? '{}') as { result: Snap }).result;
    expect(saved.problems.find((p) => p.state === 'solving')).toBeDefined();

    // Export ▸ Paused Solver State: refused while it runs, offered once paused
    expect((await menuItem(page, 'exportPaused'))?.enabled).toBe(false);
    await runButton(page, 'pause');
    await runEnded(page, 'paused');
    await expect.poll(async () => (await menuItem(page, 'exportPaused'))?.enabled).toBe(true);
    const paused = join(dir, 'dominoes');
    await answerDialogs(page, { save: paused });
    await menu(page, 'exportPaused');
    await expect.poll(() => existsSync(`${paused}.slsolve`)).toBe(true);

    // Import ▸ Paused Solver State: a new, untitled document; Continue carries it on
    await answerDialogs(page, { open: `${paused}.slsolve` });
    await menu(page, 'importPaused');
    // the document has unsaved changes (the solve since the save): asked first
    await page.click('#discard [data-x="drop"]');
    // a new, untitled puzzle, not saved anywhere yet
    await expect(page.locator('#fname')).toHaveText('Untitled●');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'solver');
    await solverSettled(page);
    const imported = await docOf(page);
    expect(imported.path).toBeNull();
    const p = imported.problems.find((x) => x.state === 'solving');
    expect(p).toBeDefined();
    await expect(page.locator('#leftS')).toHaveAttribute('data-problem', p?.id ?? '');
    await runButton(page, 'continue');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling', {
      timeout: 30_000,
    });
    await runButton(page, 'abort');
    await page.locator('.cfm [data-x="1"]').click();
    await runEnded(page, 'idle');
    // refusals the app asks for and shows nothing of: a hover preview of a
    // task the shape cannot take (the mouse rests on the Shapes tab's tools
    // when the New dialog closes), and a solution's playback asked as the
    // document changes (the player treats not_found as gone)
    const refused = readCallLog(log).filter(
      (c) =>
        c.errc !== null &&
        c.errc !== 'superseded' &&
        c.method !== 'document.save' &&
        c.method !== 'shape.taskPreview' &&
        !(c.method === 'playback.open' && c.errc === 'not_found'),
    );
    expect(refused).toEqual([]);
    // the one refusal: Save while solving, asked of the engine only after the check
    expect(readCallLog(log).filter((c) => c.method === 'document.save' && c.errc !== null)).toEqual(
      [],
    );
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('notes, Convert Assemblies to Pieces, About, the help links, a file dropped without a path', async () => {
  const a = await launch();
  try {
    const { app, page } = a;
    await openFixture(page, 'Bermuda.xmpuzzle');
    // Notes: the dialog, saved as one undo step named by the engine
    await menu(page, 'notes');
    await page.locator('#notes textarea').fill('first line\nsecond line');
    await page.click('#notes [data-x="save"]');
    await expect(page.locator('#notes')).toHaveCount(0);
    expect((await docOf(page)).notes).toBe('first line\nsecond line');
    const history = await engineCall<{ undo: string[] }>(page, 'edit.history');
    await expect
      .poll(async () => (await menuItem(page, 'undo'))?.label)
      .toBe(`Undo ${history.undo.at(-1) ?? ''}`);
    // a control character is refused by the engine: the dialog stays
    await menu(page, 'notes');
    await page.locator('#notes textarea').fill('bad \u0001');
    await page.click('#notes [data-x="save"]');
    await expect(page.locator('#stText')).toContainText('Notes:');
    await expect(page.locator('#notes')).toBeVisible();
    await page.keyboard.press('Escape');

    // Convert Assemblies to Pieces: Bermuda's saved solution as shapes
    const before = await docOf(page);
    const source = before.problems.find((p) => p.saved > 0);
    expect(source).toBeDefined();
    expect((await menuItem(page, 'toShapes'))?.enabled).toBe(true);
    await menu(page, 'toShapes');
    const d = page.locator('#toShapes');
    await expect(d.locator('[data-d="dropDisconnected"]')).toBeChecked();
    await expect(d.locator('[data-d="dropIdentical"]')).toBeChecked();
    // prisms: no millable or notchable filters
    await expect(d.locator('[data-d="dropNonMillable"]')).toHaveCount(0);
    await d.locator('input[name="addTo"][value="newProblem"]').check();
    await d.locator('[data-f="max"]').fill('2');
    await d.locator('[data-x="go"]').click();
    await expect(d).toHaveCount(0);
    await expect
      .poll(async () => (await docOf(page)).shapes.length)
      .toBe(before.shapes.length + (source?.saved ?? 0));
    const after = await docOf(page);
    expect(after.problems.length).toBe(before.problems.length + 1);
    const newProblem = after.problems.at(-1)?.id ?? '';
    const detail = await engineCall<{ parts: { shape: string; min: number; max: number }[] }>(
      page,
      'problem.get',
      { problem: newProblem },
    );
    expect(detail.parts.map((p) => [p.min, p.max])).toEqual(
      after.shapes.slice(before.shapes.length).map(() => [0, 2]),
    );

    // About: the app's version and the engine's
    const info = await engineCall<{ version: string }>(page, 'engine.info');
    await appOf(page).evaluate(({ BrowserWindow }) => {
      BrowserWindow.getAllWindows()[0]?.webContents.send('bt:command', { cmd: 'about' });
    });
    await expect(page.locator('#about')).toContainText(`shapelogic_engine ${info.version}`);
    await page.keyboard.press('Escape');

    // the command line tool's dialog: what it is, its options, where it is,
    // the command; Install is never pressed here (it would link the tool
    // into /usr/local/bin of the machine running the test)
    await appOf(page).evaluate(({ BrowserWindow }) => {
      BrowserWindow.getAllWindows()[0]?.webContents.send('bt:command', { cmd: 'cliTool' });
    });
    const cli = page.locator('#cliTool');
    await expect(cli).toContainText('shapelogic_cli');
    await expect(cli).toContainText('burrTxt');
    await expect(cli).toContainText('--json');
    await expect(cli).toContainText('Where it is');
    await expect(cli).toContainText('/cli/shapelogic_cli');
    await expect(cli.locator('[data-x="install"], [data-x="uninstall"]')).toHaveCount(1);
    await expect(cli.locator('[data-x="cancel"], [data-x="close"]')).toHaveCount(1);
    await page.keyboard.press('Escape');
    await expect(cli).toHaveCount(0);

    // the help links: the main process's addresses, in the system's browser
    await app.evaluate(({ shell }) => {
      const opened: string[] = [];
      (globalThis as unknown as { opened: string[] }).opened = opened;
      shell.openExternal = (url: string) => {
        opened.push(url);
        return Promise.resolve();
      };
    });
    await menu(page, 'guide');
    await menu(page, 'tutorial');
    expect(
      await app.evaluate(() => (globalThis as unknown as { opened: string[] }).opened),
    ).toEqual(['https://shapelogic.xyz/docs/', 'https://shapelogic.xyz/docs/getting-started/']);

    // greyed: what this version does not have (the imports and grid
    // conversion came with deferred part 6: imports.e2e.ts)
    for (const id of ['anaglyph', 'anaglyph.swapped']) {
      expect((await menuItem(page, id))?.enabled, id).toBe(false);
    }

    // a file dropped with no path (from another application): opened by its bytes
    const bytes = [...readFileSync(join(FIXTURES, 'CubeInCage.xmpuzzle'))];
    await page.evaluate((b) => {
      const dt = new DataTransfer();
      dt.items.add(new File([new Uint8Array(b)], 'CubeInCage.xmpuzzle'));
      document
        .getElementById('stage')
        ?.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true }));
    }, bytes);
    await page.click('#discard [data-x="drop"]');
    await expect(page.locator('#fname')).toHaveText('CubeInCage.xmpuzzle');
    expect((await docOf(page)).path).toBeNull();
  } finally {
    await a.close();
  }
});

test('the rescue copy is kept until its offer is answered: another death, the window closed', async () => {
  const dir = tmp();
  const userData = join(dir, 'data');
  mkdirSync(userData);
  const copy = join(userData, '__rescue.xmpuzzle');
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await test.step('the session whose engine dies', () => launch({ userData }));
  try {
    const { page } = a;
    await editFile(page, file, 'kept until answered');
    await killForOffer(page);
    // the engine dies again while the offer waits: the newer offer takes the
    // question's place and the copy stays for it (it was removed as if Discarded)
    await killForOffer(page);
    await page.waitForTimeout(500); // a removal comes at once
    expect(existsSync(copy)).toBe(true);
    await page.locator('#rescueAsk [data-x="open"]').click();
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle●');
    const back = await docOf(page);
    expect(back.notes).toBe('kept until answered');
    expect(back.path).toBe(file);
    // the window closed while an offer waits: the copy stays for the next start
    await settled(page);
    await rescued(page);
    await killForOffer(page);
    await closeTheWindow(page);
    await expect.poll(() => windowCount(page)).toBe(0);
    expect(existsSync(copy)).toBe(true);
  } finally {
    await a.close();
  }
  // the next start offers it, and closing the window unanswered keeps it too
  const b = await test.step('the next start, closed unanswered', () => launch({ userData }));
  try {
    await expect(b.page.locator('#rescueAsk')).toContainText('did not close properly');
    await closeTheWindow(b.page);
    await expect.poll(() => windowCount(b.page)).toBe(0);
    expect(existsSync(copy)).toBe(true);
  } finally {
    await b.close();
  }
  const c = await test.step('the start after it, answered', () => launch({ userData }));
  try {
    await c.page.locator('#rescueAsk [data-x="open"]').click();
    await expect(c.page.locator('#fname')).toHaveText('Bermuda.xmpuzzle●');
    expect((await docOf(c.page)).notes).toBe('kept until answered');
  } finally {
    await c.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a window closed while the engine cannot run keeps the rescue copy, and says so', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  try {
    const { app, page } = a;
    await editFile(page, file, 'not saved: the engine is gone');
    // four quick deaths, each offering the copy; the last offer answered Reopen
    for (let i = 0; i < 4; i++) {
      await killForOffer(page);
    }
    await page.locator('#rescueAsk [data-x="open"]').click();
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle●');
    await settled(page);
    await rescued(page);
    // the fifth: the host gives up, and nothing can be saved
    const [pid] = engines(app.process().pid ?? 0);
    process.kill(pid ?? 0, 'SIGKILL');
    await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'failed', {
      timeout: 15_000,
    });
    const ask = page.locator('#closeAsk');
    await closeTheWindow(page);
    await expect(ask).toContainText('the engine is not running');
    await ask.locator('[data-x="cancel"]').click();
    await expect(ask).toHaveCount(0);
    expect(await windowCount(page)).toBe(1);
    // Close: the window goes, the copy stays for the next start
    await closeTheWindow(page);
    await ask.locator('[data-x="drop"]').click();
    await expect.poll(() => windowCount(page)).toBe(0);
    expect(existsSync(join(a.userData, '__rescue.xmpuzzle'))).toBe(true);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a quit asked about: Cancel keeps the app; a close of the window meanwhile does not give it up', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  const app = { exited: false };
  a.app.process().once('exit', () => {
    app.exited = true;
  });
  try {
    const { page } = a;
    await editFile(page, file, 'a quit asked about');
    const ask = page.locator('#closeAsk');
    // Cmd+Q, then Cancel: the app stays
    await a.app.evaluate(({ app: electron }) => {
      electron.quit();
    });
    await ask.locator('[data-x="cancel"]').click();
    await expect(ask).toHaveCount(0);
    await page.waitForTimeout(500);
    expect(app.exited).toBe(false);
    expect(await windowCount(page)).toBe(1);
    // Cmd+Q again; the window's close button pressed while it asks; Don't Save: the app quits
    await a.app.evaluate(({ app: electron }) => {
      electron.quit();
    });
    await expect(ask).toBeVisible();
    await closeTheWindow(page);
    await ask.locator('[data-x="drop"]').click();
    await expect.poll(() => app.exited, { timeout: 15_000 }).toBe(true);
  } finally {
    if (!app.exited) {
      await a.close();
    }
    rmSync(a.userData, { recursive: true, force: true });
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a close whose save does not end: another close asks natively, and the window can go', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  let stopped = 0;
  try {
    const { app, page } = a;
    await editFile(page, file, 'the save does not end');
    await app.evaluate(({ dialog }) => {
      const g = globalThis as unknown as { boxes: number };
      g.boxes = 0;
      dialog.showMessageBox = () => {
        g.boxes++;
        // Close Anyway
        return Promise.resolve({ response: 1, checkboxChecked: false });
      };
    });
    // the engine stops answering: the close's Save waits for ever
    stopped = engines(app.process().pid ?? 0)[0] ?? 0;
    process.kill(stopped, 'SIGSTOP');
    await closeTheWindow(page);
    await page.locator('#closeAsk [data-x="save"]').click();
    await expect(page.locator('#closeAsk')).toHaveCount(0);
    // the close button again: no question on screen takes it, so the main
    // process asks natively a few seconds later
    await closeTheWindow(page);
    await expect.poll(() => windowCount(page), { timeout: 10_000 }).toBe(0);
    expect(await app.evaluate(() => (globalThis as unknown as { boxes: number }).boxes)).toBe(1);
    // the page did not end its close: the copy stays for the next start
    expect(existsSync(join(a.userData, '__rescue.xmpuzzle'))).toBe(true);
  } finally {
    if (stopped !== 0) {
      process.kill(stopped, 'SIGCONT');
    }
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the menus outlive a window destroyed while they are built (as closeApp does)', async () => {
  const a = await launch();
  try {
    const { app, page } = a;
    await app.evaluate(({ Menu }) => {
      const g = globalThis as unknown as { errors: string[]; building: boolean };
      g.errors = [];
      g.building = false;
      // what Electron would show in a dialog that holds the main process
      process.on('uncaughtException', (e: unknown) => {
        g.errors.push(e instanceof Error ? (e.stack ?? e.message) : String(e));
      });
      // a slow menu (a busy main process): 600 ms in setApplicationMenu
      const set = Menu.setApplicationMenu.bind(Menu);
      Menu.setApplicationMenu = (m) => {
        g.building = true;
        const t = Date.now();
        while (Date.now() - t < 600) {
          // busy
        }
        set(m);
        g.building = false;
      };
    });
    // a change: the page sends its menu state and the main process builds the menus
    await engineCall(page, 'document.setNotes', { text: 'the menus change' });
    // the test's code runs through the inspector, in the middle of the build: the
    // window destroyed there, as closeApp destroys it at every test's end
    await expect
      .poll(
        () =>
          app.evaluate(({ BrowserWindow }) => {
            if (!(globalThis as unknown as { building: boolean }).building) {
              return false;
            }
            for (const w of BrowserWindow.getAllWindows()) {
              w.destroy();
            }
            return true;
          }),
        { intervals: [20], timeout: 5000 },
      )
      .toBe(true);
    await new Promise((done) => setTimeout(done, 1500));
    expect(
      await app.evaluate(() => (globalThis as unknown as { errors: string[] }).errors),
    ).toEqual([]);
  } finally {
    await a.close();
  }
});

test('a second start while the app runs hands its file over and quits: the copy stays', async () => {
  const dir = tmp();
  const userData = join(dir, 'data');
  mkdirSync(userData);
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const other = join(dir, 'CubeInCage.xmpuzzle');
  copyFileSync(join(FIXTURES, 'CubeInCage.xmpuzzle'), other);
  const a = await launch({ userData });
  try {
    const { page } = a;
    await editFile(page, file, 'the running app’s changes');
    // a second start on the same data, given a file (as a file manager starts
    // one on Windows and Linux): it offered the running app's copy as left by
    // a session that died, and its Discard removed it
    const env = Object.fromEntries(
      Object.entries(process.env).filter(
        (e): e is [string, string] => e[0] !== 'ELECTRON_RUN_AS_NODE' && e[1] !== undefined,
      ),
    );
    env.BT_USER_DATA = userData;
    env.BT_ENGINE = ENGINE ?? '';
    const electron = createRequire(import.meta.url)('electron') as string;
    const second = spawn(electron, [resolve(WEB, 'out/main/index.js'), other], {
      env,
      stdio: 'ignore',
    });
    const code = await new Promise<number | null>((done) => {
      const timer = setTimeout(() => {
        second.kill('SIGKILL');
        done(-1);
      }, 20_000);
      second.once('exit', (c) => {
        clearTimeout(timer);
        done(c);
      });
    });
    expect(code, 'the second start quits').toBe(0);
    // the running app was handed the file: it asks about its own changes first
    const ask = page.locator('#discard');
    await expect(ask).toBeVisible();
    await ask.locator('[data-x="cancel"]').click();
    expect((await docOf(page)).notes).toBe('the running app’s changes');
    expect(existsSync(join(userData, '__rescue.xmpuzzle'))).toBe(true);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a file the window was not handed is refused before the engine; the dialog’s opens', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  const a = await launch();
  try {
    const { page } = a;
    // stage 8: the main process lets a call name only a file its dialogs, a
    // drop or the system handed the window (main/path-grants.ts)
    for (const [method, params] of [
      ['document.open', { path: file }],
      ['document.save', { path: join(dir, 'new.xmpuzzle') }],
      ['solve.importPaused', { path: join(dir, 'x.slsolve') }],
    ] as const) {
      await expect(engineCall(page, method, params), method).rejects.toThrow(
        /invalid_argument: \S+ was not chosen in a file dialog or dropped on the window/,
      );
    }
    expect(existsSync(join(dir, 'new.xmpuzzle'))).toBe(false);
    expect((await docOf(page)).path).toBeNull();
    // the same file from the open dialog: opened, then saved where it is
    await answerDialogs(page, { open: file });
    await menu(page, 'open');
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle');
    expect((await docOf(page)).path).toBe(file);
    await engineCall(page, 'document.save', { path: file });
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a file given on the command line opens at the start', async () => {
  const dir = tmp();
  const file = join(dir, 'Bermuda.xmpuzzle');
  copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
  // stage 8: the command was sent when the page had loaded, before it listened, and lost
  const a = await launch({ args: [file] });
  try {
    const { page } = a;
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle');
    expect((await docOf(page)).path).toBe(file);
    await expect(page.locator('#shapeList .row')).not.toHaveCount(0);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
