// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell rows of the keyboard map (shell/keys.ts, docs/webui-spec.md
// section 6): Ctrl+1/2/3, F1, Ctrl+,, Ctrl+[ and Ctrl+], Ctrl+Space, Esc's
// step-back order, O / P, Home and F; the layout kept per workspace; Tab
// kept inside the settings dialog; the Top Up and Free Spin rotation methods.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, camera, ENGINE, launch, openFixture, settle } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;
test.beforeEach(async () => {
  a = await launch();
  await openFixture(a.page, 'Bermuda.xmpuzzle');
});
test.afterEach(async () => {
  await a.close();
});

const stage = (p: Page) => p.locator('#stage');

/** Drags on the 3D view from its centre by (dx, dy), in steps. */
async function dragView(p: Page, dx: number, dy: number, mods: string[] = []): Promise<void> {
  const box = await p.locator('#cv').boundingBox();
  const x = (box?.x ?? 0) + (box?.width ?? 0) / 2;
  const y = (box?.y ?? 0) + (box?.height ?? 0) / 2 + 60;
  for (const m of mods) {
    await p.keyboard.down(m);
  }
  await p.mouse.move(x, y);
  await p.mouse.down();
  await p.mouse.move(x + dx, y + dy, { steps: 12 });
  await p.mouse.up();
  for (const m of mods) {
    await p.keyboard.up(m);
  }
  await settle(p);
}

test('Ctrl+1, 2, 3 switch the workspace', async () => {
  const { page } = a;
  await page.keyboard.press('Control+2');
  await expect(stage(page)).toHaveAttribute('data-tab', 'puzzle');
  await page.keyboard.press('Control+3');
  await expect(stage(page)).toHaveAttribute('data-tab', 'solver');
  await page.keyboard.press('Control+1');
  await expect(stage(page)).toHaveAttribute('data-tab', 'entities');
  await expect(page.locator('.rbtn.on')).toHaveAttribute('data-tab', 'entities');
});

test('Ctrl+, opens the settings, F1 the shortcuts, Esc closes; Tab stays in the dialogs', async () => {
  const { page } = a;
  await page.keyboard.press('Control+,');
  await expect(page.locator('#scrim')).toHaveClass(/show/);
  await expect(page.locator('#setSearch')).toBeFocused();
  for (let i = 0; i < 25; i++) {
    await page.keyboard.press('Tab');
  }
  expect(
    await page.evaluate(() => document.getElementById('scrim')?.contains(document.activeElement)),
  ).toBe(true);
  await page.keyboard.press('Escape');
  await expect(page.locator('#scrim')).not.toHaveClass(/show/);
  await page.keyboard.press('F1');
  // F1 is Help ▸ Keyboard Shortcuts: a dialog of its own over the one table
  await expect(page.locator('#helpKeys')).toBeVisible();
  for (const g of [
    'Everywhere',
    '3D view',
    'Shapes',
    'Puzzle',
    'Solver',
    'Explorers',
    'Dialogs and menus',
  ]) {
    await expect(page.locator('#helpKeys .kbgrp', { hasText: g }).first()).toBeVisible();
  }
  await page.keyboard.type('redo');
  await expect(page.locator('#helpKeys .kbrow')).toHaveCount(1);
  await page.keyboard.press('Escape');
  await expect(page.locator('#helpKeys')).toHaveCount(0);
  // the settings dialog's page prints the same table
  await page.keyboard.press('Control+,');
  await page.click('#setNav [data-p="keys"]');
  await expect(page.locator('#setPg .kbgrp', { hasText: 'Explorers' })).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.locator('#scrim')).not.toHaveClass(/show/);
});

test('Ctrl+[ and Ctrl+] collapse the cards, and each workspace keeps its layout', async () => {
  const { page } = a;
  await page.keyboard.press('Control+[');
  await expect(stage(page)).toHaveClass(/\blc\b/);
  await page.keyboard.press('Control+]');
  await expect(stage(page)).toHaveClass(/\brc\b/);
  await page.keyboard.press('Control+2');
  await expect(stage(page)).not.toHaveClass(/\blc\b|\brc\b/);
  await page.keyboard.press('Control+]');
  await expect(stage(page)).toHaveClass(/\brc\b/);
  await page.keyboard.press('Control+1');
  await expect(stage(page)).toHaveClass(/\blc\b/);
  await expect(stage(page)).toHaveClass(/\brc\b/);
  await page.keyboard.press('Control+[');
  await expect(stage(page)).not.toHaveClass(/\blc\b/);
  await page.keyboard.press('Control+2');
  await expect(stage(page)).toHaveClass(/\brc\b/);
  await expect(stage(page)).not.toHaveClass(/\blc\b/);
});

test('Ctrl+Space focuses the 3D view; Esc steps back: menu first, then the focus', async () => {
  const { page } = a;
  await page.keyboard.press('Control+Space');
  await expect(stage(page)).toHaveClass(/\bf3d\b/);
  await page.click('#bDisp');
  await expect(page.locator('#pop')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.locator('#pop')).toBeHidden();
  await expect(stage(page)).toHaveClass(/\bf3d\b/);
  await page.keyboard.press('Escape');
  await expect(stage(page)).not.toHaveClass(/\bf3d\b/);
  await page.keyboard.press('Control+Space');
  await expect(stage(page)).toHaveClass(/\bf3d\b/);
  await page.keyboard.press('Control+Space');
  await expect(stage(page)).not.toHaveClass(/\bf3d\b/);
});

test('O and P choose the drag mode', async () => {
  const { page } = a;
  await page.keyboard.press('p');
  await expect(page.locator('.tbn[data-m="pan"]')).toHaveClass(/\bon\b/);
  await expect(page.locator('#navHint')).toHaveText(/^Drag pan/);
  await dragView(page, 80, 40);
  const panned = await camera(page);
  expect(panned.slice(0, 2)).toEqual([-32, 26]);
  expect(panned[4]).toBeGreaterThan(60);
  await page.keyboard.press('o');
  await expect(page.locator('.tbn[data-m="orbit"]')).toHaveClass(/\bon\b/);
  await expect(page.locator('#navHint')).toHaveText(/^Drag orbit/);
});

test('a drag orbits (Top Up: half a degree a pixel), Home goes home, F fits keeping the turn', async () => {
  const { page } = a;
  await page.click('#btnSettings');
  await page.click('#setNav [data-p="view"]');
  await page.click('#setPg [data-k="rot"][data-o="Top Up"]');
  await page.keyboard.press('Escape');
  await dragView(page, 100, 0);
  const turned = await camera(page);
  // the first move past 4 px starts the drag; the rest turns
  expect(turned[0]).toBeGreaterThan(-32 + 40);
  expect(turned[0]).toBeLessThan(-32 + 50.5);
  expect(turned[1]).toBeCloseTo(26, 1);
  // zoom and pan, then F: framed again, the turn kept
  const box = await page.locator('#cv').boundingBox();
  await page.mouse.move((box?.x ?? 0) + 200, (box?.y ?? 0) + 300);
  await page.mouse.wheel(0, -400);
  await settle(page);
  await dragView(page, 50, 30, ['Shift']);
  const moved = await camera(page);
  expect(moved[3]).toBeGreaterThan(1.2);
  expect(moved[4]).not.toBe(0);
  await page.keyboard.press('f');
  await settle(page);
  const fitted = await camera(page);
  expect(fitted.slice(3)).toEqual([1, 0, 0]);
  expect(fitted[0]).toBeCloseTo(turned[0] ?? 0, 1);
  expect(fitted[1]).toBeCloseTo(turned[1] ?? 0, 1);
  await page.keyboard.press('Home');
  await settle(page);
  expect(await camera(page)).toEqual([-32, 26, 0, 1, 0, 0]);
});

test('the Free Spin method (the arc-ball) turns the view as the original did', async () => {
  const { page } = a;
  await page.click('#btnSettings');
  await page.click('#setNav [data-p="view"]');
  await page.click('#setPg [data-k="rot"][data-o="Free Spin"]');
  await page.keyboard.press('Escape');
  // a drag along the screen's x turns about the screen's y: the view's roll stays, the pitch too
  await dragView(page, 0, -120);
  const c = await camera(page);
  expect(c[1]).not.toBeCloseTo(26, 0);
  await page.keyboard.press('Home');
  await settle(page);
  expect(await camera(page)).toEqual([-32, 26, 0, 1, 0, 0]);
});

test('settings apply at once: theme, density, tooltips', async () => {
  const { page } = a;
  const html = page.locator('html');
  await page.keyboard.press('Control+,');
  await page.click('#setPg [data-k="theme"][data-o="Dark"]');
  await expect(html).toHaveAttribute('data-theme', 'dark');
  await page.click('#setPg [data-k="density"][data-o="Minimal"]');
  await expect(html).toHaveAttribute('data-density', 'minimal');
  // Minimal hides captions: the controls carry them as tooltips
  await expect(page.locator('.rbtn[data-tab="puzzle"]')).toHaveAttribute('title', /Puzzle/);
  await page.click('#setPg .switch[data-k="tips"]');
  await expect(page.locator('#btnSettings')).not.toHaveAttribute('title', /./);
  await page.click('#setPg .switch[data-k="tips"]');
  await expect(page.locator('#btnSettings')).toHaveAttribute('title', /Settings/);
  // undo depth and threads are the engine's
  await page.selectOption('#setPg select[data-k="undo"]', '100');
  await page.click('#setNav [data-p="perf"]');
  await page.locator('#setPg input[type=range]').fill('1');
  const engineSettings = (): Promise<unknown> =>
    page.evaluate(
      () =>
        new Promise((done) => {
          const id = 2_000_000_000;
          const off = window.bt.onMessage((m) => {
            if (m.kind === 'reply' && m.id === id) {
              off();
              done(m.result);
            }
          });
          window.bt.send({ kind: 'call', id, method: 'settings.get' });
        }),
    );
  await expect.poll(engineSettings).toMatchObject({ undoDepth: 100, threads: 1 });
  await page.click('#setDefaults');
  await expect.poll(engineSettings).toMatchObject({ undoDepth: 25, threads: 0 });
  await expect(html).not.toHaveAttribute('data-theme', /./);
  await expect(html).toHaveAttribute('data-density', 'standard');
  await page.keyboard.press('Escape');
});
