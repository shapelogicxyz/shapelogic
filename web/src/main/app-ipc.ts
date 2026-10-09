// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's side of window.btApp (engine/app-bridge.ts, stage 6):
// the file dialogs (a name without the kind's extension gets it, and a
// file that then exists is asked about, as the original's Save As did),
// the store of settings, layouts and recent files, the rescue copy, the
// help links (the addresses are the main process's: the renderer names a
// link, never a URL), the menus' state and the window's close. Every
// message is checked before it is used, and only the app's windows are
// answered.

import { app, dialog, ipcMain, shell } from 'electron';
import type { BrowserWindow, IpcMainEvent, IpcMainInvokeEvent, WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';

import { isMenuState, isRescueInfo } from '../engine/app-bridge.ts';
import type { FileKind, HelpLink, MenuState, StoredState } from '../engine/app-bridge.ts';
import type { RescueStore } from './rescue.ts';
import type { StateStore } from './state-store.ts';

/** The original's user guide (platform.cpp) and its first chapter, "Getting Started". */
export const HELP_URLS: Readonly<Record<HelpLink, string>> = {
  guide: 'https://burrtools.sourceforge.net/gui-doc/toc.html',
  tutorial: 'https://burrtools.sourceforge.net/gui-doc/GettingStarted.html',
};

/** Opens a help page in the system's browser. */
export function openHelp(link: HelpLink): void {
  void shell.openExternal(HELP_URLS[link]);
}

const KINDS: Readonly<
  Record<FileKind, { ext: string; filter: { name: string; extensions: string[] }; title: string }>
> = {
  puzzle: {
    ext: '.xmpuzzle',
    filter: { name: 'Puzzle Files', extensions: ['xmpuzzle'] },
    title: 'Puzzle',
  },
  paused: {
    ext: '.btsolve',
    filter: { name: 'Paused Solver State', extensions: ['btsolve'] },
    title: 'Paused Solver State',
  },
};

const isKind = (v: unknown): v is FileKind => v === 'puzzle' || v === 'paused';

export interface AppIpcHost {
  store: StateStore;
  rescue: RescueStore;
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
    if (win === null || !isKind(kind)) {
      return null;
    }
    const k = KINDS[kind];
    const r = await dialog.showOpenDialog(win, {
      title: `Open ${k.title}`,
      properties: ['openFile'],
      filters: [k.filter, { name: 'All Files', extensions: ['*'] }],
      ...(lastDir === null ? {} : { defaultPath: lastDir }),
    });
    const path = r.canceled ? undefined : r.filePaths[0];
    if (path === undefined) {
      return null;
    }
    lastDir = dirname(path);
    return path;
  });

  ipcMain.handle('bt:choose-save', async (e, kind: unknown, name: unknown) => {
    const win = ours(e);
    if (win === null || !isKind(kind) || typeof name !== 'string') {
      return null;
    }
    const k = KINDS[kind];
    const proposed = basename(name) === '' ? `puzzle${k.ext}` : basename(name);
    const r = await dialog.showSaveDialog(win, {
      title: `Save ${k.title} As`,
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
    return path;
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
      h.store.addRecent(path);
      lastDir = dirname(path);
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
          h.rescue.write(data, info);
        }
        return null;
      case 'read':
        return h.rescue.read();
      case 'clear':
        h.rescue.clear();
        return null;
      default:
        return null;
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
