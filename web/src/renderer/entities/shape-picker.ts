// SPDX-License-Identifier: GPL-3.0-or-later
//
// A temporary shape picker in the Entities workspace's left card, until
// stage 2's shapes list replaces it: the shapes of the document (the
// engine's labels, names and counts), one of them shown in the 3D view.
// The rows and the collapsed card's chips are the mockup's
// (docs/mockup/index.html: `renderShapes`, without its actions).

import type { Store } from '../state/store.ts';
import { byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { contrast, hex, shapeColour } from '../shell/piece-colours.ts';

function render(store: Store): void {
  const shapes = store.snapshot?.shapes ?? [];
  byId('shapeCount').textContent = String(shapes.length);
  const chip = (i: number): { bg: string; fg: string } => {
    const s = shapes[i];
    const c = shapeColour(s?.colour ?? null, i);
    return { bg: hex(c), fg: contrast(c) };
  };
  byId('shapeList').innerHTML = shapes
    .map((s, i) => {
      const { bg, fg } = chip(i);
      const sel = s.id === store.selected;
      return `<div class="row ${sel ? 'sel' : ''}" role="option" aria-selected="${sel}" data-id="${s.id}" title="${esc(s.label)}${s.name === '' ? '' : ' – ' + esc(s.name)}: ${s.size.join(' × ')}"><span class="chip" style="background:${bg};color:${fg}">${esc(s.label)}</span><span class="lbl">${esc(s.name)}</span>${s.weight !== 1 ? `<span class="wbadge">W${s.weight}</span>` : ''}<span class="vcount">${s.fixed + s.variable} voxels</span></div>`;
    })
    .join('');
  byId('lrail').innerHTML =
    `<button class="ib" data-expand="L" title="Expand Shapes">${ic('panelL')}</button>` +
    shapes
      .map((s, i) => {
        const { bg, fg } = chip(i);
        const ring =
          s.id === store.selected
            ? 'box-shadow:0 0 0 2px var(--panel),0 0 0 4px var(--accent);'
            : '';
        return `<button class="chip" data-id="${s.id}" title="${esc(s.label)} ${esc(s.name)}" style="background:${bg};color:${fg};border:0;cursor:pointer;min-width:34px;${ring}">${esc(s.label)}</button>`;
      })
      .join('');
}

/** Wires the picker to the store. */
export function installShapePicker(store: Store): void {
  store.subscribe((c) => {
    if (c.kind === 'document' || c.kind === 'selection') {
      render(store);
    }
  });
  render(store);
  const pick = (e: Event): void => {
    const el = (e.target as Element).closest<HTMLElement>('[data-id]');
    if (el?.dataset.id !== undefined) {
      store.select(el.dataset.id);
    }
  };
  byId('shapeList').addEventListener('click', pick);
  byId('lrail').addEventListener('click', pick);
  byId('shapeList').addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') {
      return;
    }
    const shapes = store.snapshot?.shapes ?? [];
    const i = shapes.findIndex((s) => s.id === store.selected);
    const j = Math.max(0, Math.min(shapes.length - 1, i + (e.key === 'ArrowUp' ? -1 : 1)));
    const next = shapes[j];
    if (next !== undefined) {
      e.preventDefault();
      store.select(next.id);
      byId('shapeList').querySelector('.row.sel')?.scrollIntoView({ block: 'nearest' });
    }
  });
}
