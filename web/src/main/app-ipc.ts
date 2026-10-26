// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's side of window.btApp (engine/app-bridge.ts, stage 6):
// the file dialogs (a name without the kind's extension gets it, and a
// file that then exists is asked about, as the original's Save As did),
// the store of settings, layouts and recent files, the rescue copy, the
// command line tool (cli-tool.ts: the renderer names an operation, never a
// path), the help links (the addresses are the main process's: the renderer
// names a link, never a URL), the menus' state and the window's close; from stage
// 7 the exports' files, written here (export-file.ts) and only to a path
// this process's save dialog gave the same window for that kind, once,
// and from deferred part 5 into the folder its folder dialog gave last,
// by the names the engine gives a solution's pieces.
// From stage 8 the files a window's engine may be given (path-grants.ts)
// are granted here: what the open and save dialogs gave for a puzzle or a
// paused solve, the rescue record's file, a file dropped on the window.
// Every message is checked before it is used, and only the app's windows
// are answered.

import { app, dialog, ipcMain, shell } from 'electron';
import type { BrowserWindow, IpcMainEvent, IpcMainInvokeEvent, WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';

import {
  isExportKind,
  isFileKind,
  isMenuState,
  isOpenKind,
  isRescueInfo,
} from '../engine/app-bridge.ts';
import type {
  CliToolResult,
  FileKind,
  HelpLink,
  MenuState,
  OpenKind,
  StoredState,
} from '../engine/app-bridge.ts';
import { ExportGrants, FolderGrants, isPieceFileName, writeFileSafely } from './export-file.ts';
import { cliInfo, installCli, macPrivileged, uninstallCli } from './cli-tool.ts';
import type { PathGrants } from './path-grants.ts';
import type { RescueStore } from './rescue.ts';
import { HELP_URLS } from './security.ts';
import type { StateStore } from './state-store.ts';

/** Opens a help page in the system's browser. */
export function openHelp(link: HelpLink): void {
  void shell.openExternal(HELP_URLS[link]);
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
  rescue: RescueStore;
  /** The files each window's engine may be given. */
  paths: PathGrants<WebContents>;
  version: string;
  /** The command line tool in this install (cli-tool.ts), or null when there is none. */
  cliPath: string | null;
  /** Where Install links it, or null where this platform has no Install. */
  cliLink: string | null;
  /** The app's window of a renderer, or null (not ours: not answered). */
  window(sender: WebContents): BrowserWindow | null;
  setMenu(sender: WebContents, state: MenuState): void;
  closeTaken(sender: WebContents): void;
  closeWindow(sender: WebContents): void;
  cancelQuit(sender: WebContents): void;
}

/** Installs the handlers. */
export function installAppIpc(h: AppIpcHost): void {
  const ours = (e: IpcMainEvent | IpcMainInvokeEvent): BrowserWindow | null => h.window(e.sender);
  // where a dialog starts: the folder a dialog gave last, kept across runs
  // (the store), unless it is gone since; else the documents folder
  const lastDir = (): string | null => {
    const d = h.store.lastDir;
    return d !== undefined && existsSync(d) ? d : null;
  };

  ipcMain.on('bt:app-info', (e) => {
    e.returnValue = ours(e) === null ? null : { platform: process.platform, version: h.version };
  });

  ipcMain.handle('bt:choose-open', async (e, kind: unknown) => {
    const win = ours(e);
    if (win === null || !isOpenKind(kind)) {
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

  ipcMain.handle('bt:choose-save', async (e, kind: unknown, name: unknown) => {
    const win = ours(e);
    if (win === null || !isFileKind(kind) || typeof name !== 'string') {
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

  // an export's bytes: { ok } or { error } (an invoke's own rejection would carry Electron's wording)
  ipcMain.handle('bt:write-export', async (e, path: unknown, data: unknown) => {
    if (ours(e) === null || typeof path !== 'string' || !(data instanceof Uint8Array)) {
      return { error: 'nothing to write' };
    }
    if (grants.take(e.sender, path) === null) {
      return { error: `${basename(path)} was not chosen in the export's dialog` };
    }
    try {
      await writeFileSafely(path, data);
      return { ok: true };
    } catch (err) {
      return { error: err instanceof Error ? err.message : String(err) };
    }
  });

  // a solution's STL files (deferred part 5): the folder dialog, then
  // files of the engine's names in the folder it gave
  ipcMain.handle('bt:choose-folder', async (e) => {
    const win = ours(e);
    if (win === null) {
      return null;
    }
    const r = await dialog.showOpenDialog(win, {
      title: 'Choose the Output Folder',
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

  ipcMain.handle('bt:existing-exports', (e, folder: unknown, names: unknown) => {
    if (
      ours(e) === null ||
      typeof folder !== 'string' ||
      !folders.has(e.sender, folder) ||
      !Array.isArray(names)
    ) {
      return 0;
    }
    return names.filter(
      (n): n is string =>
        typeof n === 'string' && isPieceFileName(n) && existsSync(join(folder, n)),
    ).length;
  });

  ipcMain.handle(
    'bt:write-folder-export',
    async (e, folder: unknown, name: unknown, data: unknown) => {
      if (
        ours(e) === null ||
        typeof folder !== 'string' ||
        typeof name !== 'string' ||
        !(data instanceof Uint8Array)
      ) {
        return { error: 'nothing to write' };
      }
      if (!folders.has(e.sender, folder)) {
        return { error: "the folder was not chosen in the export's dialog" };
      }
      if (!isPieceFileName(name)) {
        return { error: `${name} is not a piece's file name` };
      }
      try {
        await writeFileSafely(join(folder, name), data);
        return { ok: true };
      } catch (err) {
        return { error: err instanceof Error ? err.message : String(err) };
      }
    },
  );

  ipcMain.handle('bt:load-state', (e): StoredState | null => {
    if (ours(e) === null) {
      return null;
    }
    const s: StoredState = {};
    if (h.store.prefs !== undefined) {
      s.prefs = h.store.prefs;
    }
    if (h.store.layout !== undefined) {
      s.layout = h.store.layout;
    }
    return s;
  });

  ipcMain.on('bt:save-state', (e, key: unknown, value: unknown) => {
    if (
      ours(e) !== null &&
      (key === 'prefs' || key === 'layout') &&
      typeof value === 'object' &&
      value !== null &&
      !Array.isArray(value)
    ) {
      h.store.set(key, value as Record<string, unknown>);
    }
  });

  ipcMain.on('bt:recent', (e, op: unknown, path: unknown) => {
    if (ours(e) === null || typeof path !== 'string' || path === '') {
      return;
    }
    if (op === 'add') {
      // only a file the window was handed: Open Recent hands it back (path-grants.ts)
      if (h.paths.allows(e.sender, path)) {
        h.store.addRecent(path);
        h.store.setLastDir(dirname(path));
      }
    } else if (op === 'remove') {
      h.store.removeRecent(path);
    }
  });

  ipcMain.handle('bt:rescue', (e, op: unknown, data: unknown, info: unknown) => {
    if (ours(e) === null) {
      return null;
    }
    switch (op) {
      case 'write':
        if (typeof data === 'string' && isRescueInfo(info)) {
          // the record's file is handed to the window that reads it: only
          // one this window was handed itself, else the copy has no file
          const path = info.path !== null && h.paths.allows(e.sender, info.path) ? info.path : null;
          h.rescue.write(data, { ...info, path });
        }
        return null;
      case 'read': {
        const copy = h.rescue.read();
        if (copy?.info.path != null) {
          // the copy reopens as the document of its file
          h.paths.grant(e.sender, copy.info.path);
        }
        return copy;
      }
      case 'clear':
        h.rescue.clear();
        return null;
      default:
        return null;
    }
  });

  // the path of a file dropped on the window (the preload's pathOf: only a
  // file the user dropped has one)
  ipcMain.on('bt:dropped', (e, path: unknown) => {
    if (ours(e) !== null && typeof path === 'string' && path !== '') {
      h.paths.grant(e.sender, path);
    }
  });

  ipcMain.on('bt:help', (e, link: unknown) => {
    if (ours(e) !== null && (link === 'tutorial' || link === 'guide')) {
      openHelp(link);
    }
  });

  // the command line tool: the renderer names an operation, never a path
  ipcMain.handle('bt:cli-tool', async (e, op: unknown): Promise<CliToolResult> => {
    if (ours(e) === null) {
      return { ok: false, error: 'not a ShapeLogic window' };
    }
    switch (op) {
      case 'info':
        return { ok: true, info: cliInfo(h.cliPath, h.cliLink) };
      case 'install':
        return installCli(h.cliPath, h.cliLink, macPrivileged);
      case 'uninstall':
        return uninstallCli(h.cliPath, h.cliLink, macPrivileged);
      default:
        return { ok: false, error: 'unknown operation' };
    }
  });

  ipcMain.on('bt:menu-state', (e, state: unknown) => {
    if (ours(e) !== null && isMenuState(state)) {
      h.setMenu(e.sender, state);
    }
  });

  ipcMain.on('bt:close', (e, op: unknown) => {
    if (ours(e) === null) {
      return;
    }
    if (op === 'taken') {
      h.closeTaken(e.sender);
    } else if (op === 'close') {
      h.closeWindow(e.sender);
    } else if (op === 'cancelled') {
      h.cancelQuit(e.sender);
    }
  });
}

function defaultDir(): string {
  try {
    return app.getPath('documents');
  } catch {
    return '';
  }
}
