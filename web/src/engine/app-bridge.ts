// SPDX-License-Identifier: GPL-3.0-or-later
//
// What crosses between Electron's main process and the renderer besides
// the engine (stage 6; the engine's half is bridge.ts): the menus'
// commands one way and their state the other, the file dialogs, the
// stored settings and layouts, the recent files, the rescue copy, the
// help links, the window's close, the windows (New Window, a file held
// read-only, the settings another window changed), from stage 7 the exports' files, and
// the cache manager's items (View ▸ Manage Cache and Autosave Files),
// and the Play tab's goal picture in a window of its own.
// The preload exposes it as `window.btApp`; the renderer never sees a path
// it did not get from a dialog, a drop, the recent files or the rescue
// copy's record, and the main process writes an export only to a path its
// own save dialog gave for that kind. Plain types and checks, free of
// Electron and Node.

import { MESH_STYLE_VALUES } from './api.generated.ts';
import type { MeshStyle } from './api.generated.ts';
import { toBase64 } from './base64.ts';
import type { BridgeMessage, BridgeRequest } from './bridge.ts';

/** A command of the menus (or the window: close), sent to the renderer. */
export type AppCommand =
  | { cmd: 'new' }
  /** File ▸ New Window (decision 0036): the main process opens one; the key asks it (BtApp.newWindow). */
  | { cmd: 'newWindow' }
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
  /** View ▸ Log File Manager…: the app's log, newest first, with its filters, Export and Clear. */
  | { cmd: 'logFiles' }
  | { cmd: 'colorView'; on: boolean }
  | { cmd: 'style'; style: MeshStyle }
  | { cmd: 'focus3d' }
  /** View ▸ Color Theme: the Theme setting. */
  | { cmd: 'theme'; theme: Theme }
  | { cmd: 'keys' }
  /** ShapeLogic ▸ Command Line Tool… (Help on Windows and Linux): the dialog that explains and installs shapelogic_cli. */
  | { cmd: 'cliTool' }
  /** Help ▸ Check for Updates… (the application menu on macOS): ask GitHub now. */
  | { cmd: 'checkUpdates' }
  | { cmd: 'about' };

/** The names of the commands. */
export type AppCommandName = AppCommand['cmd'];

/** The Theme setting's choices. */
export const THEMES = ['Light', 'Dark', 'System'] as const;

/** One of the Theme setting's choices. */
export type Theme = (typeof THEMES)[number];

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
  colorView: boolean;
  style: MeshStyle;
  focus3d: boolean;
  theme: Theme;
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
  { ok: true; info: CliToolInfo } | { ok: false; error: string; canceled?: boolean };

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
 * What choosing a cache folder found: the dialog canceled; a solve running
 * (or another window changing it); the folders in use; a folder that
 * cannot be written (the folders in use stay); or one made and written,
 * with what the folders in use hold ('' nothing), for `finishCacheFolder`.
 */
export type CacheChoice =
  | { status: 'canceled' }
  | { status: 'busy'; reason: string }
  | { status: 'same' }
  | { status: 'invalid'; folder: string; error: string }
  | { status: 'ready'; folder: string; found: string };

/** What `finishCacheFolder` does with the data in the folders in use. */
export type CacheAction = 'move' | 'purge' | 'cancel';

const CACHE_ACTIONS = { move: true, purge: true, cancel: true } as const satisfies Record<
  CacheAction,
  true
>;

/** Whether a value is a cache action. */
export function isCacheAction(v: unknown): v is CacheAction {
  return typeof v === 'string' && Object.hasOwn(CACHE_ACTIONS, v);
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

/** How much a line of the app's log matters (main/app-log.ts); DEBUG only while Debug logging is on. */
export type LogLevel = 'DEBUG' | 'INFO' | 'WARN' | 'ERROR';

/** One line of the app's log, as the file has it (its time, level and source in it). */
export interface LogEntry {
  line: string;
  level: LogLevel;
}

/** What the Log File Manager asks for: a page of the lines that match, newest first. */
export interface LogQuery {
  /** The matches skipped (the lines already shown). */
  offset: number;
  /** The lines wanted (default 100, at most 1000). */
  limit?: number;
  /** Only the errors and warnings. */
  errorsOnly: boolean;
  /** Only the lines with this text in them, any case ('' every line). */
  search: string;
}

/** Whether a value is a log query (the main process checks what the renderer sends). */
export function isLogQuery(v: unknown): v is LogQuery {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const q = v as Record<string, unknown>;
  return (
    typeof q.offset === 'number' &&
    Number.isInteger(q.offset) &&
    q.offset >= 0 &&
    (q.limit === undefined || (typeof q.limit === 'number' && Number.isInteger(q.limit))) &&
    typeof q.errorsOnly === 'boolean' &&
    typeof q.search === 'string' &&
    q.search.length <= 1000
  );
}

/** A page of the app's log (the current file), newest first. */
export interface LogPage {
  entries: LogEntry[];
  /** The matches skipped before the first entry. */
  offset: number;
  /** The lines that match, in all. */
  matches: number;
  /** The lines of the current file, in all. */
  lines: number;
  /** More matches after these (Load more messages…). */
  more: boolean;
  /** The current file's size in bytes. */
  bytes: number;
  /** There is an older, compressed file too (Export includes it). */
  archived: boolean;
  /** The current file's full path. */
  file: string;
  /** Debug logging is on (Settings ▸ General): the file grows to a larger size. */
  debug: boolean;
  /** The size in bytes past which the file is compressed and a new one begun. */
  limit: number;
  /** Why the log could not be written or read lately; null when all is well. */
  error: string | null;
}

/** What Export… offers: all of the log (the compressed file's lines too), or its newest lines. */
export interface LogExportSizes {
  /** The lines of all of the log. */
  lines: number;
  /** Its bytes, as plain text. */
  bytes: number;
  /** The newest lines offered on their own (at most 100: fewer in a shorter log). */
  tailLines: number;
  /** Their bytes. */
  tailBytes: number;
}

/** The answer to Copy Diagnostics: the report's size on the clipboard, or why not. */
export type DiagnosticsResult = { ok: true; bytes: number } | { ok: false; error: string };

/** The answer to exporting the log: where it went, or why not. */
export type LogExportResult =
  { ok: true; path: string } | { ok: false; canceled: true } | { ok: false; error: string };

/** The website: the app's help links are its pages (the demo's are its own site's). */
export const SITE_ORIGIN = 'https://shapelogic.xyz';

/**
 * The links of the Help menu, the research page on tilted trays (the Tilt
 * & tolerance section's link), and the guide's page on an engine that does
 * not start (the engine's dialog): a page of the site by its path, or an
 * address elsewhere. The renderer names a link, never a URL; the main
 * process opens only these (shell.openExternal), and the demo opens its
 * own site's (cleanup item 12.3: the table was written in four places).
 * The site's pages must exist (site/tests/help-urls.test.mjs).
 */
export const HELP_URLS = {
  guide: '/docs/',
  tutorial: '/docs/getting-started/',
  // the About page's link to the author's GitHub page
  author: 'https://github.com/hypnoz/',
  // the Log File Manager's link for reporting a problem
  issues: 'https://github.com/shapelogicxyz/shapelogic/issues',
  // the Tilt & tolerance section's link: the research page on tilted trays
  tilted: '/research/tilted-square-geometry.html',
  // the engine's dialog when it cannot start (cleanup item 2.9)
  troubleshooting: '/docs/windows-troubleshooting/',
} as const satisfies Record<string, string>;

/** A help link's name. */
export type HelpLink = keyof typeof HELP_URLS;

/** Whether a value names a help link. */
export function isHelpLink(v: unknown): v is HelpLink {
  return typeof v === 'string' && Object.hasOwn(HELP_URLS, v);
}

/** A help link's address: a page of the site at `origin` (the website's by default), or its own. */
export function helpUrl(link: HelpLink, origin: string = SITE_ORIGIN): string {
  return new URL(HELP_URLS[link], origin).toString();
}

/**
 * What the update check found (main/updates.ts): the latest release
 * (`v1.4`) newer than this version, with its notes (Markdown, '' when it
 * has none), the same or older, or no answer.
 */
export type UpdateResult =
  | { status: 'newer'; current: string; latest: string; notes: string }
  | { status: 'current'; current: string; latest: string }
  | { status: 'error'; current: string; error: string };

/** The kinds of file the open dialog asks for (a Puzzlecad file is imported and exported). */
export const OPEN_KINDS = ['puzzle', 'paused', 'ps3d', 'scad'] as const;

/** The kinds of file an export writes (through `writeExport`). */
export const EXPORT_KINDS = ['image', 'vector', 'stl', 'glb', 'scad', 'moves'] as const;

/** A kind of file the open dialog asks for. */
export type OpenKind = (typeof OPEN_KINDS)[number];

/** A kind of file an export writes. */
export type ExportKind = (typeof EXPORT_KINDS)[number];

/** The kinds of file the dialogs ask for. */
export type FileKind = OpenKind | ExportKind;

/** Whether a value is in a list of strings. */
function oneOf<T extends string>(list: readonly T[], v: unknown): v is T {
  return typeof v === 'string' && (list as readonly string[]).includes(v);
}

/** Whether a value is a kind of file. */
export function isFileKind(v: unknown): v is FileKind {
  return oneOf(OPEN_KINDS, v) || oneOf(EXPORT_KINDS, v);
}

/** Whether a kind of file is an export's. */
export function isExportKind(v: unknown): v is ExportKind {
  return oneOf(EXPORT_KINDS, v);
}

/** Whether a kind of file is one the open dialog asks for. */
export function isOpenKind(v: unknown): v is OpenKind {
  return oneOf(OPEN_KINDS, v);
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

/** A rescue copy on disk: its file (document.open's `path`) and its record. */
export interface RescueCopy {
  file: string;
  info: RescueInfo;
}

/**
 * How a rescue copy's write ended: written, or why not (the engine's errc:
 * invalid_state while a solve runs or once another document is open,
 * disconnected while it restarts; io_error when a file could not be written).
 */
export type RescueWritten = { ok: true } | { ok: false; errc: string; message: string };

/** What the renderer stores in the main process's store, by key. */
export interface StoredState {
  prefs?: Record<string, unknown>;
  layout?: Record<string, unknown>;
}

/**
 * The Play tab's goal picture, for its window of its own: an SVG drawing
 * with its styles written into it (no stylesheet, no script, nothing
 * fetched), its size, the window's title and its background color.
 */
export interface GoalPicture {
  title: string;
  svg: string;
  /** The drawing's size in pixels: the window's first size keeps its shape. */
  width: number;
  height: number;
  /** A CSS color: `#rrggbb` or `rgb(…)`. */
  background: string;
  /** The dark theme is in use: the GOAL caption above the drawing is white, else black. */
  dark: boolean;
}

/** The largest goal drawing taken, in characters (pieces' pictures are in it). */
export const GOAL_SVG_MAX = 32 * 1024 * 1024;

/** True for a goal picture the goal window takes (it comes from the page: checked). */
export function isGoalPicture(v: unknown): v is GoalPicture {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const p = v as Record<string, unknown>;
  const size = (n: unknown): boolean => typeof n === 'number' && n >= 1 && n <= 100_000;
  return (
    typeof p.title === 'string' &&
    p.title.length <= 300 &&
    typeof p.svg === 'string' &&
    p.svg.startsWith('<svg') &&
    p.svg.length <= GOAL_SVG_MAX &&
    size(p.width) &&
    size(p.height) &&
    typeof p.background === 'string' &&
    /^(#[0-9a-f]{6}|rgba?\([0-9., ]+\))$/i.test(p.background) &&
    typeof p.dark === 'boolean'
  );
}

/** The height of the goal window's GOAL caption above the drawing, in pixels. */
const GOAL_CAPTION = 30;

/**
 * The goal window's page: GOAL in bold above the drawing, which is an
 * image (so nothing in it can run or fetch) that fills the rest of the
 * window, kept in shape, under a policy that allows only that image and
 * the page's own style.
 */
export function goalPage(p: GoalPicture): string {
  const esc = (t: string): string =>
    t.replace(
      /[&<>"]/g,
      (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c] ?? c,
    );
  const ink = p.dark ? '#ffffff' : '#000000';
  return (
    '<!doctype html><html><head><meta charset="utf-8">' +
    `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'">` +
    `<title>${esc(p.title)}</title>` +
    `<style>html,body{margin:0;height:100%;overflow:hidden;background:${p.background}}` +
    `body{display:flex;flex-direction:column}` +
    `h1{flex:none;box-sizing:border-box;height:${String(GOAL_CAPTION)}px;margin:0;padding:12px 12px 0;font:bold 18px/18px system-ui,-apple-system,"Segoe UI",sans-serif;letter-spacing:.08em;text-align:center;color:${ink};user-select:none}` +
    `img{display:block;flex:1 1 auto;min-height:0;box-sizing:border-box;width:100%;padding:12px;object-fit:contain;user-select:none;-webkit-user-drag:none}</style>` +
    `</head><body><h1>GOAL</h1><img alt="${esc(p.title)}" src="data:image/svg+xml;base64,${toBase64(new TextEncoder().encode(p.svg))}"></body></html>`
  );
}

/** The goal window's first content size: the drawing's shape under its caption, at most `max` pixels across or down. */
export function goalWindowSize(p: GoalPicture, max = 640): { width: number; height: number } {
  const pad = 24;
  const k = Math.min(1, (max - pad) / p.width, (max - pad - GOAL_CAPTION) / p.height);
  return {
    width: Math.max(220, Math.round(p.width * k + pad)),
    height: Math.max(160, Math.round(p.height * k + pad + GOAL_CAPTION)),
  };
}

/** How an export's file was written: or why not (`writeExport`, `writeFolderExport`). */
export type WriteResult = { ok: true } | { ok: false; error: string };

/** The platform and the version the preload reads at once (`bt:app-info`); null for a page not the app's. */
export type AppInfo = { platform: string; version: string } | null;

/**
 * Every IPC channel between the renderer and the main process, by name
 * (cleanup item 12.2): its kind, its arguments and its result.
 * - `send`: renderer to main (ipcRenderer.send, ipcMain.on), no answer;
 * - `sync`: renderer to main, answered at once (sendSync);
 * - `invoke`: renderer to main, answered (ipcRenderer.invoke,
 *   ipcMain.handle); `result` is what main answers, a window that is not
 *   the app's too (main/app-ipc.ts's `handle` gives each channel's refusal);
 * - `event`: main to renderer (webContents.send, ipcRenderer.on).
 * The preload sends only through these types and main registers through
 * them; `APP_CHANNELS` is the same table as data, which the unit tests
 * hold to what main registers and the preload uses. Main checks every
 * argument it is sent: the renderer is not trusted.
 */
export interface AppChannels {
  // the engine's bridge (bridge.ts)
  'bt:call': { kind: 'send'; args: [request: BridgeRequest] };
  'bt:message': { kind: 'event'; args: [message: BridgeMessage] };
  // the menus, the window, its file and the settings
  'bt:app-info': { kind: 'sync'; args: []; result: AppInfo };
  'bt:command': { kind: 'event'; args: [command: AppCommand] };
  'bt:menu-state': { kind: 'send'; args: [state: MenuState] };
  'bt:read-only': { kind: 'event'; args: [readOnly: boolean] };
  'bt:prefs': { kind: 'event'; args: [prefs: Record<string, unknown>] };
  'bt:load-state': { kind: 'invoke'; args: []; result: StoredState | null };
  'bt:save-state': {
    kind: 'send';
    args: [key: keyof StoredState, value: Record<string, unknown>];
  };
  'bt:recent-add': { kind: 'send'; args: [path: string] };
  'bt:recent-remove': { kind: 'send'; args: [path: string] };
  'bt:new-window': { kind: 'send'; args: [] };
  'bt:claim-path': { kind: 'send'; args: [path: string] };
  'bt:dropped': { kind: 'send'; args: [path: string] };
  'bt:help': { kind: 'send'; args: [link: HelpLink] };
  'bt:goal-window': { kind: 'send'; args: [picture: GoalPicture, raise: boolean] };
  'bt:close-window': { kind: 'send'; args: [] };
  'bt:close-taken': { kind: 'send'; args: [] };
  'bt:close-canceled': { kind: 'send'; args: [] };
  'bt:engine-ready': { kind: 'send'; args: [] };
  'bt:engine-retry': { kind: 'send'; args: [] };
  'bt:command-held': { kind: 'send'; args: [command: AppCommandName, dropped: boolean] };
  // the dialogs and the exports
  'bt:choose-open': { kind: 'invoke'; args: [kind: OpenKind]; result: string | null };
  'bt:choose-save': { kind: 'invoke'; args: [kind: FileKind, name: string]; result: string | null };
  'bt:write-export': {
    kind: 'invoke';
    args: [path: string, data: Uint8Array];
    result: WriteResult;
  };
  'bt:choose-folder': { kind: 'invoke'; args: [save: boolean]; result: string | null };
  'bt:existing-exports': {
    kind: 'invoke';
    args: [folder: string, names: readonly string[]];
    result: number;
  };
  'bt:write-folder-export': {
    kind: 'invoke';
    args: [folder: string, name: string, data: Uint8Array];
    result: WriteResult;
  };
  // the rescue copy
  'bt:rescue-write': { kind: 'invoke'; args: [info: RescueInfo]; result: RescueWritten | null };
  'bt:rescue-read': { kind: 'invoke'; args: []; result: RescueCopy | null };
  'bt:rescue-clear': { kind: 'invoke'; args: []; result: null };
  // the update check
  'bt:check-updates': { kind: 'invoke'; args: [auto: boolean]; result: UpdateResult | null };
  'bt:skip-update': { kind: 'send'; args: [version: string] };
  'bt:open-releases': { kind: 'send'; args: [] };
  // the command line tool
  'bt:cli-info': { kind: 'invoke'; args: []; result: CliToolResult };
  'bt:cli-install': { kind: 'invoke'; args: []; result: CliToolResult };
  'bt:cli-uninstall': { kind: 'invoke'; args: []; result: CliToolResult };
  // the cache folder and the cache manager
  'bt:cache-folder': { kind: 'invoke'; args: []; result: CacheFolderInfo | null };
  'bt:cache-choose': { kind: 'invoke'; args: [useDefault: boolean]; result: CacheChoice | null };
  'bt:cache-finish': { kind: 'invoke'; args: [action: CacheAction]; result: CacheResult | null };
  'bt:cache-items': { kind: 'invoke'; args: []; result: CacheItem[] | null };
  'bt:cache-items-start': { kind: 'invoke'; args: []; result: CacheItem[] | null };
  'bt:cache-delete': { kind: 'invoke'; args: [id: string]; result: CacheDeleteResult | null };
  'bt:cache-link': { kind: 'send'; args: [link: CacheLink] };
  // the log manager and the diagnostics
  'bt:log-page': { kind: 'invoke'; args: [query: LogQuery]; result: LogPage | null };
  'bt:log-clear': { kind: 'invoke'; args: []; result: null };
  'bt:log-closed': { kind: 'invoke'; args: []; result: null };
  'bt:log-sizes': { kind: 'invoke'; args: []; result: LogExportSizes | null };
  'bt:log-export': { kind: 'invoke'; args: [lines?: number]; result: LogExportResult | null };
  'bt:diagnostics-copy': { kind: 'invoke'; args: []; result: DiagnosticsResult | null };
  'bt:diagnostics-save': { kind: 'invoke'; args: []; result: LogExportResult | null };
}

/** A channel's name. */
export type AppChannel = keyof AppChannels;

/** The channels of a kind. */
export type ChannelOf<K extends AppChannels[AppChannel]['kind']> = {
  [C in AppChannel]: AppChannels[C]['kind'] extends K ? C : never;
}[AppChannel];

/** What a channel carries. */
export type ChannelArgs<C extends AppChannel> = AppChannels[C]['args'];

/** What a `sync` or `invoke` channel answers. */
export type ChannelResult<C extends ChannelOf<'sync' | 'invoke'>> = AppChannels[C] extends {
  result: infer R;
}
  ? R
  : never;

/** The table as data: each channel's kind (the compiler holds it to AppChannels, both ways). */
export const APP_CHANNELS = {
  'bt:call': 'send',
  'bt:message': 'event',
  'bt:app-info': 'sync',
  'bt:command': 'event',
  'bt:menu-state': 'send',
  'bt:read-only': 'event',
  'bt:prefs': 'event',
  'bt:load-state': 'invoke',
  'bt:save-state': 'send',
  'bt:recent-add': 'send',
  'bt:recent-remove': 'send',
  'bt:new-window': 'send',
  'bt:claim-path': 'send',
  'bt:dropped': 'send',
  'bt:help': 'send',
  'bt:goal-window': 'send',
  'bt:close-window': 'send',
  'bt:close-taken': 'send',
  'bt:close-canceled': 'send',
  'bt:engine-ready': 'send',
  'bt:engine-retry': 'send',
  'bt:command-held': 'send',
  'bt:choose-open': 'invoke',
  'bt:choose-save': 'invoke',
  'bt:write-export': 'invoke',
  'bt:choose-folder': 'invoke',
  'bt:existing-exports': 'invoke',
  'bt:write-folder-export': 'invoke',
  'bt:rescue-write': 'invoke',
  'bt:rescue-read': 'invoke',
  'bt:rescue-clear': 'invoke',
  'bt:check-updates': 'invoke',
  'bt:skip-update': 'send',
  'bt:open-releases': 'send',
  'bt:cli-info': 'invoke',
  'bt:cli-install': 'invoke',
  'bt:cli-uninstall': 'invoke',
  'bt:cache-folder': 'invoke',
  'bt:cache-choose': 'invoke',
  'bt:cache-finish': 'invoke',
  'bt:cache-items': 'invoke',
  'bt:cache-items-start': 'invoke',
  'bt:cache-delete': 'invoke',
  'bt:cache-link': 'send',
  'bt:log-page': 'invoke',
  'bt:log-clear': 'invoke',
  'bt:log-closed': 'invoke',
  'bt:log-sizes': 'invoke',
  'bt:log-export': 'invoke',
  'bt:diagnostics-copy': 'invoke',
  'bt:diagnostics-save': 'invoke',
} as const satisfies { [C in AppChannel]: AppChannels[C]['kind'] };

/** What the preload exposes as `window.btApp`. */
export interface BtApp {
  /** The platform (`process.platform`): the menus' keys differ on macOS. */
  readonly platform: string;
  /** The app's version. */
  readonly version: string;
  /** The app keeps a log (View ▸ Log File Manager…; not the demo): the warnings' View Logs opens it. */
  readonly keepsLog: boolean;
  /** Listens to the menus' commands; returns the unsubscribe. */
  onCommand(handler: (command: AppCommand) => void): () => void;
  /** The menus show this. */
  setMenuState(state: MenuState): void;
  /** The open dialog; null when canceled. */
  chooseOpen(kind: OpenKind): Promise<string | null>;
  /** The save dialog with a name proposed; null when canceled. */
  chooseSave(kind: FileKind, name: string): Promise<string | null>;
  /**
   * Writes an export's bytes to a path the save dialog gave for an export
   * kind (once); rejects with the reason when it could not (a read-only
   * file, a full disk, a path no dialog gave).
   */
  writeExport(path: string, data: Uint8Array): Promise<void>;
  /**
   * The folder dialog (a solution's STL files, several shapes'); null when
   * canceled. `save`: the files are written as soon as it is chosen, and
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
  /**
   * Writes the rescue copy with its record: the window's engine writes the
   * document it holds (document.saveRescue at `info.revision`, cleanup item
   * 10.11), so the document never crosses IPC.
   */
  writeRescue(info: RescueInfo): Promise<RescueWritten>;
  /** The rescue copy (its file, which the window may then open), or null when there is none. */
  readRescue(): Promise<RescueCopy | null>;
  /** Removes the rescue copy (saved, or its changes given up). */
  clearRescue(): Promise<void>;
  /** File ▸ New Window: the main process opens another window (decision 0036). */
  newWindow(): void;
  /**
   * The window opened this file (Open, Open Recent, a drop, the rescue
   * copy's Reopen): it holds it, read-only when another window owns it.
   */
  claimPath(path: string): void;
  /**
   * Listens to whether the window holds its file read-only (another
   * window owns it: Save is Save As); called at once with the state now.
   * Returns the unsubscribe.
   */
  onReadOnly(handler: (readOnly: boolean) => void): () => void;
  /** Listens to the settings another window changed; returns the unsubscribe. */
  onPrefs(handler: (prefs: Record<string, unknown>) => void): () => void;
  /** Opens a help page in the system's browser. */
  openHelp(link: HelpLink): void;
  /**
   * The Play tab's goal picture in a window of its own, which the user
   * moves and sizes, above this window and closed with it. `raise`: open
   * it (or bring it forward); else only a window open already shows it.
   */
  goalWindow(picture: GoalPicture, raise: boolean): void;
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
   * deletes it, then uses it; or, canceled, keeps the folders in use.
   */
  finishCacheFolder(action: CacheAction): Promise<CacheResult>;
  /** What the cache folder and the rescue copy hold, newest first; null where there is none (the demo). */
  cacheItems(): Promise<CacheItem[] | null>;
  /**
   * The same, for the notice at the start: the first window of a run to ask
   * gets them, every other ask an empty list (null where there is none).
   */
  cacheItemsAtStart(): Promise<CacheItem[] | null>;
  /** Deletes a cache item's files (refused while a solve runs, for a search's). */
  deleteCacheItem(id: string): Promise<CacheDeleteResult>;
  /** These search folders belong to this puzzle and problem: the cache manager names them so. */
  linkCacheFolders(link: CacheLink): void;
  /** A page of the app's log, newest first; null where there is none (the demo). */
  logPage(query: LogQuery): Promise<LogPage | null>;
  /** Deletes the app's log (the current file and the compressed one). */
  clearLog(): Promise<void>;
  /** The Log File Manager closed: the main process drops the lines it read for it. */
  logClosed(): Promise<void>;
  /** What Export… offers: the lines and bytes of all of the log and of its newest lines; null where there is none (the demo). */
  logExportSizes(): Promise<LogExportSizes | null>;
  /**
   * The save dialog for the log, then the log written there as plain text:
   * all of it, or only its newest `lines` lines.
   */
  exportLog(lines?: number): Promise<LogExportResult>;
  /**
   * The page showed its first document (the log's Startup line, cleanup
   * item 2.7); a reload says it again, which the main process ignores.
   */
  ready(): void;
  /** The engine could not start: Try Again (the host forgets its quick deaths and starts one). */
  retryEngine(): void;
  /**
   * The log hears of a document command that came before the first
   * document: held until it comes, or dropped because the engine could not
   * start (the window says so instead).
   */
  commandHeld(command: AppCommandName, dropped: boolean): void;
  /**
   * Help ▸ Copy Diagnostics: the main process builds the report (the
   * health report, the engine's last failure, the log's newest lines, the
   * crash dumps; on Windows the security checks, run now) and puts it on
   * the clipboard.
   */
  copyDiagnostics(): Promise<DiagnosticsResult>;
  /** The same report saved to a text file the save dialog gives. */
  saveDiagnostics(): Promise<LogExportResult>;
  /** Closes the window: the renderer answered a `close` command (nothing to save, or saved or given up). */
  closeWindow(): void;
  /** The `close` command was taken: the renderer asks (a close not taken soon is asked natively). */
  closeTaken(): void;
  /** The question about unsaved changes was answered Cancel: a quit waiting on the window is given up. */
  closeCanceled(): void;
}

/**
 * Every command's name (cleanup item 12.3): the compiler holds the table to
 * AppCommand, so a command added there is refused until it is here too.
 */
const COMMANDS = {
  new: true,
  newWindow: true,
  open: true,
  openPath: true,
  save: true,
  saveAs: true,
  importPaused: true,
  exportPaused: true,
  importPs3d: true,
  importScad: true,
  exportScad: true,
  exportImage: true,
  exportVector: true,
  exportStl: true,
  exportGlb: true,
  close: true,
  undo: true,
  redo: true,
  toShapes: true,
  convertGrid: true,
  notes: true,
  settings: true,
  statusWindow: true,
  cacheFiles: true,
  logFiles: true,
  colorView: true,
  style: true,
  focus3d: true,
  theme: true,
  keys: true,
  cliTool: true,
  checkUpdates: true,
  about: true,
} as const satisfies Record<AppCommandName, true>;

/** Whether a value is the name of a menu command. */
export function isAppCommandName(v: unknown): v is AppCommandName {
  return typeof v === 'string' && Object.hasOwn(COMMANDS, v);
}

/** Whether a value is one of the Theme setting's choices. */
function isTheme(v: unknown): v is Theme {
  return oneOf(THEMES, v);
}

/** Whether a value is one of the 3D view's styles (the schema's: api.generated.ts). */
function isMeshStyle(v: unknown): v is MeshStyle {
  return oneOf(MESH_STYLE_VALUES, v);
}

/** Whether a message from the main process is a command the renderer knows. */
export function isAppCommand(v: unknown): v is AppCommand {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const c = v as Record<string, unknown>;
  if (!isAppCommandName(c.cmd)) {
    return false;
  }
  switch (c.cmd) {
    case 'openPath':
      return typeof c.path === 'string' && c.path !== '';
    case 'colorView':
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

/** The keys of a type whose values are booleans. */
type BooleanKeys<T> = { [K in keyof T]: T[K] extends boolean ? K : never }[keyof T];

/**
 * MenuState's switches (cleanup item 12.3): the compiler holds the table to
 * the type, so a switch added there is checked here too (`scad`, added
 * later, was left unchecked).
 */
const MENU_SWITCHES = {
  document: true,
  dirty: true,
  solving: true,
  paused: true,
  solutions: true,
  shapes: true,
  scad: true,
  animation: true,
  colorView: true,
  focus3d: true,
} as const satisfies Record<BooleanKeys<MenuState>, true>;

/** Whether a menu state is well formed (the main process checks what the renderer sends). */
export function isMenuState(v: unknown): v is MenuState {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const s = v as Record<string, unknown>;
  const label = (x: unknown): boolean => x === null || typeof x === 'string';
  return (
    Object.keys(MENU_SWITCHES).every((k) => typeof s[k] === 'boolean') &&
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
