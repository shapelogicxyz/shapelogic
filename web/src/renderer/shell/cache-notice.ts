// SPDX-License-Identifier: GPL-3.0-or-later
//
// The notice at the start (not in the mockup or the original): once the
// rescue copy's offer is answered (shell/rescue.ts), or there was none,
// what else View ▸ Manage Cache and Autosave Files… lists (paused sliding
// and stacking solves, searches a crash left unfinished, leftover files)
// is named in a list, with Manage Files… (the manager), Delete All… (after
// asking) and Ignore. The rescue copies are left out: their offer is about
// them. Shown once a run, in the first window that asks (the main
// process's `start` list); Ignore leaves the files, and the next start
// names them again.

import './cache-notice.css';
import type { BtApp, CacheItem } from '../../engine/app-bridge.ts';
import { noticeHtml, noticeItems, totalText } from './cache-text.ts';
import { cacheDeleted } from './cache-events.ts';
import { askChoice, closeDialog, openDialog } from './dialog.ts';
import { flash, reportError } from './status.ts';

const ID = 'cacheAtStart';

/**
 * Deletes the items, one after the other (a search's item is refused
 * while a solve runs): the titles of those that could not be, with why.
 */
async function deleteAll(app: BtApp, items: readonly CacheItem[]): Promise<string[]> {
  const failed: string[] = [];
  for (const item of items) {
    try {
      const r = await app.deleteCacheItem(item.id);
      if (!r.ok) {
        failed.push(`${item.title}: ${r.error}`);
      }
    } catch (e) {
      failed.push(`${item.title}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
  return failed;
}

/**
 * The notice at the start, when the cache folder holds anything but the
 * rescue copies; `manage` opens View ▸ Manage Cache and Autosave Files….
 */
export async function noticeCacheAtStart(app: BtApp, manage: () => void): Promise<void> {
  let all: CacheItem[] | null;
  try {
    all = await app.cacheItemsAtStart();
  } catch (e) {
    reportError(e, 'The cache folder could not be listed');
    return;
  }
  const items = noticeItems(all ?? []);
  if (items.length === 0) {
    return;
  }
  let busy = false;
  const err = (text: string | null): void => {
    const e = document.querySelector<HTMLElement>(`#${ID} .cn-err`);
    if (e !== null) {
      e.textContent = text ?? '';
      e.hidden = text === null;
    }
  };
  const removeAll = async (): Promise<void> => {
    busy = true;
    try {
      const n = items.length;
      const k = await askChoice(
        'cacheAtStartDelete',
        'Delete all from the disk?',
        `${n === 1 ? 'The item' : `All ${String(n)} items`} (${totalText(items)}) will be deleted. A paused solve then starts its search again from the beginning. This cannot be undone.`,
        [
          { key: 'cancel', label: 'Cancel', default: true },
          { key: 'delete', label: 'Delete All', kind: 'danger' },
        ],
      );
      if (k !== 'delete') {
        return;
      }
      const failed = await deleteAll(app, items);
      cacheDeleted();
      if (failed.length === 0) {
        closeDialog(ID);
        flash(
          `Deleted ${n === 1 ? 'the item' : `${String(n)} items`} (${totalText(items)} freed)`,
          5000,
        );
      } else {
        err(`Not everything could be deleted. ${failed.join(' ')}`);
      }
    } finally {
      busy = false;
    }
  };
  await openDialog({
    id: ID,
    title: 'Cache and autosave files',
    body: noticeHtml(items),
    buttons: [
      {
        key: 'manage',
        label: 'Manage Files…',
        tip: 'Open View ▸ Manage Cache and Autosave Files: each item’s files, and Delete',
      },
      {
        key: 'deleteAll',
        label: 'Delete All…',
        kind: 'danger',
        tip: 'Delete all of these files from the disk, after asking',
      },
      {
        key: 'ignore',
        label: 'Ignore',
        kind: 'pri',
        default: true,
        tip: 'Leave the files as they are',
      },
    ],
    onAnswer: (key) => {
      if (key === 'deleteAll') {
        if (!busy) {
          void removeAll();
        }
        return false;
      }
      if (key === 'manage') {
        manage();
      }
      return true;
    },
  });
}
