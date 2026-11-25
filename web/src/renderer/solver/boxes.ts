// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's check boxes in one ordered table (cleanup item 12.1): each
// box's stored name, where the Solver's left card shows it and on which
// puzzle types, the problems that store it, its default, and whether the
// settings dialog's Search Options set that default (the setting
// `opt<Name>` of shell/prefs.ts). The card (run-card.ts), the options'
// model (settings.ts: readOptions, writeOptions, defaultsOf), the settings
// (prefs.ts) and the settings dialog's page (shell/settings.ts) all walk
// it. The rules between the boxes stay code (settings.ts's normalize).
// DOM-free and without imports, so prefs.ts can build on it.

/** The puzzle types whose Solver card differs: assembling grids (brick; tilted, a Tilted Square tray), sliding, stacking. */
export type PuzzleKind = 'brick' | 'tilted' | 'sliding' | 'stacking';

/** Every puzzle type. */
const ALL = ['brick', 'tilted', 'sliding', 'stacking'] as const;

/** The assembling grids, a tilted tray's among them. */
const BURRS = ['brick', 'tilted'] as const;

/** One check box. */
interface Box {
  /** Its stored name (SolverSettings), which SearchOptions uses too. */
  name: string;
  /** Where the card shows it: with the two modes (Assemble Only, and the take-apart), or in the Search Options. */
  at: 'mode' | 'options';
  /** The puzzle types whose card shows it. */
  shows: readonly PuzzleKind[];
  /** The puzzle types whose problems store it (writeOptions; others keep what was stored). */
  stored: readonly PuzzleKind[];
  /** Its default for a problem that stored none (a sliding or stacking puzzle's take-apart aside). */
  def: boolean;
  /** The settings dialog's Search Options set its default: the setting `opt<Name>`. */
  setting: boolean;
  /** Drawn indented under the box before it, which it needs. */
  sub?: true;
}

/** The boxes, in the order the card and the settings dialog list them. */
export const BOXES = [
  { name: 'disassemble', at: 'mode', shows: ALL, stored: ALL, def: false, setting: false },
  // with the take-apart only, which a tilted tray has not
  { name: 'checkRotations', at: 'mode', shows: ['brick'], stored: ALL, def: false, setting: false },
  {
    name: 'keepAssemblies',
    at: 'options',
    shows: ['brick'],
    stored: BURRS,
    def: false,
    setting: true,
  },
  {
    name: 'flipPieces',
    at: 'options',
    shows: ['tilted'],
    stored: ['tilted'],
    def: true,
    setting: false,
  },
  { name: 'justCount', at: 'options', shows: ALL, stored: ALL, def: false, setting: true },
  {
    name: 'justLevels',
    at: 'options',
    shows: ['brick', 'tilted', 'stacking'],
    stored: ALL,
    def: false,
    setting: true,
  },
  { name: 'keepMirrors', at: 'options', shows: BURRS, stored: ALL, def: false, setting: true },
  { name: 'keepRotations', at: 'options', shows: BURRS, stored: ALL, def: false, setting: true },
  { name: 'deepSymmetry', at: 'options', shows: BURRS, stored: ALL, def: false, setting: true },
  { name: 'strictColors', at: 'options', shows: BURRS, stored: ALL, def: true, setting: true },
  {
    name: 'anyOrientation',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
  },
  {
    name: 'mirroredOrientations',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
    sub: true,
  },
  {
    name: 'nestedSlides',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
  },
  {
    name: 'partialNested',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
  },
  {
    name: 'verticalMoves',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
  },
  {
    name: 'optimizedPaths',
    at: 'options',
    shows: ['sliding'],
    stored: ['sliding'],
    def: false,
    setting: true,
  },
] as const satisfies readonly Box[];

/** The check boxes, by their stored names. */
export type BoxName = (typeof BOXES)[number]['name'];

/** The boxes whose default the settings dialog sets. */
export type SettingBox = Extract<(typeof BOXES)[number], { setting: true }>['name'];

/** The setting of a box's default (prefs.ts): optJustCount for justCount. */
export type OptKey = `opt${Capitalize<SettingBox>}`;

/** The setting of a box's default. */
export function optKey(name: SettingBox): OptKey {
  return `opt${name.charAt(0).toUpperCase()}${name.slice(1)}` as OptKey;
}

/** The boxes whose default the settings dialog sets, in order. */
export const SETTING_BOXES = BOXES.filter(
  (b): b is Extract<(typeof BOXES)[number], { setting: true }> => b.setting,
);

/** Whether the card shows a box on a puzzle type. */
export const showsOn = (b: (typeof BOXES)[number], kind: PuzzleKind): boolean =>
  (b.shows as readonly PuzzleKind[]).includes(kind);

/** Whether a puzzle type's problems store a box. */
export const storedOn = (b: (typeof BOXES)[number], kind: PuzzleKind): boolean =>
  (b.stored as readonly PuzzleKind[]).includes(kind);

/** Whether the card draws a box indented under the one before it. */
export const isSub = (name: BoxName): boolean => BOXES.some((b) => b.name === name && 'sub' in b);
