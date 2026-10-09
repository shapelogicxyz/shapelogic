// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine host: it starts an engine, starts another when one dies (the
// status says so), fails calls while there is none, gives up after quick
// deaths in a row, and starts none after stop().

import { describe, expect, it } from 'vitest';

import type { EngineStatus } from '../../src/engine/client-types.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { EngineHost } from '../../src/main/engine-host.ts';
import { settle, ScriptedTransport } from '../helpers/scripted-transport.ts';

/** A host over scripted engines that greet at once. */
function host(options: Partial<ConstructorParameters<typeof EngineHost>[0]> = {}): {
  host: EngineHost;
  engines: ScriptedTransport[];
  statuses: EngineStatus[];
} {
  const engines: ScriptedTransport[] = [];
  const h = new EngineHost({
    start: () => {
      const t = new ScriptedTransport();
      engines.push(t);
      // greet once the client has sent engine.hello
      setImmediate(() => {
        t.greet();
      });
      return t;
    },
    restartDelayMs: () => 0,
    ...options,
  });
  const statuses: EngineStatus[] = [];
  h.onStatus((s) => statuses.push(s));
  return { host: h, engines, statuses };
}

async function until(cond: () => boolean): Promise<void> {
  for (let i = 0; i < 200 && !cond(); i++) {
    await settle(1);
  }
  expect(cond()).toBe(true);
}

describe('the engine host', () => {
  it('starts an engine and passes calls and events on', async () => {
    const { host: h, engines, statuses } = host();
    await h.start();
    expect(statuses).toEqual([{ state: 'running', version: '0.1.0', restarts: 0 }]);
    const seen: string[] = [];
    h.on('solve.state', (p) => seen.push(p.state));
    const call = h.call('settings.get');
    const t = engines[0];
    t?.reply(2, { undoDepth: 25, threads: 0, solver: 'classic' });
    t?.event('solve.state', { problem: 'p1', state: 'finished' });
    expect((await call).undoDepth).toBe(25);
    await until(() => seen.length === 1);
  });

  it('starts another engine when one dies, and tells the window', async () => {
    const { host: h, engines, statuses } = host();
    await h.start();
    const seen: string[] = [];
    h.on('solve.state', (p) => seen.push(p.state));
    const call = h.call('engine.info');
    engines[0]?.stderr('bt-engine: oops\n');
    engines[0]?.exit({ code: null, signal: 'SIGKILL' });
    const e = (await call.catch((x: unknown) => x)) as EngineError;
    expect(e.errc).toBe('disconnected');
    // while there is no engine, calls fail at once
    expect(((await h.call('engine.info').catch((x: unknown) => x)) as EngineError).errc).toBe(
      'disconnected',
    );
    await until(() => statuses.length === 3);
    expect(statuses[1]).toEqual({
      state: 'exited',
      code: null,
      signal: 'SIGKILL',
      stderr: 'bt-engine: oops',
    });
    expect(statuses[2]).toEqual({ state: 'running', version: '0.1.0', restarts: 1 });
    // the subscriptions carry over to the new engine
    engines[1]?.event('solve.state', { problem: 'p1', state: 'idle' });
    await until(() => seen.length === 1);
    const again = h.call('settings.get');
    engines[1]?.reply(2, { undoDepth: 1, threads: 0, solver: 'classic' });
    expect((await again).undoDepth).toBe(1);
  });

  it('fails calls while a new engine has not said hello yet', async () => {
    const engines: ScriptedTransport[] = [];
    const h = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        engines.push(t);
        if (engines.length === 1) {
          setImmediate(() => {
            t.greet();
          });
        }
        return t;
      },
      restartDelayMs: () => 0,
    });
    await h.start();
    engines[0]?.exit({ code: null, signal: 'SIGKILL' });
    await until(() => engines.length === 2 && engines[1]?.sent.length === 1);
    const e = (await h.call('engine.info').catch((x: unknown) => x)) as EngineError;
    expect([e.errc, e.detail]).toEqual(['disconnected', 'the engine is restarting']);
    // nothing but its hello reached the new engine
    expect(engines[1]?.sent.map((m) => m.method)).toEqual(['engine.hello']);
    engines[1]?.greet();
    await until(() => h.status.state === 'running');
    const info = h.call('settings.get');
    engines[1]?.reply(2, { undoDepth: 25, threads: 0, solver: 'classic' });
    expect((await info).solver).toBe('classic');
  });

  it('gives up after quick deaths in a row', async () => {
    const { host: h, engines, statuses } = host({ maxQuickDeaths: 2 });
    await h.start();
    for (let i = 0; i < 3; i++) {
      await until(() => engines.length === i + 1 && h.client?.engine !== null);
      engines[i]?.exit({ code: 1, signal: null });
    }
    await until(() => statuses.at(-1)?.state === 'failed');
    expect(engines).toHaveLength(3);
  });

  it('counts an engine that cannot start as a quick death', async () => {
    let starts = 0;
    const h = new EngineHost({
      start: () => {
        starts++;
        throw new Error('no such engine');
      },
      restartDelayMs: () => 0,
      maxQuickDeaths: 2,
    });
    const statuses: EngineStatus[] = [];
    h.onStatus((s) => statuses.push(s));
    await expect(h.start()).rejects.toThrow('no such engine');
    expect(statuses).toEqual([{ state: 'failed', reason: 'no such engine' }]);
    expect(starts).toBe(1);
  });

  it('starts no engine after stop()', async () => {
    const { host: h, engines, statuses } = host();
    await h.start();
    await h.stop();
    expect(engines[0]?.closedStdin).toBe(true);
    await settle(5);
    expect(engines).toHaveLength(1);
    expect(statuses.at(-1)?.state).toBe('exited');
    expect(((await h.call('engine.info').catch((x: unknown) => x)) as EngineError).errc).toBe(
      'disconnected',
    );
  });

  it('refuses an engine of another protocol and does not restart it', async () => {
    const engines: ScriptedTransport[] = [];
    const h = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        engines.push(t);
        setImmediate(() => {
          t.greet(2);
        });
        return t;
      },
      restartDelayMs: () => 0,
    });
    await expect(h.start()).rejects.toThrow(/protocol/);
    await settle(5);
    expect(engines).toHaveLength(1);
    expect(engines[0]?.killed).toBe(true);
    expect(h.status.state).toBe('failed');
  });
});
