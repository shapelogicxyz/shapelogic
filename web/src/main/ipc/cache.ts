// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's cache folder and cache manager (a group of ../app-ipc.ts,
// cleanup item 13.4). The cache folder (cache-folder.ts) is chosen in this
// process's folder dialog, and what to do with the data there is the
// renderer's answer to the choice it was handed, never a path of its own.
// The cache manager (cache-files.ts) lists what the cache folder and the
// rescue copies hold and deletes an item by the id it listed it by; a
// window's links of its search folders name only folders in the cache
// folder in use, and a puzzle file only one the window was handed.

import { app, dialog } from 'electron';
import type { WebContents } from 'electron';

import { isCacheAction, isCacheLink } from '../../engine/app-bridge.ts';
import type { CacheFolderManager } from '../cache-folder.ts';
import { deleteItem, listItems } from '../cache-files.ts';
import type { CacheLinks } from '../cache-files.ts';
import type { PathGrants } from '../path-grants.ts';
import type { RescueSlots } from '../rescue.ts';
import type { Registrar } from './register.ts';

/** What the cache folder and the cache manager need of the main process. */
export interface CacheIpc {
  /** The cache folder in use and its choices. */
  cache: CacheFolderManager<WebContents>;
  /** The search folders' links to their puzzles (the cache manager's names). */
  cacheLinks: CacheLinks;
  /** The windows' rescue copies, which the cache manager lists too. */
  rescue: Pick<RescueSlots<number>, 'all'>;
  /** The files each window was handed (a link names only those). */
  paths: Pick<PathGrants<WebContents>, 'allows'>;
}

/** Registers the cache folder's and the cache manager's channels. */
export function installCacheIpc(r: Registrar, h: CacheIpc): void {
  // the cache folder: the renderer names an operation, never a path
  r.handle('sl:cache-folder', null, () => h.cache.info());
  r.handle('sl:cache-choose', null, async (e, win, useDefault) => {
    if (typeof useDefault !== 'boolean') {
      return null;
    }
    if (useDefault) {
      return h.cache.choose(e.sender, undefined);
    }
    const res = await dialog.showOpenDialog(win, {
      title: 'Choose the Cache Folder',
      buttonLabel: 'Use This Folder',
      message: 'ShapeLogic makes a ShapeLogic folder for its cache data in the folder you choose.',
      properties: ['openDirectory', 'createDirectory'],
      defaultPath: h.cache.chosen ?? app.getPath('home'),
    });
    const folder = res.canceled ? undefined : res.filePaths[0];
    if (folder === undefined) {
      return { status: 'canceled' };
    }
    return h.cache.choose(e.sender, folder);
  });
  r.handle('sl:cache-finish', null, (e, _win, action) =>
    isCacheAction(action) ? h.cache.finish(e.sender, action) : null,
  );

  // the cache manager: the renderer names an item by its id, never a path
  let startListed = false;
  r.handle('sl:cache-items', null, async () => {
    await h.cache.start();
    return listItems(h.cache.folders, h.rescue.all(), h.cacheLinks);
  });
  r.handle('sl:cache-items-start', null, async () => {
    await h.cache.start();
    // the notice at the start: once a run, to the first window that asks
    if (startListed) {
      return [];
    }
    startListed = true;
    return listItems(h.cache.folders, h.rescue.all(), h.cacheLinks);
  });
  r.handle('sl:cache-delete', null, async (_e, _win, id) => {
    await h.cache.start();
    return typeof id === 'string'
      ? deleteItem(id, h.cache.folders, h.rescue.all(), h.cacheLinks, () => h.cache.solving())
      : { ok: false, error: 'unknown item' };
  });

  r.on('sl:cache-link', (e, _win, link) => {
    if (!isCacheLink(link)) {
      return;
    }
    // the puzzle's file only when the window was handed it, as the rescue record's
    const path = link.path !== null && h.paths.allows(e.sender, link.path) ? link.path : null;
    void h.cache.start().then(() => h.cacheLinks.record({ ...link, path }, h.cache.folders));
  });
}
