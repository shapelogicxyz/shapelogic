// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace on each of the five grids (FRONTEND-PLAN.md,
// stage 2's gate): a new puzzle and a new shape, then every tool, the
// quick tools of the modifiers and the right button, a drag as one undo
// step, undo and redo, mirror and span, the box selection's fill and
// clear, layers, the grid size with and without "Apply to all shapes",
// every task of shape.taskSet (and a blocked nudge's message), the
// palette and paint, and 3D editing (the cell across a face from the
// client's table on cubes, from grid.neighbour elsewhere). What the UI
// did is checked against the engine (shape.get through the bridge), and
// what it asked for against the same request made through the bridge on
// a copy of the shape; the call log holds no invalid_state or
// invalid_argument reply.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, engineCall, launch, newDocument, readCallLog } from './app.ts';
import type { App } from './app.ts';
import {
  cellPoint,
  clickCell,
  dragCells,
  history,
  layer,
  selected,
  settled,
  shapeGet,
  tool,
} from './editor.ts';
import type { Cell, EngineCell, ShapeGet } from './editor.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const GRIDS = ['bricks', 'prisms', 'spheres', 'rhombic', 'tetraOcta'] as const;

interface GeoCell {
  at: Cell;
  polygon?: [number, number][];
  neighbours?: number[];
  circle?: { centre: [number, number]; radius: number };
}

const key = (c: readonly number[]): string => c.join(',');
const cellsOf = (g: ShapeGet): string[] =>
  g.cells.map((c) => `${key(c.at)}:${c.state}:${c.colour ?? ''}`).sort();

/** The engine's drawing of a layer of the selected shape's space. */
async function geometry(page: Page, size: Cell, l: number): Promise<GeoCell[]> {
  return (await engineCall<{ cells: GeoCell[] }>(page, 'grid.geometry', { size, layer: l })).cells;
}

/** A chain of cells of a layer, each next to the one before (an edge, or circles that touch). */
function chain(cells: GeoCell[], n: number): Cell[] {
  const near = (a: GeoCell, b: GeoCell): boolean => {
    if (a.circle !== undefined && b.circle !== undefined) {
      const d = Math.hypot(
        a.circle.centre[0] - b.circle.centre[0],
        a.circle.centre[1] - b.circle.centre[1],
      );
      return d <= a.circle.radius + b.circle.radius + 1e-6;
    }
    return (a.neighbours ?? []).some((i) => cells[i] === b);
  };
  // a simple path of n cells, depth first
  const walk = (path: GeoCell[]): GeoCell[] | null => {
    if (path.length === n) {
      return path;
    }
    const last = path.at(-1);
    for (const c of cells) {
      if (last !== undefined && !path.includes(c) && near(last, c)) {
        const found = walk([...path, c]);
        if (found !== null) {
          return found;
        }
      }
    }
    return null;
  };
  for (const start of cells) {
    const found = walk([start]);
    if (found !== null) {
      return found.map((c) => c.at);
    }
  }
  throw new Error(`no chain of ${n} cells`);
}

/** Runs a request through the bridge on a copy of a shape; the copy's cells (the copy is removed). */
async function onCopy(
  page: Page,
  shape: string,
  method: string,
  params: Record<string, unknown>,
): Promise<string[] | 'refused'> {
  const copy = (await engineCall<{ shape: string }>(page, 'shape.duplicate', { shape })).shape;
  let result: string[] | 'refused';
  try {
    await engineCall(page, method, { ...params, shape: copy });
    result = cellsOf(await shapeGet(page, copy));
  } catch {
    result = 'refused';
  }
  await engineCall(page, 'shape.delete', { shape: copy });
  await settled(page);
  return result;
}

async function newShape(page: Page): Promise<string> {
  const before = await page.locator('#shapeList .row').count();
  await page.click('#btnNew');
  await expect(page.locator('#shapeList .row')).toHaveCount(before + 1);
  await expect(page.locator('#shapeList .row').last()).toHaveClass(/\bsel\b/);
  await settled(page);
  return selected(page);
}

async function clearShape(page: Page, shape: string): Promise<void> {
  const g = await shapeGet(page, shape);
  await engineCall(page, 'shape.setBox', {
    shape,
    tool: 'erase',
    from: [0, 0, 0],
    to: g.shape.size.map((v) => v - 1),
  });
  await settled(page);
}

async function mods(page: Page, kind: 'mirror' | 'span', axis: string): Promise<void> {
  await page.click(`#modRow [data-g="${kind}"] [data-a="${axis}"]`);
}

for (const grid of GRIDS) {
  test(`the editor on the ${grid} grid`, async () => {
    test.setTimeout(240_000);
    const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
    const log = join(dir, 'calls.log');
    const a: App = await launch({ callLog: log });
    const page = a.page;
    try {
      await newDocument(page, grid);
      const info = await engineCall<{
        grids: { grid: string; defaultSize: number; sizeUnit: number }[];
      }>(page, 'engine.info');
      const g = info.grids.find((x) => x.grid === grid);
      const unit = g?.sizeUnit ?? 1;
      const s1 = await newShape(page);
      expect((await shapeGet(page, s1)).shape.size).toEqual([
        g?.defaultSize,
        g?.defaultSize,
        g?.defaultSize,
      ]);
      await expect(page.locator('#vtChip')).not.toBeEmpty();
      // the size steppers count in the grid's blocks, rounded up as the
      // original's (a tetrahedra-octahedra shape of 4 cells shows 2 blocks of 3)
      const blocks = async (id: string): Promise<number[]> =>
        (await shapeGet(page, id)).shape.size.map((v) => Math.ceil(v / unit));
      await expect(page.locator('#sizeSteppers .v[data-v="0"]')).toHaveText(
        String((await blocks(s1))[0]),
      );
      if (grid === 'spheres') {
        // a layer of 3 by 3 spheres has no chain of 4 that touch: 5 by 5
        for (const d of [0, 0, 1, 1]) {
          await page.click(`#sizeSteppers [data-d="${d}"][data-s="1"]`);
          await settled(page);
        }
      }
      const size = (await shapeGet(page, s1)).shape.size;

      // ---- the tools, the quick tools, the right button ----
      const geo0 = await geometry(page, size, 0);
      const [A, B, C, D] = chain(geo0, 4);
      if (A === undefined || B === undefined || C === undefined || D === undefined) {
        throw new Error('a layer too small');
      }
      await page.keyboard.press('1');
      await clickCell(page, A);
      await page.keyboard.press('2');
      await expect(page.locator('#toolStrip [data-t="variable"]')).toHaveClass(/\bon\b/);
      await clickCell(page, B);
      let got = await shapeGet(page, s1);
      expect(got.cells.map((c) => [key(c.at), c.state]).sort()).toEqual(
        [
          [key(A), 'fixed'],
          [key(B), 'variable'],
        ].sort(),
      );
      await tool(page, 'erase');
      await clickCell(page, B);
      expect((await shapeGet(page, s1)).cells.map((c) => key(c.at))).toEqual([key(A)]);
      // quick tools: Shift fixed, Alt variable, Ctrl erase, the right button erases
      await clickCell(page, C, { modifiers: ['Shift'] });
      await clickCell(page, D, { modifiers: ['Alt'] });
      got = await shapeGet(page, s1);
      expect(got.cells.find((c) => key(c.at) === key(C))?.state).toBe('fixed');
      expect(got.cells.find((c) => key(c.at) === key(D))?.state).toBe('variable');
      await page.keyboard.press('1');
      await clickCell(page, C, { modifiers: ['Control'] });
      await clickCell(page, D, { button: 'right' });
      expect((await shapeGet(page, s1)).cells.map((c) => key(c.at))).toEqual([key(A)]);
      // the status bar names the cell under the mouse
      const pa = await cellPoint(page, A);
      await page.mouse.move(pa?.x ?? 0, pa?.y ?? 0);
      await expect(page.locator('#stCursor')).not.toBeEmpty();

      // ---- a drag over N cells is one undo step; undo and redo give the cells back ----
      const before = await shapeGet(page, s1);
      const steps = (await history(page)).undo.length;
      const run = chain(geo0, Math.min(6, geo0.length));
      await page.keyboard.press('1');
      await dragCells(page, run);
      const after = await shapeGet(page, s1);
      expect(after.cells.map((c) => key(c.at)).sort()).toEqual(
        [...new Set([key(A), ...run.map(key)])].sort(),
      );
      const h = await history(page);
      expect(h.undo.length).toBe(steps + 1);
      expect(h.undo.at(-1)).toBe('Draw fixed voxels');
      await page.keyboard.press('Control+z');
      await expect.poll(async () => cellsOf(await shapeGet(page, s1))).toEqual(cellsOf(before));
      await expect(page.locator('#stText')).toHaveText('Undone: Draw fixed voxels');
      await page.keyboard.press('Control+Shift+z');
      await expect.poll(async () => cellsOf(await shapeGet(page, s1))).toEqual(cellsOf(after));
      await expect(page.locator('#stText')).toHaveText('Redone: Draw fixed voxels');
      await settled(page);
      // a drag with the right button erases
      await dragCells(page, run.slice(0, 3), 'right');
      expect((await shapeGet(page, s1)).cells.map((c) => key(c.at)).sort()).toEqual(
        after.cells
          .map((c) => key(c.at))
          .filter((k) => !run.slice(0, 3).map(key).includes(k))
          .sort(),
      );

      // ---- mirror and span: the engine's axes on a click ----
      for (const [kind, axis] of [
        ['mirror', 'x'],
        ['mirror', 'y'],
        ['span', 'y'],
        ['span', 'x'],
      ] as const) {
        await clearShape(page, s1);
        await mods(page, kind, axis);
        const want = await onCopy(page, s1, 'shape.setCells', {
          tool: 'fixed',
          cells: [A],
          [kind]: [axis],
        });
        await clickCell(page, A);
        expect(cellsOf(await shapeGet(page, s1)), `${kind} ${axis}`).toEqual(want);
        await mods(page, kind, axis);
      }

      // ---- the box selection: fill and clear (shape.setBox), Esc ----
      await clearShape(page, s1);
      await page.click('#boxSeg [data-box]');
      const corner0 = run[0] ?? A;
      const corner1 = run.at(-1) ?? A;
      await dragCells(page, [corner0, corner1]);
      await expect(page.locator('#selbar')).toHaveClass(/\bshow\b/);
      const n = Number(await page.locator('#garea svg').getAttribute('data-selected'));
      expect(n).toBeGreaterThan(0);
      await expect(page.locator('#selbar b')).toHaveText(String(n));
      const lo = [Math.min(corner0[0], corner1[0]), Math.min(corner0[1], corner1[1]), 0];
      const hi = [Math.max(corner0[0], corner1[0]), Math.max(corner0[1], corner1[1]), 0];
      const filled = await onCopy(page, s1, 'shape.setBox', { tool: 'fixed', from: lo, to: hi });
      await page.click('#selbar [data-x="fill"]');
      await settled(page);
      expect(cellsOf(await shapeGet(page, s1))).toEqual(filled);
      await page.click('#selbar [data-x="clear"]');
      await settled(page);
      expect((await shapeGet(page, s1)).cells).toEqual([]);
      await page.keyboard.press('Escape');
      await expect(page.locator('#selbar')).not.toHaveClass(/\bshow\b/);
      await page.click('#boxSeg [data-box]');

      // ---- layers: PgUp and PgDn, the chips' marks ----
      await page.locator('#garea').hover();
      await page.keyboard.press('PageUp');
      await expect(page.locator('#lstrip .lchip[data-l="1"]')).toHaveClass(/\bon\b/);
      const geo1 = await geometry(page, size, 1);
      const L1 = geo1[0]?.at;
      if (L1 === undefined) {
        throw new Error('no cell in layer 2');
      }
      await clickCell(page, L1);
      expect((await shapeGet(page, s1)).cells.map((c) => key(c.at))).toEqual([key(L1)]);
      await expect(page.locator('#lstrip .lchip[data-l="1"]')).toHaveClass(/\bhas\b/);
      await expect(page.locator('#lstrip .lchip[data-l="0"]')).not.toHaveClass(/\bhas\b/);
      await page.keyboard.press('PageDown');
      await layer(page, 0);

      // ---- the grid size, alone and with "Apply to all shapes" ----
      await clickCell(page, A);
      const s2 = await newShape(page);
      await page.click(`#shapeList .row[data-id="${s1}"]`);
      await expect(page.locator(`#shapeList .row[data-id="${s1}"]`)).toHaveClass(/\bsel\b/);
      const sz = async (id: string): Promise<Cell> => (await shapeGet(page, id)).shape.size;
      const s2size = await sz(s2);
      const b1 = await blocks(s1);
      await page.click('#sizeSteppers [data-d="0"][data-s="1"]');
      await settled(page);
      expect((await sz(s1))[0]).toBe(((b1[0] ?? 0) + 1) * unit);
      expect(await sz(s2)).toEqual(s2size);
      await expect(page.locator('#applyAll')).toHaveAttribute('title', /never shrink/);
      await page.click('#applyAll');
      await expect(page.locator('#applyAll')).toHaveClass(/\bon\b/);
      const y0 = (await sz(s1))[1];
      await page.click('#sizeSteppers [data-d="1"][data-s="1"]');
      await settled(page);
      const y1 = ((b1[1] ?? 0) + 1) * unit;
      expect((await sz(s1))[1]).toBe(y1);
      // the others grow by as much, to whole blocks
      const s2y = unit * Math.ceil((s2size[1] + (y1 - y0)) / unit);
      expect((await sz(s2))[1]).toBe(s2y);
      // shrinking: this one only, the others never shrink
      await page.click('#sizeSteppers [data-d="1"][data-s="-1"]');
      await settled(page);
      expect((await sz(s1))[1]).toBe(y1 - unit);
      expect((await sz(s2))[1]).toBe(s2y);
      await page.click('#applyAll');
      await expect(page.locator('#applyAll')).not.toHaveClass(/\bon\b/);

      // ---- every task of shape.taskSet, as the engine does it on a copy ----
      const tasks = (
        await engineCall<{ tasks: { task: string; group: string; label: string }[] }>(
          page,
          'shape.taskSet',
        )
      ).tasks;
      await page.click('#secFit > button.sh');
      await page.click('#secTools > button.sh');
      await clearShape(page, s1);
      const blob = chain(geo0, 3);
      for (const c of blob) {
        await clickCell(page, c);
      }
      let blocked = 0;
      for (const t of tasks) {
        const want = await onCopy(page, s1, 'shape.task', { task: t.task });
        const steps0 = (await history(page)).undo.length;
        const before = await shapeGet(page, s1);
        await page.click(`#insp [data-task="${t.task}"]`);
        await settled(page);
        if (want === 'refused') {
          expect(t.group, t.task).toBe('Nudge');
          await expect(page.locator('#stText')).toHaveText(
            'Blocked: the piece would leave the grid',
          );
          blocked++;
        } else {
          expect(cellsOf(await shapeGet(page, s1)), t.task).toEqual(want);
          const h1 = await history(page);
          if (h1.undo.length > steps0) {
            expect(h1.undo.at(-1), t.task).toBe(t.label);
          }
        }
        // the shape stays small: big ones (double, triple, grid scale) go back
        if (['double', 'triple', 'gridScale'].includes(t.task) && want !== 'refused') {
          await page.keyboard.press('Control+z');
          await expect
            .poll(async () => {
              const g = await shapeGet(page, s1);
              return [cellsOf(g), g.shape.size];
            })
            .toEqual([cellsOf(before), before.shape.size]);
          await settled(page);
        }
      }
      // a nudge towards the space's edge is blocked: the cells at the corner
      if (blocked === 0) {
        await clearShape(page, s1);
        await clickCell(page, A);
        for (const t of tasks.filter((x) => x.group === 'Nudge')) {
          if ((await onCopy(page, s1, 'shape.task', { task: t.task })) === 'refused') {
            await page.click(`#insp [data-task="${t.task}"]`);
            await expect(page.locator('#stText')).toHaveText(
              'Blocked: the piece would leave the grid',
            );
            blocked++;
            break;
          }
        }
      }
      expect(blocked).toBeGreaterThan(0);

      // ---- the palette and paint (on a new shape: the tasks pruned this one) ----
      const s3 = await newShape(page);
      const geo3 = await geometry(page, (await shapeGet(page, s3)).shape.size, 0);
      const [P, Q, R] = chain(geo3, 3);
      if (P === undefined || Q === undefined || R === undefined) {
        throw new Error('a layer too small');
      }
      await clickCell(page, P);
      await page.click('#cAdd');
      await settled(page);
      const colours = async (): Promise<{ id: string; rgb: string }[]> =>
        (await engineCall<{ colours: { id: string; rgb: string }[] }>(page, 'colour.list')).colours;
      const [c1] = await colours();
      expect(c1).toBeDefined();
      await expect(page.locator(`#colFoot .sw[data-c="${c1?.id}"]`)).toHaveClass(/\bon\b/);
      await page.keyboard.press('4');
      await clickCell(page, P);
      expect((await shapeGet(page, s3)).cells[0]?.colour).toBe(c1?.id);
      // a new voxel takes the drawing colour; Default is the shape's own
      await page.keyboard.press('1');
      await clickCell(page, Q);
      expect(
        (await shapeGet(page, s3)).cells.find((c: EngineCell) => key(c.at) === key(Q))?.colour,
      ).toBe(c1?.id);
      await page.click('#colFoot .sw.def');
      await clickCell(page, R);
      expect(
        (await shapeGet(page, s3)).cells.find((c: EngineCell) => key(c.at) === key(R))?.colour,
      ).toBeUndefined();
      // edit the colour, then remove it: its cells go back to the shape's own
      await page.click(`#colFoot .sw[data-c="${c1?.id}"]`);
      // (the menu's "Edit colour…" opens the system's picker, which a test cannot drive)
      await page.locator('#cPick').fill('#123456');
      await expect.poll(async () => (await colours())[0]?.rgb).toBe('#123456');
      await page.click('#cMore');
      await page.click('#pop [data-k="rm"]');
      await expect.poll(async () => (await colours()).length).toBe(0);
      expect((await shapeGet(page, s3)).cells.every((c) => c.colour === undefined)).toBe(true);
      await expect(page.locator('#colFoot .sw.def')).toHaveClass(/\bon\b/);

      // ---- 3D editing: a face's cell (erase) and the cell across it (fixed) ----
      await clearShape(page, s3);
      for (const c of geo3.slice(0, Math.min(8, geo3.length))) {
        await clickCell(page, c.at);
      }
      const cube = grid === 'bricks';
      if (!cube) {
        await page.keyboard.press('e');
        await expect(page.locator('#bEdit')).toHaveClass(/\bon\b/);
      }
      const vp = await page.locator('#cv').boundingBox();
      if (vp === null) {
        throw new Error('no view');
      }
      const size1 = (await shapeGet(page, s3)).shape.size;
      let added = false;
      for (let iy = 0; iy < 11 && !added; iy++) {
        for (let ix = 0; ix < 11 && !added; ix++) {
          const x = vp.x + vp.width * (0.25 + 0.05 * ix);
          const y = vp.y + vp.height * (0.25 + 0.05 * iy);
          await page.mouse.move(x, y);
          const m = /^Cell (\d+), (\d+), (\d+) · side (\d+)$/.exec(
            (await page.locator('#stCursor').textContent()) ?? '',
          );
          if (m === null) {
            continue;
          }
          const cell: Cell = [Number(m[1]), Number(m[2]), Number(m[3])];
          const face = Number(m[4]);
          const across = (
            await engineCall<{ cell: Cell | null }>(page, 'grid.neighbour', {
              size: size1,
              cell,
              face,
            })
          ).cell;
          const have = (await shapeGet(page, s3)).cells.map((c) => key(c.at));
          if (across === null || have.includes(key(across))) {
            continue;
          }
          if (cube) {
            // the client's table agrees with the engine's
            await page.keyboard.down('Shift');
            await page.mouse.move(x + 1, y);
            await expect(page.locator('#vp')).not.toHaveAttribute('data-preview', '0');
            await page.mouse.click(x + 1, y);
            await page.keyboard.up('Shift');
          } else {
            await expect(page.locator('#vp')).not.toHaveAttribute('data-preview', '0');
            await page.mouse.click(x, y);
          }
          await settled(page);
          const now = (await shapeGet(page, s3)).cells.map((c) => key(c.at));
          expect(now, `the cell across side ${face} of ${key(cell)}`).toContain(key(across));
          expect(now.length).toBe(have.length + 1);
          // Ctrl+click on a face erases its cell
          await page.mouse.move(x, y);
          const m2 = /^Cell (\d+), (\d+), (\d+) · /.exec(
            (await page.locator('#stCursor').textContent()) ?? '',
          );
          if (m2 !== null) {
            await page.keyboard.down('Control');
            await page.mouse.click(x, y);
            await page.keyboard.up('Control');
            await settled(page);
            expect((await shapeGet(page, s3)).cells.map((c) => key(c.at))).not.toContain(
              `${m2[1]},${m2[2]},${m2[3]}`,
            );
          }
          added = true;
        }
      }
      expect(added, 'a face to add a cell at').toBe(true);
      if (!cube) {
        await page.keyboard.press('e');
        await expect(page.locator('#bEdit')).not.toHaveClass(/\bon\b/);
      }

      // the status bar's counts are the engine's
      const last = await shapeGet(page, s3);
      await expect(page.locator('#stText')).toContainText(
        `has ${last.shape.fixed + last.shape.variable} voxels`,
      );

      // the engine refused nothing the UI asked for, but the blocked nudges
      // (refused as invalid_argument, by design: "the shape would leave the
      // grid"), one per blocked click
      const refused = readCallLog(log).filter(
        (c) => c.errc === 'invalid_state' || c.errc === 'invalid_argument',
      );
      const nudges = refused.filter(
        (c) => c.method === 'shape.task' && c.errc === 'invalid_argument',
      );
      expect(refused.filter((c) => !nudges.includes(c))).toEqual([]);
      expect(nudges.length).toBe(blocked);
    } finally {
      await a.close();
      rmSync(dir, { recursive: true, force: true });
    }
  });
}
