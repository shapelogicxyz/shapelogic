// SPDX-License-Identifier: GPL-3.0-or-later
//
// Calls waiting for a view to show a revision (decision 0012: a mutating
// call settles when the page shows what it made), for the store and the
// workspaces alike. Each wait belongs to a document (the store's
// generation): a new document, or a restarted engine that counts revisions
// from 0 again, releases every wait of the old one, so no edit stays busy
// for the rest of the session (cleanup item 6.1).

import type { Store } from './store.ts';

interface Wait {
  revision: number;
  generation: number;
  done: () => void;
}

/** Promises that resolve when a view shows a revision, or another document came. */
export class RevisionWaits {
  private waits: Wait[] = [];

  /** Resolves when `release` reaches this revision, or another generation is released. */
  wait(revision: number, generation: number): Promise<void> {
    return new Promise((done) => {
      this.waits.push({ revision, generation, done });
    });
  }

  /**
   * The view shows `revision` of document `generation` (or gave up on it):
   * resolves the waits at or below it, and every wait of another generation.
   */
  release(revision: number, generation: number): void {
    const ready = this.waits.filter((w) => w.generation !== generation || w.revision <= revision);
    if (ready.length === 0) {
      return;
    }
    this.waits = this.waits.filter((w) => !ready.includes(w));
    for (const w of ready) {
      w.done();
    }
  }

  /** The number of waits not yet released (for the tests). */
  get size(): number {
    return this.waits.length;
  }
}

/**
 * A workspace's wait for a mutating call's revision: the store's snapshot
 * reaches it, then the workspace's own load of it (`shown`), whose result,
 * success or failure, calls `waits.release`. Resolves at once when another
 * document came meanwhile.
 */
export function whenShown(
  store: Store,
  waits: RevisionWaits,
  shown: () => number,
  revision: number,
): Promise<void> {
  const gen = store.generation;
  return store.whenRevision(revision).then(() => {
    if (shown() >= revision || store.generation !== gen) {
      return;
    }
    return waits.wait(revision, gen);
  });
}
