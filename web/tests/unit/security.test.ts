// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Electron security checklist (stage 8), over what main/index.ts
// applies: the window's options (sandboxed, context-isolated, no Node, no
// webview, no navigation by a drop), the development switches a packaged
// app ignores, the requests its session lets through (the renderer's files
// only, nothing remote), the permissions (none), the CSP of the page as
// written and as built, the only addresses handed to the system's browser,
// the bridges the preload exposes; the files a window's engine may be given
// (only paths the main process handed it: its dialogs, a drop, a file
// opened with the app, the rescue record) and the exports' write grants;
// and the IPC handlers, with Electron replaced by a stand-in that records
// them: each answers only the app's windows and grants only what its
// dialog gave.

import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { BridgeMessage } from '../../src/engine/bridge.ts';
import { serveEngine } from '../../src/main/engine-ipc.ts';
import { callPath, PATH_METHODS, PathGrants } from '../../src/main/path-grants.ts';
import {
  allowedRequest,
  HELP_URLS,
  permissionAllowed,
  switches,
  webPreferences,
  windowOptions,
} from '../../src/main/security.ts';

const WEB = resolve(import.meta.dirname, '../..');
const ROOT = resolve(WEB, '..');

/** Every .ts file under a directory of web/src, with its text. */
function sources(dir: string): { file: string; text: string }[] {
  const out: { file: string; text: string }[] = [];
  const walk = (d: string): void => {
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const p = join(d, e.name);
      if (e.isDirectory()) {
        walk(p);
      } else if (e.name.endsWith('.ts')) {
        out.push({
          file: p.slice(WEB.length + 1).replaceAll('\\', '/'),
          text: readFileSync(p, 'utf8'),
        });
      }
    }
  };
  walk(resolve(WEB, dir));
  return out;
}

const SRC = sources('src');
const occurrences = (needle: RegExp): string[] =>
  SRC.flatMap(({ file, text }) => [...text.matchAll(needle)].map(() => file));

describe('the window', () => {
  it('is sandboxed and isolated, without Node, webviews or navigation by a drop', () => {
    const p = webPreferences('/app/out/preload/index.cjs');
    expect(p).toMatchObject({
      preload: '/app/out/preload/index.cjs',
      sandbox: true,
      contextIsolation: true,
      nodeIntegration: false,
      nodeIntegrationInWorker: false,
      nodeIntegrationInSubFrames: false,
      webSecurity: true,
      allowRunningInsecureContent: false,
      experimentalFeatures: false,
      webviewTag: false,
      navigateOnDragDrop: false,
    });
    const w = windowOptions('/p.cjs', 'BurrTools Alpha', { width: 10, height: 20, x: 1, y: 2 });
    expect(w).toMatchObject({ width: 10, height: 20, x: 1, y: 2, show: false });
    expect(w.webPreferences).toEqual(webPreferences('/p.cjs'));
  });

  it('is made in one place, with those options, and nothing in src turns them off', () => {
    expect(occurrences(/new BrowserWindow\(/g)).toEqual(['src/main/index.ts']);
    const index = SRC.find((s) => s.file === 'src/main/index.ts')?.text ?? '';
    expect(index).toMatch(/new BrowserWindow\(\s*windowOptions\(/);
    for (const bad of [
      /nodeIntegration\w*:\s*true/g,
      /contextIsolation:\s*false/g,
      /sandbox:\s*false/g,
      /webSecurity:\s*false/g,
      /allowRunningInsecureContent:\s*true/g,
      /webviewTag:\s*true/g,
      /experimentalFeatures:\s*true/g,
    ]) {
      expect(occurrences(bad), String(bad)).toEqual([]);
    }
    // navigation, new windows and webviews refused; the session filtered; no permission
    expect(index).toContain("contents.on('will-navigate'");
    expect(index).toContain("setWindowOpenHandler(() => ({ action: 'deny' }))");
    expect(index).toContain("'will-attach-webview'");
    expect(index).toContain('webRequest.onBeforeRequest');
    expect(index).toContain('setPermissionRequestHandler');
    expect(index).toContain('setPermissionCheckHandler');
  });

  it('loads a page other than its own only from a dev server, never when packaged', () => {
    // the one loadURL is the dev server's, which only an unpackaged app takes
    expect(occurrences(/\.loadURL\(/g)).toEqual(['src/main/index.ts']);
    const index = SRC.find((s) => s.file === 'src/main/index.ts')?.text ?? '';
    expect(index).toContain('const devUrl = env.rendererUrl;');
    expect(occurrences(/process\.env\.ELECTRON_RENDERER_URL/g)).toEqual([]);
  });
});

describe('the switches a packaged app takes', () => {
  const env = {
    BT_ENGINE: '/tmp/other-engine',
    BT_USER_DATA: '/tmp/data',
    BT_CALL_LOG: '/tmp/calls.log',
    BT_AUTOSAVE_SECONDS: '3',
    ELECTRON_RENDERER_URL: 'http://localhost:5173/',
  };

  it('only BT_USER_DATA when packaged', () => {
    expect(switches(true, env)).toEqual({ userData: '/tmp/data' });
    expect(switches(true, {})).toEqual({});
  });

  it('all of them in development; empty ones as absent', () => {
    expect(switches(false, env)).toEqual({
      engine: '/tmp/other-engine',
      userData: '/tmp/data',
      callLog: '/tmp/calls.log',
      autosave: '3',
      rendererUrl: 'http://localhost:5173/',
    });
    expect(switches(false, { BT_ENGINE: '', BT_CALL_LOG: '' })).toEqual({});
  });

  it('are read through switches() only', () => {
    for (const name of ['BT_ENGINE', 'BT_USER_DATA', 'BT_CALL_LOG', 'BT_AUTOSAVE_SECONDS']) {
      expect(occurrences(new RegExp(`process\\.env\\.${name}\\b`, 'g')), name).toEqual([]);
    }
  });
});

describe('the requests the session lets through', () => {
  const rendererDir = resolve('/app/out/renderer');
  const inside = (p: string): string => pathToFileURL(join(rendererDir, p)).href;
  const built = { rendererDir };
  const dev = { rendererDir, rendererUrl: 'http://localhost:5173/' };

  it('the renderer files, devtools, data and blob URLs', () => {
    expect(allowedRequest(inside('index.html'), built)).toBe(true);
    expect(allowedRequest(inside('assets/index-x.js'), built)).toBe(true);
    expect(allowedRequest(`${inside('index.html')}?autosave=3`, built)).toBe(true);
    expect(allowedRequest('devtools://devtools/bundled/devtools_app.html', built)).toBe(true);
    expect(allowedRequest('data:image/png;base64,AAAA', built)).toBe(true);
    expect(allowedRequest('blob:file:///0b6c1d2e', built)).toBe(true);
  });

  it('no other file, however named', () => {
    expect(allowedRequest('file:///etc/hosts', built)).toBe(false);
    expect(allowedRequest(pathToFileURL(resolve('/app/out/main/index.js')).href, built)).toBe(
      false,
    );
    expect(allowedRequest(pathToFileURL(rendererDir).href, built)).toBe(false);
    expect(allowedRequest(`${pathToFileURL(rendererDir).href}/../main/index.js`, built)).toBe(
      false,
    );
    expect(allowedRequest(`${pathToFileURL(rendererDir).href}/%2e%2e/main/index.js`, built)).toBe(
      false,
    );
    expect(allowedRequest(pathToFileURL(`${rendererDir}-other/x.js`).href, built)).toBe(false);
    expect(allowedRequest('file://server/share/x.js', built)).toBe(false);
  });

  it('nothing remote; the dev server only in development, on its own host and port', () => {
    for (const url of [
      'https://example.com/',
      'http://localhost:5173/',
      'ws://localhost:5173/',
      'https://burrtools.sourceforge.net/gui-doc/toc.html',
      'not a url',
      'chrome-extension://abc/x.js',
    ]) {
      expect(allowedRequest(url, built), url).toBe(false);
    }
    expect(allowedRequest('http://localhost:5173/src/main.ts', dev)).toBe(true);
    expect(allowedRequest('ws://localhost:5173/', dev)).toBe(true);
    expect(allowedRequest('http://localhost:5174/', dev)).toBe(false);
    expect(allowedRequest('https://localhost:5173/', dev)).toBe(false);
    expect(allowedRequest('http://example.com:5173/', dev)).toBe(false);
  });

  it('no permission granted', () => {
    for (const p of [
      'media',
      'geolocation',
      'notifications',
      'clipboard-read',
      'fullscreen',
      'openExternal',
      'fileSystem',
    ]) {
      expect(permissionAllowed(p), p).toBe(false);
    }
  });
});

describe('the page', () => {
  const CSP =
    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:";
  const cspOf = (html: string): string[] =>
    [...html.matchAll(/<meta\s+http-equiv="Content-Security-Policy"\s+content="([^"]*)"/g)].map(
      (m) => m[1] ?? '',
    );

  it('has one CSP, without eval or inline scripts, as written and as built', () => {
    const src = readFileSync(resolve(WEB, 'src/renderer/index.html'), 'utf8');
    expect(cspOf(src)).toEqual([CSP]);
    const built = resolve(WEB, 'out/renderer/index.html');
    if (existsSync(built)) {
      const html = readFileSync(built, 'utf8');
      expect(cspOf(html)).toEqual([CSP]);
      // its scripts and styles are the bundle's own files
      for (const m of html.matchAll(/\s(?:src|href)="([^"]*)"/g)) {
        expect(m[1], m[0]).toMatch(/^\.\/[\w./-]+$/);
      }
      expect(html).not.toMatch(/<script(?![^>]*\ssrc=)[^>]*>/);
    }
  });

  it('names nothing remote', () => {
    const renderer = sources('src/renderer').map((s) => s.text);
    const html = readFileSync(resolve(WEB, 'src/renderer/index.html'), 'utf8');
    const css = readFileSync(resolve(WEB, 'src/renderer/styles.css'), 'utf8');
    for (const text of [...renderer, html, css]) {
      const urls = [...text.matchAll(/https?:\/\/[^\s'"`)]+/g)].map((m) => m[0]);
      // only XML namespaces, which nothing loads
      expect(urls.filter((u) => !u.startsWith('http://www.w3.org/'))).toEqual([]);
    }
    expect(renderer.some((t) => /\bfetch\(|XMLHttpRequest|WebSocket\(/.test(t))).toBe(false);
  });

  it('gets window.bt and window.btApp from the preload, and nothing else', () => {
    expect(occurrences(/exposeInMainWorld\(/g)).toEqual([
      'src/preload/index.ts',
      'src/preload/index.ts',
    ]);
    const preload = SRC.find((s) => s.file === 'src/preload/index.ts')?.text ?? '';
    expect([...preload.matchAll(/exposeInMainWorld\('(\w+)'/g)].map((m) => m[1])).toEqual([
      'bt',
      'btApp',
    ]);
  });
});

describe('the addresses opened outside the app', () => {
  it('are the help links, https on the original guide, opened in one place', () => {
    for (const url of Object.values(HELP_URLS)) {
      const u = new URL(url);
      expect(u.protocol).toBe('https:');
      expect(u.hostname).toBe('burrtools.sourceforge.net');
    }
    expect(occurrences(/openExternal\(/g)).toEqual(['src/main/app-ipc.ts']);
    const ipc = SRC.find((s) => s.file === 'src/main/app-ipc.ts')?.text ?? '';
    expect(ipc).toContain('shell.openExternal(HELP_URLS[link])');
  });
});

describe('the files a window’s engine may be given', () => {
  it('callPath finds the path of every schema method that takes one', () => {
    const withPath: string[] = [];
    for (const f of readdirSync(resolve(ROOT, 'api')).filter((n) => n.endsWith('.json'))) {
      const api = JSON.parse(readFileSync(resolve(ROOT, 'api', f), 'utf8')) as {
        methods?: Record<string, { params?: { properties?: Record<string, unknown> } }>;
      };
      for (const [name, m] of Object.entries(api.methods ?? {})) {
        if (m.params?.properties?.path !== undefined) {
          withPath.push(name);
        }
      }
    }
    expect([...PATH_METHODS].sort()).toEqual(withPath.sort());
    for (const method of PATH_METHODS) {
      expect(callPath(method, { path: '/x/y.xmpuzzle', problem: 'p1' })).toBe('/x/y.xmpuzzle');
      expect(callPath(method, {})).toBeNull();
    }
    expect(callPath('document.get', { path: '/x' })).toBeNull();
    expect(callPath('document.open', null)).toBeNull();
    expect(callPath('document.open', { path: 3 })).toBeNull();
  });

  it('only what that window was handed', () => {
    const g = new PathGrants<object>();
    const a = {};
    const b = {};
    g.grant(a, '/docs/a.xmpuzzle');
    g.grant(a, '');
    expect(g.allows(a, '/docs/a.xmpuzzle')).toBe(true);
    expect(g.allows(b, '/docs/a.xmpuzzle')).toBe(false);
    expect(g.allows(a, '')).toBe(false);
    expect(g.refusal(a, 'document.open', { path: '/docs/a.xmpuzzle' })).toBeNull();
    // kept: a puzzle is opened, saved and opened again
    expect(g.refusal(a, 'document.save', { path: '/docs/a.xmpuzzle' })).toBeNull();
    expect(g.refusal(a, 'document.save', {})).toBeNull();
    expect(g.refusal(a, 'shape.list', {})).toBeNull();
    expect(g.refusal(a, 'document.save', { path: '/docs/../docs/a.xmpuzzle' })).toBe(
      'a.xmpuzzle was not chosen in a file dialog or dropped on the window',
    );
    expect(g.refusal(b, 'solve.exportPaused', { problem: 'p1', path: '/etc/x.btsolve' })).toBe(
      'x.btsolve was not chosen in a file dialog or dropped on the window',
    );
  });

  it('a call refused is answered invalid_argument and never reaches the engine', async () => {
    const calls: string[] = [];
    const host = {
      status: { state: 'running', restarts: 0 } as const,
      call: (method: string): Promise<unknown> => {
        calls.push(method);
        return Promise.resolve({});
      },
      onEvent: () => () => undefined,
      onStatus: () => () => undefined,
    };
    const sent: BridgeMessage[] = [];
    const logged: unknown[] = [];
    const server = serveEngine(
      host as unknown as Parameters<typeof serveEngine>[0],
      (m) => sent.push(m),
      (o) => logged.push(o),
      (method, params) =>
        callPath(method, params) === '/ok.xmpuzzle'
          ? null
          : callPath(method, params) === null
            ? null
            : 'no',
    );
    server.receive({
      kind: 'call',
      id: 1,
      method: 'document.open',
      params: { path: '/etc/passwd' },
    });
    server.receive({
      kind: 'call',
      id: 2,
      method: 'document.open',
      params: { path: '/ok.xmpuzzle' },
    });
    server.receive({ kind: 'call', id: 3, method: 'document.get' });
    await new Promise((r) => setTimeout(r, 0));
    expect(calls).toEqual(['document.open', 'document.get']);
    expect(sent.find((m) => m.kind === 'error')).toEqual({
      kind: 'error',
      id: 1,
      code: -32001,
      errc: 'invalid_argument',
      message: 'no',
    });
    expect(logged).toContainEqual({ id: 1, method: 'document.open', errc: 'invalid_argument' });
  });
});

// ---- the IPC handlers, with Electron's main-process modules stood in for ----

type Handler = (event: { sender: object; returnValue?: unknown }, ...args: unknown[]) => unknown;

const electron = vi.hoisted(() => ({
  on: new Map<string, Handler>(),
  handle: new Map<string, Handler>(),
  openPaths: [] as string[],
  savePath: '',
  opened: [] as string[],
}));

vi.mock('electron', () => ({
  app: { getPath: () => tmpdir() },
  ipcMain: {
    on: (channel: string, h: Handler) => electron.on.set(channel, h),
    handle: (channel: string, h: Handler) => electron.handle.set(channel, h),
  },
  dialog: {
    showOpenDialog: () =>
      Promise.resolve({ canceled: electron.openPaths.length === 0, filePaths: electron.openPaths }),
    showSaveDialog: () =>
      Promise.resolve({ canceled: electron.savePath === '', filePath: electron.savePath }),
    showMessageBox: () => Promise.resolve({ response: 1 }),
  },
  shell: {
    openExternal: (url: string) => {
      electron.opened.push(url);
      return Promise.resolve();
    },
  },
}));

describe('the IPC handlers', () => {
  let dir: string;
  const ours = { id: 1 };
  const foreign = { id: 2 };
  const paths = new PathGrants<object>();
  let rescueInfo: { path: string | null } | null = null;
  const recent: string[] = [];
  const rescued: { path: string | null }[] = [];

  beforeEach(async () => {
    dir = mkdtempSync(join(tmpdir(), 'bt-security-'));
    electron.on.clear();
    electron.handle.clear();
    electron.opened.length = 0;
    const { installAppIpc } = await import('../../src/main/app-ipc.ts');
    installAppIpc({
      store: {
        prefs: { a: 1 },
        layout: undefined,
        set: () => undefined,
        addRecent: (p: string) => recent.push(p),
        removeRecent: () => undefined,
      },
      rescue: {
        read: () =>
          rescueInfo === null
            ? null
            : { data: '', info: { name: 'x', revision: 1, time: 0, path: rescueInfo.path } },
        write: (_data: string, info: { path: string | null }) => rescued.push(info),
        clear: () => undefined,
      },
      paths,
      version: '0.0.0',
      window: (sender: object) => (sender === ours ? {} : null),
      setMenu: () => undefined,
      closeTaken: () => undefined,
      closeWindow: () => undefined,
      cancelQuit: () => undefined,
    } as unknown as Parameters<typeof installAppIpc>[0]);
  });

  afterEach(() => {
    rmSync(dir, { recursive: true, force: true });
  });

  const invoke = (channel: string, sender: object, ...args: unknown[]): unknown => {
    const h = electron.handle.get(channel);
    if (h === undefined) {
      throw new Error(`no handler ${channel}`);
    }
    return h({ sender }, ...args);
  };

  it('answer only the app’s windows', async () => {
    electron.openPaths = [join(dir, 'a.xmpuzzle')];
    electron.savePath = join(dir, 'b.xmpuzzle');
    rescueInfo = { path: join(dir, 'r.xmpuzzle') };
    expect(await invoke('bt:choose-open', foreign, 'puzzle')).toBeNull();
    expect(await invoke('bt:choose-save', foreign, 'puzzle', 'b')).toBeNull();
    expect(await invoke('bt:write-export', foreign, join(dir, 'x.png'), new Uint8Array(1))).toEqual(
      {
        error: 'nothing to write',
      },
    );
    expect(await invoke('bt:load-state', foreign)).toBeNull();
    expect(await invoke('bt:rescue', foreign, 'read')).toBeNull();
    const info = { sender: foreign, returnValue: undefined as unknown };
    electron.on.get('bt:app-info')?.(info);
    expect(info.returnValue).toBeNull();
    electron.on.get('bt:dropped')?.({ sender: foreign }, join(dir, 'd.xmpuzzle'));
    electron.on.get('bt:help')?.({ sender: foreign }, 'guide');
    expect(electron.opened).toEqual([]);
    for (const p of ['a', 'b', 'r', 'd']) {
      expect(paths.allows(foreign, join(dir, `${p}.xmpuzzle`))).toBe(false);
      expect(paths.allows(ours, join(dir, `${p}.xmpuzzle`))).toBe(false);
    }
  });

  it('grant the window what its dialogs, its rescue record and its drops gave', async () => {
    electron.openPaths = [join(dir, 'a.xmpuzzle')];
    expect(await invoke('bt:choose-open', ours, 'puzzle')).toBe(join(dir, 'a.xmpuzzle'));
    electron.openPaths = [join(dir, 'p.btsolve')];
    expect(await invoke('bt:choose-open', ours, 'paused')).toBe(join(dir, 'p.btsolve'));
    // the exports cannot be opened
    expect(await invoke('bt:choose-open', ours, 'image')).toBeNull();
    electron.savePath = join(dir, 'b');
    expect(await invoke('bt:choose-save', ours, 'puzzle', 'b')).toBe(join(dir, 'b.xmpuzzle'));
    rescueInfo = { path: join(dir, 'r.xmpuzzle') };
    await invoke('bt:rescue', ours, 'read');
    electron.on.get('bt:dropped')?.({ sender: ours }, join(dir, 'd.xmpuzzle'));
    electron.on.get('bt:dropped')?.({ sender: ours }, 42);
    for (const p of ['a.xmpuzzle', 'p.btsolve', 'b.xmpuzzle', 'r.xmpuzzle', 'd.xmpuzzle']) {
      expect(paths.allows(ours, join(dir, p)), p).toBe(true);
      expect(paths.allows(foreign, join(dir, p)), p).toBe(false);
    }
    // an export's path is the export's grant, not the engine's
    electron.savePath = join(dir, 'shot.png');
    expect(await invoke('bt:choose-save', ours, 'image', 'shot')).toBe(join(dir, 'shot.png'));
    expect(paths.allows(ours, join(dir, 'shot.png'))).toBe(false);
    // and is written once, only there
    expect(
      await invoke('bt:write-export', ours, join(dir, 'other.png'), new Uint8Array(1)),
    ).toEqual({
      error: "other.png was not chosen in the export's dialog",
    });
    expect(
      await invoke('bt:write-export', ours, join(dir, 'shot.png'), new Uint8Array([1])),
    ).toEqual({
      ok: true,
    });
    expect(
      await invoke('bt:write-export', ours, join(dir, 'shot.png'), new Uint8Array([1])),
    ).toMatchObject({
      error: expect.stringContaining('was not chosen') as unknown,
    });
  });

  it('keep only handed files as recent files and as the rescue record’s', async () => {
    electron.openPaths = [join(dir, 'chosen.xmpuzzle')];
    await invoke('bt:choose-open', ours, 'puzzle');
    const add = electron.on.get('bt:recent');
    add?.({ sender: ours }, 'add', join(dir, 'chosen.xmpuzzle'));
    add?.({ sender: ours }, 'add', '/etc/elsewhere.xmpuzzle');
    add?.({ sender: foreign }, 'add', join(dir, 'chosen.xmpuzzle'));
    expect(recent).toEqual([join(dir, 'chosen.xmpuzzle')]);
    const info = (path: string | null): unknown => ({ path, name: 'n', revision: 1, time: 2 });
    await invoke('bt:rescue', ours, 'write', 'AAAA', info(join(dir, 'chosen.xmpuzzle')));
    await invoke('bt:rescue', ours, 'write', 'AAAA', info('/etc/elsewhere.xmpuzzle'));
    await invoke('bt:rescue', ours, 'write', 'AAAA', info(null));
    await invoke('bt:rescue', foreign, 'write', 'AAAA', info(join(dir, 'chosen.xmpuzzle')));
    expect(rescued.map((r) => r.path)).toEqual([join(dir, 'chosen.xmpuzzle'), null, null]);
  });

  it('open only the help links, by name', () => {
    const help = electron.on.get('bt:help');
    help?.({ sender: ours }, 'guide');
    help?.({ sender: ours }, 'tutorial');
    help?.({ sender: ours }, 'https://example.com/');
    help?.({ sender: ours }, 'toString');
    expect(electron.opened).toEqual([HELP_URLS.guide, HELP_URLS.tutorial]);
  });
});
