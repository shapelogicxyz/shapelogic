// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 7's exports through the menus (FRONTEND-PLAN.md, its gate): File
// ▸ Export ▸ Image of a fixture shape at the view's size and twice it,
// each the view's picture (compared with a screenshot of the view) and a
// reference image; Vector Image of the same shape in the flat style, the
// SVG drawn by the page like the view and a reference image of it; STL
// of a shape (deferred part 5: the grid's parameters with the original's
// labels, the preview and its volume, a refusal in the original's words,
// the file the original writes, byte for byte, binary and text);
// Solutions ▸ Export to STL (the pieces of CubeInCage's first solution
// into a folder: the original's names and bytes, the question before
// files are replaced); Solution Animation of a solution the Solver
// shows, greyed without one, its .glb passing Khronos's validator with
// the playback's pieces and the player's colours; an export refused for
// a path no dialog gave and for a read-only file, which is left as it
// was; Puzzlecad greyed without a shape (its export: imports.e2e.ts). The engine tests check the .glb's poses
// against the player's and the STL's surface (tests/engine/exports.test.ts).

import {
  chmodSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

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

/** What the original's STL exports wrote for CubeInCage (`oracle.py stl-solution`). */
const RECORDED = resolve(
  import.meta.dirname,
  '../../../tests/data/stl_solution_by_original/CubeInCage',
);

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

/**
 * Two binary STL files alike: the header, the count, the corners and the
 * attributes byte for byte, the facet normals within 1e-5 (an x86-64 build
 * without fused multiply-add rounds them a little differently from the
 * arm64 build the original's recording comes from).
 */
function sameStl(a: Buffer, b: Buffer): boolean {
  if (a.length !== b.length || !a.subarray(0, 84).equals(b.subarray(0, 84))) {
    return false;
  }
  for (let t = 0; t < a.readUInt32LE(80); t++) {
    const at = 84 + 50 * t;
    if (!a.subarray(at + 12, at + 50).equals(b.subarray(at + 12, at + 50))) {
      return false;
    }
    for (let k = 0; k < 3; k++) {
      if (Math.abs(a.readFloatLE(at + 4 * k) - b.readFloatLE(at + 4 * k)) > 1e-5) {
        return false;
      }
    }
  }
  return true;
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

    // STL (stlexport.cpp): the parameters with the original's labels and values
    let n = 0;
    const stl = join(dir, 'Frame1.stl');
    await menu(page, 'export.stl');
    const dlg = page.locator('#exportStl');
    await expect(dlg).toBeVisible();
    await expect(dlg.locator('[data-f="shape"]')).toHaveValue(shape?.id ?? '');
    await expect(dlg.locator('label[title]')).toHaveText([
      'Unit Size',
      'Unit Size Y',
      'Unit Size Z',
      'Bevel',
      'Offset',
      'Interior chamfers',
      'Binary STL',
    ]);
    await expect(dlg.locator('[data-p="0"]')).toHaveValue('10.00');
    await expect(dlg.locator('[data-p="3"]')).toHaveValue('0.30');
    await expect(dlg.locator('[data-p="4"]')).toHaveValue('0.01');
    // the preview and its volume; plain cells of unit size 1 are the cells' volume
    await expect(dlg).toHaveAttribute('data-busy', '0');
    await expect(dlg.locator('[data-f="status"]')).toContainText('Volume: ');
    const { fixed: cells } = await engineCall<{ fixed: number }>(page, 'shape.info', {
      shape: shape?.id,
    });
    await dlg.locator('[data-p="0"]').fill('1');
    await dlg.locator('[data-p="3"]').fill('0');
    await dlg.locator('[data-p="4"]').fill('0');
    await dlg.locator('[data-p="4"]').press('Tab');
    await expect(dlg).toHaveAttribute('data-volume', cells.toFixed(1));
    // a value out of range: the original's words, and nothing shown
    await dlg.locator('[data-p="0"]').fill('0');
    await dlg.locator('[data-p="0"]').press('Tab');
    await expect(dlg.locator('[data-f="status"]')).toHaveText('Unit size too small');
    await expect(dlg).not.toHaveAttribute('data-volume');
    // the defaults again, and the file the original writes for this shape
    await dlg.locator('[data-p="0"]').fill('10.00');
    await dlg.locator('[data-p="3"]').fill('0.30');
    await dlg.locator('[data-p="4"]').fill('0.01');
    await dlg.locator('[data-p="4"]').press('Tab');
    await expect(dlg.locator('[data-f="status"]')).toContainText('Volume: ');
    await answerDialogs(page, { save: stl });
    await dlg.locator('[data-x="go"]').click();
    await exported(page, ++n);
    expect(sameStl(readFileSync(stl), readFileSync(join(RECORDED, 'Frame1.stl')))).toBe(true);
    // the dialog stays open, as the original's; text this time
    await dlg.locator('[data-f="binary"]').uncheck();
    await dlg.locator('[data-x="go"]').click();
    await exported(page, ++n);
    const text = readFileSync(stl, 'utf8');
    expect(text.startsWith('solid Frame1.stl\n  facet normal ')).toBe(true);
    expect(text.endsWith('endsolid\n')).toBe(true);
    await dlg.locator('[data-f="binary"]').check();

    // a read-only file is refused and left as it was
    const locked = join(dir, 'locked.stl');
    writeFileSync(locked, 'old');
    chmodSync(locked, 0o444);
    await answerDialogs(page, { save: locked });
    await dlg.locator('[data-x="go"]').click();
    await expect(page.locator('#stText')).toContainText('read-only');
    expect(readFileSync(locked, 'utf8')).toBe('old');
    expect(statSync(locked).mode & 0o777).toBe(0o444);
    await dlg.locator('[data-x="close"]').click();
    await expect(dlg).toBeHidden();
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

    // Solutions ▸ Export to STL (stlexportsolution.cpp): CubeInCage's first solution
    const folder = join(dir, 'pieces');
    mkdirSync(folder);
    await page.click('#slvAct [data-a="stl"]');
    const sd = page.locator('#exportStlSolution');
    await expect(sd).toBeVisible();
    await expect(sd.locator('[data-f="pieces"]')).toHaveText(
      [
        'Pieces placed in solution 1:',
        '  S2  Frame1  ×1',
        '  S3  Frame2  ×2',
        '  S4  ×1',
        '  S5  ×1',
        '  S6  ×1',
        'Total instances: 6',
      ].join('\n'),
    );
    // no folder yet: nothing to export into
    await expect(sd.locator('[data-x="go"]')).toBeDisabled();
    await answerDialogs(page, { open: folder });
    await sd.locator('[data-a="folder"]').click();
    await expect(sd.locator('[data-f="folder"]')).toHaveText(folder);
    await sd.locator('[data-x="go"]').click();
    await expect(sd).toHaveAttribute('data-exported', '1');
    await expect(sd.locator('[data-f="status"]')).toHaveText('Done — exported 5 piece type(s).');
    // the original's names and bytes
    const names = readFileSync(join(RECORDED, 'pieces.txt'), 'utf8')
      .trim()
      .split('\n')
      .map((l) => l.split(' ')[2] ?? '');
    expect(readdirSync(folder).sort()).toEqual([...names].sort());
    for (const name of names) {
      expect(
        sameStl(readFileSync(join(folder, name)), readFileSync(join(RECORDED, name))),
        name,
      ).toBe(true);
    }
    // again: the question first, Cancel writes nothing, Overwrite all
    await sd.locator('[data-x="go"]').click();
    await expect(page.locator('#stlOverwrite')).toContainText(
      '5 output file(s) already exist. Overwrite?',
    );
    await page.click('#stlOverwrite [data-x="cancel"]');
    await expect(sd).toHaveAttribute('data-exported', '1');
    await sd.locator('[data-x="go"]').click();
    await page.click('#stlOverwrite [data-x="overwrite"]');
    await expect(sd).toHaveAttribute('data-exported', '2');
    // a name the engine did not give is not written there, nor a folder no dialog gave
    const sneaky = await page.evaluate(async (f) => {
      const bytes = new Uint8Array([1]);
      const tries = [
        window.btApp.writeFolderExport(f, '../escape.stl', bytes),
        window.btApp.writeFolderExport(f, 'x.xmpuzzle', bytes),
        window.btApp.writeFolderExport(`${f}-other`, 'x.stl', bytes),
      ];
      return Promise.all(
        tries.map((t) =>
          t.then(
            () => 'written',
            (e: unknown) => String(e),
          ),
        ),
      );
    }, folder);
    expect(sneaky.every((r) => r !== 'written')).toBe(true);
    expect(readdirSync(dir).sort()).not.toContain('escape.stl');
    await sd.locator('[data-x="close"]').click();
    await expect(sd).toBeHidden();
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
