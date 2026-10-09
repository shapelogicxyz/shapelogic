// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine host with the real engine: the engine killed while working,
// the client reports it, the host starts another, which works.

import { describe, expect, it } from 'vitest';

import type { EngineStatus } from '../../src/engine/client-types.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { NodeTransport } from '../../src/engine/node-transport.ts';
import { EngineHost } from '../../src/main/engine-host.ts';
import { ENGINE, FIXTURES, ROOT } from '../helpers/engine.ts';

describe.skipIf(ENGINE === null)('the engine host', () => {
  it('restarts an engine that was killed, and says so', async () => {
    const transports: NodeTransport[] = [];
    const host = new EngineHost({
      start: async () => {
        const t = await NodeTransport.start(ENGINE ?? '', { cwd: ROOT });
        transports.push(t);
        return t;
      },
    });
    const statuses: EngineStatus[] = [];
    host.onStatus((st) => statuses.push(st));
    await host.start();
    expect(statuses).toEqual([{ state: 'running', version: transports[0]?.version, restarts: 0 }]);
    await host.call('document.open', { path: `${FIXTURES}/SolidSixPieceBurrs.xmpuzzle` });

    // killed with calls at the engine: they fail as disconnected
    const shapes = (await host.call('shape.list')).shapes;
    const meshes = shapes.map((sh) =>
      host.call('shape.mesh', { shape: sh.id }).then(
        () => 'drawn',
        (e: unknown) => e,
      ),
    );
    process.kill(transports[0]?.pid ?? 0, 'SIGKILL');
    const outcomes = await Promise.all(meshes);
    const failed = outcomes.filter((o): o is EngineError => o instanceof EngineError);
    expect(failed.length + outcomes.filter((o) => o === 'drawn').length).toBe(shapes.length);
    // the kill came before the engine could answer them
    expect(failed.length).toBeGreaterThan(0);
    for (const e of failed) {
      expect(e.errc).toBe('disconnected');
      expect(e.detail).toContain('signal SIGKILL');
    }

    for (let i = 0; i < 500 && statuses.length < 3; i++) {
      await new Promise((r) => setTimeout(r, 10));
    }
    expect(statuses[1]).toMatchObject({ state: 'exited', code: null, signal: 'SIGKILL' });
    expect(statuses[2]).toEqual({ state: 'running', version: transports[1]?.version, restarts: 1 });
    expect(transports[1]?.pid).not.toBe(transports[0]?.pid);
    // the new engine works, on a new document
    expect((await host.call('engine.info')).name).toBe('bt-engine');
    expect((await host.call('document.get')).shapes).toEqual([]);

    await host.stop();
    expect(statuses.at(-1)).toMatchObject({ state: 'exited', code: 0, signal: null });
    expect(transports).toHaveLength(2);
  });

  it('refuses an engine whose version is not the one asked for', async () => {
    await expect(NodeTransport.start(ENGINE ?? '', { expectVersion: '999.0.0' })).rejects.toThrow(
      /999\.0\.0/,
    );
    await expect(NodeTransport.start('/no/such/bt-engine')).rejects.toThrow(/--version/);
  });
});
