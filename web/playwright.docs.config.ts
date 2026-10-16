// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user guide's screenshots (`npm run docs-shots`, `just docs-screenshots`):
// tests/docs/*.docs.ts drive the built app (out/, `npm run build`) with the
// release engine, as the e2e tests do, and write PNGs into site/src/assets/screens.

import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: 'tests/docs',
  testMatch: '**/*.docs.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 180_000,
  use: { actionTimeout: 30_000 },
  reporter: [['list']],
});
