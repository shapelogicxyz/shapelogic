// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tilted Square fixtures (decision 0045, stage F of
// docs/TILTED-SQUARE-PLAN.md; tests/data/puzzles/tilted) through the
// client: each opens quietly with its tray, and each problem, solved with
// the options the Solver makes of its stored settings (readOptions,
// solveOptions), finds the assemblies its file holds.

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
});
