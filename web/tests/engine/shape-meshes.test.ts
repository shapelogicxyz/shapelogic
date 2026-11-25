// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view's shared shape meshes (view3d/shape-meshes.ts, cleanup item
// 12.9) over the engine's real meshes: every asker of a mesh gets the one
// geometry, built once; it is freed with its last share; and a slot's load
// that a later one overtook gives its shares back without showing them.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import type { BuiltMesh } from '../../src/renderer/view3d/mesh.ts';
import { MeshCache } from '../../src/renderer/view3d/meshcache.ts';
import { MeshSlot, ShapeMeshes } from '../../src/renderer/view3d/shape-meshes.ts';
import type { MeshAsk } from '../../src/renderer/view3d/shape-meshes.ts';
import { ENGINE, Session } from '../helpers/engine.ts';

// whether a mesh's GPU buffers were let go
const freed = new WeakSet<BuiltMesh>();
const watch = (b: BuiltMesh): BuiltMesh => {
  b.faces.addEventListener('dispose', () => freed.add(b));
  return b;
};

describe.skipIf(ENGINE === null)("the view's shared shape meshes", () => {
  let s: Session;
  let asks: MeshAsk[];
  let calls: number;
  let meshes: ShapeMeshes;
  beforeEach(async () => {
    s = await Session.start();
    const snap = await s.client.call('document.open', {
      path: 'tests/data/puzzles/Bermuda.xmpuzzle',
    });
    asks = snap.shapes.slice(0, 2).map((x) => ({
      shape: x.id,
      style: 'drawing',
      revision: snap.revision,
      size: x.size,
    }));
    const cache = new MeshCache(s.client);
    calls = 0;
    const get = cache.get.bind(cache);
    cache.get = (...a) => {
      calls++;
      return get(...a);
    };
    meshes = new ShapeMeshes(cache, () => 1);
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('gives every asker the one mesh, built once, and frees it with its last share', async () => {
    const [a] = asks;
    if (a === undefined) {
      throw new Error('no shapes');
    }
    let builds = 0;
    const [x, y] = await Promise.all([
      meshes.acquire({ ...a, built: () => builds++ }),
      meshes.acquire({ ...a, built: () => builds++ }),
    ]);
    expect(x.built).toBe(y.built);
    expect(builds).toBe(1);
    expect(calls).toBe(1);
    watch(x.built);
    x.release();
    x.release();
    expect(freed.has(y.built)).toBe(false);
    expect(meshes.size).toBe(1);
    y.release();
    expect(freed.has(y.built)).toBe(true);
    expect(meshes.size).toBe(0);
    // another style is another mesh
    const z = await meshes.acquire({ ...a, style: 'flat' });
    expect(z.built).not.toBe(x.built);
    z.release();
  });

  it('gives back a load that a later one overtook, and what was shown once the next is', async () => {
    const slot = new MeshSlot(meshes);
    const other = new MeshSlot(meshes);
    const shown: string[][] = [];
    const first = slot.load(asks, (l) => shown.push(l.map((m) => `first:${m.shape}`)));
    const second = slot.load(asks.slice(0, 1), (l) => {
      shown.push(l.map((m) => `second:${m.shape}`));
      for (const m of l) {
        watch(m.built);
      }
    });
    await Promise.all([first, second]);
    expect(shown).toEqual([[`second:${asks[0]?.shape ?? ''}`]]);
    // only the second's mesh is held
    expect(meshes.size).toBe(1);
    // another slot shows the same mesh: the one copy
    const kept: BuiltMesh[] = [];
    await other.load(asks.slice(0, 1), (l) => kept.push(...l.map((m) => m.built)));
    expect(kept).toHaveLength(1);
    expect(meshes.size).toBe(1);
    slot.clear(() => undefined);
    expect(meshes.size).toBe(1);
    expect(kept.map((b) => freed.has(b))).toEqual([false]);
    other.clear(() => undefined);
    expect(meshes.size).toBe(0);
    expect(kept.map((b) => freed.has(b))).toEqual([true]);
  });
});
