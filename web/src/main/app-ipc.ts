// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's side of window.btApp (engine/app-bridge.ts, stage 6):
// the file dialogs (a name without the kind's extension gets it, and a
// file that then exists is asked about, as the original's Save As did),
// the store of settings, layouts and recent files, the rescue copy, the
// help links (the addresses are the main process's: the renderer names a
// link, never a URL), the menus' state and the window's close; from stage
// 7 the exports' files, written here (export-file.ts) and only to a path
// this process's save dialog gave the same window for that kind, once.
// From stage 8 the files a window's engine may be given (path-grants.ts)
// are granted here: what the open and save dialogs gave for a puzzle or a
// paused solve, the rescue record's file, a file dropped on the window.
// Every message is checked before it is used, and only the app's windows
// are answered.

import { app, dialog, ipcMain, shell } from 'electron';
import type { BrowserWindow, IpcMainEvent, IpcMainInvokeEvent, WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';

import { isExportKind, isFileKind, isMenuState, isRescueInfo } from '../engine/app-bridge.ts';
import type { FileKind, HelpLink, MenuState, StoredState } from '../engine/app-bridge.ts';
import { ExportGrants, writeFileSafely } from './export-file.ts';
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
    ext: '.btsolve',
    filter: { name: 'Paused Solver State', extensions: ['btsolve'] },
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
};

/** The kinds a file can be opened as (the exports are written only). */
const isOpenKind = (v: unknown): v is 'puzzle' | 'paused' => v === 'puzzle' || v === 'paused';

/** The paths the save dialog gave each window's renderer for an export: `bt:write-export` writes only these. */
const grants = new ExportGrants<WebContents>((kind) => KINDS[kind].ext);

export interface AppIpcHost {
  store: StateStore;
  rescue: RescueStore;
  /** The files each window's engine may be given. */
  paths: PathGrants<WebContents>;
  version: string;
  /** The app's window of a renderer, or null (not ours: not answered). */
  window(sender: WebContents): BrowserWindow | null;
  setMenu(sender: WebContents, state: MenuState): void;
  closeTaken(sender: WebContents): void;
  closeWindow(sender: WebContents): void;
  cancelQuit(sender: WebContents): void;
}

/** Where a save dialog starts: beside the document's file, else the documents folder. */
let lastDir: string | null = null;

/** Installs the handlers. */
export function installAppIpc(h: AppIpcHost): void {
  const ours = (e: IpcMainEvent | IpcMainInvokeEvent): BrowserWindow | null => h.window(e.sender);

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
      title: kind === 'puzzle' ? 'Open Puzzle' : 'Open Paused Solver State',
      properties: ['openFile'],
      filters: [k.filter, { name: 'All Files', extensions: ['*'] }],
      ...(lastDir === null ? {} : { defaultPath: lastDir }),
    });
    const path = r.canceled ? undefined : r.filePaths[0];
    if (path === undefined) {
      return null;
    }
    lastDir = dirname(path);
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
      defaultPath: join(lastDir ?? defaultDir(), proposed),
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
    lastDir = dirname(path);
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
        lastDir = dirname(path);
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
