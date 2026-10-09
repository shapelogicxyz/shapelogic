// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's event-order rules (decision 0012) over the real engine's
// recorded output, replayed by the fake engine: the solve records
// (src/renderer/state/solves.ts) and the solutions pager
// (src/renderer/solver/pager.ts) as the sessions `solver-end` and `solver`
// of tools/sessions.ts drive them. A run's end is its document.changed,
// after its last solve.state; a run that starts and ends between two polls
// (the recording with the run's active states taken out) ends there too;
// an abort's own document.changed does not end the run, the second does;
// an abort of a paused solve ends no run; a new document's last idle and
// analyseEnded {stopped} leave no run and no analysis; solutionsAdded at the
// version the pager has is ignored.

import { describe, expect, it } from 'vitest';

import { FakeEngine } from '../../fake-engine/fake-engine.ts';
import { loadRecording, loadTranscript } from '../../fake-engine/recordings.ts';
import { EngineClient } from '../../src/engine/client.ts';
import { fromBase64, toBase64 } from '../../src/engine/recorder.ts';
import type { Recording, RecordingEntry } from '../../src/engine/recorder.ts';
import { Driver, SESSIONS } from '../../tools/sessions.ts';

async function replay(
  name: string,
  recording: Recording,
  timing = false,
): Promise<{ driver: Driver; fake: FakeEngine }> {
  const fake = new FakeEngine(recording, { timing, speed: 50 });
  const client = new EngineClient(fake);
  const driver = new Driver(client);
  await client.hello();
  const script = SESSIONS[name];
  if (script === undefined) {
    throw new Error(`no session ${name}`);
  }
  await script.run(driver);
  await new Promise<void>((done) => {
    client.onExit(() => {
      done();
    });
    client.close();
  });
  return { driver, fake };
}

/** The traces a session kept, with what they were kept with. */
function traces(results: unknown[]): { end?: number; trace: string[]; [k: string]: unknown }[] {
  return results.filter(
    (r): r is { trace: string[] } =>
      typeof r === 'object' && r !== null && Array.isArray((r as { trace?: unknown }).trace),
  );
}

/**
 * A recording without the active solve.state lines (preparing, assembling,
 * draining): the run as one faster than the engine's poll would show it.
 * The outputs between two requests are joined and split into lines (the
 * session sends no frames).
 */
function withoutActiveStates(r: Recording): Recording {
  const out: RecordingEntry[] = [];
  let group: { t: number; bytes: Uint8Array[] } | null = null;
  const flush = (): void => {
    if (group === null) {
      return;
    }
    const total = group.bytes.reduce((n, b) => n + b.length, 0);
    const all = new Uint8Array(total);
    let at = 0;
    for (const b of group.bytes) {
      all.set(b, at);
      at += b.length;
    }
    const lines = new TextDecoder().decode(all).split('\n');
    const kept = lines.filter((l) => {
      if (!l.includes('"solve.state"')) {
        return true;
      }
      const m = JSON.parse(l) as { params?: { state?: string } };
      return !['preparing', 'assembling', 'draining'].includes(m.params?.state ?? '');
    });
    out.push({ t: group.t, recv: toBase64(new TextEncoder().encode(kept.join('\n'))) });
    group = null;
  };
  for (const e of r.entries) {
    if ('recv' in e) {
      group ??= { t: e.t, bytes: [] };
      group.bytes.push(fromBase64(e.recv));
    } else {
      flush();
      out.push(e);
    }
  }
  flush();
  return { ...r, entries: out };
}

describe.each(['solver-end', 'solver'])('the recording %s', (name) => {
  it.each([false, true])('replays what the client saw (timing %s)', async (timing) => {
    const { driver, fake } = await replay(name, loadRecording(name), timing);
    expect(fake.mismatches).toEqual([]);
    expect(fake.through).toBe(true);
    const expected = loadTranscript(name);
    expect(driver.transcript.results).toEqual(expected.results);
    expect(driver.transcript.events).toEqual(expected.events);
  });
});

describe('the solve records and the pager follow decision 0012’s order', () => {
  it('ends a run at its document.changed, after its last solve.state', async () => {
    const { driver } = await replay('solver-end', loadRecording('solver-end'));
    const [run] = traces(driver.transcript.results);
    const t = run?.trace ?? [];
    const last = t.findIndex((x) => /^state:p\d+:finished:ending$/.test(x));
    const changed = t.findIndex((x, i) => i > last && x.startsWith('changed:'));
    expect(last).toBeGreaterThan(0);
    // the state came first, the run is over only at the next document.changed
    expect(t.slice(last, changed).every((x) => !x.startsWith('ended:'))).toBe(true);
    expect(t[changed + 1]).toMatch(/^ended:p\d+:finished:none$/);
    expect(run?.end).toBe(Number(t[changed]?.split(':')[1]));
    // what is read after it is the run's final word
    const got = driver.transcript.results.at(-1) as { state: string; solutions: number };
    expect(got.state).toBe('solved');
    expect(got.solutions).toBeGreaterThan(0);
  });

  it('ends a run that starts and ends between two polls at its document.changed', async () => {
    const original = loadRecording('solver-end');
    const recorded = traces(loadTranscript('solver-end').results)[0];
    const { driver, fake } = await replay('solver-end', withoutActiveStates(original));
    expect(fake.mismatches).toEqual([]);
    const [run] = traces(driver.transcript.results);
    const t = run?.trace ?? [];
    expect(t.some((x) => /:(preparing|assembling|draining):/.test(x))).toBe(false);
    expect(recorded?.trace.some((x) => /:(preparing|assembling):/.test(x))).toBe(true);
    // finished without an active state: still the end is the run's revision
    const last = t.findIndex((x) => /^state:p\d+:finished:ending$/.test(x));
    expect(last).toBeGreaterThan(0);
    expect(run?.end).toBe(recorded?.end);
  });

  it('ignores solutionsAdded at the version the pager has, pages at another', async () => {
    const { driver } = await replay('solver', loadRecording('solver'));
    // the pager is open in the first three traces (closed after the abort)
    const added = traces(driver.transcript.results)
      .slice(0, 3)
      .flatMap((r) => r.trace.filter((x) => /^added:\d+:/.test(x)))
      .map((x) => x.split(':').map((v) => (v === 'null' ? null : Number(v))));
    const same = added.filter(([, v, before]) => v === before);
    const other = added.filter(([, v, before]) => v !== before);
    expect(same.length).toBeGreaterThan(0);
    expect(other.length).toBeGreaterThan(0);
    expect(same.every(([, , , asked]) => asked === 0)).toBe(true);
    expect(other.every(([, , , asked]) => asked === 1)).toBe(true);
    // the carried-on run's first solutionsAdded told the version paged after the removal
    const carried = traces(driver.transcript.results)[1];
    expect(carried?.trace.at(-1)).toBe(
      `added:${String(carried?.version)}:${String(carried?.version)}:0`,
    );
  });

  it('ends an abort’s run at the second document.changed, not the abort’s own', async () => {
    const { driver } = await replay('solver', loadRecording('solver'));
    const abort = traces(driver.transcript.results)[2];
    const t = abort?.trace ?? [];
    const own = t.findIndex((x) => x.startsWith('changed:'));
    const idle = t.findIndex((x) => /^state:p\d+:idle:ending$/.test(x));
    const end = t.findIndex((x, i) => i > idle && x.startsWith('changed:'));
    expect(own).toBe(0);
    expect(idle).toBeGreaterThan(own);
    expect(t.slice(0, end).some((x) => x.startsWith('ended:'))).toBe(false);
    expect(t[end + 1]).toMatch(/^ended:p\d+:idle:none$/);
    expect(abort?.end).toBe(Number(t[end]?.split(':')[1]));
    expect(Number(t[end]?.split(':')[1])).toBeGreaterThan(Number(t[own]?.split(':')[1]));
  });

  it('ends no run when a solve paused in memory is aborted (one document.changed)', async () => {
    const { driver } = await replay('solver', loadRecording('solver'));
    const paused = traces(driver.transcript.results)[4];
    const t = paused?.trace ?? [];
    expect(paused?.phase).toBe('none');
    expect(t.filter((x) => x.startsWith('changed:'))).toHaveLength(1);
    expect(t.some((x) => x.endsWith(':ending'))).toBe(false);
    expect(t.some((x) => /^state:p\d+:idle:none$/.test(x))).toBe(true);
  });

  it('leaves no run and no analysis after a new document’s last idle and analyseEnded', async () => {
    const { driver } = await replay('solver', loadRecording('solver'));
    const last = traces(driver.transcript.results).at(-1);
    const t = last?.trace ?? [];
    const cleared = t.indexOf('cleared');
    expect(cleared).toBeGreaterThanOrEqual(0);
    expect(t.slice(cleared + 1)).toEqual([expect.stringMatching(/^state:p\d+:idle:none$/)]);
    expect(last?.phase).toBe('none');
    expect(last?.analysis).toBeNull();
    expect(last?.running).toBe(false);
    const ended = driver.transcript.events.filter((e) => e.event === 'solution.analyseEnded');
    expect(ended.at(-1)?.params).toMatchObject({ stopped: true });
  });
});
