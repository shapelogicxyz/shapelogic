// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's search options over a problem's stored SolverSettings (the
// original's Solver tab check boxes, saved with each problem; decision
// 0012, "Names that stay": the stored names are the file's, SolveOptions
// uses the API's, types.json has the mapping). DOM-free, for the unit
// tests. Ported from the original's mainwindow_solve.cpp:
// `updateSolverOptionCheckboxes` (which boxes force or gray out which),
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
// Levels, Check Rotations, Strict Colors and the symmetry options; and
// Allow Vertical Moves, which the original (trays one layer deep) did not
// have, off by default so that a piece stays in its layer; and after Just
// Count, Allow any orientation for unplaced pieces with its Include
// mirrored orientations (only with it), off by default so that every
// piece is placed as drawn; and above the Solver Type, Automatically use
// optimized paths (decision 0029), off by default: with it off, Solve asks
// first when the board fits a grid (workspace.ts). On a
// stacking puzzle (section 9.3): Find Solutions checked at first
// (stackingDisasmDefaulted), Just Levels kept (the search ignores it), no
// Check Rotations, Strict Colors or symmetry options, and one solver by
// the problem's board (syncSolverTypeMenu: the Panex Solver for Panex
// columns, else the Stacking Solver), which the original did not store.
// The original's Enable High Memory of both is the Search memory setting
// (shell/settings.ts) here: a problem's stored highMemory is kept as it
// was, and plays no part.

import type {
  Grid,
  Methods,
  SolveOptions,
  Solver,
  SolverSettings,
  SortBy,
  StackingSolver,
} from '../../engine/api.generated.ts';
import { DEFAULTS, prefs } from '../shell/prefs.ts';
import { BOXES, optKey, SETTING_BOXES, showsOn, storedOn } from './boxes.ts';
import type { BoxName, PuzzleKind } from './boxes.ts';

export type { BoxName, PuzzleKind } from './boxes.ts';

/** The puzzles whose Solver shows its own options: brick (every assembling grid), sliding, stacking. */
export type Family = 'brick' | 'sliding' | 'stacking';

/** The family of a grid's puzzles. */
export function familyOf(grid: Grid | undefined): Family {
  return grid === 'sliding' ? 'sliding' : grid === 'stacking' ? 'stacking' : 'brick';
}

/** The puzzle type of a family's puzzle, a tilted tray's apart (the boxes' table). */
export function kindOf(family: Family, tilted = false): PuzzleKind {
  return family === 'brick' && tilted ? 'tilted' : family;
}

/**
 * The options as the Solver card shows and edits them: a boolean per box
 * (solver/boxes.ts; option-text.ts says what each does), and the rest.
 */
export type SearchOptions = Record<BoxName, boolean> & {
  /**
   * The original's Autosave box as the problem stored it: read and written
   * back as it was, and not looked at. The crash recovery journal that took
   * its place is always on, and writes to the rescue copy, not to the
   * puzzle's file (autosave.ts).
   */
  autosave: boolean;
  solver: Solver;
  sortBy: SortBy;
};

/** The defaults of a problem that stored none (the boxes' own: the original's widgets as made, but Strict colors on). */
export const DEFAULT_OPTIONS: Readonly<SearchOptions> = {
  ...(Object.fromEntries(BOXES.map((b) => [b.name, b.def])) as Record<BoxName, boolean>),
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

/**
 * The options a family's problems start with when they stored none: the
 * settings dialog's Search Options (shell/prefs.ts; as shipped, the
 * constants above). The modes are not settings (Find Solutions checked at
 * first on a sliding or stacking puzzle). As set, not yet as the family's
 * rules leave them: `readOptions` and the Solver's card apply `normalize`,
 * where a default that does not go with another, or with the family, gives
 * way.
 */
export function defaultsOf(family: Family): Readonly<SearchOptions> {
  const o: SearchOptions = {
    ...(family === 'sliding'
      ? DEFAULT_SLIDING
      : family === 'stacking'
        ? DEFAULT_STACKING
        : DEFAULT_OPTIONS),
    ...(Object.fromEntries(SETTING_BOXES.map((b) => [b.name, prefs[optKey(b.name)]])) as Record<
      BoxName,
      boolean
    >),
    solver: family === 'brick' && prefs.solverType === 'Fast' ? 'crowell' : 'classic',
  };
  return o;
}

/** The two limits a solve starts with: the settings dialog's Search Options. */
export function defaultLimits(): Limits {
  return {
    keepAtMost: Math.max(0, Math.round(prefs.optKeepAtMost)),
    keepEach: Math.max(1, Math.round(prefs.optKeepEach)),
  };
}

/**
 * The boxes a puzzle type's card shows, in order (updateSolverOptionCheckboxes'
 * setVis for sliding and stacking): with the modes, or in the Search Options.
 */
export function boxesShown(kind: PuzzleKind, at?: 'mode' | 'options'): BoxName[] {
  return BOXES.filter((b) => showsOn(b, kind) && (at === undefined || b.at === at)).map(
    (b) => b.name,
  );
}

/**
 * The one solver the original offered a stacking problem
 * (syncSolverTypeMenu): the Panex Solver on a board of Panex columns,
 * else the Stacking Solver.
 */
export function stackingSolverOf(panexColumns: boolean): StackingSolver {
  return panexColumns ? 'panex' : 'stacking';
}

/** What engine.info says. */
type EngineInfo = Methods['engine.info']['result'];

/**
 * The solvers of assembling puzzles to choose from: the engine's list
 * (engine.info), the Complete one alone before it has said.
 */
export function brickSolvers(info: EngineInfo | null): EngineInfo['solvers'] {
  return info?.solvers ?? [{ solver: 'classic', label: 'Complete (ShapeLogic)' }];
}

/** A sliding puzzle's one solver, the engine's name of it (the original's before it has said). */
export function slidingSolver(info: EngineInfo | null): EngineInfo['slidingSolvers'][number] {
  return info?.slidingSolvers[0] ?? { solver: 'sliding', label: 'Sliding Puzzle Solver' };
}

/** The name of a stacking puzzle's solver, the engine's (the original's before it has said). */
export function stackingSolverLabel(info: EngineInfo | null, solver: StackingSolver): string {
  return (
    info?.stackingSolvers.find((x) => x.solver === solver)?.label ??
    (solver === 'panex' ? 'Panex Solver' : 'Stacking Solver')
  );
}

/**
 * The name of the solver a family's options choose, from the engine's
 * list: a stacking puzzle's by its board, a sliding puzzle's one, else the
 * Solver type's.
 */
export function solverLabel(
  info: EngineInfo | null,
  family: Family,
  o: Pick<SearchOptions, 'solver'>,
  stackingSolver: StackingSolver,
): string {
  if (family === 'stacking') {
    return stackingSolverLabel(info, stackingSolver);
  }
  if (family === 'sliding') {
    return slidingSolver(info).label;
  }
  const all = brickSolvers(info);
  return (all.find((x) => x.solver === o.solver) ?? all[0])?.label ?? 'Complete (ShapeLogic)';
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
  for (const { name } of BOXES) {
    const v = stored[name];
    if (typeof v === 'boolean') {
      o[name] = v;
    }
  }
  // the original's Autosave box: kept as stored, for the file
  if (typeof stored.autosave === 'boolean') {
    o.autosave = stored.autosave;
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
  tilted = false,
): SolverSettings {
  const kind = kindOf(family, tilted);
  const out: SolverSettings = { ...stored, autosave: o.autosave, sortBy: o.sortBy };
  // each box with the puzzle types that store it (the sliding boxes with a
  // sliding puzzle's problems only, a tilted puzzle's turned pieces,
  // decision 0045)
  for (const b of BOXES) {
    if (storedOn(b, kind)) {
      out[b.name] = o[b.name];
    }
  }
  // a stacking puzzle's one solver was not stored (storeSolverOptions), nor
  // is a sliding puzzle's (a solver type stored before is kept as it was)
  if (family === 'brick') {
    out.solverType = o.solver;
  }
  return out;
}

/** Options with the original's rules applied, and the boxes it grays out. */
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
 * spheres) only assembles, so the take-apart, Just
 * Levels and Check Rotations are off and grayed.
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
    // updateSolverOptionCheckboxes: no Strict Colors, symmetry options or
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
    o.anyOrientation = false;
    o.mirroredOrientations = false;
    o.optimizedPaths = false;
    o.keepAssemblies = false;
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
    // updateSolverOptionCheckboxes: no Just Levels, Strict Colors,
    // symmetry options or rotation take-apart; partially nested needs
    // nested slides and mirrored orientations any orientation (grayed,
    // their ticks kept)
    o.justLevels = false;
    o.strictColors = false;
    o.checkRotations = false;
    o.deepSymmetry = false;
    o.keepMirrors = false;
    o.keepRotations = false;
    o.keepAssemblies = false;
    if (o.justCount) {
      o.disassemble = false;
      disabled.add('disassemble');
    }
    if (!o.nestedSlides) {
      disabled.add('partialNested');
    }
    if (!o.anyOrientation) {
      disabled.add('mirroredOrientations');
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
  // Always show Assemblies needs the take-apart (grayed, its tick kept)
  if (!o.disassemble) {
    disabled.add('keepAssemblies');
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

/** The defaults of the original's two fields (the settings' defaults). */
export const DEFAULT_LIMITS: Readonly<Limits> = {
  keepAtMost: DEFAULTS.optKeepAtMost,
  keepEach: DEFAULTS.optKeepEach,
};

/**
 * The SolveOptions of a solve (solve.start, prepare, continue): the
 * stored names mapped to the API's (deepSymmetry is completeRotations,
 * strictColors strictColors); the threads are left to the engine's
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
    // the one solver of the board (the memory is the engine's setting; the
    // search saves itself as it runs, the engine's default, whatever the
    // problem stored for the original's Autosave box)
    return {
      disassemble: o.disassemble,
      justCount: o.justCount,
      keepAtMost: Math.max(0, Math.round(limits.keepAtMost)),
      keepEach: Math.max(1, Math.round(limits.keepEach)),
      stacking: { solver: stackingSolver },
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
        anyOrientation: o.anyOrientation,
        mirroredOrientations: o.anyOrientation && o.mirroredOrientations,
        optimizedPaths: o.optimizedPaths,
      },
    };
  }
  return {
    solver: o.solver,
    disassemble: o.disassemble,
    checkRotations: o.checkRotations,
    justCount: o.justCount,
    justLevels: o.justLevels,
    keepAssemblies: o.disassemble && o.keepAssemblies,
    // only a tilted puzzle shows the box, so only its problems turn it off
    ...(o.flipPieces ? {} : { flipPieces: false }),
    keepMirrors: o.keepMirrors,
    keepRotations: o.keepRotations,
    completeRotations: o.deepSymmetry,
    strictColors: o.strictColors,
    sortBy: effectiveSort(o),
    keepAtMost: Math.max(0, Math.round(limits.keepAtMost)),
    keepEach: Math.max(1, Math.round(limits.keepEach)),
  };
}

/**
 * A sliding solve's options with optimized paths on or off: the Solve
 * dialog's choice, or the full search offered when they found nothing, for
 * that solve only (the problem's stored box stays as it was).
 */
export function withOptimizedPaths(o: SolveOptions, on: boolean): SolveOptions {
  return o.sliding === undefined ? o : { ...o, sliding: { ...o.sliding, optimizedPaths: on } };
}

/** Whether two option sets store the same. */
export function sameOptions(a: SearchOptions, b: SearchOptions): boolean {
  return (Object.keys(a) as (keyof SearchOptions)[]).every((k) => a[k] === b[k]);
}
