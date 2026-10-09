// SPDX-License-Identifier: GPL-3.0-or-later
//
// Records the sessions of tools/sessions.ts against the real engine into
// web/fake-engine/recordings/<name>.json, with what the client saw in
// <name>.transcript.json (the replay test compares against it):
//
//   node tools/record-sessions.ts [names...]     (npm run record)
//
// The engine is BT_ENGINE or build/release/engine/bt-engine, run from the
// repository's root (the sessions open fixtures by relative path).

import { existsSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { EngineClient } from '../src/engine/client.ts';
import { NodeTransport } from '../src/engine/node-transport.ts';
import { RecordingTransport } from '../src/engine/recorder.ts';
import { RECORDINGS_DIR, recordingJson } from '../fake-engine/recordings.ts';
import { Driver, SESSIONS } from './sessions.ts';

const ROOT = resolve(import.meta.dirname, '../..');

async function record(name: string, engine: string): Promise<void> {
  const script = SESSIONS[name];
  if (script === undefined) {
    throw new Error(`no session ${name}`);
  }
  const node = await NodeTransport.start(engine, { cwd: ROOT });
  const transport = new RecordingTransport(node);
  const client = new EngineClient(transport);
  const driver = new Driver(client);
  await client.hello();
  await script.run(driver);
  await new Promise<void>((done) => {
    client.onExit(() => {
      done();
    });
    client.close();
  });
  const recording = transport.recording(`bt-engine ${node.version ?? '?'}`, script.note);
  writeFileSync(resolve(RECORDINGS_DIR, `${name}.json`), recordingJson(recording));
  writeFileSync(
    resolve(RECORDINGS_DIR, `${name}.transcript.json`),
    `${JSON.stringify(driver.transcript, null, 1)}\n`,
  );
  console.log(`recorded ${name}: ${recording.entries.length} entries`);
}

async function main(): Promise<void> {
  const engine = process.env.BT_ENGINE ?? resolve(ROOT, 'build/release/engine/bt-engine');
  if (!existsSync(engine)) {
    throw new Error(`no engine at ${engine}: run \`just build release\` or set BT_ENGINE`);
  }
  const names = process.argv.slice(2);
  for (const name of names.length > 0 ? names : Object.keys(SESSIONS)) {
    await record(name, engine);
  }
}

await main();
