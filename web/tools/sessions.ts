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
};
