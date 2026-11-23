// SPDX-License-Identifier: GPL-3.0-or-later
//
// The top bar: the brand, the file's name (its path on hover, a mark when
// it has unsaved changes; as wide as the bar lets it be) and the Settings
// button. From docs/mockup/index.html: `.top`. The window's title says the same (the
// name, a mark when changed). The temporary New…, Open… and Status…
// buttons of the earlier stages are the menus' items since stage 6
// (files.ts, menus.ts). A file another window owns is marked read-only,
// in the bar and the title (decision 0036).

import type { BtApp } from '../../engine/app-bridge.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from './dom.ts';

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** The window holds its file read-only (another window owns it). */
let readOnly = false;

/** Shows the document's name and state. */
function render(store: Store): void {
  const snap = store.snapshot;
  const el = byId('fname');
  const stage = byId('stage');
  if (snap === null) {
    el.textContent = '';
    el.removeAttribute('title');
    document.title = 'ShapeLogic';
    stage.dataset.dirty = '';
    return;
  }
  const name = snap.path !== null ? baseName(snap.path) : (store.displayName ?? 'Untitled');
  const ro = readOnly && snap.path !== null;
  el.innerHTML =
    esc(name) +
    (ro
      ? '<span class="ro" title="Open in another window: Save As keeps the changes under another name">(read-only)</span>'
      : '') +
    (snap.dirty ? '<span class="dirty" title="Unsaved changes">●</span>' : '');
  el.title = snap.path ?? name;
  document.title = `${snap.dirty ? '• ' : ''}${name}${ro ? ' (read-only)' : ''} — ShapeLogic`;
  // what the page shows, readable from outside (the e2e tests)
  stage.dataset.dirty = snap.dirty ? '1' : '0';
  stage.dataset.path = snap.path ?? '';
  stage.dataset.readOnly = ro ? '1' : '0';
}

/**
 * Lets the file's name use the bar's free space: up to about one tab
 * button's width short of the tabs centerd in the bar, or of the brand
 * when the tabs are at the left. The name's right end stays put (the
 * Settings button is right of it), so its room is that end less the edge.
 */
function fitName(): void {
  const el = byId('fname');
  let edge: number;
  if (document.documentElement.dataset.tabs === 'top') {
    const rail = byId('appRail');
    const button = rail.lastElementChild;
    edge = rail.getBoundingClientRect().right + (button?.getBoundingClientRect().width ?? 0);
  } else {
    edge = byId('brand').getBoundingClientRect().right + 24;
  }
  const room = Math.max(0, Math.round(el.getBoundingClientRect().right - edge));
  el.style.maxWidth = `${String(room)}px`;
}

/** Wires the top bar to the store. */
export function installTopBar(store: Store, app: BtApp): void {
  store.subscribe((c) => {
    if (c.kind === 'document') {
      render(store);
    }
  });
  app.onReadOnly((r) => {
    readOnly = r;
    render(store);
  });
  render(store);
  // the window's size, the density, the font size and where the tabs are
  // all change the size of one of these
  const fit = new ResizeObserver(fitName);
  for (const id of ['stage', 'appRail', 'brand', 'btnSettings']) {
    fit.observe(byId(id));
  }
}
