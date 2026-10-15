// SPDX-License-Identifier: GPL-3.0-or-later
//
// The web stage 8 audit: a puzzle file whose names hold markup. The
// renderer draws its cards from template strings with the shapes', the
// problems' names and the notes out of the file, so a name that is HTML
// (a tag, a quote that closes an attribute, a style sheet) must come out
// as text everywhere it is shown: the shapes list and its rename field,
// the status window, the notes, the STL dialog, the Puzzle's tiles and
// menus, the Solver's problem, its Manage menu and the convert dialog, the
// explorers. Checked by the elements the markup would make (none) and by
// the texts shown (the names themselves).

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, command, ENGINE, launch, menu } from './app.ts';
import { runButton, runEnded, toSolver } from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** A name that is markup: an element, a quote that would close an attribute, an attribute of its own. */
const probe = (at: string): string => `<i class="xp" data-at="${at}">N</i>" data-xp="${at}" x="`;
const STYLE = '<style>#stage{outline:9px solid red}</style>';
const NOTES = '</textarea><i class="xp" data-at="notes">N</i>';

/** The attribute's value in XML. */
const xml = (s: string): string =>
  s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;');

/** A small puzzle (a 2-cell box, two 1-cell pieces: one assembly) with markup in every name. */
function puzzleFile(dir: string): string {
  const voxel = (size: string, name: string, cells: string): string =>
    `<voxel ${size} name="${xml(name)}" type="0">${cells}</voxel>`;
  const text = `<?xml version="1.0"?>
<puzzle version="2">
 <gridType type="0"/>
 <colors/>
 <shapes>
  ${voxel('x="2" y="1" z="1"', probe('s1'), '##')}
  ${voxel('x="1" y="1" z="1"', probe('s2'), '#')}
  ${voxel('x="1" y="1" z="1"', probe('s3') + STYLE, '#')}
 </shapes>
 <problems>
  <problem name="${xml(probe('p1'))}" state="0">
   <shapes><shape id="1" count="1"/><shape id="2" count="1"/></shapes>
   <result id="0"/>
   <bitmap/>
   <solutions/>
  </problem>
 </problems>
 <comment>${xml(NOTES).replaceAll('&quot;', '"')}</comment>
</puzzle>
`;
  const file = join(dir, 'markup.xmpuzzle');
  writeFileSync(file, text);
  return file;
}

/** No element the markup would make is anywhere in the page, and the style sheet did not apply. */
async function nothingInjected(page: Page, where: string): Promise<void> {
  const found = await page.evaluate(() => ({
    elements: document.querySelectorAll('.xp').length,
    attributes: document.querySelectorAll('[data-xp]').length,
    styles: document.querySelectorAll('style').length,
    outline: getComputedStyle(document.getElementById('stage') ?? document.body).outlineWidth,
  }));
  expect(found, where).toEqual({ elements: 0, attributes: 0, styles: 0, outline: '3px' });
}

test('a puzzle file whose names hold markup shows them as text everywhere', async () => {
  test.setTimeout(170_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-markup-'));
  const file = puzzleFile(dir);
  const a = await launch();
  const { page } = a;
  try {
    await command(page, { cmd: 'openPath', path: file });
    await expect(page.locator('#fname')).toHaveText('markup.xmpuzzle');
    await expect(page.locator('#shapeList .row')).toHaveCount(3);
    await nothingInjected(page, 'the Entities workspace');
    await expect(page.locator('#shapeList .row[data-id="s1"]')).toContainText(probe('s1'));
    for (const id of ['s2', 's3']) {
      await page.click(`#shapeList .row[data-id="${id}"]`);
      await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`^${id}:`));
      await nothingInjected(page, `shape ${id} selected`);
    }
    await page.click('#shapeList .row[data-id="s3"] [data-a="label"]');
    await expect(page.locator('#shapeList .row[data-id="s3"] input')).toHaveValue(
      probe('s3') + STYLE,
    );
    await nothingInjected(page, 'the rename field');
    await page.keyboard.press('Escape');
    await menu(page, 'statusWindow');
    await expect(page.locator('#statusWin tr[data-shape="s1"]')).toContainText(probe('s1'));
    await nothingInjected(page, 'the status window');
    await page.keyboard.press('Escape');
    await menu(page, 'notes');
    await expect(page.locator('#notes textarea')).toHaveValue(NOTES);
    await nothingInjected(page, 'the notes');
    await page.keyboard.press('Escape');
    await menu(page, 'export.stl');
    await expect(page.locator('.cfm select[data-f="shape"]')).toContainText(probe('s2'));
    await nothingInjected(page, 'the STL dialog');
    await page.keyboard.press('Escape');

    // the Puzzle workspace: the tiles, the menus, the table
    await page.keyboard.press('Control+2');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'puzzle');
    await expect(page.locator('#leftP')).toHaveAttribute('data-problem', /p/);
    await expect(page.locator('#pzUsed .ut[data-u="s2"]')).toContainText(probe('s2'));
    await nothingInjected(page, 'the Puzzle workspace');
    await page.click('#pzUsed .ut[data-u="s2"] b');
    await expect(page.locator('#pzSel')).toHaveAttribute('data-shape', 's2');
    await page.click('#pzUsed .ut[data-u="s2"]', { button: 'right' });
    await expect(page.locator('#pop')).toBeVisible();
    await nothingInjected(page, 'a piece selected, its menu open');
    await page.keyboard.press('Escape');
    await page.click('#pzChange');
    await expect(page.locator('#pop [data-k="s2"]')).toContainText(probe('s2'));
    await nothingInjected(page, 'the result menu');
    await page.keyboard.press('Escape');
    await page.click('#pzGrpHead');
    await expect(page.locator('#pzGrpTable tr[data-u="s2"]')).toContainText(probe('s2'));
    await nothingInjected(page, 'the table');

    // the Solver: the problem, a solve, the list, Manage, the convert dialog, the explorers
    await toSolver(page);
    await expect(page.locator('#slvProb')).toContainText(probe('p1'));
    await nothingInjected(page, 'the Solver');
    await page.click('#slvMode [data-m="dis"]');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await expect(page.locator('#slvSols')).toHaveText('1');
    await nothingInjected(page, 'the solve done');
    await page.click('#slvAct [data-a="manage"]');
    await expect(page.locator('#pop')).toBeVisible();
    await nothingInjected(page, 'the Manage menu');
    await page.keyboard.press('Escape');
    await menu(page, 'toShapes');
    await expect(page.locator('.cfm select[data-f="problem"]')).toContainText(probe('p1'));
    await nothingInjected(page, 'the convert dialog');
    await page.keyboard.press('Escape');
    await page.click('#slvAdv [data-x="movements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'movements');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^mx/);
    await nothingInjected(page, 'the movement explorer');
    await runButton(page, 'prepare');
    await expect(page.locator('#slvAdv [data-x="placements"]')).toBeEnabled();
    await page.click('#slvAdv [data-x="placements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'placements');
    await expect(page.locator('#sdrawer .dl [data-exr]')).toHaveCount(2);
    await nothingInjected(page, 'the placement explorer');
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
