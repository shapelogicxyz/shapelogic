// SPDX-License-Identifier: GPL-3.0-or-later
//
// The word that files of the cache folder were deleted in this window
// (View ▸ Manage Cache and Autosave Files…, cache-files.ts, and the notice
// at the start, cache-notice.ts): the Solver reads its problem's solve
// state again, since a paused sliding solve whose saved search went is
// solved again from the beginning (solve.status's continueStartsOver).

/** The window event fired after a deletion. */
export const CACHE_DELETED = 'sl:cache-deleted';

/** Tells the window that cache files were deleted. */
export function cacheDeleted(): void {
  window.dispatchEvent(new Event(CACHE_DELETED));
}
