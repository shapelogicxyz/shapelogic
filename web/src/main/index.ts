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
// The security checklist (stage 8, security.ts): the window sandboxed and
// isolated; the page the app's own, on its own scheme (`bt-app://app/`,
// served from the renderer's directory only; a dev server only when not
// packaged); every request but the app's files cancelled; no permission
// granted; no webview, no new window, no navigation; a call naming a file
// the window was not handed refused before the engine sees it
// (path-grants.ts), and so is a page's settings.set naming one of the
// engine's folders: the main process sets the saved searches' folder
// (`panex` in the app's data directory, deferred part 4) on every engine
// before the page may call it; exports written only where the dialog said
// (app-ipc.ts).
//
// For the e2e tests: BT_USER_DATA puts the app's data (the store, the saved searches, the
// rescue copy) in a directory of the test's own; with BT_CALL_LOG set
// every call the renderer makes is written to that file with how it ended
// (a JSON line: method, errc), so the tests can check that the UI never
// asked for what the engine refused; BT_AUTOSAVE_SECONDS shortens the
// Solver's autosave (20 minutes), passed in the page's address. A
// packaged app takes BT_USER_DATA only (security.ts's switches).

import { app, BrowserWindow, dialog, ipcMain, Menu, net, protocol, session } from 'electron';
import type { WebContents } from 'electron';
import { appendFileSync, existsSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import type { AppCommand, MenuState } from '../engine/app-bridge.ts';
import { NodeTransport } from '../engine/node-transport.ts';
import { installAppIpc, openHelp } from './app-ipc.ts';
import { cliLink, findCli } from './cli-tool.ts';
import { findEngine } from './engine-path.ts';
import { EngineHost } from './engine-host.ts';
import { serveEngine } from './engine-ipc.ts';
import type { CallOutcome } from './engine-ipc.ts';
import { menuTemplate, NO_STATE } from './menu.ts';
import { PathGrants, settingsRefusal } from './path-grants.ts';
import { RescueStore } from './rescue.ts';
import {
  allowedRequest,
  APP_SCHEME,
  appFile,
  appUrl,
  permissionAllowed,
  switches,
  windowOptions,
} from './security.ts';
import { StateStore } from './state-store.ts';

const APP_NAME = 'ShapeLogic';
/** The project's version (CMakeLists.txt's, put in at build): the app's and its engine's. */
const VERSION = __BT_VERSION__;
/** The web/ directory, where development looks for the repository's engine builds. */
const WEB_DIR = fileURLToPath(new URL('../../', import.meta.url));
app.setName(APP_NAME);
const env = switches(app.isPackaged, process.env);
if (env.userData !== undefined) {
  app.setPath('userData', resolve(env.userData));
}

const callLog = env.callLog;
const logCall =
  callLog === undefined
    ? undefined
    : (o: CallOutcome): void => {
        appendFileSync(callLog, JSON.stringify(o) + '\n');
      };

/** The renderer's directory (out/renderer, in app.asar when packaged). */
const rendererDir = fileURLToPath(new URL('../renderer/', import.meta.url));
// the page's own scheme (security.ts), registered before the app is ready
protocol.registerSchemesAsPrivileged([
  { scheme: APP_SCHEME, privileges: { standard: true, secure: true, codeCache: true } },
]);

/** The files each window's renderer was handed: the only ones its engine may be given. */
const paths = new PathGrants<WebContents>();

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
const query: Record<string, string> = env.autosave !== undefined ? { autosave: env.autosave } : {};

function send(w: WindowState, command: AppCommand): void {
  if (!w.win.isDestroyed() && !w.win.webContents.isDestroyed()) {
    if (command.cmd === 'openPath') {
      // a file the menus or the system opened with the app
      paths.grant(w.win.webContents, command.path);
    }
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
  const icon = join(WEB_DIR, 'packaging', 'icon.png');
  const win = new BrowserWindow({
    ...windowOptions(fileURLToPath(new URL('../preload/index.cjs', import.meta.url)), APP_NAME, {
      width: bounds?.width ?? 1280,
      height: bounds?.height ?? 800,
      ...(bounds === undefined || windows.size > 0 ? {} : { x: bounds.x, y: bounds.y }),
    }),
    // The window icon of Windows and Linux; macOS takes the app's.
    ...(existsSync(icon) ? { icon } : {}),
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
  const engine = findEngine({
    packaged: app.isPackaged,
    resources: process.resourcesPath,
    webDir: WEB_DIR,
    engine: env.engine,
  });
  const host = new EngineHost({
    start: () => {
      if (engine === null) {
        throw new Error(
          app.isPackaged
            ? 'no bt-engine in the app'
            : 'no bt-engine: build one (just build) or set BT_ENGINE',
        );
      }
      // one version for the app and the engine (CMakeLists.txt's)
      return NodeTransport.start(engine, { expectVersion: VERSION });
    },
    // the saved searches of stacking solves in the app's data directory
    // (deferred part 4), before the page may call the engine
    setup: async (client) => {
      await client.call('settings.set', { panexFolder: join(app.getPath('userData'), 'panex') });
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
      (method, params) =>
        paths.refusal(contents, method, params) ?? settingsRefusal(method, params),
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
  const devUrl = env.rendererUrl;
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
    void win.loadURL(appUrl('index.html', query));
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
  // Development runs Electron's own bundle, whose Dock icon is Electron's;
  // a packaged app has its icon in its bundle already.
  const dockIcon = join(WEB_DIR, 'packaging', 'icon.png');
  if (!app.isPackaged && process.platform === 'darwin' && existsSync(dockIcon)) {
    app.dock?.setIcon(dockIcon);
  }
  store = new StateStore(app.getPath('userData'));
  rescue = new RescueStore(app.getPath('userData'));
  pendingOpen.push(...argFiles());
  // the page and its files, from the renderer's directory and nowhere else
  protocol.handle(APP_SCHEME, (request) => {
    const file = appFile(request.url, rendererDir);
    const notFound = (): Response => new Response('not found', { status: 404 });
    if (file === null) {
      return notFound();
    }
    return net.fetch(pathToFileURL(file).href).catch(notFound);
  });
  // no remote content, no permission
  session.defaultSession.webRequest.onBeforeRequest((details, done) => {
    done({ cancel: !allowedRequest(details.url, { rendererDir, rendererUrl: env.rendererUrl }) });
  });
  session.defaultSession.setPermissionRequestHandler((_contents, permission, done) => {
    done(permissionAllowed(permission));
  });
  session.defaultSession.setPermissionCheckHandler((_contents, permission) =>
    permissionAllowed(permission),
  );
  installAppIpc({
    store,
    rescue,
    paths,
    version: VERSION,
    cliPath: findCli({
      packaged: app.isPackaged,
      resources: process.resourcesPath,
      webDir: WEB_DIR,
    }),
    cliLink: cliLink(process.platform),
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

// no webview and no new window, whatever web contents asks
app.on('web-contents-created', (_e, contents) => {
  contents.on('will-attach-webview', (e) => {
    e.preventDefault();
  });
  contents.setWindowOpenHandler(() => ({ action: 'deny' }));
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
