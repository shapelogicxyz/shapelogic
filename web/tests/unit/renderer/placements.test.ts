// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Placements dialog's words (decision 0045, stage G of
// docs/TILTED-SQUARE-PLAN.md): what a board's row and the line over the
// list say of a fit as it runs and once it has ended, and the tray settings
// that put the grid at a board.

import { describe, expect, it } from 'vitest';

import type { FitBoard } from '../../../src/engine/api.generated.ts';
import {
  assembliesText,
  boardNote,
  fitState,
  fitSummary,
  fitText,
  listText,
  useSettings,
  wholeText,
  type Fit,
} from '../../../src/renderer/state/placements.ts';

/** A board of a fit: counted with n assemblies, or not counted with n found so far. */
function board(counted: boolean, assemblies: number): FitBoard {
  return { shiftX: 0.1, shiftY: 0.2, whole: 24, counted, assemblies };
}

function fit(
  boards: FitBoard[],
  change: Partial<Omit<Fit, 'boards' | 'total' | 'done'>> = {},
): Fit {
  return {
    active: false,
    complete: true,
    total: boards.length,
    done: boards.filter((b) => b.counted).length,
    current: 0,
    stopped: false,
    failure: null,
    ...change,
    boards,
  };
}

describe('a board of the list', () => {
  it('names its cells and which board it is', () => {
    expect(wholeText(24)).toBe('24 whole cells');
    expect(wholeText(1)).toBe('1 whole cell');
    expect(boardNote(0, 0)).toBe('Most whole cells · In use');
    expect(boardNote(0, 3)).toBe('Most whole cells');
    expect(boardNote(3, 3)).toBe('In use');
    expect(boardNote(2, null)).toBe('');
  });

  it('counts assemblies in words, thousands separated', () => {
    expect(assembliesText(1)).toBe('1 assembly');
    expect(assembliesText(2)).toBe('2 assemblies');
    expect(assembliesText(1234567)).toBe('1,234,567 assemblies');
  });

  it('says of the list how many boards there are and whether that is all', () => {
    expect(listText(1, true)).toBe('The grid gives one board in this tray, wherever it lies.');
    expect(listText(6, true)).toContain('6 boards');
    expect(listText(6, true)).not.toContain('not complete');
    expect(listText(6, false)).toContain('the list is not complete');
  });

  it('is used as the best placement when it is the first, by its shift otherwise', () => {
    const b = { shiftX: 0.25, shiftY: -0.5, whole: 24 };
    expect(useSettings(b, 0)).toEqual({ placement: 'best' });
    expect(useSettings(b, 4)).toEqual({ placement: 'custom', shiftX: 0.25, shiftY: -0.5 });
  });
});

describe('a fit of the placements', () => {
  it('says nothing before one has run', () => {
    expect(fitState(null, 0)).toBe('none');
    expect(fitText(null, 0)).toBe('');
    expect(fitSummary(null)).toBe('');
  });

  it('tells each board while it runs: counted, under way, waiting', () => {
    const f = fit([board(true, 0), board(true, 3), board(false, 0), board(false, 0)], {
      active: true,
    });
    expect([0, 1, 2, 3].map((k) => fitState(f, k))).toEqual(['empty', 'fits', 'trying', 'waiting']);
    expect([0, 1, 2, 3].map((k) => fitText(f, k))).toEqual([
      'No assembly',
      '3 assemblies',
      'Trying…',
      'Waiting',
    ]);
    expect(fitSummary(f)).toBe('Trying placement 3 of 4…');
    // the board under way has found some already
    const g = fit([board(false, 1200), board(false, 0)], { active: true });
    expect(fitText(g, 0)).toBe('1,200 so far…');
    expect(fitSummary(g)).toBe('Trying placement 1 of 2…');
  });

  it('tells how it ended: through', () => {
    const none = fit([board(true, 0), board(true, 0)]);
    expect(fitSummary(none)).toBe('No placement takes the pieces');
    const one = fit([board(true, 0), board(true, 1), board(true, 0)]);
    expect(fitSummary(one)).toBe('1 of 3 placements takes the pieces');
    const two = fit([board(true, 2), board(true, 1), board(true, 0)]);
    expect(fitSummary(two)).toBe('2 of 3 placements take the pieces');
    // a straight tray has the one board
    expect(fitSummary(fit([board(true, 4)]))).toBe('The placement takes the pieces');
    expect(fitSummary(fit([board(true, 0)]))).toBe('The placement does not take the pieces');
  });

  it('tells how it ended: stopped, or by an error', () => {
    const f = fit([board(true, 2), board(false, 7), board(false, 0)], { stopped: true });
    expect([0, 1, 2].map((k) => fitState(f, k))).toEqual(['fits', 'partial', 'untried']);
    expect([0, 1, 2].map((k) => fitText(f, k))).toEqual(['2 assemblies', '7 or more', 'Not tried']);
    expect(fitSummary(f)).toBe('Stopped after 1 of 3 placements: 2 take the pieces so far');
    const early = fit([board(false, 0), board(false, 0)], { stopped: true });
    expect(fitSummary(early)).toBe('Stopped after 0 of 2 placements: none takes the pieces so far');
    const failed = fit([board(false, 0)], { stopped: true, failure: 'the solve failed' });
    expect(fitSummary(failed)).toBe('Stopped by an error: the solve failed');
  });
});
