// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screenshots of the user guide (site/src/content/docs): the app at the
// e2e tests' window size, a fixture opened, each workspace shown and a
// solve run, written to site/src/assets/screens. Not a test of the app:
// run it when the UI changes (`just docs-screenshots`) and commit the PNGs
// the guide shows.

import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, launch, openFixture, ROOT, settle } from '../e2e/app.ts';
import { runButton, runEnded, toSolver } from '../e2e/solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const OUT = resolve(ROOT, 'site/src/assets/screens');
const FIXTURE = process.env.DOCS_FIXTURE ?? 'CubeInCage.xmpuzzle';

async function shot(page: Page, name: string): Promise<void> {
  await settle(page);
  await page.screenshot({ path: resolve(OUT, `${name}.png`) });
}

test('the guide screenshots', async () => {
  mkdirSync(OUT, { recursive: true });
  const app = await launch();
  const { page } = app;
  try {
    await openFixture(page, FIXTURE);
    await shot(page, 'entities');

    await page.keyboard.press('Control+2');
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'puzzle');
    await shot(page, 'puzzle');

    await toSolver(page);
    await shot(page, 'solver-ready');
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await shot(page, 'solver-done');
  } finally {
    await app.close();
  }
});
