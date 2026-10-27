// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings of docs/webui-spec.md section 1 as the client holds them
// (the mockup's `st.set`). Undo depth, threads, the search memory and the
// searches' disk space are the engine's (settings.set; the store mirrors
// them); the rest are the client's.
// Since stage 6 they are kept across sessions (persist.ts), read back
// through `validPrefs`.

/** Which way the axes point in the home view (the 3D view's "Z axis direction"). */
export type ZAxis = 'Z Towards' | 'Z Up';

/** The settings and their values. */
export interface Prefs {
  density: 'Standard' | 'Compact' | 'Minimal';
  theme: 'Light' | 'Dark' | 'System';
  /** The side cards' widths: Static, sideL and sideR; Variable, wider as a wide window grows. */
  sidebar: 'Static' | 'Variable';
  /** The left and right side cards' widths in Static, in pixels (SIDE_MIN to SIDE_MAX). */
  sideL: number;
  sideR: number;
  undo: number;
  tips: boolean;
  /** Ask GitHub for a newer release at startup (shell/updates.ts). */
  updates: boolean;
  cube: boolean;
  rev: boolean;
  /** Free Spin: the arc-ball, no limits; Top Up: the drag that keeps the vertical axis up. */
  rot: 'Free Spin' | 'Top Up';
  zAxis: ZAxis;
  /** The cube grid editor's plane when a puzzle is opened or made. */
  gridPlane: 'XY' | 'XZ' | 'YZ';
  /** A new shape's size when none is selected, in the size steppers' units. */
  newX: number;
  newY: number;
  newZ: number;
  /** The same for a sliding puzzle's board. */
  slideX: number;
  slideY: number;
  slideZ: number;
  lights: boolean;
  fade: boolean;
  /** The Solver's 3D view: each piece's label drawn on it (Search options). */
  pieceLabels: boolean;
  /** The Solver's Piece display: labels only (Compact), not "S2 - name" (Full). */
  pieceCompact: boolean;
  /** Worker threads; 0 until the engine said (its 0 is every hardware thread). */
  threads: number;
  /** The memory a sliding or stacking search may fill, in GB; 0 automatic (the engine's searchMemoryGB). */
  memGB: number;
  /** The disk a full sliding search's older moves may take, in GB; 0 automatic (the engine's slideDiskGB). */
  slideDiskGB: number;
  /** The disk a saved stacking search may take, in GB; 0 automatic (the engine's panexDiskGB). */
  panexDiskGB: number;
  /** The Solver type of a problem that stored none (not sliding or stacking): classic or crowell. */
  solverType: 'Complete' | 'Fast';
}

/** The defaults (the engine's for undo depth, threads, memory and disk). */
export const DEFAULTS: Readonly<Prefs> = {
  density: 'Standard',
  theme: 'System',
  sidebar: 'Static',
  sideL: 320,
  sideR: 340,
  undo: 25,
  tips: true,
  updates: true,
  cube: true,
  rev: false,
  rot: 'Free Spin',
  zAxis: 'Z Towards',
  gridPlane: 'XY',
  newX: 4,
  newY: 4,
  newZ: 4,
  slideX: 6,
  slideY: 6,
  slideZ: 1,
  lights: true,
  fade: true,
  pieceLabels: false,
  pieceCompact: false,
  threads: 0,
  memGB: 0,
  slideDiskGB: 0,
  panexDiskGB: 0,
  solverType: 'Complete',
};

/** The settings now. */
export const prefs: Prefs = { ...DEFAULTS };

const listeners = new Set<(changed: (keyof Prefs)[]) => void>();

/** Changes settings and tells the listeners which. */
export function setPrefs(change: Partial<Prefs>): void {
  const changed: (keyof Prefs)[] = [];
  for (const [k, v] of Object.entries(change) as [keyof Prefs, Prefs[keyof Prefs]][]) {
    if (prefs[k] !== v) {
      (prefs as unknown as Record<string, unknown>)[k] = v;
      changed.push(k);
    }
  }
  if (changed.length > 0) {
    for (const l of [...listeners]) {
      l(changed);
    }
  }
}

/** Listens to changes of the settings; returns the unsubscribe. */
export function onPrefs(listener: (changed: (keyof Prefs)[]) => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** The choices of the settings that have them (the settings dialog's). */
const CHOICES: Partial<Record<keyof Prefs, readonly unknown[]>> = {
  density: ['Standard', 'Compact', 'Minimal'],
  theme: ['Light', 'Dark', 'System'],
  sidebar: ['Static', 'Variable'],
  // the names before Free Spin and Top Up are dropped: back to the default
  rot: ['Free Spin', 'Top Up'],
  // Z Away and Z Up + Y Towards are dropped: back to the default
  zAxis: ['Z Towards', 'Z Up'],
  gridPlane: ['XY', 'XZ', 'YZ'],
  solverType: ['Complete', 'Fast'],
};

/** The largest new shape size the settings take, in each direction. */
export const NEW_SIZE_MAX = 100;

/** The new shape size settings, each 1 to NEW_SIZE_MAX. */
const NEW_SIZE: readonly (keyof Prefs)[] = ['newX', 'newY', 'newZ', 'slideX', 'slideY', 'slideZ'];

/** The narrowest and widest side card the settings take, in pixels. */
export const SIDE_MIN = 200;
export const SIDE_MAX = 600;

/** The side card width settings, each SIDE_MIN to SIDE_MAX. */
const SIDE: readonly (keyof Prefs)[] = ['sideL', 'sideR'];

/** The stored settings that are well formed. */
export function validPrefs(saved: Record<string, unknown>): Partial<Prefs> {
  const out: Partial<Record<keyof Prefs, unknown>> = {};
  for (const k of Object.keys(DEFAULTS) as (keyof Prefs)[]) {
    // Z Up was called Z Up + Y Away
    const v = k === 'zAxis' && saved[k] === 'Z Up + Y Away' ? 'Z Up' : saved[k];
    const choices = CHOICES[k];
    const ok =
      choices !== undefined
        ? choices.includes(v)
        : NEW_SIZE.includes(k)
          ? typeof v === 'number' && Number.isInteger(v) && v >= 1 && v <= NEW_SIZE_MAX
          : SIDE.includes(k)
            ? typeof v === 'number' && Number.isInteger(v) && v >= SIDE_MIN && v <= SIDE_MAX
            : typeof DEFAULTS[k] === 'number'
              ? typeof v === 'number' && Number.isInteger(v) && v >= 0 && v <= 100_000
              : typeof v === typeof DEFAULTS[k];
    if (ok) {
      out[k] = v;
    }
  }
  return out as Partial<Prefs>;
}
