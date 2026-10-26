// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's store (stage 6): the renderer's settings and layouts,
// the recent files, the window's bounds and the folder a file dialog gave
// last, kept in one JSON file in the
// app's data directory (`state.json`). Read once when the app starts (a
// file that is missing or not JSON is an empty store); written a moment
// after a change, whole, to a temporary file renamed over the old one, so
// a crash mid-write leaves the old file. Free of Electron (the directory is
// given), so the unit tests run it in a temporary directory.

import { mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

/** How many recent files are kept. */
export const RECENT_MAX = 10;

/** The window's place on the screen. */
export interface Bounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

interface Data {
  prefs?: Record<string, unknown>;
  layout?: Record<string, unknown>;
  recent?: string[];
  bounds?: Bounds;
  lastDir?: string;
}

const isRecord = (v: unknown): v is Record<string, unknown> =>
  typeof v === 'object' && v !== null && !Array.isArray(v);

function isBounds(v: unknown): v is Bounds {
  return (
    isRecord(v) &&
    ['x', 'y', 'width', 'height'].every((k) => typeof v[k] === 'number' && Number.isFinite(v[k]))
  );
}

/** Reads what a file held, keeping only what is well formed. */
function parse(text: string): Data {
  let v: unknown;
  try {
    v = JSON.parse(text);
  } catch {
    return {};
  }
  if (!isRecord(v)) {
    return {};
  }
  const d: Data = {};
  if (isRecord(v.prefs)) {
    d.prefs = v.prefs;
  }
  if (isRecord(v.layout)) {
    d.layout = v.layout;
  }
  if (Array.isArray(v.recent)) {
    d.recent = v.recent
      .filter((p): p is string => typeof p === 'string' && p !== '')
      .slice(0, RECENT_MAX);
  }
  if (isBounds(v.bounds)) {
    d.bounds = v.bounds;
  }
  if (typeof v.lastDir === 'string' && v.lastDir !== '') {
    d.lastDir = v.lastDir;
  }
  return d;
}

export class StateStore {
  readonly file: string;
  private readonly dir: string;
  private data: Data;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private readonly delayMs: number;
  private readonly listeners = new Set<() => void>();

  /** The store of a directory; `delayMs` is the wait before a change is written. */
  constructor(dir: string, delayMs = 300) {
    this.dir = dir;
    this.file = join(dir, 'state.json');
    this.delayMs = delayMs;
    let text = '';
    try {
      text = readFileSync(this.file, 'utf8');
    } catch {
      // no store yet
    }
    this.data = parse(text);
  }

  get prefs(): Record<string, unknown> | undefined {
    return this.data.prefs;
  }

  get layout(): Record<string, unknown> | undefined {
    return this.data.layout;
  }

  get bounds(): Bounds | undefined {
    return this.data.bounds;
  }

  /** The folder a file dialog gave last (app-ipc.ts starts the next dialog there). */
  get lastDir(): string | undefined {
    return this.data.lastDir;
  }

  /** The recent files, the newest first. */
  get recent(): readonly string[] {
    return this.data.recent ?? [];
  }

  /** Listens to changes of the recent files (the menu shows them). */
  onRecent(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  /** Stores the renderer's settings or layouts. */
  set(key: 'prefs' | 'layout', value: Record<string, unknown>): void {
    this.data[key] = value;
    this.schedule();
  }

  setBounds(bounds: Bounds): void {
    this.data.bounds = bounds;
    this.schedule();
  }

  setLastDir(dir: string): void {
    if (dir !== this.data.lastDir) {
      this.data.lastDir = dir;
      this.schedule();
    }
  }

  /** A file opened or saved goes first in the recent files. */
  addRecent(path: string): void {
    const list = [path, ...this.recent.filter((p) => p !== path)].slice(0, RECENT_MAX);
    this.setRecent(list);
  }

  removeRecent(path: string): void {
    if (this.recent.includes(path)) {
      this.setRecent(this.recent.filter((p) => p !== path));
    }
  }

  clearRecent(): void {
    this.setRecent([]);
  }

  /** Writes now what is waiting (the app quits). */
  flush(): void {
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
      this.write();
    }
  }

  private setRecent(list: string[]): void {
    this.data.recent = list;
    this.schedule();
    for (const l of [...this.listeners]) {
      l();
    }
  }

  private schedule(): void {
    if (this.timer !== null) {
      return;
    }
    this.timer = setTimeout(() => {
      this.timer = null;
      this.write();
    }, this.delayMs);
  }

  private write(): void {
    try {
      mkdirSync(this.dir, { recursive: true });
      const tmp = `${this.file}.tmp`;
      writeFileSync(tmp, JSON.stringify(this.data, null, 1));
      renameSync(tmp, this.file);
    } catch (e) {
      console.error(`could not store the settings in ${this.file}: ${String(e)}`);
    }
  }
}
