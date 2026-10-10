// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preload script (sandboxed, context-isolated; decision 0013): exposes
// the engine bridge as window.bt and, from stage 6, the app's menus,
// dialogs, store and rescue copy as window.btApp (engine/app-bridge.ts),
// with stage 7 the exports' writes, and nothing else of Electron or Node.

import { contextBridge, ipcRenderer, webUtils } from 'electron';
import type { IpcRendererEvent } from 'electron';

import { isAppCommand } from '../engine/app-bridge.ts';
import type { AppCommand, BtApp, RescueInfo, StoredState } from '../engine/app-bridge.ts';
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

const btApp: BtApp = {
  platform: info?.platform ?? '',
  version: info?.version ?? '',
  onCommand(handler: (command: AppCommand) => void): () => void {
    const listener = (_event: IpcRendererEvent, command: unknown): void => {
      if (isAppCommand(command)) {
        handler(command);
      }
    };
    ipcRenderer.on('bt:command', listener);
    return () => {
      ipcRenderer.removeListener('bt:command', listener);
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
  pathOf(file) {
    return webUtils.getPathForFile(file);
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
