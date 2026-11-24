// SPDX-License-Identifier: GPL-3.0-or-later
//
// Images on a sliding puzzle's pieces and boards (decision 0038): the
// image button beside the pencil in the Shape color area, the Apply image
// dialog's file, Crop and Stretch, Manual's dots that size and move the
// picture by hand, the picture drawn on the filled cells
// only, turned with the piece, kept by the file, and removed again; a
// board's on its floor in the Shapes grid, the Start grid and Play; and in
// Manual a stack of several (decision 0047): Additional image…, the tiles
// reordered and taken off, a picture smaller than the piece.

import { crc32, deflateSync } from 'node:zlib';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, SHAPE_ROWS, shapes } from './app.ts';
import { clickCell, settled } from './editor.ts';
import { toPlay } from './play.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** A w x h PNG, every pixel red. */
function png(w: number, h: number): Buffer {
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
  const rows = Buffer.concat(
    Array.from({ length: h }, () => Buffer.concat([Buffer.from([0]), Buffer.alloc(w * 3, 0)])),
  );
  for (let i = 0; i < rows.length; i++) {
    // the red byte of each pixel (after each row's filter byte)
    if ((i % (w * 3 + 1)) % 3 === 1) {
      rows[i] = 255;
    }
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(rows)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

interface Image {
  image: number;
  fit: string;
  src: number[];
  box: number[];
  turn: number;
}

async function stackOf(page: Page, shape: string): Promise<Image[]> {
  const snap = await engineCall<{ shapes: { id: string; images: Image[] }[] }>(
    page,
    'document.get',
  );
  return snap.shapes.find((s) => s.id === shape)?.images ?? [];
}

/** The bottom picture of the shape's stack, or null. */
async function imageOf(page: Page, shape: string): Promise<Image | null> {
  return (await stackOf(page, shape))[0] ?? null;
}

const PATTERNED = '#garea .cell[fill^="url(#pimg"]';

test('a piece’s image: applied cropped or stretched, drawn on its cells, turned, saved, removed', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    // no shape, no image button
    await expect(page.locator('#cImage')).toHaveCount(0);
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    // an L of four cells: its box is 3 by 2
    for (const [x, y] of [
      [0, 0],
      [1, 0],
      [2, 0],
      [0, 1],
    ] as const) {
      await clickCell(page, [x, y, 0]);
    }
    const piece = (await shapes(page))[0]?.id ?? '';

    // Crop (the default): a 6 x 2 picture cut to the box's 3 : 2 in the middle
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await expect(dlg).toBeVisible();
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(6, 2),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('red.png (6 × 2)');
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect(await imageOf(page, piece)).toMatchObject({
      fit: 'crop',
      src: [1, 0, 3, 2],
      box: [0, 0, 3, 2],
      turn: 0,
    });
    // drawn on the four filled cells only
    await expect(page.locator(PATTERNED)).toHaveCount(4);
    await expect(page.locator('#cImage')).toHaveClass(/\bon\b/);

    // Stretch, the same picture: the dialog opens with the piece's image
    await page.click('#cImage');
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('The piece’s image');
    await dlg.locator('input[name="imgFit"][value="stretch"]').check();
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect(await imageOf(page, piece)).toMatchObject({ fit: 'stretch', src: [0, 0, 6, 2] });

    // a quarter turn turns the image with the cells
    await engineCall(page, 'shape.task', { shape: piece, task: 'rotate10' });
    await settled(page);
    const turned = await imageOf(page, piece);
    expect(turned?.turn).not.toBe(0);
    expect(turned?.box.slice(2)).toEqual([2, 3]);
    await expect(page.locator(PATTERNED)).toHaveCount(4);

    // the file has the picture and the piece's attributes
    const saved = await engineCall<{ data: string }>(page, 'document.saveBytes');
    const xml = Buffer.from(saved.data, 'base64').toString('utf8');
    expect(xml).toContain('<image id="0" type="image/png" width="6" height="2">');
    expect(xml).toContain('imageFit="stretch" imageSrc="0 0 6 2"');

    // removed in the dialog
    await page.click('#cImage');
    await dlg.locator('[data-x="remove"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect(await imageOf(page, piece)).toBeNull();
    await expect(page.locator(PATTERNED)).toHaveCount(0);
    await expect(page.locator('#cImage')).not.toHaveClass(/\bon\b/);
  } finally {
    await a.close();
  }
});

test('a piece’s image placed by hand: Manual’s corner and middle dots, kept by the file', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    // an L of four cells: its box is 3 by 2, the picture's proportions
    for (const [x, y] of [
      [0, 0],
      [1, 0],
      [2, 0],
      [0, 1],
    ] as const) {
      await clickCell(page, [x, y, 0]);
    }
    const piece = (await shapes(page))[0]?.id ?? '';
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(60, 40),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('red.png (60 × 40)');
    const note = dlg.locator('[data-f="manualNote"]');
    await expect(note).toBeHidden();
    await dlg.locator('input[name="imgFit"][value="manual"]').check();
    await expect(note).toBeVisible();

    // Manual starts from Crop's whole picture over the 3 x 2 box, ROOM (0.85)
    // of the preview (sized to the window) in its middle; the bottom right
    // corner, dragged out, makes it larger
    const box = await dlg.locator('[data-f="preview"]').boundingBox();
    expect(box).not.toBeNull();
    const pw = box?.width ?? 0;
    const ph = box?.height ?? 0;
    const k = Math.min((0.85 * pw) / 3, (0.85 * ph) / 2);
    const at = (x: number, y: number): [number, number] => [(box?.x ?? 0) + x, (box?.y ?? 0) + y];
    const drag = async (from: [number, number], by: [number, number]): Promise<void> => {
      await page.mouse.move(...at(...from));
      await page.mouse.down();
      await page.mouse.move(...at(from[0] + by[0], from[1] + by[1]), { steps: 5 });
      await page.mouse.up();
    };
    await drag([pw / 2 + 1.5 * k, ph / 2 + k], [30, 20]);
    // then the middle (the preview's, once the view is fitted again) moves it
    // left and up, which shows more of the picture's right and bottom
    await drag([pw / 2, ph / 2], [-20, -15]);
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    const placed = await imageOf(page, piece);
    expect(placed?.fit).toBe('manual');
    const [sx = 0, sy = 0, sw = 0, sh = 0] = placed?.src ?? [];
    // a part of the picture, at its proportions, away from its top left
    expect(sw).toBeLessThan(60);
    expect(sh).toBeLessThan(40);
    expect(Math.abs(sw / sh - 1.5)).toBeLessThan(0.1);
    expect(sx).toBeGreaterThan(0);
    expect(sy).toBeGreaterThan(0);
    expect(sx + sw).toBeLessThanOrEqual(60);
    expect(sy + sh).toBeLessThanOrEqual(40);

    // the dialog opens on Manual with the same part, and keeps it
    await page.click('#cImage');
    await expect(dlg.locator('input[name="imgFit"][value="manual"]')).toBeChecked();
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect((await imageOf(page, piece))?.src).toEqual(placed?.src);

    const saved = await engineCall<{ data: string }>(page, 'document.saveBytes');
    const xml = Buffer.from(saved.data, 'base64').toString('utf8');
    expect(xml).toContain(`imageFit="manual" imageSrc="${(placed?.src ?? []).join(' ')}"`);
  } finally {
    await a.close();
  }
});

test('a board’s image: on its floor in the Shapes grid, the Start grid and the Play tab', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNewBoard');
    await settled(page);
    const snap = await engineCall<{ shapes: { id: string; startGoal: number | null }[] }>(
      page,
      'document.get',
    );
    const tray = snap.shapes.find((s) => s.startGoal !== null)?.id ?? '';
    // a new board is 6 by 6, every cell floor
    await expect(page.locator('#cImage')).toHaveAttribute('title', /board’s cells/);
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await expect(dlg).toBeVisible();
    await expect(dlg.locator('.imgdlg-note')).toContainText('under the pieces');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(6, 6),
    });
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect(await imageOf(page, tray)).toMatchObject({ src: [0, 0, 6, 6], box: [0, 0, 6, 6] });
    await expect(page.locator(PATTERNED)).toHaveCount(36);

    // a unit piece on the board, its start at the corner, its goal at the far one
    const { shape: one } = await engineCall<{ shape: string }>(page, 'shape.create', {
      size: [1, 1, 1],
    });
    await engineCall(page, 'shape.setCells', { shape: one, tool: 'fixed', cells: [[0, 0, 0]] });
    const { problem } = await engineCall<{ problem: string }>(page, 'problem.create');
    await engineCall(page, 'problem.setResult', { problem, shape: tray });
    await engineCall(page, 'sliding.place', { tray, problem, piece: one, at: [0, 0], goal: false });
    const last = await engineCall<{ revision: number }>(page, 'sliding.place', {
      tray,
      problem,
      piece: one,
      at: [5, 5],
      goal: true,
    });
    await expect(page.locator('#stage')).toHaveAttribute('data-revision', String(last.revision));

    // the Start grid: the picture on every cell but the one the piece's start marks
    await page.keyboard.press('Control+2');
    await expect(page.locator('#sgStartGrid .cell[fill^="url(#pimg"]')).toHaveCount(35);

    // Play: the picture on the floor, under the piece
    await toPlay(page);
    await expect(page.locator('.pl-floor .pl-picture image')).toHaveCount(1);
    await expect(page.locator('.pl-piece')).toHaveCount(1);
  } finally {
    await a.close();
  }
});

test('several images in Manual: Additional image…, the tiles reordered and taken off, one smaller than the piece', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    // a 3 by 2 L
    for (const [x, y] of [
      [0, 0],
      [1, 0],
      [2, 0],
      [0, 1],
    ] as const) {
      await clickCell(page, [x, y, 0]);
    }
    const piece = (await shapes(page))[0]?.id ?? '';
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    const choose = dlg.locator('[data-f="choose"]');
    const tiles = dlg.locator('.imgtile');
    await expect(choose).toHaveText('Choose image…');
    await expect(dlg.locator('[data-f="tiles"]')).toHaveText('No image chosen');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(60, 40),
    });
    await expect(tiles).toHaveCount(1);
    // Crop: still one image, Choose image… replaces it
    await expect(choose).toHaveText('Choose image…');
    await dlg.locator('input[name="imgFit"][value="manual"]').check();
    await expect(choose).toHaveText('Additional image…');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'blue.png',
      mimeType: 'image/png',
      buffer: png(30, 20),
    });
    await expect(tiles).toHaveCount(2);
    await expect(tiles.nth(1)).toContainText('blue.png (30 × 20)');
    await expect(tiles.nth(1)).toHaveClass(/\bon\b/);
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'green.png',
      mimeType: 'image/png',
      buffer: png(20, 20),
    });
    await expect(tiles).toHaveCount(3);
    // green taken off with its X
    await tiles.nth(2).locator('[data-rm]').click();
    await expect(tiles).toHaveCount(2);

    // blue dragged before red: blue at the bottom, red on top
    const red = tiles.nth(0);
    await tiles.nth(1).dragTo(red, { targetPosition: { x: 4, y: 6 } });
    await expect(tiles.nth(0)).toContainText('blue.png');
    await expect(tiles.nth(1)).toContainText('red.png');

    // Crop shows one image and says so; Manual brings both back
    await dlg.locator('input[name="imgFit"][value="crop"]').check();
    await expect(tiles).toHaveCount(1);
    await expect(dlg.locator('[data-f="singleNote"]')).toBeVisible();
    await expect(choose).toHaveText('Choose image…');
    await dlg.locator('input[name="imgFit"][value="manual"]').check();
    await expect(tiles).toHaveCount(2);
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);

    // the stack: blue, half the box's size in its middle (its part shown
    // reaching past its edges), under red, over the whole box
    const stack = await stackOf(page, piece);
    expect(stack).toHaveLength(2);
    expect(stack.map((s) => s.fit)).toEqual(['manual', 'manual']);
    expect(stack[0]?.box).toEqual(stack[1]?.box);
    const [bx = 0, by = 0, bw = 0, bh = 0] = stack[0]?.src ?? [];
    expect(bw).toBeGreaterThan(30);
    expect(bh).toBeGreaterThan(20);
    expect(bx).toBeLessThan(0);
    expect(by).toBeLessThan(0);
    expect(stack[1]?.src).toEqual([0, 0, 60, 40]);
    // drawn as one picture on the cells, in the grid and on Play
    await expect(page.locator(PATTERNED)).toHaveCount(4);
    const saved = await engineCall<{ data: string }>(page, 'document.saveBytes');
    const xml = Buffer.from(saved.data, 'base64').toString('utf8');
    expect(xml).toContain('image2="');

    // the dialog opens with both; red taken off leaves blue alone
    await page.click('#cImage');
    await expect(tiles).toHaveCount(2);
    await expect(dlg.locator('input[name="imgFit"][value="manual"]')).toBeChecked();
    await tiles.nth(1).locator('[data-rm]').click();
    await expect(tiles).toHaveCount(1);
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect(await stackOf(page, piece)).toHaveLength(1);
    expect((await imageOf(page, piece))?.src).toEqual([bx, by, bw, bh]);
  } finally {
    await a.close();
  }
});
