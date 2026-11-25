// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which window owns a puzzle file (decision 0036): several windows may be
// open, each with its own engine, and two of them may hold the same file.
// The first window to hold a file owns it; another that opens it holds it
// read-only: its Save is a Save As, and the main process refuses its
// engine's document.save to that file (the hard stop: the renderer's
// read-only mark is only a mirror). A window also may not save onto a file
// another window holds. A window holds the file its document has (the menu
// state's path, as the renderer sends it) and the one it was opened from
// (`claim`, sent once the open succeeded, so a reopen of the same file
// counts as a new hold). A window lets its file go when its document's path
// changes (New, another file, an engine that restarted) or the window
// closes; a read-only window stays read-only while it keeps the file, even
// once the owner has gone (its copy may be older than the file on disk),
// until it opens the file again. Paths are compared as the system names
// the file (realpath; without case on macOS and Windows). Electron-free.

import { realpathSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';

import { callPaths } from './path-grants.ts';

/** What a window holds: its file's key, and whether it holds it read-only. */
interface Hold {
  key: string;
  /** The path as the window named it (for the messages). */
  path: string;
  readOnly: boolean;
}

/** A path as the system names its file: the key two windows' paths are compared by. */
export function pathKey(path: string, platform: string = process.platform): string {
  let p = resolve(path);
  try {
    p = realpathSync.native(p);
  } catch {
    // a file not there yet (a Save As): its folder's real path
    try {
      p = join(realpathSync.native(dirname(p)), basename(p));
    } catch {
      // no folder either: as given
    }
  }
  return platform === 'darwin' || platform === 'win32' ? p.toLowerCase() : p;
}

/** The files the windows hold, by window. */
export class PathOwners<K> {
  private readonly holds = new Map<K, Hold>();
  private readonly owners = new Map<string, K>();
  private readonly keyOf: (path: string) => string;
  /** Called when a window's read-only state changes. */
  private readonly changed: (window: K, readOnly: boolean) => void;

  constructor(changed: (window: K, readOnly: boolean) => void, keyOf = pathKey) {
    this.changed = changed;
    this.keyOf = keyOf;
  }

  /** Whether the window holds its file read-only. */
  readOnly(window: K): boolean {
    return this.holds.get(window)?.readOnly === true;
  }

  /** The window that owns this file, or undefined. */
  owner(path: string): K | undefined {
    return this.owners.get(this.keyOf(path));
  }

  /**
   * The window's document has this file now (its menu state's path): a
   * new file is held, owned when no other window owns it; the same file
   * as before changes nothing.
   */
  hold(window: K, path: string | null): void {
    const old = this.holds.get(window);
    if (path === null) {
      this.release(window);
      return;
    }
    const key = this.keyOf(path);
    if (old?.key === key) {
      return;
    }
    this.take(window, key, path);
  }

  /**
   * The window opened this file (Open, Open Recent, a drop, the rescue
   * copy's Reopen): held again, owned when no other window owns it, even
   * when the window held it read-only before.
   */
  claim(window: K, path: string): void {
    this.take(window, this.keyOf(path), path);
  }

  /** The window closed: its file is let go. */
  release(window: K): void {
    const old = this.holds.get(window);
    if (old === undefined) {
      return;
    }
    this.holds.delete(window);
    if (this.owners.get(old.key) === window) {
      this.owners.delete(old.key);
    }
    if (old.readOnly) {
      this.changed(window, false);
    }
  }

  /**
   * Why a call may not go to the window's engine: a save over a file
   * another window holds, or over the window's own file held read-only;
   * null when it may.
   */
  refusal(window: K, method: string, params: unknown): string | null {
    if (method !== 'document.save') {
      return null;
    }
    const named = callPaths(method, params)[0] ?? null;
    const own = this.holds.get(window);
    if (named === null) {
      // the document's own file
      return own?.readOnly === true ? this.message(own.path) : null;
    }
    const key = this.keyOf(named);
    if (own?.key === key && own.readOnly) {
      return this.message(named);
    }
    // a file only held read-only elsewhere (its owner gone) may be saved
    // over: those windows never save to it
    const owner = this.owners.get(key);
    return owner !== undefined && owner !== window ? this.message(named) : null;
  }

  private message(path: string): string {
    const name = path.split(/[\\/]/).pop() ?? path;
    return `${name} is open in another window: save this puzzle under another name (Save As)`;
  }

  private take(window: K, key: string, path: string): void {
    const old = this.holds.get(window);
    if (old !== undefined && old.key !== key && this.owners.get(old.key) === window) {
      this.owners.delete(old.key);
    }
    const owner = this.owners.get(key);
    const readOnly = owner !== undefined && owner !== window;
    if (!readOnly) {
      this.owners.set(key, window);
    }
    this.holds.set(window, { key, path, readOnly });
    if ((old?.readOnly ?? false) !== readOnly) {
      this.changed(window, readOnly);
    }
  }
}
