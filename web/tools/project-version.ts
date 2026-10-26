// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's one version, from the git tag: SHAPELOGIC_VERSION in the
// environment (the Release workflow sets it from the tag it builds), else
// the newest vX.Y[.Z] tag `git describe` finds, else 0.0.0 (a source tree
// without git); vX.Y is X.Y.0. CMake follows the same rule
// (cmake/ProjectVersion.cmake) for shapelogic_engine --version
// (core/shapelogic/base/version.h); the packaged app takes it at build
// (tools/dist.ts), and the main process at bundling (__BT_VERSION__).

import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

/** The repository's root (above web/). */
export const REPO_ROOT = resolve(import.meta.dirname, '../..');

/** The version a tag names (`v1.4` is 1.4.0, `v1.4.2` is 1.4.2), or null when it names none. */
export function versionFromTag(tag: string): string | null {
  const m = /^v?(\d+)\.(\d+)(?:\.(\d+))?$/.exec(tag.trim());
  if (m === null) {
    return null;
  }
  return `${m[1] ?? '0'}.${m[2] ?? '0'}.${m[3] ?? '0'}`;
}

/** The project's version, as CMake decides it for the same tree and environment. */
export function projectVersion(
  root: string = REPO_ROOT,
  env: NodeJS.ProcessEnv = process.env,
): string {
  const given = env.SHAPELOGIC_VERSION;
  if (given !== undefined && given !== '') {
    const v = versionFromTag(given);
    if (v === null) {
      throw new Error(`SHAPELOGIC_VERSION=${given}: not vX.Y or vX.Y.Z`);
    }
    return v;
  }
  let tag = '';
  try {
    tag = execFileSync('git', ['describe', '--tags', '--match', 'v[0-9]*', '--abbrev=0'], {
      cwd: root,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    });
  } catch {
    // no git, or no tag
  }
  return versionFromTag(tag) ?? '0.0.0';
}
