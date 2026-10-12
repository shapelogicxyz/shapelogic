// SPDX-License-Identifier: GPL-3.0-or-later
//
// What crosses between Electron's main process and the renderer besides
// the engine (stage 6; the engine's half is bridge.ts): the menus'
// commands one way and their state the other, the file dialogs, the
// stored settings and layouts, the recent files, the rescue copy, the
// help links, the window's close, and from stage 7 the exports' files.
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
  | { cmd: 'notes' }
  | { cmd: 'settings' }
  | { cmd: 'statusWindow' }
  | { cmd: 'colourView'; on: boolean }
  | { cmd: 'style'; style: MeshStyle }
  | { cmd: 'focus3d' }
  | { cmd: 'keys' }
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
  /** The Solver shows a solution with its moves stored (Export ▸ Solution Animation). */
  animation: boolean;
  colourView: boolean;
  style: MeshStyle;
  focus3d: boolean;
  /** The menus' keys as the one keyboard table has them, by command (shown, not registered). */
  keys: Partial<Record<AppCommandName, string>>;
}

/** The links of the Help menu (the main process holds the addresses). */
export type HelpLink = 'tutorial' | 'guide';

/** The kinds of file the dialogs ask for. */
export type FileKind = 'puzzle' | 'paused' | ExportKind;

/** The kinds of file an export writes (through `writeExport`). */
export type ExportKind = 'image' | 'vector' | 'stl' | 'glb';

/** The kinds of file, for the checks. */
export const FILE_KINDS: readonly FileKind[] = [
  'puzzle',
  'paused',
  'image',
  'vector',
  'stl',
  'glb',
];

/** Whether a value is a kind of file. */
export function isFileKind(v: unknown): v is FileKind {
  return typeof v === 'string' && (FILE_KINDS as readonly string[]).includes(v);
}

/** Whether a kind of file is an export's. */
export function isExportKind(v: unknown): v is ExportKind {
  return v === 'image' || v === 'vector' || v === 'stl' || v === 'glb';
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
  chooseOpen(kind: FileKind): Promise<string | null>;
  /** The save dialog with a name proposed; null when cancelled. */
  chooseSave(kind: FileKind, name: string): Promise<string | null>;
  /**
   * Writes an export's bytes to a path the save dialog gave for an export
   * kind (once); rejects with the reason when it could not (a read-only
   * file, a full disk, a path no dialog gave).
   */
  writeExport(path: string, data: Uint8Array): Promise<void>;
  /** The folder dialog (a solution's STL files); null when cancelled. */
  chooseFolder(): Promise<string | null>;
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
  'exportImage',
  'exportVector',
  'exportStl',
  'exportGlb',
  'close',
  'undo',
  'redo',
  'toShapes',
  'notes',
  'settings',
  'statusWindow',
  'colourView',
  'style',
  'focus3d',
  'keys',
  'about',
]);

/** Whether a message from the main process is a command the renderer knows. */
/** Whether a value is one of the 3D view's styles. */
function isMeshStyle(v: unknown): v is MeshStyle {
  return v === 'drawing' || v === 'flat' || v === 'wireframe' || v === 'stl';
}

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
    typeof s.keys === 'object' &&
    s.keys !== null &&
    Object.values(s.keys).every((k) => typeof k === 'string')
  );
}
