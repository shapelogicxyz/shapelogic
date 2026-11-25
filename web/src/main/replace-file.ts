// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one way the main process replaces a file (cleanup item 7.4): the
// bytes go to a temporary file of this write's own beside the target
// (`<name>.<pid>.<random>.tmp`, made exclusively, so a link planted at a
// name is refused rather than followed, and two writes to one target never
// share one), are synced to the disk, and the temporary file is renamed
// over the target; on an error it is removed and the target is as it was.
// Two writes to one target at once both settle, and the target holds one
// of them whole. The state store and the log's trim use the synchronous
// twin: the store's last write is made as the app quits, and the trim must
// not let a line be appended between its read and its rename. Free of
// Electron.

import { randomBytes } from 'node:crypto';
import { closeSync, fsyncSync, openSync, renameSync, rmSync, writeSync } from 'node:fs';
import { open, rename, rm } from 'node:fs/promises';
import type { FileHandle } from 'node:fs/promises';

/** How a file is replaced. */
export interface ReplaceOptions {
  /** The new file's permissions (the export keeps those of the file it replaces). */
  mode?: number;
}

/** A temporary file's name for a write to `path`, this write's own. */
export function tempName(path: string): string {
  return `${path}.${String(process.pid)}.${randomBytes(6).toString('hex')}.tmp`;
}

const TEMP_SUFFIX = /^\.\d+\.[0-9a-f]{12}\.tmp$/;

/** Whether `name` is a temporary file of a write to the file named `base` (in one folder). */
export function isTempOf(name: string, base: string): boolean {
  return name.startsWith(`${base}.`) && TEMP_SUFFIX.test(name.slice(base.length));
}

const bytesOf = (data: string | Uint8Array): Uint8Array =>
  typeof data === 'string' ? Buffer.from(data, 'utf8') : data;

/**
 * Replaces `path` with `data` whole: written beside it, synced, renamed
 * over it. Rejects with the system's error (its `syscall` says which step:
 * `open`, `write`, `fsync` or `rename`); the target is then as it was.
 */
export async function replaceFile(
  path: string,
  data: string | Uint8Array,
  options: ReplaceOptions = {},
): Promise<void> {
  const tmp = tempName(path);
  let fh: FileHandle | null = null;
  try {
    fh = await open(tmp, 'wx');
    await fh.writeFile(bytesOf(data));
    if (options.mode !== undefined) {
      // as the export did before: a mode the system will not take is no failure
      await fh.chmod(options.mode).catch(() => undefined);
    }
    await fh.sync();
    await fh.close();
    fh = null;
    await rename(tmp, path);
  } catch (e) {
    await fh?.close().catch(() => undefined);
    await rm(tmp, { force: true }).catch(() => undefined);
    throw e;
  }
}

/** replaceFile's synchronous twin, for a caller that must finish before it returns. */
export function replaceFileSync(path: string, data: string | Uint8Array): void {
  const tmp = tempName(path);
  let fd: number | null = null;
  try {
    fd = openSync(tmp, 'wx');
    const bytes = bytesOf(data);
    for (let at = 0; at < bytes.length;) {
      at += writeSync(fd, bytes, at, bytes.length - at);
    }
    fsyncSync(fd);
    closeSync(fd);
    fd = null;
    renameSync(tmp, path);
  } catch (e) {
    if (fd !== null) {
      try {
        closeSync(fd);
      } catch {
        // closed already
      }
    }
    try {
      rmSync(tmp, { force: true });
    } catch {
      // the error that matters is the write's
    }
    throw e;
  }
}
