// SPDX-License-Identifier: GPL-3.0-or-later
//
// The workspaces and the cards. From docs/mockup/index.html: `layout`,
// `applyLayout` (a card collapses to a narrow rail; the 3D view takes the
// focus, both cards railed), `setTab` and `LAYS` (each workspace keeps its
// own layout), `renderRail` (Entities, Puzzle, Solver), and the collapse
// buttons and rails of the cards (`[data-collapse]`, `.crail`). The 2D
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
  ['entities', 'Entities', 'cube', 'Entities — create and edit pieces'],
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

/** The 3D view takes the focus, or gives it back (Ctrl+Space, the Focus button). */
export function toggleFocus3d(): void {
  layout.focus = layout.focus === '3d' ? '' : '3d';
  applyLayout();
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
