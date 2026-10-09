// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's proxy over the bridge to the host (decision 0013), in
// memory: calls, frames crossing as ArrayBuffers and viewed again, errors,
// events after their reply, the engine's status, refused messages.

import { describe, expect, it } from 'vitest';

import { FakeEngine } from '../../fake-engine/fake-engine.ts';
import { loadRecording, loadTranscript } from '../../fake-engine/recordings.ts';
import type { BridgeCall, BridgeMessage, BtBridge } from '../../src/engine/bridge.ts';
import type { EngineStatus } from '../../src/engine/client-types.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { meshArrays } from '../../src/engine/frames.ts';
import { EngineHost } from '../../src/main/engine-host.ts';
import { serveEngine } from '../../src/main/engine-ipc.ts';
import { EngineProxy } from '../../src/renderer/engine-proxy.ts';
import { checksum } from '../../tools/sessions.ts';
import { settle, ScriptedTransport } from '../helpers/scripted-transport.ts';

/** A bridge as Electron's IPC would carry it: structured clones, one task a message. */
function wire(): {
  bridge: BtBridge;
  toHost: (handler: (m: unknown) => void) => void;
  post: (m: BridgeMessage) => void;
} {
  let hostSide: ((m: unknown) => void) | null = null;
  const rendererSide = new Set<(m: BridgeMessage) => void>();
  return {
    bridge: {
      send(call: BridgeCall): void {
        const copy = structuredClone(call);
        setImmediate(() => hostSide?.(copy));
      },
      onMessage(handler) {
        rendererSide.add(handler);
        return () => {
          rendererSide.delete(handler);
        };
      },
    },
    toHost(handler) {
      hostSide = handler;
    },
    post(m) {
      const copy = structuredClone(m);
      setImmediate(() => {
        for (const h of rendererSide) {
          h(copy);
        }
      });
    },
  };
}

async function connected(recording: string): Promise<{
  proxy: EngineProxy;
  host: EngineHost;
  posted: BridgeMessage[];
  receive: (m: unknown) => void;
}> {
  const w = wire();
  const host = new EngineHost({ start: () => new FakeEngine(loadRecording(recording)) });
  const posted: BridgeMessage[] = [];
  const server = serveEngine(host, (m) => {
    posted.push(m);
    w.post(m);
  });
  w.toHost((m) => {
    server.receive(m);
  });
  await host.start();
  const proxy = new EngineProxy(w.bridge);
  return {
    proxy,
    host,
    posted,
    receive: (m) => {
      server.receive(m);
    },
  };
}

describe('the proxy', () => {
  it('calls the engine and gets frames as the client makes them', async () => {
    const { proxy } = await connected('basic');
    const statuses: EngineStatus[] = [];
    proxy.onStatus((s) => statuses.push(s));
    const settings = await proxy.call('settings.get');
    expect(settings).toEqual({ undoDepth: 25, threads: 0, solver: 'classic' });
    const snap = await proxy.call('document.open', { path: 'tests/data/puzzles/Bermuda.xmpuzzle' });
    const shape = snap.shapes[0]?.id ?? '';
    const mesh = await proxy.call('shape.mesh', { shape, style: 'drawing' });
    const arrays = meshArrays(mesh);
    expect(arrays.indices.length).toBe(3 * mesh.triangles);
    // the same bytes the engine sent (the recorded transcript's checksum)
    const recorded = loadTranscript('basic').results[2] as { frame: { checksum: string } };
    expect(checksum(new Uint8Array(mesh.frame.buffer))).toBe(recorded.frame.checksum);
    expect(proxy.status).toEqual({ state: 'running', version: '0.1.0', restarts: 0 });
  });

  it('gets errors with their errc, and refuses what is no call', async () => {
    const { proxy, posted, receive } = await connected('dominoes');
    const e = (await proxy.call('engine.info').catch((x: unknown) => x)) as EngineError;
    expect(e).toBeInstanceOf(EngineError);
    expect([e.errc, e.method]).toEqual(['internal', 'engine.info']);
    receive({ kind: 'call', id: 77, method: 'no.such' });
    receive({ kind: 'call', id: 'x', method: 'engine.info' });
    receive('nonsense');
    await settle();
    const errcs = posted.flatMap((m) => (m.kind === 'error' ? [m.errc] : []));
    expect(errcs).toEqual(['internal', 'method_not_found']);
  });

  it('tells a proxy made after the engine started the status', async () => {
    const w = wire();
    const host = new EngineHost({ start: () => new FakeEngine(loadRecording('dominoes')) });
    const server = serveEngine(host, w.post);
    w.toHost((m) => {
      server.receive(m);
    });
    await host.start();
    // the running status was posted while nobody listened (the page was loading)
    await settle(5);
    const proxy = new EngineProxy(w.bridge);
    const seen: EngineStatus[] = [];
    proxy.onStatus((s) => seen.push(s));
    await settle(5);
    expect(proxy.status).toEqual({ state: 'running', version: '0.1.0', restarts: 0 });
    expect(seen).toEqual([proxy.status]);
    await host.stop();
  });

  it('posts the errors of the calls an exit failed before the exited status', async () => {
    const engines: ScriptedTransport[] = [];
    const host = new EngineHost({
      start: () => {
        const t = new ScriptedTransport();
        engines.push(t);
        setImmediate(() => {
          t.greet();
        });
        return t;
      },
      restartDelayMs: () => 0,
    });
    const posted: string[] = [];
    const server = serveEngine(host, (m) => {
      posted.push(m.kind === 'status' ? `status ${m.status.state}` : m.kind);
    });
    await host.start();
    server.receive({ kind: 'call', id: 1, method: 'engine.info' });
    server.receive({ kind: 'call', id: 2, method: 'settings.get' });
    await settle();
    posted.length = 0;
    engines[0]?.exit({ code: null, signal: 'SIGKILL' });
    await settle();
    expect(posted.slice(0, 3)).toEqual(['error', 'error', 'status exited']);
    await host.stop();
  });

  it('settles an error before the events behind it', async () => {
    const handlers: ((m: BridgeMessage) => void)[] = [];
    const proxy = new EngineProxy({
      send() {
        // nothing listens
      },
      onMessage(h) {
        handlers.push(h);
        return () => {
          handlers.length = 0;
        };
      },
    });
    const order: string[] = [];
    proxy.on('document.changed', () => order.push('event'));
    const call = proxy.call('engine.info').catch((e: unknown) => {
      order.push(`catch ${(e as EngineError).errc}`);
    });
    // an error and an event delivered in one task
    handlers[0]?.({ kind: 'error', id: 1, code: -32002, errc: 'not_found', message: 'x' });
    handlers[0]?.({ kind: 'event', event: 'document.changed', params: {} });
    await call;
    await settle();
    expect(order).toEqual(['catch not_found', 'event']);
  });

  it('settles a reply before the events behind it', async () => {
    const w = wire();
    const t = new ScriptedTransport();
    const host = new EngineHost({
      start: () => {
        setImmediate(() => {
          t.greet();
        });
        return t;
      },
    });
    const server = serveEngine(host, w.post);
    w.toHost((m) => {
      server.receive(m);
    });
    await host.start();
    const proxy = new EngineProxy(w.bridge);
    const order: string[] = [];
    proxy.on('document.changed', (p) => order.push(`changed ${p.revision}`));
    proxy.onEvent((e) => order.push(e));
    const command = (async () => {
      const r = await proxy.call('document.setNotes', { text: 'x' });
      order.push(`reply ${r.revision}`);
      await Promise.resolve();
      order.push('after');
    })();
    while (t.sent.length < 2) {
      await settle(1);
    }
    // the reply and the events behind it in one chunk, as the engine may write them
    t.emit(
      `${JSON.stringify({ jsonrpc: '2.0', id: 2, result: { revision: 5 } })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', method: 'document.changed', params: { revision: 5, what: ['notes'], shapes: [], problems: [] } })}\n` +
        `${JSON.stringify({ jsonrpc: '2.0', method: 'solve.state', params: { problem: 'p1', state: 'idle' } })}\n`,
    );
    await command;
    await settle(5);
    expect(order).toEqual(['reply 5', 'after', 'changed 5', 'document.changed', 'solve.state']);
  });
});
