// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every shape of Bermuda and of Prisgon rebuilt from its cell list through
// the editor's actions (FRONTEND-PLAN.md, stage 2's gate): a new puzzle on
// the fixture's grid, then for each shape New, the size steppers, a rename
// (F2), the weight buttons, and its cells layer by layer with the fixed
// and variable tools, drawn as drags along runs of cells next to each
// other (the engine's layer drawing says which are) or clicks; the
// result's shape.get equals the fixture's, and the engine refused nothing.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, openFixture, readCallLog } from './app.ts';
import { clickCell, dragCells, layer, selected, settled, shapeGet } from './editor.ts';
import type { Cell, ShapeGet } from './editor.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

interface GeoCell {
  at: Cell;
  neighbours?: number[];
}

const key = (c: readonly number[]): string => c.join(',');

/** What is compared: the summary but the id and label, and the cells. */
function comparable(g: ShapeGet): unknown {
  const s = g.shape;
  return {
    shape: [s.name, s.size, s.fixed, s.variable, s.weight, s.colour],
    cells: g.cells,
  };
}

/** Runs of cells of one layer drawn as drags: each next cell shares an edge with the one before. */
async function runs(page: Page, size: Cell, z: number, cells: Cell[]): Promise<Cell[][]> {
  const geo = (await engineCall<{ cells: GeoCell[] }>(page, 'grid.geometry', { size, layer: z }))
    .cells;
  const index = new Map(geo.map((c, i) => [key(c.at), i]));
  const sorted = [...cells].sort((a, b) => a[1] - b[1] || a[0] - b[0]);
  const out: Cell[][] = [];
  for (const c of sorted) {
    const run = out.at(-1);
    const last = run?.at(-1);
    const li = last === undefined ? undefined : index.get(key(last));
    const ci = index.get(key(c));
    const near = li !== undefined && ci !== undefined && (geo[li]?.neighbours ?? []).includes(ci);
    if (run !== undefined && near) {
      run.push(c);
    } else {
      out.push([c]);
    }
  }
  return out;
}

async function steps(page: Page, shape: string, axis: number, target: number): Promise<void> {
  for (;;) {
    const now = (await shapeGet(page, shape)).shape.size[axis] ?? 0;
    if (now === target) {
      return;
    }
    await page.click(`#sizeSteppers [data-d="${axis}"][data-s="${now < target ? 1 : -1}"]`);
    await settled(page);
  }
}

async function rebuild(page: Page, want: ShapeGet): Promise<string> {
  const before = await page.locator('#shapeList .row').count();
  await page.click('#btnNew');
  await expect(page.locator('#shapeList .row')).toHaveCount(before + 1);
  await settled(page);
  const id = await selected(page);
  for (const axis of [0, 1, 2]) {
    await steps(page, id, axis, want.shape.size[axis] ?? 1);
  }
  if (want.shape.name !== '') {
    await page.keyboard.press('F2');
    await page.locator('#shapeList .lbl input').fill(want.shape.name);
    await page.keyboard.press('Enter');
    await settled(page);
  }
  while ((await shapeGet(page, id)).shape.weight !== want.shape.weight) {
    const w = (await shapeGet(page, id)).shape.weight;
    await page.click(
      `#shapeList .row[data-id="${id}"] [data-a="${w < want.shape.weight ? 'wp' : 'wm'}"]`,
    );
    await settled(page);
  }
  const layers = [...new Set(want.cells.map((c) => c.at[2]))].sort((a, b) => a - b);
  for (const z of layers) {
    await layer(page, z);
    for (const state of ['fixed', 'variable'] as const) {
      const cells = want.cells.filter((c) => c.at[2] === z && c.state === state).map((c) => c.at);
      if (cells.length === 0) {
        continue;
      }
      await page.keyboard.press(state === 'fixed' ? '1' : '2');
      for (const run of await runs(page, want.shape.size, z, cells)) {
        if (run.length === 1 && run[0] !== undefined) {
          await clickCell(page, run[0]);
        } else {
          await dragCells(page, run);
        }
      }
    }
  }
  return id;
}

for (const fixture of ['Bermuda.xmpuzzle', 'Prisgon.xmpuzzle']) {
  test(`every shape of ${fixture} rebuilt through the editor`, async () => {
    test.setTimeout(600_000);
    const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
    const log = join(dir, 'calls.log');
    const a = await launch({ callLog: log });
    const page = a.page;
    try {
      await openFixture(page, fixture);
      const snap = await engineCall<{ grid: string; shapes: { id: string }[] }>(
        page,
        'document.get',
      );
      const wants: ShapeGet[] = [];
      for (const s of snap.shapes) {
        wants.push(await shapeGet(page, s.id));
      }
      await newDocument(page, snap.grid);
      for (const want of wants) {
        const id = await rebuild(page, want);
        expect(comparable(await shapeGet(page, id)), want.shape.label).toEqual(comparable(want));
      }
      const refused = readCallLog(log).filter(
        (c) => c.errc === 'invalid_state' || c.errc === 'invalid_argument',
      );
      expect(refused).toEqual([]);
    } finally {
      await a.close();
      rmSync(dir, { recursive: true, force: true });
    }
  });
}
