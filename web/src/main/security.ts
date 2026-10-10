// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Electron security checklist as data and pure functions (stage 8), so
// that a unit test checks what main/index.ts applies: the window's web
// preferences (sandboxed, context-isolated, no Node, no webview, no
// navigation by a drop), the page it loads (the app's own file; a dev
// server only when not packaged), the requests its session lets through
// (the app's files, and in development the dev server: no remote
// content), the permissions it grants (none), and the environment
// switches a packaged app keeps. Electron's types only.

import type { BrowserWindowConstructorOptions, WebPreferences } from 'electron';
import { isAbsolute, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { HelpLink } from '../engine/app-bridge.ts';

/**
 * The original's user guide (platform.cpp) and its first chapter, "Getting
 * Started": the only addresses the app hands the system's browser
 * (shell.openExternal). The renderer names a link, never a URL.
 */
export const HELP_URLS: Readonly<Record<HelpLink, string>> = {
  guide: 'https://burrtools.sourceforge.net/gui-doc/toc.html',
  tutorial: 'https://burrtools.sourceforge.net/gui-doc/GettingStarted.html',
};

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
  /** The directory of the renderer's index.html (file: URLs are allowed only inside it). */
  rendererDir: string;
  /** The dev server's address (security.ts's switches; never when packaged). */
  rendererUrl?: string | undefined;
}

/**
 * Whether the window's session lets a request through: the renderer's own
 * files (file: URLs inside its directory, not any other file: the page
 * loads from file:, which Electron lets read other local files), what
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
