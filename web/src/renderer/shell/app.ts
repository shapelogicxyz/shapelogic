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
import { animationOf } from '../export/exports.ts';
import { StatusWindow } from '../entities/status-window.ts';
import { entitiesStatus, EntitiesWorkspace } from '../entities/workspace.ts';
import { PlayWorkspace } from '../play/workspace.ts';
import { PuzzleWorkspace } from '../puzzle/workspace.ts';
import { CacheLinker } from '../solver/cache-links.ts';
import { openToShapes } from '../solver/to-shapes.ts';
import { SolverWorkspace } from '../solver/workspace.ts';
import type { FileExport } from '../export/exports.ts';
import { isActive } from '../state/solves.ts';
import { Edits } from '../state/edits.ts';
import { Solves } from '../state/solves.ts';
import { Store } from '../state/store.ts';
import { View3D } from '../view3d/view.ts';
import type { StoredState } from '../../engine/app-bridge.ts';
import { dialogOpen, installDialogs, setLogViewer, tell } from './dialog.ts';
import { Files } from './files.ts';
import { Imports } from './imports.ts';
import { openCacheFiles } from './cache-files.ts';
import { noticeCacheAtStart } from './cache-notice.ts';
import { openLogFiles } from './log-files.ts';
import { openCliTool } from './cli-tool.ts';
import { openAbout, openKeys, openNotes } from './help.ts';
import { EngineFailure } from './engine-failure.ts';
import { Menus } from './menus.ts';
import { gridInfo, tiltedInfo } from './new-grids.ts';
import { isTilted } from '../state/tilted.ts';
import { keepState } from './persist.ts';
import { keepPicturesOf } from './piece-images.ts';
import { engineSettingsOf, prefs, setPrefs, validPrefs } from './prefs.ts';
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
import { autoCheck, checkNow } from './updates.ts';
import { closePop, installPopover } from './popover.ts';
import { installSettings, openSettings, settingsOpen } from './settings.ts';
import { flash, reportError, setHintTeller, setStatusProvider, updateStatus } from './status.ts';
import { installTheme } from './theme.ts';
import { installTopBar } from './topbar.ts';
import { byId, esc } from './dom.ts';

/**
 * What the status bar says: the engine's state, else the workspace's (the
 * mockup's updateStatus: the shape shown; pzStatus: the problem's fit).
 */
function statusLine(
  store: Store,
  puzzle: PuzzleWorkspace,
  solver: SolverWorkspace,
  play: PlayWorkspace,
): string {
  const s = store.status;
  if (s === null) {
    return 'Starting the engine…';
  }
  if (s.state === 'exited') {
    return `The engine stopped${s.signal !== null ? ` (${esc(s.signal)})` : s.code !== null ? ` (exit ${s.code})` : ''}; starting it again…`;
  }
  if (s.state === 'failed') {
    return `The engine could not be kept running: ${esc(s.reason)} (see Help ▸ Copy Diagnostics)`;
  }
  if (store.snapshot === null) {
    return '';
  }
  return tab === 'puzzle'
    ? puzzle.statusLine()
    : tab === 'solver'
      ? solver.statusLine()
      : tab === 'play'
        ? play.statusLine()
        : entitiesStatus(store);
}

// the grid of the document in the voxel editor's header (the mockup's
// renderVType: fixed when the puzzle is made), named as File ▸ New names it
function renderGridChip(store: Store): void {
  const grid = store.snapshot?.grid;
  // a brick puzzle with a tray is a Tilted Square puzzle
  const label =
    grid === undefined
      ? ''
      : isTilted(store.snapshot)
        ? tiltedInfo.name
        : ((gridInfo as Partial<Record<string, { name: string }>>)[grid]?.name ??
          store.info?.grids.find((g) => g.grid === grid)?.label ??
          grid);
  const chip = byId('vtChip');
  chip.style.display = label === '' ? 'none' : '';
  chip.innerHTML = `Type: ${esc(label)}`;
  chip.title = `Puzzle type: ${label} — fixed when the file is created (File ▸ New); it cannot be changed afterwards.`;
}

/** Starts the app in the page, with the settings and layouts the last session kept. */
export function startApp(saved: StoredState = {}): {
  store: Store;
  view: View3D;
  entities: EntitiesWorkspace;
  puzzle: PuzzleWorkspace;
  solver: SolverWorkspace;
  play: PlayWorkspace;
} {
  const app = window.btApp;
  if (saved.prefs !== undefined) {
    setPrefs(validPrefs(saved.prefs));
  }
  const engine = new EngineProxy(window.bt);
  const store = new Store(engine);
  // each engine gets the settings kept (undo depth, threads, memory, disk)
  store.wantedSettings = () => engineSettingsOf(prefs);
  // settings another window changed (persist.ts) reach this window's
  // engine too; what it has already is not sent again
  const takeEngineSettings = (): void => {
    const now = store.settings;
    if (now === null || !store.running) {
      return;
    }
    const wanted = store.wantedSettings();
    const change = Object.fromEntries(
      Object.entries(wanted).filter(([k, v]) => now[k as keyof typeof now] !== v),
    );
    if (Object.keys(change).length > 0) {
      store.setEngineSettings(change).catch(() => {
        // the engine went: the next one is started with the settings wanted
      });
    }
  };
  const solves = new Solves(engine);
  // another document (or engine): its problems' ids are new; the old
  // document's last idle and analyseEnded come after this. A run already
  // under way (the page reloaded) is taken from the snapshot.
  let generation = store.generation;
  store.subscribe((c) => {
    if (c.kind !== 'document') {
      return;
    }
    if (store.generation !== generation) {
      generation = store.generation;
      solves.clear();
    }
    solves.adopt(store.snapshot?.problems ?? []);
  });

  installTheme();
  installPopover();
  installDialogs();
  // a failed write Windows may have blocked: a message with the hint (cleanup item 2.5)
  setHintTeller((text) => {
    tell(text, true);
  });
  installPanels();
  installLayout();
  if (saved.layout !== undefined) {
    restoreLayout(saved.layout);
  }
  keepState(app, takeEngineSettings);
  installSettings(store);
  installTopBar(store, app);
  const view = new View3D(store);
  const edits = new Edits(store);
  const entities = new EntitiesWorkspace(store, view, edits);
  const puzzle = new PuzzleWorkspace(store, view, edits);
  const solver = new SolverWorkspace(store, view, edits, solves);
  // the status window: View ▸ Status Window
  const status = new StatusWindow(store, edits);
  const runsUnderWay = (): boolean =>
    solver.autosave.runsUnderWay() ||
    (store.snapshot?.problems.some((p) => isActive(p.solve)) ?? false);
  if (app.keepsLog) {
    // the warnings' and errors' View Logs (the rescue copy's offer is one)
    setLogViewer(() => {
      void openLogFiles(app);
    });
  }
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
  // the crash recovery journal writes a puzzle without a file to the rescue copy
  solver.autosave.rescue = () => rescue.saveNow();
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
      get savedSearch() {
        return solver.savedSearch;
      },
    },
    () => rescue.discard(),
  );
  // the cache manager names the search folders after the puzzle and problem
  new CacheLinker(store, solves, app, () => files.name);
  const exports = new Exports(store, app, view, {
    get name() {
      return files.name;
    },
    animation: () => animationOf(solver.player),
  });
  const imports = new Imports(store, app, {
    get name() {
      return files.name;
    },
    get problem() {
      return puzzle.current;
    },
    confirmDiscard: (action) => files.confirmDiscard(action),
    showEntities: () => {
      setTab('entities');
    },
  });
  // a sliding or a stacking puzzle played by hand (decisions 0039 and 0042)
  const play = new PlayWorkspace(store, edits, app, {
    get name() {
      return files.name;
    },
  });
  // the picture caches keep what the document names; after the views, which let go first
  keepPicturesOf(store);
  solver.exportStl = (problem, row) => {
    void exports.solutionStl(problem, row);
  };
  // File ▸ Export's formats, from the menu and from the Solver's Export menu
  const fileExport: Record<FileExport, () => void> = {
    scad: () => void imports.exportScad(),
    image: () => void exports.image(),
    vector: () => void exports.vector(),
    stl: () => void exports.stlFile(),
    glb: () => void exports.animation(),
  };
  solver.fileExport = (k) => {
    fileExport[k]();
  };
  // the window says when its engine cannot start (cleanup item 2.3)
  const engineFailure = new EngineFailure(store, app);
  const menus = new Menus(
    store,
    app,
    view,
    {
      openPath: (c) => {
        void files.openPath(c.path);
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
      importPs3d: () => {
        void imports.importFile('puzzleSolver3D');
      },
      importScad: () => {
        void imports.importFile('puzzlecad');
      },
      exportScad: fileExport.scad,
      exportImage: fileExport.image,
      exportVector: fileExport.vector,
      exportStl: fileExport.stl,
      exportGlb: fileExport.glb,
      toShapes: () => {
        openToShapes(store, edits, solver.current);
      },
      convertGrid: () => {
        void imports.convertGrid();
      },
      notes: () => {
        openNotes(store);
      },
      statusWindow: () => {
        status.open();
      },
      cacheFiles: () => {
        void openCacheFiles(app);
      },
      logFiles: () => {
        void openLogFiles(app);
      },
      about: () => {
        openAbout(store, app.version);
      },
      cliTool: () => {
        void openCliTool(app);
      },
      checkUpdates: () => {
        void checkNow(app);
      },
    },
    {
      animation: () => animationOf(solver.player) !== null,
      solverProblem: () => {
        const p = solver.problem;
        return p === null
          ? null
          : {
              // a stacking problem is paused when its search is saved (problemPaused)
              paused:
                (solver.stacking ? solver.savedSearch !== null : p.state === 'solving') &&
                !isActive(solver.stateOf(p.id)),
            };
      },
      playHistory: () => play.history(),
      engineFailed: () => engineFailure.failed,
      showEngineFailure: () => {
        engineFailure.show();
      },
    },
  );
  // the log's Startup line: the first document shown (cleanup item 2.7)
  const told = store.subscribe((c) => {
    if (c.kind === 'document' && store.snapshot !== null) {
      told();
      requestAnimationFrame(() => {
        app.ready();
      });
    }
  });
  // the Solver's problem and its solve change the menus too: look now and then
  window.setInterval(() => {
    menus.update();
  }, 1000);

  setStatusProvider(() => statusLine(store, puzzle, solver, play));
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
      if (byId('stage').dataset.generation !== String(store.generation)) {
        // another document: a menu open on the old one's shape or problem (ids repeat) closes
        closePop();
      }
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
  // only while a sliding puzzle is open: the tab is in the rail then
  onKey('workspace.play', () => {
    setTab('play');
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
  autoCheck(app);
  // what else the cache folder holds, once the rescue copy's offer is answered
  void rescue.startLooked.then(() =>
    noticeCacheAtStart(app, () => {
      void openCacheFiles(app);
    }),
  );
  return { store, view, entities, puzzle, solver, play };
}
