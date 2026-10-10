// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell put together: the engine proxy, the store and the solve
// records, the top bar, the workspace rail and cards (the Entities
// workspace: entities/workspace.ts; the Puzzle workspace:
// puzzle/workspace.ts; the Solver workspace: solver/workspace.ts),
// the 3D view, the status bar, the settings, the status window and the
// floating panels (stage 5), the menus, the files, the rescue copy, the
// help and the settings and layouts kept across sessions (stage 6), and the
// keys of the one table. From docs/mockup/index.html: its orchestration
// (`renderAll`, the keydown handler's shell rows) and `updateStatus`.

import { EngineProxy } from '../engine-proxy.ts';
import { Exports } from '../export/exports.ts';
import type { AnimationSource } from '../export/exports.ts';
import { StatusWindow } from '../entities/status-window.ts';
import { entitiesStatus, EntitiesWorkspace } from '../entities/workspace.ts';
import { PuzzleWorkspace } from '../puzzle/workspace.ts';
import { openToShapes } from '../solver/to-shapes.ts';
import { SolverWorkspace } from '../solver/workspace.ts';
import { isActive } from '../state/solves.ts';
import { Edits } from '../state/edits.ts';
import { Solves } from '../state/solves.ts';
import { Store } from '../state/store.ts';
import { View3D } from '../view3d/view.ts';
import type { StoredState } from '../../engine/app-bridge.ts';
import { installConfirm } from './confirm.ts';
import { dialogOpen, installDialogs, tell } from './dialog.ts';
import { Files } from './files.ts';
import { openAbout, openKeys, openNotes } from './help.ts';
import { Menus } from './menus.ts';
import { keepState } from './persist.ts';
import { prefs, setPrefs, validPrefs } from './prefs.ts';
import { Rescue } from './rescue.ts';
import { stepBack } from './escape.ts';
import { installKeyboard, onKey, setModal } from './keyboard.ts';
import {
  installLayout,
  onLayout,
  restoreLayout,
  setTab,
  tab,
  toggleCard,
  toggleFocus3d,
} from './layout.ts';
import { installPanels } from './panel.ts';
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
// the solution the Solver shows, when it has its moves stored (Export ▸ Solution Animation)
function animationSource(solver: SolverWorkspace): AnimationSource | null {
  const p = solver.player;
  if (p.problem === null || p.row?.disassembly !== 'tree') {
    return null;
  }
  return {
    problem: p.problem,
    row: p.row,
    colours: new Map(p.pieces.map((x) => [x.piece, x.colour])),
  };
}

function statusLine(store: Store, puzzle: PuzzleWorkspace, solver: SolverWorkspace): string {
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
  return tab === 'puzzle'
    ? puzzle.statusLine()
    : tab === 'solver'
      ? solver.statusLine()
      : entitiesStatus(store);
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

/** Starts the app in the page, with the settings and layouts the last session kept. */
export function startApp(saved: StoredState = {}): {
  store: Store;
  view: View3D;
  entities: EntitiesWorkspace;
  puzzle: PuzzleWorkspace;
  solver: SolverWorkspace;
} {
  const app = window.btApp;
  if (saved.prefs !== undefined) {
    setPrefs(validPrefs(saved.prefs));
  }
  const engine = new EngineProxy(window.bt);
  const store = new Store(engine);
  // each engine gets the settings kept (undo depth, threads)
  store.wantedSettings = () => ({ undoDepth: prefs.undo, threads: prefs.threads });
  const solves = new Solves(engine);
  // another document (or engine): its problems' ids are new; the old
  // document's last idle and analyseEnded come after this
  let generation = store.generation;
  store.subscribe((c) => {
    if (c.kind === 'document' && store.generation !== generation) {
      generation = store.generation;
      solves.clear();
    }
  });

  installTheme();
  installPopover();
  installConfirm();
  installDialogs();
  installPanels();
  installLayout();
  if (saved.layout !== undefined) {
    restoreLayout(saved.layout);
  }
  keepState(app);
  installSettings(store);
  installTopBar(store);
  const view = new View3D(store);
  const edits = new Edits(store, solves);
  const entities = new EntitiesWorkspace(store, view, edits);
  const puzzle = new PuzzleWorkspace(store, view, edits);
  const solver = new SolverWorkspace(store, view, edits, solves);
  // the status window: View ▸ Status Window
  const status = new StatusWindow(store, edits);
  const runsUnderWay = (): boolean =>
    solver.autosave.runsUnderWay() ||
    (store.snapshot?.problems.some((p) => isActive(p.solve)) ?? false);
  const rescue = new Rescue(
    store,
    app,
    () =>
      edits.idle &&
      !solver.calling &&
      !solver.autosave.busy &&
      // a run launched counts before its first state is told
      !solves.running &&
      !solves.analysisActive &&
      !runsUnderWay() &&
      !(store.snapshot?.problems.some((p) => p.analysing) ?? false),
  );
  const files = new Files(
    store,
    app,
    {
      get current() {
        return solver.current;
      },
      showProblem: (problem) => {
        setTab('solver');
        solver.selectProblem(problem);
      },
      runsUnderWay,
      saveAtSavePoint: (path) => solver.autosave.saveAtSavePoint(path, true),
    },
    () => rescue.discard(),
  );
  const exports = new Exports(store, app, view, {
    get name() {
      return files.name;
    },
    animation: () => animationSource(solver),
  });
  const menus = new Menus(store, app, view, {
    openPath: (path) => {
      void files.openPath(path);
    },
    close: () => {
      void files.close();
    },
    importPaused: () => {
      void files.importPaused();
    },
    exportPaused: () => {
      void files.exportPaused();
    },
    exportImage: () => {
      void exports.image();
    },
    exportVector: () => {
      void exports.vector();
    },
    exportStl: () => {
      void exports.stlFile();
    },
    exportGlb: () => {
      void exports.animation();
    },
    animation: () => animationSource(solver) !== null,
    toShapes: () => {
      openToShapes(store, edits, solver.current);
    },
    notes: () => {
      openNotes(store);
    },
    statusWindow: () => {
      status.open();
    },
    about: () => {
      openAbout(store, app.version);
    },
    solverProblem: () => {
      const p = solver.problem;
      return p === null
        ? null
        : { paused: p.state === 'solving' && !isActive(solver.stateOf(p.id)) };
    },
  });
  // the Solver's problem and its solve change the menus too: look now and then
  window.setInterval(() => {
    menus.update();
  }, 1000);

  setStatusProvider(() => statusLine(store, puzzle, solver));
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
  // what a change or an opened file did beyond what was asked
  store.onNotice(tell);
  onLayout(() => {
    updateStatus();
  });
  renderGridChip(store);

  setModal(() => settingsOpen() || dialogOpen());
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
    openKeys();
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
  return { store, view, entities, puzzle, solver };
}
