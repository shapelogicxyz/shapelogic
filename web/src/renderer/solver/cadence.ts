// SPDX-License-Identifier: GPL-3.0-or-later
//
// When the crash recovery journal saves a running solve (autosave.ts): often
// while a save is cheap, less often as it gets dear. The same rule as the
// engine's own searches keep to (core/shapelogic/base/save_cadence.h):
// the first save a floor after the solve starts; each next one when the
// time since the last is at least the floor and at least COST_FACTOR times
// what the last save took (its pause, the take-aparts it waited for, the
// write, the restart), so that saving costs a solve no more than about 2%
// of its time. DOM-free, for the unit tests.

/** Saving takes at most one part in this many of a solve's time. */
export const COST_FACTOR = 50;

/** Saves are never closer than this, in milliseconds. */
export const FLOOR_MS = 60_000;

/** The wait before the next save, after one that took `costMs`. */
export function nextWait(costMs: number, floorMs: number = FLOOR_MS): number {
  return Math.max(floorMs, Math.round(Math.max(0, costMs) * COST_FACTOR));
}
