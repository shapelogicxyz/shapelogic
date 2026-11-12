// SPDX-License-Identifier: GPL-3.0-or-later
//
// The names and the help of the Solver's Search Options, for the Solver's
// left card (run-card.ts) and for the settings dialog's Search Options page
// (shell/settings.ts), which sets what a problem starts with. From
// docs/mockup/index.html (`SVTIP`) and the original's tooltips
// (mainwindow_layout.cpp); the names are the original's, so they match its
// documentation. DOM-free.

import type { BoxName } from './settings.ts';

/** The switches' help (the mockup's SVTIP, the original's tooltips for the rest). */
export const TIP: Record<BoxName, string> = {
  disassemble:
    'Check each assembly for a complete disassembly; only assemblies that can be taken apart are listed.',
  justLevels:
    'Check disassemblies but keep only their level, not the moves. Recompute them later with “Analyse”.',
  checkRotations: 'Also try 90 degree piece rotations while taking the assemblies apart.',
  justCount: 'Only count solutions; they are dropped as soon as they are found.',
  keepMirrors:
    'Do not remove solutions that are mirror images of others (useful with piece ranges).',
  keepRotations: 'Do not remove solutions that are only rotations of others.',
  deepSymmetry:
    'A more thorough, slower check for duplicate solutions: removes translations and rotations that are not in the symmetry of the result shape.',
  strictColors:
    'A voxel with a color fits only a result voxel of that same color, not a neutral one; a voxel with no color fits only a result voxel that also has none.',
  nestedSlides:
    'A piece may slide together with the pieces nested inside its outline, such as a piece in another piece’s pocket. Pieces that only touch never slide together.',
  partialNested:
    'With Allow Nested Slides: a piece may also slide together with a piece that sits partly in its outline, such as a piece in another piece’s pocket that sticks out of it. Pieces that only touch never slide together.',
  verticalMoves:
    'On a tray more than one layer deep, a piece may also move up and down: lifted out over the other pieces and put down somewhere else. Off, every piece slides only in its own layer.',
  anyOrientation:
    'A piece with neither a start nor a goal on the board may be placed in any of its quarter turns. Off, every piece is placed as it is drawn. Either way a piece keeps its orientation as it slides.',
  mirroredOrientations:
    'With Allow any orientation for unplaced pieces: those pieces may also be placed mirrored, as if turned over.',
  keepAssemblies:
    'Always show Assembly results even when Disassemble is selected and no Disassemblies are found. Disassemblies will be shown at the top of the results.',
};

/** The stacking puzzles' tooltips where they differ (mainwindow_layout.cpp, mainwindow_solve.cpp). */
export const STACKING_TIP: Partial<Record<BoxName, string>> = {
  disassemble:
    'Search for a shortest sequence of rod transfers from the start stacking to the goal.',
};

/** The switches' names: the original's (mainwindow_layout.cpp), so they match its documentation. */
export const LABEL: Record<BoxName, string> = {
  disassemble: 'Assemblies and disassembly',
  justLevels: 'Just Levels',
  checkRotations: 'Check Rotations',
  justCount: 'Just Count',
  keepMirrors: 'Keep Mirror Solutions',
  keepRotations: 'Keep Rotated Solutions',
  deepSymmetry: 'Deep Symmetry Check',
  strictColors: 'Strict Color Restrictions',
  nestedSlides: 'Allow Nested Slides',
  partialNested: 'Allow Partially Nested',
  verticalMoves: 'Allow Vertical Moves',
  anyOrientation: 'Allow any orientation for unplaced pieces',
  mirroredOrientations: 'Include mirrored orientations',
  keepAssemblies: 'Always show Assemblies',
};

/** The two limits' names and help. */
/** The Live preview switch (a setting of the program, not of a problem). */
export const LIVE_PREVIEW = {
  label: 'Live preview',
  tip: 'While a search runs and no solution is chosen, show the arrangements it is looking at in the view, a few times a second',
} as const;
export const KEEP_EACH = {
  label: 'Keep every n-th',
  tip: 'Keep a sample of a large number of solutions: 1 keeps each, higher numbers skip that many before the next one is kept',
} as const;
export const KEEP_AT_MOST = {
  label: 'Limit',
  tip: 'Keep at most this many solutions (0: all)',
} as const;

/** The solver type's help on the puzzles that have a choice of it. */
export const SOLVER_TYPE_TIP =
  'Which take-apart the solve uses: Complete (ShapeLogic) or Fast (Crowell).';
