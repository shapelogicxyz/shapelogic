// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's Performance Analysis (renderer/solver/performance.ts): the
// times as a person says them, and the panel from the engine's forecast.

import { describe, expect, it } from 'vitest';

import type { SlideForecast } from '../../../src/engine/api.generated.ts';
import { durationText, performanceHtml } from '../../../src/renderer/solver/performance.ts';

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

  it('lists every fifth move to come with that move’s time and the time until it', () => {
    const html = performanceHtml(forecast({ runningMs: 252_000 }), false);
    expect(html.startsWith('<b>Performance Analysis</b>')).toBe(true);
    expect(html).toContain('Searched 36 moves deep; move 36 took 3 min 10 s.');
    expect(html).toContain('Move 37 has run 4 min 12 s.');
    expect(html).toContain('about 1.32× as long as the one before');
    expect(html).toContain(
      '<tr data-depth="40"><td>40</td><td>9 min 37 s</td><td>25 min</td></tr>',
    );
    expect(html).toContain('<td>over 1,000 years</td>');
    expect(html).toContain('>From now</th>');
    // paused: the time counts from when it is continued
    expect(performanceHtml(forecast({}), true)).toContain('>Once continued</th>');
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
