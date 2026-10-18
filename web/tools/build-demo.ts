// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds the website's demo of the app into site/public/demo/ (served at
// /demo/): the engine compiled to WebAssembly by the wasm preset
// (build/wasm/engine/shapelogic_engine.{js,wasm}, from engine/wasm_main.cpp)
// copied into src/demo/engine/, then the renderer built for the browser
// (demo.vite.config.ts, entry src/demo/main.ts). manifest.json beside the
// page records the version and a hash of the demo's inputs, so that the
// site's tests (site/tests/demo.test.mjs) can tell a demo older than the
// app it shows.
//
//   node tools/build-demo.ts [--check] [--headed]     (npm run demo; just site-demo)
//
// --check then opens the built demo in Google Chrome (Playwright's `chrome`
// channel) and drives it through its tabs, an edit and the solution's
// disassembly, failing on an error of the page.

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { extname, resolve } from 'node:path';

import { build } from 'vite';

import { DEMO_BASE, DEMO_OUT } from '../demo.vite.config.ts';
import { projectVersion } from './project-version.ts';

const WEB = resolve(import.meta.dirname, '..');
const ROOT = resolve(WEB, '..');
const WASM_BUILD = resolve(ROOT, 'build/wasm/engine');
const ENGINE_DIR = resolve(WEB, 'src/demo/engine');
const ENGINE_FILES = ['shapelogic_engine.js', 'shapelogic_engine.wasm'];

/** What the demo is built from (paths from the repository's root; the site's test hashes the same). */
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
  'site/public/examples/PelikanBurr.xmpuzzle',
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

const ORIGIN = 'http://demo.invalid';
const TYPES: Record<string, string> = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.png': 'image/png',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.xmpuzzle': 'application/gzip',
};

async function check(headed: boolean): Promise<void> {
  const { chromium } = await import('@playwright/test');
  const browser = await chromium.launch({ channel: 'chrome', headless: !headed });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    const errors: string[] = [];
    page.on('pageerror', (e) => errors.push(e.stack ?? e.message));
    page.on('console', (m) => {
      if (m.type() === 'error') {
        errors.push(m.text());
      }
    });
    await page.route(`${ORIGIN}/**`, async (route) => {
      const path = new URL(route.request().url()).pathname;
      const file = path.startsWith(DEMO_BASE)
        ? resolve(DEMO_OUT, path.slice(DEMO_BASE.length) || 'index.html')
        : resolve(ROOT, 'site/public', path.slice(1));
      if (!existsSync(file)) {
        await route.fulfill({ status: 404 });
        return;
      }
      await route.fulfill({
        body: readFileSync(file),
        contentType: TYPES[extname(file)] ?? 'application/octet-stream',
      });
    });
    await page.goto(`${ORIGIN}${DEMO_BASE}`);
    await page.waitForFunction(
      () => document.getElementById('fname')?.textContent === 'PelikanBurr.xmpuzzle',
      null,
      { timeout: 60_000 },
    );
    // the Solver: Assemble and Disassemble, the solution played to its end
    await page.keyboard.press('Control+3');
    await page.waitForSelector('#stage[data-tab="solver"][data-busy="0"]');
    await page.waitForSelector('#leftS .mcard.on[data-m="dis"]');
    await page.click('#slvList .lrow[data-i="0"]');
    await page.waitForSelector('#splayer[data-ready="1"]');
    await page.click('#splayer [data-t="end"]');
    await page.waitForFunction(
      () => document.getElementById('splayer')?.dataset.t === '108.000',
      null,
      { timeout: 30_000 },
    );
    // Solve does nothing: the solution stays
    await page.click('#leftS [data-r="start"]');
    await page.waitForTimeout(500);
    if ((await page.getAttribute('#splayer', 'data-ready')) !== '1') {
      throw new Error('Solve closed the player');
    }
    await page.keyboard.press('Control+2');
    await page.waitForSelector('#stage[data-tab="puzzle"][data-busy="0"]');
    // an edit: the third shape's first cell in the voxel editor (as in the
    // app, a changed piece drops its problem's solutions)
    await page.keyboard.press('Control+1');
    await page.waitForSelector('#stage[data-tab="entities"][data-busy="0"]');
    await page.locator('#shapeList .row').nth(2).locator('.chip').click();
    const before = await page.locator('#stText').textContent();
    await page.locator('svg .cell').first().click();
    await page.waitForFunction((b) => document.getElementById('stText')?.textContent !== b, before);
    mkdirSync(resolve(WEB, 'test-results'), { recursive: true });
    await page.screenshot({ path: resolve(WEB, 'test-results/demo.png') });
    if (errors.length > 0) {
      throw new Error(`the page reported errors:\n${errors.join('\n')}`);
    }
    console.log('the demo opens its puzzle, takes an edit, plays the solution');
  } finally {
    await browser.close();
  }
}

async function main(): Promise<void> {
  const args = new Set(process.argv.slice(2));
  for (const f of ENGINE_FILES) {
    if (!existsSync(resolve(WASM_BUILD, f))) {
      throw new Error(
        `no ${f} in ${WASM_BUILD}: build the engine for the browser first (just site-demo, or emcmake cmake --preset wasm && cmake --build --preset wasm)`,
      );
    }
  }
  mkdirSync(ENGINE_DIR, { recursive: true });
  for (const f of ENGINE_FILES) {
    copyFileSync(resolve(WASM_BUILD, f), resolve(ENGINE_DIR, f));
  }
  await build({ configFile: resolve(WEB, 'demo.vite.config.ts'), logLevel: 'warn' });
  const manifest = {
    version: projectVersion(ROOT),
    inputs: DEMO_INPUTS,
    hash: inputsHash(ROOT, DEMO_INPUTS),
    built: new Date().toISOString(),
  };
  writeFileSync(resolve(DEMO_OUT, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`built the demo in ${DEMO_OUT} (version ${manifest.version})`);
  if (args.has('--check')) {
    await check(args.has('--headed'));
  }
}

await main();
