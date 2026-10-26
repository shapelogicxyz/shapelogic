// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's time counters as words (Time used, Time left): platform-free,
// so the unit tests read them.

const MINUTE = 60_000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;
const YEAR = 365 * DAY;

/**
 * A time span in milliseconds for the counters: whole milliseconds under a
 * second ("1 millisecond", "250 milliseconds"), whole seconds under a minute
 * ("1 second", "42 seconds"), then minutes, hours, days or years to a tenth
 * ("5.5 minutes", "2 hours", "1 day").
 */
export function formatSpan(ms: number): string {
  const t = Math.max(0, Math.floor(ms));
  if (t < 1000) {
    return `${String(t)} millisecond${t === 1 ? '' : 's'}`;
  }
  if (t < 60_000) {
    const s = Math.floor(t / 1000);
    return `${String(s)} second${s === 1 ? '' : 's'}`;
  }
  const [size, name] =
    t < HOUR
      ? [MINUTE, 'minute']
      : t < DAY
        ? [HOUR, 'hour']
        : t < YEAR
          ? [DAY, 'day']
          : [YEAR, 'year'];
  // tenths rounded down, so 59.99 minutes is not "60 minutes"
  const tenths = Math.floor((t * 10) / size);
  const n = tenths % 10 === 0 ? String(tenths / 10) : (tenths / 10).toFixed(1);
  return `${n} ${name}${tenths === 10 ? '' : 's'}`;
}
