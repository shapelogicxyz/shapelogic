// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine the tests run, found one way for every suite: BT_ENGINE when it
// is set and not empty, else the release build's when it is built. And a
// copy of it for one run (cleanup item 8.4): the vitest and e2e runs take
// minutes, and another session's `just build release` meanwhile would
// replace the binary under them, or leave it half written.

import { copyFileSync, existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join, resolve } from 'node:path';

import { builtProgram } from '../../src/main/engine-path.ts';

/** The repository's root. */
export const ROOT = resolve(import.meta.dirname, '../../..');

/** The release build's engine (it may not be built). */
export const RELEASE_ENGINE = builtProgram(ROOT, 'release', 'shapelogic_engine');

/** BT_ENGINE (resolved) when set and not empty, else null. */
export function engineFromEnv(): string | null {
  const fromEnv = process.env.BT_ENGINE;
  return fromEnv !== undefined && fromEnv !== '' ? resolve(fromEnv) : null;
}

/** The engine the tests run: BT_ENGINE, else the release build's when built, else null. */
export function testEngine(): string | null {
  return engineFromEnv() ?? (existsSync(RELEASE_ENGINE) ? RELEASE_ENGINE : null);
}

/**
 * Copies the engine the tests would run into a temp folder of the run's
 * own and points BT_ENGINE at the copy, for this process and those it
 * starts (the test workers, the apps). Nothing is copied when there is no
 * engine, or BT_ENGINE names one that is not there (the tests skip or fail
 * as they would). The function returned removes the copy.
 */
export function useRunCopyOfEngine(tag: string): () => void {
  const engine = testEngine();
  if (engine === null || !existsSync(engine)) {
    return () => undefined;
  }
  const dir = mkdtempSync(join(tmpdir(), `${tag}-engine-`));
  const copy = join(dir, basename(engine));
  copyFileSync(engine, copy);
  process.env.BT_ENGINE = copy;
  return () => {
    rmSync(dir, { recursive: true, force: true });
  };
}
