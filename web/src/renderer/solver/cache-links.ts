// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tells the main process which puzzle and problem the cache folder's
// search folders belong to, so View ▸ Manage Cache and Autosave Files
// names them after it (main/cache-files.ts): the engine names a sliding or
// stacking search's folder by the problem's contents, and only the window
// knows the puzzle's file. Asked of the engine (solve.searchFolders) when a
// run of a sliding or stacking problem ends (a pause saves its search, and
// so does the crash recovery journal's pause), now and then while one runs
// (a sliding search saves itself as it goes, which a crash leaves behind),
// and for every problem when such a puzzle opens (searches saved before
// this was told). Nothing is sent for a folder the engine does not name.

import type { BtApp } from '../../engine/app-bridge.ts';
import type { Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';

/** The least time between two links of one problem while its run goes on (ms). */
const WHILE_RUNNING_MS = 60_000;

export class CacheLinker {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly name: () => string;
  /** When each problem was last linked while its run went on. */
  private readonly last = new Map<string, number>();

  /** `name`: the name the top bar shows the puzzle by. */
  constructor(store: Store, solves: Solves, app: BtApp, name: () => string) {
    this.store = store;
    this.app = app;
    this.name = name;
    let generation = -1;
    store.subscribe((c) => {
      if (c.kind !== 'document' || store.snapshot === null || store.generation === generation) {
        return;
      }
      generation = store.generation;
      this.last.clear();
      if (this.searching()) {
        for (const p of store.snapshot.problems) {
          void this.link(p.id);
        }
      }
    });
    solves.subscribe((c) => {
      if (c.problem === null || !this.searching()) {
        return;
      }
      if (c.kind === 'ended') {
        void this.link(c.problem);
      } else if (c.kind === 'progress') {
        const now = Date.now();
        if (now - (this.last.get(c.problem) ?? 0) >= WHILE_RUNNING_MS) {
          this.last.set(c.problem, now);
          void this.link(c.problem);
        }
      }
    });
  }

  /** Whether the puzzle's searches keep folders in the cache folder (sliding and stacking). */
  private searching(): boolean {
    const grid = this.store.snapshot?.grid;
    return grid === 'sliding' || grid === 'stacking';
  }

  private async link(problem: string): Promise<void> {
    const gen = this.store.generation;
    let folders: string[];
    try {
      ({ folders } = await this.store.engine.call('solve.searchFolders', { problem }));
    } catch {
      // the problem went, or the engine with it: nothing to name
      return;
    }
    const snap = this.store.snapshot;
    const p = snap?.problems.find((x) => x.id === problem);
    if (folders.length === 0 || snap === null || p === undefined || this.store.generation !== gen) {
      return;
    }
    this.app.linkCacheFolders({
      folders,
      path: snap.path,
      name: this.name(),
      problem: p.name !== '' ? p.name : p.label,
    });
  }
}
