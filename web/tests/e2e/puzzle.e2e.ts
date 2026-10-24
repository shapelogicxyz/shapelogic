// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace (FRONTEND-PLAN.md, stage 3's gate): the problem of
// CubeInCage and of a piece-range fixture (DemoPieceGenerator) built from
// an empty problem through the UI's actions (Change…, double-click, drags
// in and out and between tiles, the tiles' and the library's + and −, the
// menus, the keys, the table's fields, the selected piece's steppers,
// switch and groups, the More menu and the bulk range), equal to the
// fixture's problem.get; after every action the chips and the table agree
// with problem.get, the fit summary and the status bar with problem.fit,
// and the scene has one mesh per copy with the optional ones faded. The
// call log holds no refusal (only the client's own superseded reads).

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  BUILT,
  camera,
  ENGINE,
  engineCall,
  launch,
  newDocument,
  openFixture,
  readCallLog,
  settle,
  style,
} from './app.ts';
import type { LoggedCall } from './app.ts';
import { checkShown, currentProblem, definition, problemGet, pzSettled } from './puzzle.ts';
import type { Problem } from './puzzle.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** The calls the engine refused (the client's own superseded reads are none). */
function refusals(calls: LoggedCall[]): LoggedCall[] {
  return calls.filter((c) => c.errc !== null && c.errc !== 'superseded');
}

async function toPuzzle(page: Page): Promise<string> {
  await page.keyboard.press('Control+2');
  await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'puzzle');
  await pzSettled(page);
  return currentProblem(page);
}

async function newProblem(page: Page): Promise<string> {
  const before = await currentProblem(page);
  await page.click('#pzNew');
  await expect(page.locator('#leftP')).not.toHaveAttribute('data-problem', before);
  const p = await checkShown(page);
  expect(p.parts).toEqual([]);
  expect(p.result).toBeNull();
  return p.id;
}

const lib = (s: string): string => `#pzLib .lc[data-u="${s}"]`;
const tile = (s: string): string => `#pzUsed .ut[data-u="${s}"]`;

async function menu(page: Page, target: string, key: string): Promise<void> {
  await page.click(target, { button: 'right' });
  await page.click(`#pop [data-k="${key}"]`);
}

async function select(page: Page, target: string): Promise<void> {
  await page.click(`${target} b`);
  const u = /data-u="([^"]+)"/.exec(target)?.[1] ?? '';
  await expect(page.locator('#pzSel')).toHaveAttribute('data-shape', u);
}

async function fillAndLeave(page: Page, sel: string, value: string): Promise<void> {
  await page.fill(sel, value);
  await page.press(sel, 'Tab');
}

test('CubeInCage’s problem built from an empty problem through the UI', async () => {
  test.setTimeout(240_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'CubeInCage.xmpuzzle');
    const fixture = await toPuzzle(page);
    const want = await problemGet(page, fixture);
    await checkShown(page);

    const id = await newProblem(page);
    // the result by Change…
    await page.click('#pzChange');
    await page.click('#pop [data-k="s1"]');
    expect((await checkShown(page)).result).toBe('s1');
    await expect(page.locator('#pzResult')).toContainText('99 voxels · can hold 60–99');

    // S2 by a double-click in the library
    await page.dblclick(`${lib('s2')} b`);
    await checkShown(page);
    // S3 dragged in, then one more by its tile's +
    await page.dragAndDrop(`${lib('s3')} b`, '#pzUsed');
    await checkShown(page);
    await page.click(`${tile('s3')} .t2 [data-d="1"]`);
    await checkShown(page);
    // S4 by the library's +
    await page.click(`${lib('s4')} .la`);
    await checkShown(page);
    // S6 by the menu, S5 by the key with the chip selected
    await menu(page, lib('s6'), 'up');
    await checkShown(page);
    await select(page, lib('s5'));
    await page.keyboard.press('+');
    let p = await checkShown(page);
    expect(p.parts.map((x) => x.shape)).toEqual(['s2', 's3', 's4', 's6', 's5']);
    // the order: S5 moved earlier by its menu; S6 dragged onto S2 and back to the end
    await menu(page, tile('s5'), 'earlier');
    p = await checkShown(page);
    expect(p.parts.map((x) => x.shape)).toEqual(['s2', 's3', 's4', 's5', 's6']);
    await page.dragAndDrop(`${tile('s6')} .t1`, `${tile('s2')} .t1`);
    p = await checkShown(page);
    expect(p.parts.map((x) => x.shape)).toEqual(['s6', 's2', 's3', 's4', 's5']);
    expect((await engineCall<{ undo: string[] }>(page, 'edit.history')).undo.at(-1)).toBe(
      'Move piece',
    );
    await page.dragAndDrop(`${tile('s6')} .t1`, `${tile('s5')} .t1`);
    p = await checkShown(page);
    expect(p.parts.map((x) => x.shape)).toEqual(['s2', 's3', 's4', 's5', 's6']);

    // the keys on S4: one more, one fewer; Del on S6, then S6 back
    await select(page, tile('s4'));
    await page.keyboard.press('+');
    expect((await checkShown(page)).parts[2]).toMatchObject({ shape: 's4', min: 2, max: 2 });
    await page.keyboard.press('-');
    await checkShown(page);
    await select(page, tile('s6'));
    await page.keyboard.press('Delete');
    await expect(page.locator('#stText')).toHaveText('Removed from the puzzle');
    expect((await checkShown(page)).parts.map((x) => x.shape)).not.toContain('s6');
    await page.dblclick(`${lib('s6')} b`);
    await checkShown(page);
    // one fewer by the tile's −, back by the menu's one more
    await page.click(`${tile('s3')} .t2 [data-d="-1"]`);
    await checkShown(page);
    await menu(page, tile('s3'), 'up');
    await checkShown(page);

    // the groups: S2 in a new group, S3 in it; a copy of S3 out and back; the group deleted and made again
    await select(page, tile('s2'));
    await page.click('#pzSel [data-grp="new"]');
    await checkShown(page);
    await select(page, tile('s3'));
    await page.click('#pzSel [data-grp="1"]');
    p = await checkShown(page);
    expect(p.parts[1]?.groups).toEqual([{ group: 1, count: 2 }]);
    await page.click('#pzSel [data-gc="-1"]');
    p = await checkShown(page);
    expect(p.parts[1]?.groups).toEqual([{ group: 1, count: 1 }]);
    await expect(page.locator('#pzGroups .pzg[data-g="1"] .ptile[data-u="s3"] .pb')).toHaveText(
      '×1',
    );
    await page.click('#pzSel [data-gc="1"]');
    await checkShown(page);
    await page.click('#pzGroups [data-delg="1"]');
    p = await checkShown(page);
    expect(p.parts.every((x) => x.groups.every((g) => g.count === 0))).toBe(true);
    await expect(page.locator('#pzGroupsSec')).toBeHidden();
    await select(page, tile('s2'));
    await page.click('#pzSel [data-grp="new"]');
    await checkShown(page);
    await select(page, tile('s3'));
    await page.click('#pzSel [data-grp="1"]');
    await checkShown(page);

    // the table: S4's count typed, S5 as a range and back, S4 in a new group and out
    await page.click('#pzGrpHead');
    await expect(page.locator('#pzGrpBody')).toBeVisible();
    await fillAndLeave(page, '#pzGrpTable tr[data-u="s4"] [data-f=max]', '3');
    await checkShown(page);
    await fillAndLeave(page, '#pzGrpTable tr[data-u="s4"] [data-f=max]', '1');
    await checkShown(page);
    await page.check('#pzGrpTable tr[data-u="s5"] [data-range]');
    await expect(page.locator('#pzGrpTable tr[data-u="s5"] [data-f=min]')).toBeVisible();
    await fillAndLeave(page, '#pzGrpTable tr[data-u="s5"] [data-f=min]', '0');
    p = await checkShown(page);
    expect(p.parts[3]).toMatchObject({ shape: 's5', min: 0, max: 1 });
    await page.uncheck('#pzGrpTable tr[data-u="s5"] [data-range]');
    p = await checkShown(page);
    expect(p.parts[3]).toMatchObject({ shape: 's5', min: 1, max: 1 });
    await page.selectOption('#pzGrpTable tr[data-u="s4"] [data-grp]', 'new');
    p = await checkShown(page);
    expect(p.parts[2]?.groups).toEqual([{ group: 2, count: 1 }]);
    await page.selectOption('#pzGrpTable tr[data-u="s4"] [data-grp]', '');
    await checkShown(page);
    // S4's weight up and back: the tile shows it while it is not 1
    const weight = async (): Promise<number | undefined> =>
      (
        await engineCall<{ shapes: { id: string; weight: number }[] }>(page, 'shape.list')
      ).shapes.find((s) => s.id === 's4')?.weight;
    await page.click('#pzGrpTable tr[data-u="s4"] [data-w="1"]');
    await expect.poll(weight).toBe(2);
    await expect(page.locator('#pzGrpTable tr[data-u="s4"] td.wt .v')).toHaveText('2');
    await expect(page.locator('#pzUsed .ut[data-u="s4"] .t3')).toHaveText('Weight: 2');
    await page.click('#pzGrpTable tr[data-u="s4"] [data-w="-1"]');
    await expect.poll(weight).toBe(1);
    await expect(page.locator('#pzUsed .ut[data-u="s4"] .t3')).toHaveCount(0);
    await checkShown(page);
    await page.click('#pzGrpHead');
    await expect(page.locator('#pzGrpBody')).toBeHidden();

    // the scene: a click on a piece selects it; an orbit turns each piece in its place
    const boxes = async (): Promise<string[]> =>
      page.$$eval('#pzScene .pzbox', (els) =>
        els.map((e) => {
          const r = e.getBoundingClientRect();
          return `${(e as HTMLElement).dataset.role ?? ''}:${(e as HTMLElement).dataset.shape ?? ''}:${String(Math.round(r.x))},${String(Math.round(r.y))}`;
        }),
      );
    const s4 = await page.locator('#pzScene .pzbox.piece[data-shape="s4"]').boundingBox();
    expect(s4).not.toBeNull();
    await page.mouse.click(
      (s4?.x ?? 0) + (s4?.width ?? 0) / 2,
      (s4?.y ?? 0) + (s4?.height ?? 0) / 2,
    );
    await expect(page.locator('#pzSel')).toHaveAttribute('data-shape', 's4');
    await expect(page.locator('#pzScene .pzbox.selected[data-shape="s4"]')).toHaveCount(1);
    await expect(page.locator('#pzScene .pzbox.piece.sel[data-shape="s4"]')).toHaveCount(1);
    await checkShown(page);
    await settle(page);
    const before = await boxes();
    const cam = await camera(page);
    const vp = await page.locator('#cv').boundingBox();
    const x0 = (vp?.x ?? 0) + (vp?.width ?? 0) - 60;
    const y0 = (vp?.y ?? 0) + (vp?.height ?? 0) - 60;
    await page.mouse.move(x0, y0);
    await page.mouse.down();
    await page.mouse.move(x0 - 80, y0 - 30, { steps: 8 });
    await page.mouse.up();
    await settle(page);
    expect((await camera(page))[0]).not.toBeCloseTo(cam[0] ?? 0, 1);
    expect(await boxes()).toEqual(before);
    // another style: the same items drawn again
    await style(page, 'flat');
    await expect(page.locator('#vp')).toHaveAttribute('data-scene-style', 'flat');
    await checkShown(page);
    await style(page, 'drawing');
    await expect(page.locator('#vp')).toHaveAttribute('data-scene-style', 'drawing');

    // equal to the fixture's problem
    const built = await problemGet(page, id);
    expect(definition(built)).toEqual(definition(want));
    const calls = readCallLog(log);
    expect(refusals(calls)).toEqual([]);
    expect(calls.some((c) => c.method === 'problem.setGroupCount')).toBe(true);
    expect(calls.some((c) => c.method === 'problem.movePart')).toBe(true);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('DemoPieceGenerator’s piece range built through the UI', async () => {
  test.setTimeout(180_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'DemoPieceGenerator.xmpuzzle');
    const fixture = await toPuzzle(page);
    const want = await problemGet(page, fixture);
    let p: Problem = await checkShown(page);
    // the fixture's optional copies are drawn faded
    expect(await page.locator('#vp').getAttribute('data-scene')).toContain('piece:s2:10/10:faded');

    const id = await newProblem(page);
    // the result from a library chip's menu
    await menu(page, lib('s3'), 'res');
    expect((await checkShown(page)).result).toBe('s3');
    await expect(page.locator(lib('s3'))).toHaveCount(0);
    // All +1 (one of each), Clear All
    await page.click('#pzAll');
    p = await checkShown(page);
    expect(p.parts.map((x) => [x.shape, x.min, x.max])).toEqual([
      ['s1', 1, 1],
      ['s2', 1, 1],
    ]);
    await page.click('#pzClear');
    expect((await checkShown(page)).parts).toEqual([]);
    // S1 by the library's +, S2 dragged in
    await page.click(`${lib('s1')} .la`);
    await checkShown(page);
    await page.dragAndDrop(`${lib('s2')} b`, '#pzUsed');
    await checkShown(page);
    // one range for all: 0 to 10
    await page.click('#pzGrpHead');
    await page.click('#pzRangeAll');
    await expect(page.locator('#pzBulk')).toBeVisible();
    await fillAndLeave(page, '#pzBulk [data-r="mx"]', '10');
    await expect(page.locator('#pzBulk [data-r="mx"]')).toHaveValue('10');
    await page.click('#pzBulk [data-r="mn+"]');
    await page.click('#pzBulk [data-r="mn-"]');
    await page.click('#pzBulk [data-x="1"]');
    p = await checkShown(page);
    expect(p.parts.map((x) => [x.shape, x.min, x.max])).toEqual([
      ['s1', 0, 10],
      ['s2', 0, 10],
    ]);
    // S1 back to one copy: the range switched off (both ends the maximum), then the count typed
    await select(page, tile('s1'));
    await page.click('#pzRange');
    p = await checkShown(page);
    expect(p.parts[0]).toMatchObject({ min: 10, max: 10 });
    await fillAndLeave(page, '#pzSel input[data-sf="max"]', '1');
    p = await checkShown(page);
    expect(p.parts[0]).toMatchObject({ min: 1, max: 1 });
    // S2's minimum up and down by the selected piece's steppers
    await select(page, tile('s2'));
    await page.click('#pzSel button[data-sf="min"][data-d="1"]');
    p = await checkShown(page);
    expect(p.parts[1]).toMatchObject({ min: 1, max: 10 });
    await page.click('#pzSel button[data-sf="min"][data-d="-1"]');
    await checkShown(page);
    // a fixed count shown as a range and back: no call, nothing changes
    await select(page, tile('s1'));
    await page.click('#pzRange');
    await expect(page.locator('#pzSel button[data-sf="min"]').first()).toBeVisible();
    await page.click('#pzRange');
    await expect(page.locator('#pzSel button[data-sf="min"]')).toHaveCount(0);

    const built = await problemGet(page, id);
    expect(definition(built)).toEqual(definition(want));
    // the scene: S1 once, ten copies of S2, all optional
    await checkShown(page);
    expect(refusals(readCallLog(log))).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('problems, colour rules, the frozen problem, sliding and stacking puzzles', async () => {
  test.setTimeout(180_000);
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  const log = join(dir, 'calls.log');
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    // a new puzzle: a box of 2x4x6 and a domino (a long solve on one thread), two colours
    await newDocument(page, 'bricks');
    const box = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 4, 6] }))
      .shape;
    await engineCall(page, 'shape.setBox', {
      shape: box,
      tool: 'fixed',
      from: [0, 0, 0],
      to: [1, 3, 5],
    });
    const dom = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 1, 1] }))
      .shape;
    await engineCall(page, 'shape.setBox', {
      shape: dom,
      tool: 'fixed',
      from: [0, 0, 0],
      to: [1, 0, 0],
    });
    const c1 = (await engineCall<{ colour: string }>(page, 'colour.add', { rgb: '#ff0000' }))
      .colour;
    const c2 = (await engineCall<{ colour: string }>(page, 'colour.add', { rgb: '#0000ff' }))
      .colour;
    await page.keyboard.press('Control+2');
    await expect(page.locator('#emptyMsg')).toHaveText('No problems — add one with + New');
    await expect(page.locator('#stText')).toContainText('No problems');

    // problems: new, rename, cancel a rename, duplicate, new, move by drag, delete
    await page.click('#pzNew');
    await expect(page.locator('#pzProblems .row')).toHaveCount(1);
    await pzSettled(page);
    await expect(page.locator('#pzProblems .row [data-a=del]')).toBeDisabled();
    const p1 = await currentProblem(page);
    await page.dblclick(`#pzProblems .row[data-id="${p1}"] .chip`);
    await page.keyboard.type('Box');
    await page.keyboard.press('Enter');
    await pzSettled(page);
    await page.click(`#pzProblems .row[data-id="${p1}"] [data-a=rename]`);
    await page.keyboard.type('Nothing');
    await page.keyboard.press('Escape');
    await pzSettled(page);
    await expect(page.locator(`#pzProblems .row[data-id="${p1}"] .lbl`)).toHaveText('Box');
    await page.click(`#pzProblems .row[data-id="${p1}"] [data-a=dup]`);
    await expect(page.locator('#pzProblems .row')).toHaveCount(2);
    await pzSettled(page);
    const p2 = await currentProblem(page);
    expect(p2).not.toBe(p1);
    await page.click('#pzNew');
    await expect(page.locator('#pzProblems .row')).toHaveCount(3);
    await pzSettled(page);
    const p3 = await currentProblem(page);
    await page.dragAndDrop(
      `#pzProblems .row[data-id="${p3}"] .grip`,
      `#pzProblems .row[data-id="${p1}"] .lbl`,
    );
    await pzSettled(page);
    const list = async (): Promise<string[]> =>
      (
        await engineCall<{ problems: { id: string; label: string; name: string }[] }>(
          page,
          'problem.list',
        )
      ).problems.map((q) => `${q.id}:${q.label}:${q.name}`);
    expect(await list()).toEqual([`${p3}:P1:`, `${p1}:P2:Box`, `${p2}:P3:`]);
    const shown = await page.$$eval('#pzProblems .row', (rows) =>
      rows.map(
        (r) =>
          `${(r as HTMLElement).dataset.id ?? ''}:${r.querySelector('.chip')?.textContent ?? ''}:${r.querySelector('.lbl')?.textContent ?? ''}`,
      ),
    );
    expect(shown).toEqual(await list());
    await page.click(`#pzProblems .row[data-id="${p3}"] [data-a=del]`);
    await expect(page.locator('#pzProblems .row')).toHaveCount(2);
    await pzSettled(page);
    expect(await list()).toEqual([`${p1}:P1:Box`, `${p2}:P2:`]);

    // colour rules of P1: each colour on itself (a new problem's), a cell, all, none, the preset
    await page.click(`#pzProblems .row[data-id="${p1}"] .chip`);
    await pzSettled(page);
    const rules = async (): Promise<string[]> =>
      (await problemGet(page, p1)).colourRules.map((r) => `${r.piece}>${r.result}`).sort();
    const matrix = async (): Promise<string[]> =>
      (
        await page.$$eval('#pzRules .mxc.on', (els) =>
          els.map(
            (e) => `${(e as HTMLElement).dataset.p ?? ''}>${(e as HTMLElement).dataset.r ?? ''}`,
          ),
        )
      ).sort();
    expect(await rules()).toEqual([`${c1}>${c1}`, `${c2}>${c2}`]);
    expect(await matrix()).toEqual(await rules());
    await page.click(`#pzRules .mxc[data-p="${c1}"][data-r="${c2}"]`);
    await pzSettled(page);
    expect(await rules()).toEqual([`${c1}>${c1}`, `${c1}>${c2}`, `${c2}>${c2}`]);
    expect(await matrix()).toEqual(await rules());
    for (const [preset, want] of [
      ['allowAll', [`${c1}>${c1}`, `${c1}>${c2}`, `${c2}>${c1}`, `${c2}>${c2}`]],
      ['clear', []],
      ['eachOnItself', [`${c1}>${c1}`, `${c2}>${c2}`]],
    ] as const) {
      await page.click(`#pzRules [data-rule="${preset}"]`);
      await pzSettled(page);
      expect(await rules()).toEqual(want);
      expect(await matrix()).toEqual(want);
    }

    // P1: the box filled with 24 dominoes, solved through the bridge: P1 frozen, P2 not
    await page.click('#pzChange');
    await page.click(`#pop [data-k="${box}"]`);
    await checkShown(page);
    await select(page, lib(dom));
    await fillAndLeave(page, '#pzSel input[data-sf="max"]', '24');
    expect((await checkShown(page)).parts).toEqual([
      { shape: dom, min: 24, max: 24, groups: [], required: false },
    ]);
    await engineCall(page, 'solve.start', { problem: p1, options: { threads: 1 } });
    await expect(page.locator('#stage')).toHaveAttribute('data-pzfrozen', '1');
    await expect(page.locator('#stText')).toContainText('pause the solve first');
    const mark = readCallLog(log).length;
    await select(page, tile(dom));
    await page.keyboard.press('+');
    await expect(page.locator('#stText')).toHaveText('Pause the solve first');
    await page.keyboard.press('-');
    await page.keyboard.press('Delete');
    await page.locator(`${tile(dom)} .t2 [data-d="1"]`).dispatchEvent('click');
    await page.locator('#pzChange').dispatchEvent('click');
    await page.locator('#pzRules [data-rule="allowAll"]').dispatchEvent('click');
    await page.locator('#pzNew').dispatchEvent('click');
    await page.click(tile(dom), { button: 'right' });
    await page.click('#pop [data-k="up"]');
    await expect(page.locator('#stText')).toHaveText('Pause the solve first');
    await expect(page.locator('#pzUsed .t2')).toHaveCSS('pointer-events', 'none');
    const edits = (from: number): string[] =>
      readCallLog(log)
        .slice(from)
        .map((c) => c.method)
        .filter(
          (m) =>
            m.startsWith('problem.') && !['problem.get', 'problem.fit', 'problem.kind'].includes(m),
        );
    expect(edits(mark)).toEqual([]);
    // P2 is not being solved: its pieces can be edited, the list cannot
    await page.click(`#pzProblems .row[data-id="${p2}"] .chip`);
    await expect(page.locator('#leftP')).toHaveAttribute('data-problem', p2);
    await expect(page.locator('#stage')).toHaveAttribute('data-pzfrozen', '0');
    await page.click(`${lib(dom)} .la`);
    await pzSettled(page);
    expect((await problemGet(page, p2)).parts).toEqual([
      { shape: dom, min: 1, max: 1, groups: [], required: false },
    ]);
    await page.locator(`#pzProblems .row[data-id="${p2}"] [data-a=dup]`).dispatchEvent('click');
    await expect(page.locator('#stText')).toHaveText('Pause the solve first');
    expect(edits(mark)).toEqual(['problem.addPiece']);
    await engineCall(page, 'solve.abort', { problem: p1 });
    await page.click(`#pzProblems .row[data-id="${p1}"] .chip`);
    await expect(page.locator('#stage')).toHaveAttribute('data-pzfrozen', '0');
    await checkShown(page);
    expect(refusals(readCallLog(log))).toEqual([]);

    // a sliding puzzle opens and shows its pieces; a stacking one its board
    // and stacks instead of the result, the pieces and the pieces scene
    await openFixture(page, 'test_sliding_solver.xmpuzzle');
    await pzSettled(page);
    await checkShown(page);
    await openFixture(page, 'test_stacking_hanoi.xmpuzzle');
    await pzSettled(page);
    await expect(page.locator('#pzBoard')).toContainText('Result: R1 - Hanoi');
    await expect(page.locator('#pzUsed')).toBeHidden();
    await expect(page.locator('#pzSel')).toBeHidden();
    await expect(page.locator('#pzRightTitle')).toHaveText('Stacks');
    await expect(page.locator('#pzStacks .stackrow')).toHaveText([/S3/, /S2/, /S1/]);
    await expect(page.locator('#pzStackValid')).toHaveText('Valid');
    await expect(page.locator('#vp')).toHaveAttribute('data-scene', '');
    await expect
      .poll(async () => ((await page.getAttribute('#vp', 'data-board')) ?? '').split(' ').length)
      .toBe(3);
    // the other grids show their result and pieces again
    await openFixture(page, 'CubeInCage.xmpuzzle');
    await pzSettled(page);
    await expect(page.locator('#pzBoard')).toBeHidden();
    await expect(page.locator('#pzUsed')).toBeVisible();
    await checkShown(page);
    expect(refusals(readCallLog(log))).toEqual([]);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
