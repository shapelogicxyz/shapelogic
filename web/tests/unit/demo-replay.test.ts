// SPDX-License-Identifier: GPL-3.0-or-later
//
// The website demo's engine (src/demo/replay-bridge.ts): each call gets
// the recorded reply and the events after it, a call recorded more than
// once its replies in order, a call not recorded fails as `superseded`
// (a change is reported as refused); and the recording on the site
// (site/public/demo/session.json, tools/record-demo.ts) is well formed.

import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

import { ReplayBridge } from '../../src/demo/replay-bridge.ts';
import { callKey } from '../../src/demo/session.ts';
import type { DemoSession } from '../../src/demo/session.ts';
import type { BridgeMessage } from '../../src/engine/bridge.ts';
import { toBase64 } from '../../src/engine/recorder.ts';

const SITE_SESSION = resolve(import.meta.dirname, '../../../site/public/demo/session.json');

function session(): DemoSession {
  return {
    format: 'shapelogic-demo-session',
    version: 1,
    status: { state: 'running', version: '0.1.0', restarts: 0 },
    puzzle: 'p.xmpuzzle',
    events: [{ event: 'hello', params: {} }],
    calls: [
      { method: 'document.get', reply: { kind: 'reply', result: { revision: 1 } }, events: [] },
      {
        method: 'document.get',
        reply: { kind: 'reply', result: { revision: 2 } },
        events: [{ event: 'document.changed', params: { revision: 2 } }],
      },
      {
        method: 'shape.mesh',
        params: { style: 'flat', shape: 's1' },
        reply: {
          kind: 'reply',
          result: { mesh: {} },
          frame: { desc: { frame: 7, bytes: 4, arrays: {} }, data: 0 },
        },
        events: [],
      },
    ],
    frames: [toBase64(new Uint8Array([1, 2, 3, 4]))],
  };
}

/** A bridge over a session, and what it posted. */
function replay(refused: string[] = []): { bridge: ReplayBridge; got: BridgeMessage[] } {
  const bridge = new ReplayBridge(session(), (m) => refused.push(m));
  const got: BridgeMessage[] = [];
  bridge.onMessage((m) => got.push(m));
  return { bridge, got };
}

const tick = (): Promise<void> => new Promise((done) => setTimeout(done, 5));

describe('the demo replay', () => {
  it('tells its status, then the events before the first reply', async () => {
    const { bridge, got } = replay();
    bridge.send({ kind: 'status' });
    bridge.send({ kind: 'status' });
    await tick();
    expect(got.map((m) => m.kind)).toEqual(['status', 'event', 'status']);
  });

  it('answers a call recorded twice in order, then with its last reply', async () => {
    const { bridge, got } = replay();
    for (const id of [1, 2, 3]) {
      bridge.send({ kind: 'call', id, method: 'document.get' });
    }
    await tick();
    expect(got).toEqual([
      { kind: 'reply', id: 1, result: { revision: 1 } },
      { kind: 'reply', id: 2, result: { revision: 2 } },
      { kind: 'event', event: 'document.changed', params: { revision: 2 } },
      { kind: 'reply', id: 3, result: { revision: 2 } },
      { kind: 'event', event: 'document.changed', params: { revision: 2 } },
    ]);
  });

  it('matches parameters whatever their order, and hands out the frame', async () => {
    const { bridge, got } = replay();
    bridge.send({
      kind: 'call',
      id: 4,
      method: 'shape.mesh',
      params: { shape: 's1', style: 'flat' },
    });
    await tick();
    const m = got[0];
    expect(m?.kind === 'reply' && m.id === 4).toBe(true);
    if (m?.kind === 'reply') {
      expect(m.frame?.desc.frame).toBe(7);
      expect([...new Uint8Array(m.frame?.data ?? new ArrayBuffer(0))]).toEqual([1, 2, 3, 4]);
    }
  });

  it('fails a call it does not have as superseded; a change is refused', async () => {
    const refused: string[] = [];
    const { bridge, got } = replay(refused);
    bridge.send({ kind: 'call', id: 5, method: 'shape.mesh', params: { shape: 's2' } });
    bridge.send({ kind: 'call', id: 6, method: 'shape.setCells', params: { shape: 's1' } });
    await tick();
    expect(got).toEqual([
      { kind: 'error', id: 5, code: 0, errc: 'superseded', message: 'not in the demo' },
      { kind: 'error', id: 6, code: 0, errc: 'superseded', message: 'not in the demo' },
    ]);
    expect(refused).toEqual(['shape.setCells']);
    expect(bridge.misses).toHaveLength(2);
  });
});

describe.runIf(existsSync(SITE_SESSION))("the site's recording", () => {
  it('is well formed: its frames exist, the demo opens its puzzle, the solution plays', () => {
    const s = JSON.parse(readFileSync(SITE_SESSION, 'utf8')) as DemoSession;
    expect(s.format).toBe('shapelogic-demo-session');
    expect(s.status.state).toBe('running');
    expect(existsSync(resolve(import.meta.dirname, '../../..', s.puzzle))).toBe(true);
    for (const c of s.calls) {
      if (c.reply.kind === 'reply' && c.reply.frame !== undefined) {
        expect(s.frames[c.reply.frame.data]).toBeTypeOf('string');
      }
    }
    const keys = new Set(s.calls.map((c) => callKey(c.method, c.params)));
    expect(keys.has(callKey('document.open', { path: s.puzzle }))).toBe(true);
    expect(s.calls.some((c) => c.method === 'playback.poses')).toBe(true);
  });
});
