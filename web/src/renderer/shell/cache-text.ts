// SPDX-License-Identifier: GPL-3.0-or-later
//
// The words of the cache files (View ▸ Manage Cache and Autosave Files…,
// cache-files.ts, and the notice at the start, cache-notice.ts): a size,
// an item's kind, size and last change, and the notice's list and tip.
// Free of the DOM (its own escape, as log-rows.ts), so the unit tests
// drive it.

import type { CacheItem } from '../../engine/app-bridge.ts';

/** Text as HTML: dom.ts's `esc`, which needs the DOM's types. */
const esc = (text: string): string =>
  text.replace(/[&<>"']/g, (c) => `&#${String(c.charCodeAt(0))};`);

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

/** A time as the cache manager says it: the date and the minute, in the system's way. */
export const when = (ms: number): string =>
  new Date(ms).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });

/** What kind of thing an item is, as its row says it. */
const KIND: Readonly<Record<CacheItem['kind'], string>> = {
  sliding: 'Sliding solve',
  stacking: 'Stacking solve',
  rescue: 'Autosave',
  other: 'Leftover files',
};

/** An item's kind, size and last change, as its row says them: "Sliding solve · 1.2 GB · 20 Nov 2026, 14:39". */
export function itemMeta(item: CacheItem): string {
  return `${KIND[item.kind]} · ${sizeText(item.bytes)} · ${when(item.modified)}`;
}

/** The items the notice at the start names: all but the rescue copies (their offer is about them). */
export function noticeItems(items: readonly CacheItem[]): CacheItem[] {
  return items.filter((i) => i.kind !== 'rescue');
}

/** The items' bytes, in words. */
export function totalText(items: readonly CacheItem[]): string {
  return sizeText(items.reduce((n, i) => n + i.bytes, 0));
}

/** The notice's body: the items, their size in all, and where they are managed. */
export function noticeHtml(items: readonly CacheItem[]): string {
  const n = items.length;
  const rows = items
    .map(
      (i) =>
        `<li><span class="cn-t">${esc(i.title)}</span><span class="cn-m">${esc(itemMeta(i))}</span></li>`,
    )
    .join('');
  return (
    `<p>${n === 1 ? 'This item was' : `These ${String(n)} items were`} left in the cache folder by earlier sessions (${esc(totalText(items))} in all):</p>` +
    `<ul class="cn-list" aria-label="Cache and autosave files">${rows}</ul>` +
    `<p class="cn-err" role="alert" hidden></p>` +
    `<p class="cn-tip">Tip: These files can be managed using View ▸ Manage Cache and Autosave Files.</p>`
  );
}
