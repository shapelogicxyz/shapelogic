// SPDX-License-Identifier: GPL-3.0-or-later
//
// Images on a sliding puzzle's pieces (decision 0038): the image button
// beside the pencil in the Shape color area, the Apply image dialog's file,
// Crop and Stretch, the picture drawn on the filled cells only, turned with
// the piece, kept by the file, and removed again.

import { crc32, deflateSync } from 'node:zlib';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, SHAPE_ROWS, shapes } from './app.ts';
import { clickCell, settled } from './editor.ts';

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

async function imageOf(page: Page, shape: string): Promise<Image | null> {
  const snap = await engineCall<{ shapes: { id: string; image: Image | null }[] }>(
    page,
    'document.get',
  );
  return snap.shapes.find((s) => s.id === shape)?.image ?? null;
}

const PATTERNED = '#garea .cell[fill="url(#pimg)"]';

test('a piece’s image: applied cropped or stretched, drawn on its cells, turned, saved, removed', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'sliding');
    // the image button is for pieces: none before there is one
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
    await expect(dlg.locator('[data-f="name"]')).toContainText('red.png (6 × 2)');
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
    await expect(dlg.locator('[data-f="name"]')).toHaveText('The piece’s image');
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
