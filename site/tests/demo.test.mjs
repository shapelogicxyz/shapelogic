// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo of the app (public/demo/, built by `just site-demo`) is there
// and is the one the app's sources give: its manifest's hash of the
// demo's inputs (web/tools/build-demo.ts) matches the working tree's. A
// mismatch means the app changed since the demo was built: run
// `just site-demo` and commit public/demo/.

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { test } from 'node:test';
import assert from 'node:assert/strict';

const SITE = resolve(import.meta.dirname, '..');
const ROOT = resolve(SITE, '..');
const DEMO = resolve(SITE, 'public/demo');

function inputsHash(inputs) {
  const files = execFileSync(
    'git',
    ['ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', ...inputs],
    { cwd: ROOT },
  )
    .toString('utf8')
    .split('\0')
    .filter((f) => f !== '')
    .sort();
  const h = createHash('sha256');
  for (const f of files) {
    const path = resolve(ROOT, f);
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

test('the demo is built', () => {
  for (const f of ['index.html', 'manifest.json']) {
    assert.ok(existsSync(resolve(DEMO, f)), `public/demo/${f} is missing: run just site-demo`);
  }
});

test('the demo is the current app', () => {
  const manifest = JSON.parse(readFileSync(resolve(DEMO, 'manifest.json'), 'utf8'));
  // the version is the git tag's when the demo was built (web/tools/project-version.ts);
  // a new release tag alone does not make the demo stale, so only its form is checked
  assert.match(manifest.version, /^\d+\.\d+\.\d+$/, 'the demo has no version');
  assert.equal(
    inputsHash(manifest.inputs),
    manifest.hash,
    'the app changed since the demo was built: run just site-demo and commit site/public/demo/',
  );
});
