// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell put together: the engine proxy and the store, the top bar, the
// workspace rail and cards (the Entities workspace: entities/workspace.ts;
// the Puzzle workspace: puzzle/workspace.ts),
// the 3D view, the status bar, the settings, and the keys of the one
// table. From docs/mockup/index.html: its orchestration (`renderAll`, the
// keydown handler's shell rows) and `updateStatus`.

import { EngineProxy } from '../engine-proxy.ts';
import { entitiesStatus, EntitiesWorkspace } from '../entities/workspace.ts';
import { PuzzleWorkspace } from '../puzzle/workspace.ts';
import { Edits } from '../state/edits.ts';
import { Store } from '../state/store.ts';
import { View3D } from '../view3d/view.ts';
import { installConfirm } from './confirm.ts';
import { stepBack } from './escape.ts';
import { installKeyboard, onKey, setModal } from './keyboard.ts';
import { installLayout, onLayout, setTab, tab, toggleCard, toggleFocus3d } from './layout.ts';
import { installPopover } from './popover.ts';
import { installSettings, openSettings, settingsOpen } from './settings.ts';
import { flash, reportError, setStatusProvider, updateStatus } from './status.ts';
import { installTheme } from './theme.ts';
import { installTopBar } from './topbar.ts';
import { byId, esc } from './dom.ts';

/**
 * What the status bar says: the engine's state, else the workspace's (the
 * mockup's updateStatus: the shape shown; pzStatus: the problem's fit).
 */
function statusLine(store: Store, puzzle: PuzzleWorkspace): string {
  const s = store.status;
  if (s === null) {
    return 'Starting the engine…';
  }
  if (s.state === 'exited') {
    return `The engine stopped${s.signal !== null ? ` (${esc(s.signal)})` : s.code !== null ? ` (exit ${s.code})` : ''}; starting it again…`;
  }
  if (s.state === 'failed') {
    return `The engine could not be kept running: ${esc(s.reason)}`;
  }
  if (store.snapshot === null) {
    return '';
  }
  return tab === 'puzzle' ? puzzle.statusLine() : entitiesStatus(store);
}

// the grid of the document in the voxel editor's header (the mockup's
// renderVType: fixed when the puzzle is made)
function renderGridChip(store: Store): void {
  const grid = store.snapshot?.grid;
  const label = store.info?.grids.find((g) => g.grid === grid)?.label ?? grid ?? '';
  const chip = byId('vtChip');
  chip.style.display = label === '' ? 'none' : '';
  chip.innerHTML = `<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/></svg>${esc(label)}`;
  chip.title = `Voxel type: ${label} — fixed when the file is created (File ▸ New); it cannot be changed afterwards.`;
}

/** Starts the app in the page. */
export function startApp(): {
  store: Store;
  view: View3D;
  entities: EntitiesWorkspace;
  puzzle: PuzzleWorkspace;
} {
  const engine = new EngineProxy(window.bt);
  const store = new Store(engine);

  installTheme();
  installPopover();
  installConfirm();
  installLayout();
  installSettings(store);
  installTopBar(store);
  const view = new View3D(store);
  const edits = new Edits(store);
  const entities = new EntitiesWorkspace(store, view, edits);
  const puzzle = new PuzzleWorkspace(store, view, edits);

  setStatusProvider(() => statusLine(store, puzzle));
  store.subscribe((c) => {
    if (c.kind === 'status' || c.kind === 'document' || c.kind === 'selection') {
      updateStatus(c.kind === 'status');
    }
    if (c.kind === 'status') {
      // the engine's state, readable from outside (the e2e tests)
      byId('stage').dataset.engine = store.status?.state ?? '';
    }
    if (c.kind === 'document' || c.kind === 'settings') {
      renderGridChip(store);
    }
    if (c.kind === 'document') {
      // which document and revision the page shows (the e2e tests wait on them)
      byId('stage').dataset.generation = String(store.generation);
      byId('stage').dataset.revision = String(store.snapshot?.revision ?? '');
    }
    if (c.kind === 'document' && c.restarted) {
      flash('The engine was started again: a new, empty document', 5000);
    }
  });
  store.onError((e) => {
    reportError(e);
  });
  onLayout(() => {
    updateStatus();
  });
  renderGridChip(store);

  setModal(settingsOpen);
  onKey('workspace.entities', () => {
    setTab('entities');
    return true;
  });
  onKey('workspace.puzzle', () => {
    setTab('puzzle');
    return true;
  });
  onKey('workspace.solver', () => {
    setTab('solver');
    return true;
  });
  onKey('settings', () => {
    openSettings();
    return true;
  });
  onKey('help.keys', () => {
    openSettings('keys');
    return true;
  });
  onKey('card.left', () => {
    toggleCard('L');
    return true;
  });
  onKey('card.right', () => {
    toggleCard('R');
    return true;
  });
  onKey('focus3d', () => {
    toggleFocus3d();
    return true;
  });
  onKey('escape', stepBack);
  installKeyboard();
  return { store, view, entities, puzzle };
}
