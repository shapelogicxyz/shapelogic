// SPDX-License-Identifier: GPL-3.0-or-later
//
// The workspaces and the cards. From docs/mockup/index.html: `layout`,
// `applyLayout` (a card collapses to a narrow rail; the 3D view takes the
// focus, both cards railed), `setTab` and `LAYS` (each workspace keeps its
// own layout), `renderRail` (Entities, Puzzle, Solver), and the collapse
// buttons and rails of the cards (`[data-collapse]`, `.crail`). The
// layouts are kept across sessions (stage 6: persist.ts). The 2D
// editor's focus (`f2d`) comes with the editor in stage 2.

import { $$, byId } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';
import { ic } from './icons.ts';
import type { IconName } from './icons.ts';

/** A workspace of the rail. */
export type Workspace = 'entities' | 'puzzle' | 'solver';

/** The layout of the workspace shown. */
export const layout = { lc: false, rc: false, focus: '' as '' | '2d' | '3d' };

/** The workspace shown. */
export let tab: Workspace = 'entities';

const remembered: Partial<Record<Workspace, { lc: boolean; rc: boolean }>> = {};
const listeners = new Set<() => void>();

const RAIL: readonly [Workspace, string, IconName, string][] = [
  ['entities', 'Shapes', 'cube', 'Shapes — create and edit shapes'],
  ['puzzle', 'Puzzle', 'puzzle', 'Puzzle — choose the result, the pieces and the rules'],
  ['solver', 'Solver', 'play', 'Solver — run and browse solutions'],
];

/** Listens to layout and workspace changes. */
export function onLayout(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** Applies the layout to the stage. */
export function applyLayout(): void {
  const s = byId('stage');
  s.classList.remove('lc', 'rc', 'f2d', 'f3d');
  if (layout.focus === '3d') {
    s.classList.add('f3d');
  } else if (layout.focus === '2d') {
    s.classList.add('f2d');
  } else {
    if (layout.lc) {
      s.classList.add('lc');
    }
    if (layout.rc) {
      s.classList.add('rc');
    }
  }
  for (const l of [...listeners]) {
    l();
  }
}

function renderRail(): void {
  byId('appRail').innerHTML = RAIL.map(
    ([id, n, icon, title]) =>
      `<button class="rbtn ${tab === id ? 'on' : ''}" data-tab="${id}" title="${title}" aria-pressed="${tab === id}">${ic(icon, 22)}<span>${n}</span></button>`,
  ).join('');
}

/** Shows a workspace, with the layout it had. */
export function setTab(t: Workspace): void {
  remembered[tab] = { lc: layout.lc, rc: layout.rc };
  tab = t;
  byId('stage').dataset.tab = t;
  renderRail();
  layout.focus = '';
  const l = remembered[t] ?? { lc: false, rc: false };
  layout.lc = l.lc;
  layout.rc = l.rc;
  applyLayout();
}

/** Collapses or expands a card (Ctrl+[ and Ctrl+]); leaves a focus first. */
export function toggleCard(side: 'L' | 'R'): void {
  layout.focus = '';
  const k = side === 'L' ? 'lc' : 'rc';
  layout[k] = !layout[k];
  applyLayout();
}

/** The 3D view takes the focus, or gives it back (the Focus button, View ▸ Toggle 3D). */
export function toggleFocus3d(): void {
  layout.focus = layout.focus === '3d' ? '' : '3d';
  applyLayout();
}

/** The layouts to keep (stage 6): the workspace shown and each workspace's cards. */
export function layoutState(): Record<string, unknown> {
  return { tab, cards: { ...remembered, [tab]: { lc: layout.lc, rc: layout.rc } } };
}

const WORKSPACES: readonly Workspace[] = ['entities', 'puzzle', 'solver'];

/** Takes back the layouts a session kept (what is not well formed is left out). */
export function restoreLayout(saved: Record<string, unknown>): void {
  const cards = saved.cards;
  if (typeof cards === 'object' && cards !== null) {
    for (const w of WORKSPACES) {
      const c = (cards as Record<string, unknown>)[w];
      if (typeof c === 'object' && c !== null) {
        const { lc, rc } = c as Record<string, unknown>;
        if (typeof lc === 'boolean' && typeof rc === 'boolean') {
          remembered[w] = { lc, rc };
        }
      }
    }
  }
  const t = saved.tab;
  const shown = WORKSPACES.find((w) => w === t) ?? tab;
  // setTab keeps the layout of the workspace it leaves: the one restored for it
  const keep = remembered[tab];
  if (keep !== undefined) {
    layout.lc = keep.lc;
    layout.rc = keep.rc;
  }
  setTab(shown);
}

/** Whether the voxel editor is on screen (the mockup's editorVisible). */
export function editorVisible(): boolean {
  return tab === 'entities' && (layout.focus === '2d' || (layout.focus === '' && !layout.rc));
}

/** The rail, the cards' collapse buttons and rails, and Esc leaving a focus. */
export function installLayout(): void {
  renderRail();
  byId('appRail').addEventListener('click', (e) => {
    const b = (e.target as Element).closest<HTMLElement>('.rbtn');
    const t = b?.dataset.tab as Workspace | undefined;
    if (t !== undefined && t !== tab) {
      setTab(t);
    }
  });
  for (const b of $$('[data-collapse]')) {
    b.innerHTML = ic(b.dataset.collapse === 'L' ? 'panelL' : 'panelR');
  }
  // the collapsed cards' rails; the Entities cards' rails are filled by their views
  const rails: [string, 'L' | 'R', string, string][] = [
    ['#leftP', 'L', 'Puzzle problems', 'Problems'],
    ['#leftS', 'L', 'Solver', 'Solver'],
    ['#rightP', 'R', 'Selected piece', 'Selected piece'],
    ['#rightS', 'R', 'Solutions', 'Solutions'],
  ];
  for (const [sel, side, title, label] of rails) {
    document
      .querySelector(sel)
      ?.insertAdjacentHTML(
        'beforeend',
        `<div class="crail"><button class="ib" data-expand="${side}" title="Expand ${title}">${ic(side === 'L' ? 'panelL' : 'panelR')}</button><span class="vt2">${label}</span></div>`,
      );
  }
  byId('rrail').innerHTML =
    `<button class="ib" data-expand="R" title="Expand Voxel editor">${ic('panelR')}</button><span class="vt2">Voxel editor</span>`;
  document.addEventListener('click', (e) => {
    const t = e.target as Element;
    const c = t.closest<HTMLElement>('[data-collapse]');
    const x = t.closest<HTMLElement>('[data-expand]');
    if (c !== null) {
      layout[c.dataset.collapse === 'L' ? 'lc' : 'rc'] = true;
      applyLayout();
    } else if (x !== null) {
      layout.focus = '';
      layout[x.dataset.expand === 'L' ? 'lc' : 'rc'] = false;
      applyLayout();
    }
  });
  addEscapeStep(ESC_ORDER.focus, () => {
    if (layout.focus === '') {
      return false;
    }
    layout.focus = '';
    applyLayout();
    return true;
  });
}
