// SPDX-License-Identifier: GPL-3.0-or-later
//
// The border of a sliding puzzle's tray (decision 0046): the board's Tray
// border section above Transform (folded for a board), its switch, its
// corners, color, image, material and thickness, and the border drawn in every view
// of the board: the Shapes tab's 3D view, the Puzzle tab's two trays, the
// Play tab's tray and round the Solver's pieces; kept by the file.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { rgbPng } from '../helpers/png.ts';

import { BUILT, ENGINE, engineCall, launch, openFixture, selectShape, shapes } from './app.ts';
import { settled } from './editor.ts';
import { toPlay } from './play.ts';
import { pzSettled } from './puzzle.ts';
import { runButton, runEnded, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** A w x h PNG: red on the left half, blue on the right. */
function png(w: number, h: number): Buffer {
  return rgbPng(w, h, (x) => (x < w / 2 ? [255, 0, 0] : [0, 0, 255]));
}

interface Border {
  enabled: boolean;
  corners: string;
  material: string;
  color: string | null;
  thickness: number;
  images: { box: number[]; src: number[] }[];
}

async function borderOf(page: Page, shape: string): Promise<Border | null> {
  const snap = await engineCall<{ shapes: { id: string; border: Border | null }[] }>(
    page,
    'document.get',
  );
  return snap.shapes.find((s) => s.id === shape)?.border ?? null;
}

test('a board’s tray border: its section, its look, and the border in every view of the board', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  try {
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const all = await shapes(page);
    const tray = all.find((s) => s.label === 'B1')?.id ?? '';
    const piece = all.find((s) => s.label === 'S1')?.id ?? '';
    const vp = page.locator('#vp');
    const sec = page.locator('#secBorder');

    // a piece: no section, Transform open; the board: the section, Transform folded
    await selectShape(page, piece);
    await expect(sec).toBeHidden();
    await expect(page.locator('#secTf')).not.toHaveClass(/\bclosed\b/);
    await selectShape(page, tray);
    await expect(sec).toBeVisible();
    await expect(page.locator('#secTf')).toHaveClass(/\bclosed\b/);
    await expect(page.locator('#borderSum')).toHaveText('Off');
    await expect(sec.locator('[data-bcorners]')).toHaveCount(0);
    await expect(vp).toHaveAttribute('data-border', '');
    expect(await borderOf(page, piece)).toBeNull();

    // turned on: round corners, smooth, the board's color, in the 3D view
    await sec.locator('[data-bon]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', /^1:round:smooth:#[0-9a-f]{6}:-$/);
    await expect(page.locator('#borderSum')).toHaveText('Rounded · Smooth');

    // sharp corners, wood (its light brown), then a color of its own and Default again
    await sec.locator('[data-bcorners="sharp"]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', /^1:sharp:smooth:/);
    await sec.locator('select[data-bmaterial]').selectOption('wood');
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#c9a26b:-');
    await sec.locator('[data-bcolor]').click();
    await page.locator('#pop .cpk-hex').fill('#336699');
    await page.locator('#pop .cpk-hex').press('Enter');
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#336699:-');
    await sec.locator('[data-bdefault]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#c9a26b:-');
    await sec.locator('select[data-bmaterial]').selectOption('textured');
    await settled(page);
    await sec.locator('select[data-bmaterial]').selectOption('wood');
    await settled(page);

    // thick: one voxel at first, then three; thin again
    await expect(sec.locator('[data-bthick="thin"]')).toHaveClass(/\bon\b/);
    await expect(sec.locator('select[data-bvoxels]')).toHaveCount(0);
    await sec.locator('[data-bthick="thick"]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#c9a26b:-:rim1');
    await expect(sec.locator('select[data-bvoxels]')).toHaveValue('1');
    await sec.locator('select[data-bvoxels]').selectOption('3');
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#c9a26b:-:rim3');
    await expect(page.locator('#borderSum')).toHaveText('Sharp · Wood grain · 3 voxels');
    expect((await borderOf(page, tray))?.thickness).toBe(3);
    await sec.locator('[data-bthick="thin"]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '1:sharp:wood:#c9a26b:-');
    await expect(sec.locator('select[data-bvoxels]')).toHaveCount(0);
    expect((await borderOf(page, tray))?.thickness).toBe(0);

    // an image: over the whole tray, the board's own picture left alone
    await sec.locator('[data-bimage]').click();
    const dlg = page.locator('#imageDlg');
    await expect(dlg).toBeVisible();
    await expect(dlg.locator('h3')).toContainText('tray border');
    await expect(dlg.locator('.imgdlg-note')).toContainText('whole tray');
    // the preview as large as the window leaves room for
    const size = await page.evaluate(() => ({ width: innerWidth, height: innerHeight }));
    if (size.width >= 1200 && size.height >= 800) {
      const box = await dlg.locator('[data-f="preview"]').boundingBox();
      expect(box?.width ?? 0).toBeGreaterThan(500);
      expect(box?.height ?? 0).toBeGreaterThan(300);
    }
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'halves.png',
      mimeType: 'image/png',
      buffer: png(60, 40),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('halves.png');
    await dlg.locator('input[name="imgFit"][value="stretch"]').check();
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    const b = await borderOf(page, tray);
    expect(b).toMatchObject({ enabled: true, corners: 'sharp', material: 'wood', color: null });
    expect(b?.images[0]?.src).toEqual([0, 0, 60, 40]);
    await expect(vp).toHaveAttribute('data-border', /^1:sharp:wood:#c9a26b:[^-]/);
    await expect(sec.locator('[data-bimage]')).toHaveClass(/\bon\b/);

    // the Puzzle tab: round START and round GOAL
    await page.keyboard.press('Control+2');
    await pzSettled(page);
    await expect(vp).toHaveAttribute('data-tray-border', /^2:sharp:wood:/);

    // Play: the tray's body in the border's look
    await toPlay(page);
    await expect(page.locator('.pl-tray')).toHaveAttribute(
      'data-border',
      /^sharp:wood:#c9a26b:image$/,
    );
    await expect(page.locator('.pl-borderpic image')).toHaveCount(1);
    await expect(page.locator('.pl-wood')).toHaveCount(1);

    // the Solver: round the pieces
    await toSolver(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await expect(vp).toHaveAttribute('data-solution-border', /^1:sharp:wood:/);

    // the file keeps it
    const saved = await engineCall<{ data: string }>(page, 'document.saveBytes');
    const xml = Buffer.from(saved.data, 'base64').toString('utf8');
    expect(xml).toContain('border="1" borderCorners="sharp" borderMaterial="wood" borderImage="');

    // two voxels thick: a wider tray on Play, its thickness in the file
    await page.keyboard.press('Control+1');
    await selectShape(page, tray);
    await sec.locator('[data-bthick="thick"]').click();
    await settled(page);
    await sec.locator('select[data-bvoxels]').selectOption('2');
    await settled(page);
    await toPlay(page);
    await expect(page.locator('.pl-tray')).toHaveAttribute(
      'data-border',
      /^sharp:wood:#c9a26b:image:2$/,
    );
    const thick = Buffer.from(
      (await engineCall<{ data: string }>(page, 'document.saveBytes')).data,
      'base64',
    ).toString('utf8');
    expect(thick).toContain('borderThickness="2"');

    // turned off: no border anywhere, its look kept
    await page.keyboard.press('Control+1');
    await selectShape(page, tray);
    await sec.locator('[data-bon]').click();
    await settled(page);
    await expect(vp).toHaveAttribute('data-border', '');
    await expect(page.locator('#borderSum')).toHaveText('Off');
    expect(await borderOf(page, tray)).toMatchObject({ enabled: false, material: 'wood' });
    await toPlay(page);
    await expect(page.locator('.pl-tray')).toHaveAttribute('data-border', '');
    await expect(page.locator('.pl-grain')).toHaveCount(1);
  } finally {
    await a.close();
  }
});
