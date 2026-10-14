// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rescue copy (stage 6), as the original's `__rescue.xmpuzzle`: the
// document's bytes as the engine writes them (document.saveBytes, plain
// XML, which ShapeLogic opens as any puzzle file), kept in the app's data
// directory while the document has unsaved changes, with a record of what
// it stands for (`__rescue.json`: the document's file, its name, the
// revision, the time, and a hash of the bytes). After an engine's death,
// or at the next start after the app itself died, the renderer offers it.
// Each file is written to a temporary one and renamed, the record first:
// a crash between the two leaves a record whose hash does not match the
// copy, and such a copy is offered as a puzzle without a file (its record
// may name another document's file, which a save must never overwrite).
// Free of Electron (the directory is given).

import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { isRescueInfo } from '../engine/app-bridge.ts';
import type { RescueInfo } from '../engine/app-bridge.ts';

const sha = (bytes: Uint8Array): string => createHash('sha256').update(bytes).digest('hex');

export class RescueStore {
  /** The copy, `__rescue.xmpuzzle`. */
  readonly file: string;
  /** Its record, `__rescue.json`. */
  readonly record: string;
  private readonly dir: string;

  constructor(dir: string) {
    this.dir = dir;
    this.file = join(dir, '__rescue.xmpuzzle');
    this.record = join(dir, '__rescue.json');
  }

  /** Writes the copy (the file's bytes, base64) and its record. */
  write(data: string, info: RescueInfo): void {
    const bytes = Buffer.from(data, 'base64');
    mkdirSync(this.dir, { recursive: true });
    const rec = `${this.record}.tmp`;
    writeFileSync(rec, JSON.stringify({ ...info, sha256: sha(bytes) }));
    renameSync(rec, this.record);
    const tmp = `${this.file}.tmp`;
    writeFileSync(tmp, bytes);
    renameSync(tmp, this.file);
  }

  /** The copy and its record, or null when there is no copy. */
  read(): { data: string; info: RescueInfo } | null {
    let bytes: Buffer;
    try {
      bytes = readFileSync(this.file);
    } catch {
      return null;
    }
    let info: RescueInfo = { path: null, name: 'Rescued puzzle', revision: 0, time: 0 };
    try {
      const v: unknown = JSON.parse(readFileSync(this.record, 'utf8'));
      if (
        isRescueInfo(v) &&
        (v as unknown as Record<string, unknown>).sha256 === sha(new Uint8Array(bytes))
      ) {
        info = { path: v.path, name: v.name, revision: v.revision, time: v.time };
      }
    } catch {
      // no record, or not ours: a copy of an unknown puzzle
    }
    return { data: bytes.toString('base64'), info };
  }

  /** Removes the copy and its record. */
  clear(): void {
    rmSync(this.file, { force: true });
    rmSync(this.record, { force: true });
  }
}
