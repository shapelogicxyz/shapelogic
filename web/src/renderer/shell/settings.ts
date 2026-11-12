// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings dialog: searchable, four pages and the keyboard shortcuts,
// "Reset section", "Restore all defaults", changes applied at once. From
// docs/mockup/index.html: `SET`, `rowHtml`, `kbKeys`, `kbRow`,
// `renderSettings`, `openSettings`, `closeSettings` and the dialog's
// handlers. The mockup's "Use OpenGL display lists" is dropped (no OpenGL;
// docs/webui-spec.md section 1). Undo depth, worker threads, the search
// memory and the searches' disk space go to the engine (settings.set);
// the threads' and the memory's ranges are the machine's (engine.info's
// hardware threads and memory). The search memory replaced the Solver
// tab's Enable High Memory. The shortcuts page prints the one keyboard
// table (keys.ts). The Search Options page (not in the mockup) sets what
// the Solver's Search Options start at for a problem that stored none of
// its own: every option of that section, with its name and help from the
// Solver's card (solver/option-text.ts), and "(Only: …)" under the name of
// one that only some puzzle types have. The General page's Cache folder
// (not in the mockup) is the main process's (main/cache-folder.ts): it
// shows the folder in use, and Choose Folder… and Use Default change it,
// asking whether to move the data found there or delete it.

import type { CacheAction, CacheFolderInfo } from '../../engine/app-bridge.ts';
import type { Settings } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';
import { openColourPicker } from './colour-picker.ts';
import { askChoice, closeDialog, openDialog, tell } from './dialog.ts';
import { ic } from './icons.ts';
import { keyGroups } from './keys.ts';
import { closePop } from './popover.ts';
import type { KeyRow } from './keys.ts';
import { DEFAULTS, LIMIT_MAX, NEW_SIZE_MAX, prefs, setPrefs, SIDE_MAX, SIDE_MIN } from './prefs.ts';
import type { Prefs } from './prefs.ts';
import { flash, reportError } from './status.ts';
import { KEEP_AT_MOST, KEEP_EACH, LABEL, TIP } from '../solver/option-text.ts';

type Row = (
  | { k: keyof Prefs; t: string; d: string; type: 'sw' }
  | { k: keyof Prefs; t: string; d: string; type: 'seg'; opts: readonly string[] }
  | { k: keyof Prefs; t: string; d: string; type: 'select'; opts: readonly number[] }
  | { k: keyof Prefs; t: string; d: string; type: 'slider'; min: number }
  | {
      k: keyof Prefs;
      t: string;
      d: string;
      type: 'steps';
      /** The values the slider stops at, smallest first; 0 is automatic. */
      steps: () => readonly number[];
      label: (v: number) => string;
      /** Laid out stacked: the value over a taller slider, tick marks under it (automatic's bold). */
      ticks?: true;
      /** The warning under the slider at a value, empty for none. */
      warn?: (v: number) => string;
    }
  | { t: string; d: string; type: 'xyz'; sets: readonly XyzSet[] }
  | { k: keyof Prefs; t: string; d: string; type: 'colour' }
  | { t: string; d: string; type: 'widths' }
  /** The cache folder: the main process's, not a setting of Prefs. */
  | { t: string; d: string; type: 'cache' }
  | {
      k: NumKey;
      t: string;
      d: string;
      /** A whole number with steppers: from `min`, in steps of `step`. */
      type: 'num';
      min: number;
      step: number;
    }
) & {
  /** Shown only while this holds (a setting another one makes matter). */
  when?: () => boolean;
  /** The heading of the rows of a page it starts, until the next one. */
  group?: string;
  /** The puzzle types that have the setting, when not all do: "(Only: …)" under its name. */
  only?: string;
};

/** The settings a number row sets. */
type NumKey = 'optKeepEach' | 'optKeepAtMost';

/** The side card width settings, as the widths row shows them. */
const WIDTHS = [
  { k: 'sideL', name: 'Left' },
  { k: 'sideR', name: 'Right' },
] as const;

/** How far the width steppers move, in pixels. */
const WIDTH_STEP = 10;

/** The settings an X, Y and Z row sets. */
type XyzKey = 'newX' | 'newY' | 'newZ' | 'slideX' | 'slideY' | 'slideZ';

/** One labelled X, Y and Z of an xyz row. */
interface XyzSet {
  name: string;
  k: readonly [XyzKey, XyzKey, XyzKey];
}

/** The settings of a row. */
const keysOf = (r: Row): readonly (keyof Prefs)[] =>
  r.type === 'xyz'
    ? r.sets.flatMap((x) => x.k)
    : r.type === 'widths'
      ? WIDTHS.map((x) => x.k)
      : r.type === 'cache'
        ? []
        : [r.k];

/** Whether a row is shown now. */
const shown = (r: Row): boolean => r.when?.() ?? true;

/** The machine's memory in GB, from the engine; 0 until it says or when it cannot tell. */
const memoryGB = (): number => (store?.info?.memoryBytes ?? 0) / 1e9;

/** The automatic search memory in GB: half the machine's, at least 2 (the engine's). */
const autoMemoryGB = (): number => Math.max(2, memoryGB() / 2);

/** The search memory slider's stops: every 2 GB up to 4 GB less than the machine's, automatic (0) in its place among them. */
function memorySteps(): number[] {
  const top = memoryGB() > 0 ? memoryGB() - 4 : 64;
  const auto = autoMemoryGB();
  const fixed: number[] = [];
  for (let g = 2; g <= top; g += 2) {
    if (Math.abs(g - auto) >= 1) {
      fixed.push(g);
    }
  }
  return [...fixed.filter((g) => g < auto), 0, ...fixed.filter((g) => g > auto)];
}

/** The warning under the search memory slider: above three quarters of the machine's memory. */
const memoryWarn = (v: number): string =>
  v !== 0 && memoryGB() > 0 && v > memoryGB() * 0.75
    ? 'Very high memory allocation can make other applications and the operating system slow or unstable, and in the worst case can make ShapeLogic crash.'
    : '';

/** The most tick marks under a slider; past it only every few stops (and automatic) get one. */
const TICKS_MAX = 60;

/** The tick marks under a stacked slider, one per stop, automatic's bold. */
function ticksHtml(steps: readonly number[]): string {
  const every = Math.ceil(steps.length / TICKS_MAX);
  const last = steps.length - 1;
  return steps
    .map((x, i) =>
      x === 0 || i % every === 0 || i === last
        ? `<i class="${x === 0 ? 'auto' : ''}" style="left:${String(last > 0 ? (i / last) * 100 : 50)}%"></i>`
        : '',
    )
    .join('');
}

/** How far along a slider its value is, in percent: the filled part of a stacked slider's track. */
const rangePct = (t: HTMLInputElement): string =>
  `${String(Number(t.max) > 0 ? (Number(t.value) / Number(t.max)) * 100 : 0)}%`;

/** The disk space choices in GB, automatic (0) first. */
const DISK_GB = [0, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000];

/** A size in GB as the sliders show it. */
const gbText = (gb: number): string =>
  gb >= 1000 ? `${String(gb / 1000)} TB` : `${String(Math.round(gb))} GB`;

/** The stop of a slider a value is at, or nearest to (a value set elsewhere). */
function stepOf(steps: readonly number[], v: number, auto: number): number {
  const at = (x: number): number => (x === 0 ? auto : x);
  let best = 0;
  steps.forEach((x, i) => {
    if (Math.abs(at(x) - at(v)) < Math.abs(at(steps[best] ?? 0) - at(v))) {
      best = i;
    }
  });
  return v === 0 ? Math.max(0, steps.indexOf(0)) : best;
}

interface Page {
  id: string;
  name: string;
  /** What the page is for, under its heading. */
  note?: string;
  rows: readonly Row[];
}

/** The puzzle types of "(Only: …)": Normal Burrs is every type but Sliding and Stacking. */
const BURRS = 'Normal Burrs';
const SLIDING = 'Sliding';
const STACKING = 'Stacking';

const SET: readonly Page[] = [
  {
    id: 'general',
    name: 'General',
    rows: [
      {
        k: 'density',
        t: 'Interface density',
        d: 'Standard is the default. Compact is Standard with shorter rows in the scrolling lists (shapes, problems, solutions), so about twice as many fit, and on the Shapes tab each control on one line (its icon beside its caption) with tighter spacing and the action buttons’ descriptions in their tooltips. Minimal shows icons without captions, moves helper text into tooltips, and hides the viewport toolbar until you point at the bottom of the 3D view. Every function stays available in all three, and the text keeps its size (Font size sets it).',
        type: 'seg',
        opts: ['Standard', 'Compact', 'Minimal'],
      },
      {
        k: 'fontSize',
        t: 'Font size',
        d: 'The size of the text throughout the interface. Medium is the default; Small is about 2 points smaller and Large about 2 points larger. The text keeps its size in every interface density.',
        type: 'seg',
        opts: ['Small', 'Medium', 'Large'],
      },
      {
        k: 'sidebar',
        t: 'Sidebar width',
        d: `Variable (the default) widens the side cards as the window grows past 1600 pixels: each card and the 3D view take a third of the extra width, until a card is ${String(SIDE_MAX)} pixels wide; the right card is always 10% wider than the left. Static keeps the left and right side cards at the widths you choose.`,
        type: 'seg',
        opts: ['Static', 'Variable'],
      },
      {
        t: 'Sidebar sizes',
        d: `The widths of the left and right side cards, in pixels, from ${String(SIDE_MIN)} to ${String(SIDE_MAX)}.`,
        type: 'widths',
        when: () => prefs.sidebar === 'Static',
      },
      {
        k: 'tabsAt',
        t: 'Main tab bar location',
        d: 'Where the Shapes, Puzzle and Solver tabs are: Top centers them in the top bar, beside the puzzle’s name; Left keeps them in a rail down the left side of the window.',
        type: 'seg',
        opts: ['Top', 'Left'],
      },
      {
        k: 'theme',
        t: 'Theme',
        d: 'Light, Dark, or System. System follows the operating system’s light/dark choice when the OS provides one, otherwise Light.',
        type: 'seg',
        opts: ['Light', 'Dark', 'System'],
      },
      {
        k: 'topBar',
        t: 'Top bar theme',
        d: 'Colorful puts the red, green and blue of the X, Y and Z axes behind the ShapeLogic name, fading into the bar, and gives each tab its own color: Shapes green, Puzzle blue, Solver red. Boring keeps the bar plain, with a small colored square beside the name.',
        type: 'seg',
        opts: ['Colorful', 'Boring'],
      },
      {
        k: 'undo',
        t: 'Undo history depth',
        d: 'Number of undo steps kept in memory. Higher values use more RAM for large puzzles.',
        type: 'select',
        opts: [25, 50, 100, 200, 500],
      },
      {
        k: 'tips',
        t: 'Show tooltips',
        d: 'Show short help text when the mouse rests on buttons and other controls.',
        type: 'sw',
      },
      {
        k: 'updates',
        t: 'Check for updates at startup',
        d: 'Ask GitHub once each time ShapeLogic starts whether a newer version is out, and say so if it is. You can also check any time by selecting "Check for Updates…" in the main dropdown menu.',
        type: 'sw',
      },
      {
        t: 'Cache folder',
        d: 'Where the solver keeps its cache data, which can take a lot of disk space: the files a sliding search writes once it outgrows its memory, and the saved searches of sliding and stacking puzzles that Continue carries on, in a sliding and a panex folder. By default it is ShapeLogic’s folder in your system’s application data folder. Choose Folder… makes a ShapeLogic folder in the folder you choose to hold it, and checks at once that it can write there. If the cache folder in use holds data, you are asked whether to move it to the new folder or delete it and start clean. It can’t be changed while a solve runs. Use Default goes back to the default folder; Reset section and Restore all defaults leave the cache folder as it is.',
        type: 'cache',
        when: () => cacheInfo !== null,
      },
    ],
  },
  {
    id: 'grid',
    name: 'Grid editor',
    rows: [
      {
        k: 'gridPlane',
        t: 'Grid plane',
        d: 'The plane the voxel editor’s grid starts in: the XY, XZ and YZ buttons above the grid. XY shows the shape in layers along Z, XZ in layers along Y, YZ in layers along X. The grid is set to it when a puzzle is opened or made, and when you change it here; the buttons still switch the plane at any time. Grids other than cubes have only XY.',
        type: 'seg',
        opts: ['XY', 'XZ', 'YZ'],
      },
      {
        t: 'New shape size',
        d: `The size of a shape made with + New when no shape is selected, in X, Y and Z, as the size steppers count it (blocks on grids that size in blocks). Normal Burrs is the size on every grid but sliding; Sliding is the size on a sliding puzzle’s board. With a shape selected, + New copies that shape’s size. A disc of a stacking puzzle is always size 1. From 1 to ${String(NEW_SIZE_MAX)} in each direction.`,
        type: 'xyz',
        sets: [
          { name: 'Normal Burrs', k: ['newX', 'newY', 'newZ'] },
          { name: 'Sliding', k: ['slideX', 'slideY', 'slideZ'] },
        ],
      },
      {
        k: 'boardColourLight',
        t: 'Sliding board color, Light theme',
        d: 'The color of a sliding puzzle’s boards (its start and goal shapes) in the Light theme, in the shapes list, the 2D editor and the 3D view. A board that has not been given a color of its own follows the theme: it takes this color in the Light theme and the Dark theme’s color in the Dark theme. A board you color with the pencil in the voxel editor keeps its color.',
        type: 'colour',
      },
      {
        k: 'boardColourDark',
        t: 'Sliding board color, Dark theme',
        d: 'The color of a sliding puzzle’s boards in the Dark theme. See the Light theme’s setting.',
        type: 'colour',
      },
      {
        k: 'hintLayer',
        t: 'Hinted layer',
        d: 'The voxel editor shows faintly where the shape has cells in a layer next to the one you edit, so you can see what you build on: a pale fill in the shape’s colour under the edited layer’s cells. Below hints the layer under it (the original’s), Above the layer over it. In the XZ and YZ planes the layers are along Y and X. The bottom layer has nothing below it, and the top nothing above.',
        type: 'seg',
        opts: ['Below', 'Above'],
        group: 'Voxel hint layer',
      },
    ],
  },
  {
    id: 'view',
    name: '3D view',
    rows: [
      {
        k: 'cube',
        t: 'Show view cube',
        d: 'Display the 3D orientation cube in the corner of the 3D preview. Click it to snap to a face, edge, or corner view.',
        type: 'sw',
      },
      {
        k: 'rev',
        t: 'Reverse scroll zoom direction',
        d: 'Reverse the direction of the preview zoom when the mouse wheel is used.',
        type: 'sw',
      },
      {
        k: 'rot',
        t: 'Rotation method',
        d: 'Free Spin has no limits: the shape turns whichever way you drag it, as far as you like, and can end up upside down (the original’s arc-ball). Top Up spins the shape around its vertical axis without limit, but tilts it only until you look straight down or straight up, so it never turns upside down. The vertical axis is the one Z axis direction stands up.',
        type: 'seg',
        opts: ['Free Spin', 'Top Up'],
      },
      {
        k: 'zAxis',
        t: 'Z axis direction',
        d: 'Which way the axes point in the home view (the view cube’s house, or Home) and in the first view when the app starts. Z Towards: X across, Y up, Z coming towards you. Z Up: X across, Z up, Y going away from you. The home view looks from a little above and to one side. A stacking puzzle always stands its stack up.',
        type: 'seg',
        opts: ['Z Towards', 'Z Up'],
      },
      {
        k: 'lights',
        t: 'Lighting',
        d: 'Light the 3D preview so pieces look solid and shaded. Turn off for a flatter, unlit appearance.',
        type: 'sw',
      },
      {
        k: 'fade',
        t: 'Fade out removed pieces',
        d: 'During a disassembly animation, pieces that have been removed fade away instead of vanishing at once.',
        type: 'sw',
      },
    ],
  },
  {
    id: 'perf',
    name: 'Solver',
    rows: [
      {
        k: 'threads',
        t: 'Worker threads',
        d: 'Threads used for assembling and disassembling. Both stages share this pool, so raising it speeds up both.',
        type: 'slider',
        min: 1,
      },
      {
        k: 'memGB',
        t: 'Search memory',
        d: 'The memory sliding and stacking searches may fill with the arrangements they have found. Searches running at the same time share it equally: each gets a smaller part when another starts, and a larger one again when one ends. Automatic (the bold tick) is half of this computer’s memory. More lets a large search go further; the choices go up in steps of 2 GB to 4 GB less than the memory, and above three quarters of it the computer may become slow or unstable while a search runs. The Sliding Puzzle Solver keeps older moves on disk, so a smaller part only slows it down; a stacking search that outgrows its part stops, and a Panex search can be carried on with Continue once the others have finished.',
        type: 'steps',
        steps: memorySteps,
        label: (v) => (v === 0 ? `${gbText(autoMemoryGB())} (automatic)` : gbText(v)),
        ticks: true,
        warn: memoryWarn,
        group: 'Memory',
      },
      {
        k: 'slideDiskGB',
        t: 'Sliding disk space',
        d: 'The disk the Sliding Puzzle Solver may fill with older moves while it runs, in your cache folder; the files go when the search ends. Automatic is half the free space, at most 200 GB. A larger choice is held to what the disk has free, less a little room for everything else, so the search stops before the disk is full.',
        type: 'steps',
        steps: () => DISK_GB,
        label: (v) => (v === 0 ? 'Automatic' : gbText(v)),
        group: 'Disk space',
      },
      {
        k: 'panexDiskGB',
        t: 'Stacking disk space',
        d: 'The disk a stacking search (the Panex Solver) may take for the levels it saves, so Pause and Continue can carry it on. Automatic is half the free space, at most 200 GB. Less disk never stops the search, but keeps fewer levels, so finding the path at the end takes longer. A larger choice is held to what the disk has free.',
        type: 'steps',
        steps: () => DISK_GB,
        label: (v) => (v === 0 ? 'Automatic' : gbText(v)),
      },
    ],
  },
  {
    id: 'search',
    name: 'Search Options',
    note: `These are the defaults for the <b>Search Options</b> shown in the Solver: what a problem starts with when it has saved none of its own. Changing an option on the Solver tab changes it for that problem only (it is saved with the puzzle), and leaves these defaults as they are. Where an option exists only for some puzzle types, they are named under it; ${BURRS} are every puzzle type but ${SLIDING} and ${STACKING}.`,
    rows: [
      {
        k: 'optKeepAssemblies',
        t: LABEL.keepAssemblies,
        d: TIP.keepAssemblies,
        type: 'sw',
        only: BURRS,
      },
      { k: 'optJustCount', t: LABEL.justCount, d: TIP.justCount, type: 'sw' },
      {
        k: 'optJustLevels',
        t: LABEL.justLevels,
        d: TIP.justLevels,
        type: 'sw',
        only: `${BURRS}, ${STACKING}`,
      },
      { k: 'optKeepMirrors', t: LABEL.keepMirrors, d: TIP.keepMirrors, type: 'sw', only: BURRS },
      {
        k: 'optKeepRotations',
        t: LABEL.keepRotations,
        d: TIP.keepRotations,
        type: 'sw',
        only: BURRS,
      },
      {
        k: 'optDeepSymmetry',
        t: LABEL.deepSymmetry,
        d: TIP.deepSymmetry,
        type: 'sw',
        only: BURRS,
      },
      {
        k: 'optStrictColors',
        t: LABEL.strictColors,
        d: TIP.strictColors,
        type: 'sw',
        only: BURRS,
      },
      {
        k: 'optAnyOrientation',
        t: LABEL.anyOrientation,
        d: TIP.anyOrientation,
        type: 'sw',
        only: SLIDING,
      },
      {
        k: 'optMirroredOrientations',
        t: LABEL.mirroredOrientations,
        d: TIP.mirroredOrientations,
        type: 'sw',
        only: SLIDING,
      },
      {
        k: 'optNestedSlides',
        t: LABEL.nestedSlides,
        d: TIP.nestedSlides,
        type: 'sw',
        only: SLIDING,
      },
      {
        k: 'optPartialNested',
        t: LABEL.partialNested,
        d: TIP.partialNested,
        type: 'sw',
        only: SLIDING,
      },
      {
        k: 'optVerticalMoves',
        t: LABEL.verticalMoves,
        d: TIP.verticalMoves,
        type: 'sw',
        only: SLIDING,
      },
      {
        k: 'solverType',
        t: 'Solver type',
        d: 'The Solver type a problem starts with when it has none saved: Complete (ShapeLogic) or Fast (Crowell). A problem that saved its own keeps it. Sliding and stacking puzzles have solvers of their own.',
        type: 'seg',
        opts: ['Complete', 'Fast'],
        only: BURRS,
      },
      {
        k: 'optKeepEach',
        t: KEEP_EACH.label,
        d: `${KEEP_EACH.tip}. No problem saves this: the Solver starts with it each time ShapeLogic starts.`,
        type: 'num',
        min: 1,
        step: 1,
      },
      {
        k: 'optKeepAtMost',
        t: KEEP_AT_MOST.label,
        d: `${KEEP_AT_MOST.tip}. No problem saves this: the Solver starts with it each time ShapeLogic starts.`,
        type: 'num',
        min: 0,
        step: 10,
      },
    ],
  },
];

let page = 'general';
let query = '';
let store: Store | null = null;
/** The cache folder in use, from the main process; null until it says, or where there is none (the demo). */
let cacheInfo: CacheFolderInfo | null = null;

/** The machine's threads (the slider's end), from the engine. */
function maxThreads(): number {
  const info = store?.info;
  return Math.max(1, Math.min(info?.maxThreads ?? 32, info?.hardwareThreads ?? 32));
}

/** The threads shown: the engine's 0 is every hardware thread. */
function threadsShown(): number {
  return prefs.threads === 0 ? maxThreads() : prefs.threads;
}

/** A row's default, as the "Default:" line under its description says it. */
function defaultText(r: Row): string {
  const on = (k: keyof Prefs): string => String(DEFAULTS[k]);
  switch (r.type) {
    case 'sw':
      return DEFAULTS[r.k] === true ? 'On' : 'Off';
    case 'steps':
      return r.label(DEFAULTS[r.k] as number);
    case 'slider':
      return DEFAULTS[r.k] === 0 ? `${String(maxThreads())} (all)` : on(r.k);
    case 'xyz':
      return r.sets.map((x) => `${x.name} ${x.k.map(on).join(' × ')}`).join(', ');
    case 'widths':
      return WIDTHS.map(({ k, name }) => `${name} ${on(k)} px`).join(', ');
    case 'cache':
      return cacheInfo?.defaultFolder ?? 'ShapeLogic’s application data folder';
    case 'colour':
    case 'seg':
    case 'select':
    case 'num':
      return on(r.k);
  }
}

/** The description with the default under it. */
const descHtml = (r: Row): string =>
  `<span>${esc(r.d)}</span><span class="def">Default: ${esc(defaultText(r))}</span>`;

/** The row's name with, under it, the puzzle types that have the setting when not all do. */
const nameHtml = (r: Row): string =>
  `<b>${esc(r.t)}</b>${r.only === undefined ? '' : `<em class="only">(Only: ${esc(r.only)})</em>`}`;

/** A number held to a number row's range. */
const numValue = (r: { min: number }, n: number): number =>
  Math.min(LIMIT_MAX, Math.max(r.min, Math.round(n)));

/** The number row of a setting. */
function numRow(k: string): (Row & { type: 'num' }) | undefined {
  const row = SET.flatMap((p) => p.rows).find((x) => x.type === 'num' && x.k === k);
  return row?.type === 'num' ? row : undefined;
}

/** The cache folder's row: the folder in use, under it its buttons. */
function cacheRowHtml(r: Row): string {
  const info = cacheInfo;
  const path =
    info === null
      ? ''
      : `<small>In use</small><code>${esc(info.custom ?? info.defaultFolder)}</code>`;
  const isDefault = info?.custom === null;
  return `<div class="setrow under"><div class="t">${nameHtml(r)}${descHtml(r)}</div><div class="cachefolder"><div class="cachepaths">${path}</div><div class="cachebt"><button class="btn sm" data-cache="choose" title="Choose the folder to keep the cache data in">Choose Folder…</button><button class="btn sm" data-cache="default" title="${esc(isDefault ? 'The default cache folder is in use' : 'Go back to the default cache folder')}" ${isDefault ? 'disabled' : ''}>Use Default</button></div></div></div>`;
}

function rowHtml(r: Row): string {
  if (r.type === 'cache') {
    return cacheRowHtml(r);
  }
  if (r.type === 'num') {
    const v = prefs[r.k];
    const what = r.t.toLowerCase();
    return `<div class="setrow"><div class="t">${nameHtml(r)}${descHtml(r)}</div><div class="step"><button data-num="${r.k}" data-s="-1" title="${esc(`Decrease ${what} by ${String(r.step)}`)}" ${v <= r.min ? 'disabled' : ''}>${ic('minus', 12)}</button><input class="v" data-num="${r.k}" value="${String(v)}" inputmode="numeric" aria-label="${esc(r.t)}" title="${esc(r.d)}"><button data-num="${r.k}" data-s="1" title="${esc(`Increase ${what} by ${String(r.step)}`)}" ${v >= LIMIT_MAX ? 'disabled' : ''}>${ic('plus', 12)}</button></div></div>`;
  }
  if (r.type === 'widths') {
    const steps = WIDTHS.map(({ k, name }) => {
      const v = prefs[k];
      return `<div class="step"><button data-w="${k}" data-s="-1" title="Narrow the ${name.toLowerCase()} side card" ${v <= SIDE_MIN ? 'disabled' : ''}>${ic('minus', 12)}</button><input class="v" data-w="${k}" value="${String(v)}" inputmode="numeric" aria-label="${esc(`${name} sidebar width`)}" title="Type the ${name.toLowerCase()} side card’s width in pixels"><button data-w="${k}" data-s="1" title="Widen the ${name.toLowerCase()} side card" ${v >= SIDE_MAX ? 'disabled' : ''}>${ic('plus', 12)}</button></div>`;
    });
    const sets = WIDTHS.map(
      (x, i) => `<small>${esc(x.name)}</small><div class="size3">${steps[i] ?? ''}</div>`,
    ).join('');
    return `<div class="setrow"><div class="t"><b>${esc(r.t)}</b>${descHtml(r)}</div><div class="xyzsets">${sets}</div></div>`;
  }
  if (r.type === 'xyz') {
    const sets = r.sets
      .map((x) => {
        const steps = x.k
          .map((k, i) => {
            const a = 'XYZ'[i] ?? '';
            const v = prefs[k];
            return `<div class="step"><b class="ax-${a.toLowerCase()}">${a}</b><button data-n="${k}" data-s="-1" title="Decrease ${a}" ${v <= 1 ? 'disabled' : ''}>${ic('minus', 12)}</button><input class="v" data-n="${k}" value="${String(v)}" inputmode="numeric" aria-label="${esc(`${r.t} ${x.name} ${a}`)}" title="Type the size in ${a}"><button data-n="${k}" data-s="1" title="Increase ${a}" ${v >= NEW_SIZE_MAX ? 'disabled' : ''}>${ic('plus', 12)}</button></div>`;
          })
          .join('');
        return `<small>${esc(x.name)}</small><div class="size3">${steps}</div>`;
      })
      .join('');
    return `<div class="setrow"><div class="t"><b>${esc(r.t)}</b>${descHtml(r)}</div><div class="xyzsets">${sets}</div></div>`;
  }
  const v = prefs[r.k];
  let c = '';
  switch (r.type) {
    case 'colour':
      c = `<button class="swatch" data-colour="${r.k}" aria-label="${esc(r.t)}" title="Pick the color: ${esc(String(v))}" style="width:60px;height:28px;border-radius:6px;border:1px solid var(--line2);cursor:pointer;background:${esc(String(v))}"></button>`;
      break;
    case 'sw':
      c = `<div class="switch ${v === true ? 'on' : ''}" role="switch" aria-checked="${v === true}" aria-label="${esc(r.t)}" title="${esc(r.d)}" tabindex="0" data-k="${r.k}"><span class="tr"></span></div>`;
      break;
    case 'select': {
      const opts = r.opts.includes(v as number)
        ? r.opts
        : [...r.opts, v as number].sort((a, b) => a - b);
      c = `<select class="dd" data-k="${r.k}" aria-label="${esc(r.t)}" title="${esc(r.d)}">${opts.map((o) => `<option value="${o}" ${o === v ? 'selected' : ''}>${o}</option>`).join('')}</select>`;
      break;
    }
    case 'steps': {
      const steps = r.steps();
      const auto = r.k === 'memGB' ? autoMemoryGB() : 0;
      const n = v as number;
      if (r.ticks === true) {
        const warn = r.warn?.(n) ?? '';
        const at = stepOf(steps, n, auto);
        const pct = steps.length > 1 ? (at / (steps.length - 1)) * 100 : 0;
        // the value over the slider, the tick marks and the warning under it
        c = `<div class="rangebox"><b>${esc(r.label(n))}</b><input type="range" min="0" max="${String(steps.length - 1)}" step="1" value="${String(at)}" data-k="${r.k}" data-steps="${steps.join(',')}" aria-label="${esc(r.t)}" aria-valuetext="${esc(r.label(n))}" title="${esc(r.d)}" style="--pct:${String(pct)}%"><div class="ticks" aria-hidden="true">${ticksHtml(steps)}</div><div class="rangewarn" role="status" ${warn === '' ? 'hidden' : ''}>${esc(warn)}</div></div>`;
        break;
      }
      c = `<div style="display:flex;align-items:center;gap:10px;width:260px"><input type="range" min="0" max="${String(steps.length - 1)}" step="1" value="${String(stepOf(steps, n, auto))}" data-k="${r.k}" data-steps="${steps.join(',')}" aria-label="${esc(r.t)}" aria-valuetext="${esc(r.label(n))}" title="${esc(r.d)}" style="flex:1;accent-color:var(--accent)"><b style="width:118px;text-align:right;white-space:nowrap">${esc(r.label(n))}</b></div>`;
      break;
    }
    case 'slider': {
      const shown = threadsShown();
      c = `<div style="display:flex;align-items:center;gap:10px;width:230px"><input type="range" min="${r.min}" max="${maxThreads()}" value="${shown}" data-k="${r.k}" aria-label="${esc(r.t)}" title="${esc(r.d)}" style="flex:1;accent-color:var(--accent)"><b style="width:26px;text-align:right">${shown}</b></div>`;
      break;
    }
    case 'seg':
      c = `<div class="seg">${r.opts.map((o) => `<button class="${o === v ? 'on' : ''}" data-k="${r.k}" data-o="${esc(o)}" aria-pressed="${o === v}" title="${esc(`${r.t}: ${o}`)}">${esc(o)}</button>`).join('')}</div>`;
      break;
  }
  // more than three choices do not fit beside the text: they go under it
  const under = r.type === 'seg' && r.opts.length > 3;
  return `<div class="setrow${under ? ' under' : ''}"><div class="t">${nameHtml(r)}${descHtml(r)}</div>${c}</div>`;
}

/** The keys of a row as kbd chips (the mockup's kbKeys). */
export function kbKeys(spec: string): string {
  return spec
    .split(' / ')
    .map((alt) =>
      alt
        .split(' · ')
        .map((part) =>
          part.startsWith('@')
            ? `<em>${esc(part.slice(1))}</em>`
            : part
                .split(/\+(?=.)/)
                .map((t) =>
                  t.startsWith('@') ? `<em>${esc(t.slice(1))}</em>` : `<kbd>${esc(t)}</kbd>`,
                )
                .join('<i>+</i>'),
        )
        .join('<i>·</i>'),
    )
    .join('<i>/</i>');
}

/** A row of the keyboard map as the help prints it. */
export const kbRowHtml = (r: KeyRow): string =>
  `<div class="kbrow"><span>${esc(r.does)}</span><span class="k">${kbKeys(r.keys)}</span></div>`;

/** Rows with the headings of their groups before them. */
function rowsHtml(rows: readonly Row[]): string {
  return rows
    .map(
      (r) =>
        (r.group === undefined ? '' : `<div class="setgrp">${esc(r.group)}</div>`) + rowHtml(r),
    )
    .join('');
}

/** Draws the dialog's navigation and page. */
export function renderSettings(): void {
  byId('setNav').innerHTML =
    SET.map(
      (p) =>
        `<button class="${query === '' && p.id === page ? 'on' : ''}" data-p="${p.id}" title="${esc(`Show the ${p.name} settings`)}">${esc(p.name)}</button>`,
    ).join('') +
    `<button class="${query === '' && page === 'keys' ? 'on' : ''}" data-p="keys" title="Show every keyboard shortcut">Keyboard shortcuts</button>`;
  let h = '';
  if (query !== '') {
    const q = query.toLowerCase();
    for (const p of SET) {
      const rs = p.rows.filter(
        (r) => shown(r) && `${r.t} ${r.only ?? ''} ${r.d}`.toLowerCase().includes(q),
      );
      if (rs.length > 0) {
        h += `<h3>${esc(p.name)}</h3>` + rowsHtml(rs);
      }
    }
    const hk = keyGroups()
      .flatMap((g) =>
        g.rows.filter((r) => `${r.keys} ${r.does} ${g.group}`.toLowerCase().includes(q)),
      )
      .map(kbRowHtml);
    if (hk.length > 0) {
      h += '<h3>Keyboard shortcuts</h3>' + hk.join('');
    }
    if (h === '') {
      h = `<div class="mute" style="padding:60px 0;text-align:center">No settings or shortcuts match “${esc(query)}”</div>`;
    }
  } else if (page === 'keys') {
    h =
      '<div class="pgh"><h3>Keyboard shortcuts</h3></div><div class="kbnote">Grouped by workspace. In the application this list lives in <b>Help ▸ Keyboard shortcuts</b> (F1). Keys act when focus is not in a text field; menu shortcuts (File, Edit, …) are shown in the menus.</div>' +
      keyGroups()
        .map(
          (g) =>
            `<div class="kbgrp">${esc(g.group)}${g.sub === undefined ? '' : `<small>${esc(g.sub)}</small>`}</div>` +
            g.rows.map(kbRowHtml).join(''),
        )
        .join('');
  } else {
    const p = SET.find((x) => x.id === page) ?? SET[0];
    if (p !== undefined) {
      h =
        `<div class="pgh"><h3>${esc(p.name)}</h3><button class="btn sm" data-reset="${p.id}" title="Restore this section’s settings to their default values">Reset section</button></div>` +
        (p.note === undefined ? '' : `<div class="kbnote" id="setNote">${p.note}</div>`) +
        rowsHtml(p.rows.filter(shown));
    }
  }
  byId('setPg').innerHTML = h;
}

/** Whether the dialog is open. */
export function settingsOpen(): boolean {
  return byId('scrim').classList.contains('show');
}

let returnFocus: HTMLElement | null = null;

/** Opens the dialog, on a page if given. */
export function openSettings(on?: 'keys'): void {
  if (on !== undefined) {
    page = on;
    query = '';
    byId<HTMLInputElement>('setSearch').value = '';
  }
  if (!settingsOpen()) {
    returnFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  }
  closePop();
  byId('scrim').classList.add('show');
  renderSettings();
  byId('setSearch').focus();
}

/** Closes the dialog; the focus goes back where it was. */
export function closeSettings(): void {
  byId('scrim').classList.remove('show');
  if (returnFocus !== null && returnFocus !== document.body && returnFocus.isConnected) {
    returnFocus.focus();
  } else {
    // nothing had the focus: it must not stay in the hidden dialog's
    // search field, where every key counts as typing
    const active = document.activeElement;
    if (active instanceof HTMLElement && byId('scrim').contains(active)) {
      active.blur();
    }
  }
  returnFocus = null;
}

/** Applies a change of settings, sending the engine's to the engine. */
function change(c: Partial<Prefs>): void {
  setPrefs(c);
  renderSettings();
  const engine: Partial<Settings> = {};
  if (c.undo !== undefined) {
    engine.undoDepth = c.undo;
  }
  if (c.threads !== undefined) {
    engine.threads = c.threads;
  }
  if (c.memGB !== undefined) {
    engine.searchMemoryGB = c.memGB;
  }
  if (c.slideDiskGB !== undefined) {
    engine.slideDiskGB = c.slideDiskGB;
  }
  if (c.panexDiskGB !== undefined) {
    engine.panexDiskGB = c.panexDiskGB;
  }
  if (store !== null && Object.keys(engine).length > 0) {
    store.setEngineSettings(engine).catch((e: unknown) => {
      reportError(e, 'The setting was not taken');
    });
  }
}

/** The cache folder in use, in words. */
const cacheName = (info: CacheFolderInfo | null): string =>
  info === null ? 'the cache folder in use' : (info.custom ?? info.defaultFolder);

/** Asks the main process for the cache folder in use again, and shows it. */
async function refreshCache(): Promise<void> {
  cacheInfo = await window.btApp.cacheFolder();
  if (settingsOpen()) {
    renderSettings();
  }
}

/**
 * Changes the cache folder: the folder dialog (or the defaults), the new
 * folder made and written at once, then, when the folders in use hold
 * data, whether to move it there or delete it and start clean. True when
 * the cache folder is another now.
 */
async function changeCache(useDefault: boolean): Promise<boolean> {
  const app = window.btApp;
  const c = await app.chooseCacheFolder(useDefault);
  switch (c.status) {
    case 'cancelled':
    case 'same':
      return false;
    case 'busy':
      tell(c.reason);
      return false;
    case 'invalid':
      tell(
        `The new cache destination is invalid: ${c.error} (${c.folder}). ShapeLogic keeps using ${cacheName(cacheInfo)}.`,
      );
      return false;
    case 'ready':
      break;
  }
  let action: CacheAction = 'move';
  if (c.found !== '') {
    const k = await askChoice(
      'cacheData',
      'Cache Data Found',
      `${cacheName(cacheInfo).replace(/^the/, 'The')} holds ${c.found}.\n\nMove it to ${c.folder}, or delete it and start clean there?`,
      [
        { key: 'cancel', label: 'Cancel', tip: 'Keep the cache folder in use and its data' },
        {
          key: 'purge',
          label: 'Delete and Start Clean',
          kind: 'danger',
          tip: 'Delete the cache data, and use the new folder from empty',
        },
        {
          key: 'move',
          label: 'Move Data',
          kind: 'pri',
          default: true,
          tip: 'Move the cache data to the new folder, and use it',
        },
      ],
    );
    action = k === 'move' || k === 'purge' ? k : 'cancel';
  }
  if (action === 'move' && c.found !== '') {
    void openDialog({
      id: 'cacheMoving',
      title: 'Moving Cache Data',
      body: `<p>Moving ${esc(c.found)} to ${esc(c.folder)}. On another disk this can take a while.</p>`,
      buttons: [],
      modalOnly: true,
    });
  }
  const r = await app.finishCacheFolder(action).finally(() => {
    closeDialog('cacheMoving');
  });
  if (!r.ok) {
    tell(`The cache folder was not changed: ${r.error}.`);
    await refreshCache();
    return false;
  }
  cacheInfo = r.info;
  if (settingsOpen()) {
    renderSettings();
  }
  if (action !== 'cancel') {
    flash(`The cache folder is now ${cacheName(r.info)}`);
  }
  return action !== 'cancel';
}

/**
 * Settings ▸ General ▸ Cache folder's Choose Folder…, for the solver's
 * question about a cache folder its search could not write (decision
 * 0028). True when the cache folder is another now.
 */
export function chooseCacheFolder(): Promise<boolean> {
  return changeCache(false);
}

/** A new shape size held to the settings' range. */
const newSize = (n: number): number => Math.min(NEW_SIZE_MAX, Math.max(1, n));

/** A side card width held to the settings' range. */
const sideWidth = (n: number): number => Math.min(SIDE_MAX, Math.max(SIDE_MIN, n));

function defaultsOf(rows: readonly Row[]): Partial<Prefs> {
  return Object.fromEntries(rows.flatMap(keysOf).map((k) => [k, DEFAULTS[k]]));
}

/** Wires the dialog; the engine's settings come from the store. */
export function installSettings(s: Store): void {
  store = s;
  s.subscribe((c) => {
    if (c.kind === 'settings' && s.settings !== null) {
      setPrefs({
        undo: s.settings.undoDepth,
        threads: s.settings.threads,
        memGB: s.settings.searchMemoryGB,
        slideDiskGB: s.settings.slideDiskGB,
        panexDiskGB: s.settings.panexDiskGB,
      });
      if (settingsOpen()) {
        renderSettings();
      }
    }
  });
  // the cache folder in use, and what the start found wrong with a folder chosen earlier
  refreshCache().then(
    () => {
      if (cacheInfo?.notice !== undefined) {
        tell(cacheInfo.notice);
      }
    },
    (e: unknown) => {
      reportError(e, 'The cache folder could not be read');
    },
  );
  byId('btnSettings').innerHTML = ic('gear');
  byId('setX').innerHTML = ic('close');
  byId('btnSettings').addEventListener('click', () => {
    openSettings();
  });
  byId('setX').addEventListener('click', closeSettings);
  byId('setDone').addEventListener('click', closeSettings);
  byId('scrim').addEventListener('mousedown', (e) => {
    if ((e.target as Element).id === 'scrim') {
      closeSettings();
    }
  });
  byId('setNav').addEventListener('click', (e) => {
    const b = (e.target as Element).closest<HTMLElement>('button');
    if (b === null) {
      return;
    }
    page = b.dataset.p ?? 'general';
    query = '';
    byId<HTMLInputElement>('setSearch').value = '';
    renderSettings();
  });
  byId('setSearch').addEventListener('input', () => {
    query = byId<HTMLInputElement>('setSearch').value.trim();
    renderSettings();
  });
  byId('setDefaults').addEventListener('click', () => {
    change(defaultsOf(SET.flatMap((p) => p.rows)));
  });
  const pg = byId('setPg');
  const toggle = (k: string): void => {
    const key = k as keyof Prefs;
    change({ [key]: prefs[key] !== true });
    pg.querySelector<HTMLElement>(`.switch[data-k="${k}"]`)?.focus();
  };
  pg.addEventListener('click', (e) => {
    const t = e.target as Element;
    const sw = t.closest<HTMLElement>('.switch');
    if (sw !== null) {
      toggle(sw.dataset.k ?? '');
      return;
    }
    const b = t.closest<HTMLElement>('button');
    if (b === null) {
      return;
    }
    if (b.dataset.cache !== undefined) {
      changeCache(b.dataset.cache === 'default').catch((err: unknown) => {
        closeDialog('cacheMoving');
        reportError(err, 'The cache folder was not changed');
      });
    } else if (b.dataset.colour !== undefined) {
      const k = b.dataset.colour as 'boardColourLight' | 'boardColourDark';
      openColourPicker(b, {
        rgb: prefs[k],
        onPick: (rgb) => {
          change({ [k]: rgb });
        },
        onDefault: () => {
          change({ [k]: DEFAULTS[k] });
        },
        isDefault: prefs[k] === DEFAULTS[k],
      });
    } else if (b.dataset.w !== undefined && b.dataset.s !== undefined) {
      const k = b.dataset.w as (typeof WIDTHS)[number]['k'];
      change({ [k]: sideWidth(prefs[k] + WIDTH_STEP * Number(b.dataset.s)) });
      pg.querySelector<HTMLElement>(`button[data-w="${k}"][data-s="${b.dataset.s}"]`)?.focus();
    } else if (b.dataset.num !== undefined && b.dataset.s !== undefined) {
      const row = numRow(b.dataset.num);
      if (row !== undefined) {
        change({ [row.k]: numValue(row, prefs[row.k] + row.step * Number(b.dataset.s)) });
        pg.querySelector<HTMLElement>(
          `button[data-num="${row.k}"][data-s="${b.dataset.s}"]`,
        )?.focus();
      }
    } else if (b.dataset.n !== undefined && b.dataset.s !== undefined) {
      const k = b.dataset.n as XyzKey;
      change({ [k]: newSize(prefs[k] + Number(b.dataset.s)) });
      pg.querySelector<HTMLElement>(`button[data-n="${k}"][data-s="${b.dataset.s}"]`)?.focus();
    } else if (b.dataset.reset !== undefined) {
      const p = SET.find((x) => x.id === b.dataset.reset);
      if (p !== undefined) {
        change(defaultsOf(p.rows));
      }
    } else if (b.dataset.o !== undefined && b.dataset.k !== undefined) {
      change({ [b.dataset.k]: b.dataset.o });
    }
  });
  pg.addEventListener('change', (e) => {
    const t = e.target as HTMLInputElement | HTMLSelectElement;
    if (t.dataset.w !== undefined) {
      // a width typed in: whole pixels, held to the range; anything else shows the width again
      const n = Number(t.value.trim());
      if (t.value.trim() !== '' && Number.isFinite(n)) {
        change({ [t.dataset.w]: sideWidth(Math.round(n)) });
      }
      renderSettings();
      return;
    }
    if (t.dataset.num !== undefined) {
      // a number typed in: whole, in the row's range; anything else shows the number again
      const row = numRow(t.dataset.num);
      const n = Number(t.value.trim());
      if (row !== undefined && t.value.trim() !== '' && Number.isFinite(n)) {
        change({ [row.k]: numValue(row, n) });
      }
      renderSettings();
      return;
    }
    if (t.dataset.n !== undefined) {
      // a size typed in: whole, 1 to NEW_SIZE_MAX; anything else shows the size again
      const n = Number(t.value.trim());
      if (t.value.trim() !== '' && Number.isFinite(n)) {
        change({ [t.dataset.n]: newSize(Math.round(n)) });
      }
      renderSettings();
      return;
    }
    if (t.dataset.k !== undefined && t.dataset.steps !== undefined) {
      const steps = t.dataset.steps.split(',').map(Number);
      change({ [t.dataset.k]: steps[Number(t.value)] ?? 0 });
      return;
    }
    if (t.dataset.k !== undefined && (t.tagName === 'SELECT' || t.type === 'range')) {
      change({ [t.dataset.k]: Number(t.value) });
    }
  });
  pg.addEventListener('input', (e) => {
    const t = e.target as HTMLInputElement;
    if (t.type !== 'range' || t.nextElementSibling === null) {
      return;
    }
    const row = SET.flatMap((p) => p.rows).find((r) => r.type === 'steps' && r.k === t.dataset.k);
    if (row?.type === 'steps' && t.dataset.steps !== undefined) {
      // the size the stop stands for, before the change is applied
      const v = Number(t.dataset.steps.split(',')[Number(t.value)] ?? 0);
      const box = t.closest('.rangebox');
      (box?.querySelector('b') ?? t.nextElementSibling).textContent = row.label(v);
      t.setAttribute('aria-valuetext', row.label(v));
      if (box !== null) {
        t.style.setProperty('--pct', rangePct(t));
        const warn = box.querySelector<HTMLElement>('.rangewarn');
        if (warn !== null) {
          const text = row.warn?.(v) ?? '';
          warn.textContent = text;
          warn.hidden = text === '';
        }
      }
    } else {
      t.nextElementSibling.textContent = t.value;
    }
  });
  pg.addEventListener('keydown', (e) => {
    const v = (e.target as Element).closest<HTMLInputElement>('input[data-n],input[data-num]');
    if (v !== null && e.key === 'Enter') {
      v.blur();
      return;
    }
    const sw = (e.target as Element).closest<HTMLElement>('.switch');
    if (sw !== null && (e.key === ' ' || e.key === 'Enter')) {
      e.preventDefault();
      toggle(sw.dataset.k ?? '');
    }
  });
  // the dialog keeps the keyboard: Tab cycles inside it
  byId('scrim').addEventListener('keydown', (e) => {
    if (e.key !== 'Tab') {
      return;
    }
    const all = [
      ...byId('scrim').querySelectorAll<HTMLElement>(
        'button:not(:disabled),input,select,[tabindex="0"]',
      ),
    ].filter((el) => el.offsetParent !== null);
    const first = all[0];
    const last = all.at(-1);
    if (first === undefined || last === undefined) {
      return;
    }
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first.focus();
    }
  });
  addEscapeStep(ESC_ORDER.dialog, () => {
    if (!settingsOpen()) {
      return false;
    }
    closeSettings();
    return true;
  });
}
