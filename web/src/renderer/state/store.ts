// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's mirror of the engine (decision 0013, "State"): the
// document's Snapshot, the engine's info and settings, its status, and the
// shape the view shows. The UI never edits the mirror: it calls the engine
// and reloads what `document.changed` names (stage 1b reloads the whole
// snapshot; it is small). It waits for the engine to say `running` before
// its first call and loads everything again when `restarts` grows (a new
// engine has a new, empty document).

import type { Methods, Settings, ShapeSummary, Snapshot } from '../../engine/api.generated.ts';
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
  /** The name of a document opened from bytes (it has no path). */
  displayName: string | null = null;
  /**
   * Counts the documents: grows when another is opened or the engine was
   * restarted, so ids (s1, s2...) of the old one are not taken for the new.
   */
  generation = 0;
  /** Problems the store met (calls that failed), for the status bar. */
  private readonly errorHandlers = new Set<(e: unknown) => void>();
  private readonly listeners = new Set<(c: StoreChange) => void>();
  private restarts = -1;
  private infoLoads = 0;
  private snapshotLoads = 0;

  constructor(engine: EngineLink) {
    this.engine = engine;
    engine.onStatus((s) => {
      this.onStatus(s);
    });
    engine.on('document.changed', (p) => {
      this.emit({ kind: 'changed', change: p });
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

  /** Whether calls can be made now. */
  get running(): boolean {
    return this.status?.state === 'running';
  }

  /** The shape the view shows, as the snapshot has it. */
  get shape(): ShapeSummary | null {
    return this.snapshot?.shapes.find((s) => s.id === this.selected) ?? null;
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
    this.snapshotLoads++; // a reload under way answers for the old document
    this.displayName = name;
    this.generation++;
    this.selected = null;
    this.setSnapshot(snap, false);
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
      const [info, settings] = await Promise.all([
        this.engine.call('engine.info'),
        this.engine.call('settings.get'),
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
    this.engine.call('document.get').then(
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
  }

  private fail(e: unknown): void {
    // calls fail while the engine restarts; the status says so, and the
    // store loads again when it runs
    if (e instanceof EngineError && e.errc === 'disconnected') {
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
