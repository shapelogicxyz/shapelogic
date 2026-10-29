// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screenshots of the user guide's reference pages (the 3D Model Viewer,
// the Shapes, Puzzle and Solver tab options, color restrictions, piece
// rotations, imports and exports, keyboard shortcuts, solve notation):
// whole-window shots and crops of single cards, toolbars and menus, so a
// page can show the button it talks about. Written to
// site/src/assets/screens/guide. Not a test of the app: run it with the
// other guide screenshots (`just docs-screenshots`) when the UI changes.

import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  BUILT,
  display,
  ENGINE,
  launch,
  menu,
  openFixture,
  ROOT,
  settle,
  style,
} from '../e2e/app.ts';
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
  const app = await launch();
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
  const app = await launch();
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
    for (const s of ['drawing', 'flat', 'wireframe', 'stl'] as const) {
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
  const app = await launch();
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
  const app = await launch();
  const { page } = app;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    await menu(page, 'export.stl');
    await expect(page.locator('#exportStl .cfm')).toBeVisible();
    // a piece: the result has variable voxels, which the STL export refuses
    await page.selectOption('#exportStl [data-f="shape"]', { index: 1 });
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
