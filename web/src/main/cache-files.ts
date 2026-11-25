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
// running). What else ShapeLogic leaves there is named too: a sliding
// search of version 1.5 or earlier (`search-<key>`, never saved), and a
// paused solve's import that stopped part way (`<folder>.import`). The
// solves' lock files are not listed (decision 0035: a solve removes those
// nobody holds; only the system knows which are held), nor the files the
// system's file browser writes. Anything else in the two folders is one
// item each. Each
// rescue copy (one per window, rescue.ts) and its record are one item,
// named from the record.
//
// An item is deleted by its id, which the renderer has from the list:
// the folders are listed again and the item found by it, so a path the
// renderer names is never deleted. A search's item is refused while a
// solve runs (its search writes the folders). At each start the files
// that are no search ShapeLogic can carry on, not changed in a week, are
// deleted, unasked (`sweepOther`); the lock files are left. Free of
// Electron (the folders are given), so the unit tests run it in a
// temporary directory.

import { lstat, readdir, readFile, rm } from 'node:fs/promises';
import { basename, dirname, join, resolve } from 'node:path';

import { isRescueInfo } from '../engine/app-bridge.ts';
import type { CacheItem, CacheItemKind, CacheLink } from '../engine/app-bridge.ts';
import type { CacheFolders } from './cache-folder.ts';
import { isTempOf, replaceFile } from './replace-file.ts';
import { isLockFile, SEARCH_NAME, SOLVING, walk } from './search-folders.ts';

/** The most files an item lists. */
export const MAX_FILES = 1000;

/** A sliding search's folder of version 1.5 and earlier, which never saved its search. */
const OLDER_SLIDE_NAME = /^search-[0-9a-f]{16}$/;

/** The folder a paused solve's import unpacks into first (io/paused_solve.cpp), by kind. */
const IMPORT_NAME: Readonly<Record<'sliding' | 'stacking', RegExp>> = {
  sliding: /^(slide-[0-9a-f]{16})\.import$/,
  stacking: /^([0-9a-f]{16})\.import$/,
};

/** The files a system's file browser leaves in a folder it showed. */
const SYSTEM_NAME = /^(\.DS_Store|Thumbs\.db|desktop\.ini)$/i;

/**
 * Whether a name in a search folder is left out of the list and the sweep:
 * every lock file (isLockFile; a lock another solve holds must stay), and
 * what a file browser leaves.
 */
const unlisted = (name: string): boolean => isLockFile(name) || SYSTEM_NAME.test(name);

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
      try {
        await replaceFile(
          this.file,
          JSON.stringify({ version: 1, links: Object.fromEntries(links) }),
        );
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
  /** A search's leftover ShapeLogic knows: an older version's search, an import cut short. */
  leftover?: 'older' | 'import';
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
  if (g.leftover === 'older') {
    return {
      title: 'Unfinished sliding search from version 1.5 or earlier',
      about:
        'The files a sliding search of ShapeLogic 1.5 or earlier wrote while it ran, left when the app or the computer stopped during the solve. Those versions never saved a sliding search, so it cannot be carried on. Nothing uses it; deleting it is safe, and ShapeLogic deletes it by itself at a start once nothing has changed it for a week.',
    };
  }
  if (g.leftover === 'import') {
    const kind = g.kind === 'stacking' ? 'stacking' : 'sliding';
    return {
      title: `Unfinished import of a paused ${kind} solve${of}${problem}`,
      about: `A paused solve being imported (File ▸ Import ▸ Paused Solver State) is unpacked here first and put in place once the whole file is read; this one was left when the import stopped part way (the app or the computer stopped). The saved search it was to replace, if any, is as it was. Nothing uses it; deleting it is safe, and ShapeLogic deletes it by itself at a start once nothing has changed it for a week.`,
    };
  }
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
          'Files that are not a search ShapeLogic can carry on, and that no version of ShapeLogic writes there: put there by hand or by another program. Nothing uses them; deleting them is safe, and ShapeLogic deletes them by itself at a start once nothing has changed them for a week.',
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
    if (unlisted(name)) {
      continue;
    }
    if (!(await isSearch(p, kind))) {
      const isDir = (await lstat(p).catch(() => null))?.isDirectory() === true;
      const imported = isDir ? IMPORT_NAME[kind].exec(name) : null;
      if (imported?.[1] !== undefined) {
        // the search it was to replace keeps its link, there or not
        present.add(`${kind}/${imported[1]}`);
        const link = (await links.get(kind, imported[1])) ?? null;
        groups.push({ kind, roots: [p], saved: false, link, leftover: 'import' });
      } else if (isDir && kind === 'sliding' && OLDER_SLIDE_NAME.test(name)) {
        groups.push({ kind, roots: [p], saved: false, link: null, leftover: 'older' });
      } else {
        other.push(p);
      }
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

/** The rescue copies: one, or every window's (rescue.ts's RescueSlots.all). */
export type RescueCopies = RescueFiles | readonly RescueFiles[];

const copies = (r: RescueCopies): readonly RescueFiles[] => ('file' in r ? [r] : r);

async function rescueGroup(r: RescueFiles): Promise<Group | null> {
  const roots: string[] = [];
  for (const p of [r.file, r.record, `${r.file}.tmp`, `${r.record}.tmp`]) {
    if (await exists(p)) {
      roots.push(p);
    }
  }
  // the temporary files replace-file.ts names for each write, left by a crash
  const dir = dirname(r.file);
  const bases = [basename(r.file), basename(r.record)];
  for (const name of (await readdir(dir).catch(() => [])).sort()) {
    if (bases.some((b) => isTempOf(name, b))) {
      roots.push(join(dir, name));
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
  const w = await walk(g.roots);
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
  rescue: RescueCopies,
  links: CacheLinks,
): Promise<{ item: CacheItem; roots: string[] }[]> {
  const present = new Set<string>();
  const groups = [
    ...(await searchGroups(f.slide, 'sliding', links, present)),
    ...(await searchGroups(f.panex, 'stacking', links, present)),
  ];
  for (const c of copies(rescue)) {
    const r = await rescueGroup(c);
    if (r !== null) {
      groups.push(r);
    }
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
  rescue: RescueCopies,
  links: CacheLinks,
): Promise<CacheItem[]> {
  return (await listWithRoots(f, rescue, links)).map((x) => x.item);
}

/**
 * Deletes the item of this id, found by listing again: the items left, or
 * why not. `solving` says whether a solve runs in any window (a search's
 * item waits for it).
 */
export async function deleteItem(
  id: string,
  f: CacheFolders,
  rescue: RescueCopies,
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
 * Deletes what the search folders hold that is no search ShapeLogic can
 * carry on (an older version's search, an import cut short, other files)
 * and that nothing changed for `maxAge`: each file or folder by the newest
 * change under it. The lock files are left: a solve of the command line
 * may hold one, and only the system knows (a solve removes those nobody
 * holds). Run at the start, before any solve; nothing is told, and what
 * cannot be deleted is left for the next start.
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
      if (unlisted(name) || (await isSearch(p, kind))) {
        continue;
      }
      const w = await walk([p]);
      if (now - w.modified > maxAge) {
        await rm(p, { recursive: true, force: true }).catch(() => undefined);
      }
    }
  }
}
