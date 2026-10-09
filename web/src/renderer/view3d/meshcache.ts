// SPDX-License-Identifier: GPL-3.0-or-later
//
// Meshes from the engine, cached by shape, style and transformation, with
// the document revision they were asked at (decision 0013, "State").
// Requests are coalesced by the key mesh:<shape>:<style>:<transformation>,
// so a burst of edits never queues more than two meshes of a shape at the
// engine (client.ts). `document.changed` drops what it names (no ids: all).

import type { MeshStyle } from '../../engine/api.generated.ts';
import type { EngineApi, Reply } from '../../engine/client-types.ts';

/** A shape.mesh reply: the frame with its typed-array views. */
export type MeshReply = Reply<'shape.mesh'>;

interface Entry {
  revision: number;
  promise: Promise<MeshReply>;
}

/** The newest meshes asked for, at most this many kept. */
const LIMIT = 48;

export class MeshCache {
  private readonly engine: EngineApi;
  private readonly entries = new Map<string, Entry>();

  constructor(engine: EngineApi) {
    this.engine = engine;
    engine.on('document.changed', (p) => {
      if (p.what.includes('shapes') || p.what.includes('colours')) {
        this.drop(p.shapes.length === 0 || p.what.includes('colours') ? null : p.shapes);
      }
    });
  }

  /** The coalescing key of a mesh request. */
  static key(shape: string, style: MeshStyle, transformation = 0): string {
    return `mesh:${shape}:${style}:${transformation}`;
  }

  /**
   * The mesh of a shape; `revision` is the document's now (a cached mesh
   * asked at an older one is asked again). Fails as `superseded` when a
   * newer request of the same key took its place before it was sent.
   */
  get(shape: string, style: MeshStyle, revision: number, transformation = 0): Promise<MeshReply> {
    const key = MeshCache.key(shape, style, transformation);
    const hit = this.entries.get(key);
    if (hit !== undefined && hit.revision >= revision) {
      // most recently used last
      this.entries.delete(key);
      this.entries.set(key, hit);
      return hit.promise;
    }
    const promise = this.engine.call(
      'shape.mesh',
      { shape, style, ...(transformation === 0 ? {} : { transformation }) },
      { coalesce: key },
    );
    const entry = { revision, promise };
    this.entries.delete(key);
    this.entries.set(key, entry);
    promise.catch(() => {
      // a failed request is not kept
      if (this.entries.get(key) === entry) {
        this.entries.delete(key);
      }
    });
    while (this.entries.size > LIMIT) {
      const oldest = this.entries.keys().next().value;
      if (oldest === undefined) {
        break;
      }
      this.entries.delete(oldest);
    }
    return promise;
  }

  /** Forgets the meshes of these shapes (null: of every shape). */
  drop(shapes: readonly string[] | null): void {
    if (shapes === null) {
      this.entries.clear();
      return;
    }
    for (const key of [...this.entries.keys()]) {
      if (shapes.some((s) => key.startsWith(`mesh:${s}:`))) {
        this.entries.delete(key);
      }
    }
  }
}
