// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's Performance Analysis (renderer/solver/performance.ts): the
// times as a person says them, and the panel from the engine's forecast.

import { describe, expect, it } from 'vitest';

import type { SlideForecast } from '../../../src/engine/api.generated.ts';
import {
  counterText,
  durationText,
  liveCells,
  performanceHtml,
  PICKING_UP_NOTE,
  pickingUpText,
} from '../../../src/renderer/solver/performance.ts';

const forecast = (f: Partial<SlideForecast>): SlideForecast => ({
  depth: 36,
  levelMs: Array.from({ length: 36 }, () => 190_000),
  runningMs: 0,
  growth: 1.32,
  estimates: [
    { depth: 40, levelMs: 577_000, fromNowMs: 1_500_000 },
    { depth: 100, levelMs: 31_557_600_000_000, fromNowMs: 31_557_600_000_000 },
  ],
  ...f,
});

describe('the Performance Analysis', () => {
  it('says a time in its largest units', () => {
    expect(durationText(400)).toBe('under a second');
    expect(durationText(12_400)).toBe('12 s');
    expect(durationText(59_700)).toBe('1 min');
    expect(durationText(252_000)).toBe('4 min 12 s');
    expect(durationText(3 * 3_600_000 + 20 * 60_000)).toBe('3 h 20 min');
    expect(durationText(2 * 3_600_000)).toBe('2 h');
    expect(durationText(5 * 86_400_000 + 2 * 3_600_000)).toBe('5 days 2 h');
    expect(durationText(86_400_000)).toBe('1 day');
    expect(durationText(200 * 86_400_000)).toBe('200 days');
    expect(durationText(3.4 * 365.25 * 86_400_000)).toBe('3.4 years');
    expect(durationText(250 * 365.25 * 86_400_000)).toBe('250 years');
    expect(durationText(31_557_600_000_000)).toBe('over 1,000 years');
  });

  it('counts whole seconds, and below zero with a minus', () => {
    expect(counterText(0)).toBe('0 s');
    expect(counterText(400)).toBe('0 s');
    expect(counterText(-400)).toBe('0 s');
    expect(counterText(1_600)).toBe('2 s');
    expect(counterText(252_000)).toBe('4 min 12 s');
    expect(counterText(-75_000)).toBe('−1 min 15 s');
  });

  it('shows the move searched, the move under way and every fifth move to come', () => {
    const html = performanceHtml(forecast({ runningMs: 252_000 }), false);
    expect(html.startsWith('<b>Performance Analysis</b>')).toBe(true);
    expect(html).toContain(
      '<b>Performance Analysis</b>Each move takes about 1.32× as long as the one before.<table',
    );
    expect(html).not.toContain('Searched');
    expect(html).not.toContain('has run');
    // the last move: its time, complete
    expect(html).toContain(
      '<tbody><tr class="slv-perf-done" data-depth="36"><td>36</td><td>3 min 10 s</td><td>Complete</td></tr>',
    );
    // the move under way: run so far, and the last move's time × the growth less that
    // (190 s × 1.32 = 250.8 s, 252 s run)
    expect(html).toContain(
      '<tr class="slv-perf-now over" data-depth="37"><td>37</td><td>4 min 12 s</td><td>−1 s</td></tr>',
    );
    expect(html).toContain(
      '<tr data-depth="40"><td>40</td><td>9 min 37 s</td><td>25 min</td></tr>',
    );
    expect(html).toContain('<td>over 1,000 years</td>');
    expect(html).toContain('>Remaining</th>');
    // paused: the time counts from when it is continued
    expect(performanceHtml(forecast({}), true)).toContain('>Once continued</th>');
  });

  it('counts the move under way on from when the forecast came', () => {
    const f = forecast({ runningMs: 10_000 });
    // 250.8 s due: 240.8 s left, 60 s on 180.8 s
    expect(liveCells(f, 0)).toEqual(['10 s', '4 min 1 s']);
    expect(liveCells(f, 60_000)).toEqual(['1 min 10 s', '3 min 1 s']);
    expect(liveCells(f, 300_000)).toEqual(['5 min 10 s', '−59 s']);
    const html = performanceHtml(f, false, 60_000);
    expect(html).toContain(
      '<tr class="slv-perf-now" data-depth="37"><td>37</td><td>1 min 10 s</td><td>3 min 1 s</td></tr>',
    );
  });

  it('says the saved search is being taken up, and for how long, instead of a move that does not run yet', () => {
    // after a Continue: the run reads its saved search back in, no move's
    // clock runs; 47 s into it, by the engine's count
    const html = performanceHtml(forecast({ runningMs: 0 }), false, 0, 47_200);
    expect(html).toContain(
      '<tr class="slv-perf-now picking" data-depth="37"><td>37</td><td colspan="2">Picking up the saved search… 47 s</td></tr>',
    );
    // the move searched and the moves to come stay
    expect(html).toContain('<tr class="slv-perf-done" data-depth="36">');
    expect(html).toContain('<tr data-depth="40">');
    // counted in seconds, however long it takes
    expect(pickingUpText(0)).toBe('Picking up the saved search… 0 s');
    expect(pickingUpText(806_200)).toBe('Picking up the saved search… 13 min 26 s');
    expect(PICKING_UP_NOTE).toBe(
      'This usually takes only a few seconds. In the later moves of a big solve it can occasionally take 2 to 10 minutes or more.',
    );
    // before there are estimates: said in the text
    const early = performanceHtml(
      forecast({ depth: 3, levelMs: [0, 1, 4], growth: null, estimates: [] }),
      false,
      0,
      3_000,
    );
    expect(early).toContain(
      'Searched 3 moves deep; move 3 took under a second. Picking up the saved search… 3 s',
    );
    expect(early).not.toContain('<table');
  });

  it('leaves out the fifth move that is the move under way', () => {
    const html = performanceHtml(
      forecast({
        depth: 39,
        levelMs: Array.from({ length: 39 }, () => 190_000),
        estimates: [
          { depth: 40, levelMs: 250_800, fromNowMs: 250_800 },
          { depth: 45, levelMs: 1_000_000, fromNowMs: 3_000_000 },
        ],
      }),
      false,
    );
    const depths = [...html.matchAll(/data-depth="(\d+)"/g)].map((m) => Number(m[1]));
    expect(depths).toEqual([39, 40, 45]);
  });

  it('says when there is too little to go on', () => {
    const early = performanceHtml(
      forecast({ depth: 3, levelMs: [0, 1, 4], growth: null, estimates: [] }),
      false,
    );
    expect(early).toContain('Searched 3 moves deep; move 3 took under a second.');
    expect(early).toContain(
      'Estimates appear once a few moves in a row have taken measurable time.',
    );
    expect(early).not.toContain('<table');
  });

  it('says which moves were searched before the times were kept', () => {
    // a paused solve opened from a file without times, carried on one move
    const untimed: (number | null)[] = Array.from({ length: 36 }, () => null);
    const one = performanceHtml(
      forecast({ depth: 37, levelMs: [...untimed, 60_000], growth: null, estimates: [] }),
      false,
    );
    expect(one).toContain('Searched 37 moves deep; move 37 took 1 min.');
    expect(one).toContain('No times were kept for moves 1 to 36.');
    expect(one).toContain('Estimates start once two moves in a row have been timed.');
    // opened, before it is carried on
    const opened = performanceHtml(
      forecast({ depth: 36, levelMs: untimed, growth: null, estimates: [] }),
      true,
    );
    expect(opened).toContain('Searched 36 moves deep. No times were kept for moves 1 to 36.');
    // two timed: the estimates
    const two = performanceHtml(
      forecast({ depth: 38, levelMs: [...untimed, 60_000, 90_000] }),
      false,
    );
    expect(two).toContain('No times were kept for moves 1 to 36.');
    expect(two).toContain('<table');
  });
});
