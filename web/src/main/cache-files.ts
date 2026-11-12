// SPDX-License-Identifier: GPL-3.0-or-later
//
// View ▸ Manage Cache and Autosave Files: what the cache folder
// (cache-folder.ts) and the rescue copy (rescue.ts) hold, as items a
// person would name rather than files. A sliding search's folders are
// `slide-<key>` and a stacking search's `<key>`, keyed by the problem's
// contents, so nothing in them says which puzzle they came from: each
// window tells it (`CacheLinks.record`) when a run of a sliding or
// stacking problem ends or a document opens, from the engine's
// solve.searchFolders, and the links are kept in the app's data folder
// (`cache-links.json`, by folder name, so a move of the cache folder
// keeps them). The folders of one problem's starts are one item; a folder
// without its `state.bin` is a search that never saved (a crash, or one
// running); anything else in the two folders is one item each. The rescue
// copy and its record are one item, named from the record.
//
// An item is deleted by its id, which the renderer has from the list:
// the folders are listed again and the item found by it, so a path the
// renderer names is never deleted. A search's item is refused while a
// solve runs (its search writes the folders). At each start the other
// files not changed in a week are deleted, unasked (`sweepOther`). Free of
// Electron (the folders are given), so the unit tests run it in a
// temporary directory.

import { lstat, readdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import { basename, dirname, join, resolve } from 'node:path';

import { isRescueInfo } from '../engine/app-bridge.ts';
import type { CacheItem, CacheItemKind, CacheLink } from '../engine/app-bridge.ts';
import type { CacheFolders } from './cache-folder.ts';

/** The most files an item lists. */
export const MAX_FILES = 1000;

/** The names the engine gives a search's folder, by kind. */
const SEARCH_NAME: Readonly<Record<'sliding' | 'stacking', RegExp>> = {
  sliding: /^slide-[0-9a-f]{16}$/,
  stacking: /^[0-9a-f]{16}$/,
};

/** What a window told of a search folder. */
export interface LinkRecord {
  /** The puzzle's file, or null for a puzzle without one. */
  path: string | null;
  name: string;
  problem: string;
  /** When it was told (ms since 1970). */
  time: number;
}

const isLinkRecord = (v: unknown): v is LinkRecord => {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const r = v as Record<string, unknown>;
  return (
    (r.path === null || typeof r.path === 'string') &&
    typeof r.name === 'string' &&
    typeof r.problem === 'string' &&
    typeof r.time === 'number'
  );
};

/** The links of search folders to their puzzles, by `<kind>/<folder name>`. */
export class CacheLinks {
  private readonly file: string;
  private links: Map<string, LinkRecord> | null = null;
  /** The writes, one after the other. */
  private chain: Promise<void> = Promise.resolve();

  constructor(dir: string) {
    this.file = join(dir, 'cache-links.json');
  }

  private async load(): Promise<Map<string, LinkRecord>> {
    if (this.links !== null) {
      return this.links;
    }
    const links = new Map<string, LinkRecord>();
    try {
      const v: unknown = JSON.parse(await readFile(this.file, 'utf8'));
      const all = (v as { links?: unknown } | null)?.links;
      if (typeof all === 'object' && all !== null) {
        for (const [k, r] of Object.entries(all)) {
          if (isLinkRecord(r)) {
            links.set(k, r);
          }
        }
      }
    } catch {
      // none yet, or not ours: no links
    }
    this.links ??= links;
    return this.links;
  }

  private save(): Promise<void> {
    const links = this.links;
    if (links === null) {
      return this.chain;
    }
    this.chain = this.chain.then(async () => {
      const tmp = `${this.file}.tmp`;
      try {
        await writeFile(tmp, JSON.stringify({ version: 1, links: Object.fromEntries(links) }));
        await rename(tmp, this.file);
      } catch {
        // the links only name the items: without them they are "not known"
      }
    });
    return this.chain;
  }

  /** The link of one folder, or undefined. */
  async get(kind: CacheItemKind, name: string): Promise<LinkRecord | undefined> {
    return (await this.load()).get(`${kind}/${name}`);
  }

  /**
   * A window's link (its folders full paths, as solve.searchFolders gave
   * them): only folders directly in the folders in use, by the engine's
   * names, are kept. `path` was checked by the caller (a file the window
   * was handed, else null).
   */
  async record(link: CacheLink, f: CacheFolders, now = Date.now()): Promise<void> {
    const links = await this.load();
    let changed = false;
    for (const folder of link.folders) {
      const kind = kindOf(folder, f);
      if (kind === null) {
        continue;
      }
      links.set(`${kind}/${basename(folder)}`, {
        path: link.path,
        name: link.name,
        problem: link.problem,
        time: now,
      });
      changed = true;
    }
    if (changed) {
      await this.save();
    }
  }

  /** Keeps only the links of these folders (`<kind>/<name>`): the others' folders are gone. */
  async keep(present: ReadonlySet<string>): Promise<void> {
    const links = await this.load();
    let changed = false;
    for (const k of [...links.keys()]) {
      if (!present.has(k)) {
        links.delete(k);
        changed = true;
      }
    }
    if (changed) {
      await this.save();
    }
  }
}

/** Whether a folder is a search's in the folders in use: its kind, or null. */
export function kindOf(folder: string, f: CacheFolders): 'sliding' | 'stacking' | null {
  const parent = resolve(dirname(folder));
  const name = basename(folder);
  if (f.slide !== '' && parent === resolve(f.slide) && SEARCH_NAME.sliding.test(name)) {
    return 'sliding';
  }
  if (f.panex !== '' && parent === resolve(f.panex) && SEARCH_NAME.stacking.test(name)) {
    return 'stacking';
  }
  return null;
}

/** What walking a file or folder found. */
interface Walked {
  files: string[];
  bytes: number;
  created: number;
  modified: number;
}

const created = (s: { birthtimeMs: number; mtimeMs: number }): number =>
  s.birthtimeMs > 0 ? s.birthtimeMs : s.mtimeMs;

/** Every file under a path (links not followed), their bytes and times. */
async function walk(path: string, into: Walked): Promise<void> {
  let s;
  try {
    s = await lstat(path);
  } catch {
    return;
  }
  into.created = Math.min(into.created, created(s));
  into.modified = Math.max(into.modified, s.mtimeMs);
  if (!s.isDirectory()) {
    into.files.push(path);
    into.bytes += s.size;
    return;
  }
  for (const name of (await readdir(path).catch(() => [])).sort()) {
    await walk(join(path, name), into);
  }
}

async function walkAll(paths: readonly string[]): Promise<Walked> {
  const w: Walked = { files: [], bytes: 0, created: Infinity, modified: 0 };
  for (const p of paths) {
    await walk(p, w);
  }
  if (!Number.isFinite(w.created)) {
    w.created = w.modified;
  }
  return w;
}

const exists = (p: string): Promise<boolean> =>
  lstat(p).then(
    () => true,
    () => false,
  );

/** What an item is made of before it is named: the paths it deletes. */
interface Group {
  kind: CacheItemKind;
  roots: string[];
  /** Saved (a search with its state.bin; always for the rescue copy). */
  saved: boolean;
  link: LinkRecord | null;
}

const baseName = (path: string): string => path.split(/[\\/]/).pop() ?? path;

/** The puzzle a link names, as a title says it: its file's name, or the name it was shown by. */
function puzzleTitle(link: LinkRecord | null): string | null {
  if (link === null) {
    return null;
  }
  return link.path !== null ? baseName(link.path) : link.name;
}

const SEARCH_WORDS: Readonly<Record<'sliding' | 'stacking', string>> = {
  sliding: 'sliding',
  stacking: 'stacking (Panex)',
};

/** An item's title and its words. */
function named(g: Group): { title: string; about: string } {
  const puzzle = puzzleTitle(g.link);
  const of = puzzle === null ? ' (puzzle not known)' : ` of ${puzzle}`;
  const problem = g.link !== null && g.link.problem !== '' ? `, ${g.link.problem}` : '';
  switch (g.kind) {
    case 'rescue':
      return {
        title: `Crash recovery copy${of}`,
        about:
          'Unsaved changes, written a moment after each change while the puzzle has changes not saved, and while a solve runs, the solve and its position (the crash recovery journal). After a crash, ShapeLogic offers it when it starts again. Deleting it loses those unsaved changes; a puzzle still open with changes not saved writes it again at its next change.',
      };
    case 'other': {
      const where = g.roots[0] === undefined ? '' : baseName(dirname(g.roots[0]));
      return {
        title: `Other files in the ${where === 'panex' ? 'stacking' : 'sliding'} search folder`,
        about:
          'Files that are not a search ShapeLogic can carry on: left by an older version, or half written when the app stopped. Nothing uses them; deleting them is safe.',
      };
    }
    default: {
      const kind = SEARCH_WORDS[g.kind];
      const starts =
        g.kind === 'sliding' && g.roots.length > 1
          ? ` It holds the searches of ${String(g.roots.length)} starts.`
          : '';
      if (g.saved) {
        return {
          title: `Paused ${g.kind} solve${of}${problem}`,
          about: `A ${kind} search saved when its solve was paused or stopped (Pause, a save, the crash recovery journal), so Continue carries it on where it was.${starts} Deleting it frees the space; the solve then starts that search again from the beginning.`,
        };
      }
      return {
        title: `Unfinished ${g.kind} search${of}${problem}`,
        about: `The files a ${kind} search writes while it runs, never saved: a solve under way, or one that stopped without saving (the app or the computer crashed). It cannot be carried on. Deleting it is safe once no solve runs.`,
      };
    }
  }
}

/** Whether a path in a search folder is a search's folder, by the engine's names. */
async function isSearch(p: string, kind: 'sliding' | 'stacking'): Promise<boolean> {
  const s = await lstat(p).catch(() => null);
  return s?.isDirectory() === true && SEARCH_NAME[kind].test(basename(p));
}

/** The groups of one search folder: the saved searches by their link, the rest one each. */
async function searchGroups(
  dir: string,
  kind: 'sliding' | 'stacking',
  links: CacheLinks,
  present: Set<string>,
): Promise<Group[]> {
  if (dir === '') {
    return [];
  }
  const names = (await readdir(dir).catch(() => [])).sort();
  const groups: Group[] = [];
  const byLink = new Map<string, Group>();
  const other: string[] = [];
  for (const name of names) {
    const p = join(dir, name);
    if (!(await isSearch(p, kind))) {
      other.push(p);
      continue;
    }
    present.add(`${kind}/${name}`);
    const link = (await links.get(kind, name)) ?? null;
    const saved = await exists(join(p, 'state.bin'));
    // the saved searches of one problem's starts are one item
    if (saved && link !== null) {
      const key = JSON.stringify([link.path, link.name, link.problem]);
      const g = byLink.get(key);
      if (g !== undefined) {
        g.roots.push(p);
        continue;
      }
      const ng: Group = { kind, roots: [p], saved, link };
      byLink.set(key, ng);
      groups.push(ng);
      continue;
    }
    groups.push({ kind, roots: [p], saved, link });
  }
  if (other.length > 0) {
    groups.push({ kind: 'other', roots: other, saved: false, link: null });
  }
  return groups;
}

/** The rescue copy's files (the copy, its record, their temporary files). */
export interface RescueFiles {
  file: string;
  record: string;
}

async function rescueGroup(r: RescueFiles): Promise<Group | null> {
  const roots: string[] = [];
  for (const p of [r.file, r.record, `${r.file}.tmp`, `${r.record}.tmp`]) {
    if (await exists(p)) {
      roots.push(p);
    }
  }
  if (roots.length === 0) {
    return null;
  }
  let link: LinkRecord | null = null;
  try {
    const v: unknown = JSON.parse(await readFile(r.record, 'utf8'));
    if (isRescueInfo(v)) {
      link = { path: v.path, name: v.name, problem: '', time: v.time };
    }
  } catch {
    // no record: a copy of a puzzle not known
  }
  return { kind: 'rescue', roots, saved: true, link };
}

/** An item's id: its kind and what it deletes. */
const idOf = (g: Group): string => `${g.kind}:${g.roots.map((p) => baseName(p)).join('|')}`;

/** A group, named and walked. */
async function itemOf(g: Group): Promise<{ item: CacheItem; roots: string[] }> {
  const w = await walkAll(g.roots);
  const { title, about } = named(g);
  return {
    item: {
      id: idOf(g),
      kind: g.kind,
      title,
      about,
      bytes: w.bytes,
      puzzle: g.link?.path ?? null,
      puzzleName: g.link !== null && g.link.path === null ? g.link.name : null,
      problem: g.link !== null && g.link.problem !== '' ? g.link.problem : null,
      created: w.created,
      modified: w.modified,
      files: w.files.slice(0, MAX_FILES),
      moreFiles: Math.max(0, w.files.length - MAX_FILES),
    },
    roots: g.roots,
  };
}

/** The items with what each deletes, the newest first; links of folders gone are dropped. */
async function listWithRoots(
  f: CacheFolders,
  rescue: RescueFiles,
  links: CacheLinks,
): Promise<{ item: CacheItem; roots: string[] }[]> {
  const present = new Set<string>();
  const groups = [
    ...(await searchGroups(f.slide, 'sliding', links, present)),
    ...(await searchGroups(f.panex, 'stacking', links, present)),
  ];
  const r = await rescueGroup(rescue);
  if (r !== null) {
    groups.push(r);
  }
  await links.keep(present);
  const items = [];
  for (const g of groups) {
    items.push(await itemOf(g));
  }
  return items.sort((a, b) => b.item.modified - a.item.modified);
}

/** What the cache folder and the rescue copy hold, the newest first. */
export async function listItems(
  f: CacheFolders,
  rescue: RescueFiles,
  links: CacheLinks,
): Promise<CacheItem[]> {
  return (await listWithRoots(f, rescue, links)).map((x) => x.item);
}

/** The words of a running solve. */
const SOLVING =
  'A solve is running in a ShapeLogic window. Pause or stop it first: its search writes to the cache folder.';

/**
 * Deletes the item of this id, found by listing again: the items left, or
 * why not. `solving` says whether a solve runs in any window (a search's
 * item waits for it).
 */
export async function deleteItem(
  id: string,
  f: CacheFolders,
  rescue: RescueFiles,
  links: CacheLinks,
  solving: () => Promise<boolean>,
): Promise<{ ok: true; items: CacheItem[] } | { ok: false; error: string }> {
  const found = (await listWithRoots(f, rescue, links)).find((x) => x.item.id === id);
  if (found === undefined) {
    return { ok: false, error: 'It is not there any more: another window may have deleted it.' };
  }
  if (found.item.kind !== 'rescue' && (await solving())) {
    return { ok: false, error: SOLVING };
  }
  try {
    for (const p of found.roots) {
      await rm(p, { recursive: true, force: true });
    }
  } catch (e) {
    return {
      ok: false,
      error: `It could not all be deleted: ${e instanceof Error ? e.message : String(e)}`,
    };
  }
  return { ok: true, items: await listItems(f, rescue, links) };
}

/** How long the other files stay before a start deletes them: a week. */
export const OTHER_MAX_AGE = 7 * 24 * 60 * 60 * 1000;

/**
 * Deletes the other files of the search folders (not a search ShapeLogic
 * can carry on) that nothing changed for `maxAge`: each file or folder by
 * the newest change under it. Run at the start, before any solve; nothing
 * is told, and what cannot be deleted is left for the next start.
 */
export async function sweepOther(
  f: CacheFolders,
  now = Date.now(),
  maxAge = OTHER_MAX_AGE,
): Promise<void> {
  for (const [dir, kind] of [
    [f.slide, 'sliding'],
    [f.panex, 'stacking'],
  ] as const) {
    if (dir === '') {
      continue;
    }
    for (const name of await readdir(dir).catch(() => [])) {
      const p = join(dir, name);
      if (await isSearch(p, kind)) {
        continue;
      }
      const w = await walkAll([p]);
      if (now - w.modified > maxAge) {
        await rm(p, { recursive: true, force: true }).catch(() => undefined);
      }
    }
  }
}
