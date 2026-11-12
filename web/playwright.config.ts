// SPDX-License-Identifier: GPL-3.0-or-later
//
// Playwright's Electron driver over the built app (out/, `npm run build`):
// tests/e2e/*.e2e.ts. Each test launches its own app, with its own engine
// and data directory (BT_USER_DATA, which also scopes the single-instance
// lock), so the tests run side by side, those of one file too: half the
// cores' worth of apps at once (E2E_WORKERS to choose). The one test that
// times the app (perf.e2e.ts) is a project of its own, run alone: `npm run
// e2e` runs the e2e project, `npm run e2e-timed` the timed one, and `just
// app-e2e` without arguments both, one after the other. Reference
// screenshots live in tests/e2e/__screenshots__ and are compared with a
// tolerance.

import { availableParallelism } from 'node:os';

import { defineConfig } from '@playwright/test';

const workers = Number(
  process.env.E2E_WORKERS ?? Math.max(1, Math.floor(availableParallelism() / 2)),
);

export default defineConfig({
  testDir: 'tests/e2e',
  testMatch: '**/*.e2e.ts',
  snapshotPathTemplate: '{testDir}/__screenshots__/{arg}{ext}',
  globalSetup: './tests/e2e/global-setup.ts',
  fullyParallel: true,
  workers,
  retries: 0,
  timeout: 120_000,
  // an action that cannot happen fails where it waits, not at the test's timeout
  use: { actionTimeout: 30_000 },
  expect: {
    timeout: 15_000,
    toHaveScreenshot: { maxDiffPixelRatio: 0.01, threshold: 0.15, animations: 'disabled' },
  },
  reporter: [['list']],
  projects: [
    { name: 'e2e', testIgnore: '**/perf.e2e.ts' },
    { name: 'timed', testMatch: '**/perf.e2e.ts', workers: 1 },
  ],
});
