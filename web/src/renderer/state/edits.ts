// SPDX-License-Identifier: GPL-3.0-or-later
//
// The edits under way, shared by the workspaces (stage 2's Entities
// bookkeeping, moved here for the Puzzle workspace of stage 3): a mutating
// call awaited to its revision (decision 0012's order: the reply, then its
// document.changed, then the snapshot that shows it), its failure reported
// (invalid_state as "pause first"), the refusal before a call the engine
// would refuse as invalid_state (decision 0011), and the count of edits
// under way as `#stage[data-busy]`, which the e2e tests wait on.

import { byId } from '../shell/dom.ts';
import { flash, reportError } from '../shell/status.ts';
import type { Solves } from './solves.ts';
import type { Store } from './store.ts';

export class Edits {
  private readonly store: Store;
  private readonly solves: Solves | null;
  private busy = 0;

  constructor(store: Store, solves: Solves | null = null) {
    this.store = store;
    this.solves = solves;
    this.add(0);
  }

  /** Refuses when `frozen` (a solve or an analysis runs): says so; true when refused. */
  refuse(frozen: boolean = this.store.solving): boolean {
    if (frozen) {
      flash('Pause the solve first', 4000);
      return true;
    }
    return false;
  }

  /**
   * Refuses a change of the lists of shapes, colours or problems, and undo
   * and redo, while anything solves ("pause first").
   */
  refuseList(): boolean {
    return this.refuse(this.store.solving);
  }

  /**
   * Runs a mutating call and waits until the page shows its revision
   * (`settle`, by default the store's snapshot); null when it failed (and
   * says why).
   */
  async mutate<T extends { revision: number }>(
    doing: string,
    call: () => Promise<T>,
    settle: (revision: number) => Promise<void> = (r) => this.store.whenRevision(r),
  ): Promise<T | null> {
    this.add(1);
    try {
      const r = await call();
      await settle(r.revision);
      return r;
    } catch (e) {
      reportError(e, doing);
      return null;
    } finally {
      this.add(-1);
    }
  }

  /** Counts a promise as an edit under way until it settles. */
  async track<T>(p: Promise<T>): Promise<T> {
    this.add(1);
    try {
      return await p;
    } finally {
      this.add(-1);
    }
  }

  /** Changes the count of edits under way (readable from outside: the e2e tests). */
  /** No edit under way (no mutating call waiting for its reply or its revision). */
  get idle(): boolean {
    return this.busy === 0;
  }

  add(d: number): void {
    this.busy += d;
    byId('stage').dataset.busy = String(this.busy);
  }
}
