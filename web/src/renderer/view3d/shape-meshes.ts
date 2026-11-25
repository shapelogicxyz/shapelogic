// SPDX-License-Identifier: GPL-3.0-or-later
//
// One built copy of each shape mesh the view draws (cleanup item 12.9): the
// shape view, the Puzzle's pieces scene, a sliding board's trays and a
// stacking board's discs each built and uploaded their own copy of the
// same mesh before. A mesh is asked for by (document, shape, style, the
// revision the shape last changed at); every asker gets a lease of the one
// geometry, counted, and the geometry is freed when its last lease is
// released. A MeshSlot is one asker's place: a load in it makes the loads
// before it stale (their leases go back as they arrive), and it releases
// what it kept once the next is shown.

import { meshArrays } from '../../engine/frames.ts';
import type { MeshStyle, Size } from '../../engine/api.generated.ts';
import { buildMesh, disposeBuilt } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';
import type { MeshCache, MeshReply } from './meshcache.ts';

/** A mesh asked for. */
export interface MeshAsk {
  shape: string;
  style: MeshStyle;
  /** The revision the shape last changed at (an older one's mesh is not given). */
  revision: number;
  /** The shape's size (a cell index is x + X*(y + Y*z)). */
  size: Size;
  /** Called when this ask built the mesh, with when the build began (the timed test's measure). */
  built?: (start: number, reply: MeshReply) => void;
}

/** A share of a mesh: valid until released. */
export interface MeshLease {
  readonly shape: string;
  readonly built: BuiltMesh;
  /** The engine's reply (its box: lo, hi). */
  readonly reply: MeshReply;
  /** Gives the share back; the mesh is freed with its last share. Once only. */
  release(): void;
}

interface Entry {
  users: number;
  value: Promise<{ built: BuiltMesh; reply: MeshReply }>;
  done: { built: BuiltMesh; reply: MeshReply } | null;
}

/** The shared meshes. */
export class ShapeMeshes {
  private readonly cache: MeshCache;
  private readonly generation: () => number;
  private readonly entries = new Map<string, Entry>();

  /** `generation` names the document (a new one's shapes are new, whatever their ids). */
  constructor(cache: MeshCache, generation: () => number) {
    this.cache = cache;
    this.generation = generation;
  }

  /** How many meshes are held (the tests read it). */
  get size(): number {
    return this.entries.size;
  }

  /** A share of a shape's mesh; it fails as the engine's request did. */
  async acquire(ask: MeshAsk): Promise<MeshLease> {
    const key = `${String(this.generation())}:${ask.shape}:${ask.style}:${String(ask.revision)}`;
    let e = this.entries.get(key);
    if (e === undefined) {
      const entry: Entry = { users: 0, value: this.build(ask), done: null };
      e = entry;
      this.entries.set(key, entry);
      entry.value.then(
        (v) => {
          entry.done = v;
          // every share went back while it was on its way
          if (entry.users === 0) {
            this.forget(key, entry);
          }
        },
        () => {
          // a failed request is not kept: the next ask asks again
          if (this.entries.get(key) === entry) {
            this.entries.delete(key);
          }
        },
      );
    }
    const entry = e;
    entry.users++;
    let v: { built: BuiltMesh; reply: MeshReply };
    try {
      v = await entry.value;
    } catch (err: unknown) {
      entry.users--;
      throw err;
    }
    let held = true;
    return {
      shape: ask.shape,
      built: v.built,
      reply: v.reply,
      release: () => {
        if (!held) {
          return;
        }
        held = false;
        if (--entry.users === 0) {
          this.forget(key, entry);
        }
      },
    };
  }

  private async build(ask: MeshAsk): Promise<{ built: BuiltMesh; reply: MeshReply }> {
    const reply = await this.cache.get(ask.shape, ask.style, ask.revision);
    const t0 = performance.now();
    const built = buildMesh(meshArrays(reply), ask.size, ask.style, {
      kind: 'shape',
      shape: ask.shape,
      revision: ask.revision,
      transformation: 0,
    });
    ask.built?.(t0, reply);
    return { built, reply };
  }

  // the last share went back: free the mesh (once it has come)
  private forget(key: string, entry: Entry): void {
    if (entry.done === null) {
      return;
    }
    if (this.entries.get(key) === entry) {
      this.entries.delete(key);
    }
    disposeBuilt(entry.done.built);
  }
}

/**
 * One asker's meshes: a load makes the loads before it stale, and what is
 * shown is kept until the next is.
 */
export class MeshSlot {
  private readonly meshes: ShapeMeshes;
  private count = 0;
  private kept: readonly MeshLease[] = [];

  constructor(meshes: ShapeMeshes) {
    this.meshes = meshes;
  }

  /**
   * Asks for the meshes, and once they all came, while this is still the
   * latest load, shows them (`show`, in the order asked) and gives back
   * what was shown before. A load that a later one or `stale()` overtook
   * gives its shares back. It fails as the first request that failed,
   * while it is the latest.
   */
  async load(asks: readonly MeshAsk[], show: (leases: MeshLease[]) => void): Promise<void> {
    const n = this.stale();
    const got = await Promise.allSettled(asks.map((a) => this.meshes.acquire(a)));
    const leases = got.flatMap((r) => (r.status === 'fulfilled' ? [r.value] : []));
    const failed = got.find((r) => r.status === 'rejected');
    if (n !== this.count || failed !== undefined) {
      for (const l of leases) {
        l.release();
      }
      if (n === this.count && failed !== undefined) {
        throw failed.reason as Error;
      }
      return;
    }
    show(leases);
    this.keep(leases);
  }

  /** Makes the loads on their way stale (what is shown already is right). */
  stale(): number {
    return ++this.count;
  }

  /** How many loads began (a load's number, read right after it began: still the latest while equal). */
  get loads(): number {
    return this.count;
  }

  /** Shows nothing (`show`), with the loads on their way stale, and gives back what was shown. */
  clear(show: () => void): void {
    this.stale();
    show();
    this.keep([]);
  }

  private keep(leases: readonly MeshLease[]): void {
    const old = this.kept;
    this.kept = leases;
    for (const l of old) {
      l.release();
    }
  }
}
