// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sliding puzzles through the UI (the deferred plan's part 2,
// docs/webui-spec.md section 8): the 81-move test puzzle built from File ▸
// New (a board drawn as any shape, its pieces drawn, put in the puzzle and
// their starts and goal placed in the Puzzle workspace's Start and goal),
// equal to the fixture as the engine
// reads it; solved with each sliding solver, counted and moved as `bt
// --json`; played step by step on the engine's poses with the walls
// drawn; the nested fixture with and without nested slides; what the
// original greyed out hidden; the notices in message boxes; copies of a
// piece placed one by one.

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
  SHAPE_ROWS,
  shapes,
} from './app.ts';
import { clickCell, settled, shapeGet } from './editor.ts';
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

/** A new piece (+ New Piece: 6 by 6, one layer to begin with) with cells drawn. */
async function piece(page: Page, cells: [number, number][]): Promise<string> {
  const n = (await shapes(page)).length;
  await page.click('#btnNew');
  await expect(page.locator(SHAPE_ROWS)).toHaveCount(n + 1);
  await settled(page);
  for (const [x, y] of cells) {
    await clickCell(page, [x, y, 0]);
  }
  return page.locator('#shapeList .row.sel').getAttribute('data-id') as Promise<string>;
}

/** The Start or the Goal grid of the Puzzle workspace's Start and goal. */
function gridOf(goal: boolean): string {
  return goal ? '#sgGoalGrid' : '#sgStartGrid';
}

/** Clicks a cell of the Start or Goal grid (the right button takes a copy off). */
async function boardClick(
  page: Page,
  goal: boolean,
  at: [number, number],
  button: 'left' | 'right' = 'left',
): Promise<void> {
  const cell = page.locator(`${gridOf(goal)} .cell[data-at="${String(at[0])},${String(at[1])},0"]`);
  await expect(cell).toHaveCount(1);
  const box = await cell.boundingBox();
  expect(box).not.toBeNull();
  await page.mouse.click(
    (box?.x ?? 0) + (box?.width ?? 0) / 2,
    (box?.y ?? 0) + (box?.height ?? 0) / 2,
    {
      button,
    },
  );
  await pzSettled(page);
}

/** Places a copy of a piece's start or goal: chosen above the grid, its top-left cell clicked. */
async function stamp(
  page: Page,
  pieceId: string,
  at: [number, number],
  goal = false,
): Promise<void> {
  const parts = goal ? '#sgGoalParts' : '#sgStartParts';
  await page.click(`${parts} [data-sp="${pieceId}"]`);
  await expect(page.locator(`${parts} [data-sp="${pieceId}"]`)).toHaveClass(/\bon\b/);
  await boardClick(page, goal, at);
}

test('the 81-move puzzle built from New, solved as bt, played with its walls', async () => {
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

    // File ▸ New ▸ Sliding: empty, + New Piece and + New Board offered, no palette, no paint
    await newDocument(page, 'sliding');
    await expect(page.locator('#btnNew')).toHaveText('+ New Piece');
    await expect(page.locator('#btnNewBoard')).toBeVisible();

    // + New Board: 6 by 6 (nothing selected); its trash deletes it, then
    // another, made 4 by 5
    await page.click('#btnNewBoard');
    await expect(page.locator('#boardList .row')).toHaveCount(1);
    await expect(page.locator('#shapeList .row')).toHaveCount(0);
    await settled(page);
    await expect(page.locator('#boardList .row.sel .lbl')).toHaveText('Board 1');
    await page.click('#boardList .row.sel [data-a="del"]');
    await expect(page.locator('#boardList .row')).toHaveCount(0);
    await settled(page);
    // the Solver's pager read the gone problem's list once more (not_found, ignored there)
    const deleted = readCallLog(log).length;
    await page.click('#btnNewBoard');
    await expect(page.locator('#boardList .row')).toHaveCount(1);
    await settled(page);
    await expect(page.locator('#boardList .row.sel .lbl')).toHaveText('Board 1');
    // a board is drawn with the ordinary tools, which say what its cells are
    await expect(page.locator('#trayRow')).toHaveCount(0);
    await expect(page.locator('#toolStrip [data-t="erase"]')).toHaveAttribute('title', /^Wall/);
    // no constraint colours: the shape's own color, with only the pencil
    await expect(page.locator('#cEdit')).toBeVisible();
    await expect(page.locator('#cAdd')).toHaveCount(0);
    await expect(page.locator('#cRm')).toHaveCount(0);
    await expect(page.locator('#toolStrip [data-t="paint"]')).toHaveCount(0);
    // one layer to begin with; more may be added
    await expect(page.locator('#sizeSteppers [data-d="2"][data-s="-1"]')).toBeDisabled();
    await expect(page.locator('#sizeSteppers [data-d="2"][data-s="1"]')).toBeEnabled();
    for (const [d, n] of [
      [0, 2],
      [1, 1],
    ] as const) {
      for (let i = 0; i < n; i++) {
        await page.click(`#sizeSteppers [data-d="${String(d)}"][data-s="-1"]`);
        await settled(page);
      }
    }
    const tray = await idOf(page, 'B1');
    expect((await engineCall<Tray>(page, 'sliding.tray', { tray })).size).toEqual([4, 5, 1]);
    // the validity bar is the Puzzle workspace's
    await expect(page.locator('#slideValid')).toBeHidden();

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
      await expect(page.locator(SHAPE_ROWS)).toHaveCount(4 + i);
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
      await expect(page.locator(SHAPE_ROWS)).toHaveCount(9 + i);
      await settled(page);
      unit.push((await page.locator('#shapeList .row.sel').getAttribute('data-id')) ?? '');
    }
    // the Puzzle: Start and goal in the selected piece's place, the view's
    // two trays, nothing on them yet; not solvable yet
    await page.keyboard.press('Control+2');
    await pzSettled(page);
    await expect(page.locator('#pzRightTitle')).toHaveText('Start and goal');
    await expect(page.locator('#sgStartHint')).toContainText('Add pieces');
    await expect(page.locator('#vp')).toHaveAttribute(
      'data-tray',
      `-1:${tray}:solid:on -2:${tray}:solid:on`,
    );
    await expect(page.locator('#trayTags span')).toHaveText(['START', 'GOAL']);
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '0');
    await expect(page.locator('#slideValid')).toContainText('Invalid Start/Goal positions - ');
    // the starts, in the order the fixture's problem lists its pieces
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
    // every piece in the puzzle, one copy each, in that order
    for (const [p] of starts) {
      await page.click(`#pzLib .lc[data-u="${p}"] [data-add]`);
      await pzSettled(page);
    }
    await expect(page.locator('#sgStartParts .sgpart')).toHaveCount(10);
    for (const [p, at] of starts) {
      await stamp(page, p, at);
    }
    // a piece that does not fit is refused with the original's reason
    await page.click(`#sgStartParts [data-sp="${block}"]`);
    const refused = page.locator('#sgStartGrid svg');
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
    await boardClick(page, false, [3, 3]);
    await expect.poll(() => places().slice(placed)).toEqual(['invalid_argument']);
    await expect(page.locator('#stText')).toHaveText(why.why ?? '');
    // the outline follows the mouse: orange where the piece is
    await page.hover(`#sgStartGrid .cell[data-at="1,4,0"]`);
    await expect(refused).toHaveAttribute('data-stamp', /^rgb\(245,150,30\);/);
    await expect(page.locator(`#sgStartParts [data-sp="${block}"] .n`)).toHaveText('1/1');
    // the goal: still invalid until it is placed
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '0');
    await stamp(page, block, [1, 1], true);
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '1');
    await expect(page.locator('#slideValid')).toHaveText(
      'Valid Start/Goal positions for pieces in Start and Goal of this board',
    );

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
    // the view: both trays and the eleven copies (ten starts, one goal)
    await expect
      .poll(async () => (await attr(page, '#vp', 'data-tray')).split(' ').length)
      .toBe(13);

    // no new, copied or deleted problem, no Change…, no groups or rules; a
    // placed piece has no fewer copies than are placed, and may have more
    await expect(page.locator('#pzNew')).toBeHidden();
    await expect(page.locator('#pzProblems [data-a="dup"]')).toHaveCount(0);
    await expect(page.locator('#pzProblems [data-a="del"]')).toHaveCount(0);
    await expect(page.locator('#pzChange')).toHaveCount(0);
    await expect(page.locator('#pzRules')).toBeHidden();
    await expect(page.locator('#pzUsed [data-u] [data-d="-1"]').first()).toBeDisabled();
    await expect(page.locator('#pzUsed [data-u] [data-d="1"]').first()).toBeEnabled();

    // solved by the sliding solver: as bt counts and moves it; the problem
    // then is the fixture's (its pieces, the maximum of holes)
    await toSolver(page);
    expect(await shownProblem(page)).toBe(p);
    await page.click('#slvOptHead');
    await expect(page.locator('#slvOpts')).toBeVisible();
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await expect(page.locator('#slvMode')).toContainText('Find Solutions');
    await expect(page.locator('#slvOpts [data-o="keepMirrors"]')).toHaveCount(0);
    // its memory is the Search memory setting, not a box here
    await expect(page.locator('#slvOpts [data-o="highMemory"]')).toHaveCount(0);
    await expect(page.locator('#slvOpts [data-o="partialNested"]')).toHaveClass(/\boff\b/);
    // pieces stay in their layers unless Allow Vertical Moves is on
    await expect(page.locator('#slvOpts [data-o="verticalMoves"]')).toHaveAttribute(
      'aria-checked',
      'false',
    );
    // one solver, shown as stacking's is: nothing to choose, nothing stored
    const solvers = page.locator('#slvOpts select[data-o="slidingSolver"] option');
    await expect(solvers).toHaveCount(1);
    await expect(solvers).toHaveText('Sliding Puzzle Solver');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const stored = await engineCall<{ solverSettings: { solverType?: string } }>(
      page,
      'problem.get',
      { problem: p },
    );
    expect(stored.solverSettings.solverType).toBeUndefined();
    const bt = btJson(SOLVER) as {
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
      (c, i) =>
        c.errc !== null &&
        c.errc !== 'superseded' &&
        c.method !== 'sliding.place' &&
        c.method !== 'sliding.preview' &&
        !(i < deleted && c.method === 'solution.page' && c.errc === 'not_found'),
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
    // a sliding shape of two layers keeps them on open, and nothing is said
    const deep = join(dir, 'two-layers.xmpuzzle');
    writeFileSync(
      deep,
      '<?xml version="1.0"?>\n<puzzle version="2"><gridType type="5"/><colors/><shapes><voxel x="2" y="1" z="2" type="0">####</voxel></shapes><problems/></puzzle>\n',
    );
    await openPath(page, deep);
    await settled(page);
    await expect(page.locator('#notice')).toHaveCount(0);
    const opened = await engineCall<{ shapes: { size: number[] }[] }>(page, 'document.get', {});
    expect(opened.shapes[0]?.size).toEqual([2, 1, 2]);

    // a stamped piece whose shape changes loses its start: told in a message box
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const s3 = await idOf(page, 'S2');
    await selectShape(page, s3);
    await expect(page.locator('#cAdd')).toHaveCount(0);
    // the tasks that add layers are offered
    await expect(page.locator('#tmat [data-task="rotate6"]')).toHaveCount(1);
    await clickCell(page, [1, 4, 0]);
    await expect(page.locator('#notice .cb')).toContainText(
      'S2 changed shape: its start and goal were removed from B1.',
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

test('copies of a piece: a start and a goal for each, placed one by one', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  try {
    // a 4 by 1 board, a unit piece the puzzle has two of, another of one
    await newDocument(page, 'sliding');
    await page.click('#btnNewBoard');
    await settled(page);
    for (const [d, n] of [
      [0, 2],
      [1, 5],
    ] as const) {
      for (let i = 0; i < n; i++) {
        await page.click(`#sizeSteppers [data-d="${String(d)}"][data-s="-1"]`);
        await settled(page);
      }
    }
    const tray = await idOf(page, 'B1');
    expect((await engineCall<Tray>(page, 'sliding.tray', { tray })).size).toEqual([4, 1, 1]);
    const one = await piece(page, [[0, 0]]);
    const two = await piece(page, [[0, 0]]);
    await page.keyboard.press('Control+2');
    await pzSettled(page);
    await page.click('#pzAll');
    await pzSettled(page);
    await page.click(`#pzUsed [data-u="${two}"] [data-d="1"]`);
    await pzSettled(page);
    await expect(page.locator(`#sgStartParts [data-sp="${two}"] .n`)).toHaveText('0/2');

    // S1 at its start and goal; S2's two starts, a third refused
    await stamp(page, one, [0, 0]);
    await stamp(page, one, [0, 0], true);
    await stamp(page, two, [1, 0]);
    await expect(page.locator('#sgStartHint')).toContainText('2 copies');
    await boardClick(page, false, [2, 0]);
    await expect(page.locator(`#sgStartParts [data-sp="${two}"] .n`)).toHaveText('2/2');
    await boardClick(page, false, [3, 0]);
    await expect(page.locator('#stText')).toContainText('All 2 copies');
    // one copy fewer than placed is refused: − is off
    await expect(page.locator(`#pzUsed [data-u="${two}"] [data-d="-1"]`)).toBeDisabled();
    // a goal for one copy only: valid (S1 has its start and goal; the other copy ends anywhere)
    await stamp(page, two, [3, 0], true);
    await expect(page.locator('#slideValid')).toHaveAttribute('data-valid', '1');
    const t = await engineCall<{ placed: { piece: string; goal: boolean }[] }>(
      page,
      'sliding.tray',
      { tray },
    );
    expect(t.placed.filter((c) => c.piece === two && !c.goal)).toHaveLength(2);
    // the right click takes off the copy under it, not the other
    await boardClick(page, false, [2, 0], 'right');
    await expect(page.locator(`#sgStartParts [data-sp="${two}"] .n`)).toHaveText('1/2');
    // the 3D view: two trays and the four copies
    await expect.poll(async () => (await attr(page, '#vp', 'data-tray')).split(' ').length).toBe(6);
    // solved: S2's other copy starts on 2 or 3; any copy may end on the goal
    await toSolver(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    expect(Number(await attr(page, '#leftS', 'data-assemblies'))).toBe(2);
    expect(Number(await attr(page, '#leftS', 'data-solutions'))).toBeGreaterThan(0);
  } finally {
    await a.close();
  }
});

test('reference screenshots: a board drawn, its Start and goal, its two trays, the walls in the player', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  try {
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const tray = await idOf(page, 'B1');
    await selectShape(page, tray);
    await page.mouse.move(2, 2);
    await settle(page);
    await expect(page.locator('#garea')).toHaveScreenshot('sliding-board.png');
    await page.keyboard.press('Control+2');
    await pzSettled(page);
    await expect(page.locator('#vp')).toHaveAttribute('data-tray', /^(\S+ ){12}\S+$/);
    await page.click(`#sgStartParts [data-sp="${await idOf(page, 'S1')}"]`);
    await page.mouse.move(2, 2);
    await settle(page);
    await expect(page.locator('#rightP')).toHaveScreenshot('sliding-start-goal.png');
    await expect(page.locator('#vp')).toHaveScreenshot('sliding-trays.png');
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

/** Types a size into an axis's grid size field and waits until the engine has it. */
async function sizeTo(page: Page, shape: string, axis: number, n: number): Promise<void> {
  const field = page.locator(`#sizeSteppers input[data-v="${String(axis)}"]`);
  await field.fill(String(n));
  await field.press('Enter');
  await expect.poll(async () => (await shapeGet(page, shape)).shape.size[axis]).toBe(n);
  await settled(page);
}

test('Focus 2D editor: the tip on a grid of 20 or more; one focus for Shapes and Puzzle, kept across the tabs', async () => {
  test.setTimeout(120_000);
  const a = await launch();
  const page = a.page;
  const stage = page.locator('#stage');
  const tip = page.locator('#gridTip');
  const shapesBtn = page.locator('#btnF2D');
  const puzzleBtn = page.locator('#pzF2D');
  const pzTip = page.locator('#pzGridTip');
  const toTab = async (key: string, tab: string): Promise<void> => {
    await page.keyboard.press(`Control+${key}`);
    await expect(stage).toHaveAttribute('data-tab', tab);
    if (tab === 'puzzle') {
      await pzSettled(page);
    }
  };
  try {
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    const tray = await idOf(page, 'B1');
    await selectShape(page, tray);
    const [x, y] = (await shapeGet(page, tray)).shape.size;
    expect(x).toBeLessThan(20);
    expect(y).toBeLessThan(20);
    await expect(tip).toBeHidden();

    // 20 across: the tip; 19 by 19: none; 20 up: the tip again
    await sizeTo(page, tray, 0, 20);
    await expect(tip).toBeVisible();
    await expect(tip).toHaveText("Tip: Use 'Focus 2D editor' to increase the grid view size");
    await sizeTo(page, tray, 0, 19);
    await sizeTo(page, tray, 1, 19);
    await expect(tip).toBeHidden();
    await sizeTo(page, tray, 1, 20);
    await expect(tip).toBeVisible();

    // the focus on Shapes: the tip goes, the button shows pressed
    await shapesBtn.click();
    await expect(stage).toHaveClass(/\bf2d\b/);
    await expect(shapesBtn).toHaveClass(/\bact\b/);
    await expect(tip).toBeHidden();

    // the same focus on Puzzle: Start and Goal side by side
    await toTab('2', 'puzzle');
    await expect(stage).toHaveClass(/\bf2d\b/);
    await expect(puzzleBtn).toHaveClass(/\bact\b/);
    await expect(page.locator('#pzSlide')).toHaveCSS('display', 'grid');
    await expect(pzTip).toBeHidden();
    // the Solver has no 2D editor; back on Shapes the focus is still on
    await toSolver(page);
    await expect(stage).not.toHaveClass(/\bf2d\b/);
    await toTab('1', 'entities');
    await expect(stage).toHaveClass(/\bf2d\b/);
    await expect(shapesBtn).toHaveClass(/\bact\b/);

    // pressed again on Shapes: off on both tabs, the tip back
    await shapesBtn.click();
    await expect(stage).not.toHaveClass(/\bf2d\b/);
    await expect(shapesBtn).not.toHaveClass(/\bact\b/);
    await expect(tip).toBeVisible();
    await toTab('2', 'puzzle');
    await expect(stage).not.toHaveClass(/\bf2d\b/);
    await expect(puzzleBtn).not.toHaveClass(/\bact\b/);
    await expect(page.locator('#pzSlide')).toHaveCSS('flex-direction', 'column');
    await expect(pzTip).toBeVisible();
    await expect(pzTip).toHaveText(await tip.innerText());

    // turned on in Puzzle: on in Shapes too
    await puzzleBtn.click();
    await expect(stage).toHaveClass(/\bf2d\b/);
    await expect(puzzleBtn).toHaveClass(/\bact\b/);
    await toTab('1', 'entities');
    await expect(stage).toHaveClass(/\bf2d\b/);
    await expect(shapesBtn).toHaveClass(/\bact\b/);
    await expect(tip).toBeHidden();

    // turned off in Puzzle: off in Shapes too
    await toTab('2', 'puzzle');
    await puzzleBtn.click();
    await expect(stage).not.toHaveClass(/\bf2d\b/);
    await toTab('1', 'entities');
    await expect(stage).not.toHaveClass(/\bf2d\b/);
    await expect(shapesBtn).not.toHaveClass(/\bact\b/);
    await expect(tip).toBeVisible();

    // a board under 20 both ways: no tip on Puzzle either
    await sizeTo(page, tray, 1, 19);
    await toTab('2', 'puzzle');
    await expect(pzTip).toBeHidden();
  } finally {
    await a.close();
  }
});

test('the Boards list: above Shapes with + New Board, four and a half rows before it scrolls', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // other grids have no Boards list
    await newDocument(page, 'bricks');
    await expect(page.locator('#boardsSec')).toBeHidden();
    await newDocument(page, 'sliding');
    const sec = page.locator('#boardsSec');
    const boards = page.locator('#boardList');
    await expect(sec).toBeVisible();
    await expect(sec.locator('#btnNewBoard')).toBeVisible();
    await expect(page.locator('#boardCount')).toHaveText('0');
    // the boards in their list, the pieces in Shapes
    await piece(page, [[0, 0]]);
    const scrolls = (): Promise<boolean> =>
      boards.evaluate((el) => el.scrollHeight > el.clientHeight + 1);
    for (let i = 1; i <= 5; i++) {
      await page.click('#btnNewBoard');
      await expect(page.locator('#boardList .row')).toHaveCount(i);
      await settled(page);
      await expect(page.locator('#boardCount')).toHaveText(String(i));
      // four rows fit; the fifth shows half a row, and the list scrolls
      expect(await scrolls()).toBe(i === 5);
    }
    await expect(page.locator('#shapeList .row')).toHaveCount(1);
    await expect(page.locator('#shapeCount')).toHaveText('1');
    const rowH = await page
      .locator('#boardList .row')
      .first()
      .evaluate((el) => (el as HTMLElement).offsetHeight);
    const listH = await boards.evaluate((el) => el.clientHeight);
    expect(Math.abs(listH - (6 + 4.5 * rowH))).toBeLessThanOrEqual(1);
  } finally {
    await a.close();
  }
});
