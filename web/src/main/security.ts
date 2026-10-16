// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Electron security checklist as data and pure functions (stage 8), so
// that a unit test checks what main/index.ts applies: the window's web
// preferences (sandboxed, context-isolated, no Node, no webview, no
// navigation by a drop), the page it loads (the app's own, on a scheme of
// its own, `bt-app://app/`, served from the renderer's directory and
// nowhere else; a dev server only when not packaged), the requests its
// session lets through (the app's files, and in development the dev
// server: no remote content), the permissions it grants (none), and the
// environment switches a packaged app keeps. Electron's types only.
//
// The page is not loaded from file: (the web stage 8 audit): on a file:
// page the CSP's 'self' matches every file: URL and Electron's
// grantFileProtocolExtraPrivileges fuse lets it fetch other files, so the
// request filter alone stood between markup in a puzzle file's names and a
// script loaded from disk. On its own scheme 'self' is the page's origin,
// the fuse is off, and the filter is the second check, not the only one.

import type { BrowserWindowConstructorOptions, WebPreferences } from 'electron';
import { isAbsolute, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { HelpLink } from '../engine/app-bridge.ts';

/**
 * The user guide and its Getting started page (site/src/content/docs/docs):
 * the only addresses the app hands the system's browser
 * (shell.openExternal). The renderer names a link, never a URL. The pages
 * must exist in the site (site/tests/help-urls.test.mjs checks them).
 */
export const HELP_URLS: Readonly<Record<HelpLink, string>> = {
  guide: 'https://shapelogic.xyz/docs/',
  tutorial: 'https://shapelogic.xyz/docs/getting-started/',
};

/** The scheme the renderer's page is served on: standard and secure, the app's own. */
export const APP_SCHEME = 'bt-app';
/** The one host of that scheme. */
export const APP_HOST = 'app';

/** The page's URL for a file of the renderer's directory, with a query. */
export function appUrl(file: string, query: Readonly<Record<string, string>> = {}): string {
  const u = new URL(`${APP_SCHEME}://${APP_HOST}/${file}`);
  for (const [k, v] of Object.entries(query)) {
    u.searchParams.set(k, v);
  }
  return u.toString();
}

/**
 * The file of the renderer's directory an app URL names, or null: another
 * scheme or host, a path that leaves the directory (`..`, encoded or not),
 * the directory itself. What `protocol.handle` serves.
 */
export function appFile(url: string, rendererDir: string): string | null {
  let u: URL;
  try {
    u = new URL(url);
  } catch {
    return null;
  }
  if (u.protocol !== `${APP_SCHEME}:` || u.host !== APP_HOST) {
    return null;
  }
  let rel: string;
  try {
    rel = decodeURIComponent(u.pathname);
  } catch {
    return null;
  }
  if (rel.includes('\0')) {
    return null;
  }
  const path = join(rendererDir, rel);
  const r = relative(rendererDir, path);
  return r !== '' && !r.startsWith('..') && !isAbsolute(r) ? path : null;
}

/** The renderer's web preferences, given the preload script's path. */
export function webPreferences(preload: string): WebPreferences {
  return {
    preload,
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
    // the page needs no spelling and no autoplay: off is less to trust
    spellcheck: false,
    safeDialogs: true,
  };
}

/** A window's options: its size and place, its title, the web preferences. */
export function windowOptions(
  preload: string,
  title: string,
  bounds: { width: number; height: number; x?: number; y?: number },
): BrowserWindowConstructorOptions {
  return {
    ...bounds,
    title,
    show: false,
    webPreferences: webPreferences(preload),
  };
}

/** The switches the environment may set, as this run of the app takes them. */
export interface Switches {
  /** The engine to run (BT_ENGINE); development only. */
  engine?: string;
  /** The app's data directory (BT_USER_DATA). */
  userData?: string;
  /** The file every call is logged to (BT_CALL_LOG); development only. */
  callLog?: string;
  /** The Solver's autosave interval in seconds (BT_AUTOSAVE_SECONDS); development only. */
  autosave?: string;
  /** The dev server the renderer is loaded from (electron-vite's ELECTRON_RENDERER_URL); development only. */
  rendererUrl?: string;
}

/**
 * The environment's switches a run takes. A packaged app takes only
 * BT_USER_DATA (where its own data goes; the smoke test keeps the user's
 * apart): the others run another program (BT_ENGINE), load another page
 * into the window that has window.btApp (ELECTRON_RENDERER_URL) or write
 * a file (BT_CALL_LOG), which nothing outside the app may ask of it.
 */
export function switches(
  packaged: boolean,
  env: Readonly<Record<string, string | undefined>>,
): Switches {
  const s: Switches = {};
  const take = (name: string): string | undefined => {
    const v = env[name];
    return v === undefined || v === '' ? undefined : v;
  };
  const userData = take('BT_USER_DATA');
  if (userData !== undefined) {
    s.userData = userData;
  }
  if (packaged) {
    return s;
  }
  const engine = take('BT_ENGINE');
  const callLog = take('BT_CALL_LOG');
  const autosave = take('BT_AUTOSAVE_SECONDS');
  const rendererUrl = take('ELECTRON_RENDERER_URL');
  if (engine !== undefined) {
    s.engine = engine;
  }
  if (callLog !== undefined) {
    s.callLog = callLog;
  }
  if (autosave !== undefined) {
    s.autosave = autosave;
  }
  if (rendererUrl !== undefined) {
    s.rendererUrl = rendererUrl;
  }
  return s;
}

/** Where the page comes from: the renderer's directory, or in development the dev server. */
export interface PageSource {
  /** The directory of the renderer's index.html (the app's scheme serves it; file: URLs are allowed only inside it). */
  rendererDir: string;
  /** The dev server's address (security.ts's switches; never when packaged). */
  rendererUrl?: string | undefined;
}

/**
 * Whether the window's session lets a request through: the renderer's own
 * files (the app's scheme naming a file inside its directory, and file:
 * URLs inside it, which the scheme's handler reads; no other file), what
 * Chromium makes itself (devtools:, and the data: and blob: URLs the page
 * made), and in development the dev server's origin (its page, its
 * modules, its hot-reload socket). Nothing remote.
 */
export function allowedRequest(url: string, source: PageSource): boolean {
  let u: URL;
  try {
    u = new URL(url);
  } catch {
    return false;
  }
  if (u.protocol === `${APP_SCHEME}:`) {
    return appFile(url, source.rendererDir) !== null;
  }
  if (u.protocol === 'file:') {
    let path: string;
    try {
      path = fileURLToPath(u);
    } catch {
      return false;
    }
    const r = relative(source.rendererDir, path);
    return r !== '' && !r.startsWith('..') && !isAbsolute(r);
  }
  if (['devtools:', 'data:', 'blob:'].includes(u.protocol)) {
    return true;
  }
  const rendererUrl = source.rendererUrl;
  if (rendererUrl === undefined) {
    return false;
  }
  const dev = new URL(rendererUrl);
  const host = (x: URL): string => `${x.hostname}:${x.port}`;
  return (
    host(u) === host(dev) &&
    (u.protocol === dev.protocol || (u.protocol === 'ws:' && dev.protocol === 'http:'))
  );
}

/** The permissions a page may be granted: none (it needs no camera, no notifications, no clipboard). */
const GRANTED: ReadonlySet<string> = new Set();

/** Whether the page may have a permission (Electron's permission handlers). */
export function permissionAllowed(permission: string): boolean {
  return GRANTED.has(permission);
}
