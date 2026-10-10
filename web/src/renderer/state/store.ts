// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's mirror of the engine (decision 0013, "State"): the
// document's Snapshot, the engine's info and settings, its status, and the
// shape the view shows. The UI never edits the mirror: it calls the engine
// and reloads what `document.changed` names (the whole snapshot: it is
// small; the reloads are coalesced, so a drag's burst of changes asks for
// at most two), and waits for a call's revision (`whenRevision`) before it
// acts on what the call made. It waits for the engine to say `running`
// before its first call and loads everything again when `restarts` grows
// (a new engine has a new, empty document).

import type {
  Grid,
  Methods,
  Settings,
  ShapeSummary,
  Snapshot,
} from '../../engine/api.generated.ts';
import type { EngineApi, EngineStatus, EventParams } from '../../engine/client-types.ts';
import { EngineError } from '../../engine/errors.ts';

/** The engine as the store needs it: the proxy (engine-proxy.ts) or a test's stand-in. */
export interface EngineLink extends EngineApi {
  readonly status: EngineStatus | null;
  onStatus(handler: (status: EngineStatus) => void): () => void;
}

export type EngineInfo = Methods['engine.info']['result'];

/** What changed in the store. */
export type StoreChange =
  | { kind: 'status' }
  /** The snapshot was replaced: another document, or a reload after a change. */
  | { kind: 'document'; restarted: boolean }
  /** The engine said the document changed (after the call that changed it). */
  | { kind: 'changed'; change: EventParams<'document.changed'> }
  | { kind: 'selection' }
  | { kind: 'settings' };

export class Store {
  readonly engine: EngineLink;
  /** The engine's status as the host last told it. */
  status: EngineStatus | null = null;
  info: EngineInfo | null = null;
  settings: Settings | null = null;
  snapshot: Snapshot | null = null;
  /** The shape the view shows, or null. */
  selected: string | null = null;
  /** The name of a document without a file that the top bar shows (opened from bytes, a rescue copy), or null. */
  displayName: string | null = null;
  /**
   * Counts the documents: grows when another is opened or the engine was
   * restarted, so ids (s1, s2...) of the old one are not taken for the new.
   */
  generation = 0;
  /** The engine settings each engine is given when it starts (the stored ones). */
  wantedSettings: () => Partial<Settings> = () => ({});
  /** Problems the store met (calls that failed), for the status bar. */
  private readonly errorHandlers = new Set<(e: unknown) => void>();
  /** What a change or an opened file did beyond what was asked (the original's message boxes). */
  private readonly noticeHandlers = new Set<(text: string) => void>();
  private readonly listeners = new Set<(c: StoreChange) => void>();
  private restarts = -1;
  private infoLoads = 0;
  private snapshotLoads = 0;
  /** Calls waiting for the snapshot to reach a revision. */
  private waiting: { revision: number; generation: number; done: () => void }[] = [];

  constructor(engine: EngineLink) {
    this.engine = engine;
    engine.onStatus((s) => {
      this.onStatus(s);
    });
    engine.on('document.changed', (p) => {
      this.emit({ kind: 'changed', change: p });
      if (p.notice !== undefined) {
        this.notice(p.notice);
      }
      this.reloadSnapshot();
    });
    if (engine.status !== null) {
      this.onStatus(engine.status);
    }
  }

  /** Listens to changes; returns the unsubscribe. */
  subscribe(listener: (c: StoreChange) => void): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  /** Listens to failed calls the store made on its own. */
  onError(handler: (e: unknown) => void): () => void {
    this.errorHandlers.add(handler);
    return () => {
      this.errorHandlers.delete(handler);
    };
  }

  /**
   * Listens to notices: what a change did beyond what was asked
   * (`document.changed.notice`) or what opening a file changed
   * (`Snapshot.notice`), to tell the user. Returns the unsubscribe.
   */
  onNotice(handler: (text: string) => void): () => void {
    this.noticeHandlers.add(handler);
    return () => {
      this.noticeHandlers.delete(handler);
    };
  }

  /** Whether calls can be made now. */
  get running(): boolean {
    return this.status?.state === 'running';
  }

  /** The shape the view shows, as the snapshot has it. */
  get shape(): ShapeSummary | null {
    return this.snapshot?.shapes.find((s) => s.id === this.selected) ?? null;
  }

  /** A solve or an analysis is active: shapes and lists are frozen (decision 0011). */
  get solving(): boolean {
    return this.snapshot?.solving ?? false;
  }

  /**
   * Resolves when the snapshot shows a revision at least this one (a
   * mutating call's), or when another document took its place.
   */
  whenRevision(revision: number): Promise<void> {
    if (this.snapshot !== null && this.snapshot.revision >= revision) {
      return Promise.resolve();
    }
    return new Promise((done) => {
      this.waiting.push({ revision, generation: this.generation, done });
    });
  }

  /** The place of a shape in the list (its default colour's number), or -1. */
  indexOf(shape: string): number {
    return this.snapshot?.shapes.findIndex((s) => s.id === shape) ?? -1;
  }

  /** Shows another shape. */
  select(shape: string | null): void {
    if (shape !== this.selected) {
      this.selected = shape;
      this.emit({ kind: 'selection' });
    }
  }

  /** Opens a puzzle from its file's bytes; `name` is what the top bar shows (the document has no path). */
  async openBytes(name: string, bytes: Uint8Array): Promise<void> {
    const snap = await this.engine.call('document.openBytes', { data: toBase64(bytes) });
    this.replaced(snap, name);
  }

  /** Opens a puzzle file. */
  async openPath(path: string): Promise<void> {
    const snap = await this.engine.call('document.open', { path });
    this.replaced(snap, null);
  }

  /**
   * Reopens a rescue copy (the file's bytes, base64): the document stands
   * for its file (`path`, or none) and has unsaved changes.
   */
  async openRescue(data: string, path: string | null, name: string): Promise<void> {
    const snap = await this.engine.call('document.openBytes', {
      data,
      dirty: true,
      ...(path === null ? {} : { path }),
    });
    this.replaced(snap, path === null ? name : null);
  }

  /** Opens a paused solve (.btsolve) as a new, untitled document; its problem. */
  async importPaused(path: string): Promise<string> {
    const r = await this.engine.call('solve.importPaused', { path });
    this.replaced(r.document, null);
    return r.problem;
  }

  /** A new empty puzzle on a grid (the grid never changes). */
  async newDocument(grid: Grid): Promise<void> {
    const snap = await this.engine.call('document.new', { grid });
    this.replaced(snap, null);
  }

  /**
   * Saves to a file (the document's own when none is given) and waits for
   * the snapshot that shows it clean: a save sends no document.changed.
   * Returns the file's path.
   */
  async save(path?: string): Promise<string> {
    const r = await this.engine.call('document.save', path === undefined ? {} : { path });
    // the save has a revision of its own; the snapshot read after it shows the document clean
    this.reloadSnapshot();
    await this.whenRevision(r.revision);
    return r.path;
  }

  // another document took the place of the old one
  private replaced(snap: Snapshot, name: string | null): void {
    this.snapshotLoads++; // a reload under way answers for the old document
    this.displayName = name;
    this.generation++;
    this.selected = null;
    this.setSnapshot(snap, false);
    if (snap.notice !== undefined) {
      this.notice(snap.notice);
    }
  }

  private notice(text: string): void {
    for (const h of [...this.noticeHandlers]) {
      try {
        h(text);
      } catch (e) {
        console.error('store: a notice handler threw', e);
      }
    }
  }

  /** Changes engine settings (undo depth, threads); the store mirrors the answer. */
  async setEngineSettings(change: Partial<Settings>): Promise<void> {
    this.settings = await this.engine.call('settings.set', change);
    this.emit({ kind: 'settings' });
  }

  private onStatus(s: EngineStatus): void {
    this.status = s;
    this.emit({ kind: 'status' });
    if (s.state === 'running' && s.restarts !== this.restarts) {
      const restarted = this.restarts >= 0;
      this.restarts = s.restarts;
      void this.loadAll(restarted);
    }
  }

  private async loadAll(restarted: boolean): Promise<void> {
    const n = ++this.infoLoads;
    if (restarted) {
      this.displayName = null;
      this.generation++;
      this.selected = null;
    }
    this.reloadSnapshot(restarted);
    try {
      // the settings kept from the last session (undo depth, threads) go to each new engine
      const wanted = this.wantedSettings();
      const [info, settings] = await Promise.all([
        this.engine.call('engine.info'),
        Object.keys(wanted).length > 0
          ? this.engine.call('settings.set', wanted)
          : this.engine.call('settings.get'),
      ]);
      if (n === this.infoLoads) {
        this.info = info;
        this.settings = settings;
        this.emit({ kind: 'settings' });
      }
    } catch (e) {
      this.fail(e);
    }
  }

  private reloadSnapshot(restarted = false): void {
    const n = ++this.snapshotLoads;
    this.engine.call('document.get', {}, { coalesce: 'document.get' }).then(
      (snap) => {
        // the newest load answers; after a restart the revision starts again
        if (n === this.snapshotLoads) {
          this.setSnapshot(snap, restarted);
        }
      },
      (e: unknown) => {
        this.fail(e);
      },
    );
  }

  private setSnapshot(snap: Snapshot, restarted: boolean): void {
    this.snapshot = snap;
    if (this.selected === null || !snap.shapes.some((s) => s.id === this.selected)) {
      this.selected = snap.shapes[0]?.id ?? null;
    }
    this.emit({ kind: 'document', restarted });
    const ready = this.waiting.filter(
      (w) => w.generation !== this.generation || w.revision <= snap.revision,
    );
    this.waiting = this.waiting.filter((w) => !ready.includes(w));
    for (const w of ready) {
      w.done();
    }
  }

  private fail(e: unknown): void {
    // calls fail while the engine restarts; the status says so, and the
    // store loads again when it runs; a reload a newer one replaced is no news
    if (e instanceof EngineError && (e.errc === 'disconnected' || e.errc === 'superseded')) {
      return;
    }
    for (const h of [...this.errorHandlers]) {
      h(e);
    }
  }

  private emit(c: StoreChange): void {
    for (const l of [...this.listeners]) {
      try {
        l(c);
      } catch (e) {
        console.error('store: a listener threw', e);
      }
    }
  }
}

/** Bytes as base64 (document.openBytes), without Node. */
export function toBase64(bytes: Uint8Array): string {
  let s = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    s += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(s);
}
