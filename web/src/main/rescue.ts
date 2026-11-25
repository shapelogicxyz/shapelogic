// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rescue copies (stage 6), as the original's `__rescue.xmpuzzle`: the
// document's file, written by the window's engine itself
// (document.saveRescue, gzip; cleanup item 10.11: the document never
// crosses IPC; plain XML from earlier versions, which opens as well), kept
// in the app's data directory while the document has unsaved changes,
// with a record of what it stands for (`__rescue.json`: the document's
// file, its name, the revision, the time, and a hash of the copy). After
// an engine's death, or at the next start after the app itself died, the
// renderer offers it, and the engine opens the copy by its path
// (document.open with `as` and `dirty`). The engine replaces the copy
// whole (a temporary file beside it, synced, renamed), then the record is
// replaced through replace-file.ts: a crash between the two leaves a
// record whose hash does not match the copy, and such a copy is offered as
// a puzzle without a file (its record may name another document's file,
// which a save must never overwrite). A store's writes and clears run one
// after the other, so a clear is never undone by a write before it.
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
import { mkdirSync, readdirSync } from 'node:fs';
import { readFile, rm } from 'node:fs/promises';
import { join } from 'node:path';

import { isRescueInfo } from '../engine/app-bridge.ts';
import type { RescueCopy, RescueInfo } from '../engine/app-bridge.ts';
import { replaceFile } from './replace-file.ts';

const sha = (bytes: Uint8Array): string => createHash('sha256').update(bytes).digest('hex');

/** A slot's file names without their extension: `__rescue`, `__rescue-1`, …. */
const stem = (slot: number): string => (slot === 0 ? '__rescue' : `__rescue-${String(slot)}`);

/**
 * The slot a rescue file is of, or null when it is not one: the copy, its
 * record, and their temporary files (replace-file.ts's, or the `.tmp` of
 * the versions before it).
 */
export function rescueSlot(name: string): number | null {
  const m =
    /^__rescue(?:-([1-9]\d{0,5}))?\.(?:xmpuzzle|json)(?:(?:\.\d+\.[0-9a-f]{12})?\.tmp)?$/.exec(
      name,
    );
  return m === null ? null : m[1] === undefined ? 0 : Number(m[1]);
}

export class RescueStore {
  /** The copy, `__rescue.xmpuzzle` (`__rescue-n.xmpuzzle` for slot n). */
  readonly file: string;
  /** Its record, `__rescue.json` (`__rescue-n.json`). */
  readonly record: string;
  readonly slot: number;
  private readonly dir: string;
  /** The writes and clears, one after the other. */
  private chain: Promise<void> = Promise.resolve();

  constructor(dir: string, slot = 0) {
    this.dir = dir;
    this.slot = slot;
    this.file = join(dir, `${stem(slot)}.xmpuzzle`);
    this.record = join(dir, `${stem(slot)}.json`);
  }

  /**
   * Writes the copy and its record: `save` has the engine write the copy to
   * the file it is given (document.saveRescue) and answers the revision it
   * holds. `save` is called at once, before this returns (the caller's
   * engine call then goes ahead of any later call of the window); the
   * record follows once the copy is whole.
   */
  write(save: (file: string) => Promise<number>, info: RescueInfo): Promise<void> {
    // the engine writes into the folder, which must be there first
    mkdirSync(this.dir, { recursive: true });
    const saved = save(this.file);
    // a failure is this call's, never the chain's (nor unhandled)
    saved.catch(() => undefined);
    return this.then(async () => {
      const revision = await saved;
      const bytes = await readFile(this.file);
      await replaceFile(this.record, JSON.stringify({ ...info, revision, sha256: sha(bytes) }));
    });
  }

  /** The copy (its file, which the engine opens) and its record, or null when there is none. */
  async read(): Promise<RescueCopy | null> {
    let bytes: Buffer;
    try {
      bytes = await readFile(this.file);
    } catch {
      return null;
    }
    let info: RescueInfo = { path: null, name: 'Rescued puzzle', revision: 0, time: 0 };
    try {
      const v: unknown = JSON.parse(await readFile(this.record, 'utf8'));
      if (
        isRescueInfo(v) &&
        (v as unknown as Record<string, unknown>).sha256 === sha(new Uint8Array(bytes))
      ) {
        info = { path: v.path, name: v.name, revision: v.revision, time: v.time };
      }
    } catch {
      // no record, or not ours: a copy of an unknown puzzle
    }
    return { file: this.file, info };
  }

  /** Removes the copy and its record. */
  clear(): Promise<void> {
    return this.then(async () => {
      await rm(this.file, { force: true });
      await rm(this.record, { force: true });
    });
  }

  /** Runs `step` after the writes and clears before it; a failure is its own caller's. */
  private then(step: () => Promise<void>): Promise<void> {
    const p = this.chain.then(step);
    this.chain = p.catch(() => undefined);
    return p;
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
