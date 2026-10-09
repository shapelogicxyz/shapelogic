// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preload script (sandboxed, context-isolated; decision 0013): exposes
// the engine bridge as window.bt and nothing else of Electron or Node.

import { contextBridge, ipcRenderer } from 'electron';
import type { IpcRendererEvent } from 'electron';

import type { BridgeCall, BridgeMessage, BtBridge } from '../engine/bridge.ts';

const bridge: BtBridge = {
  send(call: BridgeCall): void {
    ipcRenderer.send('bt:call', call);
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

contextBridge.exposeInMainWorld('bt', bridge);
