// SPDX-License-Identifier: GPL-3.0-or-later
//
// Electron's main process (decision 0013): a window, one engine host per
// window, the bridge between them, and (stage 6) the menus, the file
// dialogs, the store of settings, layouts and recent files, the rescue
// copy, and the window's close. The renderer is sandboxed and isolated; it
// reaches the engine through the preload's window.bt and the rest through
// window.btApp (app-ipc.ts). A main-frame navigation (a reload) gets a new
// bridge server: the new page's proxy numbers its calls from 1 again, so
// replies to the old page's calls must not reach it; any other navigation
// (a file dropped outside the page's drop zone) and every new window are
// refused.
//
// Closing a window (its button, File ▸ Close, quitting) is the renderer's
// question: the close is held and the renderer told (`close`); it asks
// about unsaved changes and answers `closeWindow`. A renderer that does
// not take the command within a few seconds (hung, or crashed) gets the
// question natively, so a window can always be closed. A quit held by a
// window's question goes on once that window closes. One app runs at a
// time for a data directory: a second start hands its files to the first.
//
// For the e2e tests: BT_USER_DATA puts the app's data (the store, the
// rescue copy) in a directory of the test's own; with BT_CALL_LOG set
// every call the renderer makes is written to that file with how it ended
// (a JSON line: method, errc), so the tests can check that the UI never
// asked for what the engine refused; BT_AUTOSAVE_SECONDS shortens the
// Solver's autosave (20 minutes), passed in the page's address.

import { app, BrowserWindow, dialog, ipcMain, Menu } from 'electron';
import { appendFileSync, existsSync } from 'node:fs';
import { extname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { AppCommand, MenuState } from '../engine/app-bridge.ts';
import { NodeTransport } from '../engine/node-transport.ts';
import { installAppIpc, openHelp } from './app-ipc.ts';
import { findEngine } from './engine-path.ts';
import { EngineHost } from './engine-host.ts';
import { serveEngine } from './engine-ipc.ts';
import type { CallOutcome } from './engine-ipc.ts';
import { menuTemplate, NO_STATE } from './menu.ts';
import { RescueStore } from './rescue.ts';
import { StateStore } from './state-store.ts';

const APP_NAME = 'BurrTools Alpha';
app.setName(APP_NAME);
const userData = process.env.BT_USER_DATA;
if (userData !== undefined && userData !== '') {
  app.setPath('userData', resolve(userData));
}

const callLog = process.env.BT_CALL_LOG;
const logCall =
  callLog === undefined || callLog === ''
    ? undefined
    : (o: CallOutcome): void => {
        appendFileSync(callLog, JSON.stringify(o) + '\n');
      };

/** How long a renderer may take to take a close before it is asked natively. */
const CLOSE_TAKEN_MS = 4000;

/** What the main process keeps of a window. */
interface WindowState {
  win: BrowserWindow;
  menu: MenuState;
  /** The renderer answered a close: the next close goes through. */
  allowClose: boolean;
  /** A close was sent and not yet taken. */
  closeTimer: ReturnType<typeof setTimeout> | null;
  /** The close being asked came from a quit: quit once the window closed. */
  quitAfterClose: boolean;
}

const servers = new Map<number, ReturnType<typeof serveEngine>>();
const windows = new Map<number, WindowState>();
let store: StateStore;
let rescue: RescueStore;
/** A quit is under way (before-quit came, the windows are asked to close). */
let quitting = false;
/** Files the system asked to open before a window could take them. */
const pendingOpen: string[] = [];

/** The page's parameters: the autosave's interval when BT_AUTOSAVE_SECONDS sets one. */
const autosave = process.env.BT_AUTOSAVE_SECONDS;
const query: Record<string, string> = autosave !== undefined && autosave !== '' ? { autosave } : {};

function send(w: WindowState, command: AppCommand): void {
  if (!w.win.isDestroyed() && !w.win.webContents.isDestroyed()) {
    w.win.webContents.send('bt:command', command);
  }
}

/** The focused window (the menus act on it), else the first not destroyed. */
function current(): WindowState | undefined {
  const focused = BrowserWindow.getFocusedWindow();
  const w = focused === null ? undefined : windows.get(focused.webContents.id);
  if (w !== undefined) {
    return w;
  }
  for (const x of windows.values()) {
    if (!x.win.isDestroyed()) {
      return x;
    }
  }
  return undefined;
}

/** Builds the menus from the focused window's state. */
function updateMenu(): void {
  const w = current();
  const template = menuTemplate(w?.menu ?? NO_STATE, {
    platform: process.platform,
    name: APP_NAME,
    recent: store.recent,
    dev: !app.isPackaged,
    send: (command) => {
      const target = current();
      if (target === undefined) {
        if (command.cmd === 'new' || command.cmd === 'open' || command.cmd === 'openPath') {
          // macOS: no window open: a new one takes the command
          createWindow(command);
        }
        return;
      }
      send(target, command);
    },
    openHelp,
    clearRecent: () => {
      store.clearRecent();
    },
  });
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
  // the window may have gone while the menu was built: code run through the
  // inspector (the e2e tests' app.evaluate, which destroys the windows at a
  // test's end) can interrupt this function between two statements
  if (w !== undefined && !w.win.isDestroyed() && process.platform === 'darwin') {
    w.win.setDocumentEdited(w.menu.dirty);
    w.win.setRepresentedFilename(w.menu.path ?? '');
  }
}

/** The window could not be asked through its page: ask natively. */
async function askNatively(w: WindowState): Promise<void> {
  if (w.win.isDestroyed()) {
    return;
  }
  const { response } = await dialog.showMessageBox(w.win, {
    type: 'warning',
    buttons: ['Cancel', 'Close Anyway'],
    defaultId: 0,
    cancelId: 0,
    message: 'The window does not answer.',
    detail:
      'Close it anyway? Changes not saved are lost; the rescue copy of the last ones is offered when the app starts again.',
  });
  if (response === 1 && !w.win.isDestroyed()) {
    w.allowClose = true;
    w.win.close();
  } else {
    w.quitAfterClose = false;
  }
}

function createWindow(first?: AppCommand): void {
  const bounds = store.bounds;
  const win = new BrowserWindow({
    width: bounds?.width ?? 1280,
    height: bounds?.height ?? 800,
    ...(bounds === undefined || windows.size > 0 ? {} : { x: bounds.x, y: bounds.y }),
    title: APP_NAME,
    show: false,
    webPreferences: {
      preload: fileURLToPath(new URL('../preload/index.cjs', import.meta.url)),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  const contents = win.webContents;
  const id = contents.id;
  const w: WindowState = {
    win,
    menu: NO_STATE,
    allowClose: false,
    closeTimer: null,
    quitAfterClose: false,
  };
  windows.set(id, w);
  const engine = findEngine(app.getAppPath());
  const host = new EngineHost({
    start: () => {
      if (engine === null) {
        throw new Error('no bt-engine: build one (just build) or set BT_ENGINE');
      }
      return NodeTransport.start(engine);
    },
  });
  const serve = (): ReturnType<typeof serveEngine> =>
    serveEngine(
      host,
      (message) => {
        if (!contents.isDestroyed()) {
          contents.send('bt:message', message);
        }
      },
      logCall,
    );
  servers.set(id, serve());
  contents.on('did-start-navigation', (details) => {
    if (details.isMainFrame && !details.isSameDocument) {
      servers.get(id)?.dispose();
      servers.set(id, serve());
    }
  });
  // the page never leaves itself: a file dropped outside its drop zone, a link
  contents.on('will-navigate', (e) => {
    e.preventDefault();
  });
  contents.setWindowOpenHandler(() => ({ action: 'deny' }));

  win.on('close', (e) => {
    if (w.allowClose) {
      return;
    }
    e.preventDefault();
    // a quit's close: the quit goes on once the window closed; a close of
    // the window's own meanwhile (its button) does not give the quit up
    if (quitting) {
      w.quitAfterClose = true;
      quitting = false;
    }
    if (w.closeTimer !== null) {
      // asked already and not taken yet: the timer asks natively
      return;
    }
    if (contents.isCrashed()) {
      void askNatively(w);
      return;
    }
    send(w, { cmd: 'close' });
    w.closeTimer = setTimeout(() => {
      w.closeTimer = null;
      void askNatively(w);
    }, CLOSE_TAKEN_MS);
  });
  win.on('closed', () => {
    if (w.closeTimer !== null) {
      clearTimeout(w.closeTimer);
    }
    windows.delete(id);
    servers.get(id)?.dispose();
    servers.delete(id);
    void host.stop();
    updateMenu();
    if (w.quitAfterClose) {
      app.quit();
    }
  });
  const keepBounds = (): void => {
    if (!win.isMinimized() && !win.isMaximized() && !win.isFullScreen()) {
      store.setBounds(win.getBounds());
    }
  };
  win.on('resize', keepBounds);
  win.on('move', keepBounds);
  win.on('focus', updateMenu);
  win.once('ready-to-show', () => {
    win.show();
  });
  contents.once('did-finish-load', () => {
    for (const path of pendingOpen.splice(0)) {
      send(w, { cmd: 'openPath', path });
    }
    if (first !== undefined) {
      send(w, first);
    }
  });
  host.start().catch((e: unknown) => {
    console.error(`bt-engine did not start: ${String(e)}`);
  });
  const devUrl = process.env.ELECTRON_RENDERER_URL;
  if (devUrl !== undefined) {
    // in development the renderer's warnings and errors show in the terminal
    contents.on('console-message', (event) => {
      if (event.level === 'warning' || event.level === 'error') {
        console.log(`renderer ${event.level}: ${event.message}`);
      }
    });
    const url = new URL(devUrl);
    for (const [k, v] of Object.entries(query)) {
      url.searchParams.set(k, v);
    }
    void win.loadURL(url.toString());
  } else {
    void win.loadFile(fileURLToPath(new URL('../renderer/index.html', import.meta.url)), {
      query,
    });
  }
}

ipcMain.on('bt:call', (event, call: unknown) => {
  servers.get(event.sender.id)?.receive(call);
});

/** A puzzle or paused-solve file given on the command line (Windows, Linux). */
function argFiles(): string[] {
  return process.argv
    .slice(app.isPackaged ? 1 : 2)
    .filter((a) => ['.xmpuzzle', '.btsolve'].includes(extname(a).toLowerCase()) && existsSync(a))
    .map((a) => resolve(a));
}

/** Opens files the system gave the app (a window takes them, or a new one once ready). */
function openFiles(paths: readonly string[]): void {
  if (!primary) {
    return;
  }
  const w = current();
  if (w === undefined || !app.isReady()) {
    pendingOpen.push(...paths);
    if (app.isReady()) {
      createWindow();
    }
    return;
  }
  for (const path of paths) {
    send(w, { cmd: 'openPath', path });
  }
}

// One app at a time for a data directory: the store and the rescue copy are
// the app's. A second start (Windows and Linux start one for each file opened
// from the file manager) would offer the running app's rescue copy as left by
// a session that died, and its Discard or a clean close would remove the copy
// of a document still being edited: it hands its files to the first and quits.
const primary = app.requestSingleInstanceLock({ files: argFiles() });
if (!primary) {
  app.quit();
}
app.on('second-instance', (_e, _argv, _dir, data: unknown) => {
  const files = (data as { files?: unknown } | null)?.files;
  if (Array.isArray(files)) {
    openFiles(files.filter((f): f is string => typeof f === 'string' && f !== ''));
  }
  const w = current();
  if (w !== undefined && !w.win.isDestroyed()) {
    if (w.win.isMinimized()) {
      w.win.restore();
    }
    w.win.focus();
  }
});

// macOS: a file opened with the app (Finder, the Dock)
app.on('open-file', (e, path) => {
  e.preventDefault();
  openFiles([path]);
});

app.on('before-quit', () => {
  quitting = true;
});

app.on('will-quit', () => {
  if (primary) {
    store.flush();
  }
});

void app.whenReady().then(() => {
  if (!primary) {
    // a second start: quitting, its files handed to the first
    return;
  }
  store = new StateStore(app.getPath('userData'));
  rescue = new RescueStore(app.getPath('userData'));
  pendingOpen.push(...argFiles());
  installAppIpc({
    store,
    rescue,
    version: app.getVersion(),
    window: (sender) => windows.get(sender.id)?.win ?? null,
    setMenu: (sender, state) => {
      const w = windows.get(sender.id);
      if (w !== undefined) {
        w.menu = state;
        updateMenu();
      }
    },
    closeTaken: (sender) => {
      const w = windows.get(sender.id);
      if (w?.closeTimer != null) {
        clearTimeout(w.closeTimer);
        w.closeTimer = null;
      }
    },
    closeWindow: (sender) => {
      const w = windows.get(sender.id);
      if (w !== undefined && !w.win.isDestroyed()) {
        w.allowClose = true;
        w.win.close();
      }
    },
    cancelQuit: (sender) => {
      const w = windows.get(sender.id);
      if (w !== undefined) {
        w.quitAfterClose = false;
      }
    },
  });
  store.onRecent(updateMenu);
  updateMenu();
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
