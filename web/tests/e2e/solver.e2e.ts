// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace (FRONTEND-PLAN.md, stage 4's gate) in the built app
// with the release engine: Bermuda solved with take-apart on every thread,
// its counts those of `bt -q -n -d --json`, its solution played to the end,
// stepped back and forward and jumped, the poses at every whole step the
// engine's playback.poses samples; the list sorted every way as
// solution.page sorts it, paged to its end, its keys; every Remove variant
// with its confirmation; Analyse with the assemblies that do not come
// apart marked, and with progress and a stop that keeps what was done;
// pause, continue and abort on a box of dominoes, a save at a save point
// then carrying on (the autosave), Prepare then Continue, the error run of
// the contract suite shown with its failure; two problems solved at once;
// a sliding puzzle solved as bt solves it and played along its routes, a
// stacking one too (stacking.e2e.ts plays it in transfers). Each test's call log holds no refusal the
// UI could have avoided (only the client's own superseded reads).

import { existsSync, mkdtempSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  answerDialogs,
  BUILT,
  ENGINE,
  engineCall,
  FIXTURES,
  launch,
  newDocument,
  openFixture,
  readCallLog,
} from './app.ts';
import { pzSettled } from './puzzle.ts';
import {
  btJson,
  confirm,
  dominoBox,
  enginePoses,
  expectPosesAt,
  listCurrent,
  listRows,
  manage,
  pageIds,
  runButton,
  runEnded,
  shownProblem,
  solverSettled,
  toSolver,
} from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

function tmpLog(): { dir: string; log: string } {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  return { dir, log: join(dir, 'calls.log') };
}

/** No call failed but the client's own superseded reads. */
function expectNoRefusal(log: string): void {
  const bad = readCallLog(log).filter((c) => c.errc !== null && c.errc !== 'superseded');
  expect(bad).toEqual([]);
}

async function attr(page: Page, sel: string, name: string): Promise<string> {
  return (await page.getAttribute(sel, name)) ?? '';
}

test('Bermuda: solved with take-apart as bt counts it, played, stepped, jumped, posed as the engine', async () => {
  test.setTimeout(120_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'Bermuda.xmpuzzle');
    await toSolver(page);
    const p = await shownProblem(page);
    // what to check: assemblies and disassembly, stored with the problem
    await page.click('#slvMode [data-m="dis"]');
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await solverSettled(page);
    const stored = await engineCall<{ solverSettings: { disassemble?: boolean } }>(
      page,
      'problem.get',
      { problem: p },
    );
    expect(stored.solverSettings.disassemble).toBe(true);
    // every thread: the engine's setting (0)
    const settings = await engineCall<{ threads: number }>(page, 'settings.get');
    expect(settings.threads).toBe(0);

    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const bt = btJson(resolve(FIXTURES, 'Bermuda.xmpuzzle'));
    expect(Number(await attr(page, '#leftS', 'data-assemblies'))).toBe(bt.assemblies);
    expect(Number(await attr(page, '#leftS', 'data-solutions'))).toBe(bt.solutions);
    await expect(page.locator('#slvAsm')).toHaveText(String(bt.assemblies));
    await expect(page.locator('#slvSols')).toHaveText(String(bt.solutions));
    await expect(page.locator('#slvPct')).toHaveText('100.0%');
    await expect(page.locator('#slvActivity')).toHaveText('Search finished');
    await expect(page.locator('#slvDetails')).toHaveText(
      /^Scanned .+ search steps\. Tried to take apart \d+ assemblies\.$/,
    );
    await expect(page.locator('#slvRun [data-r="abort"]')).toBeDisabled();
    const detail = await engineCall<{ assemblies: number; solutions: number; saved: number }>(
      page,
      'problem.get',
      { problem: p },
    );
    expect(detail).toMatchObject({ assemblies: bt.assemblies, solutions: bt.solutions });
    await expect(page.locator('#slvCount')).toHaveText(String(detail.saved));

    // the first solution, assembled, then its playback
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await expect(page.locator('#vp')).toHaveAttribute('data-solution-view', 'playback');
    const sol = await attr(page, '#splayer', 'data-solution');
    const engine = await enginePoses(page, p, sol);
    expect(Number(await attr(page, '#splayer', 'data-steps'))).toBe(engine.steps);
    expect(engine.steps).toBe(11);
    await expectPosesAt(page, engine, 0);

    // played to the end
    await page.click('#splayer [data-t="play"]');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '1');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '0', {
      timeout: 30_000,
    });
    await expectPosesAt(page, engine, engine.steps);

    // the keys on the list: one move back and forward, the jumps
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('ArrowLeft');
    await expectPosesAt(page, engine, engine.steps - 1);
    await page.keyboard.press('ArrowRight');
    await expectPosesAt(page, engine, engine.steps);
    await page.keyboard.press('Shift+ArrowLeft');
    await expectPosesAt(page, engine, 0);
    // every whole step forward
    for (let s = 1; s <= engine.steps; s++) {
      await page.keyboard.press('ArrowRight');
      await expectPosesAt(page, engine, s);
    }
    await page.keyboard.press('Shift+ArrowLeft');
    await page.keyboard.press('Shift+ArrowRight');
    await expectPosesAt(page, engine, engine.steps);
    // Space plays from the start (it was at the end), Esc pauses
    await page.keyboard.press(' ');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '1');
    await page.keyboard.press('Escape');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '0');
    // the seek bar: Home and End are its start and end
    await page.focus('#slvMove');
    await page.keyboard.press('Home');
    await expectPosesAt(page, engine, 0);
    await page.keyboard.press('End');
    await expectPosesAt(page, engine, engine.steps);
    // the buttons
    await page.click('#splayer [data-t="start"]');
    await expectPosesAt(page, engine, 0);
    await page.click('#splayer [data-t="fwd"]');
    await expectPosesAt(page, engine, 1);
    await page.click('#splayer [data-t="end"]');
    await expectPosesAt(page, engine, engine.steps);
    await page.click('#splayer [data-t="back"]');
    await expectPosesAt(page, engine, engine.steps - 1);

    // the piece chips: solid → wire frame → hidden, Show all
    const chip = page.locator('#slvPcs .pvc').first();
    const piece = await chip.getAttribute('data-p');
    await chip.click();
    await expect(page.locator(`#slvPcs .pvc[data-p="${piece ?? ''}"]`)).toHaveAttribute(
      'data-mode',
      'wire',
    );
    await expect(page.locator('#vp')).toHaveAttribute(
      'data-solution-pieces',
      new RegExp(`(^| )${piece ?? ''}:s\\d+:wire:`),
    );
    await page.locator(`#slvPcs .pvc[data-p="${piece ?? ''}"]`).click();
    await expect(page.locator('#vp')).toHaveAttribute(
      'data-solution-pieces',
      new RegExp(`(^| )${piece ?? ''}:s\\d+:hidden:off`),
    );
    await page.click('#slvShowAll');
    await expect(page.locator('#slvPcs .pvc[data-mode="solid"]')).toHaveCount(
      await page.locator('#slvPcs .pvc').count(),
    );
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the list: sorted every way as solution.page, paged, its keys; every Remove variant asks', async () => {
  test.setTimeout(120_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'SolidSixPieceBurrs.xmpuzzle');
    await toSolver(page);
    const p = await shownProblem(page);
    const total = (await pageIds(page, p)).length;
    expect(total).toBeGreaterThan(250);
    await expect(page.locator('#slvCount')).toHaveText(String(total));

    const checkSort = async (sortBy: string, descending: boolean): Promise<void> => {
      await expect(page.locator('#slvSel')).toHaveAttribute(
        'data-sort',
        `${sortBy}:${descending ? 'desc' : 'asc'}`,
      );
      await listCurrent(page);
      const want = await pageIds(page, p, sortBy, descending);
      const rows = await listRows(page);
      expect(rows.length).toBeGreaterThan(5);
      for (const r of rows) {
        expect(r.id, `${sortBy} row ${String(r.i)}`).toBe(want[r.i]);
      }
    };
    // the saved order, then each header: its first way, the other, the saved order again
    await checkSort('list', false);
    for (const [k, first] of [
      ['assembly', false],
      ['level', true],
      ['moves', true],
      ['pieces', true],
    ] as const) {
      await page.click(`#slvHead [data-sort="${k}"]`);
      await checkSort(k, first);
      await page.click(`#slvHead [data-sort="${k}"]`);
      await checkSort(k, !first);
      await page.click(`#slvHead [data-sort="${k}"]`);
      await checkSort('list', false);
    }

    // the keys on the list: End pages to the end, PgUp 8 back, Home the first
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('End');
    await expect(page.locator(`#slvList .lrow.sel[data-i="${String(total - 1)}"]`)).toHaveCount(1);
    await listCurrent(page);
    const ids = await pageIds(page, p);
    for (const r of await listRows(page)) {
      expect(r.id).toBe(ids[r.i]);
    }
    await page.keyboard.press('PageUp');
    await expect(page.locator(`#slvList .lrow.sel[data-i="${String(total - 9)}"]`)).toHaveCount(1);
    await page.keyboard.press('ArrowUp');
    await expect(page.locator(`#slvList .lrow.sel[data-i="${String(total - 10)}"]`)).toHaveCount(1);
    await page.keyboard.press('Home');
    await expect(page.locator('#slvList .lrow.sel[data-i="0"]')).toHaveCount(1);
    // the slider view shows the same solution
    await page.click('#slvViewSeg [data-v="slider"]');
    await expect(page.locator('#slvNavT')).toHaveText(
      new RegExp(`assemblies —1 of ${String(total)}$`, 'i'),
    );
    await page.click('#slvSel .nb[data-n="1"]');
    await expect(page.locator('#slvNavT')).toHaveText(
      new RegExp(`assemblies —2 of ${String(total)}$`, 'i'),
    );
    await expect(page.locator('#splayer')).toHaveAttribute('data-solution', ids[1] ?? '');
    await page.click('#slvViewSeg [data-v="list"]');

    // Del on the list removes the selected solution, asking first; Cancel keeps it
    await page.click('#slvList .lrow[data-i="5"]');
    await page.keyboard.press('Delete');
    await confirm(page, /^Delete 1 solution\?$/, false);
    expect(await pageIds(page, p)).toHaveLength(total);
    await page.keyboard.press('Delete');
    await confirm(page, /^Delete 1 solution\?$/, true);
    await solverSettled(page);
    let now = await pageIds(page, p);
    expect(now).toHaveLength(total - 1);
    expect(now).not.toContain(ids[5]);
    await expect(page.locator('#slvCount')).toHaveText(String(total - 1));
    // undoable
    await page.keyboard.press('Control+z');
    await expect(page.locator('#slvCount')).toHaveText(String(total));
    await solverSettled(page);

    // this one only, all before, all after, all: each asks first
    await page.click('#slvList .lrow[data-i="3"]');
    await manage(page, 'at');
    await confirm(page, /^Delete 1 solution\?$/, true);
    await solverSettled(page);
    now = await pageIds(page, p);
    expect(now).toEqual(ids.filter((_, i) => i !== 3));
    await page.click('#slvList .lrow[data-i="3"]');
    await manage(page, 'before');
    await confirm(page, /^Delete 3 solutions\?$/, true);
    await solverSettled(page);
    now = await pageIds(page, p);
    expect(now).toEqual(ids.slice(4));
    const left = now.length;
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('PageDown');
    await page.keyboard.press('PageDown');
    await expect(page.locator('#slvList .lrow.sel[data-i="16"]')).toHaveCount(1);
    await manage(page, 'after');
    await confirm(page, new RegExp(`^Delete ${String(left - 17)} solutions\\?$`), true);
    await solverSettled(page);
    now = await pageIds(page, p);
    expect(now).toEqual(ids.slice(4, 21));
    await manage(page, 'all');
    await confirm(page, /^Delete 17 solutions\?$/, false);
    expect(await pageIds(page, p)).toHaveLength(17);
    await manage(page, 'all');
    await confirm(page, /^Delete 17 solutions\?$/, true);
    await solverSettled(page);
    expect(await pageIds(page, p)).toEqual([]);
    await expect(page.locator('#slvCount')).toHaveText('0');
    await expect(page.locator('#splayer')).toHaveAttribute('data-solution', '');
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('Analyse: what does not come apart is marked, the rest removed; a stop keeps what was done', async () => {
  test.setTimeout(180_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    // PelikanBurr solved without take-apart: 12 assemblies, one comes apart
    await openFixture(page, 'PelikanBurr.xmpuzzle');
    await toSolver(page);
    const p = await shownProblem(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const bt = btJson(resolve(FIXTURES, 'PelikanBurr.xmpuzzle'), []);
    await expect(page.locator('#slvCount')).toHaveText('12');
    await expect(page.locator('#slvList .lrow .c5[title="Disassembly not checked"]')).toHaveCount(
      12,
    );
    await expect(page.locator('#splayer .msg')).toContainText('Disassembly was not checked');
    await manage(page, 'ada');
    await expect(page.locator('#slvList .lrow .c5.ok')).toHaveCount(bt.solutions, {
      timeout: 60_000,
    });
    await solverSettled(page);
    await expect(page.locator('#slvList .lrow .c5.bad')).toHaveCount(12 - bt.solutions);
    await expect(page.locator('#slvList .lrow .c5.bad').first()).toHaveAttribute(
      'title',
      'Cannot be disassembled',
    );
    const bad = page.locator('#slvList .lrow:has(.c5.bad)').first();
    await bad.click();
    await expect(page.locator('#splayer .msg')).toContainText('cannot be taken apart');
    await manage(page, 'nda');
    await confirm(page, /^Delete the solutions without a disassembly\?$/, true);
    await solverSettled(page);
    const rows = await engineCall<{ rows: { disassembly: string }[] }>(page, 'solution.page', {
      problem: p,
    });
    expect(rows.rows.map((r) => r.disassembly)).toEqual(['tree']);
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    // its moves dropped, the level kept; analysed again, they come back
    await page.click('#slvList .lrow[data-i="0"]');
    await manage(page, 'ddc');
    await expect(page.locator('#slvList .lrow[data-i="0"]')).toHaveAttribute('data-da', 'summary');
    await expect(page.locator('#splayer .msg')).toContainText('Only the level was kept');
    await solverSettled(page);
    await manage(page, 'adc');
    await expect(page.locator('#slvList .lrow[data-i="0"]')).toHaveAttribute('data-da', 'tree');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await solverSettled(page);
    // Enter plays it from the list
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('Enter');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '1');
    await page.keyboard.press('Enter');
    await expect(page.locator('#splayer')).toHaveAttribute('data-playing', '0');

    // a box of dominoes kept whole (Limit 0, 83442 assemblies) without take-apart, analysed and stopped
    await newDocument(page, 'bricks');
    await engineCall(page, 'settings.set', { threads: 1 });
    const box = await dominoBox(page, 2, 4, 5);
    await toSolver(page);
    expect(await shownProblem(page)).toBe(box.problem);
    await page.click('#slvOptHead');
    await page.fill('#slvOpts input[data-sti="keepAtMost"]', '0');
    await page.locator('#slvOpts input[data-sti="keepAtMost"]').press('Tab');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const total = (
      await engineCall<{ saved: number }>(page, 'problem.get', { problem: box.problem })
    ).saved;
    expect(total).toBe(83442);
    await page.click('#slvList .lrow[data-i="0"]');
    await manage(page, 'ada');
    await expect(page.locator('#slvAna')).toHaveAttribute('data-active', '1');
    await expect
      .poll(async () => Number(((await page.textContent('#slvAnaN')) ?? '0').split(' ')[0]))
      .toBeGreaterThan(0);
    await page.click('#slvAna [data-a="stopAnalysis"]');
    await expect(page.locator('#slvAna')).toHaveAttribute('data-active', '0', { timeout: 30_000 });
    await solverSettled(page);
    const after = await engineCall<{ rows: { disassembly: string }[] }>(page, 'solution.page', {
      problem: box.problem,
      limit: 10000,
    });
    const done = after.rows.filter((r) => r.disassembly === 'tree').length;
    expect(done).toBeGreaterThan(0);
    expect(done).toBeLessThan(total);
    // what was taken apart comes first (the stored order) and is kept
    expect(after.rows.slice(0, done).every((r) => r.disassembly === 'tree')).toBe(true);
    await expect(page.locator('#slvList .lrow[data-i="0"] .c5')).toHaveClass(/ok/);
    await page.click('#slvList .lrow[data-i="0"]');
    await page.keyboard.press('End');
    await expect(page.locator(`#slvList .lrow[data-i="${String(total - 1)}"] .c5`)).toHaveAttribute(
      'title',
      'Disassembly not checked',
    );
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a box of dominoes: pause, continue, a save at a save point, abort; Prepare then Continue; an error shown', async () => {
  test.setTimeout(180_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log, autosave: 2 });
  const page = a.page;
  const file = join(dir, 'box.xmpuzzle');
  try {
    await newDocument(page, 'bricks');
    await engineCall(page, 'settings.set', { threads: 1 });
    const box = await dominoBox(page, 2, 4, 5);
    // the puzzle gets a file (its save dialog's, then through the bridge: the
    // test of File ▸ Save is files.e2e.ts)
    await answerDialogs(page, { save: file });
    expect(await page.evaluate(() => window.btApp.chooseSave('puzzle', 'box'))).toBe(file);
    await engineCall(page, 'document.save', { path: file });
    const saved0 = statSync(file).mtimeMs;
    await toSolver(page);
    // with take-apart: a long solve on one thread; the store sees the path at this change
    await page.click('#slvMode [data-m="dis"]');
    await solverSettled(page);
    await expect(page.locator('#fname')).toHaveText(/box\.xmpuzzle/);

    await runButton(page, 'start');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');
    await expect
      .poll(async () => Number(await attr(page, '#leftS', 'data-assemblies')))
      .toBeGreaterThan(0);
    await expect(page.locator('#slvRun [data-r="abort"]')).toBeEnabled();
    await runButton(page, 'pause');
    await runEnded(page, 'paused');
    const pausedAt = Number(await attr(page, '#leftS', 'data-assemblies'));
    expect(pausedAt).toBeGreaterThan(0);
    await expect(page.locator('#slvActivity')).toHaveText('Search paused');
    await expect(page.locator('#slvDetails')).toContainText(/^Scanned .+ search steps\./);
    await expect(page.locator('#slvRun [data-r="continue"]')).toBeEnabled();
    await expect(page.locator('#slvRun [data-r="abort"]')).toBeEnabled();
    await expect(page.locator('#slvRun [data-r="pause"]')).toBeDisabled();

    // carried on: the autosave pauses at a save point, saves the file, carries on
    await runButton(page, 'continue');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');
    await expect(page.locator('#leftS')).toHaveAttribute('data-autosaved', '1', {
      timeout: 30_000,
    });
    expect(statSync(file).mtimeMs).toBeGreaterThan(saved0);
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling', {
      timeout: 15_000,
    });
    await expect
      .poll(async () => Number(await attr(page, '#leftS', 'data-assemblies')))
      .toBeGreaterThan(pausedAt);
    expect(existsSync(file)).toBe(true);

    // aborted: asked first, the results thrown away, Abort greyed once over
    await runButton(page, 'abort');
    await confirm(page, /^Clear the solve\?$/, false);
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');
    await runButton(page, 'abort');
    await confirm(page, /^Clear the solve\?$/, true);
    await runEnded(page, 'idle');
    await expect(page.locator('#slvRun [data-r="abort"]')).toBeDisabled();
    await expect(page.locator('#slvCount')).toHaveText('0');
    const gone = await engineCall<{ saved: number; state: string }>(page, 'problem.get', {
      problem: box.problem,
    });
    expect(gone).toMatchObject({ saved: 0, state: 'unsolved' });

    // Prepare then Continue, on a smaller box
    const small = await dominoBox(page, 2, 4, 3);
    await page.click(`#slvProbs .row[data-id="${small.problem}"]`);
    await solverSettled(page);
    expect(await shownProblem(page)).toBe(small.problem);
    await runButton(page, 'prepare');
    await runEnded(page, 'paused');
    await expect(page.locator('#slvActivity')).not.toHaveText('');
    await runButton(page, 'continue');
    await runEnded(page, 'finished');
    expect(Number(await attr(page, '#leftS', 'data-assemblies'))).toBeGreaterThan(0);
    await expect(page.locator('#slvRun [data-r="abort"]')).toBeDisabled();

    // the error run of the contract suite: three unit cubes cannot fill a 2x2x2 box
    const big = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 2, 2] }))
      .shape;
    await engineCall(page, 'shape.setBox', {
      shape: big,
      tool: 'fixed',
      from: [0, 0, 0],
      to: [1, 1, 1],
    });
    const unit = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [1, 1, 1] }))
      .shape;
    await engineCall(page, 'shape.setCells', { shape: unit, tool: 'fixed', cells: [[0, 0, 0]] });
    const q = (await engineCall<{ problem: string }>(page, 'problem.create')).problem;
    await engineCall(page, 'problem.setResult', { problem: q, shape: big });
    await engineCall(page, 'problem.setPieceCount', { problem: q, shape: unit, min: 3, max: 3 });
    await page.click(`#slvProbs .row[data-id="${q}"]`);
    await solverSettled(page);
    await runButton(page, 'start');
    await runEnded(page, 'error');
    const status = await engineCall<{ failure: { message: string } }>(page, 'solve.status', {
      problem: q,
    });
    await expect(page.locator('#slvFail')).toContainText(status.failure.message);
    await expect(page.locator('#slvActivity')).toHaveText('Search failed');
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('two problems solved at once; sliding and stacking solved', async () => {
  test.setTimeout(180_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await newDocument(page, 'bricks');
    await engineCall(page, 'settings.set', { threads: 1 });
    const one = await dominoBox(page, 2, 4, 5);
    const two = await dominoBox(page, 2, 4, 5);
    await toSolver(page);
    expect(await shownProblem(page)).toBe(one.problem);
    // the first with take-apart
    await page.click('#slvMode [data-m="dis"]');
    await solverSettled(page);
    await runButton(page, 'start');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');

    // the second meanwhile
    await expect(page.locator(`#slvProbs .row[data-id="${one.problem}"]`)).toContainText('solving');
    await page.click(`#slvProbs .row[data-id="${two.problem}"]`);
    await expect(page.locator('#leftS')).toHaveAttribute('data-problem', two.problem);
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'idle');
    await page.click('#slvMode [data-m="dis"]');
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
    await runButton(page, 'start');
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');
    const s1 = await engineCall<{ state: string }>(page, 'solve.status', { problem: one.problem });
    expect(s1.state).toBe('assembling');
    await runButton(page, 'pause');
    await runEnded(page, 'paused');
    // the first still runs
    await page.click(`#slvProbs .row[data-id="${one.problem}"]`);
    await expect(page.locator('#leftS')).toHaveAttribute('data-state', 'assembling');
    await runButton(page, 'pause');
    await runEnded(page, 'paused');

    // aborted, the lists can change again
    await toSolver(page);
    await runButton(page, 'abort');
    await confirm(page, /^Clear the solve\?$/, true);
    await runEnded(page, 'idle');
    await page.keyboard.press('Control+2');
    const before = (await engineCall<{ problems: unknown[] }>(page, 'document.get')).problems
      .length;
    await page.click('#pzNew');
    await expect
      .poll(
        async () =>
          (await engineCall<{ problems: unknown[] }>(page, 'document.get')).problems.length,
      )
      .toBe(before + 1);
    // the Puzzle shows the new problem (its reads done) before another document comes
    await pzSettled(page);
    await expect(page.locator('#leftP')).toHaveAttribute('data-problem', /^p\d+$/);

    // a sliding puzzle solves as bt solves it (the Sliding Puzzle Solver),
    // and its solution plays along its routes
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    await toSolver(page);
    const sp = await shownProblem(page);
    await page.click('#slvMode [data-m="dis"]');
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await solverSettled(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const slid = btJson(resolve(FIXTURES, 'test_sliding_solver.xmpuzzle'));
    expect(Number(await attr(page, '#leftS', 'data-assemblies'))).toBe(slid.assemblies);
    expect(Number(await attr(page, '#leftS', 'data-solutions'))).toBe(slid.solutions);
    await expect(page.locator('#slvSols')).toHaveText(String(slid.solutions));
    await expect(page.locator('#slvDetails')).toContainText(/^Scanned .+ arrangements\./);
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    await expect(page.locator('#vp')).toHaveAttribute('data-solution-view', 'playback');
    const slide = await enginePoses(page, sp, await attr(page, '#splayer', 'data-solution'));
    expect(slide.steps).toBe(81);
    expect(Number(await attr(page, '#splayer', 'data-steps'))).toBe(81);
    await expectPosesAt(page, slide, 0);
    await page.click('#splayer [data-t="end"]');
    await expectPosesAt(page, slide, 81);
    await expect(page.locator('#emptyMsg')).toHaveText('');
    // a stacking puzzle solves as bt solves it (the Stacking Solver, its board's)
    await openFixture(page, 'test_stacking_hanoi.xmpuzzle');
    await toSolver(page);
    await expect(page.locator('#slvRun [data-r="prepare"]')).toHaveCount(0);
    await expect(page.locator('#emptyMsg')).toHaveText('Run the solver to see solutions here');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const stacked = btJson(resolve(FIXTURES, 'test_stacking_hanoi.xmpuzzle'), [
      '--solver',
      'stacking',
      '--panex-dir',
      '',
    ]);
    expect(Number(await attr(page, '#leftS', 'data-solutions'))).toBe(stacked.solutions);
    await expect(page.locator('#splayer')).toHaveAttribute('data-steps', '7');
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
