// SPDX-License-Identifier: GPL-3.0-or-later
//
// docs/ARCHITECTURE.md's map test (tests/docs/test_architecture_map.py,
// ctest architecture-map), run here too, so that front-end work that adds,
// moves or removes a file under web/src finds out without the C++ tests:
// every source file has a map line, every map line names a file that
// exists, and the prose stays capped. Skipped with a message when no
// Python 3 is on the PATH.

import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const ROOT = resolve(import.meta.dirname, '../../..');
const SCRIPT = resolve(ROOT, 'tests/docs/test_architecture_map.py');

/** The first of the usual names that runs Python 3, or null. */
function python(): string | null {
  for (const name of ['python3', 'python']) {
    const r = spawnSync(name, ['-c', 'import sys; sys.exit(sys.version_info[0] != 3)'], {
      encoding: 'utf8',
    });
    if (r.status === 0) {
      return name;
    }
  }
  return null;
}

const PYTHON = python();

describe('docs/ARCHITECTURE.md', () => {
  it.skipIf(PYTHON === null)('maps every source file, and only files that exist', () => {
    const r = spawnSync(PYTHON ?? 'python3', [SCRIPT], { cwd: ROOT, encoding: 'utf8' });
    expect(r.stderr, 'tests/docs/test_architecture_map.py').toBe('');
    expect(r.status).toBe(0);
  });
  if (PYTHON === null) {
    console.warn('architecture-map: no Python 3 on the PATH, the map test is skipped');
  }
});
