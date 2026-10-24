// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every row of the keyboard map fires its action (stage 6's gate: one e2e
// over the table). The test walks KEY_MAP (renderer/shell/keys.ts) in its
// order: each row whose keys include a key press has a scenario here that
// sets up what the row needs (a fixture, a workspace, a solve, an
// explorer), presses its keys and checks what they did; a row of mouse
// actions only names the e2e test that drives it. A row added to the table
// without either fails the test, as does a scenario whose row is gone.

import { copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { KEY_MAP } from '../../src/renderer/shell/keys.ts';
import type { KeyRow } from '../../src/renderer/shell/keys.ts';
import {
  answerDialogs,
  appOf,
  BUILT,
  camera,
  command,
  ENGINE,
  engineCall,
  FIXTURES,
  launch,
  openFixture,
  settle,
} from './app.ts';
import type { App } from './app.ts';
import { confirm, runButton, runEnded, solverSettled, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** A row's id: its group and keys. */
const rowId = (r: KeyRow): string => `${r.group}|${r.keys}`;

/** Whether a row names a key press (not only mouse actions, marked @). */
const hasKeys = (r: KeyRow): boolean =>
  r.keys.split(' / ').some((alt) => alt.split(' · ').some((part) => !part.includes('@')));

/** The rows of mouse actions only, and the e2e test that drives each. */
const MOUSE: Readonly<Record<string, string>> = {
  '3D view|@Left-drag': 'keys.e2e.ts: a drag orbits; O and P choose the drag mode',
  '3D view|Shift+@Left-drag': 'keys.e2e.ts: a drag orbits … F fits (the Shift-drag pans)',
  '3D view|@Middle-drag': 'view.e2e.ts',
  '3D view|@Wheel': 'keys.e2e.ts: a drag orbits … (the wheel zooms)',
  '3D view|@Click view-cube face, edge or corner': 'view.e2e.ts',
  '3D view|@Double-click a view-cube face': 'view.e2e.ts',
  'Shapes|Shift+@Click': 'entities.e2e.ts: the quick tools',
  'Shapes|Alt+@Click': 'entities.e2e.ts: the quick tools',
  'Shapes|Ctrl+@Click': 'entities.e2e.ts: the quick tools',
  'Shapes|@Right-click / @Right-drag': 'entities.e2e.ts: the right button',
  'Shapes|@Drag across cells': 'entities.e2e.ts: a drag is one undo step',
  'Shapes|@Right-click a row': 'shapes.e2e.ts: the menu',
  'Shapes|@Drag a row': 'shapes.e2e.ts: dragging a row onto another',
  'Puzzle|@Drag a chip in / @Drag a tile out': 'puzzle.e2e.ts',
  'Puzzle|@Right-click a tile or chip': 'puzzle.e2e.ts',
  'Puzzle|@Click a piece in the 3D view': 'puzzle.e2e.ts: a click in the scene',
  'Solver|@Double-click a row': 'solver.e2e.ts',
  'Solver|@Click a column header': 'solver.e2e.ts: sorted every way',
  'Solver|@Orbit or click in the 3D view': 'solver.e2e.ts',
  'Explorers|@Alt-drag a piece in the 3D view': 'explore.e2e.ts: an Alt-drag’s matching move',
};

const HOME = [-32, 26, 0, 1, 0, 0];

interface Ctx {
  a: App;
  page: Page;
  dir: string;
  /** What is open: a fixture and how far it got (solved, prepared). */
  state: string;
}

const stage = (p: Page) => p.locator('#stage');

async function snapshot(page: Page): Promise<{
  dirty: boolean;
  path: string | null;
  shapes: { id: string }[];
  problems: { id: string }[];
}> {
  return engineCall(page, 'document.get');
}

async function settled(page: Page): Promise<void> {
  await expect(stage(page)).toHaveAttribute('data-busy', '0');
  await expect
    .poll(async () => {
      const r = await engineCall<{ revision: number }>(page, 'document.get');
      return Number(await page.getAttribute('#stage', 'data-revision')) >= r.revision;
    })
    .toBe(true);
}

/** Opens a fixture (again, if something else or a changed copy is open) in a workspace. */
async function use(
  c: Ctx,
  state: 'bermuda' | 'six' | 'bermuda-solved' | 'bermuda-prepared',
): Promise<void> {
  const { page } = c;
  if (c.state !== state || (await page.getAttribute('#stage', 'data-dirty')) === '1') {
    await openFixture(page, state === 'six' ? 'SolidSixPieceBurrs.xmpuzzle' : 'Bermuda.xmpuzzle');
    c.state = state;
    if (state === 'bermuda-solved' || state === 'bermuda-prepared') {
      await toSolver(page);
      if (state === 'bermuda-solved') {
        await page.click('#slvMode [data-m="dis"]');
        await solverSettled(page);
        await runButton(page, 'start');
        await runEnded(page, 'finished');
      } else {
        await runButton(page, 'prepare');
        await runEnded(page, 'paused');
      }
    }
  }
  const tab = state === 'bermuda' ? 'Control+1' : 'Control+3';
  await page.keyboard.press(tab);
  await settled(page);
}

async function drawerClosed(page: Page): Promise<void> {
  if ((await page.getAttribute('#sdrawer', 'data-mode')) !== '') {
    await page.locator('#sdrawer .dl').focus();
    await page.keyboard.press('Escape');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', '');
  }
}

/** The scenarios, by row id. */
const SCENARIOS: Readonly<Record<string, (c: Ctx) => Promise<void>>> = {
  'Everywhere|Ctrl+1 / Ctrl+2 / Ctrl+3': async (c) => {
    await use(c, 'bermuda');
    for (const [k, t] of [
      ['2', 'puzzle'],
      ['3', 'solver'],
      ['1', 'entities'],
    ] as const) {
      await c.page.keyboard.press(`Control+${k}`);
      await expect(stage(c.page)).toHaveAttribute('data-tab', t);
    }
  },
  'Everywhere|F1': async (c) => {
    await c.page.keyboard.press('F1');
    await expect(c.page.locator('#helpKeys')).toBeVisible();
    await c.page.keyboard.press('Escape');
    await expect(c.page.locator('#helpKeys')).toHaveCount(0);
  },
  'Everywhere|Ctrl+,': async (c) => {
    await c.page.keyboard.press('Control+,');
    await expect(c.page.locator('#scrim')).toHaveClass(/show/);
    await c.page.keyboard.press('Escape');
    await expect(c.page.locator('#scrim')).not.toHaveClass(/show/);
  },
  'Everywhere|Ctrl+[ / Ctrl+]': async (c) => {
    for (const [k, cls] of [
      ['[', 'lc'],
      [']', 'rc'],
    ] as const) {
      await c.page.keyboard.press(`Control+${k}`);
      await expect(stage(c.page)).toHaveClass(new RegExp(`\\b${cls}\\b`));
      await c.page.keyboard.press(`Control+${k}`);
      await expect(stage(c.page)).not.toHaveClass(new RegExp(`\\b${cls}\\b`));
    }
  },
  'Everywhere|Ctrl+Space': async (c) => {
    await c.page.keyboard.press('Control+Space');
    await expect(stage(c.page)).toHaveClass(/\bf3d\b/);
    await c.page.keyboard.press('Control+Space');
    await expect(stage(c.page)).not.toHaveClass(/\bf3d\b/);
  },
  'Everywhere|Esc': async (c) => {
    // the step-back order: the menu first, then the focus
    await c.page.keyboard.press('Control+Space');
    await c.page.click('#bDisp');
    await expect(c.page.locator('#pop')).toBeVisible();
    await c.page.keyboard.press('Escape');
    await expect(c.page.locator('#pop')).toBeHidden();
    await expect(stage(c.page)).toHaveClass(/\bf3d\b/);
    await c.page.keyboard.press('Escape');
    await expect(stage(c.page)).not.toHaveClass(/\bf3d\b/);
  },
  'Everywhere|Ctrl+Z / Shift+Ctrl+Z': async (c) => {
    await use(c, 'bermuda');
    const { page } = c;
    const n = (await snapshot(page)).shapes.length;
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Control+d');
    await expect(page.locator('#shapeList .row')).toHaveCount(n + 1);
    await page.keyboard.press('Control+z');
    await expect(page.locator('#shapeList .row')).toHaveCount(n);
    await expect(page.locator('#stText')).toHaveText(/^Undone: /);
    await page.keyboard.press('Control+Shift+z');
    await expect(page.locator('#shapeList .row')).toHaveCount(n + 1);
    await expect(page.locator('#stText')).toHaveText(/^Redone: /);
    await settled(page);
  },
  'Everywhere|Ctrl+N / Ctrl+O': async (c) => {
    await use(c, 'bermuda');
    const { page } = c;
    await page.keyboard.press('Control+n');
    await expect(page.locator('#newPuzzle')).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(page.locator('#newPuzzle')).toHaveCount(0);
    await appOf(page).evaluate(({ dialog }) => {
      const g = globalThis as unknown as { asked: number };
      g.asked = 0;
      dialog.showOpenDialog = () => {
        g.asked++;
        return Promise.resolve({ canceled: true, filePaths: [] });
      };
    });
    await page.keyboard.press('Control+o');
    await expect
      .poll(() => appOf(page).evaluate(() => (globalThis as unknown as { asked: number }).asked))
      .toBe(1);
  },
  'Everywhere|Ctrl+S / Shift+Ctrl+S': async (c) => {
    const { page } = c;
    const file = join(c.dir, 'Bermuda.xmpuzzle');
    copyFileSync(join(FIXTURES, 'Bermuda.xmpuzzle'), file);
    await command(page, { cmd: 'openPath', path: file });
    await expect(stage(page)).toHaveAttribute('data-path', file);
    c.state = 'copy';
    await engineCall(page, 'document.setNotes', { text: 'saved by Ctrl+S' });
    await expect(stage(page)).toHaveAttribute('data-dirty', '1');
    await page.keyboard.press('Control+s');
    await expect(stage(page)).toHaveAttribute('data-dirty', '0');
    const other = join(c.dir, 'other.xmpuzzle');
    await answerDialogs(page, { save: other });
    await page.keyboard.press('Control+Shift+s');
    await expect(stage(page)).toHaveAttribute('data-path', other);
    expect((await snapshot(page)).path).toBe(other);
  },
  'Everywhere|Ctrl+W': async (c) => {
    const { page } = c;
    await engineCall(page, 'document.setNotes', { text: 'not saved' });
    await expect(stage(page)).toHaveAttribute('data-dirty', '1');
    await page.keyboard.press('Control+w');
    await expect(page.locator('#closeAsk')).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(page.locator('#closeAsk')).toHaveCount(0);
    expect(
      await appOf(page).evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length),
    ).toBe(1);
  },
  'Everywhere|Tab / Shift+Tab': async (c) => {
    await use(c, 'bermuda');
    const { page } = c;
    await page.focus('#btnSettings');
    await page.keyboard.press('Tab');
    expect(await page.evaluate(() => document.activeElement?.id)).not.toBe('btnSettings');
    await page.keyboard.press('Shift+Tab');
    expect(await page.evaluate(() => document.activeElement?.id)).toBe('btnSettings');
  },
  'Everywhere|Enter / Space': async (c) => {
    const { page } = c;
    for (const k of ['Enter', ' ']) {
      await page.focus('#btnSettings');
      await page.keyboard.press(k);
      await expect(page.locator('#scrim')).toHaveClass(/show/);
      await page.keyboard.press('Escape');
      await expect(page.locator('#scrim')).not.toHaveClass(/show/);
    }
  },

  '3D view|O / P': async (c) => {
    await c.page.keyboard.press('p');
    await expect(c.page.locator('.tbn[data-m="pan"]')).toHaveClass(/\bon\b/);
    await c.page.keyboard.press('o');
    await expect(c.page.locator('.tbn[data-m="orbit"]')).toHaveClass(/\bon\b/);
  },
  '3D view|Home': async (c) => {
    const { page } = c;
    const box = await page.locator('#cv').boundingBox();
    const x = (box?.x ?? 0) + (box?.width ?? 0) / 2;
    const y = (box?.y ?? 0) + (box?.height ?? 0) / 2 + 60;
    await page.mouse.move(x, y);
    await page.mouse.down();
    await page.mouse.move(x + 90, y + 30, { steps: 10 });
    await page.mouse.up();
    await settle(page);
    expect(await camera(page)).not.toEqual(HOME);
    await page.keyboard.press('Home');
    await settle(page);
    expect(await camera(page)).toEqual(HOME);
  },
  '3D view|F': async (c) => {
    const { page } = c;
    const box = await page.locator('#cv').boundingBox();
    await page.mouse.move((box?.x ?? 0) + 200, (box?.y ?? 0) + 300);
    await page.mouse.wheel(0, -400);
    await settle(page);
    expect((await camera(page))[3]).not.toBe(1);
    await page.keyboard.press('f');
    await settle(page);
    expect((await camera(page)).slice(3)).toEqual([1, 0, 0]);
  },
  '3D view|@Point at the top of the view / Tab / O / P / E / F': async (c) => {
    const { page } = c;
    await page.keyboard.press('Control+,');
    await page.click('#setPg [data-k="density"][data-o="Minimal"]');
    await page.keyboard.press('Escape');
    await page.mouse.move(5, 5);
    await expect(page.locator('#vt')).not.toHaveClass(/\bshow\b/);
    await page.keyboard.press('o');
    await expect(page.locator('#vt')).toHaveClass(/\bshow\b/);
    await page.keyboard.press('Control+,');
    await page.click('#setPg [data-k="density"][data-o="Standard"]');
    await page.keyboard.press('Escape');
  },

  'Shapes|1 / 2 / 3 / 4': async (c) => {
    await use(c, 'bermuda');
    for (const [k, t] of [
      ['2', 'variable'],
      ['3', 'erase'],
      ['1', 'fixed'],
    ] as const) {
      await c.page.keyboard.press(k);
      await expect(c.page.locator(`#toolStrip [data-t="${t}"]`)).toHaveClass(/\bon\b/);
    }
    // Paint is the brush by a restriction color (the document is dirty after: reopened)
    await c.page.click('#cAdd');
    await settled(c.page);
    await c.page.keyboard.press('4');
    await expect(c.page.locator('#cPaint')).toHaveClass(/\bon\b/);
    await c.page.keyboard.press('1');
    await expect(c.page.locator('#toolStrip [data-t="fixed"]')).toHaveClass(/\bon\b/);
  },
  'Shapes|E': async (c) => {
    await c.page.keyboard.press('e');
    await expect(c.page.locator('#bEdit')).toHaveClass(/\bon\b/);
    await c.page.keyboard.press('e');
    await expect(c.page.locator('#bEdit')).not.toHaveClass(/\bon\b/);
  },
  'Shapes|PgUp / PgDn': async (c) => {
    const { page } = c;
    // a shape of more than one layer: Bermuda's result
    const big = await page.$$eval('#shapeList .row', (rows) => rows.length);
    await page.click(`#shapeList .row >> nth=${String(big - 1)}`);
    await page.locator('#garea').hover();
    await page.keyboard.press('PageUp');
    await expect(page.locator('#lstrip .lchip[data-l="1"]')).toHaveClass(/\bon\b/);
    await page.keyboard.press('PageDown');
    await expect(page.locator('#lstrip .lchip[data-l="0"]')).toHaveClass(/\bon\b/);
  },
  'Shapes|↑ / ↓': async (c) => {
    const { page } = c;
    const ids = (await snapshot(page)).shapes.map((s) => s.id);
    await page.click(`#shapeList .row[data-id="${ids[0] ?? ''}"] .chip`);
    await page.locator('#shapeList').focus();
    await page.keyboard.press('ArrowDown');
    await expect(page.locator('#shapeList .row.sel')).toHaveAttribute('data-id', ids[1] ?? '');
    await page.keyboard.press('ArrowUp');
    await expect(page.locator('#shapeList .row.sel')).toHaveAttribute('data-id', ids[0] ?? '');
  },
  'Shapes|Alt+↑ / Alt+↓': async (c) => {
    const { page } = c;
    const ids = (await snapshot(page)).shapes.map((s) => s.id);
    await page.keyboard.press('Alt+ArrowDown');
    await expect
      .poll(async () => (await snapshot(page)).shapes.map((s) => s.id).slice(0, 2))
      .toEqual([ids[1], ids[0]]);
    await settled(page);
    await page.keyboard.press('Alt+ArrowUp');
    await expect.poll(async () => (await snapshot(page)).shapes.map((s) => s.id)).toEqual(ids);
    await settled(page);
  },
  'Shapes|F2 / Enter / @Double-click': async (c) => {
    const { page } = c;
    for (const k of ['F2', 'Enter']) {
      await page.locator('#shapeList').focus();
      await page.keyboard.press(k);
      await expect(page.locator('#shapeList .lbl input')).toBeVisible();
      await page.keyboard.press('Escape');
      await expect(page.locator('#shapeList .lbl input')).toHaveCount(0);
    }
  },
  'Shapes|Del': async (c) => {
    const { page } = c;
    const n = (await snapshot(page)).shapes.length;
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Delete');
    await expect(page.locator('#shapeList .row')).toHaveCount(n - 1);
    await settled(page);
    await page.keyboard.press('Control+z');
    await expect(page.locator('#shapeList .row')).toHaveCount(n);
    await settled(page);
  },
  'Shapes|Ctrl+D': async (c) => {
    const { page } = c;
    const n = (await snapshot(page)).shapes.length;
    await page.locator('#shapeList').focus();
    await page.keyboard.press('Control+d');
    await expect(page.locator('#shapeList .row')).toHaveCount(n + 1);
    await settled(page);
  },

  'Puzzle|+ / −': async (c) => {
    await use(c, 'bermuda');
    const { page } = c;
    await page.keyboard.press('Control+2');
    const problem = (await snapshot(page)).problems[0]?.id ?? '';
    const tile = page.locator('#pzUsed .ut').first();
    const shape = (await tile.getAttribute('data-u')) ?? '';
    await tile.locator('b').click();
    await expect(page.locator('#pzSel')).toHaveAttribute('data-shape', shape);
    const count = async (): Promise<number> =>
      (
        await engineCall<{ parts: { shape: string; max: number }[] }>(page, 'problem.get', {
          problem,
        })
      ).parts.find((p) => p.shape === shape)?.max ?? 0;
    const n = await count();
    await page.keyboard.press('+');
    await expect.poll(count).toBe(n + 1);
    await page.keyboard.press('-');
    await expect.poll(count).toBe(n);
    await settled(page);
  },
  'Puzzle|Del': async (c) => {
    const { page } = c;
    const tiles = await page.locator('#pzUsed .ut').count();
    await page.locator('#pzUsed .ut').first().locator('b').click();
    await page.keyboard.press('Delete');
    await expect(page.locator('#stText')).toHaveText('Removed from the puzzle');
    await expect(page.locator('#pzUsed .ut')).toHaveCount(tiles - 1);
    await settled(page);
    await page.keyboard.press('Control+z');
    await expect(page.locator('#pzUsed .ut')).toHaveCount(tiles);
    await settled(page);
  },
  'Puzzle|@Double-click a chip / +': async (c) => {
    const { page } = c;
    const problem = (await snapshot(page)).problems[0]?.id ?? '';
    const chip = page.locator('#pzLib .lc').first();
    const shape = (await chip.getAttribute('data-u')) ?? '';
    await chip.locator('b').click();
    await expect(page.locator('#pzSel')).toHaveAttribute('data-shape', shape);
    const count = async (): Promise<number> =>
      (
        await engineCall<{ parts: { shape: string; max: number }[] }>(page, 'problem.get', {
          problem,
        })
      ).parts.find((p) => p.shape === shape)?.max ?? 0;
    const n = await count();
    await page.keyboard.press('+');
    await expect.poll(count).toBe(n + 1);
    await settled(page);
  },

  'Solver|Space / Enter': async (c) => {
    await use(c, 'bermuda-solved');
    const { page } = c;
    await page.click('#slvList .lrow[data-i="0"]');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    for (const k of [' ', 'Enter']) {
      await page.keyboard.press(k);
      await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '1');
      await page.keyboard.press(k);
      await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '0');
    }
  },
  'Solver|← / →': async (c) => {
    const { page } = c;
    await page.keyboard.press('Shift+ArrowLeft');
    await expect(page.locator('#splayer')).toHaveAttribute('data-t', '0.000');
    await page.keyboard.press('ArrowRight');
    await expect(page.locator('#splayer')).toHaveAttribute('data-t', '1.000');
    await page.keyboard.press('ArrowLeft');
    await expect(page.locator('#splayer')).toHaveAttribute('data-t', '0.000');
  },
  'Solver|Shift+← / Shift+→': async (c) => {
    const { page } = c;
    const steps = (await page.getAttribute('#splayer', 'data-steps')) ?? '';
    await page.keyboard.press('Shift+ArrowRight');
    await expect(page.locator('#splayer')).toHaveAttribute('data-t', `${steps}.000`);
    await page.keyboard.press('Shift+ArrowLeft');
    await expect(page.locator('#splayer')).toHaveAttribute('data-t', '0.000');
  },
  'Solver|↑ / ↓': async (c) => {
    await use(c, 'six');
    const { page } = c;
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('ArrowDown');
    await expect(page.locator('#slvList .lrow.sel[data-i="1"]')).toHaveCount(1);
    await page.keyboard.press('ArrowUp');
    await expect(page.locator('#slvList .lrow.sel[data-i="0"]')).toHaveCount(1);
  },
  'Solver|Home / End': async (c) => {
    const { page } = c;
    const total = Number(await page.locator('#slvCount').textContent());
    await page.keyboard.press('End');
    await expect(page.locator(`#slvList .lrow.sel[data-i="${String(total - 1)}"]`)).toHaveCount(1);
    await page.keyboard.press('Home');
    await expect(page.locator('#slvList .lrow.sel[data-i="0"]')).toHaveCount(1);
  },
  'Solver|PgUp / PgDn': async (c) => {
    const { page } = c;
    await page.keyboard.press('PageDown');
    await expect(page.locator('#slvList .lrow.sel[data-i="8"]')).toHaveCount(1);
    await page.keyboard.press('PageUp');
    await expect(page.locator('#slvList .lrow.sel[data-i="0"]')).toHaveCount(1);
  },
  'Solver|Del': async (c) => {
    const { page } = c;
    await page.keyboard.press('Delete');
    await confirm(page, /^Delete 1 solution\?$/, false);
  },
  'Solver|Esc': async (c) => {
    await use(c, 'bermuda-solved');
    const { page } = c;
    await page.click('#slvList .lrow[data-i="0"]');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await page.keyboard.press(' ');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '1');
    await page.keyboard.press('Escape');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '0');
  },
  'Solver|Space / Enter · ← / →': async (c) => {
    const { page } = c;
    const chips = page.locator('#slvPcs .pvc');
    const first = (await chips.first().getAttribute('data-p')) ?? '';
    const second = (await chips.nth(1).getAttribute('data-p')) ?? '';
    await chips.first().focus();
    await page.keyboard.press('Enter');
    await expect(page.locator(`#slvPcs .pvc[data-p="${first}"]`)).toHaveAttribute(
      'data-mode',
      'wire',
    );
    await page.keyboard.press(' ');
    await expect(page.locator(`#slvPcs .pvc[data-p="${first}"]`)).toHaveAttribute(
      'data-mode',
      'hidden',
    );
    await page.keyboard.press('ArrowRight');
    expect(
      await page.evaluate(() => (document.activeElement as HTMLElement | null)?.dataset.p),
    ).toBe(second);
    await page.keyboard.press('ArrowLeft');
    expect(
      await page.evaluate(() => (document.activeElement as HTMLElement | null)?.dataset.p),
    ).toBe(first);
    await page.click('#slvShowAll');
  },

  'Explorers|↑ / ↓ · ← / →': async (c) => {
    await use(c, 'bermuda-prepared');
    const { page } = c;
    await page.click('#slvAdv [data-x="placements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'placements');
    // the placements read (a handle, every piece's count) before a key
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^ax/);
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-busy', '0');
    await expect(page.locator('#sdrawer .dl [data-exr="1"]')).toHaveAttribute('data-count', /\d/);
    await page.click('#sdrawer .dl [data-exr="0"]');
    await page.keyboard.press('ArrowDown');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', '1');
    await page.keyboard.press('ArrowUp');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', '0');
    await page.keyboard.press('ArrowRight');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-piece', '1');
    await page.keyboard.press('ArrowLeft');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-piece', '0');
  },
  'Explorers|Home / End': async (c) => {
    const { page } = c;
    const n = Number(await page.locator('#sdrawer .dl [data-exr="0"]').getAttribute('data-count'));
    await page.keyboard.press('End');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', String(n - 1));
    await page.keyboard.press('Home');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', '0');
  },
  'Explorers|Space / Enter': async (c) => {
    const { page } = c;
    await page.click('#slvAdv [data-x="step"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'step');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-steps', '1');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-busy', '0');
    // the drawer's list has the focus
    await page.locator('#sdrawer .dl').focus();
    await page.keyboard.press(' ');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-steps', '2');
    await page.keyboard.press('Enter');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-steps', '3');
    await drawerClosed(page);
  },
  'Explorers|↑ / ↓ · ← / → · Enter': async (c) => {
    await use(c, 'bermuda-solved');
    const { page } = c;
    await page.click('#slvList .lrow[data-i="0"]');
    await page.click('#slvAdv [data-x="movements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'movements');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^mx/);
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-busy', '0');
    await expect(page.locator('#sdrawer .dl [data-node]')).toHaveCount(1);
    const root = (await page.getAttribute('#sdrawer', 'data-node')) ?? '';
    await page.click(`#sdrawer .dl [data-node="${root}"]`);
    await page.keyboard.press('Enter');
    await expect.poll(() => page.locator('#sdrawer .dl [data-node]').count()).toBeGreaterThan(1);
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-busy', '0');
    await page.keyboard.press('ArrowRight');
    await expect(page.locator('#sdrawer')).not.toHaveAttribute('data-node', root);
    const child = (await page.getAttribute('#sdrawer', 'data-node')) ?? '';
    await page.keyboard.press('ArrowDown');
    await expect(page.locator('#sdrawer')).not.toHaveAttribute('data-node', child);
    await page.keyboard.press('ArrowUp');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', child);
    await page.keyboard.press('ArrowLeft');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', root);
  },
  'Explorers|Esc': async (c) => {
    const { page } = c;
    await page.locator('#sdrawer .dl').focus();
    await page.keyboard.press('Escape');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', '');
  },

  'Dialogs and menus|Esc': async (c) => {
    const { page } = c;
    await page.keyboard.press('Control+,');
    await expect(page.locator('#scrim')).toHaveClass(/show/);
    await page.keyboard.press('Escape');
    await expect(page.locator('#scrim')).not.toHaveClass(/show/);
    await page.click('#bDisp');
    await expect(page.locator('#pop')).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(page.locator('#pop')).toBeHidden();
  },
  'Dialogs and menus|Enter': async (c) => {
    await use(c, 'bermuda');
    const { page } = c;
    const first = (await snapshot(page)).shapes[0]?.id ?? '';
    await page.click(`#shapeList .row[data-id="${first}"] .chip`);
    await page.keyboard.press('F2');
    await page.locator('#shapeList .lbl input').fill('Renamed');
    await page.keyboard.press('Enter');
    await expect
      .poll(
        async () =>
          (
            await engineCall<{ shapes: { id: string; name: string }[] }>(page, 'document.get')
          ).shapes.find((s) => s.id === first)?.name,
      )
      .toBe('Renamed');
    await settled(page);
  },
  'Dialogs and menus|Space / Enter': async (c) => {
    const { page } = c;
    await page.keyboard.press('Control+,');
    const sw = page.locator('#setPg .switch[data-k="tips"]');
    await sw.focus();
    await page.keyboard.press(' ');
    await expect(sw).not.toHaveClass(/\bon\b/);
    await page.keyboard.press('Enter');
    await expect(page.locator('#setPg .switch[data-k="tips"]')).toHaveClass(/\bon\b/);
    await page.keyboard.press('Escape');
  },
};

test('every row of the keyboard map fires its action', async () => {
  test.setTimeout(240_000);
  const keyRows = KEY_MAP.filter(hasKeys).map(rowId);
  const mouseRows = KEY_MAP.filter((r) => !hasKeys(r)).map(rowId);
  // every row is driven here or named with the test that drives it
  expect(keyRows.filter((id) => !(id in SCENARIOS))).toEqual([]);
  expect(mouseRows.filter((id) => !(id in MOUSE))).toEqual([]);
  expect(Object.keys(SCENARIOS).filter((id) => !keyRows.includes(id))).toEqual([]);
  expect(Object.keys(MOUSE).filter((id) => !mouseRows.includes(id))).toEqual([]);

  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-keymap-'));
  const a = await launch();
  const c: Ctx = { a, page: a.page, dir, state: '' };
  try {
    for (const id of keyRows) {
      const run = SCENARIOS[id];
      if (run !== undefined) {
        await test.step(id, () => run(c));
      }
    }
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
