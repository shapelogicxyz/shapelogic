// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings dialog: searchable, three pages and the keyboard shortcuts,
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
import { DEFAULTS, prefs, setPrefs } from './prefs.ts';
import type { Prefs } from './prefs.ts';
import { reportError } from './status.ts';

type Row =
  | { k: keyof Prefs; t: string; d: string; type: 'sw' }
  | { k: keyof Prefs; t: string; d: string; type: 'seg'; opts: readonly string[] }
  | { k: keyof Prefs; t: string; d: string; type: 'select'; opts: readonly number[] }
  | { k: keyof Prefs; t: string; d: string; type: 'slider'; min: number };

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
        d: 'Drag is the newer click-and-drag rotation. Arc-ball is the original method.',
        type: 'seg',
        opts: ['Drag', 'Arc-ball'],
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
    name: 'Performance',
    rows: [
      {
        k: 'threads',
        t: 'Worker threads',
        d: 'Threads used for assembling and disassembling. Both stages share this pool, so raising it speeds up both.',
        type: 'slider',
        min: 1,
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
  const v = prefs[r.k];
  let c = '';
  switch (r.type) {
    case 'sw':
      c = `<div class="switch ${v === true ? 'on' : ''}" role="switch" aria-checked="${v === true}" aria-label="${esc(r.t)}" tabindex="0" data-k="${r.k}"><span class="tr"></span></div>`;
      break;
    case 'select': {
      const opts = r.opts.includes(v as number)
        ? r.opts
        : [...r.opts, v as number].sort((a, b) => a - b);
      c = `<select class="dd" data-k="${r.k}" aria-label="${esc(r.t)}">${opts.map((o) => `<option value="${o}" ${o === v ? 'selected' : ''}>${o}</option>`).join('')}</select>`;
      break;
    }
    case 'slider': {
      const shown = threadsShown();
      c = `<div style="display:flex;align-items:center;gap:10px;width:230px"><input type="range" min="${r.min}" max="${maxThreads()}" value="${shown}" data-k="${r.k}" aria-label="${esc(r.t)}" style="flex:1;accent-color:var(--accent)"><b style="width:26px;text-align:right">${shown}</b></div>`;
      break;
    }
    case 'seg':
      c = `<div class="seg">${r.opts.map((o) => `<button class="${o === v ? 'on' : ''}" data-k="${r.k}" data-o="${esc(o)}" aria-pressed="${o === v}">${esc(o)}</button>`).join('')}</div>`;
      break;
  }
  return `<div class="setrow"><div class="t"><b>${esc(r.t)}</b><span>${esc(r.d)}</span></div>${c}</div>`;
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

const kbRow = (r: KeyRow): string =>
  `<div class="kbrow"><span>${esc(r.does)}</span><span class="k">${kbKeys(r.keys)}</span></div>`;

/** Draws the dialog's navigation and page. */
export function renderSettings(): void {
  byId('setNav').innerHTML =
    SET.map(
      (p) =>
        `<button class="${query === '' && p.id === page ? 'on' : ''}" data-p="${p.id}">${esc(p.name)}</button>`,
    ).join('') +
    `<button class="${query === '' && page === 'keys' ? 'on' : ''}" data-p="keys">Keyboard shortcuts</button>`;
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
      .map(kbRow);
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
            g.rows.map(kbRow).join(''),
        )
        .join('');
  } else {
    const p = SET.find((x) => x.id === page) ?? SET[0];
    if (p !== undefined) {
      h =
        `<div class="pgh"><h3>${esc(p.name)}</h3><button class="btn sm" data-reset="${p.id}">Reset section</button></div>` +
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
  returnFocus?.focus();
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

function defaultsOf(rows: readonly Row[]): Partial<Prefs> {
  return Object.fromEntries(rows.map((r) => [r.k, DEFAULTS[r.k]]));
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
    if (b.dataset.reset !== undefined) {
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
