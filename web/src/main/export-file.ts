// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writing an exported file (stage 7: the image, the vector image, the STL
// and the animation), the way the core writes a puzzle (core/shapelogic/io/
// gzip_file.cpp: saveTarget and replaceWith, since the web stage 6 audit):
// the bytes go to a temporary file beside the file and are renamed over it
// once whole (replace-file.ts), so a failed write leaves the old file; the
// place is the file the path names, a symbolic link followed (at most 40),
// so the link stays; a read-only file is refused, where a rename would
// replace it; the new file gets the permissions of the one it replaces;
// the temporary file is this write's own, with a name of its own, made
// exclusively, never one already at that name (the web stage 8 audit: a
// link planted there carried the bytes to the file it named; cleanup item
// 7.4: two windows exporting to one folder at once removed each other's
// temporary file, and one installed the other's partial bytes). And which
// paths a window may have written
// (`ExportGrants`): those its save dialog gave it for an export, once each;
// and from deferred part 5 the folder its folder dialog gave it for a
// solution's STL files (`FolderGrants`), files there named only as the
// engine names them (`isPieceFileName`). Free of Electron (the unit tests
// use it).

import { access, constants, lstat, readlink, stat } from 'node:fs/promises';
import { basename, dirname, extname, isAbsolute, join } from 'node:path';

import type { ExportKind } from '../engine/app-bridge.ts';
import { replaceFile } from './replace-file.ts';
import { errorCode } from './search-folders.ts';

/** As the core's kMaxLinks. */
const MAX_LINKS = 40;

/** Why a file could not be written, in words for the user. */
export class WriteError extends Error {
  override name = 'WriteError';
}

/** The file a path names: its links followed; refused when read-only. */
export async function writeTarget(path: string): Promise<string> {
  let target = path;
  for (let i = 0; ; i++) {
    let link = false;
    try {
      link = (await lstat(target)).isSymbolicLink();
    } catch {
      // not there yet: it is made
    }
    if (!link) {
      break;
    }
    if (i === MAX_LINKS) {
      throw new WriteError(`too many links: ${path}`);
    }
    const to = await readlink(target);
    target = isAbsolute(to) ? to : join(dirname(target), to);
  }
  let exists = true;
  try {
    await stat(target);
  } catch {
    exists = false;
  }
  if (exists) {
    try {
      await access(target, constants.W_OK);
    } catch {
      throw new WriteError(`${basename(path)} is read-only`);
    }
  }
  return target;
}

/** Writes the bytes to the file a path names, beside it first and renamed over it. */
export async function writeFileSafely(path: string, data: Uint8Array): Promise<void> {
  const target = await writeTarget(path);
  // the file keeps who may read and write it (a new file has the defaults)
  const mode = await stat(target).then(
    (s) => s.mode & 0o7777,
    () => undefined,
  );
  try {
    await replaceFile(target, data, mode === undefined ? {} : { mode });
  } catch (e) {
    const what = (e as { syscall?: unknown }).syscall === 'rename' ? 'replace' : 'write';
    throw new WriteError(`could not ${what} ${basename(path)} (${errorCode(e) || String(e)})`);
  }
}

/**
 * The paths each window (by a key: its renderer) may write an export to:
 * those its save dialog gave it, each once, and only with the extension of
 * the kind it was asked for (so no export overwrites a puzzle).
 */
export class ExportGrants<K extends WeakKey> {
  private readonly granted = new WeakMap<K, Map<string, ExportKind>>();
  private readonly extension: (kind: ExportKind) => string;

  constructor(extension: (kind: ExportKind) => string) {
    this.extension = extension;
  }

  /** The dialog gave this path for an export of this kind. */
  grant(key: K, path: string, kind: ExportKind): void {
    let m = this.granted.get(key);
    if (m === undefined) {
      m = new Map();
      this.granted.set(key, m);
    }
    m.set(path, kind);
  }

  /** Takes the grant of a path: its kind, or null when none (or not the kind's extension). */
  take(key: K, path: string): ExportKind | null {
    const m = this.granted.get(key);
    const kind = m?.get(path);
    if (m === undefined || kind === undefined) {
      return null;
    }
    m.delete(path);
    return extname(path).toLowerCase() === this.extension(kind) ? kind : null;
  }
}

/**
 * Whether a name is one a solution's STL export may write into its folder:
 * what the engine's export.solutionPieces names files (ASCII letters,
 * digits, '-' and '_', then ".stl"), so it cannot leave the folder or
 * name another kind of file.
 */
export function isPieceFileName(name: string): boolean {
  return name.length <= 255 && /^[A-Za-z0-9_-]+\.stl$/.test(name);
}

/**
 * The folder each window (by a key: its renderer) may write a solution's
 * STL files into (deferred part 5): the one its folder dialog gave last.
 */
export class FolderGrants<K extends WeakKey> {
  private readonly granted = new WeakMap<K, string>();

  /** The folder dialog gave this folder (replacing the one before). */
  grant(key: K, folder: string): void {
    this.granted.set(key, folder);
  }

  /** Whether the window may write into this folder. */
  has(key: K, folder: string): boolean {
    return this.granted.get(key) === folder;
  }
}
