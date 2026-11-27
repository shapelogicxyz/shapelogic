// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's window.slApp: what Electron's main process gives the app,
// for a page on the website. There are no menus, dialogs or files: the
// settings start from the defaults and are not kept, nothing is written,
// Help opens the site's pages, and the Play tab's goal window is a popup.

import { goalPage, goalWindowSize, helpUrl } from '../engine/app-bridge.ts';
import type { AppCommand, SlApp, CliToolResult } from '../engine/app-bridge.ts';

/** The demo's app, with a way to send it a command as the menus would. */
export interface DemoApp extends SlApp {
  command(command: AppCommand): void;
}

const NOT_HERE = 'not in the demo';

const nothing = (): void => undefined;

/** The demo's app (version: the recorded engine's). */
export function demoApp(version: string): DemoApp {
  const handlers = new Set<(command: AppCommand) => void>();
  const refused: CliToolResult = { ok: false, error: NOT_HERE };
  /** The goal's popup, once opened, and its page's URL. */
  let goalPopup: Window | null = null;
  let goalUrl: string | null = null;
  return {
    platform: /Mac|iPhone|iPad/.test(navigator.userAgent) ? 'darwin' : 'linux',
    version,
    // the demo keeps no log
    keepsLog: false,
    command(command) {
      for (const h of [...handlers]) {
        h(command);
      }
    },
    onCommand(handler) {
      handlers.add(handler);
      return () => {
        handlers.delete(handler);
      };
    },
    setMenuState: nothing,
    chooseOpen: () => Promise.resolve(null),
    chooseSave: () => Promise.resolve(null),
    writeExport: () => Promise.reject(new Error(NOT_HERE)),
    chooseFolder: () => Promise.resolve(null),
    existingExports: () => Promise.resolve(0),
    writeFolderExport: () => Promise.reject(new Error(NOT_HERE)),
    pathOf: () => '',
    loadState: () => Promise.resolve({}),
    saveState: nothing,
    addRecent: nothing,
    removeRecent: nothing,
    writeRescue: () => Promise.resolve({ ok: true }),
    readRescue: () => Promise.resolve(null),
    clearRescue: () => Promise.resolve(),
    // one page, one document: no other window, nothing held read-only
    newWindow: nothing,
    claimPath: nothing,
    onReadOnly(handler) {
      handler(false);
      return nothing;
    },
    onPrefs: () => nothing,
    openHelp(link) {
      // the site's pages on the demo's own site
      window.open(helpUrl(link, location.origin), '_blank', 'noopener');
    },
    goalWindow(picture, raise) {
      // a popup of the page's own, its page a blob: URL of this origin
      if (!raise && (goalPopup === null || goalPopup.closed)) {
        return;
      }
      const url = URL.createObjectURL(new Blob([goalPage(picture)], { type: 'text/html' }));
      if (goalUrl !== null) {
        URL.revokeObjectURL(goalUrl);
      }
      goalUrl = url;
      if (raise) {
        const size = goalWindowSize(picture);
        const features = `popup,width=${String(size.width)},height=${String(size.height)}`;
        goalPopup = window.open(url, 'shapelogic-goal', features);
        goalPopup?.focus();
      } else {
        goalPopup?.location.replace(url);
      }
    },
    // the site is always the latest: no update to look for
    checkForUpdates: () => Promise.resolve(null),
    skipUpdate: nothing,
    openReleases: nothing,
    cliToolInfo: () => Promise.resolve({ path: null, link: null, state: 'none' }),
    installCliTool: () => Promise.resolve(refused),
    uninstallCliTool: () => Promise.resolve(refused),
    cacheFolder: () => Promise.resolve(null),
    chooseCacheFolder: () => Promise.resolve({ status: 'canceled' }),
    finishCacheFolder: () => Promise.resolve({ ok: false, error: NOT_HERE }),
    cacheItems: () => Promise.resolve(null),
    cacheItemsAtStart: () => Promise.resolve(null),
    deleteCacheItem: () => Promise.resolve({ ok: false, error: NOT_HERE }),
    linkCacheFolders: nothing,
    // the demo keeps no log
    logPage: () => Promise.resolve(null),
    clearLog: () => Promise.resolve(),
    logClosed: () => Promise.resolve(),
    logExportSizes: () => Promise.resolve(null),
    exportLog: () => Promise.resolve({ ok: false, error: NOT_HERE }),
    closeWindow: nothing,
    closeTaken: nothing,
    closeCanceled: nothing,
    // the demo's engine runs in the page: nothing to start again or diagnose
    ready: nothing,
    retryEngine: nothing,
    commandHeld: nothing,
    copyDiagnostics: () => Promise.resolve({ ok: false, error: NOT_HERE }),
    saveDiagnostics: () => Promise.resolve({ ok: false, error: NOT_HERE }),
  };
}
