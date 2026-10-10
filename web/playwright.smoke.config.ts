// SPDX-License-Identifier: GPL-3.0-or-later
//
// The packaged app's smoke test (tests/e2e/packaged.smoke.ts, stage 8):
// `just web-smoke`, after `just web-dist --dir`. Separate from the e2e
// tests, which run the build in out/ with Playwright's Electron driver.

import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: 'tests/e2e',
  testMatch: '**/*.smoke.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 180_000,
  use: { actionTimeout: 30_000 },
  expect: { timeout: 15_000 },
  reporter: [['list']],
});
