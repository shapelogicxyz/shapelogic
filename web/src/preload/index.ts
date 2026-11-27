// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preload script (sandboxed, context-isolated; decision 0013): exposes
// the engine bridge as window.sl and, from stage 6, the app's menus,
// dialogs, store and rescue copy as window.slApp (engine/app-bridge.ts),
// with stage 7 the exports' writes, and nothing else of Electron or Node.
// A dropped file's path is reported to the main process as it is read
// (stage 8): only such paths, and the dialogs', reach the engine.

import { contextBridge, ipcRenderer, webUtils } from 'electron';
import type { IpcRendererEvent } from 'electron';

import { isAppCommand } from '../engine/app-bridge.ts';
import type {
  AppCommand,
  SlApp,
  ChannelArgs,
  ChannelOf,
  ChannelResult,
  WriteResult,
} from '../engine/app-bridge.ts';
import type { BridgeMessage, BridgeRequest, SlBridge } from '../engine/bridge.ts';

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

const bridge: SlBridge = {
  send(request: BridgeRequest): void {
    send('sl:call', request);
  },
  onMessage(handler: (message: BridgeMessage) => void): () => void {
    return listen('sl:message', (m) => {
      handler(m as BridgeMessage);
    });
  },
};

const info = ipcRenderer.sendSync('sl:app-info') as ChannelResult<'sl:app-info'>;

// The main process sends a window's first commands (a file given on the
// command line or opened from the file manager at the start) when its page
// has loaded, before the page has read its stored settings and listens
// (renderer/main.ts): they are kept here, and handed to the first handler,
// in order, a task after it subscribes (stage 8: they were lost).
const commandHandlers = new Set<(command: AppCommand) => void>();
let early: AppCommand[] | null = [];
listen('sl:command', (command) => {
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
listen('sl:read-only', (value) => {
  if (typeof value === 'boolean') {
    readOnly = value;
    for (const h of readOnlyHandlers) {
      h(value);
    }
  }
});

const slApp: SlApp = {
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
    send('sl:menu-state', state);
  },
  chooseOpen(kind) {
    return invoke('sl:choose-open', kind);
  },
  chooseSave(kind, name) {
    return invoke('sl:choose-save', kind, name);
  },
  writeExport(path, data) {
    return written(invoke('sl:write-export', path, data));
  },
  chooseFolder(save = false) {
    return invoke('sl:choose-folder', save);
  },
  existingExports(folder, names) {
    return invoke('sl:existing-exports', folder, names);
  },
  writeFolderExport(folder, name, data) {
    return written(invoke('sl:write-folder-export', folder, name, data));
  },
  pathOf(file) {
    // a file from the file system (dropped) has a path; one the page made has none
    const path = webUtils.getPathForFile(file);
    if (path !== '') {
      // the window's engine may be given it (main/path-grants.ts)
      send('sl:dropped', path);
    }
    return path;
  },
  async loadState() {
    return (await invoke('sl:load-state')) ?? {};
  },
  saveState(key, value) {
    send('sl:save-state', key, value);
  },
  addRecent(path) {
    send('sl:recent-add', path);
  },
  removeRecent(path) {
    send('sl:recent-remove', path);
  },
  async writeRescue(rescueInfo) {
    return (
      (await invoke('sl:rescue-write', rescueInfo)) ?? {
        ok: false,
        errc: 'invalid_argument',
        message: NOT_OURS,
      }
    );
  },
  readRescue() {
    return invoke('sl:rescue-read');
  },
  async clearRescue() {
    await invoke('sl:rescue-clear');
  },
  newWindow() {
    send('sl:new-window');
  },
  claimPath(path) {
    send('sl:claim-path', path);
  },
  onReadOnly(handler) {
    readOnlyHandlers.add(handler);
    handler(readOnly);
    return () => {
      readOnlyHandlers.delete(handler);
    };
  },
  onPrefs(handler) {
    return listen('sl:prefs', (value) => {
      if (typeof value === 'object' && value !== null && !Array.isArray(value)) {
        handler(value as Record<string, unknown>);
      }
    });
  },
  openHelp(link) {
    send('sl:help', link);
  },
  goalWindow(picture, raise) {
    send('sl:goal-window', picture, raise);
  },
  checkForUpdates(auto) {
    return invoke('sl:check-updates', auto);
  },
  skipUpdate(version) {
    send('sl:skip-update', version);
  },
  openReleases() {
    send('sl:open-releases');
  },
  async cliToolInfo() {
    const r = await invoke('sl:cli-info');
    if (!r.ok) {
      throw new Error(r.error);
    }
    return r.info;
  },
  installCliTool() {
    return invoke('sl:cli-install');
  },
  uninstallCliTool() {
    return invoke('sl:cli-uninstall');
  },
  cacheFolder() {
    return invoke('sl:cache-folder');
  },
  async chooseCacheFolder(useDefault) {
    return (await invoke('sl:cache-choose', useDefault)) ?? { status: 'canceled' };
  },
  async finishCacheFolder(action) {
    return (await invoke('sl:cache-finish', action)) ?? { ok: false, error: NOT_OURS };
  },
  cacheItems() {
    return invoke('sl:cache-items');
  },
  cacheItemsAtStart() {
    return invoke('sl:cache-items-start');
  },
  async deleteCacheItem(id) {
    return (await invoke('sl:cache-delete', id)) ?? { ok: false, error: NOT_OURS };
  },
  linkCacheFolders(link) {
    send('sl:cache-link', link);
  },
  logPage(query) {
    return invoke('sl:log-page', query);
  },
  async clearLog() {
    await invoke('sl:log-clear');
  },
  async logClosed() {
    await invoke('sl:log-closed');
  },
  logExportSizes() {
    return invoke('sl:log-sizes');
  },
  async exportLog(lines) {
    return (
      (await (lines === undefined ? invoke('sl:log-export') : invoke('sl:log-export', lines))) ?? {
        ok: false,
        error: NOT_OURS,
      }
    );
  },
  closeWindow() {
    send('sl:close-window');
  },
  closeTaken() {
    send('sl:close-taken');
  },
  ready() {
    send('sl:engine-ready');
  },
  retryEngine() {
    send('sl:engine-retry');
  },
  commandHeld(command, dropped) {
    send('sl:command-held', command, dropped);
  },
  async copyDiagnostics() {
    return (await invoke('sl:diagnostics-copy')) ?? { ok: false, error: 'not available' };
  },
  async saveDiagnostics() {
    return (await invoke('sl:diagnostics-save')) ?? { ok: false, error: 'not available' };
  },
  closeCanceled() {
    send('sl:close-canceled');
  },
};

contextBridge.exposeInMainWorld('sl', bridge);
contextBridge.exposeInMainWorld('slApp', slApp);
