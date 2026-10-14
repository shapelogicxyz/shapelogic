// SPDX-License-Identifier: GPL-3.0-or-later
//
// The top bar: the brand, the file's name (its path on hover, a mark when
// it has unsaved changes) and the Settings button. From
// docs/mockup/index.html: `.top`. The window's title says the same (the
// name, a mark when changed). The temporary New…, Open… and Status…
// buttons of the earlier stages are the menus' items since stage 6
// (files.ts, menus.ts).

import type { Store } from '../state/store.ts';
import { byId, esc } from './dom.ts';

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

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
  el.innerHTML =
    esc(name) + (snap.dirty ? '<span class="dirty" title="Unsaved changes">●</span>' : '');
  el.title = snap.path ?? name;
  document.title = `${snap.dirty ? '• ' : ''}${name} — ShapeLogic`;
  // what the page shows, readable from outside (the e2e tests)
  stage.dataset.dirty = snap.dirty ? '1' : '0';
  stage.dataset.path = snap.path ?? '';
}

/** Wires the top bar to the store. */
export function installTopBar(store: Store): void {
  store.subscribe((c) => {
    if (c.kind === 'document') {
      render(store);
    }
  });
  render(store);
}
