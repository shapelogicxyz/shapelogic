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
// channel) and drives it through its tabs, the solution's disassembly, a
// played Solve and an edit, then the theme list, every puzzle type's
// example (each one's played Solve, the Play tab of Sliding and Stacking),
// a reload that keeps the type and the tab, and a tab named in the URL,
// failing on an error of the page.

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
  'site/public/examples/demo',
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

/** Opens the `type` example from the list and plays its Solve (and Play, for Sliding and Stacking). */
async function examplePlays(page: import('@playwright/test').Page, type: string): Promise<void> {
  const sliding = type.startsWith('sliding');
  await page.selectOption('#demoType', type);
  await page.waitForFunction(
    (t) =>
      window.location.search.includes(`type=${t}`) &&
      !(document.getElementById('demoType') as HTMLSelectElement).disabled,
    type,
    { timeout: 30_000 },
  );
  await page.keyboard.press('Control+3');
  await page.waitForSelector('#stage[data-tab="solver"][data-busy="0"]');
  await page.waitForSelector('#slvList .lrow[data-i="0"]', { timeout: 15_000 });
  await page.click('#leftS [data-r="start"]');
  // a sliding board on a coarser grid asks first, as in the app (the
  // nested example's does; the standard one's starts at once)
  const fullSearch = page.getByRole('button', { name: 'Full Search' });
  await page.waitForFunction(
    () =>
      (document.getElementById('demoType') as HTMLSelectElement).disabled ||
      [...document.querySelectorAll('button')].some((b) => b.textContent === 'Full Search'),
    null,
    { timeout: 5_000 },
  );
  if (sliding && (await fullSearch.isVisible())) {
    await fullSearch.click();
  }
  await page.waitForFunction(
    () => (document.getElementById('demoType') as HTMLSelectElement).disabled,
    null,
    { timeout: 5_000 },
  );
  await page.waitForFunction(
    () => !(document.getElementById('demoType') as HTMLSelectElement).disabled,
    null,
    { timeout: 15_000 },
  );
  await page.waitForSelector('#slvList .lrow[data-i="0"]', { timeout: 15_000 });
  if (sliding || type === 'stacking') {
    await page.keyboard.press('Control+4');
    await page.waitForSelector('#stage[data-tab="play"][data-busy="0"]');
    await page.waitForSelector(
      sliding ? '#playBoard:not([hidden]) > *' : '#stackBoard:not([hidden]) > *',
      { timeout: 15_000 },
    );
    // the standard sliding example's board without its goal's shadows and
    // lines, grid and labels; the nested one's with them
    if (sliding) {
      const plain = type === 'sliding-standard';
      for (const c of ['noshades', 'nolines', 'nogrid', 'nolabels']) {
        const on = await page.$eval('#playBoard', (b, k) => b.classList.contains(k), c);
        if (on !== plain) {
          throw new Error(`the ${type} example's board ${on ? 'has' : 'lacks'} ${c}`);
        }
      }
    }
    await page.screenshot({ path: resolve(WEB, `test-results/demo-${type}.png`) });
  }
}

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
    // Solve plays a run of about three seconds, then the solution is back
    await page.click('#leftS [data-r="start"]');
    await page.waitForFunction(
      () =>
        (document.getElementById('stText')?.textContent ?? '').includes('Activity: Search active'),
      null,
      { timeout: 5_000 },
    );
    await page.waitForSelector('#splayer[data-ready="1"]', { timeout: 15_000 });
    await page.keyboard.press('Control+2');
    await page.waitForSelector('#stage[data-tab="puzzle"][data-busy="0"]');
    // an edit: the third shape's first cells in the voxel editor (as in the
    // app, a changed piece drops its problem's solutions)
    await page.keyboard.press('Control+1');
    await page.waitForSelector('#stage[data-tab="entities"][data-busy="0"]');
    await page.locator('#shapeList .row').nth(2).locator('.chip').click();
    const before = await page.locator('#stText').textContent();
    for (let i = 0; i < 6; i++) {
      await page.locator('svg .cell').nth(i).click();
    }
    await page.waitForFunction((b) => document.getElementById('stText')?.textContent !== b, before);
    mkdirSync(resolve(WEB, 'test-results'), { recursive: true });
    await page.screenshot({ path: resolve(WEB, 'test-results/demo.png') });
    // the theme list: the setting and the URL follow it
    await page.selectOption('#demoTheme', 'Dark');
    await page.waitForSelector('html[data-theme="dark"]');
    await page.waitForFunction(() => window.location.search.includes('theme=dark'));
    // every puzzle type's example: opened from the list, its Solve played
    // to its saved solution; Sliding and Stacking are played in Play too
    const types = await page.$$eval('#demoType option', (os) =>
      os.map((o) => (o as HTMLOptionElement).value),
    );
    for (const type of types.slice(1)) {
      console.log(`the ${type} example`);
      await examplePlays(page, type).catch(async (e: unknown) => {
        await page.screenshot({ path: resolve(WEB, `test-results/demo-${type}-failed.png`) });
        const status = await page.locator('#stText').textContent();
        throw new Error(`the ${type} example (status: ${status ?? ''}): ${String(e)}`);
      });
    }
    // Reset (a reload) keeps the type and the theme
    const last = types.at(-1) ?? '';
    await page.reload();
    await page.waitForFunction(
      (t) => (document.getElementById('demoType') as HTMLSelectElement | null)?.value === t,
      last,
      { timeout: 60_000 },
    );
    await page.waitForSelector('html[data-theme="dark"]');
    // and the tab (the last example's Solve was played in the Solver)
    await page.waitForSelector('#stage[data-tab="solver"]');
    // a tab asked for in the URL, and a key an older demo used
    await page.goto(`${ORIGIN}${DEMO_BASE}?type=sliding&tab=play`);
    await page.waitForSelector('#stage[data-tab="play"]', { timeout: 60_000 });
    await page.waitForFunction(
      () =>
        window.location.search.includes('type=sliding-nested') &&
        window.location.search.includes('tab=play'),
    );
    await page.keyboard.press('Control+2');
    await page.waitForFunction(() => window.location.search.includes('tab=puzzle'));
    if (errors.length > 0) {
      throw new Error(`the page reported errors:\n${errors.join('\n')}`);
    }
    console.log(
      'the demo opens its puzzle, takes an edit, plays the solution; every puzzle type opens and plays',
    );
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
