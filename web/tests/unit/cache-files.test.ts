// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cache manager (src/main/cache-files.ts): the search folders and the
// rescue copy as items named after the puzzle a window linked them to,
// the saved searches of one problem's starts as one item, unfinished
// searches and stray files apart, deleting by id (refused for a search
// while a solve runs), the links kept by folder name, and the start's
// sweep of stray files a week old, in a temporary directory.

import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  utimesSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  CacheLinks,
  deleteItem,
  kindOf,
  listItems,
  OTHER_MAX_AGE,
  sweepOther,
} from '../../src/main/cache-files.ts';
import type { RescueFiles } from '../../src/main/cache-files.ts';
import type { CacheFolders } from '../../src/main/cache-folder.ts';

let dir: string;
let f: CacheFolders;
let rescue: RescueFiles;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), 'bt-cachefiles-'));
  f = { slide: join(dir, 'sliding'), panex: join(dir, 'panex') };
  rescue = { file: join(dir, '__rescue.xmpuzzle'), record: join(dir, '__rescue.json') };
});

afterEach(() => {
  rmSync(dir, { recursive: true, force: true });
});

/** A search's folder: `state.bin` when saved, and a level file of `bytes` bytes. */
function search(folder: string, name: string, bytes: number, saved = true): string {
  const p = join(folder, name);
  mkdirSync(p, { recursive: true });
  writeFileSync(join(p, 'f1.lvl'), 'x'.repeat(bytes));
  if (saved) {
    writeFileSync(join(p, 'state.bin'), 'BTSLSV01');
  }
  return p;
}

const S1 = 'slide-0123456789abcdef';
const S2 = 'slide-fedcba9876543210';
const S3 = 'slide-00000000000000aa';
const P1 = '0123456789abcdef';
const never = (): Promise<boolean> => Promise.resolve(false);

describe('the cache items', () => {
  it('lists nothing when there is nothing', async () => {
    expect(await listItems(f, rescue, new CacheLinks(dir))).toEqual([]);
  });

  it('names linked searches after their puzzle and problem, a problem’s starts as one item', async () => {
    const a = search(f.slide, S1, 100);
    const b = search(f.slide, S2, 50);
    search(f.slide, S3, 7);
    search(f.panex, P1, 30);
    const links = new CacheLinks(dir);
    await links.record(
      { folders: [a, b], path: '/puzzles/Block 15.xmpuzzle', name: 'Block 15', problem: 'Main' },
      f,
    );
    await links.record(
      { folders: [join(f.panex, P1)], path: null, name: 'Untitled', problem: 'P1' },
      f,
    );
    const items = await listItems(f, rescue, links);
    const sliding = items.find((i) => i.title.includes('Block 15'));
    expect(sliding).toMatchObject({
      kind: 'sliding',
      title: 'Paused sliding solve of Block 15.xmpuzzle, Main',
      bytes: 150 + 16,
      puzzle: '/puzzles/Block 15.xmpuzzle',
      puzzleName: null,
      problem: 'Main',
      moreFiles: 0,
    });
    expect(sliding?.about).toContain('searches of 2 starts');
    expect(sliding?.files).toEqual([
      join(a, 'f1.lvl'),
      join(a, 'state.bin'),
      join(b, 'f1.lvl'),
      join(b, 'state.bin'),
    ]);
    expect(sliding?.created).toBeGreaterThan(0);
    expect(sliding?.modified).toBeGreaterThanOrEqual(sliding?.created ?? 0);
    // not linked: a puzzle not known, an item of its own
    expect(items.find((i) => i.files[0]?.includes(S3))?.title).toBe(
      'Paused sliding solve (puzzle not known)',
    );
    expect(items.find((i) => i.kind === 'stacking')).toMatchObject({
      title: 'Paused stacking solve of Untitled, P1',
      puzzle: null,
      puzzleName: 'Untitled',
      problem: 'P1',
    });
    expect(items).toHaveLength(3);
  });

  it('tells an unfinished search and stray files apart', async () => {
    search(f.slide, S1, 10, false);
    writeFileSync(join(f.slide, 'state.tmp'), 'half');
    mkdirSync(join(f.panex, 'not-a-search'), { recursive: true });
    const items = await listItems(f, rescue, new CacheLinks(dir));
    expect(items.map((i) => i.title).sort()).toEqual([
      'Other files in the sliding search folder',
      'Other files in the stacking search folder',
      'Unfinished sliding search (puzzle not known)',
    ]);
    expect(items.every((i) => i.kind === 'other' || i.kind === 'sliding')).toBe(true);
  });

  it('names the rescue copy from its record', async () => {
    writeFileSync(rescue.file, '<puzzle/>');
    writeFileSync(
      rescue.record,
      JSON.stringify({ path: '/p/Dad.xmpuzzle', name: 'Dad', revision: 3, time: 1 }),
    );
    const [item] = await listItems(f, rescue, new CacheLinks(dir));
    expect(item).toMatchObject({
      kind: 'rescue',
      title: 'Crash recovery copy of Dad.xmpuzzle',
      puzzle: '/p/Dad.xmpuzzle',
      problem: null,
      files: [rescue.file, rescue.record],
    });
  });

  it('keeps only links of folders in the folders in use, and keeps them across a restart', async () => {
    const a = search(f.slide, S1, 1);
    const links = new CacheLinks(dir);
    await links.record(
      {
        folders: [a, join(dir, 'elsewhere', S2), join(f.slide, 'slide-nothex')],
        path: null,
        name: 'X',
        problem: 'P1',
      },
      f,
    );
    const kept = JSON.parse(readFileSync(join(dir, 'cache-links.json'), 'utf8')) as {
      links: Record<string, unknown>;
    };
    expect(Object.keys(kept.links)).toEqual([`sliding/${S1}`]);
    expect((await new CacheLinks(dir).get('sliding', S1))?.name).toBe('X');
    expect(kindOf(a, f)).toBe('sliding');
    expect(kindOf(join(f.panex, P1), f)).toBe('stacking');
    expect(kindOf(join(f.panex, P1, 'state.bin'), f)).toBeNull();
  });

  it('drops the links of folders that are gone', async () => {
    const a = search(f.slide, S1, 1);
    const links = new CacheLinks(dir);
    await links.record({ folders: [a], path: null, name: 'X', problem: 'P1' }, f);
    rmSync(a, { recursive: true });
    expect(await listItems(f, rescue, links)).toEqual([]);
    search(f.slide, S1, 1);
    expect((await listItems(f, rescue, links))[0]?.title).toBe(
      'Paused sliding solve (puzzle not known)',
    );
  });
});

describe('deleting an item', () => {
  it('deletes its files and lists what is left', async () => {
    const a = search(f.slide, S1, 10);
    const b = search(f.slide, S2, 10);
    const c = search(f.panex, P1, 10);
    const links = new CacheLinks(dir);
    await links.record({ folders: [a, b], path: null, name: 'X', problem: 'P1' }, f);
    const [item] = (await listItems(f, rescue, links)).filter((i) => i.kind === 'sliding');
    const r = await deleteItem(item?.id ?? '', f, rescue, links, never);
    expect(r.ok).toBe(true);
    expect(existsSync(a) || existsSync(b)).toBe(false);
    expect(existsSync(c)).toBe(true);
    expect(r.ok && r.items.map((i) => i.kind)).toEqual(['stacking']);
  });

  it('refuses a search while a solve runs, but not the rescue copy', async () => {
    const a = search(f.slide, S1, 10);
    writeFileSync(rescue.file, 'x');
    const links = new CacheLinks(dir);
    const items = await listItems(f, rescue, links);
    const solving = (): Promise<boolean> => Promise.resolve(true);
    const slide = items.find((i) => i.kind === 'sliding');
    const r = await deleteItem(slide?.id ?? '', f, rescue, links, solving);
    expect(r).toMatchObject({ ok: false });
    expect(!r.ok && r.error).toContain('Pause or stop it first');
    expect(existsSync(a)).toBe(true);
    const copy = items.find((i) => i.kind === 'rescue');
    expect((await deleteItem(copy?.id ?? '', f, rescue, links, solving)).ok).toBe(true);
    expect(existsSync(rescue.file)).toBe(false);
  });

  it('refuses an id it does not list', async () => {
    search(f.slide, S1, 10);
    const r = await deleteItem(`sliding:${S2}`, f, rescue, new CacheLinks(dir), never);
    expect(r).toMatchObject({ ok: false });
    expect(existsSync(join(f.slide, S1))).toBe(true);
  });
});

describe('the start’s sweep of other files', () => {
  /** Sets a path's times this many days before `now`. */
  const age = (p: string, now: number, days: number): void => {
    const t = new Date(now - days * 24 * 60 * 60 * 1000);
    utimesSync(p, t, t);
  };

  it('deletes the other files a week old, keeps newer ones and every search', async () => {
    const now = Date.now();
    const old = search(f.slide, S1, 10, false);
    age(join(old, 'f1.lvl'), now, 30);
    age(old, now, 30);
    const saved = search(f.panex, P1, 10);
    for (const n of ['f1.lvl', 'state.bin', '']) {
      age(join(saved, n), now, 30);
    }
    writeFileSync(join(f.slide, 'state.tmp'), 'half');
    age(join(f.slide, 'state.tmp'), now, 8);
    writeFileSync(join(f.slide, 'fresh.tmp'), 'half');
    age(join(f.slide, 'fresh.tmp'), now, 6);
    // a stray folder goes by the newest change under it
    const stray = join(f.panex, 'not-a-search');
    mkdirSync(stray, { recursive: true });
    writeFileSync(join(stray, 'a'), 'x');
    writeFileSync(join(stray, 'b'), 'x');
    age(join(stray, 'a'), now, 20);
    age(stray, now, 20);
    await sweepOther(f, now);
    expect(existsSync(join(f.slide, 'state.tmp'))).toBe(false);
    expect(existsSync(join(f.slide, 'fresh.tmp'))).toBe(true);
    expect(existsSync(stray)).toBe(true);
    expect(existsSync(old) && existsSync(saved)).toBe(true);
    await sweepOther(f, now + OTHER_MAX_AGE + 1000);
    expect(existsSync(stray) || existsSync(join(f.slide, 'fresh.tmp'))).toBe(false);
    expect(existsSync(old) && existsSync(saved)).toBe(true);
  });

  it('does nothing without the folders', async () => {
    await expect(sweepOther({ slide: '', panex: join(dir, 'none') })).resolves.toBeUndefined();
  });
});
