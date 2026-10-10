// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 7's exports through the menus (FRONTEND-PLAN.md, its gate): File
// ▸ Export ▸ Image of a fixture shape at the view's size and twice it,
// each the view's picture (compared with a screenshot of the view) and a
// reference image; Vector Image of the same shape in the flat style, the
// SVG drawn by the page like the view and a reference image of it; STL
// of a shape, binary and text, its triangles the flat mesh's, the dialog
// saying it is an interim; Solution Animation of a solution the Solver
// shows, greyed without one, its .glb passing Khronos's validator with
// the playback's pieces and the player's colours; an export refused for
// a path no dialog gave and for a read-only file, which is left as it
// was; Puzzlecad still greyed. The engine tests check the .glb's poses
// against the player's and the STL's surface (tests/engine/exports.test.ts).

import { chmodSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { Glb, validateGlb } from '../helpers/gltf.ts';
import {
  answerDialogs,
  BUILT,
  ENGINE,
  engineCall,
  launch,
  menu,
  menuItem,
  openFixture,
  selectShape,
  settle,
  shapes,
  style,
} from './app.ts';
import { listRows, solverSettled, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** The size of a PNG (its IHDR). */
function pngSize(data: Buffer): { width: number; height: number } {
  expect(data.subarray(1, 4).toString('latin1')).toBe('PNG');
  return { width: data.readUInt32BE(16), height: data.readUInt32BE(20) };
}

/**
 * How far two pictures are apart, drawn by the page at one size: the mean
 * difference of a channel (0 to 255) and the share of pixels off by more
 * than 48 in a channel.
 */
async function difference(
  page: Page,
  a: string,
  b: string,
  w: number,
  h: number,
): Promise<{ mean: number; off: number }> {
  return page.evaluate(
    async ({ a, b, w, h }) => {
      const load = (src: string): Promise<HTMLImageElement> =>
        new Promise((done, fail) => {
          const img = new Image();
          img.onload = () => {
            done(img);
          };
          img.onerror = () => {
            fail(new Error('the picture does not load'));
          };
          img.src = src;
        });
      const pixels = async (src: string): Promise<Uint8ClampedArray> => {
        const img = await load(src);
        const c = document.createElement('canvas');
        c.width = w;
        c.height = h;
        const g = c.getContext('2d');
        if (g === null) {
          throw new Error('no 2D context');
        }
        g.drawImage(img, 0, 0, w, h);
        return g.getImageData(0, 0, w, h).data;
      };
      const pa = await pixels(a);
      const pb = await pixels(b);
      let sum = 0;
      let off = 0;
      for (let i = 0; i < pa.length; i += 4) {
        let worst = 0;
        for (let k = 0; k < 3; k++) {
          const d = Math.abs((pa[i + k] ?? 0) - (pb[i + k] ?? 0));
          sum += d;
          worst = Math.max(worst, d);
        }
        if (worst > 48) {
          off++;
        }
      }
      return { mean: sum / ((pa.length / 4) * 3), off: off / (pa.length / 4) };
    },
    { a, b, w, h },
  );
}

const png = (data: Buffer): string => `data:image/png;base64,${data.toString('base64')}`;

/** The view's canvas alone (its toolbar, cube and tag hidden), as a PNG in CSS pixels. */
async function viewShot(page: Page): Promise<Buffer> {
  const style = await page.addStyleTag({
    content: '#vp > :not(#cv) { visibility: hidden !important; }',
  });
  try {
    await settle(page);
    return await page.locator('#cv').screenshot({ scale: 'css', animations: 'disabled' });
  } finally {
    await style.evaluate((el) => {
      el.parentNode?.removeChild(el);
    });
  }
}

/** Waits for the n-th export written in this window. */
async function exported(page: Page, n: number): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-exported', String(n));
}

test('Image and Vector Image of a fixture shape are the view’s picture', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-export-'));
  const a = await launch();
  const page = a.page;
  try {
    expect(await menuItem(page, 'export.scad')).toMatchObject({ enabled: false });
    await openFixture(page, 'CubeInCage.xmpuzzle');
    const shape = (await shapes(page)).find((s) => s.label === 'S1');
    await selectShape(page, shape?.id ?? '');
    await page.mouse.move(2, 2);
    await settle(page);
    const w = await page.locator('#cv').evaluate((c) => c.clientWidth);
    const h = await page.locator('#cv').evaluate((c) => c.clientHeight);

    // the image at the view's size and twice it
    let n = 0;
    for (const scale of [1, 2]) {
      const file = join(dir, `view-${String(scale)}x.png`);
      await answerDialogs(page, { save: file });
      await menu(page, 'export.image');
      await expect(page.locator('#exportImage')).toContainText(`${String(w)} × ${String(h)}`);
      await page.check(`#exportImage input[name="scale"][value="${String(scale)}"]`);
      await page.click('#exportImage [data-x="go"]');
      await exported(page, ++n);
      const data = readFileSync(file);
      expect(pngSize(data)).toEqual({ width: w * scale, height: h * scale });
      const d = await difference(page, png(data), png(await viewShot(page)), w, h);
      expect(d.mean, `${String(scale)}x against the view`).toBeLessThan(2);
      expect(d.off).toBeLessThan(0.005);
      if (scale === 1) {
        expect(data).toMatchSnapshot('export-image-bricks-1x.png');
      }
    }

    // the vector image in the flat style, drawn by the page as the view draws it
    await style(page, 'flat');
    for (const key of ['axes', 'bounds', 'layer']) {
      await page.click('#bDisp');
      await page.click(`#pop [data-k="${key}"]`);
      await page.keyboard.press('Escape');
    }
    await settle(page);
    const svgFile = join(dir, 'view.svg');
    await answerDialogs(page, { save: svgFile });
    await menu(page, 'export.vector');
    await exported(page, ++n);
    const svg = readFileSync(svgFile, 'utf8');
    expect(svg.startsWith('<?xml')).toBe(true);
    expect(svg).toContain(
      `viewBox="${String(-w / 2)} ${String(-h / 2)} ${String(w)} ${String(h)}"`,
    );
    expect((svg.match(/<path /g) ?? []).length).toBeGreaterThan(2);
    const svgUrl = `data:image/svg+xml;base64,${Buffer.from(svg).toString('base64')}`;
    const drawn = await page.evaluate(
      async ({ src, w, h }) => {
        const img = new Image();
        await new Promise<void>((done, fail) => {
          img.onload = () => {
            done();
          };
          img.onerror = () => {
            fail(new Error('the SVG does not load'));
          };
          img.src = src;
        });
        const c = document.createElement('canvas');
        c.width = w;
        c.height = h;
        c.getContext('2d')?.drawImage(img, 0, 0, w, h);
        return c.toDataURL('image/png');
      },
      { src: svgUrl, w, h },
    );
    const raster = Buffer.from(drawn.slice(drawn.indexOf(',') + 1), 'base64');
    const d = await difference(page, png(raster), png(await viewShot(page)), w, h);
    expect(d.mean, 'the SVG against the view').toBeLessThan(4);
    expect(d.off).toBeLessThan(0.02);
    expect(raster).toMatchSnapshot('export-vector-bricks.png');
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('STL of a shape, Solution Animation of the solution shown, and the refusals', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-export-'));
  const a = await launch();
  const page = a.page;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    const shape = (await shapes(page)).find((s) => s.label === 'S2');
    await selectShape(page, shape?.id ?? '');
    const flat = await engineCall<{ triangles: number }>(page, 'shape.mesh', {
      shape: shape?.id,
      style: 'flat',
    });

    // STL: the dialog says it is an interim; binary, then text at another unit
    let n = 0;
    const stl = join(dir, 'piece.stl');
    await answerDialogs(page, { save: stl });
    await menu(page, 'export.stl');
    await expect(page.locator('#exportStl')).toContainText('later version');
    await expect(page.locator('#exportStl [data-f="shape"]')).toHaveValue(shape?.id ?? '');
    await page.click('#exportStl [data-x="go"]');
    await exported(page, ++n);
    const bin = readFileSync(stl);
    expect(bin.length).toBe(84 + 50 * flat.triangles);
    expect(bin.readUInt32LE(80)).toBe(flat.triangles);
    await menu(page, 'export.stl');
    await page.fill('#exportStl [data-f="unit"]', '0');
    await page.click('#exportStl [data-x="go"]');
    // a unit of 0 keeps the dialog open
    await expect(page.locator('#exportStl')).toBeVisible();
    await page.fill('#exportStl [data-f="unit"]', '2.5');
    await page.uncheck('#exportStl [data-f="binary"]');
    await page.click('#exportStl [data-x="go"]');
    await exported(page, ++n);
    const text = readFileSync(stl, 'utf8');
    expect(text.startsWith('solid')).toBe(true);
    expect(text.match(/facet normal/g)).toHaveLength(flat.triangles);

    // a read-only file is refused and left as it was
    const locked = join(dir, 'locked.stl');
    writeFileSync(locked, 'old');
    chmodSync(locked, 0o444);
    await answerDialogs(page, { save: locked });
    await menu(page, 'export.stl');
    await page.click('#exportStl [data-x="go"]');
    await expect(page.locator('#stText')).toContainText('read-only');
    expect(readFileSync(locked, 'utf8')).toBe('old');
    expect(statSync(locked).mode & 0o777).toBe(0o444);
    // a path no dialog gave is not written
    const stray = join(dir, 'stray.png');
    const refused = await page.evaluate(
      (p) =>
        window.btApp.writeExport(p, new Uint8Array([1, 2, 3])).then(
          () => 'written',
          (e: unknown) => String(e),
        ),
      stray,
    );
    expect(refused).toContain('not chosen');
    expect(() => statSync(stray)).toThrow();

    // the animation: greyed without a solution shown, then of the first one
    expect(await menuItem(page, 'export.glb')).toMatchObject({ enabled: false });
    await toSolver(page);
    const rows = await listRows(page);
    expect(rows.length).toBeGreaterThan(0);
    await page.click(`#slvList .lrow[data-id="${rows[0]?.id ?? ''}"]`);
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await solverSettled(page);
    await expect.poll(async () => (await menuItem(page, 'export.glb'))?.enabled).toBe(true);
    const glbFile = join(dir, 'solution.glb');
    await answerDialogs(page, { save: glbFile });
    await menu(page, 'export.glb');
    const steps = await page.getAttribute('#splayer', 'data-steps');
    await expect(page.locator('#exportGlb')).toContainText(`${steps ?? ''} steps`);
    await page.fill('#exportGlb [data-f="cell"]', '20');
    await page.click('#exportGlb [data-x="go"]');
    await exported(page, ++n);
    const glb = new Uint8Array(readFileSync(glbFile));
    const report = await validateGlb(glb);
    expect(report.issues.messages.filter((m) => m.severity <= 1)).toEqual([]);
    const file = new Glb(glb);
    const root = file.json.nodes.find((x) => x.name === 'Puzzle');
    expect(root?.scale).toEqual([0.02, 0.02, 0.02]);
    const problem = (await page.getAttribute('#leftS', 'data-problem')) ?? '';
    const pb = await engineCall<{ handle: string; pieces: { name: string }[] }>(
      page,
      'playback.open',
      { problem, solution: rows[0]?.id },
    );
    await engineCall(page, 'playback.close', { handle: pb.handle });
    expect((root?.children ?? []).map((c) => file.json.nodes[c]?.name)).toEqual(
      pb.pieces.map((p) => p.name),
    );
    expect(file.json.animations[0]?.channels.length).toBeGreaterThan(0);
    // the pieces' colours: the player's, in glTF's linear colours
    expect(file.json.materials.length).toBe(pb.pieces.length);
    for (const m of file.json.materials) {
      const c = m.pbrMetallicRoughness?.baseColorFactor ?? [];
      expect(c).toHaveLength(4);
      expect(c[3]).toBe(1);
    }
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
