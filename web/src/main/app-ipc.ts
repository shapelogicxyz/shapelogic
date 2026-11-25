// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's side of window.btApp (engine/app-bridge.ts, stage 6):
// the file dialogs (a name without the kind's extension gets it, and a
// file that then exists is asked about, as the original's Save As did),
// the store of settings, layouts and recent files, the rescue copy, the
// command line tool (cli-tool.ts: the renderer names an operation, never a
// path), the help links (app-bridge.ts's table: the renderer names a link,
// never a URL), the menus' state and the window's close; from stage
// 7 the exports' files, written here (export-file.ts) and only to a path
// this process's save dialog gave the same window for that kind, once,
// and from deferred part 5 into the folder its folder dialog gave last,
// by the names the engine gives a solution's pieces.
// From stage 8 the files a window's engine may be given (path-grants.ts)
// are granted here: what the open and save dialogs gave for a puzzle or a
// paused solve, the rescue record's file and the copy's own (the engine
// writes and opens the copy itself), a file dropped on the window.
// The update check (updates.ts) asks GitHub here, once at startup and when
// the menu asks; its Download opens a fixed address, never one the
// renderer gives. The cache folder (cache-folder.ts) is chosen in this
// process's folder dialog, and what to do with the data there is the
// renderer's answer to the choice it was handed, never a path of its own.
// The cache manager (cache-files.ts) lists what the cache folder and the
// rescue copies hold and deletes an item by the id it listed it by; a
// window's links of its search folders name only folders in the cache
// folder in use, and a puzzle file only one the window was handed.
// Several windows (decision 0036): each has its own rescue copy (rescue.ts's
// slots), a window tells which file it opened (path-owners.ts: the first
// window to hold a file owns it, another holds it read-only), New Window
// asks for another, and a change of the settings in one reaches the others.
// Every message is checked before it is used, and only the app's windows
// are answered: each channel of app-bridge.ts's table (cleanup item 12.2)
// is registered through `handle`, `on` or `sync`, which check the sender
// once; the engine's bridge (`bt:call`) goes to main/index.ts's server.

import { app, dialog, ipcMain, shell } from 'electron';
import type { BrowserWindow, IpcMainEvent, IpcMainInvokeEvent, WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';

import {
  helpUrl,
  isAppCommandName,
  isCacheAction,
  isCacheLink,
  isLogQuery,
  isExportKind,
  isFileKind,
  isMenuState,
  isOpenKind,
  isGoalPicture,
  isHelpLink,
  isRescueInfo,
} from '../engine/app-bridge.ts';
import type {
  AppCommandName,
  ChannelArgs,
  ChannelOf,
  ChannelResult,
  DiagnosticsResult,
  FileKind,
  HelpLink,
  LogExportResult,
  MenuState,
  OpenKind,
  StoredState,
  WriteResult,
} from '../engine/app-bridge.ts';
import { isEngineError } from '../engine/errors.ts';
import { writeHint } from '../engine/write-hint.ts';
import type { AppLog } from './app-log.ts';
import type { CacheFolderManager } from './cache-folder.ts';
import { deleteItem, listItems } from './cache-files.ts';
import type { CacheLinks } from './cache-files.ts';
import { ExportGrants, FolderGrants, isPieceFileName, writeFileSafely } from './export-file.ts';
import { GoalWindows } from './goal-window.ts';
import { cliInfo, installCli, macPrivileged, uninstallCli } from './cli-tool.ts';
import type { EngineHost } from './engine-host.ts';
import type { PathGrants } from './path-grants.ts';
import type { PathOwners } from './path-owners.ts';
import type { RescueSlots } from './rescue.ts';
import { SYSTEM_URLS } from './security.ts';
import type { StateStore } from './state-store.ts';
import { checkForUpdate, parseVersion } from './updates.ts';
import type { FetchFn } from './updates.ts';

/** A date for an exported log's name: 2026-11-16. */
function logStamp(d: Date): string {
  const p = (n: number): string => String(n).padStart(2, '0');
  return `${String(d.getFullYear())}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

/** What a handler is handed of a channel's arguments: as many, each unchecked (the renderer is not trusted). */
type Unchecked<A extends readonly unknown[]> = { [K in keyof A]-?: unknown };

/** Sends a window's renderer a message of a main-to-renderer channel (app-bridge.ts's table). */
export function post<C extends ChannelOf<'event'>>(
  contents: Pick<WebContents, 'send'>,
  channel: C,
  ...args: ChannelArgs<C>
): void {
  contents.send(channel, ...args);
}

/** Opens a help page in the system's browser. */
export function openHelp(link: HelpLink): void {
  void shell.openExternal(helpUrl(link));
}

/** Each kind's extension, the dialog's filter and its title. */
export const KINDS: Readonly<
  Record<FileKind, { ext: string; filter: { name: string; extensions: string[] }; title: string }>
> = {
  puzzle: {
    ext: '.xmpuzzle',
    filter: { name: 'Puzzle Files', extensions: ['xmpuzzle'] },
    title: 'Save Puzzle As',
  },
  paused: {
    ext: '.slsolve',
    filter: { name: 'Paused Solver State', extensions: ['slsolve'] },
    title: 'Save Paused Solver State As',
  },
  image: {
    ext: '.png',
    filter: { name: 'PNG Images', extensions: ['png'] },
    title: 'Export Image',
  },
  vector: {
    ext: '.svg',
    filter: { name: 'SVG Images', extensions: ['svg'] },
    title: 'Export Vector Image',
  },
  stl: {
    ext: '.stl',
    filter: { name: 'STL Files', extensions: ['stl'] },
    title: 'Export STL',
  },
  glb: {
    ext: '.glb',
    filter: { name: 'glTF Binary', extensions: ['glb'] },
    title: 'Export Solution Animation',
  },
  // the moves of a sliding or a stacking puzzle played by hand (decisions 0039 and 0042)
  moves: {
    ext: '.slmoves',
    filter: { name: 'ShapeLogic Moves', extensions: ['slmoves'] },
    title: 'Export Moves',
  },
  // deferred part 6: mainwindow_file.cpp's choosers
  ps3d: {
    ext: '.puz',
    filter: { name: 'PuzzleSolver3D Files', extensions: ['puz'] },
    title: 'Import PuzzleSolver3D File',
  },
  scad: {
    ext: '.scad',
    filter: { name: 'OpenSCAD Files', extensions: ['scad'] },
    title: 'Export Puzzlecad File',
  },
};

/** The open dialog's title of each kind it opens. */
const OPEN_TITLES: Readonly<Record<OpenKind, string>> = {
  puzzle: 'Open Puzzle',
  paused: 'Open Paused Solver State',
  ps3d: 'Import PuzzleSolver3D File',
  scad: 'Import Puzzlecad File',
};

/** The paths the save dialog gave each window's renderer for an export: `bt:write-export` writes only these. */
const grants = new ExportGrants<WebContents>((kind) => KINDS[kind].ext);
/** The folder the folder dialog gave each window's renderer last: `bt:write-folder-export` writes only there. */
const folders = new FolderGrants<WebContents>();

export interface AppIpcHost {
  store: StateStore;
  /** The windows' rescue copies, by the web contents' id. */
  rescue: RescueSlots<number>;
  /** A window's engine, or null (the main process asks it to write the rescue copy). */
  engine(sender: WebContents): Pick<EngineHost, 'call'> | null;
  /** The files each window's engine may be given. */
  paths: PathGrants<WebContents>;
  /** Which window owns each file it holds, by the web contents' id. */
  owners: PathOwners<number>;
  version: string;
  /** Whether the app is packaged: only a packaged app checks for updates at startup. */
  packaged: boolean;
  /** The fetch the update check uses (a session of its own: the windows' lets nothing remote through). */
  fetch: FetchFn;
  /** The command line tool in this install (cli-tool.ts), or null when there is none. */
  cliPath: string | null;
  /** Where Install links it, or null where this platform has no Install. */
  cliLink: string | null;
  /** The cache folder in use and its choices. */
  cache: CacheFolderManager<WebContents>;
  /** The search folders' links to their puzzles (the cache manager's names). */
  cacheLinks: CacheLinks;
  /** The app's log (View ▸ Log File Manager…). */
  log: AppLog;
  /** The settings were stored again (a window changed one): Debug logging follows them. */
  prefsChanged(prefs: Record<string, unknown>): void;
  /** The app's window of a renderer, or null (not ours: not answered). */
  window(sender: WebContents): BrowserWindow | null;
  setMenu(sender: WebContents, state: MenuState): void;
  /** File ▸ New Window: another window, with an empty puzzle. */
  newWindow(): void;
  /** The app's windows but this one (a change of the settings reaches them). */
  others(sender: WebContents): WebContents[];
  closeTaken(sender: WebContents): void;
  closeWindow(sender: WebContents): void;
  cancelQuit(sender: WebContents): void;
  /** An update check's outcome in words (updates.ts), for the log and the diagnostics. */
  updateChecked(text: string): void;
  /** The page showed its first document (the Startup line). */
  ready(sender: WebContents): void;
  /** Try Again: the window's engine is started again. */
  retryEngine(sender: WebContents): void;
  /** A document command was held for the first document, or dropped (the engine failed). */
  commandHeld(sender: WebContents, command: AppCommandName, dropped: boolean): void;
  /** Copy Diagnostics (diagnostics.ts): the report on the clipboard. */
  copyDiagnostics(sender: WebContents): Promise<DiagnosticsResult>;
  /** The report saved where the save dialog says. */
  saveDiagnostics(sender: WebContents): Promise<LogExportResult>;
  /** Whether windows are kept hidden (security.ts's BT_BACKGROUND). */
  background: boolean;
  /** A message of a window's engine bridge (`bt:call`): to its engine (main/index.ts's server). */
  engineCall(sender: WebContents, request: unknown): void;
}

/** Installs the handlers. */
export function installAppIpc(h: AppIpcHost): void {
  // a write Windows refused as access denied: the log says where, and that
  // Controlled Folder Access may be why (cleanup item 2.5)
  const logRefusedWrite = (path: string, message: string): void => {
    const hint = writeHint(process.platform, message);
    if (hint !== null) {
      h.log.warn('app', 'Write Refused', `${message} in the folder ${dirname(path)}. ${hint}`);
    }
  };
  // The three ways a channel is registered (cleanup item 12.2): each checks
  // once that the sender is one of the app's windows (a window that is not
  // is answered the channel's `refused`, or not at all), and is typed by
  // app-bridge.ts's table.
  const handle = <C extends ChannelOf<'invoke'>>(
    channel: C,
    refused: NoInfer<ChannelResult<C>>,
    fn: NoInfer<
      (
        e: IpcMainInvokeEvent,
        win: BrowserWindow,
        ...args: Unchecked<ChannelArgs<C>>
      ) => ChannelResult<C> | Promise<ChannelResult<C>>
    >,
  ): void => {
    ipcMain.handle(channel, (e, ...args: unknown[]) => {
      const win = h.window(e.sender);
      return win === null ? refused : fn(e, win, ...(args as Unchecked<ChannelArgs<C>>));
    });
  };
  const on = <C extends ChannelOf<'send'>>(
    channel: C,
    fn: (e: IpcMainEvent, win: BrowserWindow, ...args: Unchecked<ChannelArgs<C>>) => void,
  ): void => {
    ipcMain.on(channel, (e, ...args: unknown[]) => {
      const win = h.window(e.sender);
      if (win !== null) {
        fn(e, win, ...(args as Unchecked<ChannelArgs<C>>));
      }
    });
  };
  const sync = <C extends ChannelOf<'sync'>>(
    channel: C,
    refused: ChannelResult<C>,
    fn: (e: IpcMainEvent, win: BrowserWindow) => ChannelResult<C>,
  ): void => {
    ipcMain.on(channel, (e) => {
      const win = h.window(e.sender);
      e.returnValue = win === null ? refused : fn(e, win);
    });
  };
  // where a dialog starts: the folder a dialog gave last, kept across runs
  // (the store), unless it is gone since; else the documents folder
  const lastDir = (): string | null => {
    const d = h.store.lastDir;
    return d !== undefined && existsSync(d) ? d : null;
  };

  // the engine's bridge: a window's calls go to its engine (main/index.ts serves it)
  on('bt:call', (e, _win, request) => {
    h.engineCall(e.sender, request);
  });

  // the update check at startup runs once a run, whichever window asks first
  let autoChecked = false;
  handle('bt:check-updates', null, async (_e, _win, auto) => {
    if (typeof auto !== 'boolean') {
      return null;
    }
    if (auto) {
      if (!h.packaged || autoChecked) {
        return null;
      }
      autoChecked = true;
    }
    const r = await checkForUpdate(h.version, h.fetch, (text) => {
      h.updateChecked(text);
    });
    if (auto && (r.status !== 'newer' || r.latest === h.store.skipVersion)) {
      return null;
    }
    return r;
  });

  on('bt:skip-update', (_e, _win, version) => {
    if (typeof version === 'string' && parseVersion(version) !== null) {
      h.store.setSkipVersion(version);
    }
  });

  on('bt:open-releases', () => {
    void shell.openExternal(SYSTEM_URLS.releases);
  });

  sync('bt:app-info', null, () => ({ platform: process.platform, version: h.version }));

  handle('bt:choose-open', null, async (e, win, kind) => {
    if (!isOpenKind(kind)) {
      return null;
    }
    const k = KINDS[kind];
    const r = await dialog.showOpenDialog(win, {
      title: OPEN_TITLES[kind],
      properties: ['openFile'],
      filters: [k.filter, { name: 'All Files', extensions: ['*'] }],
      defaultPath: lastDir() ?? defaultDir(),
    });
    const path = r.canceled ? undefined : r.filePaths[0];
    if (path === undefined) {
      return null;
    }
    h.store.setLastDir(dirname(path));
    h.paths.grant(e.sender, path);
    return path;
  });

  handle('bt:choose-save', null, async (e, win, kind, name) => {
    if (!isFileKind(kind) || typeof name !== 'string') {
      return null;
    }
    const k = KINDS[kind];
    const proposed = basename(name) === '' ? `puzzle${k.ext}` : basename(name);
    const r = await dialog.showSaveDialog(win, {
      title: k.title,
      defaultPath: join(lastDir() ?? defaultDir(), proposed),
      filters: [k.filter],
      properties: ['showOverwriteConfirmation', 'createDirectory'],
    });
    if (r.canceled || r.filePath === '') {
      return null;
    }
    let path = r.filePath;
    if (extname(path).toLowerCase() !== k.ext) {
      path += k.ext;
      // the dialog asked about the name chosen, not this one
      if (existsSync(path)) {
        const { response } = await dialog.showMessageBox(win, {
          type: 'warning',
          buttons: ['Cancel', 'Overwrite'],
          defaultId: 0,
          cancelId: 0,
          message: `${basename(path)} exists. Overwrite it?`,
        });
        if (response !== 1) {
          return null;
        }
      }
    }
    h.store.setLastDir(dirname(path));
    if (isExportKind(kind)) {
      grants.grant(e.sender, path, kind);
    } else {
      h.paths.grant(e.sender, path);
    }
    return path;
  });

  // an export's bytes: ok, or the reason (an invoke's own rejection would carry Electron's wording)
  const nothingToWrite: WriteResult = { ok: false, error: 'nothing to write' };
  handle('bt:write-export', nothingToWrite, async (e, _win, path, data) => {
    if (typeof path !== 'string' || !(data instanceof Uint8Array)) {
      return nothingToWrite;
    }
    if (grants.take(e.sender, path) === null) {
      return { ok: false, error: `${basename(path)} was not chosen in the export's dialog` };
    }
    try {
      await writeFileSafely(path, data);
      return { ok: true };
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      logRefusedWrite(path, message);
      return { ok: false, error: message };
    }
  });

  // a solution's STL files (deferred part 5) and several shapes' (File ▸
  // Export ▸ STL): the folder dialog, then files of the engine's names in
  // the folder it gave; its button says Save when the files are written
  // straight away
  handle('bt:choose-folder', null, async (e, win, save) => {
    const r = await dialog.showOpenDialog(win, {
      title: save === true ? 'Export STL Files to a Folder' : 'Choose the Output Folder',
      ...(save === true ? { buttonLabel: 'Save' } : {}),
      properties: ['openDirectory', 'createDirectory'],
      defaultPath: lastDir() ?? defaultDir(),
    });
    const folder = r.canceled ? undefined : r.filePaths[0];
    if (folder === undefined) {
      return null;
    }
    h.store.setLastDir(folder);
    folders.grant(e.sender, folder);
    return folder;
  });

  handle('bt:existing-exports', 0, (e, _win, folder, names) => {
    if (typeof folder !== 'string' || !folders.has(e.sender, folder) || !Array.isArray(names)) {
      return 0;
    }
    return names.filter(
      (n): n is string =>
        typeof n === 'string' && isPieceFileName(n) && existsSync(join(folder, n)),
    ).length;
  });

  handle('bt:write-folder-export', nothingToWrite, async (e, _win, folder, name, data) => {
    if (typeof folder !== 'string' || typeof name !== 'string' || !(data instanceof Uint8Array)) {
      return nothingToWrite;
    }
    if (!folders.has(e.sender, folder)) {
      return { ok: false, error: "the folder was not chosen in the export's dialog" };
    }
    if (!isPieceFileName(name)) {
      return { ok: false, error: `${name} is not a piece's file name` };
    }
    try {
      await writeFileSafely(join(folder, name), data);
      return { ok: true };
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      logRefusedWrite(join(folder, name), message);
      return { ok: false, error: message };
    }
  });

  handle('bt:load-state', null, () => {
    const s: StoredState = {};
    if (h.store.prefs !== undefined) {
      s.prefs = h.store.prefs;
    }
    if (h.store.layout !== undefined) {
      s.layout = h.store.layout;
    }
    return s;
  });

  on('bt:save-state', (e, _win, key, value) => {
    if (
      (key === 'prefs' || key === 'layout') &&
      typeof value === 'object' &&
      value !== null &&
      !Array.isArray(value)
    ) {
      const v = value as Record<string, unknown>;
      h.store.set(key, v);
      if (key === 'prefs') {
        h.prefsChanged(v);
        // the settings are the app's: every window shows them (a layout is the window's own)
        for (const c of h.others(e.sender)) {
          post(c, 'bt:prefs', v);
        }
      }
    }
  });

  // the window opened this file: it holds it, owned or read-only (path-owners.ts)
  on('bt:claim-path', (e, _win, path) => {
    if (typeof path === 'string' && h.paths.allows(e.sender, path)) {
      h.owners.claim(e.sender.id, path);
    }
  });

  on('bt:new-window', () => {
    h.newWindow();
  });

  // only a file the window was handed: Open Recent hands it back (path-grants.ts)
  on('bt:recent-add', (e, _win, path) => {
    if (typeof path === 'string' && path !== '' && h.paths.allows(e.sender, path)) {
      h.store.addRecent(path);
      h.store.setLastDir(dirname(path));
    }
  });

  on('bt:recent-remove', (_e, _win, path) => {
    if (typeof path === 'string' && path !== '') {
      h.store.removeRecent(path);
    }
  });

  handle('bt:rescue-write', null, async (e, _win, info) => {
    if (!isRescueInfo(info)) {
      return null;
    }
    const engine = h.engine(e.sender);
    if (engine === null) {
      return { ok: false, errc: 'disconnected', message: 'the window has no engine' };
    }
    // the record's file is handed to the window that reads it: only
    // one this window was handed itself, else the copy has no file
    const path = info.path !== null && h.paths.allows(e.sender, info.path) ? info.path : null;
    const store = h.rescue.of(e.sender.id);
    try {
      // the engine writes the copy itself (cleanup item 10.11). The call
      // is made before this handler first awaits, so it goes ahead of
      // the window's later calls: the copy is of the document at the
      // revision the window saw, or refused (another one is open)
      await store.write(
        (file) =>
          engine
            .call('document.saveRescue', { path: file, revision: info.revision })
            .then((r) => r.revision),
        { ...info, path },
      );
      return { ok: true };
    } catch (err) {
      if (isEngineError(err)) {
        if (err.errc === 'io_error') {
          logRefusedWrite(store.file, err.detail);
        }
        return { ok: false, errc: err.errc, message: err.detail };
      }
      const message = err instanceof Error ? err.message : String(err);
      logRefusedWrite(store.record, message);
      return { ok: false, errc: 'io_error', message };
    }
  });

  handle('bt:rescue-read', null, async (e) => {
    const copy = await h.rescue.of(e.sender.id).read();
    if (copy !== null) {
      // the engine opens the copy itself, as the document of its file
      h.paths.grant(e.sender, copy.file);
      if (copy.info.path !== null) {
        h.paths.grant(e.sender, copy.info.path);
      }
    }
    return copy;
  });

  handle('bt:rescue-clear', null, async (e) => {
    await h.rescue.of(e.sender.id).clear();
    return null;
  });

  // the path of a file dropped on the window (the preload's pathOf: only a
  // file the user dropped has one)
  on('bt:dropped', (e, _win, path) => {
    if (typeof path === 'string' && path !== '') {
      h.paths.grant(e.sender, path);
    }
  });

  // the Play tab's goal picture in its own window (goal-window.ts)
  const goals = new GoalWindows(h.background);
  on('bt:goal-window', (_e, win, picture, raise) => {
    if (isGoalPicture(picture)) {
      goals.show(win, picture, raise === true);
    }
  });

  on('bt:help', (_e, _win, link) => {
    if (isHelpLink(link)) {
      openHelp(link);
    }
  });

  // the command line tool: the renderer names an operation, never a path
  const notOurs = { ok: false, error: 'not a ShapeLogic window' } as const;
  handle('bt:cli-info', notOurs, () => ({ ok: true, info: cliInfo(h.cliPath, h.cliLink) }));
  handle('bt:cli-install', notOurs, () => installCli(h.cliPath, h.cliLink, macPrivileged));
  handle('bt:cli-uninstall', notOurs, () => uninstallCli(h.cliPath, h.cliLink, macPrivileged));

  // the cache folder: the renderer names an operation, never a path
  handle('bt:cache-folder', null, () => h.cache.info());
  handle('bt:cache-choose', null, async (e, win, useDefault) => {
    if (typeof useDefault !== 'boolean') {
      return null;
    }
    if (useDefault) {
      return h.cache.choose(e.sender, undefined);
    }
    const r = await dialog.showOpenDialog(win, {
      title: 'Choose the Cache Folder',
      buttonLabel: 'Use This Folder',
      message: 'ShapeLogic makes a ShapeLogic folder for its cache data in the folder you choose.',
      properties: ['openDirectory', 'createDirectory'],
      defaultPath: h.cache.chosen ?? app.getPath('home'),
    });
    const folder = r.canceled ? undefined : r.filePaths[0];
    if (folder === undefined) {
      return { status: 'canceled' };
    }
    return h.cache.choose(e.sender, folder);
  });
  handle('bt:cache-finish', null, (e, _win, action) =>
    isCacheAction(action) ? h.cache.finish(e.sender, action) : null,
  );

  // the cache manager: the renderer names an item by its id, never a path
  let startListed = false;
  handle('bt:cache-items', null, async () => {
    await h.cache.start();
    return listItems(h.cache.folders, h.rescue.all(), h.cacheLinks);
  });
  handle('bt:cache-items-start', null, async () => {
    await h.cache.start();
    // the notice at the start: once a run, to the first window that asks
    if (startListed) {
      return [];
    }
    startListed = true;
    return listItems(h.cache.folders, h.rescue.all(), h.cacheLinks);
  });
  handle('bt:cache-delete', null, async (_e, _win, id) => {
    await h.cache.start();
    return typeof id === 'string'
      ? deleteItem(id, h.cache.folders, h.rescue.all(), h.cacheLinks, () => h.cache.solving())
      : { ok: false, error: 'unknown item' };
  });

  // the log manager: a page, Clear, its close, and Export to the file this process's
  // own save dialog gives (the renderer never names a path)
  handle('bt:log-page', null, (_e, _win, query) => (isLogQuery(query) ? h.log.page(query) : null));
  handle('bt:log-clear', null, async () => {
    await h.log.clear();
    return null;
  });
  handle('bt:log-closed', null, () => {
    // the manager closed: the lines it read go (cleanup item 10.9)
    h.log.forget();
    return null;
  });
  handle('bt:log-sizes', null, () => h.log.exportSizes());
  handle('bt:log-export', null, async (_e, win, lines) => {
    // all of the log, or its newest lines
    if (
      lines !== undefined &&
      (typeof lines !== 'number' || !Number.isInteger(lines) || lines < 1 || lines > 1_000_000)
    ) {
      return null;
    }
    const r = await dialog.showSaveDialog(win, {
      title: 'Export Log',
      defaultPath: join(lastDir() ?? defaultDir(), `shapelogic-log-${logStamp(new Date())}.txt`),
      filters: [
        { name: 'Text Files', extensions: ['txt', 'log'] },
        { name: 'All Files', extensions: ['*'] },
      ],
      properties: ['showOverwriteConfirmation', 'createDirectory'],
    });
    if (r.canceled || r.filePath === '') {
      return { ok: false, canceled: true } satisfies LogExportResult;
    }
    const path = extname(r.filePath) === '' ? `${r.filePath}.txt` : r.filePath;
    try {
      await h.log.exportTo(path, lines);
      h.store.setLastDir(dirname(path));
      return { ok: true, path } satisfies LogExportResult;
    } catch (err) {
      return {
        ok: false,
        error: err instanceof Error ? err.message : String(err),
      } satisfies LogExportResult;
    }
  });

  on('bt:cache-link', (e, _win, link) => {
    if (!isCacheLink(link)) {
      return;
    }
    // the puzzle's file only when the window was handed it, as the rescue record's
    const path = link.path !== null && h.paths.allows(e.sender, link.path) ? link.path : null;
    void h.cache.start().then(() => h.cacheLinks.record({ ...link, path }, h.cache.folders));
  });

  on('bt:menu-state', (e, _win, state) => {
    if (isMenuState(state)) {
      h.setMenu(e.sender, state);
    }
  });

  on('bt:close-taken', (e) => {
    h.closeTaken(e.sender);
  });
  on('bt:close-window', (e) => {
    h.closeWindow(e.sender);
  });
  on('bt:close-canceled', (e) => {
    h.cancelQuit(e.sender);
  });

  on('bt:engine-ready', (e) => {
    h.ready(e.sender);
  });
  on('bt:engine-retry', (e) => {
    h.retryEngine(e.sender);
  });
  on('bt:command-held', (e, _win, command, dropped) => {
    if (isAppCommandName(command) && typeof dropped === 'boolean') {
      h.commandHeld(e.sender, command, dropped);
    }
  });

  handle('bt:diagnostics-copy', null, (e) => h.copyDiagnostics(e.sender));
  handle('bt:diagnostics-save', null, (e) => h.saveDiagnostics(e.sender));
}

function defaultDir(): string {
  try {
    return app.getPath('documents');
  } catch {
    return '';
  }
}
