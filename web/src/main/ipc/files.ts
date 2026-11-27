// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's dialogs and exports (a group of ../app-ipc.ts, cleanup item
// 13.4): the open and save dialogs (a name without the kind's extension
// gets it, and a file that then exists is asked about, as the original's
// Save As did); the exports' files, written here (export-file.ts) and only
// to a path this process's save dialog gave the same window for that kind,
// once, and from deferred part 5 into the folder its folder dialog gave
// last, by the names the engine gives a solution's pieces. From stage 8
// the files a window's engine may be given (path-grants.ts) are granted
// here: what the open and save dialogs gave for a puzzle or a paused
// solve, and a file dropped on the window.

import { dialog } from 'electron';
import type { WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';

import { isExportKind, isFileKind, isOpenKind } from '../../engine/app-bridge.ts';
import type { FileKind, OpenKind, WriteResult } from '../../engine/app-bridge.ts';
import type { AppLog } from '../app-log.ts';
import { ExportGrants, FolderGrants, isPieceFileName, writeFileSafely } from '../export-file.ts';
import type { PathGrants } from '../path-grants.ts';
import type { StateStore } from '../state-store.ts';
import { logRefusedWrite, startDir } from './register.ts';
import type { Registrar } from './register.ts';

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

/** The paths the save dialog gave each window's renderer for an export: `sl:write-export` writes only these. */
const grants = new ExportGrants<WebContents>((kind) => KINDS[kind].ext);
/** The folder the folder dialog gave each window's renderer last: `sl:write-folder-export` writes only there. */
const folders = new FolderGrants<WebContents>();

/** What the dialogs and exports need of the main process. */
export interface FilesIpc {
  /** Where the dialogs start, and the folder they gave last. */
  store: Pick<StateStore, 'lastDir' | 'setLastDir'>;
  /** The files each window's engine may be given. */
  paths: PathGrants<WebContents>;
  /** The app's log: a write Windows refused. */
  log: Pick<AppLog, 'warn'>;
}

/** Registers the dialogs' and the exports' channels. */
export function installFilesIpc(r: Registrar, h: FilesIpc): void {
  r.handle('sl:choose-open', null, async (e, win, kind) => {
    if (!isOpenKind(kind)) {
      return null;
    }
    const k = KINDS[kind];
    const res = await dialog.showOpenDialog(win, {
      title: OPEN_TITLES[kind],
      properties: ['openFile'],
      filters: [k.filter, { name: 'All Files', extensions: ['*'] }],
      defaultPath: startDir(h.store),
    });
    const path = res.canceled ? undefined : res.filePaths[0];
    if (path === undefined) {
      return null;
    }
    h.store.setLastDir(dirname(path));
    h.paths.grant(e.sender, path);
    return path;
  });

  r.handle('sl:choose-save', null, async (e, win, kind, name) => {
    if (!isFileKind(kind) || typeof name !== 'string') {
      return null;
    }
    const k = KINDS[kind];
    const proposed = basename(name) === '' ? `puzzle${k.ext}` : basename(name);
    const res = await dialog.showSaveDialog(win, {
      title: k.title,
      defaultPath: join(startDir(h.store), proposed),
      filters: [k.filter],
      properties: ['showOverwriteConfirmation', 'createDirectory'],
    });
    if (res.canceled || res.filePath === '') {
      return null;
    }
    let path = res.filePath;
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
  r.handle('sl:write-export', nothingToWrite, async (e, _win, path, data) => {
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
      logRefusedWrite(h.log, path, message);
      return { ok: false, error: message };
    }
  });

  // a solution's STL files (deferred part 5) and several shapes' (File ▸
  // Export ▸ STL): the folder dialog, then files of the engine's names in
  // the folder it gave; its button says Save when the files are written
  // straight away
  r.handle('sl:choose-folder', null, async (e, win, save) => {
    const res = await dialog.showOpenDialog(win, {
      title: save === true ? 'Export STL Files to a Folder' : 'Choose the Output Folder',
      ...(save === true ? { buttonLabel: 'Save' } : {}),
      properties: ['openDirectory', 'createDirectory'],
      defaultPath: startDir(h.store),
    });
    const folder = res.canceled ? undefined : res.filePaths[0];
    if (folder === undefined) {
      return null;
    }
    h.store.setLastDir(folder);
    folders.grant(e.sender, folder);
    return folder;
  });

  r.handle('sl:existing-exports', 0, (e, _win, folder, names) => {
    if (typeof folder !== 'string' || !folders.has(e.sender, folder) || !Array.isArray(names)) {
      return 0;
    }
    return names.filter(
      (n): n is string =>
        typeof n === 'string' && isPieceFileName(n) && existsSync(join(folder, n)),
    ).length;
  });

  r.handle('sl:write-folder-export', nothingToWrite, async (e, _win, folder, name, data) => {
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
      logRefusedWrite(h.log, join(folder, name), message);
      return { ok: false, error: message };
    }
  });

  // the path of a file dropped on the window (the preload's pathOf: only a
  // file the user dropped has one)
  r.on('sl:dropped', (e, _win, path) => {
    if (typeof path === 'string' && path !== '') {
      h.paths.grant(e.sender, path);
    }
  });
}
