// SPDX-License-Identifier: GPL-3.0-or-later
//
// Several windows (decision 0036): File ▸ New Window and a second start
// without a file open one, each with its own engine; a file open in two
// windows is read-only in the second (its Save is Save As, and the main
// process refuses its engine's save over the file); each window keeps its
// own rescue copy, and a copy left by a session that died is offered in one
// window only; a quit asks the windows one after the other, and a Cancel
// stops it; a change of the settings in one window reaches the others.

import { copyFileSync, existsSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { ElectronApplication, Page } from '@playwright/test';

import {
  answerDialogs,
  BUILT,
  docOf,
  ENGINE,
  engineCall,
  engines,
  FIXTURES,
  launch,
  menu,
  nextWindow,
  settled,
  windowCount,
} from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

function tmp(): string {
  return mkdtempSync(join(tmpdir(), 'bt-e2e-windows-'));
}

/** A copy of a fixture in the test's directory (the tests run side by side). */
function copyOf(dir: string, name: string): string {
  const file = join(dir, name);
  copyFileSync(join(FIXTURES, name), file);
  return file;
}

/** Opens a file in this window by its key (Ctrl+O), the open dialog answering it. */
async function openIn(page: Page, file: string): Promise<void> {
  await answerDialogs(page, { open: file });
  await page.keyboard.press('Control+o');
  await expect(page.locator('#stage')).toHaveAttribute('data-path', file);
  await settled(page);
}

/** Changes the notes and waits for the window's rescue copy to hold them. */
async function edit(page: Page, notes: string): Promise<void> {
  await engineCall(page, 'document.setNotes', { text: notes });
  await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');
  await settled(page);
  const rev = (await docOf(page)).revision;
  await expect(page.locator('#stage')).toHaveAttribute('data-rescued', String(rev), {
    timeout: 10_000,
  });
}

/** Closes this page's window (its close button). */
async function closeWindowOf(app: ElectronApplication, page: Page): Promise<void> {
  const win = await app.browserWindow(page);
  await win.evaluate((w: Electron.BrowserWindow) => {
    w.close();
  });
}

test('New Window opens a window of its own; a file open in both is read-only in the second', async () => {
  const dir = tmp();
  const file = copyOf(dir, 'Bermuda.xmpuzzle');
  const a = await launch();
  try {
    const { app, page } = a;
    await openIn(page, file);
    const b = await nextWindow(app, () => menu(page, 'newWindow'));
    expect(await windowCount(app)).toBe(2);
    // each window its own engine
    await expect.poll(() => engines(app.process().pid ?? 0).length).toBe(2);

    await openIn(b, file);
    await expect(b.locator('#stage')).toHaveAttribute('data-read-only', '1');
    await expect(b.locator('#fname')).toContainText('(read-only)');
    await expect(page.locator('#stage')).toHaveAttribute('data-read-only', '0');
    expect(await b.title()).toContain('(read-only)');

    // the main process refuses the read-only window's save over the file, by
    // its path or without one; the owner saves
    await expect(engineCall(b, 'document.save')).rejects.toThrow(/open in another window/);
    await expect(engineCall(b, 'document.save', { path: file })).rejects.toThrow(
      /open in another window/,
    );
    await edit(page, 'saved by the owner');
    await page.keyboard.press('Control+s');
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');

    // the read-only window's Save is Save As, proposing another name
    await edit(b, 'kept under another name');
    const copy = join(dir, 'Bermuda copy.xmpuzzle');
    await app.evaluate(({ dialog }, path) => {
      const g = globalThis as unknown as { proposed?: string };
      dialog.showSaveDialog = (...args: unknown[]) => {
        const opts = args.at(-1) as { defaultPath?: string };
        g.proposed = opts.defaultPath;
        return Promise.resolve({ canceled: false, filePath: path });
      };
    }, copy);
    await b.keyboard.press('Control+s');
    await expect(b.locator('#stage')).toHaveAttribute('data-path', copy);
    await expect(b.locator('#stage')).toHaveAttribute('data-dirty', '0');
    // it owns its new file
    await expect(b.locator('#stage')).toHaveAttribute('data-read-only', '0');
    const proposed = await app.evaluate(
      () => (globalThis as unknown as { proposed?: string }).proposed,
    );
    expect(basename(proposed ?? '')).toBe('Bermuda copy.xmpuzzle');
    expect((await docOf(page)).notes).toBe('saved by the owner');
    expect(existsSync(copy)).toBe(true);
    // nor may it save over the file the first window owns
    await expect(engineCall(b, 'document.save', { path: file })).rejects.toThrow(
      /open in another window/,
    );
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the owner closes: the read-only window stays so until it opens the file again', async () => {
  const dir = tmp();
  const file = copyOf(dir, 'Bermuda.xmpuzzle');
  const a = await launch();
  try {
    const { app, page } = a;
    await openIn(page, file);
    const b = await nextWindow(app, () => menu(page, 'newWindow'));
    await openIn(b, file);
    await expect(b.locator('#stage')).toHaveAttribute('data-read-only', '1');
    await closeWindowOf(app, page);
    await expect.poll(() => windowCount(app)).toBe(1);
    // its copy may be older than the file: still read-only
    await b.waitForTimeout(300);
    await expect(b.locator('#stage')).toHaveAttribute('data-read-only', '1');
    await expect(engineCall(b, 'document.save')).rejects.toThrow(/open in another window/);
    // opened again, it owns the file
    await openIn(b, file);
    await expect(b.locator('#stage')).toHaveAttribute('data-read-only', '0');
    await engineCall(b, 'document.save');
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('each window keeps its own rescue copy; a session that died offers each in one window', async () => {
  const dir = tmp();
  const userData = join(dir, 'data');
  mkdirSync(userData);
  const first = copyOf(dir, 'Bermuda.xmpuzzle');
  const second = copyOf(dir, 'CubeInCage.xmpuzzle');
  const a = await test.step('the session that dies', () => launch({ userData }));
  try {
    const { app, page } = a;
    await openIn(page, first);
    await edit(page, 'first window');
    const b = await nextWindow(app, () => menu(page, 'newWindow'));
    await openIn(b, second);
    await edit(b, 'second window');
    expect(existsSync(join(userData, '__rescue.xmpuzzle'))).toBe(true);
    expect(existsSync(join(userData, '__rescue-1.xmpuzzle'))).toBe(true);
    // the app dies: no window closes, both copies stay (its engines go with it)
    const main = app.process();
    const gone = new Promise((done) => main.once('exit', done));
    const orphans = engines(main.pid ?? 0);
    process.kill(main.pid ?? 0, 'SIGKILL');
    await gone;
    for (const pid of orphans) {
      try {
        process.kill(pid, 'SIGKILL');
      } catch {
        // gone already
      }
    }
  } catch (e) {
    await a.close();
    throw e;
  }
  const c = await test.step('the next start', () => launch({ userData }));
  try {
    const { app, page } = c;
    const ask = page.locator('#rescueAsk');
    await expect(ask).toContainText('Bermuda.xmpuzzle');
    await ask.locator('[data-x="open"]').click();
    await expect(page.locator('#stage')).toHaveAttribute('data-path', first);
    expect((await docOf(page)).notes).toBe('first window');
    // the other copy goes to the next window, and to it only
    const d = await nextWindow(app, () => menu(page, 'newWindow'));
    const askD = d.locator('#rescueAsk');
    await expect(askD).toContainText('CubeInCage.xmpuzzle');
    await askD.locator('[data-x="open"]').click();
    await expect(d.locator('#stage')).toHaveAttribute('data-path', second);
    expect((await docOf(d)).notes).toBe('second window');
    const e = await nextWindow(app, () => menu(page, 'newWindow'));
    await e.waitForTimeout(500);
    await expect(e.locator('#rescueAsk')).toHaveCount(0);
  } finally {
    await c.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a quit asks the windows one at a time: a Cancel stops it, Don’t Save in each quits', async () => {
  const dir = tmp();
  const a = await launch();
  const quit = { exited: false };
  a.app.process().once('exit', () => {
    quit.exited = true;
  });
  try {
    const { app, page } = a;
    await openIn(page, copyOf(dir, 'Bermuda.xmpuzzle'));
    await edit(page, 'first');
    const b = await nextWindow(app, () => menu(page, 'newWindow'));
    await openIn(b, copyOf(dir, 'CubeInCage.xmpuzzle'));
    await edit(b, 'second');
    const asking = async (): Promise<Page[]> => {
      const out: Page[] = [];
      for (const p of [page, b]) {
        if (!p.isClosed() && (await p.locator('#closeAsk').count()) > 0) {
          out.push(p);
        }
      }
      return out;
    };
    await app.evaluate(({ app: electron }) => {
      electron.quit();
    });
    await expect.poll(async () => (await asking()).length).toBe(1);
    const [one] = await asking();
    await one?.locator('#closeAsk [data-x="cancel"]').click();
    await page.waitForTimeout(500);
    expect(await asking()).toEqual([]);
    expect(await windowCount(app)).toBe(2);
    expect(quit.exited).toBe(false);
    // again, Don't Save in each in turn: the app quits
    await app.evaluate(({ app: electron }) => {
      electron.quit();
    });
    for (let i = 0; i < 2; i++) {
      await expect.poll(async () => (await asking()).length, { timeout: 15_000 }).toBe(1);
      const [next] = await asking();
      await next?.locator('#closeAsk [data-x="drop"]').click();
      if (i === 0) {
        await expect.poll(() => windowCount(app)).toBe(1);
      }
    }
    await expect.poll(() => quit.exited, { timeout: 15_000 }).toBe(true);
  } finally {
    if (!quit.exited) {
      await a.close();
    }
    rmSync(a.userData, { recursive: true, force: true });
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a second start without a file opens a window; one with a file opens it in the window there', async () => {
  const dir = tmp();
  const file = copyOf(dir, 'Bermuda.xmpuzzle');
  const a = await launch();
  try {
    const { app, page } = a;
    const b = await nextWindow(app, () =>
      app.evaluate(({ app: electron }) => {
        electron.emit('second-instance', {}, [], '', { files: [] });
      }),
    );
    expect(await windowCount(app)).toBe(2);
    await app.evaluate(({ app: electron }, f) => {
      electron.emit('second-instance', {}, [], '', { files: [f] });
    }, file);
    await expect
      .poll(async () => {
        const paths = await Promise.all(
          [page, b].map((p) => p.getAttribute('#stage', 'data-path')),
        );
        return paths.filter((p) => p === file).length;
      })
      .toBe(1);
    expect(await windowCount(app)).toBe(2);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a change of the settings in one window reaches the other', async () => {
  const a = await launch();
  try {
    const { app, page } = a;
    const b = await nextWindow(app, () => menu(page, 'newWindow'));
    // View ▸ Color Theme acts on the focused window (else the first)
    await menu(page, 'theme.dark');
    for (const p of [page, b]) {
      await expect(p.locator('html')).toHaveAttribute('data-theme', 'dark');
    }
    await menu(page, 'theme.light');
    for (const p of [page, b]) {
      await expect(p.locator('html')).toHaveAttribute('data-theme', 'light');
    }
  } finally {
    await a.close();
  }
});
