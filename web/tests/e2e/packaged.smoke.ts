// SPDX-License-Identifier: GPL-3.0-or-later
//
// The smoke test of the packaged app (stage 8, `just web-smoke` after
// `just web-dist --dir`): the app electron-builder made, as a user starts
// it, with a fixture on its command line. Its fuses turn --inspect off, so
// Playwright's Electron driver cannot reach its main process: the test
// drives the page over Chromium's DevTools protocol (--remote-debugging-port)
// instead. Checked: the app is the build's (its fuses, its app.asar holding
// only the bundles), its engine the one in its resources with the project's
// version, the environment's development switches ignored (another engine,
// another page, Node's options and ELECTRON_RUN_AS_NODE all set), the page
// on the app's own scheme, sandboxed and isolated with its CSP (a script
// or image from a file: URL is refused by the CSP, before the request
// filter), no file outside the renderer read, a path no dialog gave
// refused, and the fixture opened and solved with bt's counts.
// BT_PACKAGED_APP names another executable (an installed app).

import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

import { listPackage } from '@electron/asar';
import { FuseV1Options, getCurrentFuseWire } from '@electron/fuses';
import { chromium, expect, test } from '@playwright/test';
import type { Browser, Page } from '@playwright/test';

import { projectVersion } from '../../tools/project-version.ts';
import { engineCall, FIXTURES, WEB } from './app.ts';
import { btJson, runButton, runEnded, toSolver } from './solver.ts';

/** The packaged app's executable and its resources directory, for this platform. */
function packaged(): { exe: string; resources: string } {
  const fromEnv = process.env.BT_PACKAGED_APP;
  if (fromEnv !== undefined && fromEnv !== '') {
    const exe = resolve(fromEnv);
    return {
      exe,
      resources:
        process.platform === 'darwin'
          ? resolve(dirname(exe), '../Resources')
          : join(dirname(exe), 'resources'),
    };
  }
  const dist = resolve(WEB, 'dist');
  if (process.platform === 'darwin') {
    const app = join(
      dist,
      process.arch === 'arm64' ? 'mac-arm64' : 'mac',
      'ShapeLogic.app',
      'Contents',
    );
    return { exe: join(app, 'MacOS', 'ShapeLogic'), resources: join(app, 'Resources') };
  }
  if (process.platform === 'win32') {
    const dir = join(dist, 'win-unpacked');
    return { exe: join(dir, 'ShapeLogic.exe'), resources: join(dir, 'resources') };
  }
  const dir = join(dist, 'linux-unpacked');
  return { exe: join(dir, 'shapelogic'), resources: join(dir, 'resources') };
}

/** A fuse's states in the wire: the characters '0' and '1' (@electron/fuses's FuseState, not exported). */
const DISABLE = 48;
const ENABLE = 49;

const APP = packaged();
const ENGINE = join(APP.resources, process.platform === 'win32' ? 'bt-engine.exe' : 'bt-engine');
const VERSION = projectVersion();

test.skip(!existsSync(APP.exe), `no packaged app at ${APP.exe}: run \`just web-dist --dir\``);

/** Starts the packaged app with these arguments; resolves with its DevTools address. */
function start(
  args: string[],
  env: Record<string, string>,
): Promise<{ child: ChildProcess; ws: string }> {
  return new Promise((done, fail) => {
    const child = spawn(APP.exe, ['--remote-debugging-port=0', ...args], {
      env,
      stdio: ['ignore', 'ignore', 'pipe'],
    });
    let err = '';
    const timer = setTimeout(() => {
      fail(new Error(`the app did not open its DevTools port: ${err.slice(-2000)}`));
    }, 30_000);
    child.stderr.on('data', (chunk: Buffer) => {
      err += chunk.toString();
      const m = /DevTools listening on (ws:\/\/\S+)/.exec(err);
      if (m?.[1] !== undefined) {
        clearTimeout(timer);
        done({ child, ws: m[1] });
      }
    });
    child.on('exit', (code) => {
      clearTimeout(timer);
      fail(new Error(`the app ended (${String(code)}): ${err.slice(-2000)}`));
    });
  });
}

/** The app's window's page, once it has loaded. */
async function windowPage(browser: Browser): Promise<Page> {
  let page: Page | undefined;
  await expect
    .poll(
      () => {
        page = browser
          .contexts()
          .flatMap((c) => c.pages())
          .find((p) => p.url().includes('index.html'));
        return page !== undefined;
      },
      { timeout: 30_000 },
    )
    .toBe(true);
  if (page === undefined) {
    throw new Error('no window');
  }
  return page;
}

/** The command lines of the processes whose parent is pid (macOS and Linux). */
function children(pid: number): string[] {
  const out = execFileSync('ps', ['-A', '-o', 'ppid=,command='], { encoding: 'utf8' });
  return out
    .split('\n')
    .map((l) => /^\s*(\d+)\s+(.*)$/.exec(l))
    .filter((m): m is RegExpExecArray => m?.[1] === String(pid))
    .map((m) => m[2] ?? '');
}

test('the packaged app: its fuses, its engine, its page, a fixture solved', async () => {
  test.setTimeout(180_000);

  // the build: Electron's fuses as tools/dist.ts set them, the app's archive the bundles only
  const fuses = await getCurrentFuseWire(
    process.platform === 'darwin' ? resolve(APP.exe, '../../..') : APP.exe,
  );
  expect(fuses[FuseV1Options.RunAsNode]).toBe(DISABLE);
  expect(fuses[FuseV1Options.EnableNodeOptionsEnvironmentVariable]).toBe(DISABLE);
  expect(fuses[FuseV1Options.EnableNodeCliInspectArguments]).toBe(DISABLE);
  expect(fuses[FuseV1Options.EnableEmbeddedAsarIntegrityValidation]).toBe(ENABLE);
  expect(fuses[FuseV1Options.OnlyLoadAppFromAsar]).toBe(ENABLE);
  expect(fuses[FuseV1Options.GrantFileProtocolExtraPrivileges]).toBe(DISABLE);
  const files = listPackage(join(APP.resources, 'app.asar'), { isPack: false }).map((f) =>
    f.replaceAll('\\', '/'),
  );
  expect(files).toContain('/package.json');
  expect(files).toContain('/out/main/index.js');
  expect(files).toContain('/out/preload/index.cjs');
  expect(files).toContain('/out/renderer/index.html');
  expect(
    files.filter((f) => !/^\/(out|out\/(main|preload|renderer)(\/.*)?|package\.json)$/.test(f)),
  ).toEqual([]);
  expect(files.filter((f) => f.endsWith('.map'))).toEqual([]);

  // the engine in the resources: the project's version
  expect(execFileSync(ENGINE, ['--version'], { encoding: 'utf8' }).trim()).toBe(
    `bt-engine ${VERSION}`,
  );

  // started as a user would, with a fixture, and every development switch
  // and Node override set: a packaged app takes none of them
  const userData = mkdtempSync(join(tmpdir(), 'bt-smoke-data-'));
  const log = join(userData, 'calls.log');
  const env: Record<string, string> = {};
  for (const [k, v] of Object.entries(process.env)) {
    if (v !== undefined) {
      env[k] = v;
    }
  }
  Object.assign(env, {
    BT_USER_DATA: userData,
    BT_ENGINE: process.platform === 'win32' ? 'C:\\Windows\\System32\\where.exe' : '/usr/bin/false',
    BT_CALL_LOG: log,
    ELECTRON_RENDERER_URL: 'http://127.0.0.1:9/',
    ELECTRON_RUN_AS_NODE: '1',
    NODE_OPTIONS: '--require ./no-such-module.cjs',
  });
  const fixture = resolve(FIXTURES, 'Bermuda.xmpuzzle');
  const { child, ws } = await start([fixture], env);
  const browser = await chromium.connectOverCDP(ws);
  try {
    const page = await windowPage(browser);
    // the app's own page on its own scheme, not the dev server's
    expect(page.url()).toMatch(/^bt-app:\/\/app\/index\.html/);
    await expect(page.locator('#stage')).toHaveAttribute('data-engine', 'running', {
      timeout: 30_000,
    });

    // the engine the app runs is the one in its resources, not BT_ENGINE
    if (process.platform !== 'win32' && child.pid !== undefined) {
      const engines = children(child.pid).filter((c) => c.includes('bt-engine'));
      expect(engines).toEqual([ENGINE]);
    }

    // the page: sandboxed and isolated, the bridges only, its CSP, its version
    const page_ = await page.evaluate(() => ({
      require: typeof (globalThis as Record<string, unknown>).require,
      process: typeof (globalThis as Record<string, unknown>).process,
      bt: Object.keys(window.bt).sort(),
      version: window.btApp.version,
      csp: document
        .querySelector('meta[http-equiv="Content-Security-Policy"]')
        ?.getAttribute('content'),
    }));
    expect(page_).toEqual({
      require: 'undefined',
      process: 'undefined',
      bt: ['onMessage', 'send'],
      version: VERSION,
      csp: "default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'none'; worker-src 'none'; frame-src 'none'; object-src 'none'; media-src 'none'; base-uri 'none'; form-action 'none'",
    });

    // a script or an image from a file: URL is refused by Chromium itself: a
    // page on the app's scheme may not load local files at all (on a file:
    // page the CSP's 'self' matched every file: URL and only the request
    // filter stood in the way); the filter is the second check, not the only one
    const probe = join(userData, 'probe.js');
    writeFileSync(probe, 'window.__probeRan = 1;\n');
    const consoleErrors: string[] = [];
    page.on('console', (m) => {
      if (m.type() === 'error') {
        consoleErrors.push(m.text());
      }
    });
    const loads = await page.evaluate(async (probeUrl) => {
      const violations: string[] = [];
      document.addEventListener('securitypolicyviolation', (e) => {
        violations.push(`${e.violatedDirective} ${e.blockedURI}`);
      });
      const script = document.createElement('script');
      script.src = probeUrl;
      const scriptEnd = new Promise<string>((done) => {
        script.addEventListener('load', () => {
          done('loaded');
        });
        script.addEventListener('error', () => {
          done('refused');
        });
        setTimeout(() => {
          done('no event');
        }, 10_000);
      });
      const image = document.createElement('img');
      image.src = 'file:///etc/hosts';
      document.body.append(script, image);
      const scriptResult = await scriptEnd;
      await new Promise((done) => setTimeout(done, 500));
      return {
        script: scriptResult,
        imageDrawn: image.naturalWidth > 0,
        ran: (window as unknown as Record<string, unknown>).__probeRan,
        violations,
      };
    }, pathToFileURL(probe).href);
    expect(loads).toEqual({ script: 'refused', imageDrawn: false, ran: undefined, violations: [] });
    expect(consoleErrors).toContain(
      `Not allowed to load local resource: ${pathToFileURL(probe).href}`,
    );
    expect(consoleErrors).toContain('Not allowed to load local resource: file:///etc/hosts');

    // the fixture given on the command line is open; a path no dialog gave is refused
    await expect(page.locator('#fname')).toHaveText('Bermuda.xmpuzzle', { timeout: 30_000 });
    await expect(
      engineCall(page, 'document.save', { path: join(userData, 'elsewhere.xmpuzzle') }),
    ).rejects.toThrow(/invalid_argument: elsewhere\.xmpuzzle was not chosen/);
    expect(existsSync(join(userData, 'elsewhere.xmpuzzle'))).toBe(false);

    // solved: the counts bt gives
    await toSolver(page);
    await page.click('#slvMode [data-m="dis"]');
    await expect(page.locator('#slvMode .mcard[data-m="dis"]')).toHaveClass(/on/);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    const bt = btJson(fixture);
    await expect(page.locator('#slvAsm')).toHaveText(String(bt.assemblies));
    await expect(page.locator('#slvSols')).toHaveText(String(bt.solutions));
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');

    // BT_CALL_LOG was not taken
    expect(existsSync(log)).toBe(false);
  } finally {
    await browser.close().catch(() => undefined);
    // the app and its engine gone before their data directory goes
    const exited = new Promise((done) => child.once('exit', done));
    child.kill('SIGKILL');
    await exited;
    rmSync(userData, { recursive: true, force: true, maxRetries: 5 });
  }
});
