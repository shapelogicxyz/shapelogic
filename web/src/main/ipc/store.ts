// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.btApp's store, recent files and updates (a group of
// ../app-ipc.ts, cleanup item 13.4): the stored settings and layouts (a
// change of the settings in one window reaches the others, decision
// 0036), Open Recent's list (only a file the window was handed), the app's
// platform and version, and the update check (updates.ts), which asks
// GitHub here, once at startup and when the menu asks; its Download opens
// a fixed address, never one the renderer gives.

import { shell } from 'electron';
import type { WebContents } from 'electron';
import { dirname } from 'node:path';

import type { StoredState } from '../../engine/app-bridge.ts';
import type { PathGrants } from '../path-grants.ts';
import { SYSTEM_URLS } from '../security.ts';
import type { StateStore } from '../state-store.ts';
import { checkForUpdate, parseVersion } from '../updates.ts';
import type { FetchFn } from '../updates.ts';
import { post } from './register.ts';
import type { Registrar } from './register.ts';

/** What the store, the recent files and the update check need of the main process. */
export interface StoreIpc {
  store: StateStore;
  /** The files each window was handed (Open Recent takes only those). */
  paths: Pick<PathGrants<WebContents>, 'allows'>;
  version: string;
  /** Whether the app is packaged: only a packaged app checks for updates at startup. */
  packaged: boolean;
  /** The fetch the update check uses (a session of its own: the windows' lets nothing remote through). */
  fetch: FetchFn;
  /** An update check's outcome in words (updates.ts), for the log and the diagnostics. */
  updateChecked(text: string): void;
  /** The settings were stored again (a window changed one): Debug logging follows them. */
  prefsChanged(prefs: Record<string, unknown>): void;
  /** The app's windows but this one (a change of the settings reaches them). */
  others(sender: WebContents): Pick<WebContents, 'send'>[];
}

/** Registers the store's, the recent files' and the update check's channels. */
export function installStoreIpc(r: Registrar, h: StoreIpc): void {
  // the update check at startup runs once a run, whichever window asks first
  let autoChecked = false;
  r.handle('bt:check-updates', null, async (_e, _win, auto) => {
    if (typeof auto !== 'boolean') {
      return null;
    }
    if (auto) {
      if (!h.packaged || autoChecked) {
        return null;
      }
      autoChecked = true;
    }
    const res = await checkForUpdate(h.version, h.fetch, (text) => {
      h.updateChecked(text);
    });
    if (auto && (res.status !== 'newer' || res.latest === h.store.skipVersion)) {
      return null;
    }
    return res;
  });

  r.on('bt:skip-update', (_e, _win, version) => {
    if (typeof version === 'string' && parseVersion(version) !== null) {
      h.store.setSkipVersion(version);
    }
  });

  r.on('bt:open-releases', () => {
    void shell.openExternal(SYSTEM_URLS.releases);
  });

  r.sync('bt:app-info', null, () => ({ platform: process.platform, version: h.version }));

  r.handle('bt:load-state', null, () => {
    const s: StoredState = {};
    if (h.store.prefs !== undefined) {
      s.prefs = h.store.prefs;
    }
    if (h.store.layout !== undefined) {
      s.layout = h.store.layout;
    }
    return s;
  });

  r.on('bt:save-state', (e, _win, key, value) => {
    if (
      (key === 'prefs' || key === 'layout') &&
      typeof value === 'object' &&
      value !== null &&
      !Array.isArray(value)
    ) {
      const v = value as Record<string, unknown>;
      h.store.set(key, v);
      if (key === 'prefs') {
        h.prefsChanged(v);
        // the settings are the app's: every window shows them (a layout is the window's own)
        for (const c of h.others(e.sender)) {
          post(c, 'bt:prefs', v);
        }
      }
    }
  });

  // only a file the window was handed: Open Recent hands it back (path-grants.ts)
  r.on('bt:recent-add', (e, _win, path) => {
    if (typeof path === 'string' && path !== '' && h.paths.allows(e.sender, path)) {
      h.store.addRecent(path);
      h.store.setLastDir(dirname(path));
    }
  });

  r.on('bt:recent-remove', (_e, _win, path) => {
    if (typeof path === 'string' && path !== '') {
      h.store.removeRecent(path);
    }
  });
}
