// SPDX-License-Identifier: GPL-3.0-or-later
//
// Electron's main process (decision 0013): one window, one engine host per
// window, the bridge between them. The renderer is sandboxed and isolated;
// it reaches the engine only through the preload's window.bt. A main-frame
// navigation (a reload) gets a new bridge server: the new page's proxy
// numbers its calls from 1 again, so replies to the old page's calls must
// not reach it. Stage 6 adds the menus, files and recovery.

import { app, BrowserWindow, ipcMain } from 'electron';
import { fileURLToPath } from 'node:url';

import { NodeTransport } from '../engine/node-transport.ts';
import { findEngine } from './engine-path.ts';
import { EngineHost } from './engine-host.ts';
import { serveEngine } from './engine-ipc.ts';

const servers = new Map<number, ReturnType<typeof serveEngine>>();

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    show: false,
    webPreferences: {
      preload: fileURLToPath(new URL('../preload/index.cjs', import.meta.url)),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  const contents = win.webContents;
  const engine = findEngine(app.getAppPath());
  const host = new EngineHost({
    start: () => {
      if (engine === null) {
        throw new Error('no bt-engine: build one (just build) or set BT_ENGINE');
      }
      return NodeTransport.start(engine);
    },
  });
  const serve = (): ReturnType<typeof serveEngine> =>
    serveEngine(host, (message) => {
      if (!contents.isDestroyed()) {
        contents.send('bt:message', message);
      }
    });
  servers.set(contents.id, serve());
  contents.on('did-start-navigation', (details) => {
    if (details.isMainFrame && !details.isSameDocument) {
      servers.get(contents.id)?.dispose();
      servers.set(contents.id, serve());
    }
  });
  const id = contents.id;
  win.on('closed', () => {
    servers.get(id)?.dispose();
    servers.delete(id);
    void host.stop();
  });
  win.once('ready-to-show', () => {
    win.show();
  });
  host.start().catch((e: unknown) => {
    console.error(`bt-engine did not start: ${String(e)}`);
  });
  const devUrl = process.env.ELECTRON_RENDERER_URL;
  if (devUrl !== undefined) {
    // in development the renderer's warnings and errors show in the terminal
    contents.on('console-message', (event) => {
      if (event.level === 'warning' || event.level === 'error') {
        console.log(`renderer ${event.level}: ${event.message}`);
      }
    });
    void win.loadURL(devUrl);
  } else {
    void win.loadFile(fileURLToPath(new URL('../renderer/index.html', import.meta.url)));
  }
}

ipcMain.on('bt:call', (event, call: unknown) => {
  servers.get(event.sender.id)?.receive(call);
});

void app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
