// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cache folder (Settings ▸ General ▸ Cache folder): where the engine's
// sliding searches keep what outgrows their memory and their saved
// searches (`slideFolder`), and where stacking searches save themselves
// (`panexFolder`). By default both are in the system's application data
// folder for ShapeLogic (`appDataDir`), as `sliding` and `panex`; a folder
// the user chooses holds both, in a `ShapeLogic` folder made inside it.
// Before, the defaults were `sliding` in the system's cache folder (the
// engine's own default: core/shapelogic/base/system.cpp,
// userCacheDirectory) and `panex` in Electron's data directory (deferred
// part 4): what those hold is moved to the new defaults at the first start
// that uses them.
//
// Choosing one (`CacheFolders`) makes that folder at once and writes a
// file in it to prove it can; a folder that cannot be written is refused
// and the folders in use stay. What the folders in use hold is then
// described, so the user can move it to the new folder or delete it and
// start clean. Never while a solve runs: its search writes the folders.
// Free of Electron (the directories and the engines are given), so the
// unit tests run it in a temporary directory.

import { existsSync } from 'node:fs';
import { cp, lstat, mkdir, readdir, rename, rm, rmdir, writeFile } from 'node:fs/promises';
import { isAbsolute, join, relative, resolve } from 'node:path';

import type {
  CacheAction,
  CacheChoice,
  CacheFolderInfo,
  CacheResult,
} from '../engine/app-bridge.ts';

/** The folder made inside the folder the user chooses. */
export const CACHE_NAME = 'ShapeLogic';

/** The engine's two folders. */
export interface CacheFolders {
  /** Sliding searches (settings.set slideFolder); '' when there is none. */
  slide: string;
  /** Saved stacking searches (settings.set panexFolder). */
  panex: string;
}

/**
 * The system's cache folder for ShapeLogic, as the engine finds it
 * (userCacheDirectory): '' when it cannot be found.
 */
export function systemCacheDir(
  platform: string,
  env: Readonly<Record<string, string | undefined>>,
): string {
  const set = (v: string | undefined): v is string => v !== undefined && v !== '';
  if (platform === 'win32') {
    return set(env.LOCALAPPDATA) ? `${env.LOCALAPPDATA}\\ShapeLogic` : '';
  }
  if (platform === 'darwin') {
    return set(env.HOME) ? `${env.HOME}/Library/Caches/ShapeLogic` : '';
  }
  if (set(env.XDG_CACHE_HOME)) {
    return `${env.XDG_CACHE_HOME}/shapelogic`;
  }
  return set(env.HOME) ? `${env.HOME}/.cache/shapelogic` : '';
}

/**
 * The system's application data folder for ShapeLogic: '' when it cannot
 * be found. On Windows the local one, not the roaming one Electron's
 * appData is, which a roaming profile copies between computers; on Linux
 * the XDG data folder, not the configuration folder Electron's is.
 */
export function appDataDir(
  platform: string,
  env: Readonly<Record<string, string | undefined>>,
): string {
  const set = (v: string | undefined): v is string => v !== undefined && v !== '';
  if (platform === 'win32') {
    return set(env.LOCALAPPDATA) ? `${env.LOCALAPPDATA}\\ShapeLogic` : '';
  }
  if (platform === 'darwin') {
    return set(env.HOME) ? `${env.HOME}/Library/Application Support/ShapeLogic` : '';
  }
  if (set(env.XDG_DATA_HOME)) {
    return `${env.XDG_DATA_HOME}/shapelogic`;
  }
  return set(env.HOME) ? `${env.HOME}/.local/share/shapelogic` : '';
}

/** Where the defaults are. */
export interface CacheDefaults {
  /** The folder of the default `sliding` and `panex` (appDataDir's answer). */
  dataDir: string;
  /** Where earlier versions kept them by default: moved to the defaults at the start. */
  legacy?: CacheFolders;
}

/** The folders of a chosen folder, or the defaults when none is chosen. */
export function foldersFor(root: string | undefined, d: CacheDefaults): CacheFolders {
  const base = root === undefined ? d.dataDir : join(root, CACHE_NAME);
  return { slide: join(base, 'sliding'), panex: join(base, 'panex') };
}

const errCode = (e: unknown): string =>
  typeof e === 'object' && e !== null && 'code' in e ? String(e.code) : '';

/** Why a folder could not be written, in words for the user. */
function writeError(e: unknown): string {
  switch (errCode(e)) {
    case 'EACCES':
    case 'EPERM':
      return 'ShapeLogic does not have permission to write there';
    case 'EROFS':
      return 'the disk is read-only';
    case 'ENOSPC':
      return 'the disk is full';
    case 'ENOTDIR':
    case 'EEXIST':
      return 'a file is in the way of the folder';
    case 'ENOENT':
      return 'the folder is no longer there';
    default:
      return e instanceof Error ? e.message : String(e);
  }
}

/** The folders that `prepare` made: what `undoPrepare` takes away again. */
export type Made = readonly string[];

/**
 * Makes a chosen folder's `ShapeLogic` folder and the two folders in it,
 * and writes and removes a file in each: what it made, or why it could not
 * (what it made is taken away again then).
 */
export async function prepare(
  root: string,
): Promise<{ ok: true; made: Made } | { ok: false; error: string }> {
  const base = join(root, CACHE_NAME);
  const made = [base, join(base, 'sliding'), join(base, 'panex')].filter((p) => !existsSync(p));
  try {
    for (const dir of [join(base, 'sliding'), join(base, 'panex')]) {
      await mkdir(dir, { recursive: true });
      const probe = join(dir, `.write-test-${String(process.pid)}`);
      await writeFile(probe, 'ShapeLogic', { flag: 'wx' });
      await rm(probe, { force: true });
    }
    return { ok: true, made };
  } catch (e) {
    await undoPrepare(made);
    return { ok: false, error: writeError(e) };
  }
}

/** Takes away the folders `prepare` made, those that are still empty. */
export async function undoPrepare(made: Made): Promise<void> {
  // the deepest first: a folder goes only once what it held went
  for (const dir of [...made].reverse()) {
    await rmdir(dir).catch(() => undefined);
  }
}

/** Whether `inner` is `outer` or inside it. */
export function within(inner: string, outer: string): boolean {
  if (inner === '' || outer === '') {
    return false;
  }
  const r = relative(resolve(outer), resolve(inner));
  return r === '' || (!r.startsWith('..') && !isAbsolute(r));
}

/** Whether two sets of folders are the same. */
export const sameFolders = (a: CacheFolders, b: CacheFolders): boolean =>
  resolve(a.slide || '.') === resolve(b.slide || '.') && resolve(a.panex) === resolve(b.panex);

/**
 * Why data cannot move from one set of folders to another (one inside the
 * other), or null when it can.
 */
export function overlap(from: CacheFolders, to: CacheFolders): string | null {
  const a = [from.slide, from.panex].filter((p) => p !== '');
  const b = [to.slide, to.panex].filter((p) => p !== '');
  for (const x of a) {
    for (const y of b) {
      if (within(x, y) || within(y, x)) {
        return 'it is inside the cache folder in use, or holds it';
      }
    }
  }
  return null;
}

/** What one of the folders holds. */
export interface FolderContents {
  /** The saved searches in it (a folder each). */
  searches: number;
  /** Everything in it: the searches' folders and anything else. */
  entries: number;
  bytes: number;
}

/** What the folders in use hold. */
export interface CacheContents {
  sliding: FolderContents;
  stacking: FolderContents;
}

/** The bytes of a file, or of a folder and everything in it (links not followed). */
async function sizeOf(path: string): Promise<number> {
  let s;
  try {
    s = await lstat(path);
  } catch {
    return 0;
  }
  if (!s.isDirectory()) {
    return s.size;
  }
  let total = 0;
  for (const name of await readdir(path).catch(() => [])) {
    total += await sizeOf(join(path, name));
  }
  return total;
}

/** The names in a folder; none when it is not there. */
const namesIn = async (dir: string): Promise<string[]> =>
  dir === '' ? [] : readdir(dir).catch(() => []);

/** What a folder holds; `search` says which names are a saved search's folder. */
async function contentsOf(dir: string, search: RegExp): Promise<FolderContents> {
  const names = await namesIn(dir);
  let bytes = 0;
  let searches = 0;
  for (const name of names) {
    const p = join(dir, name);
    bytes += await sizeOf(p);
    if (search.test(name)) {
      const s = await lstat(p).catch(() => null);
      if (s?.isDirectory() === true) {
        searches++;
      }
    }
  }
  return { searches, entries: names.length, bytes };
}

/** What the folders hold: the engine names a sliding search `slide-<key>` and a stacking one by its key. */
export async function scan(f: CacheFolders): Promise<CacheContents> {
  return {
    sliding: await contentsOf(f.slide, /^slide-[0-9a-f]{16}$/),
    stacking: await contentsOf(f.panex, /^[0-9a-f]{16}$/),
  };
}

/** Whether the folders hold nothing. */
export const isEmpty = (c: CacheContents): boolean =>
  c.sliding.entries === 0 && c.stacking.entries === 0;

/** A size as the settings show it. */
export function sizeText(bytes: number): string {
  const units = ['bytes', 'KB', 'MB', 'GB', 'TB'];
  let v = bytes;
  let u = 0;
  while (v >= 1000 && u < units.length - 1) {
    v /= 1000;
    u++;
  }
  return u === 0
    ? `${String(v)} bytes`
    : `${v < 10 ? v.toFixed(1) : String(Math.round(v))} ${units[u] ?? ''}`;
}

/** One folder's contents in words, or null when it holds nothing. */
function folderText(c: FolderContents, kind: string): string | null {
  if (c.entries === 0) {
    return null;
  }
  const size = sizeText(c.bytes);
  if (c.searches === 0) {
    return `files of earlier ${kind} searches (${size})`;
  }
  const n =
    c.searches === 1 ? `1 saved ${kind} search` : `${String(c.searches)} saved ${kind} searches`;
  return `${n} (${size})`;
}

/** What the folders hold, in words: "2 saved sliding searches (1.2 GB) and 1 saved stacking search (340 MB)". */
export function describe(c: CacheContents): string {
  const parts = [folderText(c.sliding, 'sliding'), folderText(c.stacking, 'stacking')].filter(
    (p): p is string => p !== null,
  );
  return parts.join(' and ');
}

/** Moves a file or folder; across disks by copying it, then removing the old one. */
async function moveEntry(from: string, to: string): Promise<void> {
  await rm(to, { recursive: true, force: true });
  try {
    await rename(from, to);
  } catch (e) {
    if (errCode(e) !== 'EXDEV') {
      throw e;
    }
    try {
      await cp(from, to, { recursive: true, preserveTimestamps: true, verbatimSymlinks: true });
    } catch (copyError) {
      await rm(to, { recursive: true, force: true });
      throw copyError;
    }
    await rm(from, { recursive: true, force: true });
  }
}

/**
 * Moves what the folders in use hold into the new ones (a saved search
 * there of the same name is replaced). When a move fails, what had moved
 * goes back, and the error says why.
 */
export async function moveData(from: CacheFolders, to: CacheFolders): Promise<void> {
  const moved: [string, string][] = [];
  try {
    for (const [a, b] of [
      [from.slide, to.slide],
      [from.panex, to.panex],
    ] as const) {
      // a folder that is already the new one stays as it is
      if (a === '' || b === '' || resolve(a) === resolve(b)) {
        continue;
      }
      const names = await namesIn(a);
      if (names.length === 0) {
        continue;
      }
      await mkdir(b, { recursive: true });
      for (const name of names) {
        await moveEntry(join(a, name), join(b, name));
        moved.push([join(a, name), join(b, name)]);
      }
    }
  } catch (e) {
    for (const [a, b] of moved.reverse()) {
      await moveEntry(b, a).catch(() => undefined);
    }
    throw new Error(`the cache data could not be moved: ${writeError(e)}`, { cause: e });
  }
  // the old folders go once empty (a folder that is a new one too stays)
  for (const dir of [from.slide, from.panex]) {
    if (dir !== '' && dir !== to.slide && dir !== to.panex) {
      await rmdir(dir).catch(() => undefined);
    }
  }
}

/** Deletes everything the folders hold, and the folders. */
export async function purgeData(f: CacheFolders): Promise<void> {
  for (const dir of [f.slide, f.panex]) {
    if (dir !== '') {
      await rm(dir, { recursive: true, force: true });
    }
  }
}

/** What the cache folder needs of the app. */
export interface CacheHost {
  defaults: CacheDefaults;
  /** The folder the user chose, as the store keeps it; undefined for the defaults. */
  stored(): string | undefined;
  /** Stores the folder chosen (undefined: the defaults). */
  store(root: string | undefined): void;
  /** Whether a solve or an analysis runs in any window. */
  solving(): Promise<boolean>;
  /** Gives every running engine these folders. */
  apply(f: CacheFolders): Promise<void>;
}

/** A choice made and written, waiting for what to do with the data. */
interface Pending {
  root: string | undefined;
  folders: CacheFolders;
  made: Made;
}

/** The words of a running solve. */
const SOLVING =
  'A solve is running in a ShapeLogic window. Pause or stop it first: its search writes to the cache folder.';

/** The cache folder in use and the choices of a new one, a choice at a time. */
export class CacheFolderManager<K extends WeakKey> {
  private readonly host: CacheHost;
  private root: string | undefined;
  private notice: string | undefined;
  private ready: Promise<void> | null = null;
  private readonly pending = new WeakMap<K, Pending>();
  /** The window whose choice is under way: another waits for it. */
  private owner: K | null = null;

  constructor(host: CacheHost) {
    this.host = host;
    this.root = host.stored();
  }

  /**
   * Checks once that a folder chosen earlier can still be written; one that
   * cannot is given up for the default, and the first window is told. With
   * the default in use, what the earlier defaults hold moves into it.
   */
  start(): Promise<void> {
    this.ready ??= (async () => {
      const root = this.root;
      if (root === undefined) {
        await this.moveLegacy();
        return;
      }
      const r = await prepare(root);
      if (!r.ok) {
        this.root = undefined;
        this.host.store(undefined);
        this.notice = `The cache folder ${join(root, CACHE_NAME)} can no longer be used (${r.error}). ShapeLogic uses the default cache folder again; choose another in Settings ▸ General.`;
      }
    })();
    return this.ready;
  }

  /** Moves what the earlier versions' default folders hold into the default. */
  private async moveLegacy(): Promise<void> {
    const legacy = this.host.defaults.legacy;
    if (legacy === undefined) {
      return;
    }
    try {
      await moveData(legacy, this.folders);
    } catch (e) {
      this.notice = `The saved searches of the earlier cache folders (${[legacy.slide, legacy.panex].filter((p) => p !== '').join(' and ')}) were left there: ${e instanceof Error ? e.message : String(e)}.`;
    }
  }

  /** The folder the cache folder in use was made in; undefined for the defaults. */
  get chosen(): string | undefined {
    return this.root;
  }

  /** The folders in use. */
  get folders(): CacheFolders {
    return foldersFor(this.root, this.host.defaults);
  }

  /** The folders in use for the settings, with the start's notice the first time. */
  async info(): Promise<CacheFolderInfo> {
    await this.start();
    const f = this.folders;
    const info: CacheFolderInfo = {
      custom: this.root === undefined ? null : join(this.root, CACHE_NAME),
      defaultFolder: this.host.defaults.dataDir,
      slide: f.slide,
      panex: f.panex,
    };
    if (this.notice !== undefined) {
      info.notice = this.notice;
      this.notice = undefined;
    }
    return info;
  }

  /**
   * A new folder (`root`; undefined the defaults): made and written at
   * once; a ready choice waits for `finish` from the same window.
   */
  async choose(key: K, root: string | undefined): Promise<CacheChoice> {
    await this.start();
    if (this.owner !== null && this.owner !== key) {
      return { status: 'busy', reason: 'The cache folder is being changed in another window.' };
    }
    if (await this.host.solving()) {
      return { status: 'busy', reason: SOLVING };
    }
    const to = foldersFor(root, this.host.defaults);
    const from = this.folders;
    if (sameFolders(from, to)) {
      return { status: 'same' };
    }
    const folder = root === undefined ? this.host.defaults.dataDir : join(root, CACHE_NAME);
    const clash = overlap(from, to);
    if (clash !== null) {
      return { status: 'invalid', folder, error: clash };
    }
    let made: Made = [];
    if (root !== undefined) {
      const r = await prepare(root);
      if (!r.ok) {
        return { status: 'invalid', folder, error: r.error };
      }
      made = r.made;
    }
    const old = this.pending.get(key);
    if (old !== undefined) {
      await undoPrepare(old.made);
    }
    this.pending.set(key, { root, folders: to, made });
    this.owner = key;
    return { status: 'ready', folder, found: describe(await scan(from)) };
  }

  /** Takes a window's ready choice: the data moved or deleted, or (cancel) the choice given up. */
  async finish(key: K, action: CacheAction): Promise<CacheResult> {
    const p = this.pending.get(key);
    if (p === undefined) {
      return { ok: false, error: 'no cache folder was chosen' };
    }
    this.pending.delete(key);
    try {
      if (action === 'cancel') {
        await undoPrepare(p.made);
        return { ok: true, info: await this.info() };
      }
      if (await this.host.solving()) {
        await undoPrepare(p.made);
        return { ok: false, error: SOLVING };
      }
      const from = this.folders;
      const oldRoot = this.root;
      if (action === 'move') {
        try {
          await moveData(from, p.folders);
        } catch (e) {
          await undoPrepare(p.made);
          return { ok: false, error: e instanceof Error ? e.message : String(e) };
        }
      }
      this.root = p.root;
      this.host.store(p.root);
      await this.host.apply(p.folders);
      if (action === 'purge') {
        await purgeData(from).catch(() => undefined);
      }
      if (oldRoot !== undefined) {
        // the old chosen folder's own ShapeLogic folder goes once empty
        await rmdir(join(oldRoot, CACHE_NAME)).catch(() => undefined);
      }
      return { ok: true, info: await this.info() };
    } finally {
      this.owner = null;
    }
  }

  /** A window closed: its choice, if one waits, is given up. */
  async forget(key: K): Promise<void> {
    const p = this.pending.get(key);
    this.pending.delete(key);
    if (this.owner === key) {
      this.owner = null;
    }
    if (p !== undefined) {
      await undoPrepare(p.made);
    }
  }
}
