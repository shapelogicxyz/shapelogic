// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Puzzle workspace reads off the engine and relies on, against
// the real engine: every count a field or a stepper asks for
// (puzzle/counts.ts's nextCount) is one problem.setPieceCount takes; the
// groups read off problem.get (a copy added later joins no group, so the
// UI sets a group after the count, and setGroup takes every copy); a new
// problem allows each colour on itself; the kinds of the puzzles this
// version opens but does not solve; and the fit summary's numbers for
// every fixture's problems hold together as the cards show them.

import { readdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { groupOf, groupsOf, nextCount } from '../../src/renderer/puzzle/counts.ts';
import { puzLayout } from '../../src/renderer/puzzle/layout.ts';
import { ENGINE, FIXTURES, ROOT, Session } from '../helpers/engine.ts';

describe.skipIf(ENGINE === null)('the Puzzle workspace and the engine', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('asks only for counts the engine takes, and gets them', async () => {
    const c = s.client;
    await c.call('document.open', { path: `${FIXTURES}/DemoPieceGenerator.xmpuzzle` });
    const problem = (await c.call('problem.create')).problem;
    await c.call('problem.setResult', { problem, shape: 's3' });
    for (const [field, value, range] of [
      ['max', 3, false],
      ['max', 5, true],
      ['min', 2, true],
      ['min', 9, true],
      ['max', 1, true],
      ['max', 4, false],
      ['min', 0, true],
      ['max', 0, true],
      ['max', 1, true],
      ['max', -1, false],
    ] as const) {
      const before = (await c.call('problem.get', { problem })).parts.find((p) => p.shape === 's1');
      const want = nextCount(before, field, value, range);
      await c.call('problem.setPieceCount', { problem, shape: 's1', ...want });
      const after = (await c.call('problem.get', { problem })).parts.find((p) => p.shape === 's1');
      expect(after === undefined ? { min: 0, max: 0 } : { min: after.min, max: after.max }).toEqual(
        want,
      );
    }
  });

  it('keeps groups as the UI reads them', async () => {
    const c = s.client;
    await c.call('document.open', { path: `${FIXTURES}/CubeInCage.xmpuzzle` });
    const fixture = (await c.call('problem.list')).problems[0]?.id ?? '';
    expect(groupsOf(await c.call('problem.get', { problem: fixture }))).toEqual([1]);
    const problem = (await c.call('problem.create')).problem;
    await c.call('problem.addPiece', { problem, shape: 's3' });
    await c.call('problem.setGroup', { problem, shape: 's3', group: 1 });
    await c.call('problem.addPiece', { problem, shape: 's3' });
    // a copy added later is in no group
    let d = await c.call('problem.get', { problem });
    expect(d.parts[0]?.groups).toEqual([{ group: 1, count: 1 }]);
    // setGroup takes every copy, setGroupCount some, deleteGroup all
    await c.call('problem.setGroup', { problem, shape: 's3', group: 1 });
    d = await c.call('problem.get', { problem });
    expect(d.parts[0]?.groups).toEqual([{ group: 1, count: 2 }]);
    await c.call('problem.setGroupCount', { problem, shape: 's3', group: 2, count: 1 });
    d = await c.call('problem.get', { problem });
    expect(groupsOf(d)).toEqual([1, 2]);
    expect(groupOf(d.parts[0])).toBe(1);
    await c.call('problem.deleteGroup', { problem, group: 1 });
    d = await c.call('problem.get', { problem });
    expect(groupsOf(d)).toEqual([2]);
  });

  it('starts a problem with each colour on itself; knows what it does not solve', async () => {
    const c = s.client;
    await c.call('document.new', { grid: 'bricks' });
    const a = (await c.call('colour.add', { rgb: '#ff0000' })).colour;
    const b = (await c.call('colour.add', { rgb: '#00ff00' })).colour;
    const problem = (await c.call('problem.create')).problem;
    const rules = (await c.call('problem.get', { problem })).colourRules;
    expect(rules.map((r) => `${r.piece}>${r.result}`).sort()).toEqual([`${a}>${a}`, `${b}>${b}`]);
    for (const [f, kind] of [
      ['test_sliding_solver.xmpuzzle', 'sliding'],
      ['test_stacking_hanoi.xmpuzzle', 'stacking'],
      ['Bermuda.xmpuzzle', 'brick'],
    ] as const) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      const id = snap.problems[0]?.id ?? '';
      expect(await c.call('problem.kind', { problem: id })).toEqual({
        kind,
        supported: kind !== 'stacking',
      });
      const grid = (await c.call('engine.info')).grids.find((g) => g.grid === snap.grid);
      expect(grid?.supported).toBe(kind !== 'stacking');
    }
  });

  it('gives every fixture’s problems a fit and a scene the cards can show', async () => {
    const c = s.client;
    const files = readdirSync(resolve(ROOT, FIXTURES)).filter((f) => f.endsWith('.xmpuzzle'));
    let problems = 0;
    for (const f of files) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      for (const p of snap.problems) {
        problems++;
        const d = await c.call('problem.get', { problem: p.id });
        const fit = await c.call('problem.fit', { problem: p.id });
        expect(d.result).toBe(p.result);
        expect(fit.verdict === 'noResult').toBe(d.result === null);
        if (d.result !== null) {
          expect(fit.statusLine).toMatch(new RegExp(`^Problem ${p.label} result can contain`));
          expect(fit.piecesMax).toBe(d.parts.reduce((n, x) => n + x.max, 0));
          expect(fit.piecesMin).toBe(d.parts.reduce((n, x) => n + x.min, 0));
        }
        // every part a piece of a shape of the document, never the result
        for (const x of d.parts) {
          expect(snap.shapes.some((sh) => sh.id === x.shape)).toBe(true);
          expect(x.shape).not.toBe(d.result);
          expect(x.min).toBeLessThanOrEqual(x.max);
        }
        const { items } = puzLayout(d.result, null, d.parts);
        expect(items.filter((i) => i.role === 'piece')).toHaveLength(
          Math.min(
            100,
            d.parts.reduce((n, x) => n + x.max, 0),
          ),
        );
      }
    }
    expect(problems).toBeGreaterThan(15);
  });
});
