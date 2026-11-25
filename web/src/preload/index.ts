// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preload script (sandboxed, context-isolated; decision 0013): exposes
// the engine bridge as window.bt and, from stage 6, the app's menus,
// dialogs, store and rescue copy as window.btApp (engine/app-bridge.ts),
// with stage 7 the exports' writes, and nothing else of Electron or Node.
// A dropped file's path is reported to the main process as it is read
// (stage 8): only such paths, and the dialogs', reach the engine.

import { contextBridge, ipcRenderer, webUtils } from 'electron';
import type { IpcRendererEvent } from 'electron';

import { isAppCommand } from '../engine/app-bridge.ts';
import type {
  AppCommand,
  BtApp,
  ChannelArgs,
  ChannelOf,
  ChannelResult,
  WriteResult,
} from '../engine/app-bridge.ts';
import type { BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';

// Every channel through these three, typed by app-bridge.ts's table
// (cleanup item 12.2): a name, an argument or a result that main does not
// have fails the build.

/** Renderer to main, no answer. */
function send<C extends ChannelOf<'send'>>(channel: C, ...args: ChannelArgs<C>): void {
  ipcRenderer.send(channel, ...args);
}

/** Renderer to main, answered as the table says (main answers a window not the app's too). */
function invoke<C extends ChannelOf<'invoke'>>(
  channel: C,
  ...args: ChannelArgs<C>
): Promise<ChannelResult<C>> {
  return ipcRenderer.invoke(channel, ...args) as Promise<ChannelResult<C>>;
}

/**
 * Main to renderer: the handler hears what came, unchecked (main is
 * trusted, but each listener still checks: a page reloaded meanwhile).
 * Returns the unsubscribe.
 */
function listen(channel: ChannelOf<'event'>, handler: (value: unknown) => void): () => void {
  const listener = (_event: IpcRendererEvent, value: unknown): void => {
    handler(value);
  };
  ipcRenderer.on(channel, listener);
  return () => {
    ipcRenderer.removeListener(channel, listener);
  };
}

/** A write main answered: done, or its reason as the rejection. */
async function written(r: Promise<WriteResult>): Promise<void> {
  const w = await r;
  if (!w.ok) {
    throw new Error(w.error);
  }
}

const NOT_OURS = 'not a ShapeLogic window';

const bridge: BtBridge = {
  send(request: BridgeRequest): void {
    send('bt:call', request);
  },
  onMessage(handler: (message: BridgeMessage) => void): () => void {
    return listen('bt:message', (m) => {
      handler(m as BridgeMessage);
    });
  },
};

const info = ipcRenderer.sendSync('bt:app-info') as ChannelResult<'bt:app-info'>;

// The main process sends a window's first commands (a file given on the
// command line or opened from the file manager at the start) when its page
// has loaded, before the page has read its stored settings and listens
// (renderer/main.ts): they are kept here, and handed to the first handler,
// in order, a task after it subscribes (stage 8: they were lost).
const commandHandlers = new Set<(command: AppCommand) => void>();
let early: AppCommand[] | null = [];
listen('bt:command', (command) => {
  if (!isAppCommand(command)) {
    return;
  }
  if (early !== null) {
    early.push(command);
    return;
  }
  for (const h of commandHandlers) {
    h(command);
  }
});

// Whether the window holds its file read-only (main/path-owners.ts): kept
// here, so a page that subscribes late (or a reload) gets the state now.
let readOnly = false;
const readOnlyHandlers = new Set<(readOnly: boolean) => void>();
listen('bt:read-only', (value) => {
  if (typeof value === 'boolean') {
    readOnly = value;
    for (const h of readOnlyHandlers) {
      h(value);
    }
  }
});

const btApp: BtApp = {
  platform: info?.platform ?? '',
  version: info?.version ?? '',
  keepsLog: true,
  onCommand(handler: (command: AppCommand) => void): () => void {
    commandHandlers.add(handler);
    if (early !== null) {
      // after the code subscribing is through; commands meanwhile join the queue
      setTimeout(() => {
        const queued = early ?? [];
        early = null;
        for (const c of queued) {
          for (const h of commandHandlers) {
            h(c);
          }
        }
      }, 0);
    }
    return () => {
      commandHandlers.delete(handler);
    };
  },
  setMenuState(state) {
    send('bt:menu-state', state);
  },
  chooseOpen(kind) {
    return invoke('bt:choose-open', kind);
  },
  chooseSave(kind, name) {
    return invoke('bt:choose-save', kind, name);
  },
  writeExport(path, data) {
    return written(invoke('bt:write-export', path, data));
  },
  chooseFolder(save = false) {
    return invoke('bt:choose-folder', save);
  },
  existingExports(folder, names) {
    return invoke('bt:existing-exports', folder, names);
  },
  writeFolderExport(folder, name, data) {
    return written(invoke('bt:write-folder-export', folder, name, data));
  },
  pathOf(file) {
    // a file from the file system (dropped) has a path; one the page made has none
    const path = webUtils.getPathForFile(file);
    if (path !== '') {
      // the window's engine may be given it (main/path-grants.ts)
      send('bt:dropped', path);
    }
    return path;
  },
  async loadState() {
    return (await invoke('bt:load-state')) ?? {};
  },
  saveState(key, value) {
    send('bt:save-state', key, value);
  },
  addRecent(path) {
    send('bt:recent-add', path);
  },
  removeRecent(path) {
    send('bt:recent-remove', path);
  },
  async writeRescue(rescueInfo) {
    return (
      (await invoke('bt:rescue-write', rescueInfo)) ?? {
        ok: false,
        errc: 'invalid_argument',
        message: NOT_OURS,
      }
    );
  },
  readRescue() {
    return invoke('bt:rescue-read');
  },
  async clearRescue() {
    await invoke('bt:rescue-clear');
  },
  newWindow() {
    send('bt:new-window');
  },
  claimPath(path) {
    send('bt:claim-path', path);
  },
  onReadOnly(handler) {
    readOnlyHandlers.add(handler);
    handler(readOnly);
    return () => {
      readOnlyHandlers.delete(handler);
    };
  },
  onPrefs(handler) {
    return listen('bt:prefs', (value) => {
      if (typeof value === 'object' && value !== null && !Array.isArray(value)) {
        handler(value as Record<string, unknown>);
      }
    });
  },
  openHelp(link) {
    send('bt:help', link);
  },
  goalWindow(picture, raise) {
    send('bt:goal-window', picture, raise);
  },
  checkForUpdates(auto) {
    return invoke('bt:check-updates', auto);
  },
  skipUpdate(version) {
    send('bt:skip-update', version);
  },
  openReleases() {
    send('bt:open-releases');
  },
  async cliToolInfo() {
    const r = await invoke('bt:cli-info');
    if (!r.ok) {
      throw new Error(r.error);
    }
    return r.info;
  },
  installCliTool() {
    return invoke('bt:cli-install');
  },
  uninstallCliTool() {
    return invoke('bt:cli-uninstall');
  },
  cacheFolder() {
    return invoke('bt:cache-folder');
  },
  async chooseCacheFolder(useDefault) {
    return (await invoke('bt:cache-choose', useDefault)) ?? { status: 'canceled' };
  },
  async finishCacheFolder(action) {
    return (await invoke('bt:cache-finish', action)) ?? { ok: false, error: NOT_OURS };
  },
  cacheItems() {
    return invoke('bt:cache-items');
  },
  cacheItemsAtStart() {
    return invoke('bt:cache-items-start');
  },
  async deleteCacheItem(id) {
    return (await invoke('bt:cache-delete', id)) ?? { ok: false, error: NOT_OURS };
  },
  linkCacheFolders(link) {
    send('bt:cache-link', link);
  },
  logPage(query) {
    return invoke('bt:log-page', query);
  },
  async clearLog() {
    await invoke('bt:log-clear');
  },
  async logClosed() {
    await invoke('bt:log-closed');
  },
  logExportSizes() {
    return invoke('bt:log-sizes');
  },
  async exportLog(lines) {
    return (
      (await (lines === undefined ? invoke('bt:log-export') : invoke('bt:log-export', lines))) ?? {
        ok: false,
        error: NOT_OURS,
      }
    );
  },
  closeWindow() {
    send('bt:close-window');
  },
  closeTaken() {
    send('bt:close-taken');
  },
  ready() {
    send('bt:engine-ready');
  },
  retryEngine() {
    send('bt:engine-retry');
  },
  commandHeld(command, dropped) {
    send('bt:command-held', command, dropped);
  },
  async copyDiagnostics() {
    return (await invoke('bt:diagnostics-copy')) ?? { ok: false, error: 'not available' };
  },
  async saveDiagnostics() {
    return (await invoke('bt:diagnostics-save')) ?? { ok: false, error: 'not available' };
  },
  closeCanceled() {
    send('bt:close-canceled');
  },
};

contextBridge.exposeInMainWorld('bt', bridge);
contextBridge.exposeInMainWorld('btApp', btApp);
