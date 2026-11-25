// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rescue copies (stage 6), as the original's `__rescue.xmpuzzle`: the
// document's bytes as the engine writes them (document.saveBytes, gzip as
// in a saved .xmpuzzle; plain XML from earlier versions, which opens as
// well), kept in the app's data directory while the document has unsaved
// changes, with a record of what
// it stands for (`__rescue.json`: the document's file, its name, the
// revision, the time, and a hash of the bytes). After an engine's death,
// or at the next start after the app itself died, the renderer offers it.
// Each file is written to a temporary one and renamed, the record first:
// a crash between the two leaves a record whose hash does not match the
// copy, and such a copy is offered as a puzzle without a file (its record
// may name another document's file, which a save must never overwrite).
//
// One copy per window (decision 0036): each window takes a slot as it
// opens and keeps it while it lives (slot 0 is `__rescue.xmpuzzle`, the
// name of the one copy the earlier versions kept; slot n is
// `__rescue-n.xmpuzzle`). A new window takes the lowest free slot that
// holds a copy no window has (left by a session that died, or by a window
// that closed with its copy not answered), so it offers that copy, each
// one in one window only; else the lowest free slot. Free of Electron (the
// directory is given).

import { createHash } from 'node:crypto';
import { mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { isRescueInfo } from '../engine/app-bridge.ts';
import type { RescueInfo } from '../engine/app-bridge.ts';

const sha = (bytes: Uint8Array): string => createHash('sha256').update(bytes).digest('hex');

/** A slot's file names without their extension: `__rescue`, `__rescue-1`, …. */
const stem = (slot: number): string => (slot === 0 ? '__rescue' : `__rescue-${String(slot)}`);

/** The slot a rescue file is of, or null when it is not one. */
export function rescueSlot(name: string): number | null {
  const m = /^__rescue(?:-([1-9]\d{0,5}))?\.(?:xmpuzzle|json)(?:\.tmp)?$/.exec(name);
  return m === null ? null : m[1] === undefined ? 0 : Number(m[1]);
}

export class RescueStore {
  /** The copy, `__rescue.xmpuzzle` (`__rescue-n.xmpuzzle` for slot n). */
  readonly file: string;
  /** Its record, `__rescue.json` (`__rescue-n.json`). */
  readonly record: string;
  readonly slot: number;
  private readonly dir: string;

  constructor(dir: string, slot = 0) {
    this.dir = dir;
    this.slot = slot;
    this.file = join(dir, `${stem(slot)}.xmpuzzle`);
    this.record = join(dir, `${stem(slot)}.json`);
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

/** The windows' slots: one rescue copy each. */
export class RescueSlots<K> {
  private readonly dir: string;
  private readonly held = new Map<K, RescueStore>();

  constructor(dir: string) {
    this.dir = dir;
  }

  /** The slots that have files on disk. */
  private onDisk(): Set<number> {
    const out = new Set<number>();
    let names: string[];
    try {
      names = readdirSync(this.dir);
    } catch {
      return out;
    }
    for (const n of names) {
      const s = rescueSlot(n);
      if (s !== null) {
        out.add(s);
      }
    }
    return out;
  }

  /**
   * The window's slot, taken when it has none: the lowest free one with a
   * copy left on disk, else the lowest free one.
   */
  of(window: K): RescueStore {
    const had = this.held.get(window);
    if (had !== undefined) {
      return had;
    }
    const used = new Set([...this.held.values()].map((r) => r.slot));
    const left = [...this.onDisk()].filter((s) => !used.has(s)).sort((a, b) => a - b);
    let slot = left[0];
    if (slot === undefined) {
      slot = 0;
      while (used.has(slot)) {
        slot++;
      }
    }
    const r = new RescueStore(this.dir, slot);
    this.held.set(window, r);
    return r;
  }

  /** The window closed: its slot is free (a copy it left stays, for the next window). */
  release(window: K): void {
    this.held.delete(window);
  }

  /** Every slot with files on disk or held by a window (the cache manager lists them). */
  all(): RescueStore[] {
    const slots = new Set([...this.onDisk(), ...[...this.held.values()].map((r) => r.slot)]);
    return [...slots].sort((a, b) => a - b).map((s) => new RescueStore(this.dir, s));
  }
}
