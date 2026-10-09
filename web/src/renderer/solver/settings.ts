// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's search options over a problem's stored SolverSettings (the
// original's Solver tab check boxes, saved with each problem; decision
// 0012, "Names that stay": the stored names are the file's, SolveOptions
// uses the API's, types.json has the mapping). DOM-free, for the unit
// tests. Ported from the original's mainwindow_solve.cpp:
// `updateSolverOptionCheckboxes` (which boxes force or grey out which),
// `cb_SolverOptions` (Just Levels newly checked turns Check Rotations
// off), `storeSolverOptions` / `applySolverOptions` (the names, the
// solver type and sort-by values, other keys kept) and the layout's
// defaults (Autosave and Sort by moves checked, the Display Limit 100 and
// Keep Each 1, which the original did not save with the problem).

import type { SolveOptions, Solver, SolverSettings, SortBy } from '../../engine/api.generated.ts';

/** The check boxes this version shows, by their stored names. */
export type BoxName =
  | 'disassemble'
  | 'checkRotations'
  | 'justCount'
  | 'justLevels'
  | 'deepSymmetry'
  | 'keepMirrors'
  | 'keepRotations'
  | 'strictColors'
  | 'autosave';

/** The options as the Solver card shows and edits them. */
export interface SearchOptions {
  disassemble: boolean;
  checkRotations: boolean;
  justCount: boolean;
  justLevels: boolean;
  deepSymmetry: boolean;
  keepMirrors: boolean;
  keepRotations: boolean;
  strictColors: boolean;
  autosave: boolean;
  solver: Solver;
  sortBy: SortBy;
}

/** The defaults of a problem that stored none (the original's widgets as made). */
export const DEFAULT_OPTIONS: Readonly<SearchOptions> = {
  disassemble: false,
  checkRotations: false,
  justCount: false,
  justLevels: false,
  deepSymmetry: false,
  keepMirrors: false,
  keepRotations: false,
  strictColors: false,
  autosave: true,
  solver: 'classic',
  sortBy: 'moves',
};

const SOLVERS: readonly Solver[] = ['classic', 'crowell', 'bt2'];
const SORTS: readonly SortBy[] = ['unsorted', 'moves', 'level', 'rotations'];

/** The sort-by choices, the original's labels (its sortMethod menu). */
export const SORT_CHOICES: readonly { value: SortBy; label: string; needsDisassembly: boolean }[] =
  [
    { value: 'unsorted', label: 'Unsorted', needsDisassembly: false },
    { value: 'moves', label: 'Moves for complete disassembly', needsDisassembly: true },
    { value: 'level', label: 'Level', needsDisassembly: true },
    { value: 'rotations', label: 'Rotations', needsDisassembly: true },
  ];

/**
 * The options a problem stored (applySolverOptions): a missing setting
 * keeps the default, an unknown solver type or sort the default too.
 */
export function readOptions(stored: SolverSettings, base = DEFAULT_OPTIONS): SearchOptions {
  const o: SearchOptions = { ...base };
  const boxes: BoxName[] = [
    'disassemble',
    'checkRotations',
    'justCount',
    'justLevels',
    'deepSymmetry',
    'keepMirrors',
    'keepRotations',
    'strictColors',
    'autosave',
  ];
  for (const b of boxes) {
    const v = stored[b];
    if (typeof v === 'boolean') {
      o[b] = v;
    }
  }
  const st = stored.solverType;
  if (typeof st === 'string' && (SOLVERS as readonly string[]).includes(st)) {
    o.solver = st as Solver;
  }
  const sb = stored.sortBy;
  if (typeof sb === 'string' && (SORTS as readonly string[]).includes(sb)) {
    o.sortBy = sb as SortBy;
  }
  return normalize(o, null).options;
}

/**
 * The settings to store for options (storeSolverOptions): the boxes and
 * choices this version shows, over what was stored (the boxes it does
 * not show, High Memory and the sliding ones, and settings of other
 * versions are kept as they were).
 */
export function writeOptions(o: SearchOptions, stored: SolverSettings): SolverSettings {
  return {
    ...stored,
    disassemble: o.disassemble,
    checkRotations: o.checkRotations,
    justCount: o.justCount,
    justLevels: o.justLevels,
    deepSymmetry: o.deepSymmetry,
    keepMirrors: o.keepMirrors,
    keepRotations: o.keepRotations,
    strictColors: o.strictColors,
    autosave: o.autosave,
    solverType: o.solver,
    sortBy: o.sortBy,
  };
}

/** Options with the original's rules applied, and the boxes it greys out. */
export interface Normalized {
  options: SearchOptions;
  disabled: ReadonlySet<BoxName>;
}

/**
 * The original's rules (updateSolverOptionCheckboxes, cb_SolverOptions):
 * Just Count drops the take-apart and Just Levels; Just Levels takes apart
 * and drops Just Count; Check Rotations needs the take-apart (and goes off
 * when Just Levels is newly checked: `changed`); Keep Rotated turns the
 * rotation filter off, so the thorough check and Keep Mirror go with it;
 * the thorough check rules Keep Rotated out. The sort by moves, level or
 * rotations needs the take-apart (the mockup's rule): without it the solve
 * keeps them unsorted.
 */
export function normalize(input: SearchOptions, changed: BoxName | null): Normalized {
  const o = { ...input };
  const disabled = new Set<BoxName>();
  if (changed === 'justLevels' && o.justLevels) {
    o.checkRotations = false;
  }
  if (o.justCount) {
    o.justLevels = false;
    o.disassemble = false;
    o.checkRotations = false;
    disabled.add('justLevels');
    disabled.add('disassemble');
    disabled.add('checkRotations');
  } else if (o.justLevels) {
    o.justCount = false;
    o.disassemble = true;
    disabled.add('justCount');
    disabled.add('disassemble');
  } else if (!o.disassemble) {
    o.checkRotations = false;
    disabled.add('checkRotations');
  }
  if (o.keepRotations) {
    o.deepSymmetry = false;
    o.keepMirrors = false;
    disabled.add('deepSymmetry');
    disabled.add('keepMirrors');
  } else if (o.deepSymmetry) {
    o.keepRotations = false;
    disabled.add('keepRotations');
  }
  return { options: o, disabled };
}

/** The sort a solve uses: by moves, level or rotations only when it takes apart. */
export function effectiveSort(o: SearchOptions): SortBy {
  const needs = SORT_CHOICES.find((c) => c.value === o.sortBy)?.needsDisassembly ?? false;
  return needs && !o.disassemble ? 'unsorted' : o.sortBy;
}

/** How many solutions a solve keeps (the original's Display Limit and Keep Each, not stored). */
export interface Limits {
  /** Keep at most this many (0: all). */
  keepAtMost: number;
  /** Keep every n-th (1: each). */
  keepEach: number;
}

/** The defaults of the original's two fields. */
export const DEFAULT_LIMITS: Readonly<Limits> = { keepAtMost: 100, keepEach: 1 };

/**
 * The SolveOptions of a solve (solve.start, prepare, continue): the
 * stored names mapped to the API's (deepSymmetry is completeRotations,
 * strictColors strictColours); the threads are left to the engine's
 * setting (the settings dialog's worker threads).
 */
export function solveOptions(o: SearchOptions, limits: Limits): SolveOptions {
  return {
    solver: o.solver,
    disassemble: o.disassemble,
    checkRotations: o.checkRotations,
    justCount: o.justCount,
    justLevels: o.justLevels,
    keepMirrors: o.keepMirrors,
    keepRotations: o.keepRotations,
    completeRotations: o.deepSymmetry,
    strictColours: o.strictColors,
    sortBy: effectiveSort(o),
    keepAtMost: Math.max(0, Math.round(limits.keepAtMost)),
    keepEach: Math.max(1, Math.round(limits.keepEach)),
  };
}

/** Whether two option sets store the same. */
export function sameOptions(a: SearchOptions, b: SearchOptions): boolean {
  return (Object.keys(a) as (keyof SearchOptions)[]).every((k) => a[k] === b[k]);
}
