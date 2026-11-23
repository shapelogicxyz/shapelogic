// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tilted Square fixtures (decision 0045, stage F of
// docs/TILTED-SQUARE-PLAN.md; tests/data/puzzles/tilted) through the
// client: each opens quietly with its tray, and each problem, solved with
// the options the Solver makes of its stored settings (readOptions,
// solveOptions), finds the assemblies its file holds. Every placement
// (stage G): the boards of the fixture whose best placement takes no
// pieces, and the fit that finds the one that does.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { defaultLimits, readOptions, solveOptions } from '../../src/renderer/solver/settings.ts';
import { ENGINE, FIXTURES, Session } from '../helpers/engine.ts';

/** Each fixture: its tray's whole cells and walls, and each problem's assemblies. */
const FILES = [
  {
    file: 'tilted/TriplePlay.xmpuzzle',
    whole: 24,
    walls: [
      [5, 0],
      [0, 5],
    ],
    flip: [true, false],
    assemblies: [1, 2],
  },
  { file: 'tilted/EightByEight.xmpuzzle', whole: 45, walls: [], flip: [true], assemblies: [2] },
  {
    file: 'tilted/OtherDiagonal.xmpuzzle',
    whole: 24,
    walls: [
      [0, 0],
      [5, 5],
    ],
    flip: [true],
    assemblies: [0],
  },
] as const;

describe.skipIf(ENGINE === null)('Tilted Square fixtures', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  for (const f of FILES) {
    it(`${f.file}: opened quietly, solved as the Solver solves it`, async () => {
      const c = s.client;
      const from = s.events.length;
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f.file}` });
      expect(snap.dirty).toBe(false);
      expect(snap.grid).toBe('bricks');
      expect(snap.tray?.whole).toBe(f.whole);
      expect(snap.tray?.walls).toEqual(f.walls);
      // the board first, then pieces that lie on its whole cells
      expect(snap.shapes[0]?.board).toBe(true);
      expect(snap.shapes[0]?.variable).toBe(f.whole);
      expect(snap.shapes.slice(1).every((x) => x.fits && !x.board)).toBe(true);
      await s.drain(20);
      expect(s.events.slice(from).filter((e) => e.event === 'document.changed')).toEqual([]);

      expect(snap.problems.length).toBe(f.assemblies.length);
      for (const [k, row] of snap.problems.entries()) {
        const detail = await c.call('problem.get', { problem: row.id });
        expect(detail.assemblies).toBe(f.assemblies[k]);
        // a tilted puzzle's Solver: assemble only, the stored flipPieces
        const o = readOptions(detail.solverSettings, undefined, 'brick', false);
        expect(o.disassemble).toBe(false);
        expect(o.flipPieces).toBe(f.flip[k]);
        await c.call('solve.reset', { problem: row.id });
        const start = s.events.length;
        await c.call('solve.start', {
          problem: row.id,
          options: solveOptions(o, defaultLimits()),
        });
        const end = await s.waitFor(
          'solve.state',
          (e) => e.problem === row.id && (e.state === 'finished' || e.state === 'error'),
          start,
          60_000,
        );
        expect(end.state).toBe('finished');
        await s.drain(20);
        const again = await c.call('problem.get', { problem: row.id });
        expect(again.assemblies).toBe(f.assemblies[k]);
        expect(again.saved).toBe(f.assemblies[k]);
      }
    });
  }

  it('OtherDiagonal: the best placement takes no pieces, another does', async () => {
    const c = s.client;
    const snap = await c.call('document.open', {
      path: `${FIXTURES}/tilted/OtherDiagonal.xmpuzzle`,
    });
    const problem = snap.problems[0]?.id ?? '';
    const listed = await c.call('tray.placements');
    expect(listed.complete).toBe(true);
    expect(listed.inUse).toBe(0);
    expect(listed.boards.map((b) => b.whole)).toEqual([24, 24, 24, 24, 24, 23]);
    expect(listed.boards[0]?.shiftX).toBe(snap.tray?.shiftX);
    // each board drawn at its shift, as the dialog draws it
    for (const b of listed.boards) {
      const t = await c.call('tray.preview', {
        tray: { placement: 'custom', shiftX: b.shiftX, shiftY: b.shiftY },
      });
      expect(t.whole).toBe(b.whole);
    }
    // the fit, with the options the dialog makes of the stored settings
    const detail = await c.call('problem.get', { problem });
    const options = solveOptions(
      readOptions(detail.solverSettings, undefined, 'brick', false),
      defaultLimits(),
    );
    const start = s.events.length;
    expect((await c.call('tray.fitPlacements', { problem, options })).total).toBe(6);
    const end = await s.waitFor('tray.fitEnded', (e) => e.problem === problem, start, 60_000);
    expect(end.stopped).toBe(false);
    expect(end.failure).toBeNull();
    expect(end.done).toBe(6);
    expect(end.boards.map((b) => b.counted)).toEqual([true, true, true, true, true, true]);
    expect(end.boards.map((b) => b.assemblies)).toEqual([0, 0, 0, 0, 1, 0]);
    await s.drain(20);
    // nothing changed but the revision that ends the freeze
    const after = await c.call('document.get');
    expect(after.dirty).toBe(false);
    expect(after.solving).toBe(false);
    expect(after.tray).toEqual(snap.tray);
    // the board that takes them, used and solved
    const fits = listed.boards[4];
    await c.call('tray.set', {
      tray: { placement: 'custom', shiftX: fits?.shiftX ?? 0, shiftY: fits?.shiftY ?? 0 },
    });
    expect((await c.call('tray.placements')).inUse).toBe(4);
    const from = s.events.length;
    await c.call('solve.start', { problem, options });
    const solved = await s.waitFor(
      'solve.state',
      (e) => e.problem === problem && (e.state === 'finished' || e.state === 'error'),
      from,
      60_000,
    );
    expect(solved.state).toBe('finished');
    await s.drain(20);
    expect((await c.call('problem.get', { problem })).assemblies).toBe(1);
  });
});
