// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the website's demo is built from, and a hash of it: the demo's
// build (tools/build-demo.ts) writes it into its manifest, and the site's
// test (site/tests/demo.test.mjs, which imports this file) compares the
// working tree's with it, so a demo older than the app it shows fails
// there (cleanup item 12.11: the hash was written in both). Node only;
// erasable syntax, so Node runs it by stripping its types.

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/** What the demo is built from (paths from the repository's root). */
export const DEMO_INPUTS = [
  'CMakeLists.txt',
  'CMakePresets.json',
  'cmake',
  'core',
  'engine',
  'api',
  'web/src',
  'web/demo.vite.config.ts',
  'web/package-lock.json',
  'site/public/examples/demo',
];

/** A hash of the files under the inputs (tracked or not yet, never ignored), as they are in the working tree. */
export function inputsHash(root: string, inputs: readonly string[]): string {
  const files = execFileSync(
    'git',
    ['ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', ...inputs],
    { cwd: root },
  )
    .toString('utf8')
    .split('\0')
    .filter((f) => f !== '')
    .sort();
  const h = createHash('sha256');
  for (const f of files) {
    const path = resolve(root, f);
    if (!existsSync(path)) {
      continue;
    }
    h.update(f);
    h.update('\0');
    h.update(readFileSync(path));
    h.update('\0');
  }
  return h.digest('hex');
}
