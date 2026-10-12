// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exports of stage 7 against the real engine: the solution animation
// (.glb) of every fixture solution with its moves stored passes Khronos's
// glTF validator and, read back as a viewer reads it, puts every piece
// where the player puts it at sampled times (the holds, the moves, the
// moves backwards; between keyframes too), its mesh the playback's; the
// engine's STL file (export.stl, deferred part 5) of a one-cell cube is a
// closed surface, every face facing out, and every fixture shape's STL
// loads in three.js's STL viewer code with its triangles and its size and
// is a closed, consistently oriented surface (each edge used as often one
// way as the other).

import { readdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { STLLoader } from 'three/addons/loaders/STLLoader.js';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';

import { fileBytes, meshArrays, poseArrays } from '../../src/engine/frames.ts';
import type { MeshArrays } from '../../src/engine/frames.ts';
import { ANIMATION_SAMPLES, animationGlb } from '../../src/renderer/export/glb.ts';
import { PoseTrack } from '../../src/renderer/solver/poses.ts';
import { ENGINE, FIXTURES, ROOT, Session } from '../helpers/engine.ts';
import { Glb, installFileReader, validateGlb } from '../helpers/gltf.ts';

const SECONDS = 0.5;
const CELL = 0.01;

/**
 * The closed surface's check: `unpaired` edges are used more often one way
 * than the other (an open or inconsistently oriented surface), `shared`
 * ones more than once each way (a manifold has none).
 */
function edgeBalance(positions: ArrayLike<number>): {
  unpaired: number;
  shared: number;
  triangles: number;
} {
  const key = (k: number): string =>
    [0, 1, 2].map((a) => (positions[3 * k + a] ?? 0).toFixed(4)).join(',');
  const edges = new Map<string, number>();
  const triangles = positions.length / 9;
  for (let t = 0; t < triangles; t++) {
    for (let j = 0; j < 3; j++) {
      const a = key(3 * t + j);
      const b = key(3 * t + ((j + 1) % 3));
      edges.set(`${a}>${b}`, (edges.get(`${a}>${b}`) ?? 0) + 1);
    }
  }
  let unpaired = 0;
  let shared = 0;
  for (const [e, n] of edges) {
    const [a, b] = e.split('>');
    if (edges.get(`${b ?? ''}>${a ?? ''}`) !== n) {
      unpaired++;
    } else if (n > 1 && (a ?? '') < (b ?? '')) {
      // each edge once, not each way
      shared++;
    }
  }
  return { unpaired, shared, triangles };
}

/** The positions of a binary STL's triangles (its normals left out). */
function stlPositions(bytes: Uint8Array): Float32Array {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const n = view.getUint32(80, true);
  const out = new Float32Array(9 * n);
  for (let t = 0; t < n; t++) {
    for (let k = 0; k < 9; k++) {
      out[9 * t + k] = view.getFloat32(84 + 50 * t + 12 + 4 * k, true);
    }
  }
  return out;
}

/** The fixtures whose large shapes take the Minkowski mesher long, or are large spheres. */
const SLOW = new Set([
  '12PieceSeparation.xmpuzzle',
  'AugmentedSecondStellation.xmpuzzle',
  'BallRoom.xmpuzzle',
  'BallRoom_solved.xmpuzzle',
]);

describe.skipIf(ENGINE === null)('the exports and the engine', () => {
  let s: Session;
  beforeAll(() => {
    installFileReader();
  });
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('writes a valid .glb of every stored disassembly whose pieces move as the player’s', async () => {
    const c = s.client;
    let checked = 0;
    let turns = 0;
    const files = readdirSync(resolve(ROOT, FIXTURES)).filter((f) => f.endsWith('.xmpuzzle'));
    for (const f of files.sort()) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      if (snap.grid === 'sliding' || snap.grid === 'stacking') {
        continue;
      }
      for (const prob of snap.problems) {
        const page = await c.call('solution.page', { problem: prob.id, limit: 2 });
        for (const row of page.rows.filter((r) => r.disassembly === 'tree')) {
          const what = `${f} ${prob.id} ${row.id}`;
          const pb = await c.call('playback.open', { problem: prob.id, solution: row.id });
          const meshes: MeshArrays[] = [];
          for (let i = 0; i < pb.pieces.length; i++) {
            meshes.push(meshArrays(await c.call('playback.mesh', { handle: pb.handle, piece: i })));
          }
          const track = poseArrays(
            await c.call('playback.track', {
              handle: pb.handle,
              samplesPerStep: ANIMATION_SAMPLES,
              secondsPerStep: SECONDS,
              reassemble: true,
            }),
            'time',
          );
          const glb = await animationGlb({
            name: `Solution ${String(row.index + 1)}`,
            pieces: meshes.map((mesh, i) => ({
              name: pb.pieces[i]?.name ?? '',
              colour: [0.2 + 0.1 * (i % 5), 0.5, 0.8],
              mesh,
            })),
            track,
            cellSize: CELL,
          });

          // Khronos's validator: no errors, no warnings
          const report = await validateGlb(glb);
          expect(
            report.issues.messages.filter((m) => m.severity <= 1),
            what,
          ).toEqual([]);

          // the player's poses: the engine's samples at every step, interpolated
          const player = new PoseTrack(pb.pieces.length, pb.steps);
          for (let k = 0; k < player.chunkCount; k++) {
            const { from, to } = player.range(k);
            player.add(
              k,
              poseArrays(
                await c.call('playback.poses', { handle: pb.handle, from, to, samplesPerStep: 8 }),
                'step',
              ),
            );
          }

          const file = new Glb(glb);
          const root = file.json.nodes.findIndex((n) => n.name === 'Puzzle');
          expect(file.json.nodes[root]?.scale).toEqual([CELL, CELL, CELL]);
          const children = file.json.nodes[root]?.children ?? [];
          expect(children.map((n) => file.json.nodes[n]?.name)).toEqual(
            pb.pieces.map((p) => p.name),
          );
          expect(file.json.animations[0]?.name).toBe('Disassembly');
          expect(file.json.scenes[file.json.scene ?? 0]?.name).toBe(
            `Solution ${String(row.index + 1)}`,
          );

          // each piece's mesh is the playback's (its outer triangles)
          children.forEach((n, i) => {
            const prim = file.json.meshes[file.json.nodes[n]?.mesh ?? -1]?.primitives[0];
            const at = file.accessor(prim?.attributes.POSITION ?? -1);
            const idx = file.accessor(prim?.indices ?? -1);
            const m = meshes[i];
            if (m === undefined) {
              throw new Error(`${what}: no mesh of piece ${String(i)}`);
            }
            const outer = [...m.flags].filter((fl) => (fl & 128) === 0).length;
            expect(idx.length, `${what} piece ${String(i)}`).toBe(3 * outer);
            const box = (p: ArrayLike<number>, idx2: ArrayLike<number>): number[] => {
              const lo = [Infinity, Infinity, Infinity];
              const hi = [-Infinity, -Infinity, -Infinity];
              for (const v of Array.from(idx2)) {
                for (let a = 0; a < 3; a++) {
                  const x = p[3 * v + a] ?? 0;
                  lo[a] = Math.min(lo[a] ?? 0, x);
                  hi[a] = Math.max(hi[a] ?? 0, x);
                }
              }
              return [...lo, ...hi];
            };
            const want = box(m.positions, m.indices);
            box(at, idx).forEach((x, a) => {
              expect(x).toBeCloseTo(want[a] ?? 0, 5);
            });
          });

          // the times: hold, moves, hold, moves backwards, hold
          const hold = 2 * SECONDS;
          const forward = pb.steps * SECONDS;
          expect(track.at.at(-1)).toBeCloseTo(3 * hold + 2 * forward, 4);
          const probes: { time: number; step: number; visibility: boolean }[] = [];
          probes.push({ time: 0.3 * hold, step: 0, visibility: true });
          for (let q = 0; q <= pb.steps * 16; q++) {
            const step = q / 16;
            probes.push({ time: hold + step * SECONDS, step, visibility: true });
            // backwards: a STEP sampler holds the later key, the player the earlier sample
            probes.push({
              time: 2 * hold + forward + step * SECONDS,
              step: pb.steps - step,
              visibility: false,
            });
          }
          probes.push({ time: hold + forward + 0.5 * hold, step: pb.steps, visibility: true });
          probes.push({ time: 3 * hold + 2 * forward, step: 0, visibility: true });
          for (const p of probes) {
            const poses = player.at(p.step, false);
            expect(poses).not.toBeNull();
            // the samples either side: beside a piece's vanishing the .glb keeps the
            // pose it had while shown (gltfexport.cpp: it neither stalls nor drifts)
            const before = player.at(
              Math.floor(p.step * ANIMATION_SAMPLES) / ANIMATION_SAMPLES,
              false,
            );
            const after = player.at(
              Math.ceil(p.step * ANIMATION_SAMPLES) / ANIMATION_SAMPLES,
              false,
            );
            children.forEach((n, i) => {
              const want = poses?.[i];
              const got = file.trsAt(n, p.time);
              const shown = (got.s[0] ?? 0) > 0.5;
              const label = `${what} piece ${String(i)} at ${p.time.toFixed(3)} s (step ${p.step.toFixed(3)})`;
              if (p.visibility) {
                expect(shown, label).toBe(want?.visible);
              }
              if (
                want?.visible !== true ||
                !shown ||
                before?.[i]?.visible !== true ||
                after?.[i]?.visible !== true
              ) {
                return;
              }
              for (let a = 0; a < 3; a++) {
                expect(Math.abs((got.t[a] ?? 0) - (want.t[a] ?? 0)), label).toBeLessThan(1e-3);
              }
              const dot = Math.abs(got.r.reduce((sum, x, a) => sum + x * (want.q[a] ?? 0), 0));
              expect(dot, label).toBeGreaterThan(1 - 1e-5);
            });
          }
          turns +=
            file.json.animations[0]?.channels.filter((ch) => ch.target.path === 'rotation')
              .length ?? 0;
          checked++;
          await c.call('playback.close', { handle: pb.handle });
        }
      }
    }
    // the fixtures' stored disassemblies, among them turns (test_rotation_solvers)
    expect(checked).toBeGreaterThanOrEqual(7);
    expect(turns).toBeGreaterThan(0);
  }, 120_000);

  it('writes the STL of a one-cell cube as a closed surface facing out', async () => {
    const c = s.client;
    await c.call('document.new', { grid: 'bricks' });
    const { shape } = await c.call('shape.create', { size: [1, 1, 1] });
    await c.call('shape.setCells', { shape, tool: 'fixed', cells: [[0, 0, 0]] });
    // plain cells, unit size 10
    const parameters = [10, 0, 0, 0, 0, 1];
    const bin = fileBytes(await c.call('export.stl', { shape, parameters, name: 'cube' }));
    expect(bin.length).toBe(84 + 12 * 50);
    expect(new TextDecoder().decode(bin.subarray(0, 4))).toBe('cube');
    const pos = stlPositions(bin);
    expect(edgeBalance(pos)).toEqual({ unpaired: 0, shared: 0, triangles: 12 });
    // facing out: the signed volume is the cube's, as the preview says
    let volume = 0;
    for (let t = 0; t < pos.length; t += 9) {
      const [ax, ay, az, bx, by, bz, cx, cy, cz] = Array.from(pos.slice(t, t + 9));
      volume +=
        ((ax ?? 0) * ((by ?? 0) * (cz ?? 0) - (bz ?? 0) * (cy ?? 0)) -
          (ay ?? 0) * ((bx ?? 0) * (cz ?? 0) - (bz ?? 0) * (cx ?? 0)) +
          (az ?? 0) * ((bx ?? 0) * (cy ?? 0) - (by ?? 0) * (cx ?? 0))) /
        6;
    }
    expect(volume).toBeCloseTo(1000, 3);
    expect((await c.call('export.stlMesh', { shape, parameters })).volume).toBeCloseTo(1000, 3);
    // the text file: the same triangles
    const text = new TextDecoder().decode(
      fileBytes(await c.call('export.stl', { shape, parameters, binary: false, name: 'cube' })),
    );
    expect(text.startsWith('solid cube\n')).toBe(true);
    expect(text.match(/facet normal/g)).toHaveLength(12);
  });

  it('writes an STL of every fixture shape that a viewer loads, a closed surface of its size', async () => {
    const c = s.client;
    const loader = new STLLoader();
    let shapes = 0;
    let shared = 0;
    const files = readdirSync(resolve(ROOT, FIXTURES)).filter(
      (f) => f.endsWith('.xmpuzzle') && !SLOW.has(f),
    );
    for (const f of files.sort()) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      for (const shape of snap.shapes.filter((x) => x.variable === 0 && x.fixed > 0)) {
        const what = `${f} ${shape.id}`;
        const preview = await c.call('export.stlMesh', { shape: shape.id });
        const bin = fileBytes(await c.call('export.stl', { shape: shape.id, name: shape.name }));
        const g = loader.parse(bin.slice().buffer);
        const count = new DataView(bin.buffer, bin.byteOffset).getUint32(80, true);
        expect(g.getAttribute('position').count, what).toBe(3 * count);
        g.computeBoundingBox();
        const box = g.boundingBox;
        for (let a = 0; a < 3; a++) {
          expect(box?.min.getComponent(a), what).toBeCloseTo(preview.lo[a] ?? 0, 3);
          expect(box?.max.getComponent(a), what).toBeCloseTo(preview.hi[a] ?? 0, 3);
        }
        const balance = edgeBalance(stlPositions(bin));
        expect(balance.unpaired, what).toBe(0);
        shared += balance.shared;
        shapes++;
      }
    }
    expect(shapes).toBeGreaterThan(100);
    // one edge of Bermuda's shape 22 carries four triangles, as the original's file
    // (docs/handoff/deferred-5.md, "Found in the original")
    expect(shared).toBe(1);
  }, 120_000);
});
