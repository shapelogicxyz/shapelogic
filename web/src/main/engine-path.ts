// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the programs are: shapelogic_engine and the command line tool
// (shapelogic_cli), with `.exe` on Windows, in a packaged app's resources
// or where the repository's build puts them (`build/<preset>/engine/`,
// `build/<preset>/cli/`). One rule for the main process, the tools
// (tools/dist.ts) and the tests (tests/helpers/engine-path.ts; cleanup
// item 12.11). Node only, free of Electron.
//
// A packaged app runs the copies in its own resources (electron-builder's
// extraResources, tools/dist.ts) and nothing else: no environment variable
// makes it run another program (security.ts). In development the engine is
// SHAPELOGIC_ENGINE, else the repository's dev build, else its release build; the
// tool is the dev build's, else the release build's.

import { existsSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';

/** The programs the app ships. */
export type Program = 'shapelogic_engine' | 'shapelogic_cli';

/** The folder of a build that holds each program. */
const BUILD_FOLDERS: Readonly<Record<Program, string>> = {
  shapelogic_engine: 'engine',
  shapelogic_cli: 'cli',
};

/** The builds a development app runs from, the first built first. */
export const DEV_PRESETS = ['dev', 'release'] as const;

/** A program's file name on a platform. */
export function programFile(program: Program, platform: NodeJS.Platform): string {
  return platform === 'win32' ? `${program}.exe` : program;
}

/** Where a build of the repository at `root` puts a program: `build/<preset>/{engine,cli}/<file>`. */
export function builtProgram(
  root: string,
  preset: string,
  program: Program,
  platform: NodeJS.Platform = process.platform,
): string {
  return resolve(root, 'build', preset, BUILD_FOLDERS[program], programFile(program, platform));
}

/** A program of the same build as another (the tool beside an engine), for the other's platform. */
export function sameBuild(path: string, program: Program): string {
  const platform: NodeJS.Platform = basename(path).endsWith('.exe') ? 'win32' : 'linux';
  return resolve(path, '..', '..', BUILD_FOLDERS[program], programFile(program, platform));
}

/** Where the app runs from, as findEngine and findProgram need it. */
export interface EngineSearch {
  /** The app is packaged (Electron's app.isPackaged). */
  packaged: boolean;
  /** The packaged app's resources (process.resourcesPath). */
  resources: string;
  /** The web/ directory (in development: above the main bundle, out/main). */
  webDir: string;
  /** SHAPELOGIC_ENGINE, as security.ts's switches took it (never when packaged). */
  engine?: string | undefined;
  platform?: NodeJS.Platform;
}

/**
 * A program to run; null when none is built (or the packaged app lacks
 * its own). A packaged app has the copy in its resources and nothing else;
 * development takes the repository's dev build, else its release build.
 */
export function findProgram(s: Omit<EngineSearch, 'engine'>, program: Program): string | null {
  const platform = s.platform ?? process.platform;
  if (s.packaged) {
    const path = join(s.resources, programFile(program, platform));
    return existsSync(path) ? path : null;
  }
  for (const preset of DEV_PRESETS) {
    const path = builtProgram(resolve(s.webDir, '..'), preset, program, platform);
    if (existsSync(path)) {
      return path;
    }
  }
  return null;
}

/** The engine to run; null when none is built (or the packaged app lacks its own). */
export function findEngine(s: EngineSearch): string | null {
  if (!s.packaged && s.engine !== undefined && s.engine !== '') {
    return s.engine;
  }
  return findProgram(s, 'shapelogic_engine');
}
