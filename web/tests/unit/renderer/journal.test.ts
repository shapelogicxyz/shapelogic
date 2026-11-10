// SPDX-License-Identifier: GPL-3.0-or-later
//
// The crash recovery journal's DOM-free parts and the Search Options'
// defaults: when the next save is due (solver/cadence.ts), that the journal
// is no option of a problem or of the settings, what a problem
// that stored no options starts with (the settings dialog's Search Options
// page, over shell/prefs.ts), the limits the Solver starts with, and the
// settings that are kept across sessions.

import { afterEach, describe, expect, it } from 'vitest';

import { DEFAULTS, LIMIT_MAX, setPrefs, validPrefs } from '../../../src/renderer/shell/prefs.ts';
import { COST_FACTOR, FLOOR_MS, nextWait } from '../../../src/renderer/solver/cadence.ts';
import { LABEL, TIP } from '../../../src/renderer/solver/option-text.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_OPTIONS,
  DEFAULT_SLIDING,
  DEFAULT_STACKING,
  defaultLimits,
  defaultsOf,
  readOptions,
  solveOptions,
  writeOptions,
} from '../../../src/renderer/solver/settings.ts';

afterEach(() => {
  setPrefs({ ...DEFAULTS });
});

describe('the journal’s saves', () => {
  it('are never closer than the floor, and cost at most one part in fifty', () => {
    expect(FLOOR_MS).toBe(60_000);
    expect(COST_FACTOR).toBe(50);
    // a cheap save: the floor
    expect(nextWait(0)).toBe(FLOOR_MS);
    expect(nextWait(200)).toBe(FLOOR_MS);
    expect(nextWait(1200)).toBe(FLOOR_MS);
    // a dear one (slow take-aparts, a large solution list): spaced by its cost
    expect(nextWait(2000)).toBe(100_000);
    expect(nextWait(30_000)).toBe(25 * 60_000);
    // the tests' floor
    expect(nextWait(10, 1000)).toBe(1000);
    expect(nextWait(100, 1000)).toBe(5000);
    expect(nextWait(-5, 1000)).toBe(1000);
  });

  it('are not an option: no box, no default, and the stored Autosave box is only carried', () => {
    expect(Object.keys(LABEL)).not.toContain('autosave');
    expect(Object.keys(TIP)).not.toContain('autosave');
    expect(Object.keys(DEFAULTS)).not.toContain('optJournal');
    // what a problem stored for the original's box is read and written back as it was
    expect(readOptions({ autosave: false }).autosave).toBe(false);
    expect(writeOptions(readOptions({ autosave: false }), {}).autosave).toBe(false);
    expect(readOptions({}).autosave).toBe(true);
    // and plays no part in a solve: a stacking search always saves itself (the engine's default)
    for (const stored of [{ autosave: false }, { autosave: true }, {}]) {
      const o = readOptions(stored, undefined, 'stacking');
      expect(solveOptions(o, defaultLimits(), 'stacking', 'panex').stacking).toEqual({
        solver: 'panex',
      });
    }
  });
});

describe('the Search Options’ defaults', () => {
  it('are the shipped ones until the settings say otherwise', () => {
    expect(defaultsOf('brick')).toEqual(DEFAULT_OPTIONS);
    expect(defaultsOf('sliding')).toEqual(DEFAULT_SLIDING);
    expect(defaultsOf('stacking')).toEqual(DEFAULT_STACKING);
    expect(defaultLimits()).toEqual(DEFAULT_LIMITS);
  });

  it('start a problem that stored none, and leave one that stored its own', () => {
    setPrefs({
      optKeepMirrors: true,
      optStrictColors: false,
      optKeepAssemblies: true,
      solverType: 'Fast',
    });
    const fresh = readOptions({});
    expect(fresh.keepMirrors).toBe(true);
    expect(fresh.strictColors).toBe(false);
    expect(fresh.keepAssemblies).toBe(true);
    expect(fresh.solver).toBe('crowell');
    // what the problem stored stays; what it did not takes the default
    const stored = readOptions({ keepMirrors: false, solverType: 'classic' });
    expect(stored.keepMirrors).toBe(false);
    expect(stored.solver).toBe('classic');
    expect(stored.strictColors).toBe(false);
  });

  it('apply to the puzzle types that have the option', () => {
    setPrefs({
      optNestedSlides: true,
      optPartialNested: true,
      optAnyOrientation: true,
      optVerticalMoves: true,
      optKeepRotations: true,
      optJustLevels: true,
    });
    const sliding = readOptions({}, undefined, 'sliding');
    expect(sliding.nestedSlides).toBe(true);
    expect(sliding.partialNested).toBe(true);
    expect(sliding.anyOrientation).toBe(true);
    expect(sliding.verticalMoves).toBe(true);
    // a sliding puzzle has no Just Levels and no symmetry options
    expect(sliding.justLevels).toBe(false);
    expect(sliding.keepRotations).toBe(false);
    // a stacking one keeps Just Levels, and none of the sliding ones
    const stacking = readOptions({}, undefined, 'stacking');
    expect(stacking.justLevels).toBe(true);
    expect(stacking.nestedSlides).toBe(false);
    // a brick one: Just Levels takes apart
    const brick = readOptions({});
    expect(brick.justLevels).toBe(true);
    expect(brick.disassemble).toBe(true);
    expect(brick.keepRotations).toBe(true);
    expect(brick.nestedSlides).toBe(true); // stored with sliding problems only, and ignored here
    expect(solveOptions(brick, defaultLimits()).sliding).toBeUndefined();
  });

  it('defaults that rule each other out give way as the boxes do', () => {
    setPrefs({
      optJustCount: true,
      optJustLevels: true,
      optKeepRotations: true,
      optDeepSymmetry: true,
    });
    const o = readOptions({});
    expect(o.justCount).toBe(true);
    expect(o.justLevels).toBe(false);
    expect(o.keepRotations).toBe(true);
    expect(o.deepSymmetry).toBe(false);
  });

  it('set the limits the Solver starts with', () => {
    setPrefs({ optKeepEach: 5, optKeepAtMost: 0 });
    expect(defaultLimits()).toEqual({ keepEach: 5, keepAtMost: 0 });
    expect(solveOptions(readOptions({}), defaultLimits()).keepEach).toBe(5);
    expect(solveOptions(readOptions({}), defaultLimits()).keepAtMost).toBe(0);
  });

  it('are kept across sessions when well formed', () => {
    expect(
      validPrefs({
        optJournal: false, // a setting of an earlier build: dropped
        optJustCount: true,
        optKeepEach: 3,
        optKeepAtMost: 0,
        optStrictColors: 'yes',
      }),
    ).toEqual({ optJustCount: true, optKeepEach: 3, optKeepAtMost: 0 });
    // keep every n-th is at least 1; the limits have a ceiling
    expect(validPrefs({ optKeepEach: 0 })).toEqual({});
    expect(validPrefs({ optKeepEach: LIMIT_MAX + 1 })).toEqual({});
    expect(validPrefs({ optKeepAtMost: LIMIT_MAX + 1 })).toEqual({});
    expect(validPrefs({ optKeepAtMost: 2.5 })).toEqual({});
  });
});
