// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sessions recorded in web/fake-engine/recordings/ (tools/record-sessions.ts
// records them against the real engine; the unit tests replay them through
// the fake engine). A session's calls depend only on the replies and events
// it gets, so a replay makes exactly the recorded requests. What it saw is
// kept as a transcript: results and events in order, frames by checksum.

import type { EngineClient } from '../src/engine/client.ts';
import type { EventName, EventParams } from '../src/engine/client-types.ts';
import { isEngineError } from '../src/engine/errors.ts';
import type { FrameView } from '../src/engine/frames.ts';
import { SolutionPager } from '../src/renderer/solver/pager.ts';
import { Solves } from '../src/renderer/state/solves.ts';

/** What a session saw. */
export interface Transcript {
  results: unknown[];
  events: { event: string; params: unknown }[];
}

/** FNV-1a of bytes, as hex. */
export function checksum(bytes: Uint8Array): string {
  let h = 0x811c9dc5;
  for (const b of bytes) {
    h = Math.imul(h ^ b, 0x01000193) >>> 0;
  }
  return h.toString(16).padStart(8, '0');
}

function isFrameView(v: unknown): v is FrameView {
  return typeof v === 'object' && v !== null && 'buffer' in v && 'layout' in v && 'arrays' in v;
}

/** A value with its frame views as plain descriptions and checksums. */
function plain(v: unknown): unknown {
  if (isFrameView(v)) {
    return {
      frame: v.frame,
      bytes: v.bytes,
      layout: v.layout,
      checksum: checksum(new Uint8Array(v.buffer)),
    };
  }
  if (Array.isArray(v)) {
    return v.map(plain);
  }
  if (typeof v === 'object' && v !== null) {
    return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, plain(x)]));
  }
  return v;
}

/** Drives a client through a session and keeps its transcript. */
export class Driver {
  readonly client: EngineClient;
  readonly transcript: Transcript = { results: [], events: [] };
  private waiters: (() => void)[] = [];

  constructor(client: EngineClient) {
    this.client = client;
    client.onEvent((event, params) => {
      this.transcript.events.push({ event, params });
      for (const w of this.waiters.splice(0)) {
        w();
      }
    });
  }

  /** Keeps a result (or an error's errc) in the transcript; returns it. */
  keep<T>(value: T): T {
    this.transcript.results.push(plain(value));
    return value;
  }

  /** A call that must fail: keeps its errc. */
  async fails(p: Promise<unknown>): Promise<void> {
    try {
      await p;
      this.transcript.results.push({ unexpectedSuccess: true });
    } catch (e) {
      this.transcript.results.push({ errc: isEngineError(e) ? e.errc : String(e) });
    }
  }

  /** The number of events so far (a starting point for waitFor). */
  get mark(): number {
    return this.transcript.events.length;
  }

  /** The first event from `from` on that fits; waits for it. */
  async waitFor<E extends EventName>(
    event: E,
    pred: (p: EventParams<E>) => boolean,
    from: number,
  ): Promise<EventParams<E>> {
    for (;;) {
      const events = this.transcript.events;
      for (let i = from; i < events.length; i++) {
        const e = events[i];
        if (e?.event === event && pred(e.params as EventParams<E>)) {
          return e.params as EventParams<E>;
        }
      }
      if (this.client.closed) {
        throw new Error(`the engine ended while waiting for ${event}`);
      }
      await new Promise<void>((resolve) => this.waiters.push(resolve));
    }
  }
}

export interface SessionScript {
  note: string;
  run: (d: Driver) => Promise<void>;
}

/** A box of x*y*z cells to fill with dominoes (the contract suite's), and its problem. */
async function dominoBox(
  c: EngineClient,
  x: number,
  y: number,
  z: number,
  solves?: Solves,
): Promise<string> {
  await c.call('document.new', { grid: 'bricks' });
  // the store sees a new document at its reply: the records of the old one go
  solves?.clear();
  const box = (await c.call('shape.create', { size: [x, y, z] })).shape;
  await c.call('shape.setBox', {
    shape: box,
    tool: 'fixed',
    from: [0, 0, 0],
    to: [x - 1, y - 1, z - 1],
  });
  const dom = (await c.call('shape.create', { size: [2, 1, 1] })).shape;
  await c.call('shape.setBox', { shape: dom, tool: 'fixed', from: [0, 0, 0], to: [1, 0, 0] });
  const p = (await c.call('problem.create')).problem;
  await c.call('problem.setResult', { problem: p, shape: box });
  const n = (x * y * z) / 2;
  await c.call('problem.setPieceCount', { problem: p, shape: dom, min: n, max: n });
  return p;
}

/**
 * The solve records (src/renderer/state/solves.ts) over a client, with a
 * trace of what they told, in order with the document.changed events
 * (registered first, so a change comes before what it ended).
 */
function traceSolves(c: EngineClient): { solves: Solves; trace: string[] } {
  const trace: string[] = [];
  c.on('document.changed', (p) => {
    trace.push(`changed:${String(p.revision)}:${p.problems.join(',')}`);
  });
  const solves = new Solves(c);
  solves.subscribe((x) => {
    const r = x.problem === null ? undefined : solves.peek(x.problem);
    trace.push(r === undefined ? x.kind : `${x.kind}:${r.problem}:${r.state}:${r.phase}`);
  });
  return { solves, trace };
}

/** The recorded sessions, by recording name. */
export const SESSIONS: Record<string, SessionScript> = {
  basic: {
    note:
      'Bermuda opened, meshes in every style (some pipelined), solved with take-apart, paged, ' +
      'played back (mesh, poses, track), a turned mesh, a refused call, an edit and its document.changed',
    run: async (d) => {
      const c = d.client;
      d.keep(await c.call('settings.get'));
      const snap = d.keep(
        await c.call('document.open', { path: 'tests/data/puzzles/Bermuda.xmpuzzle' }),
      );
      for (const s of snap.shapes.slice(0, 3)) {
        for (const style of ['drawing', 'wireframe', 'flat'] as const) {
          d.keep(await c.call('shape.mesh', { shape: s.id, style }));
        }
      }
      d.keep(
        await Promise.all(
          snap.shapes.slice(3, 9).map((s) => c.call('shape.mesh', { shape: s.id })),
        ),
      );
      const p = snap.problems[0]?.id ?? 'p0';
      const from = d.mark;
      const started = d.keep(
        await c.call('solve.start', { problem: p, options: { disassemble: true } }),
      );
      await d.waitFor(
        'document.changed',
        (e) => e.revision > started.revision && e.problems.includes(p),
        from,
      );
      d.keep(await c.call('problem.get', { problem: p }));
      const page = d.keep(await c.call('solution.page', { problem: p }));
      const row = page.rows[0];
      if (row === undefined) {
        throw new Error('Bermuda has no solution');
      }
      const got = d.keep(await c.call('solution.get', { problem: p, solution: row.id }));
      const pb = d.keep(await c.call('playback.open', { problem: p, solution: row.id }));
      d.keep(await c.call('playback.mesh', { handle: pb.handle, piece: 0, style: 'flat' }));
      d.keep(await c.call('playback.poses', { handle: pb.handle, samplesPerStep: 2 }));
      d.keep(await c.call('playback.track', { handle: pb.handle, reassemble: true }));
      d.keep(await c.call('playback.close', { handle: pb.handle }));
      const placed = got.placements[0];
      const shape = snap.shapes[0]?.id ?? 's0';
      d.keep(await c.call('shape.mesh', { shape, transformation: placed?.transformation ?? 0 }));
      await d.fails(c.call('shape.get', { shape: 's999' }));
      const from2 = d.mark;
      const r = d.keep(await c.call('document.setNotes', { text: 'recorded' }));
      await d.waitFor('document.changed', (e) => e.revision === r.revision, from2);
    },
  },
  dominoes: {
    note:
      'a 2x4x5 box of dominoes solved on one thread: paused, carried on, aborted (its two ' +
      'document.changed), then a new document',
    run: async (d) => {
      const c = d.client;
      await c.call('document.new', { grid: 'bricks' });
      const box = (await c.call('shape.create', { size: [2, 4, 5] })).shape;
      await c.call('shape.setBox', { shape: box, tool: 'fixed', from: [0, 0, 0], to: [1, 3, 4] });
      const dom = (await c.call('shape.create', { size: [2, 1, 1] })).shape;
      await c.call('shape.setBox', { shape: dom, tool: 'fixed', from: [0, 0, 0], to: [1, 0, 0] });
      const p = (await c.call('problem.create')).problem;
      await c.call('problem.setResult', { problem: p, shape: box });
      await c.call('problem.setPieceCount', { problem: p, shape: dom, min: 20, max: 20 });
      const options = { disassemble: true, keepAtMost: 5, threads: 1 };
      const state = (wanted: string[]) => (e: EventParams<'solve.state'>) =>
        e.problem === p && wanted.includes(e.state);
      let from = d.mark;
      d.keep(await c.call('solve.start', { problem: p, options }));
      d.keep(await d.waitFor('solve.state', state(['assembling', 'finished']), from));
      d.keep(await c.call('solve.pause', { problem: p }));
      d.keep(await d.waitFor('solve.state', state(['paused', 'finished']), from));
      d.keep(await c.call('problem.get', { problem: p }));
      from = d.mark;
      d.keep(await c.call('solve.continue', { problem: p, options }));
      d.keep(await d.waitFor('solve.state', state(['assembling', 'finished']), from));
      from = d.mark;
      const aborted = d.keep(await c.call('solve.abort', { problem: p }));
      await d.waitFor('solve.state', state(['idle']), from);
      await d.waitFor('document.changed', (e) => e.revision > aborted.revision, from);
      d.keep(await c.call('document.new', { grid: 'bricks' }));
    },
  },
  'solver-end': {
    note:
      'a small box of dominoes solved with take-apart, its end waited for by the solve ' +
      "records (the replay drops the run's active solve.state lines for a run faster than a poll)",
    run: async (d) => {
      const c = d.client;
      const { solves, trace } = traceSolves(c);
      const p = await dominoBox(c, 2, 2, 3, solves);
      d.keep(await c.call('solve.start', { problem: p, options: { disassemble: true } }));
      solves.launch(p, 'classic');
      const end = await solves.whenEnded(p);
      d.keep({ end, trace: trace.splice(0) });
      d.keep(await c.call('problem.get', { problem: p }));
    },
  },
  solver: {
    note:
      'the solve records and the solutions pager of the Solver workspace over a 2x4x5 box of ' +
      'dominoes on one thread: paused, a solution removed (the pager reads the new version), ' +
      'carried on (its first solutionsAdded tells the version paged: ignored), aborted (two ' +
      'document.changed); a paused solve aborted (one); a new document while an analysis runs ' +
      '(the last idle and analyseEnded {stopped})',
    run: async (d) => {
      const c = d.client;
      const { solves, trace } = traceSolves(c);
      const p = await dominoBox(c, 2, 4, 5, solves);
      // the pager as the Solver workspace drives it: solutionsAdded, and a
      // document.changed naming the problem's solutions
      const pager = new SolutionPager(c, p, 'list', false);
      c.on('solve.solutionsAdded', (e) => {
        if (e.problem === p) {
          const before = pager.version;
          const asked = pager.requests;
          pager.added(e.solutionsVersion);
          trace.push(
            `added:${String(e.solutionsVersion)}:${String(before)}:${String(pager.requests - asked)}`,
          );
        }
      });
      c.on('document.changed', (e) => {
        if (e.problems.includes(p) && e.what.includes('solutions')) {
          pager.refresh();
        }
      });
      const paged = (): Promise<void> =>
        new Promise((done) => {
          const off = pager.onChange(() => {
            off();
            done();
          });
        });
      pager.want(0, 5);
      const options = { disassemble: true, keepAtMost: 5, threads: 1 };
      let from = d.mark;
      d.keep(await c.call('solve.start', { problem: p, options }));
      solves.launch(p, 'classic');
      await d.waitFor('solve.state', (e) => e.problem === p && e.state === 'assembling', from);
      await d.waitFor('solve.solutionsAdded', (e) => e.problem === p && e.saved > 0, from);
      d.keep(await c.call('solve.pause', { problem: p }));
      d.keep({ end: await solves.whenEnded(p), trace: trace.splice(0) });

      // a solution removed while paused: the version moves, told by its
      // document.changed only; the pager reads it
      const next = paged();
      const row = pager.row(0);
      d.keep(
        await c.call('solution.delete', { problem: p, which: 'one', solution: row?.id ?? 'sol1' }),
      );
      await next;
      // past the progress events' quarter second, so carrying on tells the list's version at once
      await new Promise((done) => setTimeout(done, 400));
      from = d.mark;
      d.keep(await c.call('solve.continue', { problem: p, options }));
      solves.launch(p, 'classic');
      await d.waitFor('solve.solutionsAdded', (e) => e.problem === p, from);
      d.keep({ version: pager.version, trace: trace.splice(0) });

      // an abort of a run: the abort's own document.changed, then the run's end
      d.keep(await c.call('solve.abort', { problem: p }));
      d.keep({ end: await solves.whenEnded(p), trace: trace.splice(0) });
      pager.close();

      // an abort of a solve paused in memory: one document.changed, no run ends
      from = d.mark;
      d.keep(await c.call('solve.start', { problem: p, options }));
      solves.launch(p, 'classic');
      await d.waitFor('solve.state', (e) => e.problem === p && e.state === 'assembling', from);
      d.keep(await c.call('solve.pause', { problem: p }));
      d.keep({ end: await solves.whenEnded(p), trace: trace.splice(0) });
      from = d.mark;
      d.keep(await c.call('solve.abort', { problem: p }));
      await d.waitFor('solve.state', (e) => e.problem === p && e.state === 'idle', from);
      d.keep({ phase: solves.get(p).phase, trace: trace.splice(0) });

      // a new document while an analysis runs (a 2x4x4 box's assemblies taken apart afterwards)
      const q = await dominoBox(c, 2, 4, 4, solves);
      d.keep(
        await c.call('solve.start', {
          problem: q,
          options: { disassemble: false, keepAtMost: 0, threads: 1 },
        }),
      );
      solves.launch(q, 'classic');
      d.keep({ end: await solves.whenEnded(q), trace: trace.splice(0) });
      const total = (await c.call('solution.analyse', { problem: q, which: 'all', threads: 1 }))
        .total;
      solves.analysing(q, total);
      from = d.mark;
      d.keep(await c.call('document.new', { grid: 'bricks' }));
      // the store sees the new document now: the records of the old one go
      solves.clear();
      await d.waitFor('solution.analyseEnded', (e) => e.problem === q, from);
      await d.waitFor('solve.state', (e) => e.problem === q && e.state === 'idle', from);
      const r = solves.get(q);
      d.keep({
        phase: r.phase,
        analysis: r.analysis,
        running: solves.running,
        trace: trace.splice(0),
      });
    },
  },
};
