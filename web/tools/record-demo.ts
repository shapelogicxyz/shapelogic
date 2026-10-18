// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds the website's demo (demo.vite.config.ts, into site/public/demo/)
// and records its session.json: the page is opened in Chrome with the
// real engine behind its bridge (src/demo/live-bridge.ts, through a
// Playwright binding, as Electron's main process serves a window:
// main/engine-ipc.ts), driven through the demo puzzle (every shape in
// every style, the tabs, the solution's disassembly played to its end),
// and every distinct call is kept with its reply and the events after it.
// The demo page answers from that file (src/demo/replay-bridge.ts).
//
//   node tools/record-demo.ts [--headed] [--check]     (npm run demo)
//
// --check replays the built demo instead (no engine) through the same
// tour and lists the calls the recording does not have.
//
// Needs the release engine (just build release) or BT_ENGINE, and Google
// Chrome (Playwright's `chrome` channel). The engine runs from the
// repository's root with a neutral home, so no path of this machine is
// recorded.

import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { extname, resolve } from 'node:path';

import { chromium } from '@playwright/test';
import type { Page } from '@playwright/test';
import { build } from 'vite';

import { DEMO_BASE, DEMO_OUT } from '../demo.vite.config.ts';
import { callKey, canonical } from '../src/demo/session.ts';
import type { DemoSession, SessionCall, SessionEvent, WireMessage } from '../src/demo/session.ts';
import type { BridgeCall, BridgeMessage, BridgeRequest } from '../src/engine/bridge.ts';
import type { EngineStatus } from '../src/engine/client-types.ts';
import { NodeTransport } from '../src/engine/node-transport.ts';
import { toBase64 } from '../src/engine/recorder.ts';
import { EngineHost } from '../src/main/engine-host.ts';
import { serveEngine } from '../src/main/engine-ipc.ts';

const WEB = resolve(import.meta.dirname, '..');
const ROOT = resolve(WEB, '..');
/** The demo's puzzle, as the engine is given it (from the repository's root). */
const PUZZLE = 'site/public/examples/PelikanBurr.xmpuzzle';
const PUZZLE_NAME = 'PelikanBurr.xmpuzzle';
const ORIGIN = 'http://demo.invalid';
const STYLES = ['drawing', 'flat', 'wireframe', 'stl'] as const;

const TYPES: Record<string, string> = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.png': 'image/png',
  '.json': 'application/json',
};

type Logged = { call: BridgeCall } | { message: WireMessage };

function wire(m: BridgeMessage): WireMessage {
  if (m.kind !== 'reply' || m.frame === undefined) {
    return m as WireMessage;
  }
  return {
    ...m,
    frame: { desc: m.frame.desc, data: toBase64(new Uint8Array(m.frame.data)) },
  };
}

// a reply without what differs between two runs of the same call (the frame's number)
function replyKey(c: SessionCall): string {
  const r = c.reply;
  const same =
    r.kind === 'reply' && r.frame !== undefined
      ? { ...r, frame: { ...r.frame, desc: { ...r.frame.desc, frame: 0 } } }
      : r;
  return canonical({ r: same, e: c.events });
}

/** The log as the session file: each distinct call with its reply, in first-made order. */
function session(log: Logged[], status: EngineStatus): DemoSession {
  const calls = new Map<number, BridgeCall>();
  const out: SessionCall[] = [];
  const seen = new Set<string>();
  const before: SessionEvent[] = [];
  const frames: string[] = [];
  const frameIndex = new Map<string, number>();
  const frameOf = (data: string): number => {
    let i = frameIndex.get(data);
    if (i === undefined) {
      i = frames.length;
      frames.push(data);
      frameIndex.set(data, i);
    }
    return i;
  };
  let last: SessionCall | null = null;
  for (const l of log) {
    if ('call' in l) {
      calls.set(l.call.id, l.call);
      continue;
    }
    const m = l.message;
    if (m.kind === 'event') {
      (last?.events ?? before).push({ event: m.event, params: m.params });
      continue;
    }
    if (m.kind === 'status') {
      continue;
    }
    const call = calls.get(m.id);
    if (call === undefined) {
      continue;
    }
    last = {
      method: call.method,
      ...(call.params === undefined ? {} : { params: call.params }),
      reply:
        m.kind === 'error'
          ? { kind: 'error', code: m.code, errc: m.errc, message: m.message }
          : {
              kind: 'reply',
              result: m.result,
              ...(m.frame === undefined
                ? {}
                : { frame: { desc: m.frame.desc, data: frameOf(m.frame.data) } }),
            },
      events: [],
    };
    out.push(last);
  }
  const calls_ = out.filter((c) => {
    const k = `${callKey(c.method, c.params)} ${replyKey(c)}`;
    if (seen.has(k)) {
      return false;
    }
    seen.add(k);
    return true;
  });
  return {
    format: 'shapelogic-demo-session',
    version: 1,
    status,
    puzzle: PUZZLE,
    events: before,
    calls: calls_,
    frames,
  };
}

async function frames(page: Page): Promise<void> {
  await page.evaluate(
    () =>
      new Promise<void>((done) => {
        requestAnimationFrame(() => {
          requestAnimationFrame(() => {
            done();
          });
        });
      }),
  );
}

async function settle(page: Page): Promise<void> {
  await frames(page);
  await page.waitForSelector('#vp[data-moving="0"]');
  await frames(page);
}

async function shownStyle(page: Page, style: string): Promise<void> {
  await page.click('#bDisp');
  await page.click(`#pop [data-k="${style}"]`);
  await page.keyboard.press('Escape');
  await page.waitForFunction(
    (s) => (document.getElementById('vp')?.dataset.shown ?? '').endsWith(`:${s}`),
    style,
  );
  await settle(page);
}

async function toTab(page: Page, key: string, tab: string): Promise<void> {
  await page.keyboard.press(key);
  await page.waitForSelector(`#stage[data-tab="${tab}"][data-busy="0"]`);
  await settle(page);
}

/** What the demo lets a visitor do, so that its calls are in the recording. */
async function tour(page: Page): Promise<void> {
  await page.waitForFunction(
    (name) => document.getElementById('fname')?.textContent === name,
    PUZZLE_NAME,
  );
  await page.waitForSelector('#shapeList .row.sel');
  await settle(page);
  const ids = await page.$$eval('#shapeList .row', (rows) =>
    rows.map((r) => (r as HTMLElement).dataset.id ?? ''),
  );
  for (const style of STYLES) {
    await shownStyle(page, style);
    for (const id of ids) {
      await page.click(`#shapeList .row[data-id="${id}"] .chip`);
      await page.waitForFunction(
        (id) => (document.getElementById('vp')?.dataset.shown ?? '').startsWith(`${id}:`),
        id,
      );
      await settle(page);
    }
  }
  await shownStyle(page, 'drawing');
  await page.click(`#shapeList .row[data-id="${ids[0] ?? ''}"] .chip`);
  await settle(page);

  await toTab(page, 'Control+2', 'puzzle');
  for (const style of STYLES) {
    await shownStyle(page, style);
  }
  await shownStyle(page, 'drawing');
  await toTab(page, 'Control+3', 'solver');
  await page.click('#slvList .lrow[data-i="0"]');
  await page.waitForSelector('#splayer[data-ready="1"]');
  await settle(page);
  for (const style of STYLES) {
    await shownStyle(page, style);
  }
  await shownStyle(page, 'drawing');
  const steps = Number(await page.getAttribute('#splayer', 'data-steps'));
  for (let i = 0; i < steps; i++) {
    await page.click('#splayer [data-t="fwd"]');
  }
  await page.waitForSelector(`#splayer[data-t="${steps.toFixed(3)}"]`);
  await page.click('#splayer [data-t="start"]');
  await page.waitForSelector('#splayer[data-t="0.000"]');
  await settle(page);
  await toTab(page, 'Control+1', 'entities');
}

async function serveDemo(page: Page): Promise<void> {
  await page.route(`${ORIGIN}/**`, async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (!path.startsWith(DEMO_BASE)) {
      await route.fulfill({ status: 404 });
      return;
    }
    const rel = path.slice(DEMO_BASE.length) || 'index.html';
    const file = resolve(DEMO_OUT, rel);
    if (!file.startsWith(DEMO_OUT) || !existsSync(file)) {
      await route.fulfill({ status: 404 });
      return;
    }
    await route.fulfill({
      body: readFileSync(file),
      contentType: TYPES[extname(file)] ?? 'application/octet-stream',
    });
  });
}

async function record(page: Page, engine: string): Promise<DemoSession> {
  const host = new EngineHost({ start: () => NodeTransport.start(engine, { cwd: ROOT }) });
  await host.start();
  const status = host.status;
  if (status.state !== 'running') {
    throw new Error(`the engine did not start: ${JSON.stringify(status)}`);
  }
  const log: Logged[] = [];
  // messages reach the page in order, one evaluate after another
  let delivering = Promise.resolve();
  const served: { server: ReturnType<typeof serveEngine> | null } = { server: null };
  const post = (m: BridgeMessage): void => {
    const w = wire(m);
    log.push({ message: w });
    delivering = delivering.then(async () => {
      await page
        .evaluate((m) => {
          (window as { __btDemoDeliver?: (m: unknown) => void }).__btDemoDeliver?.(m);
        }, w)
        .catch(() => undefined);
    });
  };
  await page.exposeBinding('__btDemoSend', (_source, request: BridgeRequest) => {
    served.server ??= serveEngine(host, post);
    if (request.kind === 'call') {
      log.push({ call: request });
    }
    served.server.receive(request);
  });
  await page.addInitScript(
    (demo) => {
      (window as { __btDemo?: unknown }).__btDemo = demo;
    },
    { puzzle: PUZZLE, version: status.version },
  );
  await page.goto(`${ORIGIN}${DEMO_BASE}`);
  await tour(page);
  await delivering;
  served.server?.dispose();
  await host.stop();
  return session(log, status);
}

async function main(): Promise<void> {
  const args = new Set(process.argv.slice(2));
  const check = args.has('--check');
  const engine = process.env.BT_ENGINE ?? resolve(ROOT, 'build/release/engine/shapelogic_engine');
  // the engine's default folders (settings.get) are in the user's cache folder
  process.env.HOME = '/home/user';
  process.env.XDG_CACHE_HOME = '';
  if (!check) {
    if (!existsSync(engine)) {
      throw new Error(`no engine at ${engine}: run \`just build release\` or set BT_ENGINE`);
    }
    await build({ configFile: resolve(WEB, 'demo.vite.config.ts'), logLevel: 'warn' });
  }
  const browser = await chromium.launch({ channel: 'chrome', headless: !args.has('--headed') });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    const errors: string[] = [];
    page.on('pageerror', (e) => errors.push(e.stack ?? e.message));
    await serveDemo(page);
    if (check) {
      const misses: string[] = [];
      page.on('console', (m) => {
        if (m.text().startsWith('demo: not in the recording')) {
          misses.push(m.text());
        }
      });
      await page.goto(`${ORIGIN}${DEMO_BASE}`);
      await tour(page);
      await page.screenshot({ path: resolve(WEB, 'test-results/demo.png') });
      console.log(misses.length === 0 ? 'every call was recorded' : misses.join('\n'));
    } else {
      const s = await record(page, engine);
      const json = JSON.stringify(s);
      if (json.includes(ROOT) || json.includes(process.cwd())) {
        throw new Error('the recording names a path of this machine');
      }
      writeFileSync(resolve(DEMO_OUT, 'session.json'), json);
      console.log(
        `recorded ${String(s.calls.length)} calls, ${String(Math.round(json.length / 1024))} KB: ${resolve(DEMO_OUT, 'session.json')}`,
      );
    }
    if (errors.length > 0) {
      throw new Error(`the page threw:\n${errors.join('\n')}`);
    }
  } finally {
    await browser.close();
  }
}

await main();
