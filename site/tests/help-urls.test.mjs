// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's Help menu opens the addresses in web/src/main/security.ts
// (HELP_URLS). Fails when the built site (`npm run build`) has no page at
// one of them, so a renamed or removed doc page cannot break the menu.

import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { test } from 'node:test';

const source = readFileSync(new URL('../../web/src/main/security.ts', import.meta.url), 'utf8');
const urls = [...source.matchAll(/^\s+\w+: '(https:\/\/shapelogic\.xyz\/[^']*)',$/gm)].map((m) => m[1]);

test('HELP_URLS were found', () => {
  assert.ok(urls.length >= 2, `found ${urls.length} help URLs in security.ts`);
});

for (const url of urls) {
  test(`${url} is a page of the built site`, () => {
    const { pathname } = new URL(url);
    const file = pathname.endsWith('/') ? `${pathname}index.html` : pathname;
    assert.ok(
      existsSync(new URL(`../dist${file}`, import.meta.url)),
      `dist${file} is missing: build the site, or fix HELP_URLS`,
    );
  });
}
