// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fake engine answers the recorded sessions as the real engine did:
// the same session code over it sees what the recording's client saw.

import { describe, expect, it } from 'vitest';

import { FakeEngine } from '../../fake-engine/fake-engine.ts';
import { loadRecording, loadTranscript } from '../../fake-engine/recordings.ts';
import { EngineClient } from '../../src/engine/client.ts';
import { isEngineError } from '../../src/engine/errors.ts';
import { Driver, SESSIONS } from '../../tools/sessions.ts';

async function replay(
  name: string,
  timing: boolean,
): Promise<{ driver: Driver; fake: FakeEngine }> {
  const fake = new FakeEngine(loadRecording(name), { timing, speed: 50 });
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

describe.each(['basic', 'dominoes'])('the recording %s', (name) => {
  it.each([false, true])('replays what the client saw (timing %s)', async (timing) => {
    const { driver, fake } = await replay(name, timing);
    expect(fake.mismatches).toEqual([]);
    expect(fake.through).toBe(true);
    const expected = loadTranscript(name);
    expect(driver.transcript.results).toEqual(expected.results);
    expect(driver.transcript.events).toEqual(expected.events);
  });
});

describe('a request the recording does not have', () => {
  it('is answered with an error and noted', async () => {
    const fake = new FakeEngine(loadRecording('dominoes'));
    const client = new EngineClient(fake);
    await client.hello();
    const e: unknown = await client.call('engine.info').catch((x: unknown) => x);
    expect(isEngineError(e, 'internal')).toBe(true);
    expect(fake.mismatches).toHaveLength(1);
    expect(fake.mismatches[0]).toContain('document.new');
    client.kill();
  });
});
