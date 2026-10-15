// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Solver workspace computes or relies on, against the real
// engine: the assembled view's placement (the client's cellScale and
// gridExtent tables, with the hotspot of shape.mesh {transformation}) puts
// every piece of every fixture's first solutions where the player's first
// pose does, on all five grids; the solutions pager, in every sort and
// direction over pages, gives the rows one solution.page gives in one
// piece; the poses of a solution read in chunks are the engine's samples at
// every whole step. A stacking solution's discs (deferred part 4) stand,
// assembled and at the end, where the boards the Puzzle draws put them
// (stacking.board's layout, by their centres), in the frame the client
// works the board's middle out in.

import { mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { poseArrays } from '../../src/engine/frames.ts';
import type { SortKey } from '../../src/renderer/solver/pager.ts';
import { SolutionPager } from '../../src/renderer/solver/pager.ts';
import { PoseTrack, SAMPLES } from '../../src/renderer/solver/poses.ts';
import { boardCentre, discAt } from '../../src/renderer/view3d/board.ts';
import { cellScale, gridExtent, placedAt } from '../../src/renderer/view3d/extent.ts';
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
            const at = placedAt(pl?.at ?? [0, 0, 0], mesh.hotspot, scale, extent);
            for (let k = 0; k < 3; k++) {
              const want = at[k] ?? 0;
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

  it('places a stacking solution’s discs where the boards and the player’s poses have them', async () => {
    const c = s.client;
    // the saved searches in a folder of the test's own, never the user's cache
    const work = mkdtempSync(join(tmpdir(), 'shapelogic_engine-panex-'));
    await c.call('settings.set', { panexFolder: work });
    let discs = 0;
    try {
      for (const f of [
        'test_stacking_hanoi.xmpuzzle',
        'stacking/stack_panex_swap_3.xmpuzzle',
        'stacking/stack_panex_jr.xmpuzzle',
        'stacking/stack_two_boards.xmpuzzle',
        'stacking/stack_copies.xmpuzzle',
      ]) {
        const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
        const size = new Map(snap.shapes.map((x) => [x.id, x.disc ?? 1]));
        for (const prob of snap.problems) {
          const start = await c.call('stacking.board', { problem: prob.id, goal: false });
          const goal = await c.call('stacking.board', { problem: prob.id, goal: true });
          if (start.layout === null || start.centre === null || start.error !== null) {
            continue;
          }
          // the client's middle of the board is the engine's
          expect(boardCentre(start.layout), f).toEqual(start.centre);
          const centre = start.centre;
          await c.call('solve.start', { problem: prob.id, options: { disassemble: true } });
          for (let i = 0; i < 400; i++) {
            const st = await c.call('solve.status', { problem: prob.id });
            if (st.state === 'finished' || st.state === 'error') {
              break;
            }
            await new Promise((r) => setTimeout(r, 25));
          }
          const page = await c.call('solution.page', { problem: prob.id, limit: 1 });
          const row = page.rows[0];
          if (row === undefined) {
            continue;
          }
          const got = await c.call('solution.get', { problem: prob.id, solution: row.id });
          const pb = await c.call('playback.open', { problem: prob.id, solution: row.id });
          const poses = poseArrays(
            await c.call('playback.poses', { handle: pb.handle, samplesPerStep: 1 }),
            'step',
          );
          const n = pb.pieces.length;
          const last = pb.steps;
          for (const [i, piece] of pb.pieces.entries()) {
            const pl = got.placements.find((q) => q.piece === piece.piece);
            const r = size.get(piece.shape) ?? 1;
            // assembled: the player's placedAt with the disc's centre, centred on the board
            const at = placedAt(
              pl?.at ?? [0, 0, 0],
              [r, r, 0],
              [1, 1, 1],
              [2 * centre[0], 2 * centre[1], 2 * centre[2]],
            );
            // the start's and the goal's board put the disc where the first and the last pose do
            const copy = piece.piece - pb.pieces.findIndex((q) => q.shape === piece.shape);
            const onStart = start.layout.discs.find(
              (d) => d.shape === piece.shape && d.instance === copy,
            );
            const onGoal = goal.layout?.discs.find(
              (d) => d.shape === piece.shape && d.instance === copy,
            );
            const first = discAt(onStart?.at ?? [0, 0, 0], r, centre);
            const end = discAt(onGoal?.at ?? [0, 0, 0], r, centre);
            for (let k = 0; k < 3; k++) {
              const pose0 = poses.translation[3 * i + k] ?? NaN;
              const poseN = poses.translation[3 * (last * n + i) + k] ?? NaN;
              expect(pose0, `${f} piece ${String(i)} assembled`).toBeCloseTo(at[k] ?? 0, 4);
              expect(pose0, `${f} piece ${String(i)} on the start`).toBeCloseTo(first[k] ?? 0, 4);
              expect(poseN, `${f} piece ${String(i)} on the goal`).toBeCloseTo(end[k] ?? 0, 4);
            }
            discs++;
          }
          await c.call('playback.close', { handle: pb.handle });
        }
      }
    } finally {
      rmSync(work, { recursive: true, force: true });
    }
    expect(discs).toBeGreaterThan(15);
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
