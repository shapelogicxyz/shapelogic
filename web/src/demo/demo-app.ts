// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's window.btApp: what Electron's main process gives the app,
// for a page on the website. There are no menus, dialogs or files: the
// settings start from the defaults and are not kept, nothing is written,
// and Help opens the site's guide.

import type { AppCommand, BtApp, CliToolResult } from '../engine/app-bridge.ts';

/** The demo's app, with a way to send it a command as the menus would. */
export interface DemoApp extends BtApp {
  command(command: AppCommand): void;
}

const NOT_HERE = 'not in the demo';

const nothing = (): void => undefined;

/** The demo's app (version: the recorded engine's). */
export function demoApp(version: string): DemoApp {
  const handlers = new Set<(command: AppCommand) => void>();
  const refused: CliToolResult = { ok: false, error: NOT_HERE };
  return {
    platform: /Mac|iPhone|iPad/.test(navigator.userAgent) ? 'darwin' : 'linux',
    version,
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
    writeRescue: () => Promise.resolve(),
    readRescue: () => Promise.resolve(null),
    clearRescue: () => Promise.resolve(),
    openHelp(link) {
      window.open(link === 'tutorial' ? '/docs/getting-started/' : '/docs/', '_blank', 'noopener');
    },
    // the site is always the latest: no update to look for
    checkForUpdates: () => Promise.resolve(null),
    skipUpdate: nothing,
    openReleases: nothing,
    cliToolInfo: () => Promise.resolve({ path: null, link: null, state: 'none' }),
    installCliTool: () => Promise.resolve(refused),
    uninstallCliTool: () => Promise.resolve(refused),
    closeWindow: nothing,
    closeTaken: nothing,
    closeCancelled: nothing,
  };
}
