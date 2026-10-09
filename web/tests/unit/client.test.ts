// SPDX-License-Identifier: GPL-3.0-or-later
//
// The client against an engine the test plays by hand: hello first, the
// order of replies and events, frames, coalescing, lost replies, the end.

import { describe, expect, it } from 'vitest';

import { EngineClient } from '../../src/engine/client.ts';
import type { ClientProblem } from '../../src/engine/client.ts';
import { EngineError, isSuperseded } from '../../src/engine/errors.ts';
import { settle, ScriptedTransport } from '../helpers/scripted-transport.ts';

async function ready(options?: ConstructorParameters<typeof EngineClient>[1]): Promise<{
  t: ScriptedTransport;
  client: EngineClient;
  problems: ClientProblem[];
}> {
  const t = new ScriptedTransport();
  const client = new EngineClient(t, options);
  const problems: ClientProblem[] = [];
  client.onProblem((p) => problems.push(p));
  const hello = client.hello();
  t.greet();
  await hello;
  return { t, client, problems };
}

async function rejection(p: Promise<unknown>): Promise<EngineError> {
  try {
    await p;
  } catch (e) {
    if (e instanceof EngineError) {
      return e;
    }
    throw e;
  }
  throw new Error('it did not fail');
}

describe('engine.hello', () => {
  it('goes first; calls made before it wait for its answer', async () => {
    const t = new ScriptedTransport();
    const client = new EngineClient(t);
    const info = client.call('settings.get');
    await settle();
    expect(t.sent).toEqual([]);
    const hello = client.hello();
    const settings = client.call('settings.get');
    await settle();
    expect(t.sent).toEqual([
      { jsonrpc: '2.0', id: 2, method: 'engine.hello', params: { protocol: 1 } },
    ]);
    t.greet();
    expect(await hello).toEqual({ protocol: 1, name: 'bt-engine', version: '0.1.0' });
    expect(client.engine?.version).toBe('0.1.0');
    // ids are given when a call is made; replies come in the order sent
    expect(t.sent.map((m) => m.id)).toEqual([2, 1, 3]);
    t.reply(1, { undoDepth: 25, threads: 0, solver: 'classic' });
    t.reply(3, { undoDepth: 25, threads: 0, solver: 'bt2' });
    expect((await info).solver).toBe('classic');
    expect((await settings).solver).toBe('bt2');
  });

  it('refuses an engine that speaks another protocol, and fails the calls waiting', async () => {
    const t = new ScriptedTransport();
    const client = new EngineClient(t);
    const hello = client.hello();
    const waiting = client.call('engine.info');
    t.greet(2);
    const e = await rejection(hello);
    expect(e.errc).toBe('unsupported');
    const failed = await rejection(waiting);
    expect([failed.errc, failed.method]).toEqual(['unsupported', 'engine.info']);
    expect(t.killed).toBe(true);
    expect(t.sent).toHaveLength(1);
    expect((await rejection(client.call('engine.info'))).errc).toBe('unsupported');
  });

  it('fails when the engine refuses the protocol', async () => {
    const t = new ScriptedTransport();
    const client = new EngineClient(t);
    const hello = client.hello();
    t.error(1, -32005, 'unsupported', 'protocol 1 is not spoken here');
    const e = await rejection(hello);
    expect([e.code, e.errc, e.detail]).toEqual([
      -32005,
      'unsupported',
      'protocol 1 is not spoken here',
    ]);
    expect(client.closed).toBe(true);
  });

  it('is said once', async () => {
    const { client } = await ready();
    await expect(client.hello()).rejects.toThrow(/once/);
  });
});

describe('calls', () => {
  it('resolve with their result, or fail with the code and errc', async () => {
    const { t, client } = await ready();
    const got = client.call('shape.get', { shape: 's1' });
    const missing = client.call('shape.get', { shape: 's9' });
    expect(t.sent.slice(1)).toEqual([
      { jsonrpc: '2.0', id: 2, method: 'shape.get', params: { shape: 's1' } },
      { jsonrpc: '2.0', id: 3, method: 'shape.get', params: { shape: 's9' } },
    ]);
    t.reply(2, { shape: { id: 's1' }, cells: [] });
    t.error(3, -32002, 'not_found', 'no shape s9');
    expect(await got).toEqual({ shape: { id: 's1' }, cells: [] });
    const e = await rejection(missing);
    expect([e.code, e.errc, e.method, e.message]).toEqual([
      -32002,
      'not_found',
      'shape.get',
      'shape.get: no shape s9',
    ]);
  });

  it('are pipelined in order, a bounded number at a time', async () => {
    const { t, client } = await ready({ maxInFlight: 3 });
    const calls = Array.from({ length: 7 }, (_, i) =>
      client.call('shape.get', { shape: `s${i + 1}` }),
    );
    expect(t.sent.slice(1).map((m) => m.id)).toEqual([2, 3, 4]);
    t.reply(2, 'a');
    await settle();
    expect(t.sent.slice(1).map((m) => m.id)).toEqual([2, 3, 4, 5]);
    t.reply(3, 'b');
    t.reply(4, 'c');
    t.reply(5, 'd');
    await settle();
    expect(t.sent.slice(1).map((m) => m.id)).toEqual([2, 3, 4, 5, 6, 7, 8]);
    t.reply(6, 'e');
    t.reply(7, 'f');
    t.reply(8, 'g');
    expect(await Promise.all(calls)).toEqual(['a', 'b', 'c', 'd', 'e', 'f', 'g']);
  });

  it('too long for the engine fail here, as the engine would fail them', async () => {
    const { t, client } = await ready({ maxLineBytes: 100 });
    const e = await rejection(client.call('document.setNotes', { text: 'n'.repeat(100) }));
    expect([e.code, e.errc]).toEqual([-32600, 'invalid_request']);
    expect(t.sent).toHaveLength(1);
  });

  it('whose reply never came fail when a later reply comes (replies come in order)', async () => {
    const { t, client, problems } = await ready();
    const first = client.call('engine.info');
    const second = client.call('settings.get');
    t.reply(3, 'second');
    expect((await rejection(first)).errc).toBe('protocol');
    expect(await second).toBe('second');
    expect(problems.map((p) => p.reason)).toEqual(['lost-reply']);
  });

  it('get the reply without an id: the engine could not read the oldest one', async () => {
    const { t, client } = await ready();
    const first = client.call('engine.info');
    const second = client.call('settings.get');
    t.error(null, -32600, 'invalid_request', 'the line is too long');
    t.reply(3, 'fine');
    expect((await rejection(first)).errc).toBe('invalid_request');
    expect(await second).toBe('fine');
  });

  it('ignore a reply no call waits for, and report it', async () => {
    const { t, client, problems } = await ready();
    const call = client.call('engine.info');
    t.reply(99, 'stray');
    t.reply(2, 'mine');
    expect(await call).toBe('mine');
    expect(problems.map((p) => p.reason)).toEqual(['unexpected-reply']);
  });

  it('with a malformed reply fail as protocol errors', async () => {
    const { t, client } = await ready();
    const a = client.call('engine.info');
    const b = client.call('engine.info');
    t.line({ jsonrpc: '2.0', id: 2 });
    t.line({ jsonrpc: '2.0', id: 3, error: { code: 'x' } });
    expect((await rejection(a)).errc).toBe('protocol');
    expect((await rejection(b)).errc).toBe('protocol');
  });
});

describe('frames', () => {
  const mesh = (n: number): Record<string, unknown> => ({
    frame: { frame: n, bytes: 8, arrays: { voxel: { type: 'u32', offset: 0, length: 2 } } },
    triangles: 2,
    vertices: 0,
    lo: [0, 0, 0],
    hi: [1, 1, 1],
  });

  it('come with their reply as typed-array views', async () => {
    const { t, client } = await ready();
    const call = client.call('shape.mesh', { shape: 's1' });
    t.frame(1, new Uint8Array(new Uint32Array([7, 9]).buffer));
    t.reply(2, mesh(1));
    const reply = await call;
    expect(reply.frame.arrays.voxel).toEqual(new Uint32Array([7, 9]));
    expect(reply.frame.layout.voxel).toEqual({ type: 'u32', offset: 0, length: 2 });
    expect(reply.triangles).toBe(2);
  });

  it('missing from a reply that names them fail it as a protocol error', async () => {
    const { t, client, problems } = await ready();
    const call = client.call('shape.mesh', { shape: 's1' });
    t.frame(1, new Uint8Array(8));
    t.reply(2, mesh(2));
    expect((await rejection(call)).errc).toBe('protocol');
    expect(problems.map((p) => p.reason)).toEqual(['bad-frame-reply', 'frame-unclaimed']);
  });

  it('no reply names are a protocol error', async () => {
    const { t, client, problems } = await ready();
    const call = client.call('engine.info');
    t.frame(1, new Uint8Array(4));
    t.reply(2, 'info');
    expect(await call).toBe('info');
    expect(problems.map((p) => p.reason)).toEqual(['frame-unclaimed']);
  });
});

describe('events', () => {
  it('are typed, and come after the reply in front of them is settled', async () => {
    const { t, client } = await ready();
    const order: string[] = [];
    client.on('document.changed', (p) => order.push(`changed ${p.revision}`));
    client.onEvent((e) => order.push(`any ${e}`));
    // an await two levels down, as the front end's commands will have
    const setNotes = async (): Promise<number> => {
      const r = await client.call('document.setNotes', { text: 'x' });
      order.push('reply');
      return r.revision;
    };
    const command = (async () => {
      const revision = await setNotes();
      order.push(`command ${revision}`);
    })();
    // the reply and the events behind it in one chunk
    t.emit(
      `${JSON.stringify({ jsonrpc: '2.0', id: 2, result: { revision: 5 } })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', method: 'document.changed', params: { revision: 5, what: ['notes'], shapes: [], problems: [] } })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', method: 'solve.state', params: { problem: 'p1', state: 'idle' } })}\n`,
    );
    await command;
    await settle();
    expect(order).toEqual([
      'reply',
      'command 5',
      'changed 5',
      'any document.changed',
      'any solve.state',
    ]);
  });

  it('keep coming when a handler throws', async () => {
    const { t, client, problems } = await ready();
    const seen: string[] = [];
    client.on('solve.state', () => {
      throw new Error('boom');
    });
    client.on('solve.state', (p) => seen.push(p.state));
    t.event('solve.state', { problem: 'p1', state: 'paused' });
    t.event('solve.state', { problem: 'p1', state: 'finished' });
    await settle();
    expect(seen).toEqual(['paused', 'finished']);
    expect(problems.filter((p) => p.reason === 'handler')).toHaveLength(2);
  });

  it('can be unsubscribed', async () => {
    const { t, client } = await ready();
    const seen: unknown[] = [];
    const off = client.on('solve.error', (p) => seen.push(p));
    t.event('solve.error', { problem: 'p1' });
    await settle();
    off();
    t.event('solve.error', { problem: 'p2' });
    await settle();
    expect(seen).toHaveLength(1);
  });
});

describe('coalescing', () => {
  it('keeps one call of a key at the engine; a newer waits, an even newer takes its place', async () => {
    const { t, client } = await ready();
    const mesh = (n: number): Promise<unknown> =>
      client.call('shape.get', { shape: `s${n}` }, { coalesce: 'k' });
    const a = mesh(1);
    const b = mesh(2);
    const c = mesh(3);
    const other = client.call('engine.info');
    expect(
      t.sent.slice(1).map((m) => (m.params as { shape?: string } | undefined)?.shape ?? m.method),
    ).toEqual(['s1', 'engine.info']);
    expect(isSuperseded(await rejection(b))).toBe(true);
    t.reply(2, 'a');
    expect(await a).toBe('a');
    expect(t.last()).toMatchObject({ method: 'shape.get', params: { shape: 's3' } });
    t.reply(5, 'other');
    t.reply(t.last().id as number, 'c');
    expect(await c).toBe('c');
    expect(await other).toBe('other');
  });

  it('replaces a call still queued in its place', async () => {
    const { t, client } = await ready({ maxInFlight: 1 });
    const first = client.call('engine.info');
    const a = client.call('shape.get', { shape: 's1' }, { coalesce: 'k' });
    const between = client.call('settings.get');
    const b = client.call('shape.get', { shape: 's2' }, { coalesce: 'k' });
    expect((await rejection(a)).errc).toBe('superseded');
    t.reply(2, 'first');
    await settle();
    expect(t.last()).toMatchObject({ params: { shape: 's2' } });
    t.reply(t.last().id as number, 'b');
    await settle();
    expect(t.last()).toMatchObject({ method: 'settings.get' });
    t.reply(t.last().id as number, 'between');
    expect(await Promise.all([first, b, between])).toEqual(['first', 'b', 'between']);
  });

  it('keys stay apart', async () => {
    const { t, client } = await ready();
    void client.call('shape.get', { shape: 's1' }, { coalesce: 'a' });
    void client.call('shape.get', { shape: 's2' }, { coalesce: 'b' });
    expect(t.sent).toHaveLength(3);
  });
});

describe('the end of the engine', () => {
  it('fails every call waiting as disconnected, with the last words on stderr', async () => {
    const { t, client } = await ready({ maxInFlight: 1 });
    const exits: unknown[] = [];
    client.onExit((info) => exits.push(info));
    const sent = client.call('engine.info');
    const queued = client.call('settings.get');
    const waiting = client.call('shape.get', { shape: 's1' }, { coalesce: 'k' });
    t.stderr('bt-engine: assertion failed: x\n');
    t.exit({ code: null, signal: 'SIGSEGV' });
    for (const p of [sent, queued, waiting]) {
      const e = await rejection(p);
      expect(e.errc).toBe('disconnected');
      expect(e.detail).toContain('signal SIGSEGV');
      expect(e.detail).toContain('assertion failed');
    }
    expect(exits).toEqual([{ code: null, signal: 'SIGSEGV' }]);
    expect(client.closed).toBe(true);
    expect((await rejection(client.call('engine.info'))).errc).toBe('disconnected');
  });

  it('in the middle of a frame is reported, then the calls fail', async () => {
    const { t, client, problems } = await ready();
    const call = client.call('shape.mesh', { shape: 's1' });
    t.emit(
      `${JSON.stringify({ jsonrpc: '2.0', method: 'frame', params: { frame: 1, bytes: 100 } })}\nabc`,
    );
    t.exit({ code: 1, signal: null });
    expect((await rejection(call)).errc).toBe('disconnected');
    expect(problems.map((p) => p.reason)).toEqual(['truncated']);
  });

  it('after replies still to be delivered comes after them', async () => {
    const { t, client } = await ready();
    const a = client.call('engine.info');
    const b = client.call('settings.get');
    t.emit(
      `${JSON.stringify({ jsonrpc: '2.0', id: 2, result: 'a' })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', method: 'solve.state', params: {} })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', id: 3, result: 'b' })}\n`,
    );
    t.exit({ code: 0, signal: null });
    expect(await a).toBe('a');
    expect(await b).toBe('b');
  });

  it('while greeting fails hello', async () => {
    const t = new ScriptedTransport();
    const client = new EngineClient(t);
    const hello = client.hello();
    t.exit({ code: 2, signal: null });
    expect((await rejection(hello)).errc).toBe('disconnected');
  });

  it('comes when the output cannot be followed: the calls fail and the engine is ended', async () => {
    const { t, client, problems } = await ready();
    const call = client.call('engine.info');
    t.line({ jsonrpc: '2.0', method: 'frame', params: { frame: 1 } });
    expect((await rejection(call)).errc).toBe('protocol');
    expect(t.killed).toBe(true);
    expect(problems.map((p) => p.reason)).toEqual(['bad-frame']);
  });

  it('is asked for by close (stdin) or kill', async () => {
    const { t, client } = await ready();
    client.close();
    expect(t.closedStdin).toBe(true);
    const other = await ready();
    other.client.kill();
    expect(other.t.killed).toBe(true);
  });
});
