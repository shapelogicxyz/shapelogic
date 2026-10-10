// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rescue copy (stage 6), as the original's `__rescue.xmpuzzle`: while
// the document has unsaved changes, its bytes (document.saveBytes) are
// written to the app's data directory a moment after each change, at most
// every two seconds; once the document is clean (saved, or another one
// opened after the question about the old one), the copy is removed. When
// the engine dies, the host starts another with an empty document; the
// copy is then offered (Reopen: the bytes opened as the document they were,
// with its file and its unsaved changes: document.openBytes {path,
// dirty}). A copy left by a session that ended without closing (the app
// died) is offered when the app starts. Nothing is written or removed while
// an offer waits for its answer (a newer offer, after another death, takes
// an older one's place: the copy stays for it), and a copy not answered yet
// survives the window's close, as does one the close could not save because
// the engine was not running: both are offered at the next start. Nothing
// is written while a solve or
// an analysis is active or a call that may start one is under way (the
// engine refuses saveBytes then; the solve's own autosave keeps its
// work). Writes and removals run one at a time, in order, so a
// removal is never overtaken by a write that started before it.

import type { BtApp, RescueInfo } from '../../engine/app-bridge.ts';
import type { Store } from '../state/store.ts';
import { askChoice } from './dialog.ts';
import { byId } from './dom.ts';
import { flash, reportError } from './status.ts';

/** The quickest a change is written after the one before (ms). */
const EVERY_MS = 2000;
/** The wait after a change before it is written (ms): a burst of changes is one write. */
const AFTER_MS = 300;

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

function ago(time: number): string {
  const s = Math.max(0, Math.round((Date.now() - time) / 1000));
  return s < 90
    ? `${s} seconds ago`
    : s < 5400
      ? `${Math.round(s / 60)} minutes ago`
      : new Date(time).toLocaleString();
}

export class Rescue {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly quiet: () => boolean;
  private timer: number | null = null;
  private lastWrite = 0;
  /** The writes and removals, one after the other. */
  private chain: Promise<void> = Promise.resolve();
  /** Whether a copy may exist (written this session, or left by the last). */
  private copy = true;
  /** The copy a last session left was looked at (offered, or there was none). */
  private looked = false;
  /** Offers waiting for their answer (after a second death, two: the newer shown). */
  private asking = 0;
  /** The window is closing: nothing more is written. */
  private closed = false;

  /**
   * `quiet`: nothing is under way that the engine would refuse
   * saveBytes beside (an edit or a run's call waiting for its answer, a
   * run, an analysis); checked in the same task as the call is made.
   */
  constructor(store: Store, app: BtApp, quiet: () => boolean = () => true) {
    this.store = store;
    this.app = app;
    this.quiet = quiet;
    let started = false;
    store.subscribe((c) => {
      if (c.kind !== 'document') {
        return;
      }
      if (!started && store.snapshot !== null) {
        started = true;
        if (store.snapshot.dirty) {
          // a reload of the page: the engine's document is newer than any copy
          this.looked = true;
          this.changed();
        } else {
          // the first document: a copy the last session left?
          void this.offer(false);
        }
      } else if (c.restarted) {
        // the engine died: its document is gone, the copy holds its last changes
        void this.offer(true);
      } else {
        this.changed();
      }
    });
  }

  /**
   * Nothing is written or removed: before the copy a last session left was
   * looked at, and while an offer waits for its answer.
   */
  private held(): boolean {
    return !this.looked || this.asking > 0;
  }

  // read through a call: the flags change across the awaits of write()
  private stopped(): boolean {
    return this.closed || this.held();
  }

  /** The revision the copy was last written at (for the tests: `#stage[data-rescued]`). */
  private written(revision: number | null): void {
    byId('stage').dataset.rescued = revision === null ? '' : String(revision);
  }

  // the document changed: write the copy soon, or remove it
  private changed(): void {
    if (this.held() || this.closed) {
      return;
    }
    const snap = this.store.snapshot;
    if (snap === null) {
      return;
    }
    if (!snap.dirty) {
      this.cancel();
      if (this.copy) {
        this.copy = false;
        this.enqueue(() => this.app.clearRescue()).then(() => {
          this.written(null);
        }, noop);
      }
      return;
    }
    if (snap.solving || this.timer !== null) {
      return;
    }
    const wait = Math.max(AFTER_MS, this.lastWrite + EVERY_MS - Date.now());
    this.timer = window.setTimeout(() => {
      this.timer = null;
      this.lastWrite = Date.now();
      void this.enqueue(() => this.write());
    }, wait);
  }

  private cancel(): void {
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }

  private enqueue(op: () => Promise<void>): Promise<void> {
    const p = this.chain.then(op);
    this.chain = p.catch(noop);
    return p;
  }

  private async write(): Promise<void> {
    const snap = this.store.snapshot;
    if (this.stopped() || snap === null || !snap.dirty || snap.solving) {
      return;
    }
    if (!this.quiet()) {
      // something under way: a moment later
      this.timer ??= window.setTimeout(() => {
        this.timer = null;
        this.changed();
      }, AFTER_MS);
      return;
    }
    const gen = this.store.generation;
    let data: string;
    try {
      data = (await this.store.engine.call('document.saveBytes')).data;
    } catch {
      // a run started, or the engine went: the next change writes again
      return;
    }
    const now = this.store.snapshot;
    if (this.stopped() || this.store.generation !== gen || now?.dirty !== true) {
      return;
    }
    const info: RescueInfo = {
      path: now.path,
      name: now.path === null ? (this.store.displayName ?? 'Untitled') : baseName(now.path),
      revision: snap.revision,
      time: Date.now(),
    };
    try {
      await this.app.writeRescue(data, info);
      this.copy = true;
      this.written(snap.revision);
    } catch (e) {
      reportError(e, 'The rescue copy could not be written');
    }
    // changes that came while it was written
    if (this.store.snapshot !== null && this.store.snapshot.revision !== snap.revision) {
      this.changed();
    }
  }

  /** Offers the copy, if there is one: after the engine died, or one a last session left. */
  private async offer(restarted: boolean): Promise<void> {
    this.asking++;
    this.cancel();
    try {
      await this.chain;
      const found = await this.app.readRescue();
      if (found === null) {
        this.copy = false;
        return;
      }
      const { info } = found;
      const what = info.path === null ? `“${info.name}”` : `“${baseName(info.path)}”`;
      const k = await askChoice(
        'rescueAsk',
        restarted ? 'The engine stopped' : 'A rescue copy was kept',
        restarted
          ? `The engine stopped and was started again with an empty puzzle. The unsaved changes of ${what} were kept in a rescue copy (${ago(info.time)}).\nReopen it?`
          : `BurrTools Alpha did not close properly. The unsaved changes of ${what} were kept in a rescue copy (${ago(info.time)}).\nReopen it?`,
        [
          { key: 'drop', label: 'Discard', kind: 'danger' },
          { key: 'open', label: 'Reopen', kind: 'pri', default: true },
        ],
        true,
      );
      if (k === 'open') {
        try {
          await this.store.openRescue(found.data, info.path, info.name);
          flash(`Reopened the rescue copy of ${what}: save it to keep it`, 5000);
          return;
        } catch (e) {
          // kept on disk: offered again at the next start
          reportError(e, 'The rescue copy could not be reopened');
          return;
        }
      }
      if (k === 'drop') {
        await this.app.clearRescue();
        this.copy = false;
        this.written(null);
      }
      // no answer (a newer offer took the question's place after another
      // death, or the page went): the copy stays, for that offer or the next start
    } catch (e) {
      reportError(e, 'The rescue copy');
    } finally {
      this.asking--;
      this.looked = true;
      this.changed();
    }
  }

  /**
   * The window closes (the document saved, or its changes given up): no
   * more writes, and the copy removed. A copy not answered yet (its offer
   * waits, or the one a last session left was not looked at) stays, and so
   * does one the close could not save because the engine is not running:
   * each is offered at the next start.
   */
  async discard(): Promise<void> {
    this.closed = true;
    this.cancel();
    if (this.held() || !this.store.running) {
      return;
    }
    await this.enqueue(() => this.app.clearRescue()).catch(noop);
    this.copy = false;
  }
}

const noop = (): void => undefined;
