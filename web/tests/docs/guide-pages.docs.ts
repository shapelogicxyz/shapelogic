// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screenshots of the user guide's reference pages (the 3D Model Viewer,
// the Shapes, Puzzle, Solver and Play tabs, color restrictions, piece
// rotations, imports and exports, keyboard shortcuts, solve notation):
// whole-window shots and crops of single cards, toolbars and menus, so a
// page can show the button it talks about. Written to
// site/src/assets/screens/guide. Not a test of the app: run it with the
// other guide screenshots (`just docs-screenshots`) when the UI changes.

import { mkdirSync } from 'node:fs';
import { basename, resolve } from 'node:path';
import { crc32, deflateSync } from 'node:zlib';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  BUILT,
  command,
  display,
  ENGINE,
  launch,
  menu,
  openFixture,
  FIXTURES,
  ROOT,
  selectShape,
  settle,
  shapes,
  style,
} from '../e2e/app.ts';
import { carry, drag, expectStacks, STACK, STAGE, toPlay, toStackPlay } from '../e2e/play.ts';
import { runButton, runEnded, toSolver } from '../e2e/solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const OUT = resolve(ROOT, 'site/src/assets/screens/guide');

/** The whole window. */
async function shot(page: Page, name: string): Promise<void> {
  await settle(page);
  await page.screenshot({ path: resolve(OUT, `${name}.png`) });
}

/** One element of the window, scrolled into sight. */
async function crop(page: Page, selector: string, name: string): Promise<void> {
  await settle(page);
  const el = page.locator(selector).first();
  await el.scrollIntoViewIfNeeded();
  await el.screenshot({ path: resolve(OUT, `${name}.png`) });
}

/** A rectangle of the window, in CSS pixels. */
interface Box {
  x: number;
  y: number;
  width: number;
  height: number;
}

/**
 * One element of the window cut off below what it holds: down to the lowest
 * of its visible leaf elements, and a margin. For cards and strips whose box
 * runs on below their contents.
 */
async function cropContent(page: Page, selector: string, name: string, pad = 10): Promise<void> {
  await settle(page);
  const el = page.locator(selector).first();
  await el.scrollIntoViewIfNeeded();
  const clip = await el.evaluate((root, margin): Box => {
    const outer = root.getBoundingClientRect();
    let bottom = outer.top;
    for (const e of root.querySelectorAll('*')) {
      const r = e.getBoundingClientRect();
      if (e.children.length === 0 && r.width > 0 && r.height > 0) {
        bottom = Math.max(bottom, r.bottom);
      }
    }
    return {
      x: outer.left,
      y: outer.top,
      width: outer.width,
      height: Math.min(outer.height, bottom - outer.top + margin),
    };
  }, pad);
  await page.screenshot({ path: resolve(OUT, `${name}.png`), clip });
}

/** One element of the window from its top down to where another element starts. */
async function cropAbove(page: Page, selector: string, stop: string, name: string): Promise<void> {
  await settle(page);
  const outer = await page.locator(selector).first().boundingBox();
  const end = await page.locator(stop).first().boundingBox();
  if (outer === null || end === null) {
    throw new Error(`${selector} or ${stop} is not on screen`);
  }
  await page.screenshot({
    path: resolve(OUT, `${name}.png`),
    clip: { x: outer.x, y: outer.y, width: outer.width, height: end.y - outer.y },
  });
}

/** Opens a collapsed section of the Edit shape inspector. */
async function openSection(page: Page, id: string): Promise<void> {
  const sec = page.locator(`#${id}`);
  if ((await sec.getAttribute('class'))?.includes('closed') === true) {
    await sec.locator(':scope > .sh').click();
  }
  await expect(sec).not.toHaveClass(/closed/);
}

/** Opens the Solver's search options. */
async function openOptions(page: Page): Promise<void> {
  if (!(await page.locator('#slvOpts').isVisible())) {
    await page.click('#slvOptHead');
  }
  await expect(page.locator('#slvOpts')).toBeVisible();
}

test.beforeAll(() => {
  mkdirSync(OUT, { recursive: true });
});

test('the 3D view and the Shapes tab', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    await shot(page, 'shapes-tab');
    // the 3D view's parts
    await crop(page, '#vt', 'view-toolbar');
    await crop(page, '#vcube', 'viewcube');
    await crop(page, '#vhint', 'view-hint');
    await page.click('#bDisp');
    await crop(page, '#pop', 'display-menu');
    await page.keyboard.press('Escape');
    await expect(page.locator('#pop')).toBeHidden();

    // the left card: the shapes and the Edit shape inspector
    await crop(page, '#left .lfull > .ch', 'shapes-header');
    await crop(page, '#shapeList .row.sel', 'shapes-row');
    await crop(page, '#secTf', 'inspector-transform');
    await openSection(page, 'secFit');
    await crop(page, '#secFit', 'inspector-fit');
    await openSection(page, 'secTools');
    await crop(page, '#secTools', 'inspector-repair');
    await page.click('#shapeList .row.sel', { button: 'right' });
    await crop(page, '#pop', 'shape-menu');
    await page.keyboard.press('Escape');

    // the right card: the voxel editor
    await crop(page, '#right', 'voxel-editor');
    await crop(page, '#toolStrip', 'editor-tools');
    // Mirror and the planes are behind the Mirror & Plane button
    await page.click('#modRow [data-mp="1"]');
    await crop(page, '#modRow', 'editor-mirror-span');
    await crop(page, '#right .subbar', 'editor-planes');
    await cropContent(page, '#lstrip', 'editor-layers');
    await crop(page, '#gridSize', 'editor-grid-size');
    await crop(page, '#colFoot', 'editor-colors');
  } finally {
    await app.close();
  }
});

test('the Solver tab and the display styles', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    await toSolver(page);
    // the take-apart on, so the solutions have levels
    await page.click('#slvMode [data-m="dis"]');
    await openOptions(page);
    await crop(page, '#slvMode', 'solver-mode');
    await crop(page, '#slvOpts', 'solver-options');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await page.locator('#leftS .pz-scroll').evaluate((e) => {
      e.scrollTop = 0;
    });
    await shot(page, 'solver-tab');
    await crop(page, '#leftS .slv-sec:has(#slvRun)', 'solver-run');
    await crop(page, '#slvSortSec', 'solver-sort');
    await crop(page, '#leftS .slv-sec:has(.slv-adv)', 'solver-explorer');
    await crop(page, '#rightS', 'solutions-card');
    await crop(page, '#slvSel', 'solutions-list');
    await crop(page, '#splayer', 'solutions-player');
    await cropContent(page, '#slvPcs', 'solutions-pieces');
    await page.click('#slvAct [data-a="manage"]');
    await crop(page, '#pop', 'solutions-manage');
    await page.keyboard.press('Escape');
    await page.click('#slvViewSeg [data-v="slider"]');
    await cropContent(page, '#slvSel', 'solutions-slider');
    await page.click('#slvViewSeg [data-v="list"]');

    // the Display menu's styles, on the solution shown
    for (const s of ['drawing', 'flat', 'wireframe', 'stl', 'uniform'] as const) {
      await style(page, s);
      await crop(page, '#vp', `style-${s}`);
    }
    await style(page, 'drawing');
    await display(page, 'xray');
    await settle(page);
    await crop(page, '#vp', 'style-xray');
    await display(page, 'xray');
    await display(page, 'ortho');
    await settle(page);
    await crop(page, '#vp', 'projection-ortho');
    await display(page, 'persp');

    // the exports that need a solution
    await menu(page, 'export.glb');
    await crop(page, '#exportGlb .cfm', 'export-glb');
    await page.keyboard.press('Escape');
    await page.click('#slvAct [data-a="export"]');
    await page.click('#pop [data-k="pieces"]');
    await crop(page, '#exportStlSolution .cfm', 'export-stl-solution');
    await page.keyboard.press('Escape');
  } finally {
    await app.close();
  }
});

test('the Puzzle tab and color restrictions', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openFixture(page, 'DraculasDentalDesaster.xmpuzzle');
    // the voxel colors of the first shape, then as the constraint colors
    await crop(page, '#colFoot', 'colors-palette');
    await crop(page, '#vp', 'colors-piece-color');
    await display(page, 'cvox');
    await settle(page);
    await crop(page, '#vp', 'colors-voxel-color');
    await display(page, 'cpiece');

    await page.keyboard.press('Control+2');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'puzzle');
    await shot(page, 'puzzle-tab');
    await crop(page, '#leftP', 'puzzle-left');
    await crop(page, '#rightP', 'puzzle-right');
    await crop(page, '#pzRules', 'colors-rules');
    if ((await page.locator('#pzGrpBody').getAttribute('class'))?.includes('closed') === true) {
      await page.click('#pzGrpHead');
    }
    await crop(page, '#pzGrpBody', 'puzzle-groups');
    await crop(page, '#pzSummary', 'puzzle-summary');
    // a piece chosen: its count, range and group
    await page
      .locator('#pzUsed [data-u]')
      .first()
      .click({ position: { x: 60, y: 12 } });
    await crop(page, '#pzSel', 'puzzle-selected');
    await cropAbove(page, '#rightP', '#pzRulesHead', 'puzzle-right-selected');
    await shot(page, 'puzzle-tab-selected');
  } finally {
    await app.close();
  }
});

test('the exports, the keys and the rotations', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    await menu(page, 'export.stl');
    await expect(page.locator('#exportStl .cfm')).toBeVisible();
    // the pieces to export: the result has variable voxels, which the STL export refuses
    await page.click('#exportStl [data-a="addAll"]');
    await page.selectOption('#exportStl [data-f="chosen"]', { index: 0 });
    await page.click('#exportStl [data-a="remove"]');
    await page.selectOption('#exportStl [data-f="chosen"]', { index: 0 });
    await page.waitForTimeout(2000);
    await crop(page, '#exportStl .cfm', 'export-stl');
    await page.keyboard.press('Escape');
    await menu(page, 'export.image');
    await crop(page, '#exportImage .cfm', 'export-image');
    await page.keyboard.press('Escape');
    await page.keyboard.press('F1');
    await expect(page.locator('#helpKeys .cfm')).toBeVisible();
    await shot(page, 'keyboard-help');
    await page.keyboard.press('Escape');

    // a puzzle that needs a piece turned: Check Rotations on
    await openFixture(page, 'test_rotation_solvers.xmpuzzle');
    await toSolver(page);
    await page.click('#slvMode [data-m="dis"]');
    const rot = page.locator('#slvMode .switch[data-o="checkRotations"]');
    if ((await rot.getAttribute('aria-checked')) !== 'true') {
      await rot.click();
    }
    await expect(rot).toHaveAttribute('aria-checked', 'true');
    await crop(page, '#slvMode', 'rotations-mode');
    await runButton(page, 'start');
    await runEnded(page, 'finished', 170_000);
    await shot(page, 'rotations-solved');
    await crop(page, '#slvSel', 'rotations-list');
    await page.click('#slvViewSeg [data-v="slider"]');
    await cropContent(page, '#slvSel', 'rotations-slider');
  } finally {
    await app.close();
  }
});

/** Opens a fixture in a folder of tests/data/puzzles (openFixture takes top-level names only). */
async function openNested(page: Page, name: string): Promise<void> {
  const before = await page.locator('#stage').getAttribute('data-generation');
  await command(page, { cmd: 'openPath', path: resolve(FIXTURES, name) });
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
  await expect(page.locator('#fname')).toHaveText(basename(name));
  await settle(page);
}

/** The cells the pieces on the Play board stand at. */
async function pieceCells(page: Page): Promise<string[]> {
  return page
    .locator('.pl-piece')
    .evaluateAll((els) => els.map((e) => e.getAttribute('data-at') ?? ''));
}

/**
 * Makes one move with the first piece that can slide a cell, leaving the
 * pieces at `skip` alone; returns the cell the moved piece now stands at.
 */
async function oneMove(page: Page, skip: string[]): Promise<string> {
  const stage = page.locator(STAGE);
  for (const at of await pieceCells(page)) {
    if (skip.includes(at)) {
      continue;
    }
    for (const step of [
      [0, 1],
      [0, -1],
      [1, 0],
      [-1, 0],
    ] as [number, number][]) {
      const moves = Number(await stage.getAttribute('data-moves'));
      const before = await pieceCells(page);
      await drag(page, at, [step]);
      await page.waitForTimeout(300);
      if (Number(await stage.getAttribute('data-moves')) === moves + 1) {
        const now = await pieceCells(page);
        const moved = now.find((c) => !before.includes(c));
        if (moved === undefined) {
          throw new Error('a move was counted but no piece moved');
        }
        return moved;
      }
    }
  }
  throw new Error('no piece can move');
}

test('the Play tab', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openNested(page, 'sliding/slide_klotski.xmpuzzle');
    await toPlay(page);
    // two moves made, so the count and the playback bar have something to show
    const first = await oneMove(page, []);
    await oneMove(page, [first]);
    await expect(page.locator(STAGE)).toHaveAttribute('data-moves', '2');
    await page.mouse.move(0, 0);
    await shot(page, 'play-tab');
    await cropContent(page, '#leftPl', 'play-left');
    await crop(page, '#plMid .pl-side', 'play-side');
    await crop(page, '#plBar', 'play-bar');
    // the board drawn flat
    await page.click('#plSimple');
    await expect(page.locator('#plSimple')).toHaveAttribute('aria-pressed', 'true');
    await page.mouse.move(0, 0);
    await crop(page, '#playBoard', 'play-simple');
    await page.click('#plSimple');
    await expect(page.locator('#plSimple')).toHaveAttribute('aria-pressed', 'false');
    // the goal in its own window, GOAL above it
    const opened = app.app.waitForEvent('window');
    await page.click('#plGoalWin');
    const goal = await opened;
    await expect(goal.locator('h1')).toHaveText('GOAL');
    await expect(goal.locator('img')).toBeVisible();
    await goal.waitForTimeout(400);
    await goal.screenshot({ path: resolve(OUT, 'play-goal-window.png') });
  } finally {
    await app.close();
  }
});

/**
 * A w x h PNG of a sunset: a sky going from blue to orange, a sun low in
 * it and dark hills, so a picture on a piece reads as one in a screenshot.
 */
function sunset(w: number, h: number): Buffer {
  const chunk = (name: string, data: Buffer): Buffer => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(name, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body));
    return Buffer.concat([len, body, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; // bits
  ihdr[9] = 2; // RGB
  const rows = Buffer.alloc(h * (w * 3 + 1));
  const mix = (a: number, b: number, t: number): number => Math.round(a + (b - a) * t);
  for (let y = 0; y < h; y++) {
    const row = y * (w * 3 + 1);
    for (let x = 0; x < w; x++) {
      const t = y / h;
      let c = [mix(42, 250, t), mix(58, 150, t), mix(140, 70, t)];
      if (Math.hypot(x - w * 0.55, y - h * 0.62) < w * 0.17) {
        c = [255, 222, 120];
      }
      const hill = h * (0.74 + 0.08 * Math.sin((x / w) * Math.PI * 2.2));
      if (y > hill) {
        c = [mix(60, 30, (y - hill) / h), mix(48, 26, (y - hill) / h), 70];
      }
      rows.set(c, row + 1 + x * 3);
    }
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(rows)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

test('a picture on a sliding piece', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    await openNested(page, 'sliding/slide_klotski.xmpuzzle');
    await page.keyboard.press('Control+1');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'entities');
    // the big square piece, S1: the first piece after the board
    const big = (await shapes(page)).find((s) => s.label === 'S1')?.id ?? '';
    await selectShape(page, big);
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await expect(dlg).toBeVisible();
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'sunset.png',
      mimeType: 'image/png',
      buffer: sunset(320, 240),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('sunset.png');
    await page.mouse.move(0, 0);
    await crop(page, '#imageDlg .cfm', 'image-dialog');
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await expect(page.locator('#cImage')).toHaveClass(/\bon\b/);
    // the piece on the Play board, its picture on it
    await toPlay(page);
    await expect(page.locator('.pl-piece image')).toHaveCount(1);
    await page.mouse.move(0, 0);
    await crop(page, '#playBoard', 'play-picture');
  } finally {
    await app.close();
  }
});

test('the Play tab on a stacking puzzle', async () => {
  const app = await launch({ scale: 2 });
  const { page } = app;
  try {
    // the Tower of Hanoi the guide's Stacking page follows, two transfers made
    await openNested(page, 'stacking/stack_hanoi_3.xmpuzzle');
    await toStackPlay(page);
    await carry(page, 0, 2);
    await carry(page, 0, 1);
    await expect(page.locator(STAGE)).toHaveAttribute('data-moves', '2');
    await page.mouse.move(0, 0);
    // the discs have come to rest
    await page.waitForTimeout(400);
    await shot(page, 'play-stacking');
    // a Panex board: a small disc on the middle column, the next raised into the bridge over it
    await openNested(page, 'stacking/stack_panex_swap_4.xmpuzzle');
    await toStackPlay(page);
    await carry(page, 2, 1);
    await carry(page, 2, 1);
    await expectStacks(page, '0,1,2,3|7,6|4,5');
    await page.mouse.move(0, 0);
    await page.waitForTimeout(400);
    await crop(page, STACK, 'play-panex');
  } finally {
    await app.close();
  }
});
