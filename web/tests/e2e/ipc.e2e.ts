// SPDX-License-Identifier: GPL-3.0-or-later
//
// The IPC path end to end (docs/handoff/web-1a.md, "Brief for the next
// stages"): the app starts its engine, a mesh crosses the bridge as a frame
// (window.bt, as the proxy sees it), an engine killed is reported and
// restarted with a new document, and a reload with calls in flight gets a
// new bridge server, so the old page's replies never reach the new one;
// a page that crashed is reloaded and shows the engine's document again.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';

import { BUILT, ENGINE, engines, launch, openFixture, shapes } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;
test.beforeEach(async () => {
  a = await launch();
});
test.afterEach(async () => {
  await a.close();
});

test('a mesh crosses the bridge as a frame', async () => {
  const { page } = a;
  await openFixture(page, 'Bermuda.xmpuzzle');
  const [first] = await shapes(page);
  expect(first).toBeDefined();
  const reply = await page.evaluate(
    (shape) =>
      new Promise<{
        bytes: number;
        desc: number;
        triangles: number;
        vertices: number;
        positions: number;
        indices: number;
      }>((done, fail) => {
        const id = 1_000_000_000;
        const off = window.bt.onMessage((m) => {
          if (m.kind === 'error' && m.id === id) {
            off();
            fail(new Error(m.message));
          }
          if (m.kind !== 'reply' || m.id !== id) {
            return;
          }
          off();
          const r = m.result as {
            triangles: number;
            vertices: number;
            frame: { arrays: Record<string, { length: number }> };
          };
          done({
            bytes: m.frame?.data.byteLength ?? -1,
            desc: m.frame?.desc.bytes ?? -2,
            triangles: r.triangles,
            vertices: r.vertices,
            positions: r.frame.arrays.positions?.length ?? -1,
            indices: r.frame.arrays.indices?.length ?? -1,
          });
        });
        window.bt.send({ kind: 'call', id, method: 'shape.mesh', params: { shape } });
      }),
    first?.id ?? '',
  );
  expect(reply.bytes).toBe(reply.desc);
  expect(reply.triangles).toBeGreaterThan(0);
  expect(reply.positions).toBe(3 * reply.vertices);
  expect(reply.indices).toBe(3 * reply.triangles);
  // the app's own drawing of it went the same way
  expect(Number(await page.getAttribute('#vp', 'data-triangles'))).toBeGreaterThan(0);
});

test('an engine killed is reported, restarted, and the document is new', async () => {
  const { app, page } = a;
  await openFixture(page, 'CubeInCage.xmpuzzle');
  const main = app.process().pid;
  expect(main).toBeDefined();
  const before = engines(main ?? 0);
  expect(before).toHaveLength(1);

  // what the status bar says, as it says it
  await page.evaluate(() => {
    const seen: string[] = [];
    (window as unknown as { seen: string[] }).seen = seen;
    const el = document.getElementById('stText');
    if (el !== null) {
      new MutationObserver(() => seen.push(el.textContent)).observe(el, {
        childList: true,
        characterData: true,
        subtree: true,
      });
    }
  });
  process.kill(before[0] ?? 0, 'SIGKILL');

  await expect(page.locator('#fname')).toHaveText('Untitled');
  await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'running');
  await expect(page.locator('#shapeList .row')).toHaveCount(0);
  await expect(page.locator('#vp')).not.toHaveAttribute('data-shown', /./);
  const seen = await page.evaluate(() => (window as unknown as { seen: string[] }).seen);
  expect(seen.some((t) => t.startsWith('The engine stopped (SIGKILL)'))).toBe(true);
  expect(seen).toContain('The engine was started again: a new, empty document');
  const after = engines(main ?? 0);
  expect(after).toHaveLength(1);
  expect(after[0]).not.toBe(before[0]);

  // and it works
  await openFixture(page, 'Bermuda.xmpuzzle');
});

test('a reload gets a bridge of its own: the old page’s replies do not reach it', async () => {
  const { page } = a;
  await openFixture(page, 'DraculasDentalDesaster.xmpuzzle');
  const ids = (await shapes(page)).map((s) => s.id);
  const largest = ids.at(-1) ?? '';
  // slow calls whose ids the new page will use again, the lowest last, so
  // their replies come after the new page has made its first calls
  await page.evaluate((shape) => {
    for (let i = 300; i >= 1; i--) {
      window.bt.send({ kind: 'call', id: i, method: 'shape.mesh', params: { shape } });
    }
  }, largest);
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => {
    if (m.type() === 'error') {
      errors.push(m.text());
    }
  });
  await page.reload();
  await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'running');
  // the new page's first calls (engine.info, settings.set, document.get) got their own replies;
  // they wait behind the 300 meshes (about 11 s on a quiet machine, more beside the other
  // e2e apps), so this waits longer than the usual 15 s
  await expect(page.locator('#shapeList .row')).toHaveCount(ids.length, { timeout: 60_000 });
  await expect(page.locator('#fname')).toHaveText('DraculasDentalDesaster.xmpuzzle');
  await expect(page.locator('#vp')).toHaveAttribute('data-shown', /^s\d+:drawing$/);
  expect(errors).toEqual([]);
});

test('a crashed page is reloaded and attaches to its document again (cleanup item 7.1)', async () => {
  const { app, page, userData } = a;
  await openFixture(page, 'CubeInCage.xmpuzzle');
  const rows = await page.locator('#shapeList .row').count();
  const main = app.process().pid;
  const engine = engines(main ?? 0);
  expect(engine).toHaveLength(1);
  await app.evaluate(({ BrowserWindow }) => {
    BrowserWindow.getAllWindows()[0]?.webContents.forcefullyCrashRenderer();
  });
  // Playwright's page is gone with its process: the new page is read
  // through the main process (a script sent while the page is replaced may
  // never answer: each try gives up after a second)
  const shown = (): Promise<{ name: string; rows: number; engine: string } | null> =>
    app.evaluate(async ({ BrowserWindow }) => {
      const c = BrowserWindow.getAllWindows()[0]?.webContents;
      if (c === undefined || c.isCrashed() || c.isLoading()) {
        return null;
      }
      const read = c
        .executeJavaScript(
          `({ name: document.getElementById('fname')?.textContent ?? '',
              rows: document.querySelectorAll('#shapeList .row').length,
              engine: document.getElementById('stage')?.dataset.engine ?? '' })`,
        )
        .catch(() => null) as Promise<{ name: string; rows: number; engine: string } | null>;
      return Promise.race([
        read,
        new Promise<null>((done) => {
          setTimeout(() => {
            done(null);
          }, 1000);
        }),
      ]);
    });
  await expect
    .poll(shown, { timeout: 30_000 })
    .toEqual({ name: 'CubeInCage.xmpuzzle', rows, engine: 'running' });
  // the same engine: its document was kept, not opened again
  expect(engines(main ?? 0)).toEqual(engine);
  const log = readFileSync(join(userData, 'logs', 'shapelogic.log'), 'utf8');
  expect(log).toMatch(
    /ERROR \[window \d+\] Renderer Gone: the page's process ended: (crashed|killed)/,
  );
  expect(log).toContain('Renderer Reloaded: reload 1 of 3');
});
