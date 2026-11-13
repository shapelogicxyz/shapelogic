// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's Performance Analysis, under the empty solutions card of a
// sliding puzzle whose solve is under way or paused: how long each move
// deep of the slide search took, the rate those times grow at, and from it
// how long the moves to come will take, every fifth move to 100 (the
// engine's SlideForecast: solve.progress while the run is on, the
// problem's detail once it is paused; it is worked out again as each move
// ends). It goes when the solve is through and the solution shows.
// DOM-free (HTML strings of numbers and fixed words, nothing to escape),
// for the unit tests.

import type { SlideForecast } from '../../engine/api.generated.ts';

/** The engine's longest time: a thousand years (sliding::kForecastMaxMs). */
const MAX_MS = 31_557_600_000_000;

const SECOND = 1000;
const MINUTE = 60 * SECOND;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;
const YEAR = 365.25 * DAY;

function unit(n: number, one: string, many = `${one}s`): string {
  return `${n.toLocaleString('en-US')} ${n === 1 ? one : many}`;
}

/** A time as a person says it: 12 s, 4 min 12 s, 3 h 20 min, 5 days 2 h, 3.4 years. */
export function durationText(ms: number): string {
  if (ms >= MAX_MS) {
    return 'over 1,000 years';
  }
  if (ms < SECOND) {
    return 'under a second';
  }
  // what rounds to a minute is said in minutes
  if (ms < MINUTE - SECOND / 2) {
    return `${String(Math.round(ms / SECOND))} s`;
  }
  if (ms < HOUR) {
    const m = Math.floor(ms / MINUTE);
    const s = Math.round((ms - m * MINUTE) / SECOND);
    return s === 0 || s === 60
      ? `${String(s === 60 ? m + 1 : m)} min`
      : `${String(m)} min ${String(s)} s`;
  }
  if (ms < DAY) {
    const h = Math.floor(ms / HOUR);
    const m = Math.round((ms - h * HOUR) / MINUTE);
    return m === 0 || m === 60
      ? `${String(m === 60 ? h + 1 : h)} h`
      : `${String(h)} h ${String(m)} min`;
  }
  if (ms < 100 * DAY) {
    const d = Math.floor(ms / DAY);
    const h = Math.round((ms - d * DAY) / HOUR);
    return h === 0 || h === 24
      ? unit(h === 24 ? d + 1 : d, 'day')
      : `${unit(d, 'day')} ${String(h)} h`;
  }
  if (ms < YEAR) {
    return unit(Math.round(ms / DAY), 'day');
  }
  const y = ms / YEAR;
  return `${y < 10 ? y.toFixed(1) : Math.round(y).toLocaleString('en-US')} years`;
}

/**
 * The panel's contents (its heading included): what the search has done,
 * the growth, and the table of estimates; `paused` says the times from now
 * count from when the solve is continued.
 */
export function performanceHtml(f: SlideForecast, paused: boolean): string {
  const last = f.levelMs.at(-1);
  const done =
    f.depth === 0
      ? 'The search has not finished a move yet.'
      : `Searched ${unit(f.depth, 'move')} deep${last === undefined || last === null ? '' : `; move ${String(f.depth)} took ${durationText(last)}`}.`;
  // the moves searched before the times were kept (a paused solve opened
  // from a file without them): no time, and no part in the estimates
  const firstTimed = f.levelMs.findIndex((ms) => ms !== null);
  const untimed = firstTimed < 0 ? f.levelMs.length : firstTimed;
  const notKept =
    untimed === 0
      ? ''
      : ` No times were kept for ${untimed === 1 ? 'move 1' : `moves 1 to ${String(untimed)}`}.`;
  const running =
    f.runningMs > 0 ? ` Move ${String(f.depth + 1)} has run ${durationText(f.runningMs)}.` : '';
  if (f.growth === null || f.estimates.length === 0) {
    const why =
      f.depth >= 100
        ? ''
        : untimed > 0
          ? ' Estimates start once two moves in a row have been timed.'
          : ' Estimates appear once a few moves in a row have taken measurable time.';
    return `<b>Performance Analysis</b>${done}${notKept}${running}${why}`;
  }
  const growth = `Each move takes about ${f.growth.toFixed(2)}× as long as the one before, from the last moves; worked out again as each move ends.`;
  const from = paused ? 'Once continued' : 'From now';
  const rows = f.estimates
    .map(
      (e) =>
        `<tr data-depth="${String(e.depth)}"><td>${String(e.depth)}</td><td>${durationText(e.levelMs)}</td><td>${durationText(e.fromNowMs)}</td></tr>`,
    )
    .join('');
  return (
    `<b>Performance Analysis</b>${done}${notKept}${running} ${growth}` +
    `<table class="slv-perf-t"><thead><tr><th title="How many moves deep">Moves deep</th><th title="How long that one move will take">That move</th><th title="How long until the search is that deep">${from}</th></tr></thead><tbody>${rows}</tbody></table>`
  );
}
