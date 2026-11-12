// SPDX-License-Identifier: GPL-3.0-or-later
//
// View ▸ Manage Cache and Autosave Files… (not in the mockup or the
// original): what solves, pauses, the crash recovery journal and crashes
// left in the cache folder and the app's data folder, one item for each
// thing a person would name (main/cache-files.ts names them and lists their
// files). The list is on top, the newest first; below it the selected
// item's details: what it is, the disk space it takes, the puzzle file and
// problem it belongs to, when it was made and last written, every file by
// its full path, and Delete beside its title, which asks first. Refresh lists it again. The
// renderer deletes an item by its id; the main process finds its files.

import type { BtApp, CacheItem } from '../../engine/app-bridge.ts';
import { askChoice, openDialog } from './dialog.ts';
import { esc } from './dom.ts';
import { flash, reportError } from './status.ts';

const ID = 'cacheFiles';

/** A size in words: "1.2 GB". */
export function sizeText(bytes: number): string {
  const units = ['bytes', 'KB', 'MB', 'GB', 'TB'];
  let v = bytes;
  let u = 0;
  while (v >= 1000 && u < units.length - 1) {
    v /= 1000;
    u++;
  }
  if (u === 0) {
    return bytes === 1 ? '1 byte' : `${String(v)} bytes`;
  }
  return `${v < 10 ? v.toFixed(1) : String(Math.round(v))} ${units[u] ?? ''}`;
}

const when = (ms: number): string =>
  new Date(ms).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });

/** What kind of thing an item is, as its row says it. */
const KIND: Readonly<Record<CacheItem['kind'], string>> = {
  sliding: 'Sliding solve',
  stacking: 'Stacking solve',
  rescue: 'Autosave',
  other: 'Leftover files',
};

function rowHtml(item: CacheItem, selected: boolean): string {
  return `<div class="cf-row" role="option" id="cf-${esc(item.id)}" data-id="${esc(item.id)}" aria-selected="${String(selected)}"><span class="cf-t">${esc(item.title)}</span><span class="cf-m">${esc(KIND[item.kind])} · ${esc(sizeText(item.bytes))} · ${esc(when(item.modified))}</span></div>`;
}

function listHtml(items: readonly CacheItem[], selected: string | null): string {
  if (items.length === 0) {
    return `<div class="cf-empty">Nothing here: no paused sliding or stacking solves, unfinished searches or crash recovery copies.</div>`;
  }
  return items.map((i) => rowHtml(i, i.id === selected)).join('');
}

function totalHtml(items: readonly CacheItem[]): string {
  if (items.length === 0) {
    return '';
  }
  const bytes = items.reduce((n, i) => n + i.bytes, 0);
  return `${items.length === 1 ? '1 item' : `${String(items.length)} items`}, ${esc(sizeText(bytes))} in all`;
}

function puzzleHtml(item: CacheItem): string {
  if (item.puzzle !== null) {
    return `<code>${esc(item.puzzle)}</code>`;
  }
  if (item.puzzleName !== null) {
    return `${esc(item.puzzleName)} <span class="mute">(not saved to a file)</span>`;
  }
  return item.kind === 'other'
    ? '<span class="mute">None</span>'
    : '<span class="mute">Not known: made by a version of ShapeLogic before this one recorded it</span>';
}

function detailHtml(item: CacheItem | undefined): string {
  if (item === undefined) {
    return `<div class="cf-none">Select an item above to see its details.</div>`;
  }
  const n = item.files.length + item.moreFiles;
  const files =
    item.files.map((f) => `<div>${esc(f)}</div>`).join('') +
    (item.moreFiles > 0
      ? `<div class="mute">and ${String(item.moreFiles)} more in the same folders</div>`
      : '');
  const problem =
    item.kind === 'sliding' || item.kind === 'stacking'
      ? `<dt>Problem</dt><dd>${item.problem === null ? '<span class="mute">Not known</span>' : esc(item.problem)}</dd>`
      : '';
  return (
    `<div class="cf-head"><h4>${esc(item.title)}</h4><button class="btn danger sm" data-act="delete" title="Delete these files from the disk">Delete…</button></div>` +
    `<p>${esc(item.about)}</p>` +
    `<dl class="cf-dl">` +
    `<dt>Disk space</dt><dd>${esc(sizeText(item.bytes))}${item.bytes >= 1000 ? ` <span class="mute">(${item.bytes.toLocaleString()} bytes)</span>` : ''}</dd>` +
    `<dt>Puzzle file</dt><dd>${puzzleHtml(item)}</dd>` +
    problem +
    `<dt>Created</dt><dd>${esc(when(item.created))}</dd>` +
    `<dt>Last modified</dt><dd>${esc(when(item.modified))}</dd>` +
    `<dt>${n === 1 ? 'File' : `Files (${n.toLocaleString()})`}</dt><dd><div class="cf-files">${files}</div></dd>` +
    `</dl>`
  );
}

/** The dialog's state and its parts. */
class CacheFilesView {
  private readonly app: BtApp;
  private readonly el: HTMLElement;
  private items: CacheItem[] = [];
  private selected: string | null = null;
  private busy = false;

  constructor(app: BtApp, el: HTMLElement) {
    this.app = app;
    this.el = el;
    const list = this.part('.cf-list');
    list.addEventListener('click', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.cf-row');
      if (row?.dataset.id !== undefined) {
        this.select(row.dataset.id);
      }
    });
    list.addEventListener('keydown', (e) => {
      if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp' && e.key !== 'Home' && e.key !== 'End') {
        return;
      }
      e.preventDefault();
      const at = this.items.findIndex((i) => i.id === this.selected);
      const next =
        e.key === 'Home'
          ? 0
          : e.key === 'End'
            ? this.items.length - 1
            : Math.max(0, Math.min(this.items.length - 1, at + (e.key === 'ArrowDown' ? 1 : -1)));
      const item = this.items[next];
      if (item !== undefined) {
        this.select(item.id);
      }
    });
    this.part('.cf-detail').addEventListener('click', (e) => {
      if ((e.target as Element).closest('[data-act="delete"]') !== null) {
        void this.remove();
      }
    });
  }

  private part(sel: string): HTMLElement {
    const p = this.el.querySelector<HTMLElement>(sel);
    if (p === null) {
      throw new Error(`the cache dialog has no ${sel}`);
    }
    return p;
  }

  private error(text: string | null): void {
    const e = this.part('.cf-err');
    e.textContent = text ?? '';
    e.hidden = text === null;
  }

  /** Lists the items again, keeping the selection while its item is there. */
  async refresh(): Promise<void> {
    this.error(null);
    try {
      this.show((await this.app.cacheItems()) ?? []);
    } catch (e) {
      this.error(e instanceof Error ? e.message : String(e));
    }
  }

  /** Shows these items. */
  show(items: CacheItem[]): void {
    this.items = items;
    if (!items.some((i) => i.id === this.selected)) {
      this.selected = items[0]?.id ?? null;
    }
    const list = this.part('.cf-list');
    list.innerHTML = listHtml(items, this.selected);
    if (this.selected === null) {
      list.removeAttribute('aria-activedescendant');
    } else {
      list.setAttribute('aria-activedescendant', `cf-${this.selected}`);
    }
    this.part('.cf-total').innerHTML = totalHtml(items);
    this.part('.cf-detail').innerHTML = detailHtml(items.find((i) => i.id === this.selected));
  }

  private select(id: string): void {
    if (id === this.selected) {
      return;
    }
    this.selected = id;
    this.error(null);
    for (const row of this.el.querySelectorAll<HTMLElement>('.cf-row')) {
      row.setAttribute('aria-selected', String(row.dataset.id === id));
      if (row.dataset.id === id) {
        row.scrollIntoView({ block: 'nearest' });
      }
    }
    this.part('.cf-list').setAttribute('aria-activedescendant', `cf-${id}`);
    this.part('.cf-detail').innerHTML = detailHtml(this.items.find((i) => i.id === id));
  }

  private async remove(): Promise<void> {
    const item = this.items.find((i) => i.id === this.selected);
    if (item === undefined || this.busy) {
      return;
    }
    const n = item.files.length + item.moreFiles;
    const answer = await askChoice(
      'cacheFilesDelete',
      'Delete from the disk?',
      `${item.title}\n\n${n === 1 ? 'Its file' : `Its ${n.toLocaleString()} files`} (${sizeText(item.bytes)}) will be deleted. This cannot be undone.`,
      [
        { key: 'cancel', label: 'Cancel', default: true },
        { key: 'delete', label: 'Delete', kind: 'danger' },
      ],
    );
    if (answer !== 'delete') {
      return;
    }
    this.busy = true;
    const buttons = this.el.querySelectorAll<HTMLButtonElement>('button');
    buttons.forEach((b) => {
      b.disabled = true;
    });
    try {
      const r = await this.app.deleteCacheItem(item.id);
      if (r.ok) {
        this.show(r.items);
        flash(`Deleted: ${item.title} (${sizeText(item.bytes)} freed)`, 5000);
      } else {
        this.error(r.error);
      }
    } catch (e) {
      reportError(e, 'Manage cache files');
    } finally {
      this.busy = false;
      this.el.querySelectorAll<HTMLButtonElement>('button').forEach((b) => {
        b.disabled = false;
      });
    }
  }
}

/** View ▸ Manage Cache and Autosave Files…: the items, their details, and Delete. */
export async function openCacheFiles(app: BtApp): Promise<void> {
  let items: CacheItem[] | null;
  try {
    items = await app.cacheItems();
  } catch (e) {
    reportError(e, 'Manage cache files');
    return;
  }
  if (items === null) {
    flash('There is no cache folder here: the searches keep everything in memory.', 5000);
    return;
  }
  let view: CacheFilesView | null = null;
  const shown = openDialog({
    id: ID,
    title: 'Cache and autosave files',
    wide: true,
    body:
      `<p>What paused sliding and stacking solves, the crash recovery journal and crashes left on the disk. Select one to see its files, or to delete it and free the space.</p>` +
      `<div class="cf-list" role="listbox" tabindex="0" aria-label="Cache and autosave files" data-focus></div>` +
      `<div class="cf-total mute"></div>` +
      `<p class="cf-err" role="alert" hidden></p>` +
      `<div class="cf-detail" aria-live="polite"></div>`,
    buttons: [
      { key: 'refresh', label: 'Refresh', tip: 'List the files again' },
      { key: 'close', label: 'Close', kind: 'pri', default: true },
    ],
    onAnswer: (key) => {
      if (key === 'refresh') {
        void view?.refresh();
        return false;
      }
      return true;
    },
  });
  const el = document.getElementById(ID);
  if (el !== null) {
    view = new CacheFilesView(app, el);
    // the list as it was asked for; Refresh asks again
    view.show(items);
  }
  await shown;
}
