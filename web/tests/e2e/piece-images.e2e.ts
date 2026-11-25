// SPDX-License-Identifier: GPL-3.0-or-later
//
// Images on a sliding puzzle's pieces and boards (decision 0038): the
// image button beside the pencil in the Shape color area, the Apply image
// dialog's file, Crop and Stretch, Manual's dots that size and move the
// picture by hand, the picture drawn on the filled cells
// only, turned with the piece, kept by the file, and removed again; a
// board's on its floor in the Shapes grid, the Start grid and Play; and in
// Manual a stack of several (decision 0047): Additional image…, the tiles
// reordered and taken off, a picture smaller than the piece. A dialog
// open while another document came applies nothing to it (cleanup item 6.3).

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { rgbPng } from '../helpers/png.ts';

import {
  appOf,
  BUILT,
  ENGINE,
  engineCall,
  engines,
  launch,
  newDocument,
  SHAPE_ROWS,
  shapes,
} from './app.ts';
import { clickCell, settled } from './editor.ts';
import { toPlay } from './play.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** A w x h PNG, every pixel red. */
function png(w: number, h: number): Buffer {
  return rgbPng(w, h, () => [255, 0, 0]);
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
    // a cell drawn and erased in the box: the picture's pattern is kept, not
    // made again from its data URL at each snapshot
    const pattern = page.locator('#garea svg .pdefs image');
    await pattern.evaluate((el) => {
      (el as Element & { kept?: boolean }).kept = true;
    });
    await clickCell(page, [1, 1, 0]);
    await expect(page.locator(PATTERNED)).toHaveCount(5);
    await clickCell(page, [1, 1, 0], { button: 'right' });
    await expect(page.locator(PATTERNED)).toHaveCount(4);
    expect(await pattern.evaluate((el) => (el as Element & { kept?: boolean }).kept)).toBe(true);

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

test('a picture the document no longer names is let go: its bitmap closed (cleanup item 10.6)', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    await clickCell(page, [0, 0, 0]);
    await clickCell(page, [1, 0, 0]);
    const piece = (await shapes(page))[0]?.id ?? '';
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(4, 2),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('red.png (4 × 2)');
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    // drawn: the cache has decoded the engine's copy
    await expect(page.locator(PATTERNED)).toHaveCount(2);
    // the dialog has closed its own bitmaps; count the closes from here
    await page.evaluate(() => {
      const w = window as unknown as { closes: number };
      w.closes = 0;
      const proto = ImageBitmap.prototype;
      const close = Object.getOwnPropertyDescriptor(proto, 'close')?.value as (
        this: ImageBitmap,
      ) => void;
      proto.close = function (this: ImageBitmap) {
        w.closes++;
        close.call(this);
      };
    });
    await engineCall(page, 'shape.clearImage', { shape: piece });
    await settled(page);
    await expect(page.locator(PATTERNED)).toHaveCount(0);
    await expect
      .poll(() => page.evaluate(() => (window as unknown as { closes: number }).closes))
      .toBe(1);
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

test('Manual’s Snap to grid: off until Manual; a side dragged near a line lands on it, unless it is off (a kept setting)', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
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
    const snap = dlg.locator('[data-f="snap"]');
    await expect(snap).toBeDisabled();
    await dlg.locator('input[name="imgFit"][value="manual"]').check();
    await expect(snap).toBeEnabled();
    await expect(snap).toBeChecked();

    // the picture over the whole 3 x 2 box, ROOM (0.85) of the preview
    const box = await dlg.locator('[data-f="preview"]').boundingBox();
    const pw = box?.width ?? 0;
    const ph = box?.height ?? 0;
    const k = Math.min((0.85 * pw) / 3, (0.85 * ph) / 2);
    const at = (x: number, y: number): [number, number] => [(box?.x ?? 0) + x, (box?.y ?? 0) + y];
    const drag = async (from: [number, number], dx: number): Promise<void> => {
      await page.mouse.move(...at(...from));
      await page.mouse.down();
      await page.mouse.move(...at(from[0] + dx, from[1]), { steps: 5 });
      await page.mouse.up();
    };
    // the right side, dragged in a cell and 5 pixels more, lands on the line 2 cells in
    await drag([pw / 2 + 1.5 * k, ph / 2], -(k + 5));
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    expect((await imageOf(page, piece))?.src).toEqual([0, 0, 90, 40]);

    // off, the same side dragged to 6 pixels short of the box's edge stays there
    await page.click('#cImage');
    await expect(snap).toBeChecked();
    await snap.uncheck();
    await drag([pw / 2 + 0.5 * k, ph / 2], k - 6);
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await settled(page);
    const [, , sw = 0] = (await imageOf(page, piece))?.src ?? [];
    expect(sw).toBeGreaterThan(60);
    expect(sw).toBeLessThan(70);

    // and the dialog opens with it off again
    await page.click('#cImage');
    await expect(snap).not.toBeChecked();
  } finally {
    await a.close();
  }
});

test('the image dialog fits the window: its preview sized to the room left, its buttons in view as the window shrinks', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    await clickCell(page, [0, 0, 0]);
    await clickCell(page, [1, 0, 0]);
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(60, 30),
    });
    const card = dlg.locator('.cfm');
    const preview = dlg.locator('[data-f="preview"]');
    // Crop, Stretch and Manual (whose help is longer) the same size
    const sizes = async (): Promise<number[]> => {
      const c = await card.boundingBox();
      const p = await preview.boundingBox();
      return [c?.width ?? 0, c?.height ?? 0, p?.width ?? 0, p?.height ?? 0].map(Math.round);
    };
    const crop = await sizes();
    for (const fit of ['stretch', 'manual']) {
      await dlg.locator(`input[name="imgFit"][value="${fit}"]`).check();
      await expect.poll(sizes).toEqual(crop);
    }
    const apply = dlg.locator('[data-x="apply"]');
    const inView = async (): Promise<void> => {
      const h = await page.evaluate(() => window.innerHeight);
      await expect
        .poll(async () => {
          const c = await card.boundingBox();
          const b = await apply.boundingBox();
          return c !== null && b !== null && c.y >= 0 && c.y + c.height <= h && b.y + b.height <= h;
        })
        .toBe(true);
    };
    await inView();
    const tall = (await preview.boundingBox())?.height ?? 0;
    const resize = (height: number, width = 1500): Promise<void> =>
      a.app.evaluate(
        ({ BrowserWindow }, s) => {
          BrowserWindow.getAllWindows()[0]?.setContentSize(s.width, s.height);
        },
        { width, height },
      );
    // the dialog no narrower than Help needs beside Fit: no room left under
    // Snap to grid (only the Fit box's padding and border) as the window
    // narrows, the preview shorter instead
    const roomUnderSnap = async (): Promise<number> => {
      const f = await dlg.locator('[data-f="fitBox"]').boundingBox();
      const s = await dlg.locator('.imgdlg-snap').boundingBox();
      return (f?.y ?? 0) + (f?.height ?? 0) - (s?.y ?? 0) - (s?.height ?? 0);
    };
    const wide = async (): Promise<number> => (await card.boundingBox())?.width ?? 0;
    await resize(900, 1500);
    await expect.poll(roomUnderSnap).toBeLessThanOrEqual(14);
    const least = await wide();
    await resize(900, 1100);
    await expect.poll(roomUnderSnap).toBeLessThanOrEqual(14);
    expect(Math.abs((await wide()) - least)).toBeLessThanOrEqual(2);
    await inView();
    // a window too narrow for that: the dialog as wide as it allows, in view
    await resize(900, 950);
    await expect.poll(wide).toBeLessThan(least);
    await expect
      .poll(async () => {
        const c = await card.boundingBox();
        return (c?.x ?? -1) >= 0 && (c?.x ?? 0) + (c?.width ?? 0) <= 950;
      })
      .toBe(true);
    await inView();
    await resize(900);

    // a shorter window: a shorter preview, the whole dialog in view
    await resize(640);
    await expect.poll(async () => (await preview.boundingBox())?.height ?? 0).toBeLessThan(tall);
    await inView();
    // too short for the smallest preview: the body scrolls, the buttons stay
    await resize(420);
    await inView();
    await apply.click();
    await expect(dlg).toHaveCount(0);
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

test('the image dialog open while another document came: Apply changes nothing of it', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    await page.click('#btnNew');
    await expect(page.locator(SHAPE_ROWS)).toHaveCount(1);
    await settled(page);
    await clickCell(page, [0, 0, 0]);
    await clickCell(page, [1, 0, 0]);
    await settled(page);
    const piece = (await shapes(page))[0]?.id ?? '';
    // the rescue copy is written, so the restart offers it (answered below)
    const rev = (await engineCall<{ revision: number }>(page, 'document.get')).revision;
    await expect(page.locator('#stage')).toHaveAttribute('data-rescued', String(rev), {
      timeout: 10_000,
    });
    await page.click('#cImage');
    const dlg = page.locator('#imageDlg');
    await dlg.locator('[data-f="file"]').setInputFiles({
      name: 'red.png',
      mimeType: 'image/png',
      buffer: png(2, 1),
    });
    await expect(dlg.locator('[data-f="tiles"]')).toContainText('red.png');
    // the engine dies under the dialog: a new document, whose first shape has the same id
    const generation = await page.locator('#stage').getAttribute('data-generation');
    const main = appOf(page).process().pid ?? 0;
    const [pid] = engines(main);
    process.kill(pid ?? 0, 'SIGKILL');
    await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', generation ?? '', {
      timeout: 15_000,
    });
    await page.locator('#rescueAsk [data-x="drop"]').click();
    const made = await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 1, 1] });
    expect(made.shape).toBe(piece);
    await expect(dlg).toBeVisible();
    await dlg.locator('[data-x="apply"]').click();
    await expect(dlg).toHaveCount(0);
    await expect(page.locator('#stText')).toContainText(
      'The document changed; nothing was applied',
    );
    expect(await stackOf(page, piece)).toEqual([]);
  } finally {
    await a.close();
  }
});
