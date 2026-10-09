// SPDX-License-Identifier: GPL-3.0-or-later
//
// Playwright's Electron driver over the built app (out/, `npm run build`):
// tests/e2e/*.e2e.ts, one app at a time (each test launches its own, with
// its own engine). Reference screenshots live in
// tests/e2e/__screenshots__ and are compared with a tolerance.

import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: 'tests/e2e',
  testMatch: '**/*.e2e.ts',
  snapshotPathTemplate: '{testDir}/__screenshots__/{arg}{ext}',
  globalSetup: './tests/e2e/global-setup.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 120_000,
  // an action that cannot happen fails where it waits, not at the test's timeout
  use: { actionTimeout: 30_000 },
  expect: {
    timeout: 15_000,
    toHaveScreenshot: { maxDiffPixelRatio: 0.01, threshold: 0.15, animations: 'disabled' },
  },
  reporter: [['list']],
});
