// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writing an exported file (stage 7: the image, the vector image, the STL
// and the animation), the way the core writes a puzzle (core/bt/io/
// gzip_file.cpp: saveTarget and replaceWith, since the web stage 6 audit):
// the bytes go to `<file>.tmp` beside the file and are renamed over it
// once whole, so a failed write leaves the old file; the place is the file
// the path names, a symbolic link followed (at most 40), so the link
// stays; a read-only file is refused, where a rename would replace it; the
// new file gets the permissions of the one it replaces; the temporary file
// is this write's own, made exclusively, never one already at that name
// (the web stage 8 audit: a link planted there carried the bytes to the
// file it named). And which paths a window may have written
// (`ExportGrants`): those its save dialog gave it for an export, once each;
// and from deferred part 5 the folder its folder dialog gave it for a
// solution's STL files (`FolderGrants`), files there named only as the
// engine names them (`isPieceFileName`). Free of Electron (the unit tests
// use it).

import {
  access,
  chmod,
  constants,
  lstat,
  readlink,
  rename,
  rm,
  stat,
  writeFile,
} from 'node:fs/promises';
import { basename, dirname, extname, isAbsolute, join } from 'node:path';

import type { ExportKind } from '../engine/app-bridge.ts';

/** As the core's kMaxLinks. */
const MAX_LINKS = 40;

/** Why a file could not be written, in words for the user. */
export class WriteError extends Error {
  override name = 'WriteError';
}

function code(e: unknown): string {
  return typeof e === 'object' && e !== null && 'code' in e ? String(e.code) : '';
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
  const tmp = `${target}.tmp`;
  try {
    // a file or link left at the temporary file's name (a write that died, or
    // a link put there so that the bytes would land in the file it names) is
    // removed, and the new file is made exclusively, so a link put there
    // meanwhile is refused rather than followed; a directory there stays and
    // fails the write
    const old = await lstat(tmp).catch(() => null);
    if (old !== null && (old.isSymbolicLink() || old.isFile())) {
      await rm(tmp, { force: true });
    }
    await writeFile(tmp, data, { flag: 'wx' });
  } catch (e) {
    await rm(tmp, { force: true }).catch(() => undefined);
    throw new WriteError(`could not write ${basename(path)} (${code(e) || String(e)})`);
  }
  // the file keeps who may read and write it (a new file has the defaults)
  try {
    const old = await stat(target);
    await chmod(tmp, old.mode & 0o7777).catch(() => undefined);
  } catch {
    // a new file
  }
  try {
    await rename(tmp, target);
  } catch (e) {
    await rm(tmp, { force: true }).catch(() => undefined);
    throw new WriteError(`could not replace ${basename(path)} (${code(e) || String(e)})`);
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
