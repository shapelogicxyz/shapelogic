// SPDX-License-Identifier: GPL-3.0-or-later
//
// The File menu's work (stage 6), from the original's mainwindow_file.cpp:
// New (the grid chosen once: cb_New's selector), Open, a recent file or a
// file dropped on the window (document.open by its path; a .btsolve file
// is imported), Save and Save As (document.save; the original's
// confirmDiscard before anything that replaces the puzzle: Save, Don't
// Save, Cancel), the window's close with the same question, and the
// paused solver state (solve.importPaused, solve.exportPaused). A save
// while a solve runs is refused by the engine; it is offered at a save
// point instead (the Solver's Autosave.saveAtSavePoint: the runs pause
// where they can be saved, the puzzle is saved, they carry on). A file
// opened or saved goes first in Open Recent. A file dropped that has no
// path (from another application) is opened by its bytes, without a file.

import type { BtApp } from '../../engine/app-bridge.ts';
import type { Grid } from '../../engine/api.generated.ts';
import { newOrder } from './files-order.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Store } from '../state/store.ts';
import { askChoice, dialogOpen, openDialog } from './dialog.ts';
import { byId } from './dom.ts';
import { onKey } from './keyboard.ts';
import { flash, reportError } from './status.ts';

/** What the files need of the Solver. */
export interface FilesSolver {
  /** The problem the Solver shows, or null. */
  readonly current: string | null;
  /** Shows a problem in the Solver. */
  showProblem(problem: string): void;
  /** Whether a run is under way in the document (a save must wait for a save point). */
  runsUnderWay(): boolean;
  /** Pauses the runs at a save point, saves, carries them on; the file, or null. */
  saveAtSavePoint(path?: string): Promise<string | null>;
}

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

const isPaused = (path: string): boolean => /\.btsolve$/i.test(path);

export class Files {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly solver: FilesSolver;
  /** What clears the rescue copy as the window closes (Rescue.discard: it keeps one that must stay). */
  private readonly discardRescue: () => Promise<void>;
  /** The window's close is under way (its question, its save). */
  private closing = false;
  /** A close came while a save of the one under way ran (not taken: see close()). */
  private closeWaiting = false;

  constructor(store: Store, app: BtApp, solver: FilesSolver, discardRescue: () => Promise<void>) {
    this.store = store;
    this.app = app;
    this.solver = solver;
    this.discardRescue = discardRescue;
    onKey('file.new', () => {
      void this.newPuzzle();
      return true;
    });
    onKey('file.open', () => {
      void this.open();
      return true;
    });
    onKey('file.save', () => {
      void this.save();
      return true;
    });
    onKey('file.saveAs', () => {
      void this.saveAs();
      return true;
    });
    onKey('file.close', () => {
      void this.close();
      return true;
    });
    this.installDrop();
  }

  /** The name a document goes by: its file's, else the one shown. */
  get name(): string {
    const snap = this.store.snapshot;
    return snap?.path != null ? baseName(snap.path) : (this.store.displayName ?? 'Untitled');
  }

  // read through a call: the checker keeps a narrowing of the snapshot across awaits
  private clean(): boolean {
    return this.store.snapshot?.dirty === false;
  }

  private ready(): boolean {
    if (!this.store.running || this.store.snapshot === null) {
      flash('The engine is not running');
      return false;
    }
    return true;
  }

  /**
   * Before the puzzle is replaced: asks about unsaved changes (Save,
   * Don't Save, Cancel); true when it may go.
   */
  async confirmDiscard(action: string): Promise<boolean> {
    if (this.store.snapshot?.dirty !== true) {
      return true;
    }
    const k = await askChoice(
      'discard',
      'Unsaved changes',
      `The puzzle “${this.name}” has unsaved changes.\nSave before you ${action}?`,
      [
        { key: 'cancel', label: 'Cancel' },
        { key: 'drop', label: 'Don’t Save', kind: 'danger' },
        { key: 'save', label: 'Save', kind: 'pri', default: true },
      ],
    );
    if (k === 'drop') {
      return true;
    }
    if (k === 'save') {
      return (await this.save()) && this.clean();
    }
    return false;
  }

  /** File ▸ New: the grid chosen once, then an empty puzzle on it. */
  async newPuzzle(): Promise<void> {
    if (!this.ready() || !(await this.confirmDiscard('create a new puzzle'))) {
      return;
    }
    let info = this.store.info;
    if (info === null) {
      // the engine's info is read beside the first document: not here yet
      try {
        info = await this.store.engine.call('engine.info');
      } catch (e) {
        reportError(e, 'New puzzle');
        return;
      }
    }
    // the grids this version solves, sliding second as in the original's
    // grid selector (gridtypegui.cpp: Brick, Sliding, then the others)
    const grids = info.grids
      .filter((g) => g.supported)
      .sort((a, b) => newOrder(a.grid) - newOrder(b.grid));
    const k = await openDialog({
      id: 'newPuzzle',
      title: 'New puzzle',
      body: '<p>Choose the grid of its shapes. It cannot be changed later.</p>',
      buttons: [
        { key: '', label: 'Cancel' },
        ...grids.map((g, i) => ({
          key: g.grid,
          label: g.label,
          kind: 'pri' as const,
          ...(i === 0 ? { default: true } : {}),
        })),
      ],
    });
    if (k === null || k === '') {
      return;
    }
    try {
      await this.store.newDocument(k as Grid);
      flash('A new, empty puzzle');
    } catch (e) {
      reportError(e, 'New puzzle');
    }
  }

  /** File ▸ Open: a puzzle file chosen. */
  async open(): Promise<void> {
    if (!this.ready() || !(await this.confirmDiscard('open another puzzle'))) {
      return;
    }
    const path = await this.app.chooseOpen('puzzle');
    if (path !== null) {
      await this.load(path, false);
    }
  }

  /** A file to open: Open Recent, the system, a drop (asks about unsaved changes first). */
  async openPath(path: string, recent = true): Promise<void> {
    if (!this.ready()) {
      return;
    }
    const action = isPaused(path) ? 'import a paused solve' : 'open another puzzle';
    if (await this.confirmDiscard(action)) {
      await this.load(path, recent);
    }
  }

  private async load(path: string, recent: boolean): Promise<void> {
    try {
      if (isPaused(path)) {
        const problem = await this.store.importPaused(path);
        this.solver.showProblem(problem);
        flash(`Imported the paused solve of ${baseName(path)}: Continue carries it on`, 5000);
        return;
      }
      await this.store.openPath(path);
      this.app.addRecent(path);
      flash(`Opened ${baseName(path)}`);
    } catch (e) {
      reportError(e, `Could not open ${baseName(path)}`);
      if (recent && e instanceof EngineError && e.errc === 'io_error') {
        this.app.removeRecent(path);
      }
    }
  }

  /** File ▸ Save; true when the puzzle was saved. */
  async save(): Promise<boolean> {
    const snap = this.store.snapshot;
    if (!this.ready() || snap === null) {
      return false;
    }
    if (snap.path === null) {
      return this.saveAs();
    }
    return this.saveTo(undefined);
  }

  /** File ▸ Save As; true when the puzzle was saved. */
  async saveAs(): Promise<boolean> {
    if (!this.ready()) {
      return false;
    }
    const name = this.name.replace(/\.xmpuzzle$/i, '') + '.xmpuzzle';
    const path = await this.app.chooseSave('puzzle', name);
    return path !== null && this.saveTo(path);
  }

  private async saveTo(path: string | undefined): Promise<boolean> {
    if (this.store.solving) {
      return this.saveWhileSolving(path);
    }
    try {
      const saved = await this.store.save(path);
      this.app.addRecent(saved);
      flash(`Saved ${baseName(saved)}`);
      return true;
    } catch (e) {
      if (e instanceof EngineError && e.errc === 'invalid_state') {
        // a run started between the check and the call
        return this.saveWhileSolving(path);
      }
      reportError(e, 'Save');
      return false;
    }
  }

  /** A save the engine refuses while solving: offered at a save point. */
  private async saveWhileSolving(path: string | undefined): Promise<boolean> {
    if (!this.solver.runsUnderWay()) {
      // an analysis, which has no save point
      flash('An analysis is running: stop it or let it finish, then save', 5000);
      return false;
    }
    const k = await askChoice(
      'savePoint',
      'A solve is running',
      'The puzzle cannot be saved while a solve runs. Save at a save point? The solves pause where they can be saved, the puzzle is saved, and they carry on.',
      [
        { key: 'cancel', label: 'Cancel' },
        { key: 'save', label: 'Save at a save point', kind: 'pri', default: true },
      ],
    );
    if (k !== 'save') {
      return false;
    }
    const saved = await this.solver.saveAtSavePoint(path);
    if (saved !== null) {
      this.app.addRecent(saved);
      return true;
    }
    return false;
  }

  /**
   * The window is closing (its button, File ▸ Close, Ctrl+W, quitting):
   * asks about unsaved changes, then closes it, or keeps it on Cancel.
   */
  async close(): Promise<void> {
    if (this.closing) {
      // another close while one is under way: a question on screen takes it;
      // a save under way does not, so a save that never ends (an engine that
      // does not answer) leaves the main process to ask natively after a few
      // seconds: the window can always be closed
      if (dialogOpen()) {
        this.app.closeTaken();
      } else {
        this.closeWaiting = true;
      }
      return;
    }
    this.app.closeTaken();
    this.closing = true;
    try {
      const snap = this.store.snapshot;
      if (snap?.dirty === true && !this.store.running) {
        // the engine is not running (it died, or could not be kept running):
        // nothing can be saved now, and the rescue copy keeps the last changes
        const k = await askChoice(
          'closeAsk',
          'Unsaved changes',
          `The puzzle “${this.name}” has unsaved changes and cannot be saved now: the engine is not running.\nIts last changes are kept in the rescue copy, offered when the app starts again. Close the window?`,
          [
            { key: 'cancel', label: 'Cancel', default: true },
            { key: 'drop', label: 'Close', kind: 'danger' },
          ],
        );
        if (k !== 'drop') {
          this.app.closeCancelled();
          return;
        }
      } else if (snap?.dirty === true) {
        const k = await askChoice(
          'closeAsk',
          'Unsaved changes',
          `The puzzle “${this.name}” has unsaved changes.\nSave before you close the window?`,
          [
            { key: 'cancel', label: 'Cancel' },
            { key: 'drop', label: 'Don’t Save', kind: 'danger' },
            { key: 'save', label: 'Save', kind: 'pri', default: true },
          ],
        );
        if (k === 'save') {
          if (!(await this.save()) || !this.clean()) {
            this.app.closeCancelled();
            return;
          }
        } else if (k !== 'drop') {
          this.app.closeCancelled();
          return;
        }
      }
      await this.discardRescue();
      this.app.closeWindow();
    } finally {
      this.closing = false;
      if (this.closeWaiting) {
        // the close that came during the save ends with this one
        this.closeWaiting = false;
        this.app.closeTaken();
      }
    }
  }

  /** File ▸ Import ▸ Paused Solver State. */
  async importPaused(): Promise<void> {
    if (!this.ready() || !(await this.confirmDiscard('import a paused solve'))) {
      return;
    }
    const path = await this.app.chooseOpen('paused');
    if (path !== null) {
      await this.load(path, false);
    }
  }

  /** File ▸ Export ▸ Paused Solver State: the paused solve of the problem the Solver shows. */
  async exportPaused(): Promise<void> {
    const problem = this.solver.current;
    const p = this.store.snapshot?.problems.find((x) => x.id === problem);
    if (!this.ready() || problem === null || p === undefined) {
      return;
    }
    if (p.state !== 'solving' || this.solver.runsUnderWay()) {
      flash('Pause a solve first: the problem the Solver shows has no paused solve', 5000);
      return;
    }
    const base = this.name.replace(/\.xmpuzzle$/i, '');
    const path = await this.app.chooseSave('paused', `${base}.btsolve`);
    if (path === null) {
      return;
    }
    try {
      await this.store.engine.call('solve.exportPaused', { problem, path });
      flash(`Exported the paused solve of ${p.label} to ${baseName(path)}`);
    } catch (e) {
      reportError(e, 'Export the paused solve');
    }
  }

  // a file dropped on the window: by its path, else by its bytes
  private installDrop(): void {
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
      if (f === undefined) {
        return;
      }
      e.preventDefault();
      const path = this.app.pathOf(f);
      if (path !== '') {
        void this.openPath(path, false);
      } else {
        void this.openDropped(f);
      }
    });
  }

  private async openDropped(f: File): Promise<void> {
    if (!this.ready() || !(await this.confirmDiscard('open another puzzle'))) {
      return;
    }
    try {
      await this.store.openBytes(f.name, new Uint8Array(await f.arrayBuffer()));
      flash(`Opened ${f.name}`);
    } catch (e) {
      reportError(e, `Could not open ${f.name}`);
    }
  }
}
