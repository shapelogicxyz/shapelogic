// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine host: it starts an engine, starts another when one dies (the
// status says so), fails calls while there is none, gives up after quick
// deaths in a row, and starts none after stop(); its engines' protocol
// problems reach its handlers (the app log's); and every transport takes
// each of its handlers once.

import { describe, expect, it } from 'vitest';

import type { ClientProblem } from '../../src/engine/client.ts';
import type { EngineStatus } from '../../src/engine/client-types.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { EngineStartError } from '../../src/engine/spawn-failure.ts';
import { NodeTransport } from '../../src/engine/node-transport.ts';
import type { Transport } from '../../src/engine/transport.ts';
import { WasmTransport } from '../../src/demo/wasm-transport.ts';
import { EngineLines, logEngineProblems, teeStderr } from '../../src/main/app-log.ts';
import { EngineHost } from '../../src/main/engine-host.ts';
import { FakeEngine } from '../../fake-engine/fake-engine.ts';
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
    engines[0]?.stderr('shapelogic_engine: oops\n');
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
      stderr: 'shapelogic_engine: oops',
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
    for (let i = 0; i < 2; i++) {
      await until(() => engines.length === i + 1 && h.client?.engine !== null);
      engines[i]?.exit({ code: 1, signal: null });
    }
    await until(() => statuses.at(-1)?.state === 'failed');
    // two quick deaths with a limit of two: no third engine
    await settle(5);
    expect(engines).toHaveLength(2);
    // with how the last one ended (cleanup item 2.2)
    expect(statuses.at(-1)).toMatchObject({
      state: 'failed',
      reason: 'the engine died 2 times in a row soon after starting',
      failure: { which: 'session', code: 'EXITED', exitCode: 1, signal: null },
    });
  });

  it('restarts even when a status handler throws', async () => {
    const { host: h, engines, statuses } = host();
    const problems: ClientProblem[] = [];
    h.onProblem((p) => problems.push(p));
    h.onStatus((s) => {
      if (s.state === 'exited') {
        throw new Error('window gone');
      }
    });
    await h.start();
    engines[0]?.exit({ code: null, signal: 'SIGKILL' });
    await until(() => statuses.length === 3 && statuses[2]?.state === 'running');
    expect(engines).toHaveLength(2);
    expect(problems.map((p) => p.reason)).toEqual(['handler']);
  });

  it('tells each engine its setup before the window may call it; a failed setup fails the start', async () => {
    const seen: string[] = [];
    const {
      host: h,
      engines,
      statuses,
    } = host({
      setup: async (client) => {
        seen.push('setup');
        await client.call('settings.set', { panexFolder: '/data/panex' });
        seen.push('set');
      },
    });
    const started = h.start();
    await until(() => engines[0]?.sent.some((m) => m.method === 'settings.set') === true);
    // not running until the setup is through
    expect(statuses).toEqual([]);
    const set = engines[0]?.sent.find((m) => m.method === 'settings.set');
    expect(set?.params).toEqual({ panexFolder: '/data/panex' });
    engines[0]?.reply(set?.id as number, { undoDepth: 25, threads: 0 });
    await started;
    expect(seen).toEqual(['setup', 'set']);
    expect(statuses).toEqual([{ state: 'running', version: '0.1.0', restarts: 0 }]);

    const bad = host({
      setup: () => Promise.reject(new Error('no folder')),
    });
    await expect(bad.host.start()).rejects.toThrow('no folder');
    expect(bad.statuses.at(-1)).toEqual({ state: 'failed', reason: 'no folder' });
  });

  it('starts one engine when start() is called twice', async () => {
    const engines: ScriptedTransport[] = [];
    const h = new EngineHost({
      start: async () => {
        await settle(1);
        const t = new ScriptedTransport();
        engines.push(t);
        setImmediate(() => {
          t.greet();
        });
        return t;
      },
    });
    await Promise.all([h.start(), h.start()]);
    expect(engines).toHaveLength(1);
    expect(h.status.state).toBe('running');
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

  it('ends an engine that was being spawned when stop() was called', async () => {
    const engines: ScriptedTransport[] = [];
    const gate: { release: (() => void) | null } = { release: null };
    const h = new EngineHost({
      start: async () => {
        const t = new ScriptedTransport();
        engines.push(t);
        if (engines.length === 2) {
          // the second engine's --version check takes a while
          await new Promise<void>((resolve) => {
            gate.release = resolve;
          });
        }
        setImmediate(() => {
          // an engine stop() ended was never asked to say hello
          if (t.sent.length > 0) {
            t.greet();
          }
        });
        return t;
      },
      restartDelayMs: () => 0,
    });
    const statuses: EngineStatus[] = [];
    h.onStatus((s) => statuses.push(s));
    await h.start();
    engines[0]?.exit({ code: 1, signal: null });
    await until(() => gate.release !== null);
    const stopped = h.stop();
    await settle();
    gate.release?.();
    await stopped;
    await settle(5);
    expect(engines).toHaveLength(2);
    expect(engines[1]?.killed).toBe(true);
    expect(h.client).toBeNull();
    expect(statuses.map((s) => s.state)).toEqual(['running', 'exited']);
  });

  it('ends an engine that has not said hello when stop() is called', async () => {
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
    engines[0]?.exit({ code: 1, signal: null });
    await until(() => engines[1]?.sent.length === 1);
    await h.stop();
    expect(engines[1]?.killed).toBe(true);
    expect(h.client).toBeNull();
    expect(h.status.state).toBe('exited');
    await settle(5);
    expect(engines).toHaveLength(2);
  });

  it('ends an engine whose first start stop() interrupted', async () => {
    const engines: ScriptedTransport[] = [];
    const gate: { release: (() => void) | null } = { release: null };
    const h = new EngineHost({
      start: async () => {
        await new Promise<void>((resolve) => {
          gate.release = resolve;
        });
        const t = new ScriptedTransport();
        engines.push(t);
        setImmediate(() => {
          if (t.sent.length > 0) {
            t.greet();
          }
        });
        return t;
      },
    });
    const started = h.start();
    await until(() => gate.release !== null);
    const stopped = h.stop();
    gate.release?.();
    await started;
    await stopped;
    expect(engines[0]?.killed).toBe(true);
    expect(h.client).toBeNull();
    expect(h.status).toEqual({ state: 'failed', reason: 'not started' });
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
  it('keeps every detail of a start the system refused (Windows: spawn UNKNOWN)', async () => {
    const h = new EngineHost({
      start: () => {
        // as NodeTransport.start rejects when execFile throws at once
        throw new EngineStartError('spawn UNKNOWN', {
          which: 'version',
          code: 'UNKNOWN',
          message: 'spawn UNKNOWN',
          errno: -4094,
          syscall: 'spawn',
          path: 'C:\\ShapeLogic\\resources\\shapelogic_engine.exe',
          afterMs: 12,
        });
      },
      restartDelayMs: () => 0,
    });
    await expect(h.start()).rejects.toThrow('spawn UNKNOWN');
    expect(h.status).toEqual({
      state: 'failed',
      reason: 'spawn UNKNOWN',
      failure: {
        which: 'version',
        code: 'UNKNOWN',
        message: 'spawn UNKNOWN',
        errno: -4094,
        syscall: 'spawn',
        path: 'C:\\ShapeLogic\\resources\\shapelogic_engine.exe',
        afterMs: 12,
      },
    });
  });

  it('says how an engine that ended before its hello ended', async () => {
    const h = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        setImmediate(() => {
          t.stderr('a library is missing\n');
          // STATUS_DLL_NOT_FOUND, as Node gives it
          t.exit({ code: 3221225781, signal: null });
        });
        return t;
      },
      restartDelayMs: () => 0,
    });
    await expect(h.start()).rejects.toThrow();
    const s = h.status;
    expect(s.state).toBe('failed');
    expect(s.state === 'failed' ? s.failure : undefined).toMatchObject({
      which: 'session',
      code: 'EXITED',
      exitCode: 3221225781,
      signal: null,
      stderr: 'a library is missing',
    });
  });

  it('keeps the system fields of a spawn error the engine reported late', async () => {
    const h = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        setImmediate(() => {
          t.exit({
            code: null,
            signal: null,
            error: 'spawn /x/engine EACCES',
            system: { code: 'EACCES', errno: -13, syscall: 'spawn /x/engine', path: '/x/engine' },
          });
        });
        return t;
      },
      restartDelayMs: () => 0,
    });
    await expect(h.start()).rejects.toThrow();
    const s = h.status;
    expect(s.state === 'failed' ? s.failure : undefined).toMatchObject({
      which: 'session',
      code: 'EACCES',
      errno: -13,
      path: '/x/engine',
      message: 'spawn /x/engine EACCES',
    });
  });

  it('gives up with how the last engine ended, and starts again when asked', async () => {
    let quick = true;
    const engines: ScriptedTransport[] = [];
    const h = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        engines.push(t);
        setImmediate(() => {
          t.greet();
          if (quick) {
            setImmediate(() => {
              t.exit({ code: 3221225477, signal: null });
            });
          }
        });
        return t;
      },
      restartDelayMs: () => 0,
      maxQuickDeaths: 2,
    });
    const statuses: EngineStatus[] = [];
    h.onStatus((x) => statuses.push(x));
    await h.start();
    await until(() => statuses.at(-1)?.state === 'failed');
    const s = h.status;
    expect(s.state === 'failed' ? s.failure : undefined).toMatchObject({
      which: 'session',
      code: 'EXITED',
      exitCode: 3221225477,
    });
    const started = engines.length;
    // Try Again: the quick deaths are forgotten
    quick = false;
    await h.retry();
    expect(engines).toHaveLength(started + 1);
    expect(h.status).toMatchObject({ state: 'running' });
    // nothing to do while one runs
    await h.retry();
    expect(engines).toHaveLength(started + 1);
  });
});

describe('the protocol problems of its engines (cleanup item 7.2)', () => {
  it('reach the host’s handlers, the app log’s among them, and end the engine at a bad frame', async () => {
    const { host: h, engines, statuses } = host();
    const problems: ClientProblem[] = [];
    h.onProblem((p) => problems.push(p));
    const logged: string[] = [];
    logEngineProblems(
      h,
      {
        warn: (source, event, text) => {
          logged.push(`${source} ${event}: ${text}`);
        },
      },
      'window 1',
    );
    await h.start();
    // a frame announced with a size it cannot have: the stream cannot be followed
    engines[0]?.event('frame', { frame: 1, bytes: -1 });
    await until(() => statuses.some((s) => s.state === 'exited'));
    expect(problems.map((p) => p.reason)).toContain('bad-frame');
    expect(logged.some((l) => l.startsWith('window 1 Engine Problem: bad-frame: '))).toBe(true);
    expect(engines[0]?.killed).toBe(true);
    await until(() => statuses.at(-1)?.state === 'running');
  });
});

describe('a transport’s handlers (cleanup item 7.2)', () => {
  const twice = (t: Transport): void => {
    t.onData(() => undefined);
    expect(() => {
      t.onData(() => undefined);
    }).toThrow('onData is already set');
    t.onStderr?.(() => undefined);
    expect(() => {
      t.onStderr?.(() => undefined);
    }).toThrow('onStderr is already set');
    t.onExit(() => undefined);
    expect(() => {
      t.onExit(() => undefined);
    }).toThrow('onExit is already set');
  };

  it('are set once in every transport: a second would silently replace the client’s', () => {
    twice(new ScriptedTransport());
    twice(
      new FakeEngine({
        format: 'shapelogic_engine-recording',
        version: 1,
        engine: '',
        note: '',
        entries: [],
      }),
    );
    const wasm = new WasmTransport({
      _bt_start: () => undefined,
      _bt_write: () => undefined,
      _bt_poll: () => undefined,
      _bt_busy: () => 0,
      _bt_take: () => undefined,
      _bt_taken_data: () => 0,
      _bt_taken_size: () => 0,
      _bt_log_take: () => undefined,
      _bt_log_data: () => 0,
      _bt_log_size: () => 0,
      _malloc: () => 0,
      _free: () => undefined,
      HEAPU8: new Uint8Array(1),
    });
    twice(wasm);
    wasm.kill();
    // the log's tee passes the client's handlers on, once each too
    twice(teeStderr(new ScriptedTransport(), () => undefined));
  });

  it('are set once in the engine’s process transport', async () => {
    // any program will do: Node itself, reading a script from its stdin
    const t = new NodeTransport(process.execPath);
    const ended = new Promise<void>((done) => {
      t.onExit(() => {
        done();
      });
    });
    expect(() => {
      t.onExit(() => undefined);
    }).toThrow('onExit is already set');
    t.onData(() => undefined);
    expect(() => {
      t.onData(() => undefined);
    }).toThrow('onData is already set');
    t.close();
    await ended;
  });

  it('end the engine’s log lines before the client hears of the exit', () => {
    const inner = new ScriptedTransport();
    const order: string[] = [];
    const lines = new EngineLines((l) => order.push(`line ${l}`));
    const tee = teeStderr(
      inner,
      (c) => {
        lines.push(c);
      },
      () => {
        lines.end();
      },
    );
    tee.onStderr?.(() => undefined);
    tee.onExit(() => order.push('exit'));
    inner.stderr('Assertion failed: x\nlibc++abi: terminating');
    inner.exit({ code: null, signal: 'SIGABRT' });
    expect(order).toEqual(['line Assertion failed: x', 'line libc++abi: terminating', 'exit']);
  });
});
