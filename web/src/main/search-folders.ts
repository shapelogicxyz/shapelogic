// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the two cache modules (cache-folder.ts: the folder in use and its
// moves; cache-files.ts: its items and the sweep) both know of the search
// folders, in one place (cleanup item 7.3): the names the engine gives a
// search's folder, which names are a solve's lock (decision 0035: never
// listed, moved, counted or swept: only the system knows which are held),
// the words of a running solve, the walk of a folder, and a system error's
// code (export-file.ts's too). Free of Electron.

import { lstat, readdir } from 'node:fs/promises';
import { join } from 'node:path';

/** The names the engine gives a search's folder, by kind: `slide-<key>` and `<key>`. */
export const SEARCH_NAME: Readonly<Record<'sliding' | 'stacking', RegExp>> = {
  sliding: /^slide-[0-9a-f]{16}$/,
  stacking: /^[0-9a-f]{16}$/,
};

/**
 * Whether a name in a search folder is a lock file. Every `*.lock`, not
 * only the names the engine gives a solve's lock (`solve-<key>.lock`,
 * `<key>.lock`): a lock of another version, or of the command line, is
 * never removed by the app.
 */
export function isLockFile(name: string): boolean {
  return name.endsWith('.lock');
}

/** The words of a running solve (a search's files are not changed while one runs). */
export const SOLVING =
  'A solve is running in a ShapeLogic window. Pause or stop it first: its search writes to the cache folder.';

/** What walking a file or folder found. */
export interface Walked {
  /** Every file under it (its own path for a file), in name order. */
  files: string[];
  bytes: number;
  /** The oldest creation time under it (ms; the change time where the system has none). */
  created: number;
  /** The newest change under it (ms). */
  modified: number;
}

const createdOf = (s: { birthtimeMs: number; mtimeMs: number }): number =>
  s.birthtimeMs > 0 ? s.birthtimeMs : s.mtimeMs;

async function walkInto(path: string, into: Walked): Promise<void> {
  let s;
  try {
    s = await lstat(path);
  } catch {
    return;
  }
  into.created = Math.min(into.created, createdOf(s));
  into.modified = Math.max(into.modified, s.mtimeMs);
  if (!s.isDirectory()) {
    into.files.push(path);
    into.bytes += s.size;
    return;
  }
  for (const name of (await readdir(path).catch(() => [])).sort()) {
    await walkInto(join(path, name), into);
  }
}

/**
 * Every file under these paths (links not followed, a path not there
 * skipped), their bytes and times; `created` is `modified` when nothing
 * was found.
 */
export async function walk(paths: readonly string[]): Promise<Walked> {
  const w: Walked = { files: [], bytes: 0, created: Infinity, modified: 0 };
  for (const p of paths) {
    await walkInto(p, w);
  }
  if (!Number.isFinite(w.created)) {
    w.created = w.modified;
  }
  return w;
}

/** A system error's code (`ENOENT`, `EACCES`, …), or '' when it has none. */
export function errorCode(e: unknown): string {
  return typeof e === 'object' && e !== null && 'code' in e ? String(e.code) : '';
}
