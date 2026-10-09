// SPDX-License-Identifier: GPL-3.0-or-later
//
// The top bar: the brand, the file's name (its path on hover, a mark when
// it has unsaved changes) and the Settings button. From
// docs/mockup/index.html: `.top`. Until the File menu of stage 6, a
// "New…" button makes an empty puzzle on a grid chosen from the engine's
// (document.new), and an "Open…" button and a file dropped on the window
// open a puzzle by its bytes (document.openBytes: the renderer has no
// paths).

import type { Store } from '../state/store.ts';
import type { Grid } from '../../engine/api.generated.ts';
import { byId, esc } from './dom.ts';
import { openPop } from './popover.ts';
import { flash, reportError } from './status.ts';

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** Shows the document's name and state. */
function render(store: Store): void {
  const snap = store.snapshot;
  const el = byId('fname');
  if (snap === null) {
    el.textContent = '';
    el.removeAttribute('title');
    document.title = 'BurrTools Alpha';
    return;
  }
  const name = snap.path !== null ? baseName(snap.path) : (store.displayName ?? 'Untitled');
  el.innerHTML =
    esc(name) + (snap.dirty ? '<span class="dirty" title="Unsaved changes">●</span>' : '');
  el.title = snap.path ?? name;
  document.title = `${snap.dirty ? '• ' : ''}${name} — BurrTools Alpha`;
}

async function open(store: Store, file: File): Promise<void> {
  if (!store.running) {
    flash('The engine is not running');
    return;
  }
  try {
    await store.openBytes(file.name, new Uint8Array(await file.arrayBuffer()));
    flash(`Opened ${file.name}`);
  } catch (e) {
    reportError(e, `Could not open ${file.name}`);
  }
}

// the grids a new puzzle can have (those this version solves)
function newMenu(store: Store): string {
  const grids = store.info?.grids.filter((g) => g.supported) ?? [];
  return `<div class="ph">New puzzle — its grid cannot change later</div>${grids.map((g) => `<button data-k="${g.grid}">${esc(g.label)}</button>`).join('')}`;
}

/** Wires the top bar to the store. */
export function installTopBar(store: Store): void {
  byId('btnNewDoc').addEventListener('click', (e) => {
    if (!store.running || store.info === null) {
      flash('The engine is not running');
      return;
    }
    openPop(e.currentTarget as Element, newMenu(store), (k) => {
      store.newDocument(k as Grid).then(
        () => {
          flash('A new, empty puzzle');
        },
        (err: unknown) => {
          reportError(err, 'New puzzle');
        },
      );
    });
  });
  store.subscribe((c) => {
    if (c.kind === 'document') {
      render(store);
    }
  });
  render(store);
  const input = byId<HTMLInputElement>('openFile');
  input.addEventListener('change', () => {
    const f = input.files?.[0];
    input.value = '';
    if (f !== undefined) {
      void open(store, f);
    }
  });
  const stage = byId('stage');
  stage.addEventListener('dragover', (e) => {
    if (e.dataTransfer?.types.includes('Files') === true) {
      e.preventDefault();
      stage.classList.add('drop');
    }
  });
  stage.addEventListener('dragleave', (e) => {
    if (e.target === stage || !stage.contains(e.relatedTarget as Node | null)) {
      stage.classList.remove('drop');
    }
  });
  stage.addEventListener('drop', (e) => {
    stage.classList.remove('drop');
    const f = e.dataTransfer?.files[0];
    if (f !== undefined) {
      e.preventDefault();
      void open(store, f);
    }
  });
}
