// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sliding puzzles through the UI (the deferred plan's part 2,
// docs/webui-spec.md section 8): the 81-move test puzzle built from File ▸
// New (a start/goal shape, its pieces drawn, their starts and goal stamped
// in the Tray edit's Start and Goal), equal to the fixture as the engine
// reads it; solved with each sliding solver, counted and moved as `bt
// --json`; played step by step on the engine's poses with the walls
// drawn; the nested fixture with and without nested slides; what the
// original greyed out hidden; the notices in message boxes.

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join, resolve } from 'node:path';

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
  openFixture,
  readCallLog,
  selectShape,
  settle,
  shapes,
} from './app.ts';
import { clickCell, settled } from './editor.ts';
import { definition, problemGet, pzSettled } from './puzzle.ts';
import type { Problem } from './puzzle.ts';
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

const SOLVER = resolve(FIXTURES, 'test_sliding_solver.xmpuzzle');
const NESTED = resolve(FIXTURES, 'sliding/test_sliding_nested.xmpuzzle');

interface Tray {
  number: number;
  size: [number, number, number];
  starts: (string | null)[];
  goals: (string | null)[];
  error: string | null;
}

function tmpLog(): { dir: string; log: string } {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  return { dir, log: join(dir, 'calls.log') };
}

async function attr(page: Page, sel: string, name: string): Promise<string> {
  return (await page.getAttribute(sel, name)) ?? '';
}

/** Opens a puzzle file by its path (fixtures in folders too). */
async function openPath(page: Page, path: string): Promise<void> {
  const before = await page.locator('#stage').getAttribute('data-generation');
  // the question about unsaved changes is the document's being replaced
  const dirty = (await page.locator('#stage').getAttribute('data-dirty')) === '1';
  await command(page, { cmd: 'openPath', path });
  if (dirty) {
    await page.click('#discard [data-x="drop"]');
  }
  await expect(page.locator('#stage')).not.toHaveAttribute('data-generation', before ?? '');
  // the name, with the mark of unsaved changes when opening changed it
  await expect(page.locator('#fname')).toHaveText(
    new RegExp(`^${basename(path).replace('.', '\\.')}●?$`),
  );
  await settled(page);
}

/** Ids replaced by the shapes' labels (two documents' ids differ). */
async function byLabel(page: Page): Promise<(v: unknown) => unknown> {
  const map = new Map((await shapes(page)).map((s) => [s.id, s.label]));
  const swap = (v: unknown): unknown =>
    typeof v === 'string'
      ? (map.get(v) ?? v)
      : Array.isArray(v)
        ? v.map(swap)
        : v !== null && typeof v === 'object'
          ? Object.fromEntries(Object.entries(v).map(([k, x]) => [k, swap(x)]))
          : v;
  return swap;
}

/** The id of the shape with a label. */
async function idOf(page: Page, label: string): Promise<string> {
  const s = (await shapes(page)).find((x) => x.label === label);
  expect(s, label).toBeDefined();
  return s?.id ?? '';
}

/** A new piece (+ New: 6 by 6, one layer) with cells drawn. */
async function piece(page: Page, cells: [number, number][]): Promise<string> {
  const n = (await shapes(page)).length;
  await page.click('#btnNew');
  await expect(page.locator('#shapeList .row')).toHaveCount(n + 1);
  await settled(page);
  for (const [x, y] of cells) {
    await clickCell(page, [x, y, 0]);
  }
  return page.locator('#shapeList .row.sel').getAttribute('data-id') as Promise<string>;
}

/** Chooses the Tray edit mode. */
async function trayMode(page: Page, m: 'walls' | 'start' | 'goal'): Promise<void> {
  await page.click(`#trayRow [data-tm="${m}"]`);
  await expect(page.locator(`#trayRow [data-tm="${m}"]`)).toHaveClass(/\bon\b/);
}

/** Places a piece's start or goal: chosen in the list, its top-left cell clicked. */
async function stamp(page: Page, pieceId: string, at: [number, number]): Promise<void> {
  await page.click(`#stampRow [data-sp="${pieceId}"]`);
  await expect(page.locator(`#stampRow [data-sp="${pieceId}"]`)).toHaveClass(/\bon\b/);
  await clickCell(page, [at[0], at[1], 0]);
}

test('the 81-move puzzle built from New, solved by each sliding solver as bt, played with its walls', async () => {
  test.setTimeout(300_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    // the fixture as the engine reads it
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const fxSwap = await byLabel(page);
    const fxSnap = await engineCall<{ problems: { id: string }[]; shapes: { id: string }[] }>(
      page,
      'document.get',
    );
    const fxProblem = await problemGet(page, fxSnap.problems[0]?.id ?? '');
    const fxTray = await engineCall<Tray>(page, 'sliding.tray', { tray: fxSnap.shapes[0]?.id });

    // File ▸ New ▸ Sliding: empty, the start/goal row offered, no palette, no paint
    await newDocument(page, 'sliding');
    await expect(page.locator('#sgRow')).toHaveClass(/show/);
    await expect(page.locator('#sgRow [data-sg="del"]')).toBeDisabled();

    // New Start/Goal Positions: 6 by 6 (nothing selected), made 4 by 5
    await page.click('#sgRow [data-sg="new"]');
    await expect(page.locator('#shapeList .row')).toHaveCount(1);
    await settled(page);
    await expect(page.locator('#shapeList .row.sel .lbl')).toHaveText('(START/GOAL 1)');
    await expect(page.locator('#sgRow [data-sg="del"]')).toBeEnabled();
    await expect(page.locator('#shapeList .row.sel [data-a="del"]')).toBeDisabled();
    await expect(page.locator('#trayRow')).toHaveClass(/show/);
    await expect(page.locator('#colFoot')).toBeHidden();
    await expect(page.locator('#toolStrip [data-t="paint"]')).toHaveCount(0);
    await expect(page.locator('#sizeSteppers [data-d="2"]').first()).toBeDisabled();
    for (const [d, n] of [
      [0, 2],
      [1, 1],
    ] as const) {
      for (let i = 0; i < n; i++) {
        await page.click(`#sizeSteppers [data-d="${String(d)}"][data-s="-1"]`);
        await settled(page);
      }
    }
    const tray = await idOf(page, 'S1');
    expect((await engineCall<Tray>(page, 'sliding.tray', { tray })).size).toEqual([4, 5, 1]);
    // the view shows the two trays, nothing on them yet
    await expect(page.locator('#vp')).toHaveAttribute('data-tray', '-1:' + tray + ':solid:on');
    await expect(page.locator('#trayTags span')).toHaveText(['START', 'GOAL']);
    // the validity bar: not solvable yet
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '0');
    await expect(page.locator('#slideValid')).toContainText('Invalid Start/Goal positions - ');

    // the pieces, in the fixture's order: a 2x2 block, four uprights (one
    // drawn, three duplicated), a flat one, four units (duplicated)
    const block = await piece(page, [
      [0, 0],
      [1, 0],
      [0, 1],
      [1, 1],
    ]);
    const upright = [
      await piece(page, [
        [0, 0],
        [0, 1],
      ]),
    ];
    for (let i = 0; i < 3; i++) {
      await page.keyboard.press('Control+d');
      await expect(page.locator('#shapeList .row')).toHaveCount(4 + i);
      await settled(page);
      upright.push((await page.locator('#shapeList .row.sel').getAttribute('data-id')) ?? '');
    }
    const flat = await piece(page, [
      [0, 0],
      [1, 0],
    ]);
    const unit = [await piece(page, [[0, 0]])];
    for (let i = 0; i < 3; i++) {
      await page.keyboard.press('Control+d');
      await expect(page.locator('#shapeList .row')).toHaveCount(9 + i);
      await settled(page);
      unit.push((await page.locator('#shapeList .row.sel').getAttribute('data-id')) ?? '');
    }
    // a piece shows no Tray edit; a piece is not named as a tray
    await expect(page.locator('#trayRow')).not.toHaveClass(/show/);

    // the starts, in the order the fixture's problem lists its pieces
    await selectShape(page, tray);
    await trayMode(page, 'start');
    await expect(page.locator('#stampRow')).toHaveClass(/show/);
    await expect(page.locator('#toolStrip')).toBeHidden();
    await expect(page.locator('#stampRow .sgp')).toHaveCount(10);
    const starts: [string, [number, number]][] = [
      [block, [1, 4]],
      [upright[0] ?? '', [0, 4]],
      [upright[1] ?? '', [0, 2]],
      [flat, [1, 2]],
      [unit[0] ?? '', [1, 1]],
      [unit[1] ?? '', [0, 0]],
      [unit[2] ?? '', [3, 0]],
      [unit[3] ?? '', [2, 1]],
      [upright[2] ?? '', [3, 4]],
      [upright[3] ?? '', [3, 2]],
    ];
    for (const [p, at] of starts) {
      await stamp(page, p, at);
    }
    // a piece that does not fit is refused with the original's reason
    await page.click(`#stampRow [data-sp="${block}"]`);
    const refused = page.locator('#garea svg');
    const places = (): string[] =>
      readCallLog(log)
        .filter((c) => c.method === 'sliding.place')
        .map((c) => c.errc ?? 'ok');
    const placed = places().length;
    const why = await engineCall<{ fits: boolean; why: string | null }>(page, 'sliding.preview', {
      tray,
      piece: block,
      at: [3, 3],
      goal: false,
    });
    expect(why.fits).toBe(false);
    await clickCell(page, [3, 3, 0]);
    await expect.poll(() => places().slice(placed)).toEqual(['invalid_argument']);
    await expect(page.locator('#stText')).toHaveText(why.why ?? '');
    // the outline follows the mouse: orange where the piece is
    await page.hover(`#garea .cell[data-at="1,4,0"]`);
    await expect(refused).toHaveAttribute('data-stamp', /^rgb\(245,150,30\);/);
    // the goal: still invalid until it is placed
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '0');
    await trayMode(page, 'goal');
    await stamp(page, block, [1, 1]);
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '1');
    await expect(page.locator('#slideValid')).toHaveText(
      'Valid Start/Goal positions for pieces in Start and Goal of this shape',
    );
    await expect(page.locator('#trayRow [data-tm="goal"]')).toHaveClass(/\bok\b/);

    // what the fixture holds: the tray's starts and goals, the problem's pieces
    const swap = await byLabel(page);
    const built = await engineCall<Tray>(page, 'sliding.tray', { tray });
    expect(swap(built)).toEqual(fxSwap(fxTray));
    const p = (await engineCall<{ problems: { id: string }[] }>(page, 'document.get')).problems[0]
      ?.id;
    expect(p).toBeDefined();
    const before = await problemGet(page, p ?? '');
    expect(swap(before.parts)).toEqual(fxSwap(fxProblem.parts));
    expect(swap(before.result)).toEqual(fxSwap(fxProblem.result));
    // the view: both trays and the eleven stamps (ten starts, one goal)
    await expect
      .poll(async () => (await attr(page, '#vp', 'data-tray')).split(' ').length)
      .toBe(12);

    // the Puzzle: no new, copied or deleted problem, no Change…, no groups or rules
    await page.keyboard.press('Control+2');
    await pzSettled(page);
    await expect(page.locator('#pzNew')).toBeHidden();
    await expect(page.locator('#pzProblems [data-a="dup"]')).toHaveCount(0);
    await expect(page.locator('#pzProblems [data-a="del"]')).toHaveCount(0);
    await expect(page.locator('#pzChange')).toHaveCount(0);
    await expect(page.locator('#pzRules')).toBeHidden();
    await expect(page.locator('#pzUsed [data-u] [data-d="1"]').first()).toBeDisabled();

    // solved by each sliding solver: as bt counts and moves it; the
    // problem then is the fixture's (its pieces, the maximum of holes)
    await toSolver(page);
    expect(await shownProblem(page)).toBe(p);
    await page.click('#slvOptHead');
    await expect(page.locator('#slvOpts')).toBeVisible();
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await expect(page.locator('#slvMode')).toContainText('Find Solutions');
    await expect(page.locator('#slvOpts [data-o="keepMirrors"]')).toHaveCount(0);
    await expect(page.locator('#slvOpts [data-o="highMemory"]')).toHaveCount(1);
    await expect(page.locator('#slvOpts [data-o="partialNested"]')).toHaveClass(/\boff\b/);
    for (const solver of ['fast', 'deep', 'full'] as const) {
      await page.selectOption('#slvOpts select[data-o="slidingSolver"]', solver);
      await solverSettled(page);
      const stored = await engineCall<{ solverSettings: { solverType?: string } }>(
        page,
        'problem.get',
        { problem: p },
      );
      expect(stored.solverSettings.solverType).toBe(solver);
      await runButton(page, 'start');
      await runEnded(page, 'finished');
      const bt = btJson(SOLVER, ['--solver', solver]) as {
        assemblies: number;
        solutions: number;
        totalmoves: number | null;
      };
      expect(Number(await attr(page, '#leftS', 'data-assemblies'))).toBe(bt.assemblies);
      expect(Number(await attr(page, '#leftS', 'data-solutions'))).toBe(bt.solutions);
      const rows = await engineCall<{ rows: { moves: number | null }[] }>(page, 'solution.page', {
        problem: p,
        sortBy: 'list',
        limit: 10,
      });
      expect(rows.rows.map((r) => r.moves)).toEqual([bt.totalmoves]);
    }
    const after: Problem = await problemGet(page, p ?? '');
    expect(swap(definition(after))).toEqual(fxSwap(definition(fxProblem)));

    // played, step by step, on the engine's poses (routes followed), the walls drawn
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await expect(page.locator('#vp')).toHaveAttribute('data-solution-view', 'playback');
    await expect.poll(async () => Number(await attr(page, '#vp', 'data-walls'))).toBeGreaterThan(0);
    const poses = await enginePoses(page, p ?? '', await attr(page, '#splayer', 'data-solution'));
    expect(poses.steps).toBe(81);
    await expectPosesAt(page, poses, 0);
    for (let s = 1; s <= 81; s++) {
      await page.click('#splayer [data-t="fwd"]');
      await expectPosesAt(page, poses, s);
    }
    const bad = readCallLog(log).filter(
      (c) => c.errc !== null && c.errc !== 'superseded' && c.method !== 'sliding.place',
    );
    expect(bad).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the nested fixture with and without nested slides, as bt; partially nested needs nested', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  try {
    const cases: [string, string[][]][] = [
      [NESTED, [[], ['--nested-slides']]],
      [resolve(FIXTURES, 'sliding/slide_pocket_partial.xmpuzzle'), [[], ['--partial-nested']]],
    ];
    for (const [file, flags] of cases) {
      await openPath(page, file);
      await toSolver(page);
      if (!(await page.locator('#slvOpts').isVisible())) {
        await page.click('#slvOptHead');
      }
      for (const f of flags) {
        const nested = f.length > 0;
        const partial = f.includes('--partial-nested');
        const sw = async (k: string, on: boolean): Promise<void> => {
          const el = page.locator(`#slvOpts .switch[data-o="${k}"]`);
          if ((await el.getAttribute('aria-checked')) !== String(on)) {
            await el.click();
          }
          await expect(el).toHaveAttribute('aria-checked', String(on));
          await solverSettled(page);
        };
        await sw('nestedSlides', nested);
        await expect(page.locator('#slvOpts .switch[data-o="partialNested"]')).toHaveClass(
          nested ? /^(?!.*\boff\b)/ : /\boff\b/,
        );
        if (nested) {
          await sw('partialNested', partial);
        }
        await runButton(page, 'start');
        await runEnded(page, 'finished');
        const bt = btJson(file, f);
        expect(Number(await attr(page, '#leftS', 'data-assemblies')), f.join(' ')).toBe(
          bt.assemblies,
        );
        expect(Number(await attr(page, '#leftS', 'data-solutions')), f.join(' ')).toBe(
          bt.solutions,
        );
        // the solver's note (no solution: proven) comes in a message box
        if (bt.solutions === 0) {
          await expect(page.locator('#notice')).toBeVisible();
          await page.click('#notice [data-x="ok"]');
        }
        await expect(page.locator('#notice')).toHaveCount(0);
        const sp = await shownProblem(page);
        expect((await pageIds(page, sp)).length).toBe(bt.solutions);
      }
    }
  } finally {
    await a.close();
  }
});

test('notices in message boxes; what the original greyed out is not offered or is refused', async () => {
  test.setTimeout(120_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const a = await launch();
  const page = a.page;
  try {
    // a sliding shape of two layers is made flat on open, and the user told
    const deep = join(dir, 'two-layers.xmpuzzle');
    writeFileSync(
      deep,
      '<?xml version="1.0"?>\n<puzzle version="2"><gridType type="5"/><colors/><shapes><voxel x="2" y="1" z="2" type="0">####</voxel></shapes><problems/></puzzle>\n',
    );
    await openPath(page, deep);
    await expect(page.locator('#notice')).toBeVisible();
    await expect(page.locator('#notice .cb')).toContainText(
      'Sliding shapes are one layer deep. S1 had more than one layer',
    );
    await page.click('#notice [data-x="ok"]');
    await expect(page.locator('#notice')).toHaveCount(0);

    // a stamped piece whose shape changes loses its start: told in a message box
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const s3 = await idOf(page, 'S3');
    await selectShape(page, s3);
    await expect(page.locator('#colFoot')).toBeHidden();
    // no task that adds layers is offered
    await expect(page.locator('#tmat [data-task="rotate6"]')).toHaveCount(0);
    await clickCell(page, [1, 4, 0]);
    await expect(page.locator('#notice .cb')).toContainText(
      'S3 changed shape: its start and goal were removed from S1.',
    );
    await page.click('#notice [data-x="ok"]');
    await page.keyboard.press('Control+z');
    await settled(page);
    // a piece is not renamed as a tray (the engine refuses, the status says why)
    await page.dblclick(`#shapeList .row[data-id="${s3}"] .chip`);
    await page.keyboard.type('sg:5:x');
    await page.keyboard.press('Enter');
    await expect(page.locator('#stText')).toContainText('cannot begin with');
    expect((await shapes(page)).find((s) => s.id === s3)).toBeDefined();
    const snap = await engineCall<{
      shapes: { id: string; name: string; startGoal: number | null }[];
    }>(page, 'document.get');
    expect(snap.shapes.find((s) => s.id === s3)?.startGoal).toBeNull();
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('reference screenshots: a start/goal shape’s Walls, Start and Goal, its two trays, the walls in the player', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  try {
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const tray = await idOf(page, 'S1');
    await selectShape(page, tray);
    await expect(page.locator('#vp')).toHaveAttribute('data-tray', /^(\S+ ){11}\S+$/);
    await page.mouse.move(2, 2);
    await settle(page);
    await expect(page.locator('#garea')).toHaveScreenshot('sliding-tray-walls.png');
    await expect(page.locator('#vp')).toHaveScreenshot('sliding-trays.png');
    for (const m of ['start', 'goal'] as const) {
      await trayMode(page, m);
      await page.click(`#stampRow [data-sp="${await idOf(page, 'S2')}"]`);
      await page.mouse.move(2, 2);
      await settle(page);
      await expect(page.locator('#right')).toHaveScreenshot(`sliding-tray-${m}.png`);
    }
    await toSolver(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await expect.poll(async () => Number(await attr(page, '#vp', 'data-walls'))).toBeGreaterThan(0);
    await page.mouse.move(2, 2);
    await settle(page);
    await expect(page.locator('#vp')).toHaveScreenshot('sliding-walls.png');
  } finally {
    await a.close();
  }
});
