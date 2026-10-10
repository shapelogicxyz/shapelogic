// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the app finds bt-engine. A packaged app runs the copy in its own
// resources (electron-builder's extraResources, tools/dist.ts) and nothing
// else: no environment variable makes it run another program
// (security.ts). In development: BT_ENGINE, else the repository's dev
// build, else its release build.

import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';

/** The engine's file name on a platform. */
export function engineFile(platform: NodeJS.Platform): string {
  return platform === 'win32' ? 'bt-engine.exe' : 'bt-engine';
}

/** Where the app runs from, as findEngine needs it. */
export interface EngineSearch {
  /** The app is packaged (Electron's app.isPackaged). */
  packaged: boolean;
  /** The packaged app's resources (process.resourcesPath). */
  resources: string;
  /** The web/ directory (in development: above the main bundle, out/main). */
  webDir: string;
  /** BT_ENGINE, as security.ts's switches took it (never when packaged). */
  engine?: string | undefined;
  platform?: NodeJS.Platform;
}

/** The engine to run; null when none is built (or the packaged app lacks its own). */
export function findEngine(s: EngineSearch): string | null {
  const exe = engineFile(s.platform ?? process.platform);
  if (s.packaged) {
    const path = join(s.resources, exe);
    return existsSync(path) ? path : null;
  }
  if (s.engine !== undefined && s.engine !== '') {
    return s.engine;
  }
  for (const preset of ['dev', 'release']) {
    const path = resolve(s.webDir, '..', 'build', preset, 'engine', exe);
    if (existsSync(path)) {
      return path;
    }
  }
  return null;
}
