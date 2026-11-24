// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's Details line while a brick solve prepares its search
// (prepareDetails): the engine's piece of pieces, and the counts that move
// while one piece of a large board takes minutes.

import { describe, expect, it } from 'vitest';

import type { SolveProgress } from '../../../src/engine/api.generated.ts';
import { prepareDetails } from '../../../src/renderer/solver/settings.ts';

type Prepare = NonNullable<SolveProgress['prepare']>;

const counts = (o: Partial<Prepare>): Prepare => ({
  pass: 0,
  done: 0,
  total: 0,
  placements: 0,
  removed: 0,
  passMs: 0,
  passSecondsLeft: null,
  ...o,
});

describe('prepareDetails', () => {
  it('counts the placements found while preparing', () => {
    expect(
      prepareDetails({
        stage: 'prepare',
        activity: 'prepare piece 6 of 9',
        piece: 6,
        pieces: 9,
        prepare: counts({ placements: 17_514 }),
      }),
    ).toBe('Prepare piece 6 of 9. 17,514 placements so far.');
    expect(
      prepareDetails({
        stage: 'prepare',
        activity: 'prepare piece 1 of 9',
        piece: 1,
        pieces: 9,
        prepare: counts({}),
      }),
    ).toBe('Prepare piece 1 of 9.');
  });

  it('tells the sweep over the cells without a piece', () => {
    expect(
      prepareDetails({
        stage: 'reduce',
        activity: 'optimize piece 1 of 9',
        piece: 1,
        pieces: 9,
        prepare: counts({ done: 1_926, total: 4_234, placements: 8_942, removed: 24_588 }),
      }),
    ).toBe(
      'Optimize: sweeping the cells, 1,926 of 4,234 checked (45%). 24,588 placements removed, 8,942 left.',
    );
  });

  it('tells a pass over the placements, with the time left in it once known', () => {
    expect(
      prepareDetails({
        stage: 'reduce',
        activity: 'optimize piece 4 of 9',
        piece: 4,
        pieces: 9,
        prepare: counts({
          pass: 2,
          done: 459,
          total: 1_165,
          placements: 1_063,
          removed: 32_467,
          passMs: 40_000,
          passSecondsLeft: 90,
        }),
      }),
    ).toBe(
      'Optimize piece 4 of 9, pass 2: 459 of 1,165 placements checked (39%). 32,467 placements removed, 1,063 left. About 1.5 minutes left in this pass.',
    );
  });

  it('falls back to the engine words alone without counts', () => {
    expect(
      prepareDetails({
        stage: 'reduce',
        activity: 'optimize piece 2 of 9',
        piece: 2,
        pieces: 9,
        prepare: null,
      }),
    ).toBe('Optimize piece 2 of 9.');
  });
});
