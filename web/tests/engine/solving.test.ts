// SPDX-License-Identifier: GPL-3.0-or-later
//
// Solves under way, through the client: pause, save point, continue and
// abort on a box of dominoes; a new document during a run; the order of
// decision 0012 (a reply before its events, a run's last solve.state before
// the document.changed of its end, an abort's two document.changed).

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import type { Errc, SolveState } from '../../src/engine/api.generated.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { ENGINE, Session } from '../helpers/engine.ts';

async function fails(p: Promise<unknown>, errc: Errc): Promise<void> {
  try {
    await p;
  } catch (e) {
    expect((e as EngineError).errc).toBe(errc);
    return;
  }
  throw new Error(`succeeded; expected ${errc}`);
}

/** A box of x*y*z cells to fill with dominoes: many assemblies, all of which come apart. */
async function dominoBox(
  s: Session,
  x: number,
  y: number,
  z: number,
): Promise<{ p: string; box: string; d: string }> {
  const c = s.client;
  await c.call('document.new', { grid: 'bricks' });
  const box = (await c.call('shape.create', { size: [x, y, z] })).shape;
  await c.call('shape.setBox', {
    shape: box,
    tool: 'fixed',
    from: [0, 0, 0],
    to: [x - 1, y - 1, z - 1],
  });
  const d = (await c.call('shape.create', { size: [2, 1, 1] })).shape;
  await c.call('shape.setBox', { shape: d, tool: 'fixed', from: [0, 0, 0], to: [1, 0, 0] });
  const p = (await c.call('problem.create')).problem;
  await c.call('problem.setResult', { problem: p, shape: box });
  const n = (x * y * z) / 2;
  await c.call('problem.setPieceCount', { problem: p, shape: d, min: n, max: n });
  return { p, box, d };
}

async function state(
  s: Session,
  p: string,
  states: SolveState[],
  from: number,
  timeoutMs = 60_000,
): Promise<SolveState> {
  return (
    await s.waitFor(
      'solve.state',
      (e) => e.problem === p && states.includes(e.state),
      from,
      timeoutMs,
    )
  ).state;
}

describe.skipIf(ENGINE === null)('solving', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('pauses, saves at a save point, carries on and aborts a box of dominoes', async () => {
    const c = s.client;
    // 2x4x5 on one thread runs for seconds in the release build: the pauses land
    const { p, d } = await dominoBox(s, 2, 4, 5);
    const one = { disassemble: true, keepAtMost: 5, threads: 1 };
    const other = (await c.call('problem.create')).problem;
    const c1 = (await c.call('colour.add', { rgb: '#ff0000' })).colour;
    await fails(c.call('solve.abort', { problem: p }), 'invalid_state');
    let from = s.events.length;
    await c.call('solve.start', { problem: p, options: one });
    expect(await state(s, p, ['assembling', 'finished'], from)).toBe('assembling');
    // frozen while a run is active (decision 0011): "pause first"
    await fails(
      c.call('shape.setCells', { shape: d, tool: 'erase', cells: [[0, 0, 0]] }),
      'invalid_state',
    );
    await fails(c.call('shape.create'), 'invalid_state');
    await fails(c.call('problem.rename', { problem: p, name: 'x' }), 'invalid_state');
    await fails(c.call('edit.undo'), 'invalid_state');
    await fails(c.call('document.saveBytes'), 'invalid_state');
    await fails(c.call('solve.start', { problem: p }), 'invalid_state');
    // and what goes on
    await c.call('document.setNotes', { text: 'while solving' });
    await c.call('colour.edit', { colour: c1, rgb: '#00ff00' });
    await c.call('problem.rename', { problem: other, name: 'not solving' });
    await c.call('shape.mesh', { shape: d });
    expect((await c.call('document.get')).solving).toBe(true);

    await c.call('solve.pause', { problem: p });
    expect(await state(s, p, ['paused', 'finished'], from)).toBe('paused');
    const detail = await c.call('problem.get', { problem: p });
    expect([detail.state, detail.savedPosition, detail.solve]).toEqual(['solving', true, 'paused']);
    expect((await c.call('solve.status', { problem: p })).progress.stage).toBe('paused');
    const dir = mkdtempSync(join(tmpdir(), 'bt-solving-'));
    try {
      await c.call('document.save', { path: join(dir, 'paused.xmpuzzle') });
    } finally {
      rmSync(dir, { recursive: true });
    }

    from = s.events.length;
    await c.call('solve.continue', { problem: p, options: one });
    expect(await state(s, p, ['assembling', 'finished'], from)).toBe('assembling');
    await c.call('solve.savePoint', { problem: p });
    expect(await state(s, p, ['paused', 'finished'], from)).toBe('paused');
    from = s.events.length;
    await c.call('solve.continue', { problem: p, options: one });
    expect(await state(s, p, ['assembling', 'draining', 'finished'], from)).not.toBe('finished');

    // the abort's reply, then its own document.changed, then the run's end from the poll
    const aborted = await c.call('solve.abort', { problem: p });
    const replied = s.next();
    await state(s, p, ['idle'], from, 30_000);
    await s.drain();
    const after = await c.call('problem.get', { problem: p });
    expect([after.state, after.saved, after.solve]).toEqual(['unsolved', 0, 'idle']);
    const changes = s.events
      .slice(from)
      .filter(
        (e) => e.event === 'document.changed' && (e.params.revision as number) >= aborted.revision,
      );
    expect(changes.map((e) => e.params.revision)).toEqual([aborted.revision, aborted.revision + 1]);
    expect(changes[0]?.seq).toBeGreaterThan(replied);
    const idle = s.events.findLastIndex(
      (e) => e.event === 'solve.state' && e.params.state === 'idle',
    );
    const runEnd = changes[1];
    expect(runEnd === undefined ? -1 : s.events.indexOf(runEnd)).toBeGreaterThan(idle);
    await fails(c.call('solve.pause', { problem: p }), 'invalid_state');
    await fails(c.call('solve.abort', { problem: p }), 'invalid_state');
  }, 120_000);

  it("ends a run with its last solve.state, then the document.changed of the run's revision", async () => {
    const c = s.client;
    const { p } = await dominoBox(s, 2, 4, 4);
    const from = s.events.length;
    const started = await c.call('solve.start', {
      problem: p,
      options: { disassemble: true, keepAtMost: 0 },
    });
    const startedAt = s.next();
    const end = await s.waitFor(
      'document.changed',
      (e) => e.revision > started.revision && e.problems.includes(p),
      from,
    );
    const run = s.events.slice(from, end.index + 1);
    // the start's own document.changed comes after its reply
    const own = run.find(
      (e) => e.event === 'document.changed' && e.params.revision === started.revision,
    );
    expect(own?.seq).toBeGreaterThan(startedAt);
    const last = run.findLastIndex((e) => e.event === 'solve.state');
    expect(run[last]?.params.state).toBe('finished');
    expect(run.slice(last + 1).map((e) => e.event)).toEqual(
      expect.arrayContaining(['solve.progress', 'solve.solutionsAdded']),
    );
    expect(
      run
        .slice(last + 1, -1)
        .every((e) => e.event === 'solve.progress' || e.event === 'solve.solutionsAdded'),
    ).toBe(true);
    expect(end.revision).toBe((await c.call('document.get')).revision);
    expect((await c.call('problem.get', { problem: p })).assemblies).toBe(2856);
  });

  it('gives the problems of a document replaced during their work a last idle and analyseEnded', async () => {
    const c = s.client;
    const { p } = await dominoBox(s, 2, 4, 4);
    let from = s.events.length;
    const started = await c.call('solve.start', {
      problem: p,
      options: { disassemble: true, keepAtMost: 0, threads: 2 },
    });
    await s.waitFor(
      'document.changed',
      (e) => e.revision > started.revision && e.problems.includes(p),
      from,
    );
    await s.drain();
    // an analysis of this problem and a solve of a copy, under way
    const q = (await c.call('problem.duplicate', { problem: p })).problem;
    from = s.events.length;
    expect((await c.call('solution.analyse', { problem: p, which: 'all', threads: 1 })).total).toBe(
      2856,
    );
    await c.call('solve.start', { problem: q, options: { disassemble: true, threads: 1 } });
    await state(s, q, ['assembling', 'finished'], from);
    from = s.events.length;
    const snap = await c.call('document.new', { grid: 'bricks' });
    const replied = s.next();
    expect(snap.problems).toEqual([]);
    expect(snap.solving).toBe(false);
    const ended = await s.waitFor('solution.analyseEnded', (e) => e.problem === p, from, 10_000);
    expect(ended.stopped).toBe(true);
    expect(ended.total).toBe(2856);
    const idle = await s.waitFor(
      'solve.state',
      (e) => e.problem === q && e.state === 'idle',
      from,
      10_000,
    );
    expect(idle.seq).toBeGreaterThan(replied);
    await s.drain();
    expect(s.events.slice(from).map((e) => e.event)).not.toContain('document.changed');
    await fails(c.call('solve.status', { problem: q }), 'not_found');
    await fails(c.call('solution.analyseStatus', { problem: p }), 'not_found');
  }, 60_000);
});
