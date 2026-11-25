// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files a window's engine may be given (stage 8, the security
// checklist: file paths only from dialogs and drops). The engine opens and
// writes whatever path a call names, so the main process checks the calls
// that name one (document.open, its `as` too, document.openBytes, document.import,
// document.save, document.saveRescue, solve.importPaused, solve.exportPaused) against the paths it handed that
// window itself: what its file dialogs gave, a file dropped on the window
// (the preload's pathOf, which only a real dropped file answers), a file
// the menus or the system opened with the app (Open Recent, the command
// line, macOS's open-file, a second start), and the rescue record's file.
// A renderer that names any other path (a page running code it should not)
// is refused before the engine sees the call. Electron-free.

/**
 * The methods that name a file, and the parameters that do (document.open's
 * `as` is the file a save then writes: a rescue copy's, cleanup item 10.11).
 * document.saveRescue is the main process's (app-ipc.ts), to the rescue
 * copy's file; a page that calls it can name only a file it was handed.
 */
const PATH_PARAMS: Readonly<Record<string, readonly string[]>> = {
  'document.open': ['path', 'as'],
  'document.openBytes': ['path'],
  'document.import': ['path'],
  'document.save': ['path'],
  'document.saveRescue': ['path'],
  'solve.importPaused': ['path'],
  'solve.exportPaused': ['path'],
};

/** The files a call names (none: an empty list). */
export function callPaths(method: string, params: unknown): string[] {
  const keys = PATH_PARAMS[method];
  if (keys === undefined || typeof params !== 'object' || params === null) {
    return [];
  }
  return keys
    .map((k) => (params as Record<string, unknown>)[k])
    .filter((v): v is string => typeof v === 'string');
}

/** The methods callPaths looks at (for the tests: every schema method with a path). */
export const PATH_METHODS: readonly string[] = Object.keys(PATH_PARAMS);

/**
 * The engine's settings that name a folder, which only the main process
 * sets (the cache folder, cache-folder.ts: by default the saved searches'
 * folder in the app's data directory, deferred part 4): a search's folder
 * is emptied and written by the engine, so a page must not point it
 * anywhere; the page changes them only through the main process's folder
 * dialog.
 */
export const FOLDER_SETTINGS: readonly string[] = ['panexFolder', 'slideFolder'];

/** Why a page's call may not set the engine's settings it names (a folder), or null. */
export function settingsRefusal(method: string, params: unknown): string | null {
  if (method !== 'settings.set' || typeof params !== 'object' || params === null) {
    return null;
  }
  const named = FOLDER_SETTINGS.filter((k) => k in params);
  return named.length === 0 ? null : `${named.join(' and ')}: the app sets the engine's folders`;
}

/** The paths each window's renderer was handed. */
export class PathGrants<K extends WeakKey> {
  private readonly granted = new WeakMap<K, Set<string>>();

  /** The window was handed this path (a dialog, a drop, a file opened with the app). */
  grant(key: K, path: string): void {
    if (path === '') {
      return;
    }
    let s = this.granted.get(key);
    if (s === undefined) {
      s = new Set();
      this.granted.set(key, s);
    }
    s.add(path);
  }

  /** Whether the window was handed this path. */
  allows(key: K, path: string): boolean {
    return this.granted.get(key)?.has(path) === true;
  }

  /**
   * Why a call may not go to the engine (it names a path the window was not
   * handed), or null when it may.
   */
  refusal(key: K, method: string, params: unknown): string | null {
    const path = callPaths(method, params).find((p) => !this.allows(key, p));
    if (path === undefined) {
      return null;
    }
    const name = path.split(/[\\/]/).pop() ?? path;
    return `${name} was not chosen in a file dialog or dropped on the window`;
  }
}
