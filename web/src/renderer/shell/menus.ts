// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's side of the application menus (stage 6; the menus are
// Electron's, main/menu.ts): what they show (the document, its dirty
// mark, the labels of Undo and Redo from edit.history, the Solver's
// problem, the display mode and style, the 3D focus, the keys of the one
// table) sent whenever it changes, and what their commands do. A command
// that has a key in the table runs that key's handlers (keyboard.ts:
// `runAction`), so a menu item and its key do the same thing.

import type { AppCommand, AppCommandName, BtApp, MenuState } from '../../engine/app-bridge.ts';
import type { Store } from '../state/store.ts';
import type { View3D } from '../view3d/view.ts';
import { runAction } from './keyboard.ts';
import { accelerator } from './keys.ts';
import type { KeyAction } from './keys.ts';
import { layout, onLayout } from './layout.ts';

/** The menu commands that are actions of the keyboard table. */
const AS_KEY: Partial<Record<AppCommandName, KeyAction>> = {
  new: 'file.new',
  open: 'file.open',
  save: 'file.save',
  saveAs: 'file.saveAs',
  undo: 'undo',
  redo: 'redo',
  settings: 'settings',
  keys: 'help.keys',
  focus3d: 'focus3d',
};

/** The keys the menus show: the table's, for the commands that have one. */
export function menuKeys(): MenuState['keys'] {
  const keys: MenuState['keys'] = {};
  for (const [cmd, action] of Object.entries(AS_KEY) as [AppCommandName, KeyAction][]) {
    const a = accelerator(action);
    if (a !== null) {
      keys[cmd] = a;
    }
  }
  const close = accelerator('file.close');
  if (close !== null) {
    keys.close = close;
  }
  return keys;
}

/** What the commands without a key do. */
export interface MenuHandlers {
  openPath(path: string): void;
  close(): void;
  importPaused(): void;
  exportPaused(): void;
  toShapes(): void;
  notes(): void;
  statusWindow(): void;
  about(): void;
  /** The Solver's problem: a paused solve, saved solutions. */
  solverProblem(): { paused: boolean } | null;
}

export class Menus {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly view: View3D;
  private readonly h: MenuHandlers;
  private readonly keys = menuKeys();
  private labels: { undo: string | null; redo: string | null } = { undo: null, redo: null };
  private historyLoads = 0;
  private sent = '';
  private timer: number | null = null;

  constructor(store: Store, app: BtApp, view: View3D, handlers: MenuHandlers) {
    this.store = store;
    this.app = app;
    this.view = view;
    this.h = handlers;
    app.onCommand((c) => {
      this.run(c);
    });
    store.subscribe((c) => {
      if (c.kind === 'document' || c.kind === 'status') {
        this.loadHistory();
        this.update();
      }
    });
    view.onDisplay(() => {
      this.update();
    });
    onLayout(() => {
      this.update();
    });
    this.update();
  }

  /** Sends the state soon (once for a burst of changes), if it changed. */
  update(): void {
    if (this.timer !== null) {
      return;
    }
    this.timer = window.setTimeout(() => {
      this.timer = null;
      const s = JSON.stringify(this.state());
      if (s !== this.sent) {
        this.sent = s;
        this.app.setMenuState(JSON.parse(s) as MenuState);
      }
    }, 30);
  }

  /** What the menus show now. */
  state(): MenuState {
    const snap = this.store.running ? this.store.snapshot : null;
    const p = this.h.solverProblem();
    return {
      document: snap !== null,
      dirty: snap?.dirty ?? false,
      path: snap?.path ?? null,
      undo: snap?.solving === false && snap.canUndo ? this.labels.undo : null,
      redo: snap?.solving === false && snap.canRedo ? this.labels.redo : null,
      solving: snap?.solving ?? false,
      paused: p?.paused ?? false,
      solutions: snap?.problems.some((x) => x.saved > 0) ?? false,
      colourView: this.view.display.colourView,
      style: this.view.display.style,
      focus3d: layout.focus === '3d',
      keys: this.keys,
    };
  }

  // the labels of the next undo and redo steps
  private loadHistory(): void {
    const snap = this.store.snapshot;
    const n = ++this.historyLoads;
    if (snap === null || !this.store.running || (!snap.canUndo && !snap.canRedo)) {
      this.labels = { undo: null, redo: null };
      return;
    }
    this.store.engine.call('edit.history', {}, { coalesce: 'edit.history' }).then(
      (h) => {
        if (n === this.historyLoads) {
          this.labels = { undo: h.undo.at(-1) ?? null, redo: h.redo[0] ?? null };
          this.update();
        }
      },
      () => {
        // a newer read took its place, or the engine went: the next change reads again
      },
    );
  }

  /** Does what a command asks. */
  run(c: AppCommand): void {
    const action = AS_KEY[c.cmd];
    if (action !== undefined) {
      runAction(action);
      return;
    }
    switch (c.cmd) {
      case 'openPath':
        this.h.openPath(c.path);
        break;
      case 'close':
        this.h.close();
        break;
      case 'importPaused':
        this.h.importPaused();
        break;
      case 'exportPaused':
        this.h.exportPaused();
        break;
      case 'toShapes':
        this.h.toShapes();
        break;
      case 'notes':
        this.h.notes();
        break;
      case 'statusWindow':
        this.h.statusWindow();
        break;
      case 'about':
        this.h.about();
        break;
      case 'colourView':
        this.view.setDisplay({ colourView: c.on });
        break;
      case 'style':
        this.view.setDisplay({ style: c.style });
        break;
      default:
        // the commands with a key ran above
        break;
    }
  }
}
