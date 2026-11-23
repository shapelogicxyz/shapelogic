// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every placement of a Tilted Square puzzle's tray (decision 0045, stage G
// of docs/TILTED-SQUARE-PLAN.md): what the Placements dialog says of the
// boards the engine lists (tray.placements) and of the fit that counts a
// problem's assemblies on each (tray.fitPlacements, its events). DOM-free,
// for the unit tests.

import type { FitBoard, FitEvent, PlacedBoard, TraySettings } from '../../engine/api.generated.ts';

/** A fit as its events tell it (the status has the same fields). */
export type Fit = Omit<FitEvent, 'problem'> & { active: boolean };

/** The rows the dialog shows at first, and adds with each "Show more". */
export const ROWS_AT_ONCE = 40;

/** A count with thousands separated: "12,345". */
function counted(n: number): string {
  return String(Math.trunc(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

/** "1 assembly", "12 assemblies". */
export function assembliesText(n: number): string {
  return `${counted(n)} ${n === 1 ? 'assembly' : 'assemblies'}`;
}

/** "1 whole cell", "24 whole cells". */
export function wholeText(n: number): string {
  return `${String(n)} whole ${n === 1 ? 'cell' : 'cells'}`;
}

/** What a board's row says under its count of cells: which board it is. */
export function boardNote(index: number, inUse: number | null): string {
  const notes: string[] = [];
  if (index === 0) {
    notes.push('Most whole cells');
  }
  if (index === inUse) {
    notes.push('In use');
  }
  return notes.join(' · ');
}

/** How a board's row stands in a fit. */
export type FitState = 'none' | 'waiting' | 'trying' | 'fits' | 'empty' | 'partial' | 'untried';

/**
 * A board's state in a fit: not tried yet (`waiting` while the fit runs,
 * `untried` once it has ended), under way (`trying`), tried to the end
 * with assemblies (`fits`) or without (`empty`), or stopped on with some
 * found (`partial`). `none` without a fit.
 */
export function fitState(fit: Fit | null, index: number): FitState {
  const b = fit?.boards[index];
  if (fit === null || b === undefined) {
    return 'none';
  }
  if (b.counted) {
    return b.assemblies > 0 ? 'fits' : 'empty';
  }
  if (fit.active) {
    return index === fit.done ? 'trying' : 'waiting';
  }
  return b.assemblies > 0 ? 'partial' : 'untried';
}

/** What a board's row says of the fit. */
export function fitText(fit: Fit | null, index: number): string {
  const b: FitBoard | undefined = fit?.boards[index];
  switch (fitState(fit, index)) {
    case 'none':
      return '';
    case 'waiting':
      return 'Waiting';
    case 'trying':
      return (b?.assemblies ?? 0) > 0 ? `${counted(b?.assemblies ?? 0)} so far…` : 'Trying…';
    case 'fits':
      return assembliesText(b?.assemblies ?? 0);
    case 'empty':
      return 'No assembly';
    case 'partial':
      return `${counted(b?.assemblies ?? 0)} or more`;
    case 'untried':
      return 'Not tried';
  }
}

/** The line over the list while a fit runs and once it has ended. */
export function fitSummary(fit: Fit | null): string {
  if (fit === null) {
    return '';
  }
  const total = fit.total;
  const taking = fit.boards.filter((b) => b.assemblies > 0).length;
  const places = (n: number): string => `${String(n)} ${n === 1 ? 'placement' : 'placements'}`;
  if (fit.active) {
    return `Trying placement ${String(Math.min(fit.done + 1, total))} of ${String(total)}…`;
  }
  if (fit.failure !== null) {
    return `Stopped by an error: ${fit.failure}`;
  }
  const takes =
    taking === 0
      ? 'none takes the pieces'
      : `${String(taking)} ${taking === 1 ? 'takes' : 'take'} the pieces`;
  if (fit.stopped) {
    return `Stopped after ${String(fit.done)} of ${places(total)}: ${takes} so far`;
  }
  if (taking === 0) {
    return total === 1 ? 'The placement does not take the pieces' : 'No placement takes the pieces';
  }
  return total === 1
    ? 'The placement takes the pieces'
    : `${String(taking)} of ${places(total)} ${taking === 1 ? 'takes' : 'take'} the pieces`;
}

/** What the dialog says of the list itself. */
export function listText(count: number, complete: boolean): string {
  const all =
    count === 1
      ? 'The grid gives one board in this tray, wherever it lies.'
      : `The grid gives ${String(count)} boards in this tray, the most whole cells first. A board that is part of another is left out: it holds nothing the larger one cannot.`;
  return complete
    ? all
    : `${all} This tray gives more boards than could be looked at: the list is not complete.`;
}

/**
 * The tray settings that put the grid at a listed board: the first is the
 * best placement, any other a shift of its own.
 */
export function useSettings(board: PlacedBoard, index: number): TraySettings {
  return index === 0
    ? { placement: 'best' }
    : { placement: 'custom', shiftX: board.shiftX, shiftY: board.shiftY };
}
