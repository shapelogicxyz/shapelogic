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
  CliToolResult,
  RescueInfo,
  StoredState,
  UpdateResult,
} from '../engine/app-bridge.ts';
import type { BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';

const bridge: BtBridge = {
  send(request: BridgeRequest): void {
    ipcRenderer.send('bt:call', request);
  },
  onMessage(handler: (message: BridgeMessage) => void): () => void {
    const listener = (_event: IpcRendererEvent, message: BridgeMessage): void => {
      handler(message);
    };
    ipcRenderer.on('bt:message', listener);
    return () => {
      ipcRenderer.removeListener('bt:message', listener);
    };
  },
};

const info = ipcRenderer.sendSync('bt:app-info') as { platform: string; version: string } | null;

// The main process sends a window's first commands (a file given on the
// command line or opened from the file manager at the start) when its page
// has loaded, before the page has read its stored settings and listens
// (renderer/main.ts): they are kept here, and handed to the first handler,
// in order, a task after it subscribes (stage 8: they were lost).
const commandHandlers = new Set<(command: AppCommand) => void>();
let early: AppCommand[] | null = [];
ipcRenderer.on('bt:command', (_event: IpcRendererEvent, command: unknown) => {
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

const btApp: BtApp = {
  platform: info?.platform ?? '',
  version: info?.version ?? '',
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
    ipcRenderer.send('bt:menu-state', state);
  },
  chooseOpen(kind) {
    return ipcRenderer.invoke('bt:choose-open', kind) as Promise<string | null>;
  },
  chooseSave(kind, name) {
    return ipcRenderer.invoke('bt:choose-save', kind, name) as Promise<string | null>;
  },
  async writeExport(path, data) {
    const r = (await ipcRenderer.invoke('bt:write-export', path, data)) as {
      ok?: boolean;
      error?: string;
    };
    if (r.ok !== true) {
      throw new Error(r.error ?? 'the file could not be written');
    }
  },
  chooseFolder() {
    return ipcRenderer.invoke('bt:choose-folder') as Promise<string | null>;
  },
  async existingExports(folder, names) {
    return ((await ipcRenderer.invoke('bt:existing-exports', folder, names)) as number | null) ?? 0;
  },
  async writeFolderExport(folder, name, data) {
    const r = (await ipcRenderer.invoke('bt:write-folder-export', folder, name, data)) as {
      ok?: boolean;
      error?: string;
    };
    if (r.ok !== true) {
      throw new Error(r.error ?? 'the file could not be written');
    }
  },
  pathOf(file) {
    // a file from the file system (dropped) has a path; one the page made has none
    const path = webUtils.getPathForFile(file);
    if (path !== '') {
      // the window's engine may be given it (main/path-grants.ts)
      ipcRenderer.send('bt:dropped', path);
    }
    return path;
  },
  async loadState() {
    return ((await ipcRenderer.invoke('bt:load-state')) as StoredState | null) ?? {};
  },
  saveState(key, value) {
    ipcRenderer.send('bt:save-state', key, value);
  },
  addRecent(path) {
    ipcRenderer.send('bt:recent', 'add', path);
  },
  removeRecent(path) {
    ipcRenderer.send('bt:recent', 'remove', path);
  },
  async writeRescue(data, rescueInfo) {
    await ipcRenderer.invoke('bt:rescue', 'write', data, rescueInfo);
  },
  readRescue() {
    return ipcRenderer.invoke('bt:rescue', 'read') as Promise<{
      data: string;
      info: RescueInfo;
    } | null>;
  },
  async clearRescue() {
    await ipcRenderer.invoke('bt:rescue', 'clear');
  },
  openHelp(link) {
    ipcRenderer.send('bt:help', link);
  },
  checkForUpdates(auto) {
    return ipcRenderer.invoke('bt:check-updates', auto) as Promise<UpdateResult | null>;
  },
  skipUpdate(version) {
    ipcRenderer.send('bt:skip-update', version);
  },
  openReleases() {
    ipcRenderer.send('bt:open-releases');
  },
  async cliToolInfo() {
    const r = (await ipcRenderer.invoke('bt:cli-tool', 'info')) as CliToolResult;
    if (!r.ok) {
      throw new Error(r.error);
    }
    return r.info;
  },
  installCliTool() {
    return ipcRenderer.invoke('bt:cli-tool', 'install') as Promise<CliToolResult>;
  },
  uninstallCliTool() {
    return ipcRenderer.invoke('bt:cli-tool', 'uninstall') as Promise<CliToolResult>;
  },
  closeWindow() {
    ipcRenderer.send('bt:close', 'close');
  },
  closeTaken() {
    ipcRenderer.send('bt:close', 'taken');
  },
  closeCancelled() {
    ipcRenderer.send('bt:close', 'cancelled');
  },
};

contextBridge.exposeInMainWorld('bt', bridge);
contextBridge.exposeInMainWorld('btApp', btApp);
