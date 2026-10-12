// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deferred part 6 through the menus (docs/webui-spec.md section 11): File ▸
// Import ▸ PuzzleSolver3D and Puzzlecad (the puzzle the original's import
// saved, a document without a file that Save asks a place for, the
// Entities workspace shown; a file that cannot be read told in the
// original's words, the open puzzle kept), File ▸ Export ▸ Puzzlecad (the
// text the original wrote, ".scad" added to a name without it; a puzzle
// not on the cube grid refused before a file is asked for), and Edit ▸
// Convert Brick Grid Type to Other (the dialog's choices, the converted
// puzzle the original's, saved over its file; a grid with nothing to
// convert to). The recordings are tests/data/imports_by_original/
// (`oracle.py record-imports`).

import { copyFileSync, existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { gunzipSync } from 'node:zlib';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  answerDialogs,
  BUILT,
  ENGINE,
  engineCall,
  FIXTURES,
  launch,
  menu,
  menuItem,
  openFixture,
} from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const DATA = resolve(import.meta.dirname, '../../../tests/data');
const INPUTS = join(DATA, 'imports');
const RECORDED = join(DATA, 'imports_by_original');

interface Doc {
  path: string | null;
  dirty: boolean;
  canUndo: boolean;
  grid: string;
  revision: number;
}

/** A recorded save's XML (the original wrote it gzip-compressed). */
function recordedXml(name: string): string {
  return gunzipSync(readFileSync(join(RECORDED, name))).toString('latin1');
}

/** The open document's XML, as the engine would save it. */
async function savedXml(page: Page): Promise<string> {
  const r = await engineCall<{ data: string }>(page, 'document.saveBytes');
  return Buffer.from(r.data, 'base64').toString('latin1');
}

/** Waits for another document than the one shown before. */
async function replaced(page: Page, before: string | null): Promise<void> {
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
}

async function generation(page: Page): Promise<string | null> {
  return page.locator('#stage').getAttribute('data-generation');
}

async function entitiesShown(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'entities');
}

test('File ▸ Import: PuzzleSolver3D and Puzzlecad files as the original imports them', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-import-'));
  const a = await launch();
  const page = a.page;
  try {
    // offered with the empty document the app starts with, as Open
    expect(await menuItem(page, 'import.ps3d')).toMatchObject({ enabled: true });
    expect(await menuItem(page, 'import.scad')).toMatchObject({ enabled: true });

    for (const [item, file, recording] of [
      ['import.ps3d', 'pieces.puz', 'pieces_puz.xmpuzzle'],
      ['import.scad', 'plate.scad', 'plate_scad.xmpuzzle'],
    ] as const) {
      // from another workspace: the import shows the Entities one
      await page.click('button[data-tab="puzzle"]');
      await answerDialogs(page, { open: join(INPUTS, file) });
      const before = await generation(page);
      const dirty = (await page.locator('#stage').getAttribute('data-dirty')) === '1';
      await menu(page, item);
      if (dirty) {
        await page.click('#discard [data-x="drop"]');
      }
      await replaced(page, before);
      const stem = file.replace(/\.[a-z]+$/, '');
      await expect(page.locator('#fname')).toContainText(stem);
      await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '1');
      await entitiesShown(page);
      await expect(page.locator('#shapeList .row').first()).toHaveClass(/sel/);
      const doc = await engineCall<Doc>(page, 'document.get');
      expect(doc).toMatchObject({ path: null, dirty: true, canUndo: false, grid: 'bricks' });
      expect(await savedXml(page)).toBe(recordedXml(recording));

      // Save asks where, proposing the source's name as a puzzle file
      const out = join(dir, `${stem}.xmpuzzle`);
      await answerDialogs(page, { save: out });
      await menu(page, 'save');
      await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');
      expect(gunzipSync(readFileSync(out)).toString('latin1')).toBe(recordedXml(recording));
      // the source is untouched
      expect(existsSync(join(INPUTS, file))).toBe(true);
    }

    // a file the reader cannot take: the original's words, the puzzle kept
    const before = await generation(page);
    await answerDialogs(page, { open: join(INPUTS, 'nopieces.scad') });
    await menu(page, 'import.scad');
    await expect(page.locator('#importFailed')).toContainText('Could not load puzzle, sorry!');
    await page.click('#importFailed [data-x="ok"]');
    expect(await generation(page)).toBe(before);
    await expect(page.locator('#fname')).toContainText('plate');
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('File ▸ Export ▸ Puzzlecad writes the text the original wrote', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-scad-'));
  const a = await launch();
  const page = a.page;
  try {
    // the empty document has no shape with cells
    expect(await menuItem(page, 'export.scad')).toMatchObject({ enabled: false });
    await openFixture(page, 'PelikanBurr.xmpuzzle');
    // a name without the extension gets it
    await answerDialogs(page, { save: join(dir, 'pelikan') });
    await menu(page, 'export.scad');
    await expect.poll(() => existsSync(join(dir, 'pelikan.scad'))).toBe(true);
    expect(readFileSync(join(dir, 'pelikan.scad'), 'utf8')).toBe(
      readFileSync(join(RECORDED, 'PelikanBurr.p0.scad'), 'utf8'),
    );

    // not the cube grid: refused in the original's words before a file is asked for
    await openFixture(page, 'Bermuda.xmpuzzle');
    await answerDialogs(page, { save: join(dir, 'bermuda.scad') });
    await menu(page, 'export.scad');
    await expect(page.locator('#exportScadFailed')).toContainText(
      'Puzzlecad export is only available for cube-grid puzzles.',
    );
    await page.click('#exportScadFailed [data-x="ok"]');
    expect(existsSync(join(dir, 'bermuda.scad'))).toBe(false);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('Edit ▸ Convert Brick Grid Type to Other converts as the original', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-convert-'));
  const a = await launch();
  const page = a.page;
  try {
    const copy = join(dir, 'CubeInCage.xmpuzzle');
    copyFileSync(join(FIXTURES, 'CubeInCage.xmpuzzle'), copy);
    await answerDialogs(page, { open: copy });
    await menu(page, 'open');
    await expect(page.locator('#fname')).toHaveText('CubeInCage.xmpuzzle');

    // the choices: the grids engine.info says bricks convert to, the first chosen
    await page.click('button[data-tab="solver"]');
    await menu(page, 'convertGrid');
    const dialog = page.locator('#convertGrid');
    await expect(dialog).toContainText('Please Select Target grid');
    const radios = dialog.locator('input[name="target"]');
    await expect(radios).toHaveCount(2);
    expect(await radios.evaluateAll((r) => r.map((x) => (x as HTMLInputElement).value))).toEqual([
      'rhombic',
      'tetraOcta',
    ]);
    await expect(radios.first()).toBeChecked();
    // Abort leaves the puzzle as it is
    await dialog.locator('[data-x="abort"]').click();
    expect((await engineCall<Doc>(page, 'document.get')).grid).toBe('bricks');

    await menu(page, 'convertGrid');
    await page.locator('#convertGrid input[value="tetraOcta"]').check();
    const before = await generation(page);
    await page.click('#convertGrid [data-x="go"]');
    await replaced(page, before);
    await entitiesShown(page);
    const doc = await engineCall<Doc>(page, 'document.get');
    expect(doc).toMatchObject({ grid: 'tetraOcta', path: copy, dirty: true, canUndo: false });
    expect(await savedXml(page)).toBe(recordedXml('CubeInCage.tetra-octa.xmpuzzle'));
    // the file is kept: Save writes the converted puzzle over it
    await menu(page, 'save');
    await expect(page.locator('#stage')).toHaveAttribute('data-dirty', '0');
    expect(gunzipSync(readFileSync(copy)).toString('latin1')).toBe(
      recordedXml('CubeInCage.tetra-octa.xmpuzzle'),
    );

    // a tetra-octa puzzle converts to nothing
    await menu(page, 'convertGrid');
    await expect(page.locator('#convertGrid')).toContainText(
      'There are no grids that this puzzle can be converted to, sorry',
    );
    await page.click('#convertGrid [data-x="ok"]');
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
