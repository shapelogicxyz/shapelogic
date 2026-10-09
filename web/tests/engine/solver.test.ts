// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Solver workspace computes or relies on, against the real
// engine: the assembled view's placement (the client's cellScale and
// gridExtent tables, with the hotspot of shape.mesh {transformation}) puts
// every piece of every fixture's first solutions where the player's first
// pose does, on all five grids; the solutions pager, in every sort and
// direction over pages, gives the rows one solution.page gives in one
// piece; the poses of a solution read in chunks are the engine's samples at
// every whole step.

import { readdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { poseArrays } from '../../src/engine/frames.ts';
import type { SortKey } from '../../src/renderer/solver/pager.ts';
import { SolutionPager } from '../../src/renderer/solver/pager.ts';
import { PoseTrack, SAMPLES } from '../../src/renderer/solver/poses.ts';
import { cellScale, gridExtent } from '../../src/renderer/view3d/extent.ts';
import { ENGINE, FIXTURES, ROOT, Session } from '../helpers/engine.ts';

describe.skipIf(ENGINE === null)('the Solver workspace and the engine', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('places an assembled piece at its cell minus its hotspot where the player’s first pose has it', async () => {
    const c = s.client;
    const grids = new Set<string>();
    let pieces = 0;
    const files = readdirSync(resolve(ROOT, FIXTURES)).filter((f) => f.endsWith('.xmpuzzle'));
    for (const f of files.sort()) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      if (snap.grid === 'sliding' || snap.grid === 'stacking') {
        continue;
      }
      for (const prob of snap.problems) {
        const d = await c.call('problem.get', { problem: prob.id });
        const result = snap.shapes.find((x) => x.id === d.result);
        if (result === undefined) {
          continue;
        }
        const extent = gridExtent(snap.grid, result.size);
        const scale = cellScale(snap.grid);
        const page = await c.call('solution.page', { problem: prob.id, limit: 3 });
        for (const row of page.rows) {
          const got = await c.call('solution.get', { problem: prob.id, solution: row.id });
          const pb = await c.call('playback.open', { problem: prob.id, solution: row.id });
          const poses = poseArrays(
            await c.call('playback.poses', {
              handle: pb.handle,
              from: 0,
              to: 0,
              samplesPerStep: 1,
            }),
            'step',
          );
          for (const [i, piece] of pb.pieces.entries()) {
            const pl = got.placements.find((q) => q.piece === piece.piece);
            expect(pl?.placed).toBe(true);
            expect(pl?.transformation).toBe(piece.orientation);
            const mesh = await c.call('shape.mesh', {
              shape: piece.shape,
              transformation: piece.orientation,
            });
            for (let k = 0; k < 3; k++) {
              const want =
                ((pl?.at[k] ?? 0) - (mesh.hotspot[k] ?? 0)) * (scale[k] ?? 1) -
                (extent[k] ?? 0) / 2;
              expect(
                poses.translation[3 * i + k],
                `${f} ${row.id} piece ${String(i)} axis ${String(k)}`,
              ).toBeCloseTo(want, 4);
            }
            pieces++;
          }
          await c.call('playback.close', { handle: pb.handle });
          grids.add(snap.grid);
        }
      }
    }
    expect([...grids].sort()).toEqual(['bricks', 'prisms', 'rhombic', 'spheres', 'tetraOcta']);
    expect(pieces).toBeGreaterThan(100);
  });

  it('pages every sort and direction as one solution.page gives it', async () => {
    const c = s.client;
    const snap = await c.call('document.open', {
      path: `${FIXTURES}/SolidSixPieceBurrs.xmpuzzle`,
    });
    const p = snap.problems[0]?.id ?? 'p0';
    const sorts: SortKey[] = ['list', 'assembly', 'level', 'moves', 'pieces'];
    for (const sortBy of sorts) {
      for (const descending of [false, true]) {
        const whole = await c.call('solution.page', {
          problem: p,
          sortBy,
          descending,
          limit: 10000,
        });
        expect(whole.total).toBeGreaterThan(250);
        const pager = new SolutionPager(c, p, sortBy, descending);
        const done = new Promise<void>((r) => {
          pager.onChange(() => {
            if (pager.current(0, whole.total)) {
              r();
            }
          });
        });
        pager.want(0, whole.total);
        await done;
        const ids = Array.from({ length: whole.total }, (_, i) => pager.row(i)?.id);
        expect(ids, `${sortBy} ${descending ? 'down' : 'up'}`).toEqual(whole.rows.map((r) => r.id));
        expect(pager.requests).toBe(Math.ceil(whole.total / 100));
        pager.close();
      }
    }
  });

  it('reads a solution’s poses in chunks that are the engine’s samples at every whole step', async () => {
    const c = s.client;
    const snap = await c.call('document.open', { path: `${FIXTURES}/PelikanBurr.xmpuzzle` });
    const p = snap.problems[0]?.id ?? 'p0';
    const row = (await c.call('solution.page', { problem: p })).rows[0];
    const pb = await c.call('playback.open', { problem: p, solution: row?.id ?? 'sol0' });
    expect(pb.steps).toBeGreaterThan(64);
    const track = new PoseTrack(pb.pieces.length, pb.steps);
    for (let k = 0; k < track.chunkCount; k++) {
      const { from, to } = track.range(k);
      track.add(
        k,
        poseArrays(
          await c.call('playback.poses', { handle: pb.handle, from, to, samplesPerStep: SAMPLES }),
          'step',
        ),
      );
    }
    const whole = poseArrays(
      await c.call('playback.poses', { handle: pb.handle, samplesPerStep: 1 }),
      'step',
    );
    const n = pb.pieces.length;
    for (let step = 0; step <= pb.steps; step++) {
      const poses = track.at(step, false);
      expect(poses).not.toBeNull();
      for (let i = 0; i < n; i++) {
        const b = step * n + i;
        expect(poses?.[i]?.t).toEqual([...whole.translation.subarray(3 * b, 3 * b + 3)]);
        expect(poses?.[i]?.q).toEqual([...whole.rotation.subarray(4 * b, 4 * b + 4)]);
        expect(poses?.[i]?.visible).toBe(whole.visible[b] === 1);
      }
    }
    await c.call('playback.close', { handle: pb.handle });
  });
});
