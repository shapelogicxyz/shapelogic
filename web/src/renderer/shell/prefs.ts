// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings of docs/webui-spec.md section 1 as the client holds them
// (the mockup's `st.set`). Undo depth, threads, the search memory and the
// searches' disk space are the engine's (settings.set; the store mirrors
// them); the rest are the client's.
// Since stage 6 they are kept across sessions (persist.ts), read back
// through `validPrefs`.
// One record per setting (cleanup item 12.1): its default, its kind, its
// choices or range, and the engine's name of it. The Prefs type, the
// defaults, the check of a stored value, the settings dialog's choices and
// ranges (shell/settings.ts) and the one map to the engine's settings
// (`engineSettingsOf`, `prefsOfEngine`) come from them. The Search
// Options' defaults are the Solver's boxes' (solver/boxes.ts). The
// settings dialog's rows keep the names and help, which only it shows.

import { THEMES } from '../../engine/app-bridge.ts';
import { LIMITS } from '../../engine/api.generated.ts';
import type { Settings } from '../../engine/api.generated.ts';
import { optKey, SETTING_BOXES } from '../solver/boxes.ts';
import type { OptKey } from '../solver/boxes.ts';

/**
 * The characters per voxel of a piece's width that Show Piece Labels draws
 * by default, and the line length per voxel when it draws all of a name.
 */
export const LABEL_LINE_PER_VOXEL = 6;

/** The largest new shape size the settings take, in each direction. */
export const NEW_SIZE_MAX = 100;

/** The most characters per voxel of width a name Show Piece Labels draws cut. */
export const LABEL_CHARS_MAX = 14;

/** The narrowest and widest side card the settings take, in pixels. */
export const SIDE_MIN = 200;
export const SIDE_MAX = 600;

/** The most the Search Options' two limits take. */
export const LIMIT_MAX = 100_000;

/** The engine's settings that are numbers, which some of the client's settings mirror. */
type EngineKey = {
  [K in keyof Settings]: Settings[K] extends number ? K : never;
}[keyof Settings];

/** An on/off setting. */
interface BoolPref {
  kind: 'bool';
  def: boolean;
}

/** A setting of a few named choices. */
interface ChoicePref<C extends string> {
  kind: 'choice';
  def: C;
  choices: readonly C[];
}

/** A whole number from min to max; `engine`: the engine's setting it is. */
interface IntPref {
  kind: 'int';
  def: number;
  min: number;
  max: number;
  engine?: EngineKey;
}

/** A color, #rrggbb. */
interface ColorPref {
  kind: 'color';
  def: string;
}

/** One setting's record. */
type Pref = BoolPref | ChoicePref<string> | IntPref | ColorPref;

const bool = (def: boolean): BoolPref => ({ kind: 'bool', def });

function choice<const C extends string>(choices: readonly C[], def: NoInfer<C>): ChoicePref<C> {
  return { kind: 'choice', def, choices };
}

const int = (def: number, min: number, max: number, engine?: EngineKey): IntPref =>
  engine === undefined ? { kind: 'int', def, min, max } : { kind: 'int', def, min, max, engine };

const color = (def: string): ColorPref => ({ kind: 'color', def });

/** The settings other than the Search Options' boxes. */
const BASE = {
  density: choice(['Standard', 'Compact', 'Minimal'], 'Standard'),
  /** The interface's text size, the same in every density: Medium and Large each about 2 points larger. */
  fontSize: choice(['Small', 'Medium', 'Large'], 'Medium'),
  /** The menus' Theme choices too (app-bridge.ts). */
  theme: choice(THEMES, 'System'),
  /** The top bar's look: Colorful, the axis colors behind the name and on the tabs; Boring, the plain bar. */
  topBar: choice(['Colorful', 'Boring'], 'Colorful'),
  /** The side cards' widths: Static, sideL and sideR; Variable, wider as a wide window grows. */
  sidebar: choice(['Static', 'Variable'], 'Variable'),
  /** The left and right side cards' widths in Static, in pixels. */
  sideL: int(340, SIDE_MIN, SIDE_MAX),
  sideR: int(374, SIDE_MIN, SIDE_MAX),
  /** Where the workspace tabs (Shapes, Puzzle, Solver) are: centered in the top bar, or the rail at the left. */
  tabsAt: choice(['Top', 'Left'], 'Top'),
  undo: int(25, 1, LIMITS.settings.undoDepth, 'undoDepth'),
  tips: bool(true),
  /** Ask GitHub for a newer release at startup (shell/updates.ts). */
  updates: bool(true),
  /** The log file's debug lines, and its larger size (the main process's app-log.ts reads it by name). */
  debugLog: bool(false),
  cube: bool(true),
  rev: bool(false),
  /** Free Spin: the arc-ball, no limits; Top Up: the drag that keeps the vertical axis up. The names before them are dropped: back to the default. */
  rot: choice(['Free Spin', 'Top Up'], 'Free Spin'),
  /** Which way the axes point in the home view. Z Away and Z Up + Y Toward are dropped: back to the default. */
  zAxis: choice(['Z Toward', 'Z Up'], 'Z Toward'),
  /** The cube grid editor's plane when a puzzle is opened or made. */
  gridPlane: choice(['XY', 'XZ', 'YZ'], 'XY'),
  /** The layer next to the edited one whose cells the voxel editor hints at (the original's was below). */
  hintLayer: choice(['Below', 'Above'], 'Below'),
  /** A new shape's size when none is selected, in the size steppers' units. */
  newX: int(4, 1, NEW_SIZE_MAX),
  newY: int(4, 1, NEW_SIZE_MAX),
  newZ: int(4, 1, NEW_SIZE_MAX),
  /** The same for a sliding puzzle's board. */
  slideX: int(6, 1, NEW_SIZE_MAX),
  slideY: int(6, 1, NEW_SIZE_MAX),
  slideZ: int(1, 1, NEW_SIZE_MAX),
  /** The color of a sliding board with none of its own: in the Light theme, */
  boardColorLight: color('#383838'),
  /** and in the Dark theme. */
  boardColorDark: color('#c8c8c8'),
  lights: bool(true),
  fade: bool(true),
  /** The Solver's 3D view: each piece's label (S2) drawn on it (Display ▸ Show Piece Names). */
  pieceLabels: bool(false),
  /** The Solver's 3D view: each piece's name from the Shapes tab drawn on it, cut to labelCharsPerVoxel characters for each voxel of its width (Display ▸ Show Piece Labels). */
  pieceNames: bool(false),
  /**
   * The characters of a name Show Piece Labels draws for each voxel of the
   * piece's width where the label sits, 1 to LABEL_CHARS_MAX; 0 all of it,
   * a line every LABEL_LINE_PER_VOXEL characters per voxel.
   */
  labelCharsPerVoxel: int(LABEL_LINE_PER_VOXEL, 0, LABEL_CHARS_MAX),
  /** The Solver's Piece display: labels only (Compact), not "S2 - name" (Full). */
  pieceCompact: bool(false),
  /** The Solver's 3D view: while a run searches, the arrangements it is looking at (the run card's Live preview). */
  livePreview: bool(true),
  /** The Apply image dialog's Manual fit: the picture's edges snap to the grid's lines (Snap to grid). */
  imageSnap: bool(true),
  /** Worker threads; 0 until the engine said (its 0 is every hardware thread). */
  threads: int(0, 0, LIMITS.settings.threads, 'threads'),
  /** The memory a sliding or stacking search may fill, in GB; 0 automatic. */
  memGB: int(0, 0, LIMITS.settings.searchMemoryGB, 'searchMemoryGB'),
  /** The disk a full sliding search's older moves may take, in GB; 0 automatic. */
  slideDiskGB: int(0, 0, LIMITS.settings.slideDiskGB, 'slideDiskGB'),
  /** The disk a saved stacking search may take, in GB; 0 automatic. */
  panexDiskGB: int(0, 0, LIMITS.settings.panexDiskGB, 'panexDiskGB'),
  /** The Solver type of a problem that stored none (not sliding or stacking): classic or crowell. */
  solverType: choice(['Complete', 'Fast'], 'Complete'),
  /** The Search Options' Keep every n-th of a solve (1: each); no problem stores it. */
  optKeepEach: int(1, 1, LIMIT_MAX),
  /** Keep at most this many solutions (0: all); no problem stores it. */
  optKeepAtMost: int(100, 0, LIMIT_MAX),
};

/**
 * The Solver's Search Options of a problem that stored none (the settings
 * dialog's Search Options page; solver/settings.ts's defaultsOf): the
 * boxes by their stored names, their defaults the boxes' own.
 */
const OPTIONS = Object.fromEntries(
  SETTING_BOXES.map((b) => [optKey(b.name), bool(b.def)]),
) as Record<OptKey, BoolPref>;

/** Every setting's record. */
export const PREFS = { ...BASE, ...OPTIONS } satisfies Record<string, Pref>;

/** A setting's name. */
export type PrefKey = keyof typeof PREFS;

/** The value of a record. */
type ValueOf<P> =
  P extends ChoicePref<infer C>
    ? C
    : P extends BoolPref
      ? boolean
      : P extends IntPref
        ? number
        : string;

/** The settings and their values. */
export type Prefs = { -readonly [K in PrefKey]: ValueOf<(typeof PREFS)[K]> };

/** Which way the axes point in the home view (the 3D view's "Z axis direction"). */
export type ZAxis = Prefs['zAxis'];

/** The settings that are whole numbers. */
export type IntKey = { [K in PrefKey]: (typeof PREFS)[K] extends IntPref ? K : never }[PrefKey];

/** The settings of named choices. */
export type ChoiceKey = {
  [K in PrefKey]: (typeof PREFS)[K] extends ChoicePref<string> ? K : never;
}[PrefKey];

/** The defaults (the engine's for undo depth, threads, memory and disk). */
export const DEFAULTS: Readonly<Prefs> = Object.freeze(
  Object.fromEntries(Object.entries(PREFS).map(([k, p]) => [k, p.def])) as Prefs,
);

/** The settings now. */
export const prefs: Prefs = { ...DEFAULTS };

const listeners = new Set<(changed: PrefKey[]) => void>();

/** Changes settings and tells the listeners which. */
export function setPrefs(change: Partial<Prefs>): void {
  const changed: PrefKey[] = [];
  for (const [k, v] of Object.entries(change) as [PrefKey, Prefs[PrefKey]][]) {
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
export function onPrefs(listener: (changed: PrefKey[]) => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** The choices of a setting of named choices. */
export const choicesOf = (k: ChoiceKey): readonly string[] => PREFS[k].choices;

/** The range of a whole-number setting. */
export const rangeOf = (k: IntKey): { min: number; max: number } => PREFS[k];

/** A number held to a whole-number setting's range, rounded. */
export function clampPref(k: IntKey, n: number): number {
  const { min, max } = PREFS[k];
  return Math.min(max, Math.max(min, Math.round(n)));
}

/** Whether a value is one a setting takes. */
export function validPref(k: PrefKey, v: unknown): boolean {
  const p: Pref = PREFS[k];
  switch (p.kind) {
    case 'bool':
      return typeof v === 'boolean';
    case 'choice':
      return (p.choices as readonly unknown[]).includes(v);
    case 'int':
      return typeof v === 'number' && Number.isInteger(v) && v >= p.min && v <= p.max;
    case 'color':
      return typeof v === 'string' && /^#[0-9a-f]{6}$/i.test(v);
  }
}

/** Whether a setting is a whole number. */
function isIntKey(k: PrefKey): k is IntKey {
  return PREFS[k].kind === 'int';
}

/** The client's settings that are the engine's, with the engine's names. */
export const ENGINE_PREFS: readonly (readonly [IntKey, EngineKey])[] = (
  Object.keys(PREFS) as PrefKey[]
).flatMap((k) => {
  const p: Pref = PREFS[k];
  return isIntKey(k) && p.kind === 'int' && p.engine !== undefined ? [[k, p.engine] as const] : [];
});

/** The engine's settings of a change of the client's (settings.set's params). */
export function engineSettingsOf(c: Partial<Prefs>): Partial<Settings> {
  const out: Partial<Record<EngineKey, number>> = {};
  for (const [k, e] of ENGINE_PREFS) {
    const v = c[k];
    if (v !== undefined) {
      out[e] = v;
    }
  }
  return out;
}

/** The client's settings that mirror the engine's. */
export function prefsOfEngine(s: Settings): Partial<Prefs> {
  return Object.fromEntries(ENGINE_PREFS.map(([k, e]) => [k, s[e]]));
}

/** The stored settings that are well formed. */
export function validPrefs(saved: Record<string, unknown>): Partial<Prefs> {
  const out: Partial<Record<PrefKey, unknown>> = {};
  for (const k of Object.keys(PREFS) as PrefKey[]) {
    // Z Up was called Z Up + Y Away
    // The board colors were first saved as boardColourLight and boardColourDark // british-ok
    const legacy = k.replace('Color', 'Colour'); // british-ok
    const v = k === 'zAxis' && saved[k] === 'Z Up + Y Away' ? 'Z Up' : (saved[k] ?? saved[legacy]);
    if (validPref(k, v)) {
      out[k] = v;
    }
  }
  return out as Partial<Prefs>;
}
