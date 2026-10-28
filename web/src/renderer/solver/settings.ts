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
// Keep Each 1, which the original did not save with the problem). On a
// sliding puzzle (docs/webui-spec.md, section 8.3) the original showed
// other boxes: Allow Nested Slides and Allow Partially Nested (only with
// nested slides) in the places of Keep Mirror, Keep Rotated and Deep
// Symmetry Check, one solver in the Solver Type list (the Sliding Puzzle
// Solver, the original's full search; its Fast and Deep solvers are not
// offered, and the solver is not stored), Find Solutions checked at first,
// and none of Just
// Levels, Check Rotations, Strict Colours and the symmetry options; and
// Allow Vertical Moves, which the original (trays one layer deep) did not
// have, off by default so that a piece stays in its layer. On a
// stacking puzzle (section 9.3): Find Solutions checked at first
// (stackingDisasmDefaulted), Just Levels kept (the search ignores it), no
// Check Rotations, Strict Colours or symmetry options, and one solver by
// the problem's board (syncSolverTypeMenu: the Panex Solver for Panex
// columns, else the Stacking Solver), which the original did not store.
// The original's Enable High Memory of both is the Search memory setting
// (shell/settings.ts) here: a problem's stored highMemory is kept as it
// was, and plays no part.

import type {
  Grid,
  SolveOptions,
  Solver,
  SolverSettings,
  SortBy,
  StackingSolver,
} from '../../engine/api.generated.ts';
import { prefs } from '../shell/prefs.ts';

/** The puzzles whose Solver shows its own options: brick (every assembling grid), sliding, stacking. */
export type Family = 'brick' | 'sliding' | 'stacking';

/** The family of a grid's puzzles. */
export function familyOf(grid: Grid | undefined): Family {
  return grid === 'sliding' ? 'sliding' : grid === 'stacking' ? 'stacking' : 'brick';
}

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
  | 'nestedSlides'
  | 'partialNested'
  | 'verticalMoves'
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
  /** Sliding: a piece may carry what is nested in its outline (Allow Nested Slides). */
  nestedSlides: boolean;
  /** Sliding: and what is partly nested in it (Allow Partially Nested; needs nestedSlides). */
  partialNested: boolean;
  /** Sliding: on a tray of several layers a piece may also move up and down (Allow Vertical Moves). */
  verticalMoves: boolean;
  autosave: boolean;
  solver: Solver;
  sortBy: SortBy;
}

/** The defaults of a problem that stored none (the original's widgets as made, but Strict colors on). */
export const DEFAULT_OPTIONS: Readonly<SearchOptions> = {
  disassemble: false,
  checkRotations: false,
  justCount: false,
  justLevels: false,
  deepSymmetry: false,
  keepMirrors: false,
  keepRotations: false,
  strictColors: true,
  nestedSlides: false,
  partialNested: false,
  verticalMoves: false,
  autosave: true,
  solver: 'classic',
  sortBy: 'moves',
};

/** A sliding puzzle's defaults: Find Solutions checked (slidingDisasmDefaulted). */
export const DEFAULT_SLIDING: Readonly<SearchOptions> = {
  ...DEFAULT_OPTIONS,
  disassemble: true,
};

/** A stacking puzzle's defaults: Find Solutions checked (stackingDisasmDefaulted). */
export const DEFAULT_STACKING: Readonly<SearchOptions> = {
  ...DEFAULT_OPTIONS,
  disassemble: true,
};

/** The defaults of a family's problems; the brick ones' solver is the Default solver type setting's. */
export function defaultsOf(family: Family): Readonly<SearchOptions> {
  return family === 'sliding'
    ? DEFAULT_SLIDING
    : family === 'stacking'
      ? DEFAULT_STACKING
      : { ...DEFAULT_OPTIONS, solver: prefs.solverType === 'Fast' ? 'crowell' : 'classic' };
}

/** The boxes a sliding puzzle shows (updateSolverOptionCheckboxes' setVis). */
export const SLIDING_BOXES: ReadonlySet<BoxName> = new Set<BoxName>([
  'disassemble',
  'justCount',
  'nestedSlides',
  'partialNested',
  'verticalMoves',
  'autosave',
]);

/** The boxes only a sliding puzzle shows. */
export const SLIDING_ONLY: ReadonlySet<BoxName> = new Set<BoxName>([
  'nestedSlides',
  'partialNested',
  'verticalMoves',
]);

/** The boxes a stacking puzzle shows (updateSolverOptionCheckboxes' setVis). */
export const STACKING_BOXES: ReadonlySet<BoxName> = new Set<BoxName>([
  'disassemble',
  'justLevels',
  'justCount',
  'autosave',
]);

/**
 * The one solver the original offered a stacking problem
 * (syncSolverTypeMenu): the Panex Solver on a board of Panex columns,
 * else the Stacking Solver.
 */
export function stackingSolverOf(panexColumns: boolean): StackingSolver {
  return panexColumns ? 'panex' : 'stacking';
}

const SOLVERS: readonly Solver[] = ['classic', 'crowell'];
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
 * keeps the default, an unknown solver type or sort the default too. A
 * sliding or stacking puzzle has one solver, so its stored solver type
 * plays no part. On a grid without a take-apart (canDisassemble false) the
 * solve only assembles.
 */
export function readOptions(
  stored: SolverSettings,
  base?: Readonly<SearchOptions>,
  family: Family = 'brick',
  canDisassemble = true,
): SearchOptions {
  const o: SearchOptions = { ...(base ?? defaultsOf(family)) };
  const boxes: BoxName[] = [
    'disassemble',
    'checkRotations',
    'justCount',
    'justLevels',
    'deepSymmetry',
    'keepMirrors',
    'keepRotations',
    'strictColors',
    'nestedSlides',
    'partialNested',
    'verticalMoves',
    'autosave',
  ];
  for (const b of boxes) {
    const v = stored[b];
    if (typeof v === 'boolean') {
      o[b] = v;
    }
  }
  const st = stored.solverType;
  if (family === 'brick' && typeof st === 'string' && (SOLVERS as readonly string[]).includes(st)) {
    o.solver = st as Solver;
  }
  const sb = stored.sortBy;
  if (typeof sb === 'string' && (SORTS as readonly string[]).includes(sb)) {
    o.sortBy = sb as SortBy;
  }
  return normalize(o, null, family, canDisassemble).options;
}

/**
 * The settings to store for options (storeSolverOptions): the boxes and
 * choices this version shows, over what was stored (the boxes it does
 * not show, the stored High Memory and the sliding ones, and settings of other
 * versions are kept as they were).
 */
export function writeOptions(
  o: SearchOptions,
  stored: SolverSettings,
  family: Family = 'brick',
): SolverSettings {
  const sliding = family === 'sliding';
  const out: SolverSettings = {
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
    sortBy: o.sortBy,
  };
  // a stacking puzzle's one solver was not stored (storeSolverOptions), nor
  // is a sliding puzzle's (a solver type stored before is kept as it was)
  if (family === 'brick') {
    out.solverType = o.solver;
  }
  // the sliding boxes go with a sliding puzzle's problems only
  if (sliding) {
    out.nestedSlides = o.nestedSlides;
    out.partialNested = o.partialNested;
    out.verticalMoves = o.verticalMoves;
  }
  return out;
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
 * keeps them unsorted. A grid without a take-apart (canDisassemble false:
 * spheres, rhombic, tetra-octa) only assembles, so the take-apart, Just
 * Levels and Check Rotations are off and greyed.
 */
export function normalize(
  input: SearchOptions,
  changed: BoxName | null,
  family: Family = 'brick',
  canDisassemble = true,
): Normalized {
  const o = { ...input };
  const disabled = new Set<BoxName>();
  if (family === 'stacking') {
    // updateSolverOptionCheckboxes: no Strict Colours, symmetry options or
    // rotation take-apart; Just Count and Just Levels rule each other out,
    // Just Levels keeps Find Solutions on (the search ignores it)
    o.strictColors = false;
    o.checkRotations = false;
    o.deepSymmetry = false;
    o.keepMirrors = false;
    o.keepRotations = false;
    o.nestedSlides = false;
    o.partialNested = false;
    o.verticalMoves = false;
    if (changed === 'justLevels' && o.justLevels) {
      o.justCount = false;
    }
    if (o.justCount) {
      o.justLevels = false;
      o.disassemble = false;
      disabled.add('justLevels');
      disabled.add('disassemble');
    } else if (o.justLevels) {
      o.disassemble = true;
      disabled.add('justCount');
      disabled.add('disassemble');
    }
    return { options: o, disabled };
  }
  if (family === 'sliding') {
    // updateSolverOptionCheckboxes: no Just Levels, Strict Colours,
    // symmetry options or rotation take-apart; partially nested needs
    // nested slides (greyed, its tick kept)
    o.justLevels = false;
    o.strictColors = false;
    o.checkRotations = false;
    o.deepSymmetry = false;
    o.keepMirrors = false;
    o.keepRotations = false;
    if (o.justCount) {
      o.disassemble = false;
      disabled.add('disassemble');
    }
    if (!o.nestedSlides) {
      disabled.add('partialNested');
    }
    return { options: o, disabled };
  }
  if (!canDisassemble) {
    o.disassemble = false;
    o.justLevels = false;
    disabled.add('disassemble');
    disabled.add('justLevels');
  }
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
export function solveOptions(
  o: SearchOptions,
  limits: Limits,
  family: Family = 'brick',
  stackingSolver: StackingSolver = 'stacking',
): SolveOptions {
  if (family === 'stacking') {
    // the solve thread's flags (cb_BtnCont): Find Solutions or Just Count,
    // the search's own autosave, the one solver of the board (the memory
    // is the engine's setting)
    return {
      disassemble: o.disassemble,
      justCount: o.justCount,
      keepAtMost: Math.max(0, Math.round(limits.keepAtMost)),
      keepEach: Math.max(1, Math.round(limits.keepEach)),
      stacking: { solver: stackingSolver, autosave: o.autosave },
    };
  }
  if (family === 'sliding') {
    // the session forces the rest (decision 0014): the original's flags
    return {
      disassemble: o.disassemble,
      justCount: o.justCount,
      keepAtMost: Math.max(0, Math.round(limits.keepAtMost)),
      keepEach: Math.max(1, Math.round(limits.keepEach)),
      sliding: {
        nestedSlides: o.nestedSlides,
        partialNested: o.nestedSlides && o.partialNested,
        verticalMoves: o.verticalMoves,
      },
    };
  }
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

/** A count for the Details line: "999,999", then "125.3 million", "23.53 billion"… (mainwindow.cpp's shortCount). */
export function shortCount(n: number): string {
  if (n < 1_000_000) {
    return String(Math.trunc(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  }
  const names = ['million', 'billion', 'trillion', 'quadrillion', 'quintillion'];
  let v = n / 1e6;
  let step = 0;
  // a value that rounds up to 1000 belongs to the next name
  while (step < 4 && Math.round(v * 10 ** (step + 1)) >= 1000 * 10 ** (step + 1)) {
    v /= 1000;
    step++;
  }
  return `${v.toFixed(step + 1)} ${names[step] ?? ''}`;
}

/**
 * The Details line while a stacking search runs (mainwindow.cpp's
 * updateInterface, without the search's name, which the Activity line
 * gives through the solver's): the Panex search's depth and stackings
 * found, or its trace back; the Stacking Solver's own search its
 * stackings scanned.
 */
export function stackingDetails(
  panex: boolean,
  searched: number,
  depth: number,
  traced: number,
): string {
  if (!panex) {
    return `Scanned ${shortCount(searched)} stackings.`;
  }
  return traced > 0
    ? `Tracing the path. Traced ${shortCount(traced)} of ${shortCount(depth)} moves.`
    : `${shortCount(depth)} moves deep. Found ${shortCount(searched)} stackings.`;
}

/**
 * The Details line while a sliding search runs (the engine's activity is
 * empty there): the original's "N moves deep. Scanned N arrangements."
 * without the search's name, and without the depth before the full
 * search's first move.
 */
export function slidingDetails(searched: number, depth: number): string {
  return depth > 0
    ? `${shortCount(depth)} moves deep. Scanned ${shortCount(searched)} arrangements.`
    : `Scanned ${shortCount(searched)} arrangements.`;
}

/**
 * The Details line of a problem not being solved: what its solves went
 * through, as kept in the file (mainwindow.cpp's finishedActivity without
 * its "Finished."); empty when nothing is known.
 */
export function savedDetails(
  family: Family,
  searched: number,
  depth: number,
  apart: number,
): string {
  const what =
    family === 'sliding' ? 'arrangements' : family === 'stacking' ? 'stackings' : 'search steps';
  const parts: string[] = [];
  if (searched > 0) {
    parts.push(`Scanned ${shortCount(searched)} ${what}.`);
  }
  if (depth > 0) {
    parts.push(`Searched ${shortCount(depth)} moves deep.`);
  } else if (apart > 0) {
    parts.push(`Tried to take apart ${shortCount(apart)} assemblies.`);
  }
  return parts.join(' ');
}

/**
 * The Activity line while a search runs: the solver's name from the
 * engine's list ("Complete (ShapeLogic)", "Sliding Puzzle Solver"…),
 * with "solver" after a name that does not say it.
 */
export function searchActive(label: string): string {
  return /solver/i.test(label)
    ? `Search active with the ${label}`
    : `Search active with the ${label} solver`;
}
