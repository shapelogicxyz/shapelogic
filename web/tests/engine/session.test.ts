// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scripted session of stage 7's contract tests (tests/contract/
// test_engine.py, solving_session) through the TypeScript client: solve
// with take-apart, page sorted every way, play back with poses and a
// track, explore, take apart again, turn into shapes, remove, save. It runs
// on DemoPieceGenerator as stage 7's does (Bermuda has a single solution,
// so sorting and removing would test nothing); Bermuda, on the prism grid,
// is solved, played and saved as well.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterAll, beforeAll, describe, expect, it } from 'vitest';

import type { Errc, ProblemDetail, SolutionRow } from '../../src/engine/api.generated.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { meshArrays, poseArrays } from '../../src/engine/frames.ts';
import { ENGINE, FIXTURES, Session } from '../helpers/engine.ts';

async function fails(p: Promise<unknown>, errc: Errc): Promise<void> {
  try {
    await p;
  } catch (e) {
    expect(e).toBeInstanceOf(EngineError);
    expect((e as EngineError).errc).toBe(errc);
    return;
  }
  throw new Error(`succeeded; expected ${errc}`);
}

function sortedBy(values: number[], descending: boolean): boolean {
  return values.every(
    (v, i) => i === 0 || (descending ? (values[i - 1] ?? v) >= v : (values[i - 1] ?? v) <= v),
  );
}

describe.skipIf(ENGINE === null)('the scripted session', () => {
  let s: Session;
  let dir: string;
  let p: string;
  let detail: ProblemDetail;
  let total: number;
  let row: SolutionRow;
  let assembled: number[];

  beforeAll(async () => {
    s = await Session.start();
    dir = mkdtempSync(join(tmpdir(), 'bt-session-'));
  });
  afterAll(async () => {
    await s.end();
    rmSync(dir, { recursive: true });
    expect(s.problems).toEqual([]);
  });

  it('solves with take-apart, its run ending in decision 0012 order', async () => {
    const c = s.client;
    const snap = await c.call('document.open', { path: `${FIXTURES}/DemoPieceGenerator.xmpuzzle` });
    p = snap.problems[0]?.id ?? '';
    const from = s.events.length;
    const started = await c.call('solve.start', {
      problem: p,
      options: { disassemble: true, keepAtMost: 0, sortBy: 'moves' },
    });
    const end = await s.waitFor(
      'document.changed',
      (e) => e.revision > started.revision && e.problems.includes(p),
      from,
    );
    const run = s.events.slice(from, end.index + 1);
    const states = run.filter((e) => e.event === 'solve.state').map((e) => e.params.state);
    expect(states.at(-1)).toBe('finished');
    // the run's last solve.state, then its progress and solutionsAdded, then its document.changed
    const lastState = run.findLastIndex((e) => e.event === 'solve.state');
    const tail = run.slice(lastState).map((e) => e.event);
    expect(tail[0]).toBe('solve.state');
    expect(tail.at(-1)).toBe('document.changed');
    expect(tail).toContain('solve.progress');
    expect(tail.indexOf('solve.progress')).toBeLessThan(tail.lastIndexOf('solve.solutionsAdded'));
    expect(tail.filter((e) => e === 'document.changed')).toHaveLength(1);
    await s.drain();

    const status = await c.call('solve.status', { problem: p });
    expect([status.state, status.failure]).toEqual(['finished', null]);
    detail = await c.call('problem.get', { problem: p });
    expect(detail.state).toBe('solved');
    expect(detail.saved).toBe(detail.solutions);
    expect(detail.saved).toBeGreaterThan(1);
    total = detail.saved;
    const added = s.events
      .slice(from)
      .filter((e) => e.event === 'solve.solutionsAdded' && e.params.problem === p)
      .map((e) => e.params as { saved: number; solutionsVersion: number });
    expect(
      sortedBy(
        added.map((a) => a.saved),
        false,
      ),
    ).toBe(true);
    expect(added.at(-1)?.saved).toBe(total);
    expect((await c.call('document.get')).revision).toBe(end.revision);
    // a finished solve is not aborted: the solutions stay
    await fails(c.call('solve.abort', { problem: p }), 'invalid_state');
    expect((await c.call('problem.get', { problem: p })).saved).toBe(total);

    // paging: a view, sorted every way
    const page = await c.call('solution.page', {
      problem: p,
      sortBy: 'moves',
      descending: true,
      limit: total,
    });
    expect([page.total, page.rows.length]).toEqual([total, total]);
    expect(page.solutionsVersion).toBe(added.at(-1)?.solutionsVersion);
    expect(
      sortedBy(
        page.rows.map((r) => r.moves ?? 0),
        true,
      ),
    ).toBe(true);
    for (const [sortBy, field] of [
      ['level', null],
      ['assembly', 'assembly'],
      ['pieces', null],
      ['list', 'index'],
    ] as const) {
      const rows = (await c.call('solution.page', { problem: p, sortBy, limit: total })).rows;
      expect(rows).toHaveLength(total);
      if (field !== null) {
        expect(
          sortedBy(
            rows.map((r) => r[field]),
            false,
          ),
        ).toBe(true);
      }
    }
    const first = await c.call('solution.page', { problem: p, limit: 5 });
    const second = await c.call('solution.page', { problem: p, offset: 5, limit: 5 });
    expect([...first.rows, ...second.rows].map((r) => r.index)).toEqual([
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    ]);
    expect((await c.call('solution.page', { problem: p, offset: total })).rows).toEqual([]);
    const top = page.rows[0];
    if (top === undefined) {
      throw new Error('no solution');
    }
    row = top;
    expect(row.disassembly).toBe('tree');
    expect(row.movesText).toBeTruthy();
    const got = await c.call('solution.get', { problem: p, solution: row.id });
    expect(got.row).toEqual(row);
    expect(got.placements).toHaveLength(detail.pieces);
    await fails(c.call('solution.get', { problem: p, solution: 'sol99999' }), 'not_found');

    // a placed piece is drawn as its shape's mesh in the placement's transformation
    const placed = got.placements[0];
    const shape = detail.parts[0]?.shape ?? '';
    const plain = await c.call('shape.mesh', { shape });
    const turned = await c.call('shape.mesh', {
      shape,
      transformation: placed?.transformation ?? 0,
    });
    expect([turned.triangles, turned.vertices]).toEqual([plain.triangles, plain.vertices]);
    const flat = await c.call('shape.mesh', { shape, transformation: 1, style: 'flat' });
    expect(meshArrays(flat).edgeLines.length).toBeGreaterThan(0);
    await fails(c.call('shape.mesh', { shape, transformation: 48 }), 'invalid_argument');
  });

  it('plays a solution back: meshes, poses, a track', async () => {
    const c = s.client;
    const pb = await c.call('playback.open', { problem: p, solution: row.id });
    const n = pb.pieces.length;
    expect(pb.steps).toBeGreaterThan(0);
    expect(n).toBe(detail.pieces);
    const mesh = await c.call('playback.mesh', { handle: pb.handle, piece: 0 });
    expect(meshArrays(mesh).indices.length).toBeGreaterThan(0);
    const poses = await c.call('playback.poses', { handle: pb.handle, samplesPerStep: 4 });
    const samples = pb.steps * 4 + 1;
    expect([poses.samples, poses.pieces]).toEqual([samples, n]);
    const a = poseArrays(poses, 'step');
    expect(a.at[0]).toBe(0);
    expect(Math.abs((a.at[samples - 1] ?? 0) - pb.steps)).toBeLessThan(1e-6);
    expect(Array.from(a.visible.subarray(0, n)).every((v) => v === 1)).toBe(true);
    assembled = Array.from(a.translation.subarray(0, n * 3));
    const track = await c.call('playback.track', {
      handle: pb.handle,
      samplesPerStep: 2,
      reassemble: false,
    });
    const times = Array.from(poseArrays(track, 'time').at);
    expect(times).toHaveLength(track.samples);
    expect(sortedBy(times, false)).toBe(true);
    const part = await c.call('playback.poses', {
      handle: pb.handle,
      from: 1,
      to: 2,
      samplesPerStep: 1,
    });
    expect(part.samples).toBe(2);
    await fails(
      c.call('playback.poses', { handle: pb.handle, from: 2, to: 1 }),
      'invalid_argument',
    );
    await fails(c.call('playback.mesh', { handle: pb.handle, piece: n }), 'invalid_argument');
    await c.call('playback.mesh', { handle: pb.handle, piece: 0, style: 'wireframe' });
    await c.call('playback.close', { handle: pb.handle });
    await fails(c.call('playback.close', { handle: pb.handle }), 'not_found');
  });

  it('explores the assembly and the movements', async () => {
    const c = s.client;
    const ax = await c.call('explore.openAssembly', { problem: p });
    expect(ax.pieces).toBe(detail.pieces);
    if (ax.placementsSupported) {
      expect(
        (await c.call('explore.placements', { handle: ax.handle, piece: 0 })).placements.length,
      ).toBeGreaterThan(0);
    }
    const stepped = await c.call('explore.step', { handle: ax.handle, steps: 100 });
    expect(stepped.placements).toHaveLength(detail.pieces);
    expect(stepped.finished).toBeGreaterThanOrEqual(0);
    expect(stepped.finished).toBeLessThanOrEqual(1);
    await c.call('explore.step', { handle: ax.handle });
    await c.call('explore.close', { handle: ax.handle });

    const mx = await c.call('explore.openMovements', { problem: p, solution: row.id });
    const root = mx.root;
    expect(root.node).toBe(root.parent);
    expect(root.pieces).toHaveLength(detail.pieces);
    expect(mx.directions.length).toBeGreaterThan(0);
    const children = (await c.call('explore.analyse', { handle: mx.handle, node: root.node }))
      .nodes;
    expect(children.length).toBeGreaterThan(0);
    const child = children[0];
    if (child === undefined) {
      throw new Error('no child');
    }
    expect(
      (await c.call('explore.node', { handle: mx.handle, node: root.node })).children.length,
    ).toBeGreaterThan(0);
    expect(await c.call('explore.node', { handle: mx.handle, node: child.node })).toEqual(child);
    const moved = await c.call('explore.addMove', {
      handle: mx.handle,
      node: root.node,
      pieces: [root.pieces[0]?.piece ?? 0],
      direction: 0,
    });
    expect(moved.parent).toBe(root.node);
    // a piece dragged one step: some piece can move some way
    const found = [];
    for (const q of root.pieces) {
      for (const delta of [
        [1, 0, 0],
        [-1, 0, 0],
        [0, 1, 0],
        [0, -1, 0],
        [0, 0, 1],
        [0, 0, -1],
      ] as [number, number, number][]) {
        try {
          found.push(
            await c.call('explore.findMatching', {
              handle: mx.handle,
              node: root.node,
              piece: q.piece,
              delta,
            }),
          );
        } catch (e) {
          expect((e as EngineError).errc).toBe('not_found');
        }
      }
    }
    expect(found.length).toBeGreaterThan(0);
    expect(found.every((f) => f.parent === root.node)).toBe(true);
    await fails(
      c.call('explore.findMatching', {
        handle: mx.handle,
        node: root.node,
        piece: 999,
        delta: [1, 0, 0],
      }),
      'invalid_argument',
    );
    await c.call('explore.prune', { handle: mx.handle, node: child.node });
    await fails(c.call('explore.node', { handle: mx.handle, node: moved.node }), 'not_found');
    await c.call('explore.close', { handle: mx.handle });
    await fails(c.call('explore.close', { handle: mx.handle }), 'not_found');
    await fails(c.call('explore.close', { handle: 'pb1' }), 'invalid_params');
  });

  it('takes apart again afterwards', async () => {
    const c = s.client;
    await c.call('solution.deleteDisassembly', { problem: p });
    let rows = (await c.call('solution.page', { problem: p, limit: total })).rows;
    expect(rows.every((r) => r.disassembly === 'summary')).toBe(true);
    // a solution without its moves still plays: the assembled pose alone
    const pb = await c.call('playback.open', { problem: p, solution: row.id });
    expect([pb.steps, pb.movesText, pb.pieces.length]).toEqual([0, null, detail.pieces]);
    const poses = await c.call('playback.poses', { handle: pb.handle });
    const a = poseArrays(poses, 'step');
    expect(poses.samples).toBe(1);
    expect(Array.from(a.visible).every((v) => v === 1)).toBe(true);
    Array.from(a.translation).forEach((v, i) => {
      expect(Math.abs(v - (assembled[i] ?? NaN))).toBeLessThan(1e-5);
    });
    expect(
      (await c.call('playback.mesh', { handle: pb.handle, piece: 0, style: 'flat' })).triangles,
    ).toBeGreaterThan(0);
    await c.call('playback.close', { handle: pb.handle });

    let from = s.events.length;
    expect((await c.call('solution.analyse', { problem: p, which: 'missing' })).total).toBe(total);
    const ended = await s.waitFor('solution.analyseEnded', (e) => e.problem === p, from);
    expect([ended.done, ended.separated, ended.stopped, ended.failure]).toEqual([
      total,
      total,
      false,
      null,
    ]);
    const changed = await s.waitFor('document.changed', (e) => e.problems.includes(p), from);
    expect(changed.index).toBeGreaterThan(ended.index);
    rows = (await c.call('solution.page', { problem: p, limit: total })).rows;
    expect(rows.every((r) => r.disassembly === 'tree')).toBe(true);
    expect((await c.call('solution.analyseStatus', { problem: p })).active).toBe(false);
    await fails(c.call('solution.analyseStop', { problem: p }), 'invalid_state');
    from = s.events.length;
    await c.call('solution.analyse', {
      problem: p,
      which: 'one',
      solution: row.id,
      rotations: true,
    });
    await s.waitFor('solution.analyseEnded', (e) => e.problem === p, from);
    await fails(c.call('solution.analyse', { problem: p, which: 'one' }), 'invalid_argument');
    // an analysis stopped at once keeps what it finished
    from = s.events.length;
    await c.call('solution.analyse', { problem: p, which: 'all', threads: 1 });
    await c.call('solution.analyseStatus', { problem: p });
    try {
      await c.call('solution.analyseStop', { problem: p });
    } catch (e) {
      // it may have ended already
      expect((e as EngineError).errc).toBe('invalid_state');
    }
    const stopped = await s.waitFor('solution.analyseEnded', (e) => e.problem === p, from);
    expect(stopped.stopped).toBe(stopped.done < stopped.total);
    await s.drain();
  });

  it('turns an assembly into shapes, removes solutions, saves', async () => {
    const c = s.client;
    const before = (await c.call('shape.list')).shapes.length;
    const made = await c.call('solution.toShapes', {
      problem: p,
      solution: row.id,
      addTo: 'newProblem',
    });
    expect(made.shapes).toHaveLength(1);
    expect((await c.call('shape.list')).shapes).toHaveLength(before + 1);
    await c.call('edit.undo');
    expect((await c.call('shape.list')).shapes).toHaveLength(before);

    const page = await c.call('solution.page', { problem: p, limit: total });
    const ids = page.rows.map((r) => r.id);
    await c.call('solution.delete', { problem: p, which: 'one', solution: ids[1] ?? '' });
    expect((await c.call('solution.page', { problem: p })).solutionsVersion).toBeGreaterThan(
      page.solutionsVersion,
    );
    await c.call('solution.delete', { problem: p, which: 'before', solution: ids[2] ?? '' });
    await c.call('solution.delete', { problem: p, which: 'after', solution: ids[4] ?? '' });
    expect((await c.call('solution.page', { problem: p })).rows.map((r) => r.id)).toEqual([
      ids[2],
      ids[3],
      ids[4],
    ]);
    await c.call('solution.delete', { problem: p, which: 'withoutDisassembly' });
    await fails(c.call('solution.delete', { problem: p, which: 'one' }), 'invalid_argument');
    await c.call('solution.delete', { problem: p, which: 'all' });
    expect((await c.call('solution.page', { problem: p })).total).toBe(0);
    await c.call('edit.undo');
    expect((await c.call('solution.page', { problem: p })).total).toBe(3);

    const path = join(dir, 'solved.xmpuzzle');
    await c.call('document.save', { path });
    const again = await c.call('document.open', { path });
    expect(again.problems[0]?.saved).toBe(3);
  });

  it('solves, plays and saves Bermuda (the prism grid)', async () => {
    const c = s.client;
    const snap = await c.call('document.open', { path: `${FIXTURES}/Bermuda.xmpuzzle` });
    expect(snap.grid).toBe('prisms');
    const q = snap.problems[0]?.id ?? '';
    const from = s.events.length;
    const started = await c.call('solve.start', {
      problem: q,
      options: { disassemble: true, keepAtMost: 0 },
    });
    await s.waitFor(
      'document.changed',
      (e) => e.revision > started.revision && e.problems.includes(q),
      from,
    );
    const page = await c.call('solution.page', { problem: q });
    expect(page.total).toBe(1);
    const sol = page.rows[0];
    expect(sol?.disassembly).toBe('tree');
    const pb = await c.call('playback.open', { problem: q, solution: sol?.id ?? '' });
    expect(pb.steps).toBeGreaterThan(0);
    for (let i = 0; i < pb.pieces.length; i++) {
      expect(
        meshArrays(await c.call('playback.mesh', { handle: pb.handle, piece: i })).indices.length,
      ).toBeGreaterThan(0);
    }
    const poses = await c.call('playback.poses', { handle: pb.handle });
    expect(poseArrays(poses, 'step').translation).toHaveLength(
      poses.samples * pb.pieces.length * 3,
    );
    const track = await c.call('playback.track', { handle: pb.handle, reassemble: true });
    expect(poseArrays(track, 'time').at).toHaveLength(track.samples);
    await c.call('playback.close', { handle: pb.handle });
    const path = join(dir, 'Bermuda.xmpuzzle');
    const saved = await c.call('document.save', { path });
    expect(saved.path).toBe(path);
    expect((await c.call('document.get')).dirty).toBe(false);
    expect((await c.call('document.open', { path })).problems[0]?.saved).toBe(1);
  });
});
