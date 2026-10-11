// SPDX-License-Identifier: GPL-3.0-or-later
//
// The client against the real engine: hello, engine.info's methods, every
// fixture opened and every shape's mesh in every style, a line over the
// engine's limit, the error codes.

import { readdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { ERROR_CODES, METHOD_NAMES } from '../../src/engine/api.generated.ts';
import type { MeshStyle } from '../../src/engine/api.generated.ts';
import { EngineClient } from '../../src/engine/client.ts';
import { EngineError } from '../../src/engine/errors.ts';
import { meshArrays } from '../../src/engine/frames.ts';
import { engineVersion, NodeTransport } from '../../src/engine/node-transport.ts';
import { ENGINE, FIXTURES, ROOT, Session } from '../helpers/engine.ts';

async function failure(p: Promise<unknown>): Promise<EngineError> {
  try {
    await p;
  } catch (e) {
    if (e instanceof EngineError) {
      return e;
    }
    throw e;
  }
  throw new Error('the call succeeded');
}

/** The code is one decision 0012 gives the errc. */
function expectCode(e: EngineError, errc: keyof typeof ERROR_CODES): void {
  expect(e.errc).toBe(errc);
  expect(ERROR_CODES[errc] as readonly number[]).toContain(e.code);
}

const fixtures = readdirSync(resolve(ROOT, FIXTURES))
  .filter((f) => f.endsWith('.xmpuzzle'))
  .sort();

describe.skipIf(ENGINE === null)('the engine', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('answers hello, and refuses another protocol', async () => {
    expect(s.client.engine).toEqual({
      protocol: 1,
      name: 'bt-engine',
      version: await engineVersion(ENGINE ?? ''),
    });
    expectCode(await failure(s.client.call('engine.hello', { protocol: 2 })), 'unsupported');
    expectCode(await failure(s.client.call('engine.hello', { protocol: 0 })), 'invalid_params');
  });

  it("has the schema's methods, no more and no fewer", async () => {
    const info = await s.client.call('engine.info');
    expect([...info.methods].sort()).toEqual([...METHOD_NAMES]);
    expect(info.protocol).toBe(1);
    expect(new Set(info.grids.map((g) => g.grid))).toEqual(
      new Set(['bricks', 'prisms', 'spheres', 'rhombic', 'tetraOcta', 'sliding', 'stacking']),
    );
  });

  it('opens every fixture, quietly', async () => {
    expect(fixtures.length).toBeGreaterThanOrEqual(19);
    const grids = new Set<string>();
    for (const f of fixtures) {
      const from = s.events.length;
      const snap = await s.client.call('document.open', { path: `${FIXTURES}/${f}` });
      expect(snap.dirty).toBe(false);
      expect(snap.path).toBe(`${FIXTURES}/${f}`);
      expect(snap.shapes.length).toBeGreaterThan(0);
      grids.add(snap.grid);
      await s.drain(20);
      expect(s.events.slice(from).filter((e) => e.event === 'document.changed')).toEqual([]);
    }
    expect(grids).toEqual(
      new Set(['bricks', 'prisms', 'spheres', 'rhombic', 'tetraOcta', 'sliding', 'stacking']),
    );
  });

  it('draws every shape of every fixture in every style, the frames fitting their counts', async () => {
    const styles: MeshStyle[] = ['drawing', 'wireframe', 'flat'];
    let meshes = 0;
    for (const f of fixtures) {
      const snap = await s.client.call('document.open', { path: `${FIXTURES}/${f}` });
      // pipelined: every mesh of the fixture asked for at once
      const replies = await Promise.all(
        snap.shapes.flatMap((shape) =>
          styles.map((style) => s.client.call('shape.mesh', { shape: shape.id, style })),
        ),
      );
      replies.forEach((mesh, i) => {
        const where = `${f} ${snap.shapes[Math.floor(i / 3)]?.label ?? '?'} ${styles[i % 3] ?? '?'}`;
        const a = meshArrays(mesh);
        expect(mesh.frame.buffer.byteLength, where).toBe(mesh.frame.bytes);
        expect(a.positions.length, where).toBe(3 * mesh.vertices);
        expect(a.voxel.length, where).toBe(mesh.triangles);
        if (styles[i % 3] === 'flat' && mesh.triangles > 0) {
          expect(a.edgeLines.length, where).toBeGreaterThan(0);
        }
      });
      meshes += replies.length;
      const frames = new Set(replies.map((m) => m.frame.frame));
      expect(frames.size).toBe(replies.length);
    }
    expect(meshes).toBeGreaterThan(300);
  });

  it('refuses a line over its limit, and goes on', async () => {
    const notes = 'n'.repeat(64 * 1024 * 1024);
    // this client refuses it without sending it
    expectCode(
      await failure(s.client.call('document.setNotes', { text: notes })),
      'invalid_request',
    );
    // a client that sends it: the engine reads it, drops it, answers without an id
    const transport = await NodeTransport.start(ENGINE ?? '', { cwd: ROOT });
    const loose = new EngineClient(transport, { maxLineBytes: 2 ** 28 });
    await loose.hello();
    const before = loose.call('settings.get');
    const long = loose.call('document.setNotes', { text: notes });
    const after = loose.call('engine.info');
    expect((await before).undoDepth).toBe(25);
    const e = await failure(long);
    expect([e.code, e.errc, e.detail]).toEqual([-32600, 'invalid_request', 'the line is too long']);
    expect((await after).name).toBe('bt-engine');
    expect((await loose.call('document.get')).notes).toBe('');
    await new Promise<void>((done) => {
      loose.onExit(() => {
        done();
      });
      loose.close();
    });
    expect(loose.exit).toEqual({ code: 0, signal: null });
  }, 60_000);

  it('answers errors with the codes of decision 0012', async () => {
    const c = s.client;
    expectCode(await failure(c.call('shape.get', { shape: 's999' })), 'not_found');
    expectCode(await failure(c.call('shape.get', { shape: 'c1' })), 'invalid_params');
    expectCode(await failure(c.call('document.open', { path: 'api/types.json' })), 'parse_error');
    expectCode(
      await failure(c.call('document.open', { path: `${FIXTURES}/none.xmpuzzle` })),
      'io_error',
    );
    expectCode(await failure(c.call('edit.undo')), 'invalid_state');
    const untyped = c.call.bind(c) as (m: string, p?: unknown) => Promise<unknown>;
    expectCode(await failure(untyped('no.such.method')), 'method_not_found');
    expectCode(await failure(c.call('document.openBytes', { data: '%%%' })), 'invalid_argument');
    // a stacking puzzle is solved with Find Solutions on (the original's
    // message without it); nothing saved: no Panex folder
    await c.call('settings.set', { panexFolder: '' });
    await c.call('document.open', { path: `${FIXTURES}/test_stacking_hanoi.xmpuzzle` });
    const p = (await c.call('problem.list')).problems[0]?.id ?? '';
    expectCode(await failure(c.call('solve.start', { problem: p })), 'invalid_argument');
    expectCode(await failure(c.call('solve.prepare', { problem: p })), 'unsupported');
  });
});
