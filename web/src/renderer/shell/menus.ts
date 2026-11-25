// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's side of the application menus (stage 6; the menus are
// Electron's, main/menu.ts): what they show (the document, its dirty
// mark, the labels of Undo and Redo from edit.history, the Solver's
// problem, the display mode and style, the 3D focus, the keys of the one
// table) sent whenever it changes, and what their commands do. A command
// that has a key in the table runs that key's handlers (keyboard.ts:
// `runAction`), so a menu item and its key do the same thing. A command
// that replaces the document and comes before the first document is shown
// (a file given on the command line, the engine still starting) waits for
// it (stage 8); once the engine has failed it is not held but shows the
// engine's dialog, as do the commands still waiting then (cleanup item
// 2.3). The log hears of each one held or dropped. One that comes while a
// dialog is open waits for it to close (cleanup item 6.2).

import type { AppCommand, AppCommandName, BtApp, MenuState } from '../../engine/app-bridge.ts';
import type { Store } from '../state/store.ts';
import type { View3D } from '../view3d/view.ts';
import { exportsAllowed } from '../export/exports.ts';
import { CommandGate } from './command-gate.ts';
import { dialogOpen, onDialogsClosed } from './dialog.ts';
import { runAction } from './keyboard.ts';
import { accelerator } from './keys.ts';
import type { KeyAction } from './keys.ts';
import { layout, onLayout, tab } from './layout.ts';
import { onPrefs, prefs, setPrefs } from './prefs.ts';

/** The menu commands that are actions of the keyboard table. */
const AS_KEY = {
  new: 'file.new',
  newWindow: 'file.newWindow',
  open: 'file.open',
  save: 'file.save',
  saveAs: 'file.saveAs',
  undo: 'undo',
  redo: 'redo',
  settings: 'settings',
  keys: 'help.keys',
  focus3d: 'focus3d',
} as const satisfies Partial<Record<AppCommandName, KeyAction>>;

/** The commands that run a key's handlers. */
type KeyCommand = keyof typeof AS_KEY;

/** The commands the menus do themselves: the 3D view's display and the theme. */
type DisplayCommand = 'colorView' | 'style' | 'theme';

/** The command of a name, with its arguments. */
type CommandOf<K extends AppCommandName> = Extract<AppCommand, { cmd: K }>;

/** A handler for each of some commands: the compiler asks for every one. */
type HandlersOf<K extends AppCommandName> = { [C in K]: (c: CommandOf<C>) => void };

/**
 * What the commands without a key do, one handler a command (a command
 * added to AppCommand fails the build until it has one).
 */
export type MenuHandlers = HandlersOf<Exclude<AppCommandName, KeyCommand | DisplayCommand>>;

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

/** What the menus ask the workspaces. */
export interface MenuQueries {
  /** Whether the Solver shows a solution with its moves stored (the animation's export). */
  animation(): boolean;
  /** The Solver's problem: a paused solve, saved solutions. */
  solverProblem(): { paused: boolean } | null;
  /** The Play workspace's Undo and Redo, as Edit names them there (null: nothing to undo or redo). */
  playHistory(): { undo: string | null; redo: string | null };
  /** The engine could not start (engine-failure.ts): no document will come. */
  engineFailed(): boolean;
  /** Shows the engine's dialog (a document command came while it has failed). */
  showEngineFailure(): void;
}

export class Menus {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly view: View3D;
  private readonly q: MenuQueries;
  /** Every command without a key, and what it does. */
  private readonly commands: HandlersOf<Exclude<AppCommandName, KeyCommand>>;
  private readonly keys = menuKeys();
  private labels: { undo: string | null; redo: string | null } = { undo: null, redo: null };
  private historyLoads = 0;
  private sent = '';
  private timer: number | null = null;
  /** Commands that came before the first document (command-gate.ts). */
  private readonly gate: CommandGate;

  constructor(
    store: Store,
    app: BtApp,
    view: View3D,
    handlers: MenuHandlers,
    queries: MenuQueries,
  ) {
    this.store = store;
    this.app = app;
    this.view = view;
    this.q = queries;
    this.commands = {
      ...handlers,
      colorView: (c) => {
        view.setDisplay({ colorView: c.on });
      },
      style: (c) => {
        view.setDisplay({ style: c.style });
      },
      theme: (c) => {
        setPrefs({ theme: c.theme });
      },
    };
    this.gate = new CommandGate({
      hasDocument: () => store.snapshot !== null,
      engineFailed: () => queries.engineFailed(),
      run: (c) => {
        this.run(c);
      },
      held: (cmd, dropped) => {
        app.commandHeld(cmd, dropped);
      },
      showFailure: () => {
        queries.showEngineFailure();
      },
      dialogOpen,
      whenDialogsClosed: onDialogsClosed,
    });
    app.onCommand((c) => {
      this.gate.command(c);
    });
    store.subscribe((c) => {
      if (c.kind === 'document' || c.kind === 'status') {
        this.gate.changed();
      }
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
    onPrefs((changed) => {
      if (changed.includes('theme')) {
        this.update();
      }
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
    const p = this.q.solverProblem();
    // on the Play tab Undo and Redo are the game's moves (decision 0039)
    const game = tab === 'play' ? this.q.playHistory() : null;
    const allowed = exportsAllowed(snap, snap !== null && this.q.animation());
    return {
      document: snap !== null,
      dirty: snap?.dirty ?? false,
      path: snap?.path ?? null,
      undo:
        game !== null
          ? game.undo
          : snap?.solving === false && snap.canUndo
            ? this.labels.undo
            : null,
      redo:
        game !== null
          ? game.redo
          : snap?.solving === false && snap.canRedo
            ? this.labels.redo
            : null,
      solving: snap?.solving ?? false,
      paused: p?.paused ?? false,
      solutions: snap?.problems.some((x) => x.saved > 0) ?? false,
      // File ▸ Export's rules (exports.ts)
      shapes: allowed.stl,
      scad: allowed.scad,
      animation: allowed.glb,
      colorView: this.view.display.colorView,
      style: this.view.display.style,
      focus3d: layout.focus === '3d',
      theme: prefs.theme,
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

  /** Does what a command asks: a key's handlers, or its own. */
  run(c: AppCommand): void {
    const action = (AS_KEY as Partial<Record<AppCommandName, KeyAction>>)[c.cmd];
    if (action !== undefined) {
      runAction(action);
      return;
    }
    const run = this.commands[c.cmd as Exclude<AppCommandName, KeyCommand>] as (
      c: AppCommand,
    ) => void;
    run(c);
  }
}
