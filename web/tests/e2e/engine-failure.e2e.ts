// SPDX-License-Identifier: GPL-3.0-or-later
//
// Startup health (cleanup stage 2): an engine that cannot start (BT_ENGINE
// a file that is not there) shows the window's dialog with the likely
// cause, ENOENT's, and File ▸ Open shows it again instead of doing nothing
// (the second Windows report of v2.1); the log has the failure with its
// details and the command dropped. A start that works logs the health
// report once, the engine's own first line, and the window's Startup line.

import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';

import { BUILT, ENGINE, launch, menu } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const logOf = (userData: string): string => {
  const file = join(userData, 'logs', 'shapelogic.log');
  return existsSync(file) ? readFileSync(file, 'utf8') : '';
};

test('an engine that cannot start: the dialog says why, and File ▸ Open shows it again', async () => {
  const missing = join(mkdtempSync(join(tmpdir(), 'bt-e2e-noengine-')), 'shapelogic_engine');
  const a = await launch({ engine: missing, waitForEngine: false });
  try {
    const { page } = a;
    await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'failed', {
      timeout: 30_000,
    });
    const dialog = page.locator('#engineFailure');
    await expect(dialog).toBeVisible();
    await expect(dialog).toContainText('The engine could not start');
    await expect(dialog).toContainText("The engine's file is missing");
    await expect(dialog).toContainText('ENOENT');
    for (const key of ['retry', 'copy', 'viewLogs', 'close']) {
      await expect(dialog.locator(`[data-x="${key}"]`)).toHaveCount(1);
    }
    await dialog.locator('[data-x="close"]').click();
    await expect(dialog).toHaveCount(0);
    // the status line points to the diagnostics
    await expect(page.locator('#stText')).toContainText('see Help ▸ Copy Diagnostics');
    // File ▸ Open: the dialog again, not nothing
    await menu(page, 'open');
    await expect(dialog).toBeVisible();
    await dialog.locator('[data-x="close"]').click();
    // Try Again starts it again: it fails the same way, and says so again
    await menu(page, 'new');
    await dialog.locator('[data-x="retry"]').click();
    await expect(dialog).toBeVisible({ timeout: 30_000 });
    await expect
      .poll(() => logOf(a.userData).match(/Engine Start Failed: .*ENOENT/g)?.length ?? 0)
      .toBeGreaterThanOrEqual(2);
    const log = logOf(a.userData);
    expect(log).toMatch(
      /Engine Start Failed: .*\(errno -?\d+, syscall .*the version check\) at .*shapelogic_engine, \d+ ms after start\. Likely cause: the engine's file is missing/,
    );
    expect(log).toContain('Command Dropped: open: the engine could not start');
    expect(log).toContain('Engine Try Again');
  } finally {
    await a.close();
    rmSync(join(missing, '..'), { recursive: true, force: true });
  }
});

test('a start that works: the health report once, the engine said hello, a Startup line', async () => {
  const a = await launch();
  try {
    await expect.poll(() => logOf(a.userData)).toMatch(/\[window \d+\] Startup: /);
    const log = logOf(a.userData);
    expect(log.match(/App Health: versions: ShapeLogic /g)).toHaveLength(1);
    for (const part of [
      'App Health: system: ',
      'App Health: engine: ',
      'App Health: folder user data: ',
      'App Health: gpu: ',
    ]) {
      expect(log).toContain(part);
    }
    // the engine's own first line, and how long it took to answer
    expect(log).toMatch(
      /INFO +\[window \d+\] Engine Started: version \S+, (release|debug) build, vectors /,
    );
    expect(log).toMatch(/Engine Ready: version \S+, \d+ ms from its start to its hello/);
    expect(log).toMatch(
      /Startup: window shown \d+ ms, page loaded \d+ ms, engine spawned \d+ ms, engine hello \d+ ms, first document shown \d+ ms/,
    );
    expect(log).toMatch(/Engine Folders: sliding ".*" \((exists, writable|not made yet)\)/);
  } finally {
    await a.close();
  }
});
