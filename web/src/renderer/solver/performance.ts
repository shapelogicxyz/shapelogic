// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's Performance Analysis, under the empty solutions card of a
// sliding puzzle whose solve is under way or paused: the rate the times of
// the moves deep of the slide search grow at, and a table of the move last
// searched (its time), the move under way (how long it has run and how long
// it has left, counted on by the card each second, below zero once it runs
// past the estimate) and how long the moves to come will take, every fifth
// move past it (58 moves deep: 60 to 110; 59 deep: 65 to 110, move 60 being
// the one under way; the engine's SlideForecast: solve.progress while the
// run is on, the problem's detail once it is paused; it is worked out again
// as each move ends). It goes when the solve is through and the solution
// shows.
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
 * A time that counts: whole seconds, "0 s" under one, and a time gone by
 * the estimate with a minus (the Remaining of the move under way).
 */
export function counterText(ms: number): string {
  const s = Math.round(ms / SECOND);
  if (s < 0) {
    return `−${counterText(-ms)}`;
  }
  return s === 0 ? '0 s' : durationText(s * SECOND);
}

/**
 * How long the move under way should take: the last move's time times the
 * growth, held to the engine's longest time; null with no growth.
 */
export function nextMoveMs(f: SlideForecast): number | null {
  const last = f.levelMs.at(-1);
  if (f.growth === null || last === undefined || last === null) {
    return null;
  }
  return Math.min(last * f.growth, MAX_MS);
}

/**
 * The move under way's Time and Remaining cells, `elapsedMs` after the
 * forecast came (the card counts them on between progress reports).
 */
export function liveCells(f: SlideForecast, elapsedMs: number): [string, string] {
  const run = f.runningMs + elapsedMs;
  const next = nextMoveMs(f);
  return [counterText(run), next === null ? '—' : counterText(next - run)];
}

/**
 * The panel's contents (its heading included): the growth, and the table:
 * the move last searched, the move under way and every fifth move to come;
 * `paused` says the times from now count from when the solve is continued,
 * `elapsedMs` how long since the forecast came (while it runs).
 */
export function performanceHtml(f: SlideForecast, paused: boolean, elapsedMs = 0): string {
  // the moves searched before the times were kept (a paused solve opened
  // from a file without them): no time, and no part in the estimates
  const firstTimed = f.levelMs.findIndex((ms) => ms !== null);
  const untimed = firstTimed < 0 ? f.levelMs.length : firstTimed;
  const notKept =
    untimed === 0
      ? ''
      : ` No times were kept for ${untimed === 1 ? 'move 1' : `moves 1 to ${String(untimed)}`}.`;
  const last = f.levelMs.at(-1);
  if (f.growth === null || f.estimates.length === 0) {
    const done =
      f.depth === 0
        ? 'The search has not finished a move yet.'
        : `Searched ${unit(f.depth, 'move')} deep${last === undefined || last === null ? '' : `; move ${String(f.depth)} took ${durationText(last)}`}.`;
    const running =
      f.runningMs > 0 ? ` Move ${String(f.depth + 1)} has run ${durationText(f.runningMs)}.` : '';
    const why =
      untimed > 0
        ? ' Estimates start once two moves in a row have been timed.'
        : ' Estimates appear once a few moves in a row have taken measurable time.';
    return `<b>Performance Analysis</b>${done}${notKept}${running}${why}`;
  }
  const growth = `Each move takes about ${f.growth.toFixed(2)}× as long as the one before.`;
  const from = paused ? 'Once continued' : 'Remaining';
  const row = (cls: string, depth: number, time: string, left: string): string =>
    `<tr${cls === '' ? '' : ` class="${cls}"`} data-depth="${String(depth)}"><td>${String(depth)}</td><td>${time}</td><td>${left}</td></tr>`;
  const done =
    f.depth > 0 && last !== undefined && last !== null
      ? row('slv-perf-done', f.depth, durationText(last), 'Complete')
      : '';
  const [time, left] = liveCells(f, elapsedMs);
  const now = row(`slv-perf-now${left.startsWith('−') ? ' over' : ''}`, f.depth + 1, time, left);
  // the move under way is on its own row: not again among every fifth
  const rows = f.estimates
    .filter((e) => e.depth > f.depth + 1)
    .map((e) => row('', e.depth, durationText(e.levelMs), durationText(e.fromNowMs)))
    .join('');
  return (
    `<b>Performance Analysis</b>${growth}${notKept}` +
    `<table class="slv-perf-t"><thead><tr><th title="How many moves deep">Move</th><th title="How long that one move will take">Time</th><th title="How long until the search is that deep">${from}</th></tr></thead><tbody>${done}${now}${rows}</tbody></table>`
  );
}
