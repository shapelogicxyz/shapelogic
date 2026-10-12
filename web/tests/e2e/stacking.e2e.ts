// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stacking and Panex puzzles through the UI (the deferred plan's part 4,
// docs/webui-spec.md section 9): the Tower of Hanoi and a Panex swap of two
// three-disc towers built from File ▸ New (discs in the disc list, the rod
// set's rules in the Rods card, the board, the start and the goal in the
// Puzzle's Stacks card), saved byte for byte as the fixtures; solved by the
// Stacking and the Panex Solver as `bt --json`; played in transfers on the
// engine's poses with the board's pegs; a seven-disc Panex search paused, the
// app restarted on the same data and the search carried on from where it
// was saved (Solve's question: Continue); the rod sets' commands and the
// rules' dependencies; reference screenshots.

import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join, resolve } from 'node:path';
import { gunzipSync } from 'node:zlib';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  BUILT,
  command,
  ENGINE,
  engineCall,
  FIXTURES,
  launch,
  newDocument,
  readCallLog,
  settle,
} from './app.ts';
import { settled } from './editor.ts';
import {
  btJson,
  enginePoses,
  expectPosesAt,
  pageIds,
  runButton,
  runEnded,
  shownProblem,
  solverSettled,
  toSolver,
} from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const HANOI = resolve(FIXTURES, 'test_stacking_hanoi.xmpuzzle');
const SWAP3 = resolve(FIXTURES, 'stacking/stack_panex_swap_3.xmpuzzle');

/** A Panex swap of two towers of n discs (the corpus's stack_panex_swap_<n>, as the contract suite makes it). */
function panexSwap(n: number): string {
  const disc = (size: number): string => {
    const d = 2 * size + 1;
    const half = Math.floor((d * d) / 2);
    return `  <voxel x="${String(d)}" y="${String(d)}" z="1" hx="${String(size)}" hy="${String(size)}" diskSize="${String(size)}" type="0">${'#'.repeat(half)}_${'#'.repeat(half)}</voxel>\n`;
  };
  const tower = Array.from({ length: n }, (_, i) => disc(n - i)).join('');
  const parts = Array.from(
    { length: 2 * n },
    (_, i) => `    <shape id="${String(i)}" count="1"/>\n`,
  ).join('');
  const left = Array.from({ length: n }, (_, i) => `${String(i)}:0`).join(',');
  const right = Array.from({ length: n }, (_, i) => `${String(n + i)}:0`).join(',');
  return (
    '<?xml version="1.0"?>\n<puzzle version="2">\n <gridType type="6"/>\n <colors/>\n' +
    ` <shapes>\n${tower}${tower} </shapes>\n <rodSets>\n` +
    `  <rodSet rods="3" grow="0" height="${String(n)}" sizeMatters="0" distanceMatters="0" moveOver="0" panex="1"/>\n </rodSets>\n <problems>\n  <problem state="0">\n` +
    `   <shapes>\n${parts}   </shapes>\n   <result id="4294967295"/>\n` +
    '   <stacking rodSet="0">\n    <start>\n' +
    `     <rod pieces="${left}"/>\n     <rod/>\n     <rod pieces="${right}"/>\n` +
    '    </start>\n    <goal>\n' +
    `     <rod pieces="${right}"/>\n     <rod/>\n     <rod pieces="${left}"/>\n` +
    '    </goal>\n   </stacking>\n   <bitmap/>\n  </problem>\n </problems>\n' +
    ' <comment/>\n</puzzle>\n'
  );
}

/** A puzzle file's XML (the corpus keeps some gzipped). */
function fileText(path: string): string {
  const b = readFileSync(path);
  return (b[0] === 0x1f && b[1] === 0x8b ? gunzipSync(b) : b).toString('utf8');
}

function tmpDir(): string {
  return mkdtempSync(join(tmpdir(), 'bt-e2e-stack-'));
}

async function attr(page: Page, sel: string, name: string): Promise<string> {
  return (await page.getAttribute(sel, name)) ?? '';
}

/** Opens a puzzle file by its path (fixtures in folders too). */
async function openPath(page: Page, path: string): Promise<void> {
  const before = await attr(page, '#stage', 'data-generation');
  const dirty = (await attr(page, '#stage', 'data-dirty')) === '1';
  await command(page, { cmd: 'openPath', path });
  if (dirty) {
    await page.click('#discard [data-x="drop"]');
  }
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before);
  await expect(page.locator('#fname')).toHaveText(
    new RegExp(`^${basename(path).replace('.', '\\.')}●?$`),
  );
  await settled(page);
}

/** The document's file as the engine would save it now. */
async function savedText(page: Page): Promise<string> {
  const r = await engineCall<{ data: string }>(page, 'document.saveBytes');
  return Buffer.from(r.data, 'base64').toString('utf8');
}

/** Waits until the Puzzle's cards show the engine's revision (the stacking's board read with it). */
async function stkSettled(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
  await expect
    .poll(async () => {
      const snap = await engineCall<{ revision: number }>(page, 'document.get');
      return attr(page, '#leftP', 'data-revision').then((r) => r === String(snap.revision));
    })
    .toBe(true);
}

/** A new disc (+ New) of a size typed in its field. */
async function disc(page: Page, size: number): Promise<void> {
  const n = await page.locator('#shapeList .row').count();
  await page.click('#btnNew');
  await expect(page.locator('#shapeList .row')).toHaveCount(n + 1);
  await settled(page);
  const field = page.locator('#shapeList .row.sel [data-dsize]');
  await expect(field).toHaveValue('1');
  if (size !== 1) {
    await field.fill(String(size));
    await field.press('Enter');
    await settled(page);
    await expect(page.locator('#shapeList .row.sel [data-dsize]')).toHaveValue(String(size));
  }
}

/** Chooses the rod on the Stacks card's slider (its place: the pocket first). */
async function rodPlace(page: Page, place: number): Promise<void> {
  await page.$eval(
    '#pzRod',
    (el, v) => {
      const i = el as HTMLInputElement;
      i.value = String(v);
      i.dispatchEvent(new Event('change', { bubbles: true }));
    },
    place,
  );
  await expect(page.locator('#pzStacks')).toHaveAttribute('data-rod', String(place));
}

/** Start or Goal on the Stacks card. */
async function stackMode(page: Page, goal: boolean): Promise<void> {
  await page.click(`#pzStacks [data-mode="${goal ? 'goal' : 'start'}"]`);
  await expect(page.locator('#pzStacks')).toHaveAttribute('data-goal', goal ? '1' : '0');
  await expect(page.locator('#pzStacks')).toHaveAttribute('data-board-goal', goal ? '1' : '0');
  await stkSettled(page);
}

/** Puts a disc (by its label) on top of the rod chosen: the library's +. */
async function place(page: Page, label: string): Promise<void> {
  const depth = Number(await attr(page, '#pzStacks', 'data-depth'));
  await page.locator('#pzLib .lc', { hasText: label }).locator('[data-add]').click();
  await stkSettled(page);
  await expect(page.locator('#pzStacks')).toHaveAttribute('data-depth', String(depth + 1));
}

/** The board the Puzzle's view draws equals the engine's: each disc's centre at its cell. */
async function expectBoardShown(page: Page, problem: string, goal: boolean): Promise<void> {
  const b = await engineCall<{
    centre: [number, number, number];
    layout: { discs: { shape: string; size: number; at: [number, number, number] | null }[] };
  }>(page, 'stacking.board', { problem, goal });
  const want = b.layout.discs
    .filter((d) => d.at !== null)
    .map((d) => {
      const at = d.at ?? [0, 0, 0];
      return [at[0] - d.size - b.centre[0], at[1] - d.size - b.centre[1], at[2] - b.centre[2]]
        .map((v) => v.toFixed(4))
        .join(',');
    });
  await expect
    .poll(async () =>
      (await attr(page, '#vp', 'data-board-poses'))
        .split(' ')
        .filter((p) => p !== '')
        .map((p) => p.split(';')[0]),
    )
    .toEqual(want);
}

test('the Tower of Hanoi built from File ▸ New, saved as the fixture', async () => {
  const dir = tmpDir();
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await newDocument(page, 'stacking');
    // the disc list in the shapes list's place, the Rods card in the voxel editor's
    await expect(page.locator('#stage')).toHaveClass(/stk/);
    await expect(page.locator('#rightTitle')).toHaveText('Rods');
    await expect(page.locator('#insp')).toBeHidden();
    await expect(page.locator('#garea')).toBeHidden();
    for (const size of [3, 2, 1]) {
      await disc(page, size);
    }
    // a size out of the list's bounds is refused with the original's message, the size then 1
    const third = page.locator('#shapeList .row').nth(2).locator('[data-dsize]');
    await third.fill('101');
    await third.press('Enter');
    await expect(page.locator('#notice')).toContainText('please pick a value between 1-100');
    await page.click('#notice [data-x="ok"]');
    await settled(page);
    await expect(page.locator('#shapeList .row').nth(2).locator('[data-dsize]')).toHaveValue('1');
    // + and − step a disc's size
    await page.locator('#shapeList .row').nth(1).locator('[data-a="dp"]').click();
    await settled(page);
    await expect(page.locator('#shapeList .row').nth(1).locator('[data-dsize]')).toHaveValue('3');
    await page.locator('#shapeList .row').nth(1).locator('[data-a="dm"]').click();
    await settled(page);
    await expect(page.locator('#shapeList .row').nth(1).locator('[data-dsize]')).toHaveValue('2');

    // the rod set: R1, labelled, its rules the defaults (three rods that grow, size matters)
    await page.click('#rodsPanel [data-rod]');
    await expect.poll(async () => Number(await attr(page, '#vp', 'data-board-boxes'))).toBe(1);
    await expect(page.locator('#vtag')).toContainText('R1');
    await page.click('#rodsPanel [data-r="label"]');
    await page.fill('#rodNameIn', 'Hanoi');
    await page.click('#rodName [data-x="ok"]');
    await settled(page);
    await expect(page.locator('#rodsPanel .rodrow.sel')).toContainText('Hanoi');
    // choosing a disc shows it again
    await page.locator('#shapeList .row').first().locator('.chip').click();
    await expect(page.locator('#vp')).toHaveAttribute('data-board-boxes', '0');

    // the Puzzle: a problem, its board, the start's and the goal's stacks
    await page.keyboard.press('Control+2');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'puzzle');
    await page.click('#pzNew');
    await stkSettled(page);
    await expect(page.locator('#pzBoard')).toContainText('No Start/Goal Rods');
    await expect(page.locator('#pzStackValid')).toHaveAttribute('data-valid', '0');
    // +1 before a board: refused with the original's words
    await page.locator('#pzLib .lc').first().click();
    await page.click('#pzStacks [data-s="add"]');
    await expect(page.locator('#stText')).toContainText(
      'Choose a rod set with Set Start/Goal Rods',
    );
    await page.click('#pzSetRods');
    await stkSettled(page);
    await expect(page.locator('#pzBoard')).toContainText('Result: R1 - Hanoi');
    const p = (await attr(page, '#leftP', 'data-problem')) || '';
    await page.locator('#pzProblems .row.sel [data-a="rename"]').click();
    await page.fill('#pzProblems .row.sel .lbl input', 'Tower of Hanoi');
    await page.press('#pzProblems .row.sel .lbl input', 'Enter');
    await stkSettled(page);
    // the start: all three on rod 1, largest first (All +1)
    await expect(page.locator('#pzRodCap')).toHaveText('Rod: 1');
    await page.click('#pzStacks [data-s="all"]');
    await stkSettled(page);
    await expect(page.locator('#pzStacks .stackrow')).toHaveText([/S3/, /S2/, /S1/]);
    // the goal is missing: invalid, with the engine's reason
    await expect(page.locator('#pzStackValid')).toHaveAttribute('data-valid', '0');
    await expect(page.locator('#pzStackValid')).toContainText('Invalid - ');
    await expectBoardShown(page, p, false);
    // the goal: rod 3, one disc at a time, then their order mended with the arrows
    await stackMode(page, true);
    await rodPlace(page, 2);
    await expect(page.locator('#pzRodCap')).toHaveText('Rod: 3');
    for (const label of ['S2', 'S1', 'S3']) {
      await place(page, label);
    }
    await expect(page.locator('#pzStacks .stackrow')).toHaveText([/S3/, /S1/, /S2/]);
    // S1 (second from the top) down: S2 above it
    await page.locator('#pzStacks .stackrow').nth(1).click();
    await page.click('#pzStacks [data-s="down"]');
    await stkSettled(page);
    await expect(page.locator('#pzStacks .stackrow')).toHaveText([/S3/, /S2/, /S1/]);
    await expect(page.locator('#pzStacks .stackrow.sel')).toHaveText(/S1/);
    await expect(page.locator('#pzStackValid')).toHaveAttribute('data-valid', '1');
    await expect(page.locator('#pzStackValid')).toHaveText('Valid');
    await expectBoardShown(page, p, true);
    await expect(page.locator('#stText')).toContainText('Valid');

    // saved as the fixture, byte for byte
    expect(await savedText(page)).toBe(fileText(HANOI));

    // −1 takes the chosen disc off both stackings; undo brings it back
    await page.locator('#pzStacks .stackrow').first().click();
    await page.click('#pzStacks [data-s="remove"]');
    await stkSettled(page);
    await expect(page.locator('#pzStacks .stackrow')).toHaveCount(2);
    await stackMode(page, false);
    await rodPlace(page, 0);
    await expect(page.locator('#pzStacks .stackrow')).toHaveCount(2);
    await page.keyboard.press('Control+z');
    await stkSettled(page);
    await expect(page.locator('#pzStacks .stackrow')).toHaveCount(3);
    expect(await savedText(page)).toBe(fileText(HANOI));

    const bad = readCallLog(log).filter(
      (c) =>
        c.errc !== null &&
        c.errc !== 'superseded' &&
        !(c.method === 'stacking.placeDisc' && c.errc === 'invalid_argument'),
    );
    expect(bad).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a Panex swap of three-disc towers built from New: the rules, the stacks, saved as the fixture', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await newDocument(page, 'stacking');
    for (const size of [3, 2, 1, 3, 2, 1]) {
      await disc(page, size);
    }
    // the rules: a defined height of 3, size matters off, Panex columns
    await page.click('#rodsPanel [data-rod]');
    await page.check('#rodsPanel input[data-f="growHeight"][value="0"]');
    await settled(page);
    const height = page.locator('#rodsPanel input[data-f="definedHeight"]');
    await expect(height).toBeEnabled();
    await height.fill('3');
    await height.press('Tab');
    await settled(page);
    await page.uncheck('#rodsPanel input[data-f="sizeMatters"]');
    await settled(page);
    // the pocket and the channel need Panex columns
    await expect(page.locator('#rodsPanel input[data-f="pocketColumn"]')).toBeDisabled();
    await expect(page.locator('#rodsPanel input[data-f="outsideChannel"]')).toBeDisabled();
    await page.check('#rodsPanel input[data-f="panexColumns"]');
    await settled(page);
    await expect(page.locator('#rodsPanel input[data-f="sizeMatters"]')).toBeDisabled();
    await expect(page.locator('#rodsPanel input[data-f="pocketColumn"]')).toBeEnabled();
    await expect(page.locator('#rodsPanel input[data-f="outsideChannel"]')).toBeEnabled();
    await expect.poll(async () => Number(await attr(page, '#vp', 'data-board-boxes'))).toBe(2);

    await page.keyboard.press('Control+2');
    await page.click('#pzNew');
    await stkSettled(page);
    await page.click('#pzSetRods');
    await stkSettled(page);
    const p = (await attr(page, '#leftP', 'data-problem')) || '';
    const build = async (goal: boolean, rod1: string[], rod3: string[]): Promise<void> => {
      await stackMode(page, goal);
      await rodPlace(page, 0);
      for (const l of rod1) {
        await place(page, l);
      }
      await rodPlace(page, 2);
      for (const l of rod3) {
        await place(page, l);
      }
    };
    await build(false, ['S1', 'S2', 'S3'], ['S4', 'S5', 'S6']);
    await build(true, ['S4', 'S5', 'S6'], ['S1', 'S2', 'S3']);
    await expect(page.locator('#pzStackValid')).toHaveAttribute('data-valid', '1');
    await expectBoardShown(page, p, true);
    await stackMode(page, false);
    await expectBoardShown(page, p, false);
    expect(await savedText(page)).toBe(fileText(SWAP3));
    await expect(page).toHaveScreenshot('stacking-puzzle-board.png');

    // the Entities' rod set alone, the Panex columns' Vs
    await page.keyboard.press('Control+1');
    await page.click('#rodsPanel [data-rod]');
    await expect.poll(async () => Number(await attr(page, '#vp', 'data-board-boxes'))).toBe(2);
    await settle(page);
    await expect(page).toHaveScreenshot('stacking-rods.png');
  } finally {
    await a.close();
  }
});

test('solved by the Stacking and the Panex Solver as bt; played in transfers on the engine poses', async () => {
  test.setTimeout(180_000);
  const dir = tmpDir();
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    for (const [file, solver, label] of [
      [HANOI, 'stacking', 'Stacking Solver'],
      [SWAP3, 'panex', 'Panex Solver'],
    ] as const) {
      await openPath(page, file);
      await toSolver(page);
      // Find Solutions checked at first; no Prepare; the board's one solver
      await expect(page.locator('#slvMode [data-m="dis"]')).toHaveClass(/on/);
      await expect(page.locator('#slvRun [data-r="prepare"]')).toHaveCount(0);
      if (!(await page.locator('#slvOpts').isVisible())) {
        await page.click('#slvOptHead');
      }
      await expect(page.locator('#slvOpts select[data-o="stackingSolver"]')).toHaveValue(solver);
      await expect(page.locator('#slvOpts select[data-o="stackingSolver"] option')).toHaveText([
        label,
      ]);
      await expect(page.locator('#slvProb')).toContainText('Result R1');
      await runButton(page, 'start');
      await runEnded(page, 'finished');
      const problem = await shownProblem(page);
      const bt = btJson(file, ['--solver', solver, '--panex-dir', '']) as {
        assemblies: number;
        solutions: number;
        totalmoves: number;
      };
      await expect(page.locator('#leftS')).toHaveAttribute('data-solutions', String(bt.solutions));
      await expect(page.locator('#leftS')).toHaveAttribute(
        'data-assemblies',
        String(bt.assemblies),
      );
      const ids = await pageIds(page, problem);
      expect(ids).toHaveLength(1);
      const row = await engineCall<{ rows: { moves: number | null }[] }>(page, 'solution.page', {
        problem,
        limit: 1,
      });
      expect(row.rows[0]?.moves).toBe(bt.totalmoves);
      // the player in transfers, the board's pegs around the discs
      await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
      await expect(page.locator('#splayer')).toHaveAttribute('data-steps', String(bt.totalmoves));
      await expect
        .poll(async () => Number(await attr(page, '#vp', 'data-board-boxes')))
        .toBe(solver === 'panex' ? 2 : 1);
      const poses = await enginePoses(page, problem, await attr(page, '#splayer', 'data-solution'));
      expect(poses.steps).toBe(bt.totalmoves);
      await expectPosesAt(page, poses, 0);
      const steps = Math.min(poses.steps, 12);
      for (let s = 1; s <= steps; s++) {
        await page.click('#splayer [data-t="fwd"]');
        await expectPosesAt(page, poses, s);
      }
      // no take-apart, no assembler: Analyse and the explorers are not offered
      await expect(page.locator('#slvAdv [data-x="placements"]')).toBeDisabled();
      await expect(page.locator('#slvAdv [data-x="movements"]')).toBeDisabled();
      await expect(page.locator('#slvAdv [data-x="step"]')).toBeDisabled();
    }
    await settle(page);
    await expect(page).toHaveScreenshot('stacking-player.png');
    const bad = readCallLog(log).filter((c) => c.errc !== null && c.errc !== 'superseded');
    expect(bad).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a Panex search paused, the app restarted, the search carried on from where it was saved', async () => {
  test.setTimeout(300_000);
  const dir = tmpDir();
  const file = join(dir, 'swap7.xmpuzzle');
  writeFileSync(file, panexSwap(7));
  const userData = join(dir, 'data');
  let a = await launch({ userData });
  try {
    let page = a.page;
    // the app's own folder for saved searches (the page cannot set it)
    const s = await engineCall<{ panexFolder: string }>(page, 'settings.get');
    expect(s.panexFolder).toBe(join(userData, 'panex'));
    await expect(engineCall(page, 'settings.set', { panexFolder: dir })).rejects.toThrow(
      /invalid_argument/,
    );
    // one thread, so the search is slow enough to pause
    await engineCall(page, 'settings.set', { threads: 1 });
    await openPath(page, file);
    await toSolver(page);
    await runButton(page, 'start');
    await expect(page.locator('#slvActivity')).toContainText(/Panex search, \d+ moves deep/, {
      timeout: 20_000,
    });
    await runButton(page, 'pause');
    await runEnded(page, 'paused');
    // the note (where it was saved) in a message box; the search saved in the app's folder
    await expect(page.locator('#notice')).toContainText('The search is saved');
    await page.click('#notice [data-x="ok"]');
    await expect(page.locator('#slvActivity')).toHaveText('Paused. The search is saved.');
    await expect(page.locator('#slvRun [data-r="continue"]')).toBeEnabled();
    await expect(page.locator('#slvRun [data-r="continue"]')).toHaveAttribute(
      'title',
      /^Carry on a saved search/,
    );
    expect(existsSync(join(userData, 'panex'))).toBe(true);
    expect(readdirSync(join(userData, 'panex')).length).toBeGreaterThan(0);
    await a.close();

    // a new start on the same data: Solve asks, Continue carries the search on
    a = await launch({ userData });
    page = a.page;
    await openPath(page, file);
    await toSolver(page);
    await expect(page.locator('#slvActivity')).toHaveText('Paused. The search is saved.');
    await page.click('#slvRun [data-r="start"]');
    await expect(page.locator('#savedSearch')).toContainText('This problem has a saved search');
    await expect(page.locator('#savedSearch')).toContainText(
      'Continue it, or start over and discard it?',
    );
    await page.click('#savedSearch [data-x="continue"]');
    await runEnded(page, 'finished', 200_000);
    const problem = await shownProblem(page);
    const row = await engineCall<{ rows: { moves: number | null }[] }>(page, 'solution.page', {
      problem,
      limit: 1,
    });
    expect(row.rows[0]?.moves).toBe(2189);
    await expect(page.locator('#splayer')).toHaveAttribute('data-steps', '2189');
    // the search is through: its folder is gone, nothing left to continue
    await expect(page.locator('#slvActivity')).toHaveText('Finished.');
    await solverSettled(page);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('rod sets: new, copy, label, order, delete; frozen while solving; Abort of a saved search', async () => {
  test.setTimeout(180_000);
  const dir = tmpDir();
  const file = join(dir, 'swap7.xmpuzzle');
  writeFileSync(file, panexSwap(7));
  const a = await launch();
  const page = a.page;
  try {
    await openPath(page, file);
    await page.click('#rodsPanel [data-rod]');
    await page.click('#rodsPanel [data-r="new"]');
    await settled(page);
    await expect(page.locator('#rodsPanel .rodrow')).toHaveCount(2);
    await expect(page.locator('#rodsPanel .rodrow.sel')).toContainText('R2');
    // a new rod set: three rods that grow, size matters
    await expect(page.locator('#rodsPanel input[data-f="rodCount"]')).toHaveValue('3');
    await expect(page.locator('#rodsPanel input[data-f="growHeight"][value="1"]')).toBeChecked();
    await expect(page.locator('#rodsPanel input[data-f="sizeMatters"]')).toBeChecked();
    // two rods: no outside channel even with Panex columns
    await page.check('#rodsPanel input[data-f="panexColumns"]');
    await settled(page);
    const count = page.locator('#rodsPanel input[data-f="rodCount"]');
    await count.fill('2');
    await count.press('Tab');
    await settled(page);
    await expect(page.locator('#rodsPanel input[data-f="outsideChannel"]')).toBeDisabled();
    await page.click('#rodsPanel [data-r="copy"]');
    await settled(page);
    await expect(page.locator('#rodsPanel .rodrow')).toHaveCount(3);
    await page.click('#rodsPanel [data-r="label"]');
    await page.fill('#rodNameIn', 'Two');
    await page.click('#rodName [data-x="ok"]');
    await settled(page);
    await page.click('#rodsPanel [data-r="left"]');
    await settled(page);
    await expect(page.locator('#rodsPanel .rodrow').nth(1).locator('.chip')).toHaveText('R2');
    await expect(page.locator('#rodsPanel .rodrow').nth(1).locator('.lbl')).toHaveText('Two');
    await page.click('#rodsPanel [data-r="del"]');
    await settled(page);
    await expect(page.locator('#rodsPanel .rodrow')).toHaveCount(2);
    const sets = await engineCall<{ rodSets: { label: string; name: string }[] }>(
      page,
      'document.get',
    );
    expect(sets.rodSets.map((r) => r.label)).toEqual(['R1', 'R2']);

    // while a search runs, the rod sets are frozen
    await engineCall(page, 'settings.set', { threads: 1 });
    await toSolver(page);
    await runButton(page, 'start');
    await expect(page.locator('#slvActivity')).toContainText(/Panex search/, { timeout: 20_000 });
    await page.keyboard.press('Control+1');
    await expect(page.locator('#rodsPanel [data-r="new"]')).toBeDisabled();
    await expect(page.locator('#rodsPanel input[data-f="rodCount"]')).toBeDisabled();
    await page.keyboard.press('Control+3');
    await runButton(page, 'pause');
    await runEnded(page, 'paused');
    await page.click('#notice [data-x="ok"]');
    // Abort throws the saved search away, asked first
    await runButton(page, 'abort');
    await page.click('.cfm-scrim [data-x="1"]');
    await solverSettled(page);
    await expect(page.locator('#slvActivity')).toHaveText('Not solved yet.');
    await expect(page.locator('#slvRun [data-r="continue"]')).toBeDisabled();
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
