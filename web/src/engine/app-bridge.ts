// SPDX-License-Identifier: GPL-3.0-or-later
//
// What crosses between Electron's main process and the renderer besides
// the engine (stage 6; the engine's half is bridge.ts): the menus'
// commands one way and their state the other, the file dialogs, the
// stored settings and layouts, the recent files, the rescue copy, the
// help links, the window's close, from stage 7 the exports' files, and
// the cache manager's items (View ▸ Manage Cache and Autosave Files).
// The preload exposes it as `window.btApp`; the renderer never sees a path
// it did not get from a dialog, a drop, the recent files or the rescue
// copy's record, and the main process writes an export only to a path its
// own save dialog gave for that kind. Plain types and checks, free of
// Electron and Node.

import type { MeshStyle } from './api.generated.ts';

/** A command of the menus (or the window: close), sent to the renderer. */
export type AppCommand =
  | { cmd: 'new' }
  | { cmd: 'open' }
  /** A file to open: Open Recent, a file the system opened with the app. */
  | { cmd: 'openPath'; path: string }
  | { cmd: 'save' }
  | { cmd: 'saveAs' }
  | { cmd: 'importPaused' }
  | { cmd: 'exportPaused' }
  /** File ▸ Import ▸ PuzzleSolver3D, Puzzlecad; File ▸ Export ▸ Puzzlecad (deferred part 6). */
  | { cmd: 'importPs3d' }
  | { cmd: 'importScad' }
  | { cmd: 'exportScad' }
  /** File ▸ Export ▸ Image, Vector Image, STL, Solution Animation (stage 7). */
  | { cmd: 'exportImage' }
  | { cmd: 'exportVector' }
  | { cmd: 'exportStl' }
  | { cmd: 'exportGlb' }
  /** The window is closing (its button, Ctrl+W, quit): ask about unsaved changes, then `closeWindow`. */
  | { cmd: 'close' }
  | { cmd: 'undo' }
  | { cmd: 'redo' }
  | { cmd: 'toShapes' }
  /** Edit ▸ Convert Brick Grid Type to Other (deferred part 6). */
  | { cmd: 'convertGrid' }
  | { cmd: 'notes' }
  | { cmd: 'settings' }
  | { cmd: 'statusWindow' }
  /** View ▸ Manage Cache and Autosave Files…: the dialog listing what solves, pauses, the journal and crashes left. */
  | { cmd: 'cacheFiles' }
  | { cmd: 'colourView'; on: boolean }
  | { cmd: 'style'; style: MeshStyle }
  | { cmd: 'focus3d' }
  /** View ▸ Color Theme: the Theme setting. */
  | { cmd: 'theme'; theme: 'Light' | 'Dark' | 'System' }
  | { cmd: 'keys' }
  /** ShapeLogic ▸ Command Line Tool… (Help on Windows and Linux): the dialog that explains and installs shapelogic_cli. */
  | { cmd: 'cliTool' }
  /** Help ▸ Check for Updates… (the application menu on macOS): ask GitHub now. */
  | { cmd: 'checkUpdates' }
  | { cmd: 'about' };

/** The names of the commands. */
export type AppCommandName = AppCommand['cmd'];

/** What the menus show; the renderer sends it when it changes. */
export interface MenuState {
  /** A document is open (the engine runs). */
  document: boolean;
  dirty: boolean;
  /** The document's file, or null. */
  path: string | null;
  /** The labels of the next undo and redo steps; null when there is none (or while solving). */
  undo: string | null;
  redo: string | null;
  /** A solve or an analysis is active (the lists are frozen). */
  solving: boolean;
  /** The problem the Solver shows has a paused solve (Export ▸ Paused solver state). */
  paused: boolean;
  /** The problem the Solver shows has saved solutions (Convert assemblies to pieces). */
  solutions: boolean;
  /** The puzzle has a shape with cells (Export ▸ STL). */
  shapes: boolean;
  /** That, on the brick or sliding grid, the only ones Export ▸ Puzzlecad writes. */
  scad: boolean;
  /** The Solver shows a solution with its moves stored (Export ▸ Solution Animation). */
  animation: boolean;
  colourView: boolean;
  style: MeshStyle;
  focus3d: boolean;
  theme: 'Light' | 'Dark' | 'System';
  /** The menus' keys as the one keyboard table has them, by command (shown, not registered). */
  keys: Partial<Record<AppCommandName, string>>;
}

/**
 * Where the command line tool (`shapelogic_cli`) is and whether it is
 * linked into the shell's path. The main process knows the paths; the
 * renderer only shows them.
 */
export interface CliToolInfo {
  /** The executable inside this install, or null when this build has none. */
  path: string | null;
  /** Where Install links it (`/usr/local/bin/shapelogic_cli`), or null when this platform has no Install yet. */
  link: string | null;
  /**
   * `none`: nothing at the link; `linked`: the link is this install's tool;
   * `stale`: a link to another copy of the tool (the app moved, or an older
   * one); `other`: a file or another link, which Install never replaces.
   */
  state: 'none' | 'linked' | 'stale' | 'other';
}

/** The answer to installing or removing the tool: the new state, or why not. */
export type CliToolResult =
  { ok: true; info: CliToolInfo } | { ok: false; error: string; cancelled?: boolean };

/**
 * The cache folder in use (main/cache-folder.ts): the engine's folders for
 * sliding and stacking searches, in a folder the user chose or in the
 * system's application data folder for ShapeLogic, the default.
 */
export interface CacheFolderInfo {
  /** The `ShapeLogic` folder in the folder the user chose, or null for the default. */
  custom: string | null;
  /** The default: the system's application data folder for ShapeLogic. */
  defaultFolder: string;
  /** Where sliding searches keep their files ('' when there is nowhere: they stay in memory). */
  slide: string;
  /** Where stacking searches save themselves. */
  panex: string;
  /** What the start could not do (a folder chosen earlier that could not be written: the default is back), told once. */
  notice?: string;
}

/**
 * What choosing a cache folder found: the dialog cancelled; a solve running
 * (or another window changing it); the folders in use; a folder that
 * cannot be written (the folders in use stay); or one made and written,
 * with what the folders in use hold ('' nothing), for `finishCacheFolder`.
 */
export type CacheChoice =
  | { status: 'cancelled' }
  | { status: 'busy'; reason: string }
  | { status: 'same' }
  | { status: 'invalid'; folder: string; error: string }
  | { status: 'ready'; folder: string; found: string };

/** What `finishCacheFolder` does with the data in the folders in use. */
export type CacheAction = 'move' | 'purge' | 'cancel';

/** Whether a value is a cache action. */
export function isCacheAction(v: unknown): v is CacheAction {
  return v === 'move' || v === 'purge' || v === 'cancel';
}

/** The answer of `finishCacheFolder`: the folders now in use, or why nothing changed. */
export type CacheResult = { ok: true; info: CacheFolderInfo } | { ok: false; error: string };

/**
 * What an item of the cache manager is: a sliding or stacking search's
 * folder (saved by a pause, or left unfinished by a crash), the rescue
 * copy (the unsaved changes and the crash recovery journal), or other
 * files in the search folders.
 */
export type CacheItemKind = 'sliding' | 'stacking' | 'rescue' | 'other';

/**
 * One item of View ▸ Manage Cache and Autosave Files (main/cache-files.ts):
 * one thing the user would name, which may be several files or folders
 * (the saved searches of a problem's starts, the rescue copy and its
 * record). The main process names it and lists its files; the renderer
 * deletes it by its id, never by a path.
 */
export interface CacheItem {
  /** Stable while the item's files are: what `deleteCacheItem` takes. */
  id: string;
  kind: CacheItemKind;
  /** Its name for people: "Paused sliding solve of Block 15.xmpuzzle". */
  title: string;
  /** What it is, what made it and what deleting it does, in a sentence or three. */
  about: string;
  /** The bytes of all its files. */
  bytes: number;
  /** The puzzle file it belongs to; null when it has none or it is not known. */
  puzzle: string | null;
  /** The puzzle's name when it has no file (a puzzle never saved); null when not known. */
  puzzleName: string | null;
  /** The problem it was solving; null when not known or not a solve's. */
  problem: string | null;
  /** When it was made and last written (ms since 1970). */
  created: number;
  modified: number;
  /** Every file it is, by its full path (at most a thousand). */
  files: string[];
  /** The files beyond those listed. */
  moreFiles: number;
}

/** What a window tells of the search folders its problem saved (solve.searchFolders). */
export interface CacheLink {
  /** Full paths, as solve.searchFolders gave them. */
  folders: string[];
  /** The document's file, or null for a puzzle without one. */
  path: string | null;
  /** The name the top bar shows. */
  name: string;
  /** The problem's name (its label when it has none). */
  problem: string;
}

/** Whether a value is a cache link (the main process checks what the renderer sends). */
export function isCacheLink(v: unknown): v is CacheLink {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const l = v as Record<string, unknown>;
  return (
    Array.isArray(l.folders) &&
    l.folders.every((f) => typeof f === 'string' && f !== '') &&
    (l.path === null || (typeof l.path === 'string' && l.path !== '')) &&
    typeof l.name === 'string' &&
    typeof l.problem === 'string'
  );
}

/** The answer to deleting a cache item: the items now there, or why not. */
export type CacheDeleteResult = { ok: true; items: CacheItem[] } | { ok: false; error: string };

/** The links of the Help menu (the main process holds the addresses). */
export type HelpLink = 'tutorial' | 'guide' | 'author';

/**
 * What the update check found (main/updates.ts): the latest release
 * (`v1.4`) newer than this version, with its notes (Markdown, '' when it
 * has none), the same or older, or no answer.
 */
export type UpdateResult =
  | { status: 'newer'; current: string; latest: string; notes: string }
  | { status: 'current'; current: string; latest: string }
  | { status: 'error'; current: string; error: string };

/** The kinds of file the dialogs ask for. */
export type FileKind = OpenKind | ExportKind;

/** The kinds of file the open dialog asks for (a Puzzlecad file is imported and exported). */
export type OpenKind = 'puzzle' | 'paused' | 'ps3d' | 'scad';

/** The kinds of file an export writes (through `writeExport`). */
export type ExportKind = 'image' | 'vector' | 'stl' | 'glb' | 'scad';

/** The kinds of file, for the checks. */
export const FILE_KINDS: readonly FileKind[] = [
  'puzzle',
  'paused',
  'ps3d',
  'image',
  'vector',
  'stl',
  'glb',
  'scad',
];

/** Whether a value is a kind of file. */
export function isFileKind(v: unknown): v is FileKind {
  return typeof v === 'string' && (FILE_KINDS as readonly string[]).includes(v);
}

/** Whether a kind of file is an export's. */
export function isExportKind(v: unknown): v is ExportKind {
  return v === 'image' || v === 'vector' || v === 'stl' || v === 'glb' || v === 'scad';
}

/** Whether a kind of file is one the open dialog asks for. */
export function isOpenKind(v: unknown): v is OpenKind {
  return v === 'puzzle' || v === 'paused' || v === 'ps3d' || v === 'scad';
}

/** A record of the rescue copy: what it stands for. */
export interface RescueInfo {
  /** The document's file, or null for a puzzle without one. */
  path: string | null;
  /** The name the top bar showed. */
  name: string;
  /** The engine's revision it was taken at. */
  revision: number;
  /** When it was written (ms since 1970). */
  time: number;
}

/** What the renderer stores in the main process's store, by key. */
export interface StoredState {
  prefs?: Record<string, unknown>;
  layout?: Record<string, unknown>;
}

/** What the preload exposes as `window.btApp`. */
export interface BtApp {
  /** The platform (`process.platform`): the menus' keys differ on macOS. */
  readonly platform: string;
  /** The app's version. */
  readonly version: string;
  /** Listens to the menus' commands; returns the unsubscribe. */
  onCommand(handler: (command: AppCommand) => void): () => void;
  /** The menus show this. */
  setMenuState(state: MenuState): void;
  /** The open dialog; null when cancelled. */
  chooseOpen(kind: OpenKind): Promise<string | null>;
  /** The save dialog with a name proposed; null when cancelled. */
  chooseSave(kind: FileKind, name: string): Promise<string | null>;
  /**
   * Writes an export's bytes to a path the save dialog gave for an export
   * kind (once); rejects with the reason when it could not (a read-only
   * file, a full disk, a path no dialog gave).
   */
  writeExport(path: string, data: Uint8Array): Promise<void>;
  /**
   * The folder dialog (a solution's STL files, several shapes'); null when
   * cancelled. `save`: the files are written as soon as it is chosen, and
   * its button says Save rather than Open.
   */
  chooseFolder(save?: boolean): Promise<string | null>;
  /** How many of these files the folder the folder dialog gave has already. */
  existingExports(folder: string, names: readonly string[]): Promise<number>;
  /**
   * Writes a solution piece's STL file into the folder the folder dialog
   * gave last, by a name as the engine's export.solutionPieces names it;
   * rejects with the reason when it could not.
   */
  writeFolderExport(folder: string, name: string, data: Uint8Array): Promise<void>;
  /** The path of a file dropped on the window (Electron's webUtils), or '' when it has none. */
  pathOf(file: File): string;
  /** The stored settings and layouts, as the last session left them. */
  loadState(): Promise<StoredState>;
  /** Stores one key (written to disk soon after). */
  saveState(key: keyof StoredState, value: Record<string, unknown>): void;
  /** A file was opened or saved: first in Open Recent. */
  addRecent(path: string): void;
  /** A recent file that could not be opened: out of the list. */
  removeRecent(path: string): void;
  /** Writes the rescue copy (the file's bytes, base64) with its record. */
  writeRescue(data: string, info: RescueInfo): Promise<void>;
  /** The rescue copy, or null when there is none. */
  readRescue(): Promise<{ data: string; info: RescueInfo } | null>;
  /** Removes the rescue copy (saved, or its changes given up). */
  clearRescue(): Promise<void>;
  /** Opens a help page in the system's browser. */
  openHelp(link: HelpLink): void;
  /**
   * Asks GitHub for the latest release. `auto` (the check at startup)
   * resolves null unless there is a newer release to show: not when the app
   * is not packaged, already checked this run, or that version was skipped.
   */
  checkForUpdates(auto: boolean): Promise<UpdateResult | null>;
  /** Skip This Version: the check at startup no longer shows this one. */
  skipUpdate(version: string): void;
  /** Opens the latest release's page (its downloads) in the system's browser. */
  openReleases(): void;
  /** Where the command line tool is, and whether it is linked into the path. */
  cliToolInfo(): Promise<CliToolInfo>;
  /** Links the tool into the path (the system asks for the administrator's password where it must). */
  installCliTool(): Promise<CliToolResult>;
  /** Removes the link Install made. */
  uninstallCliTool(): Promise<CliToolResult>;
  /** The cache folder in use; null where there is none to choose (the demo). */
  cacheFolder(): Promise<CacheFolderInfo | null>;
  /**
   * The folder dialog for a new cache folder (`useDefault`: no dialog, the
   * default folder): its `ShapeLogic` folder is made and written at once.
   * A `ready` choice waits for `finishCacheFolder`.
   */
  chooseCacheFolder(useDefault: boolean): Promise<CacheChoice>;
  /**
   * Takes the ready choice: moves the data of the folders in use into it or
   * deletes it, then uses it; or, cancelled, keeps the folders in use.
   */
  finishCacheFolder(action: CacheAction): Promise<CacheResult>;
  /** What the cache folder and the rescue copy hold, newest first; null where there is none (the demo). */
  cacheItems(): Promise<CacheItem[] | null>;
  /** Deletes a cache item's files (refused while a solve runs, for a search's). */
  deleteCacheItem(id: string): Promise<CacheDeleteResult>;
  /** These search folders belong to this puzzle and problem: the cache manager names them so. */
  linkCacheFolders(link: CacheLink): void;
  /** Closes the window: the renderer answered a `close` command (nothing to save, or saved or given up). */
  closeWindow(): void;
  /** The `close` command was taken: the renderer asks (a close not taken soon is asked natively). */
  closeTaken(): void;
  /** The question about unsaved changes was answered Cancel: a quit waiting on the window is given up. */
  closeCancelled(): void;
}

const COMMANDS: ReadonlySet<string> = new Set<AppCommandName>([
  'new',
  'open',
  'openPath',
  'save',
  'saveAs',
  'importPaused',
  'exportPaused',
  'importPs3d',
  'importScad',
  'exportScad',
  'exportImage',
  'exportVector',
  'exportStl',
  'exportGlb',
  'close',
  'undo',
  'redo',
  'toShapes',
  'convertGrid',
  'notes',
  'settings',
  'statusWindow',
  'cacheFiles',
  'colourView',
  'style',
  'focus3d',
  'theme',
  'keys',
  'cliTool',
  'checkUpdates',
  'about',
]);

/** Whether a value is one of the Theme setting's choices. */
function isTheme(v: unknown): v is MenuState['theme'] {
  return v === 'Light' || v === 'Dark' || v === 'System';
}

/** Whether a value is one of the 3D view's styles. */
function isMeshStyle(v: unknown): v is MeshStyle {
  return v === 'drawing' || v === 'flat' || v === 'wireframe' || v === 'stl' || v === 'uniform';
}

/** Whether a message from the main process is a command the renderer knows. */
export function isAppCommand(v: unknown): v is AppCommand {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const c = v as Record<string, unknown>;
  if (typeof c.cmd !== 'string' || !COMMANDS.has(c.cmd)) {
    return false;
  }
  switch (c.cmd) {
    case 'openPath':
      return typeof c.path === 'string' && c.path !== '';
    case 'colourView':
      return typeof c.on === 'boolean';
    case 'style':
      return isMeshStyle(c.style);
    case 'theme':
      return isTheme(c.theme);
    default:
      return true;
  }
}

/** Whether a rescue record is well formed (it is read back from disk). */
export function isRescueInfo(v: unknown): v is RescueInfo {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const r = v as Record<string, unknown>;
  return (
    (r.path === null || (typeof r.path === 'string' && r.path !== '')) &&
    typeof r.name === 'string' &&
    typeof r.revision === 'number' &&
    typeof r.time === 'number'
  );
}

/** Whether a menu state is well formed (the main process checks what the renderer sends). */
export function isMenuState(v: unknown): v is MenuState {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const s = v as Record<string, unknown>;
  const bools = [
    'document',
    'dirty',
    'solving',
    'paused',
    'solutions',
    'shapes',
    'animation',
    'colourView',
    'focus3d',
  ];
  const label = (x: unknown): boolean => x === null || typeof x === 'string';
  return (
    bools.every((k) => typeof s[k] === 'boolean') &&
    (s.path === null || typeof s.path === 'string') &&
    label(s.undo) &&
    label(s.redo) &&
    isMeshStyle(s.style) &&
    isTheme(s.theme) &&
    typeof s.keys === 'object' &&
    s.keys !== null &&
    Object.values(s.keys).every((k) => typeof k === 'string')
  );
}
