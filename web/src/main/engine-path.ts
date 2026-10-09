// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the app finds bt-engine: BT_ENGINE, else the repository's dev
// build, else its release build (stage 8 adds the packaged app's copy).

import { existsSync } from 'node:fs';
import { resolve } from 'node:path';

/** The engine to run, given the web/ directory; null when none is built. */
export function findEngine(webDir: string): string | null {
  const fromEnv = process.env.BT_ENGINE;
  if (fromEnv !== undefined && fromEnv !== '') {
    return fromEnv;
  }
  const exe = process.platform === 'win32' ? 'bt-engine.exe' : 'bt-engine';
  for (const preset of ['dev', 'release']) {
    const path = resolve(webDir, '..', 'build', preset, 'engine', exe);
    if (existsSync(path)) {
      return path;
    }
  }
  return null;
}
