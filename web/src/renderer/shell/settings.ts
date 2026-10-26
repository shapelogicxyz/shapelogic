// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings dialog: searchable, four pages and the keyboard shortcuts,
// "Reset section", "Restore all defaults", changes applied at once. From
// docs/mockup/index.html: `SET`, `rowHtml`, `kbKeys`, `kbRow`,
// `renderSettings`, `openSettings`, `closeSettings` and the dialog's
// handlers. The mockup's "Use OpenGL display lists" is dropped (no OpenGL;
// docs/webui-spec.md section 1). Undo depth and worker threads go to the
// engine (settings.set); the threads' range is the machine's
// (engine.info's hardware threads). The shortcuts page prints the one
// keyboard table (keys.ts).

import type { Store } from '../state/store.ts';
import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';
import { ic } from './icons.ts';
import { keyGroups } from './keys.ts';
import { closePop } from './popover.ts';
import type { KeyRow } from './keys.ts';
import { DEFAULTS, NEW_SIZE_MAX, prefs, setPrefs } from './prefs.ts';
import type { Prefs } from './prefs.ts';
import { reportError } from './status.ts';

type Row =
  | { k: keyof Prefs; t: string; d: string; type: 'sw' }
  | { k: keyof Prefs; t: string; d: string; type: 'seg'; opts: readonly string[] }
  | { k: keyof Prefs; t: string; d: string; type: 'select'; opts: readonly number[] }
  | { k: keyof Prefs; t: string; d: string; type: 'slider'; min: number }
  | { t: string; d: string; type: 'xyz'; sets: readonly XyzSet[] };

/** The settings an X, Y and Z row sets. */
type XyzKey = 'newX' | 'newY' | 'newZ' | 'slideX' | 'slideY' | 'slideZ';

/** One labelled X, Y and Z of an xyz row. */
interface XyzSet {
  name: string;
  k: readonly [XyzKey, XyzKey, XyzKey];
}

/** The settings of a row. */
const keysOf = (r: Row): readonly (keyof Prefs)[] =>
  r.type === 'xyz' ? r.sets.flatMap((x) => x.k) : [r.k];

interface Page {
  id: string;
  name: string;
  rows: readonly Row[];
}

const SET: readonly Page[] = [
  {
    id: 'general',
    name: 'General',
    rows: [
      {
        k: 'density',
        t: 'Interface density',
        d: 'Standard is the default. Minimal shows icons without captions, moves helper text into tooltips, hides the viewport toolbar until you point at the top of the 3D view and narrows the side cards. Every function stays available in both.',
        type: 'seg',
        opts: ['Standard', 'Minimal'],
      },
      {
        k: 'theme',
        t: 'Theme',
        d: 'Light, Dark, or System. System follows the operating system’s light/dark choice when the OS provides one, otherwise Light.',
        type: 'seg',
        opts: ['Light', 'Dark', 'System'],
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
        d: 'Ask GitHub once each time ShapeLogic starts whether a newer version is out, and say so if it is. Check for Updates… in the menus asks at any time.',
        type: 'sw',
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
        k: 'solverType',
        t: 'Default solver type',
        d: 'The Solver type a problem starts with when it has none saved: Complete (ShapeLogic) or Fast (Crowell). A problem that saved its own keeps it. Sliding and stacking puzzles have solvers of their own.',
        type: 'seg',
        opts: ['Complete', 'Fast'],
      },
    ],
  },
];

let page = 'general';
let query = '';
let store: Store | null = null;

/** The machine's threads (the slider's end), from the engine. */
function maxThreads(): number {
  const info = store?.info;
  return Math.max(1, Math.min(info?.maxThreads ?? 32, info?.hardwareThreads ?? 32));
}

/** The threads shown: the engine's 0 is every hardware thread. */
function threadsShown(): number {
  return prefs.threads === 0 ? maxThreads() : prefs.threads;
}

function rowHtml(r: Row): string {
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
    return `<div class="setrow"><div class="t"><b>${esc(r.t)}</b><span>${esc(r.d)}</span></div><div class="xyzsets">${sets}</div></div>`;
  }
  const v = prefs[r.k];
  let c = '';
  switch (r.type) {
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
  return `<div class="setrow${under ? ' under' : ''}"><div class="t"><b>${esc(r.t)}</b><span>${esc(r.d)}</span></div>${c}</div>`;
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
      const rs = p.rows.filter((r) => `${r.t} ${r.d}`.toLowerCase().includes(q));
      if (rs.length > 0) {
        h += `<h3>${esc(p.name)}</h3>` + rs.map(rowHtml).join('');
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
        p.rows.map(rowHtml).join('');
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
  const engine: { undoDepth?: number; threads?: number } = {};
  if (c.undo !== undefined) {
    engine.undoDepth = c.undo;
  }
  if (c.threads !== undefined) {
    engine.threads = c.threads;
  }
  if (store !== null && (engine.undoDepth !== undefined || engine.threads !== undefined)) {
    store.setEngineSettings(engine).catch((e: unknown) => {
      reportError(e, 'The setting was not taken');
    });
  }
}

/** A new shape size held to the settings' range. */
const newSize = (n: number): number => Math.min(NEW_SIZE_MAX, Math.max(1, n));

function defaultsOf(rows: readonly Row[]): Partial<Prefs> {
  return Object.fromEntries(rows.flatMap(keysOf).map((k) => [k, DEFAULTS[k]]));
}

/** Wires the dialog; the engine's settings come from the store. */
export function installSettings(s: Store): void {
  store = s;
  s.subscribe((c) => {
    if (c.kind === 'settings' && s.settings !== null) {
      setPrefs({ undo: s.settings.undoDepth, threads: s.settings.threads });
      if (settingsOpen()) {
        renderSettings();
      }
    }
  });
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
    if (b.dataset.n !== undefined && b.dataset.s !== undefined) {
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
    if (t.dataset.n !== undefined) {
      // a size typed in: whole, 1 to NEW_SIZE_MAX; anything else shows the size again
      const n = Number(t.value.trim());
      if (t.value.trim() !== '' && Number.isFinite(n)) {
        change({ [t.dataset.n]: newSize(Math.round(n)) });
      }
      renderSettings();
      return;
    }
    if (t.dataset.k !== undefined && (t.tagName === 'SELECT' || t.type === 'range')) {
      change({ [t.dataset.k]: Number(t.value) });
    }
  });
  pg.addEventListener('input', (e) => {
    const t = e.target as HTMLInputElement;
    if (t.type === 'range' && t.nextElementSibling !== null) {
      t.nextElementSibling.textContent = t.value;
    }
  });
  pg.addEventListener('keydown', (e) => {
    const v = (e.target as Element).closest<HTMLInputElement>('input[data-n]');
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
