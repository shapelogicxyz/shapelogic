// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cache folder (src/main/cache-folder.ts): the default folders, a
// chosen folder made and written at once (and refused when it cannot be),
// what the folders in use hold, moving it or deleting it, and the choice
// from the dialog to the folders in use, in a temporary directory.

import {
  chmodSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  CACHE_NAME,
  CacheFolderManager,
  describe as describeContents,
  foldersFor,
  moveData,
  overlap,
  prepare,
  purgeData,
  scan,
  sizeText,
  appDataDir,
  systemCacheDir,
} from '../../src/main/cache-folder.ts';
import type { CacheFolders, CacheHost } from '../../src/main/cache-folder.ts';

let dir: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), 'bt-cache-'));
});

afterEach(() => {
  chmodSync(dir, 0o755);
  rmSync(dir, { recursive: true, force: true });
});

/** A saved search: a folder of the engine's name with a file of `bytes` bytes. */
function search(folder: string, name: string, bytes: number): void {
  mkdirSync(join(folder, name), { recursive: true });
  writeFileSync(join(folder, name, 'state'), 'x'.repeat(bytes));
}

const KEY_A = '0123456789abcdef';
const KEY_B = 'fedcba9876543210';

/** Whether this process ignores a folder's permissions (root). */
const ignoresPermissions = (): boolean => process.getuid?.() === 0 || process.platform === 'win32';

describe('the default folders', () => {
  it('are in the system’s application data folder (local on Windows, XDG data on Linux)', () => {
    expect(appDataDir('darwin', { HOME: '/Users/u' })).toBe(
      '/Users/u/Library/Application Support/ShapeLogic',
    );
    expect(appDataDir('win32', { LOCALAPPDATA: 'C:\\Users\\u\\AppData\\Local' })).toBe(
      'C:\\Users\\u\\AppData\\Local\\ShapeLogic',
    );
    expect(appDataDir('linux', { HOME: '/home/u', XDG_DATA_HOME: '/d' })).toBe('/d/shapelogic');
    expect(appDataDir('linux', { HOME: '/home/u' })).toBe('/home/u/.local/share/shapelogic');
    expect(appDataDir('darwin', {})).toBe('');
  });

  it('were found before as the engine finds the system cache folder', () => {
    expect(systemCacheDir('darwin', { HOME: '/Users/u' })).toBe(
      '/Users/u/Library/Caches/ShapeLogic',
    );
    expect(systemCacheDir('win32', { LOCALAPPDATA: 'C:\\Users\\u\\AppData\\Local' })).toBe(
      'C:\\Users\\u\\AppData\\Local\\ShapeLogic',
    );
    expect(systemCacheDir('linux', { HOME: '/home/u', XDG_CACHE_HOME: '/c' })).toBe(
      '/c/shapelogic',
    );
    expect(systemCacheDir('linux', { HOME: '/home/u', XDG_CACHE_HOME: '' })).toBe(
      '/home/u/.cache/shapelogic',
    );
    expect(systemCacheDir('darwin', {})).toBe('');
  });

  it('hold sliding and panex, as a chosen folder’s ShapeLogic folder does', () => {
    const d = { dataDir: '/data/ShapeLogic' };
    expect(foldersFor(undefined, d)).toEqual({
      slide: join('/data/ShapeLogic', 'sliding'),
      panex: join('/data/ShapeLogic', 'panex'),
    });
    expect(foldersFor('/big', d)).toEqual({
      slide: join('/big', CACHE_NAME, 'sliding'),
      panex: join('/big', CACHE_NAME, 'panex'),
    });
  });
});

describe('a chosen folder', () => {
  it('is made and written at once, leaving no file behind', async () => {
    const r = await prepare(dir);
    expect(r.ok).toBe(true);
    for (const sub of ['sliding', 'panex']) {
      expect(existsSync(join(dir, CACHE_NAME, sub))).toBe(true);
    }
    expect(readdirSync(join(dir, CACHE_NAME, 'sliding'))).toEqual([]);
  });

  it.skipIf(ignoresPermissions())(
    'is refused when it cannot be written, and nothing stays',
    async () => {
      chmodSync(dir, 0o555);
      const r = await prepare(dir);
      expect(r).toEqual({ ok: false, error: 'ShapeLogic does not have permission to write there' });
      chmodSync(dir, 0o755);
      expect(existsSync(join(dir, CACHE_NAME))).toBe(false);
    },
  );

  it('may not be inside the folders in use, nor hold them', () => {
    const from: CacheFolders = { slide: join(dir, 'a', 'sliding'), panex: join(dir, 'a', 'panex') };
    expect(overlap(from, foldersFor(join(dir, 'b'), { dataDir: '' }))).toBeNull();
    expect(overlap(from, foldersFor(join(dir, 'a', 'sliding'), { dataDir: '' }))).not.toBeNull();
  });
});

describe('the data in the folders', () => {
  it('is counted and described', async () => {
    const f: CacheFolders = { slide: join(dir, 'sliding'), panex: join(dir, 'panex') };
    expect(describeContents(await scan(f))).toBe('');
    search(f.slide, `slide-${KEY_A}`, 1500);
    search(f.slide, `slide-${KEY_B}`, 500);
    search(f.panex, KEY_A, 2_500_000);
    expect(describeContents(await scan(f))).toBe(
      '2 saved sliding searches (2.0 KB) and 1 saved stacking search (2.5 MB)',
    );
    expect(sizeText(999)).toBe('999 bytes');
    expect(sizeText(340e6)).toBe('340 MB');
  });

  it('moves to the new folders, replacing a search of the same name there', async () => {
    const from: CacheFolders = {
      slide: join(dir, 'old', 'sliding'),
      panex: join(dir, 'old', 'panex'),
    };
    const to: CacheFolders = {
      slide: join(dir, 'new', 'sliding'),
      panex: join(dir, 'new', 'panex'),
    };
    search(from.slide, `slide-${KEY_A}`, 10);
    search(from.panex, KEY_B, 20);
    search(to.panex, KEY_B, 5);
    await moveData(from, to);
    expect(readFileSync(join(to.slide, `slide-${KEY_A}`, 'state'), 'utf8')).toHaveLength(10);
    expect(readFileSync(join(to.panex, KEY_B, 'state'), 'utf8')).toHaveLength(20);
    expect(existsSync(from.slide)).toBe(false);
    expect(existsSync(from.panex)).toBe(false);
  });

  it('is deleted with its folders', async () => {
    const f: CacheFolders = { slide: join(dir, 'sliding'), panex: join(dir, 'panex') };
    search(f.slide, `slide-${KEY_A}`, 10);
    search(f.panex, KEY_A, 10);
    await purgeData(f);
    expect(existsSync(f.slide)).toBe(false);
    expect(existsSync(f.panex)).toBe(false);
  });
});

/** A host in the temporary directory: what it stored, applied and whether a solve runs. */
function host(
  stored?: string,
  legacy?: CacheFolders,
): CacheHost & {
  root: string | undefined;
  applied: CacheFolders[];
  running: boolean;
} {
  const h = {
    defaults: { dataDir: join(dir, 'data'), ...(legacy === undefined ? {} : { legacy }) },
    root: stored,
    applied: [] as CacheFolders[],
    running: false,
    stored: () => h.root,
    store: (root: string | undefined) => {
      h.root = root;
    },
    solving: () => Promise.resolve(h.running),
    apply: (f: CacheFolders) => {
      h.applied.push(f);
      return Promise.resolve();
    },
  };
  return h;
}

describe('choosing a cache folder', () => {
  it('says what the folders in use hold, then moves it there', async () => {
    const h = host();
    const m = new CacheFolderManager<object>(h);
    const win = {};
    search(join(dir, 'data', 'sliding'), `slide-${KEY_A}`, 100);
    const big = join(dir, 'big');
    mkdirSync(big);
    const c = await m.choose(win, big);
    expect(c).toEqual({
      status: 'ready',
      folder: join(big, CACHE_NAME),
      found: '1 saved sliding search (100 bytes)',
    });
    // made at once, before the answer
    expect(existsSync(join(big, CACHE_NAME, 'panex'))).toBe(true);
    const r = await m.finish(win, 'move');
    expect(r.ok).toBe(true);
    expect(h.root).toBe(big);
    expect(h.applied).toEqual([foldersFor(big, h.defaults)]);
    expect(existsSync(join(big, CACHE_NAME, 'sliding', `slide-${KEY_A}`, 'state'))).toBe(true);
    expect(existsSync(join(dir, 'data', 'sliding', `slide-${KEY_A}`))).toBe(false);
    expect(await m.choose(win, big)).toEqual({ status: 'same' });
  });

  it('deletes the data and starts clean, or, cancelled, keeps everything as it was', async () => {
    const h = host();
    const m = new CacheFolderManager<object>(h);
    const win = {};
    search(join(dir, 'data', 'panex'), KEY_A, 10);
    const big = join(dir, 'big');
    mkdirSync(big);
    await m.choose(win, big);
    await m.finish(win, 'cancel');
    expect(h.root).toBeUndefined();
    expect(existsSync(join(big, CACHE_NAME))).toBe(false);
    expect(existsSync(join(dir, 'data', 'panex', KEY_A))).toBe(true);
    await m.choose(win, big);
    await m.finish(win, 'purge');
    expect(h.root).toBe(big);
    expect(existsSync(join(dir, 'data', 'panex'))).toBe(false);
    // back to the defaults: the chosen folder's ShapeLogic folder goes once empty
    expect((await m.choose(win, undefined)).status).toBe('ready');
    await m.finish(win, 'move');
    expect(h.root).toBeUndefined();
    expect(existsSync(join(big, CACHE_NAME))).toBe(false);
  });

  it('waits while a solve runs, and while another window chooses', async () => {
    const h = host();
    const m = new CacheFolderManager<object>(h);
    const a = {};
    const b = {};
    h.running = true;
    expect((await m.choose(a, dir)).status).toBe('busy');
    h.running = false;
    expect((await m.choose(a, dir)).status).toBe('ready');
    expect((await m.choose(b, dir)).status).toBe('busy');
    await m.forget(a);
    expect((await m.choose(b, dir)).status).toBe('ready');
  });

  it.skipIf(ignoresPermissions())(
    'refuses a folder it cannot write, and keeps the folders in use',
    async () => {
      const h = host();
      const m = new CacheFolderManager<object>(h);
      const locked = join(dir, 'locked');
      mkdirSync(locked, { mode: 0o555 });
      expect(await m.choose({}, locked)).toEqual({
        status: 'invalid',
        folder: join(locked, CACHE_NAME),
        error: 'ShapeLogic does not have permission to write there',
      });
      expect(h.root).toBeUndefined();
      expect(h.applied).toEqual([]);
      chmodSync(locked, 0o755);
    },
  );

  it.skipIf(ignoresPermissions())(
    'goes back to the defaults when a folder chosen earlier cannot be written, and says so once',
    async () => {
      const locked = join(dir, 'locked');
      mkdirSync(locked, { mode: 0o555 });
      const h = host(locked);
      const m = new CacheFolderManager<object>(h);
      const first = await m.info();
      expect(first.custom).toBeNull();
      expect(first.notice).toContain(join(locked, CACHE_NAME));
      expect(h.root).toBeUndefined();
      expect((await m.info()).notice).toBeUndefined();
      chmodSync(locked, 0o755);
    },
  );

  it('moves what the earlier default folders hold into the default at the start', async () => {
    // as on a Mac: sliding was in the cache folder, panex already in the data folder
    const legacy: CacheFolders = {
      slide: join(dir, 'syscache', 'sliding'),
      panex: join(dir, 'data', 'panex'),
    };
    search(legacy.slide, `slide-${KEY_A}`, 10);
    search(legacy.panex, KEY_B, 10);
    const m = new CacheFolderManager<object>(host(undefined, legacy));
    const info = await m.info();
    expect(info.notice).toBeUndefined();
    expect(info.defaultFolder).toBe(join(dir, 'data'));
    expect(existsSync(join(dir, 'data', 'sliding', `slide-${KEY_A}`, 'state'))).toBe(true);
    expect(existsSync(join(dir, 'data', 'panex', KEY_B, 'state'))).toBe(true);
    expect(existsSync(legacy.slide)).toBe(false);
  });
});
