// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's one version: CMakeLists.txt's `project(... VERSION x.y.z)`,
// which bt-engine --version prints (core/bt/base/version.h). The packaged
// app takes it at build (tools/dist.ts); a unit test keeps package.json's
// version, which the app reports in development, equal to it.

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/** The repository's root (above web/). */
export const REPO_ROOT = resolve(import.meta.dirname, '../..');

/** The version CMakeLists.txt's project() declares. */
export function projectVersion(root: string = REPO_ROOT): string {
  const text = readFileSync(resolve(root, 'CMakeLists.txt'), 'utf8');
  const m = /\bproject\(\s*\S+\s+VERSION\s+(\d+\.\d+\.\d+)\b/.exec(text);
  if (m?.[1] === undefined) {
    throw new Error(`${resolve(root, 'CMakeLists.txt')}: no project(... VERSION x.y.z)`);
  }
  return m[1];
}
